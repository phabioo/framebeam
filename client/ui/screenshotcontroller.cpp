#include "screenshotcontroller.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace uzume::ui {

namespace {
QString dateOf(const QString& iso, const QString& format) {
  const QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
  return t.isValid() ? t.toLocalTime().toString(format) : QString();
}
}  // namespace

ScreenshotController::ScreenshotController(HubConnection* connection, ProfileStore* profiles, QObject* parent)
    : QObject(parent),
      conn_(connection),
      profiles_(profiles),
      api_(connection, this),
      queue_(&api_, QDir(profiles->baseDir()).filePath(QStringLiteral("screenshots/pending")), this),
      cache_(this) {
  cache_.setFetcher([this](const QString& id, std::function<void(const QByteArray&)> done) {
    api_.content(id, [done = std::move(done)](const ScreenshotResult& r) { done(r.ok ? r.content : QByteArray()); });
  });
  connect(&queue_, &ScreenshotQueue::uploaded, this, [this](const QString& id, const ScreenshotInfo& info) {
    emit pendingChanged();
    if (fresh_.removeAll(id) > 0) emit notice(tr("Screenshot saved"));
    onUploaded(info);
  });
  connect(&queue_, &ScreenshotQueue::failed, this, [this](const QString& id, const QString&) {
    if (fresh_.removeAll(id) > 0) emit notice(tr("Screenshot saved on this PC. It uploads when the server is back."));
  });
  connect(&queue_, &ScreenshotQueue::dropped, this, [this](const QString& id, const QString&) {
    emit pendingChanged();
    if (fresh_.removeAll(id) > 0) emit notice(tr("The server did not accept this screenshot."));
  });
  if (conn_) {
    connect(conn_, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
      if (s != HubConnection::State::Connected) return;
      retryPending();
      if (!gameId_.isEmpty()) refreshList();
    });
    if (conn_->state() == HubConnection::State::Connected) QTimer::singleShot(0, this, [this]() { retryPending(); });
  }
}

void ScreenshotController::identity(QString* hubId, QString* userId) const {
  if (conn_ && conn_->state() == HubConnection::State::Connected) {
    *hubId = conn_->hubId();
    *userId = conn_->hubUserId();
    return;
  }
  hubId->clear();
  userId->clear();
  if (profiles_ != nullptr && !profiles_->lastHubId().isEmpty()) {
    if (const auto p = profiles_->profile(profiles_->lastHubId())) {
      *hubId = p->hubId;
      *userId = p->hubUserId;
    }
  }
}

int ScreenshotController::pendingCount() const {
  QString hub, user;
  identity(&hub, &user);
  return hub.isEmpty() || user.isEmpty() ? 0 : queue_.count(hub, user);
}

void ScreenshotController::begin() {
  if (pending_++ == 0) emit busyChanged();
}

void ScreenshotController::end() {
  if (pending_ > 0 && --pending_ == 0) emit busyChanged();
}

void ScreenshotController::setGameSource(std::function<QString()> gameId, std::function<QString()> runId, FrameSource frames) {
  currentGame_ = std::move(gameId);
  currentRun_ = std::move(runId);
  frames_ = std::move(frames);
}

void ScreenshotController::capture() {
  const QString game = currentGame_ ? currentGame_() : QString();
  if (game.isEmpty() || !frames_) return;
  const QString run = currentRun_ ? currentRun_() : QString();
  frames_([this, game, run](const QImage& frame, const QString& error) {
    if (!error.isEmpty() || frame.isNull()) {
      emit notice(tr("No screenshot: the game has no picture yet."));
      return;
    }
    submit(frame, game, run);
  });
}

void ScreenshotController::submit(const QImage& frame, const QString& gameId, const QString& runId) {
  QByteArray png;
  QBuffer buf(&png);
  buf.open(QIODevice::WriteOnly);
  // The cores' frames are XRGB: an opaque RGB picture, no alpha channel in the file.
  if (!frame.convertToFormat(QImage::Format_RGB32).save(&buf, "PNG") || png.isEmpty()) {
    emit notice(tr("The screenshot could not be saved."));
    return;
  }
  QString hub, user;
  identity(&hub, &user);
  if (hub.isEmpty() || user.isEmpty()) {
    emit notice(tr("The screenshot could not be saved: no server is set up on this PC."));
    return;
  }
  QString error;
  const QString id = queue_.enqueue(png, gameId, runId, hub, user, &error);
  if (id.isEmpty()) {
    emit notice(tr("The screenshot could not be saved on this PC."));
    return;
  }
  emit pendingChanged();
  if (conn_ && conn_->state() == HubConnection::State::Connected) {
    fresh_.append(id);
    queue_.upload(id);  // the notice follows the answer
  } else {
    emit notice(tr("Screenshot saved on this PC. It uploads when the server is back."));
  }
}

void ScreenshotController::retryPending() {
  QString hub, user;
  identity(&hub, &user);
  if (hub.isEmpty() || user.isEmpty()) return;
  queue_.retryAll(hub, user);
}

QVariantMap ScreenshotController::rowFor(const ScreenshotInfo& s) const {
  const QString runName = s.runId.isEmpty() || !runName_ ? QString() : runName_(s.runId);
  return {{QStringLiteral("id"), s.id},
          {QStringLiteral("createdAt"), s.createdAt},
          {QStringLiteral("dateText"), dateOf(s.createdAt, QStringLiteral("dd.MM.yyyy"))},
          {QStringLiteral("whenText"), dateOf(s.createdAt, QStringLiteral("dd.MM.yyyy HH:mm"))},
          {QStringLiteral("runId"), s.runId},
          {QStringLiteral("runName"), runName},
          {QStringLiteral("width"), s.width},
          {QStringLiteral("height"), s.height},
          {QStringLiteral("sizeText"), tr("%1 × %2").arg(s.width).arg(s.height)}};
}

void ScreenshotController::clear() {
  ++generation_;
  pending_ = 0;
  supported_ = false;
  gameId_.clear();
  infos_.clear();
  shots_.clear();
  cache_.clear();
  emit supportedChanged();
  emit shotsChanged();
  emit busyChanged();
}

void ScreenshotController::loadGame(const QString& gameId) {
  if (gameId != gameId_) {
    gameId_ = gameId;
    infos_.clear();
    shots_.clear();
    emit shotsChanged();
  }
  if (!gameId_.isEmpty()) refreshList();
}

void ScreenshotController::refreshList() {
  const quint64 gen = ++generation_;
  const QString game = gameId_;
  api_.listGame(game, [this, gen, game](const ScreenshotResult& r) {
    if (gen != generation_ || game != gameId_) return;
    if (!r.ok) {
      if (r.status == 404) {  // a server before 0.19: no section
        if (supported_) {
          supported_ = false;
          emit supportedChanged();
        }
        infos_.clear();
        shots_.clear();
        emit shotsChanged();
      }
      return;  // no answer: the list on the screen stays as it was
    }
    infos_ = r.screenshots;
    shots_.clear();
    for (const ScreenshotInfo& s : std::as_const(infos_)) shots_.append(rowFor(s));
    if (!supported_) {
      supported_ = true;
      emit supportedChanged();
    }
    emit shotsChanged();
  });
}

void ScreenshotController::onUploaded(const ScreenshotInfo& info) {
  if (info.gameId == gameId_) refreshList();
}

void ScreenshotController::remove(const QString& screenshotId) {
  begin();
  api_.remove(screenshotId, [this, screenshotId](const ScreenshotResult& r) {
    end();
    if (r.ok || r.status == 404) {  // already gone is fine
      cache_.drop(screenshotId);
      refreshList();
      emit finished(QStringLiteral("remove"), true, QString());
      return;
    }
    const bool offline = r.errorCode == QLatin1String("unreachable") || r.errorCode == QLatin1String("not_connected");
    emit finished(QStringLiteral("remove"), false,
                  offline ? tr("The server could not be reached. The screenshot was not deleted.")
                          : (r.errorMessage.isEmpty() ? tr("The server refused to delete the screenshot.") : r.errorMessage));
  });
}

QString ScreenshotController::suggestedFileName(const QString& screenshotId) const {
  for (const ScreenshotInfo& s : infos_) {
    if (s.id == screenshotId) {
      const QDateTime t = QDateTime::fromString(s.createdAt, Qt::ISODate).toLocalTime();
      if (t.isValid()) return QStringLiteral("screenshot-") + t.toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".png");
    }
  }
  return QStringLiteral("screenshot.png");
}

QString ScreenshotController::exportFolderUrl() const {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  return dir.isEmpty() ? QString() : QUrl::fromLocalFile(dir).toString();
}

void ScreenshotController::saveAs(const QString& screenshotId, const QString& target) {
  const QUrl url(target);
  QString path = url.isLocalFile() ? url.toLocalFile() : target;
  if (path.isEmpty()) {
    emit finished(QStringLiteral("save"), false, tr("No file was chosen."));
    return;
  }
  if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) path += QStringLiteral(".png");
  begin();
  cache_.png(screenshotId, [this, path](const QByteArray& png) {
    end();
    if (png.isEmpty()) {
      emit finished(QStringLiteral("save"), false, tr("The screenshot could not be loaded from the server."));
      return;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(png) != png.size() || !f.commit()) {
      emit finished(QStringLiteral("save"), false, tr("The file could not be written."));
      return;
    }
    emit finished(QStringLiteral("save"), true, QString());
  });
}

}  // namespace uzume::ui
