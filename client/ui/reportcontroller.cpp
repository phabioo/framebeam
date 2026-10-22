#include "reportcontroller.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

#include "timeformat.h"

namespace uzume::ui {

namespace {
constexpr qint64 kMiB = 1024 * 1024;

QString localPathOf(const QString& pathOrUrl) {
  const QUrl u(pathOrUrl);
  return u.isLocalFile() ? u.toLocalFile() : pathOrUrl;
}
}  // namespace

ReportController::ReportController(const Deps& deps, QObject* parent) : QObject(parent), d_(deps), uploader_(deps.conn, this) {
  connect(&uploader_, &ReportUploader::stateChanged, this, [this]() { onUploaderState(); });
  connect(&uploader_, &ReportUploader::progress, this, [this]() { emit progressChanged(); });
}

ReportController::~ReportController() = default;

QString ReportController::sizeText(qint64 bytes) const {
  if (bytes >= kMiB) return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / kMiB, 0, 'f', 1);
  return QStringLiteral("%1 KB").arg(std::max<qint64>(1, (bytes + 1023) / 1024));
}

QString ReportController::state() const {
  if (!ready_) return QStringLiteral("preparing");
  switch (uploader_.state()) {
    case ReportUploader::State::Sending: return QStringLiteral("sending");
    case ReportUploader::State::Sent: return QStringLiteral("sent");
    case ReportUploader::State::Offline: return QStringLiteral("offline");
    case ReportUploader::State::Failed: return QStringLiteral("failed");
    case ReportUploader::State::Idle: break;
  }
  return QStringLiteral("editing");
}

QString ReportController::hubName() const {
  if (d_.conn) {
    if (const auto p = d_.conn->profile(); p && !p->name.isEmpty()) return p->name;
    if (!d_.conn->hubInfo().name.isEmpty()) return d_.conn->hubInfo().name;
  }
  return tr("your Hub");
}

QString ReportController::linkedText() const {
  if (linkedTitle_.isEmpty()) return {};
  QString t = tr("Linked: “%1”").arg(linkedTitle_);
  if (linkedTime_.isValid()) t += QStringLiteral(" · ") + TimeFormat::when(linkedTime_);
  return t;
}

QVariantList ReportController::attachments() const {
  QVariantList out;
  if (!ready_) return out;
  for (const ProblemReport::Attachment& a : report_.attachments()) {
    if (!a.available) continue;
    QString label = a.label, detail = a.detail;
    if (a.id == QLatin1String("logs")) {
      label = tr("Logs of the last 3 days");
    } else if (a.id == QLatin1String("settings")) {
      label = tr("Settings");
      detail = tr("no passwords, no Hub token");
    } else if (a.id == QLatin1String("connection")) {
      label = a.time.isValid() ? tr("Connection check from %1").arg(TimeFormat::when(a.time)) : tr("Connection check");
    } else if (a.id == QLatin1String("system")) {
      label = tr("System info");
      detail = tr("Windows version, GPU, driver");
    }
    out.append(QVariantMap{{QStringLiteral("id"), a.id},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("detail"), detail},
                           {QStringLiteral("fileName"), a.fileName},
                           {QStringLiteral("sizeText"), sizeText(a.size())},
                           {QStringLiteral("checked"), a.included}});
  }
  return out;
}

QString ReportController::totalText() const { return ready_ ? sizeText(report_.includedBytes()) : QStringLiteral("…"); }

QStringList ReportController::includedFiles() const { return ready_ ? report_.includedFileNames() : QStringList(); }

bool ReportController::canSend() const {
  if (!open_ || !ready_) return false;
  const auto s = uploader_.state();
  if (s != ReportUploader::State::Idle && s != ReportUploader::State::Failed) return false;
  return !description_.trimmed().isEmpty() && description_.size() <= ProblemReport::kMaxDescription && !report_.tooLarge();
}

QString ReportController::blockedText() const {
  if (ready_ && report_.tooLarge()) return tr("The attachments are larger than 25 MB. Uncheck the logs to send the report.");
  return {};
}

double ReportController::progress() const {
  const qint64 total = uploader_.bytesTotal();
  return total > 0 ? std::clamp(static_cast<double>(uploader_.bytesSent()) / static_cast<double>(total), 0.0, 1.0) : 0.0;
}

QString ReportController::progressText() const {
  const qint64 total = uploader_.bytesTotal();
  if (total >= kMiB) {
    return tr("%1 of %2 MB").arg(static_cast<double>(uploader_.bytesSent()) / kMiB, 0, 'f', 1).arg(static_cast<double>(total) / kMiB, 0, 'f', 1);
  }
  return tr("%1 of %2").arg(sizeText(uploader_.bytesSent()), sizeText(total));
}

void ReportController::setDescription(const QString& text) {
  if (text == description_) return;
  description_ = text;
  emit descriptionChanged();
  emit changed();  // canSend
}

void ReportController::show() { showLinked(QString(), 0); }

void ReportController::showLinked(const QString& title, qint64 whenMsecs) {
  if (open_) return;
  open_ = true;
  ready_ = false;
  description_.clear();
  saveNote_.clear();
  linkedTitle_ = title.simplified();
  linkedTime_ = whenMsecs > 0 ? QDateTime::fromMSecsSinceEpoch(whenMsecs) : QDateTime();
  uploader_.reset();
  sentNoted_ = false;
  emit descriptionChanged();
  emit changed();
  collect();
}

void ReportController::collect() {
  const int gen = ++generation_;
  ProblemReport::Sources s;
  s.dataDir = d_.dataDir;
  s.systemInfo = d_.systemInfo ? d_.systemInfo() : QJsonObject();
  auto* watcher = new QFutureWatcher<ProblemReport>(this);
  connect(watcher, &QFutureWatcher<ProblemReport>::finished, this, [this, watcher, gen]() {
    ProblemReport r = watcher->result();
    watcher->deleteLater();
    if (gen != generation_ || !open_) return;  // closed meanwhile
    report_ = std::move(r);
    ready_ = true;
    applyPending();
    emit changed();
  });
  watcher->setFuture(QtConcurrent::run([s]() { return ProblemReport::collect(s); }));
}

void ReportController::applyPending() {
  report_.setLinkedEvent(linkedTitle_, linkedTime_);
  if (!checkJson_.isEmpty()) {
    report_.setConnectionCheckJson(checkJson_, checkTime_);
  } else if (d_.lastCheck) {
    const Deps::CheckInfo c = d_.lastCheck();
    if (!c.json.isEmpty() && c.at.isValid()) {
      report_.setConnectionCheckJson(c.json, c.at);
      report_.setIncluded(QStringLiteral("connection"), c.marked);
    }
  }
}

void ReportController::setConnectionCheckJson(const QByteArray& json, const QDateTime& when) {
  checkJson_ = json;
  checkTime_ = when;
  if (ready_) {
    report_.setConnectionCheckJson(json, when);
    emit changed();
  }
}

void ReportController::setChecked(const QString& id, bool checked) {
  if (!ready_ || uploader_.state() == ReportUploader::State::Sending || uploader_.state() == ReportUploader::State::Offline) return;
  if (report_.setIncluded(id, checked)) emit changed();
}

void ReportController::send() {
  if (!canSend()) return;
  report_.setDescription(description_);
  QByteArray contentType;
  const QByteArray body = report_.multipartBody(&contentType);
  saveNote_.clear();
  uploader_.send(body, contentType);
}

void ReportController::cancelSending() {
  uploader_.cancel();
  emit changed();
}

void ReportController::close() {
  if (!open_) return;
  ++generation_;  // a running collect() is dropped
  uploader_.reset();
  open_ = false;
  ready_ = false;
  description_.clear();
  saveNote_.clear();
  report_ = ProblemReport();
  emit descriptionChanged();
  emit changed();
}

void ReportController::onUploaderState() {
  if (uploader_.state() == ReportUploader::State::Sent && !sentNoted_) {
    sentNoted_ = true;
    if (d_.sent) d_.sent();
  }
  emit progressChanged();
  emit changed();
}

QString ReportController::suggestedFileName() const { return ProblemReport::zipFileName(QDateTime::currentDateTimeUtc()); }

QString ReportController::folderUrl() const {
  for (const auto loc : {QStandardPaths::DesktopLocation, QStandardPaths::DownloadLocation, QStandardPaths::HomeLocation}) {
    const QString dir = QStandardPaths::writableLocation(loc);
    if (!dir.isEmpty() && QFileInfo(dir).isDir()) return QUrl::fromLocalFile(dir).toString();
  }
  return {};
}

void ReportController::saveAsFile(const QString& pathOrUrl) {
  const QString out = localPathOf(pathOrUrl);
  if (!ready_ || saving_ || out.isEmpty()) return;
  saving_ = true;
  ProblemReport copy = report_;
  copy.setDescription(description_);
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, out]() {
    const QString err = watcher->result();
    watcher->deleteLater();
    saving_ = false;
    saveNote_ = err.isEmpty() ? tr("Saved to %1").arg(QDir::toNativeSeparators(out)) : tr("Could not save the file: %1").arg(err);
    emit saveFinished(err.isEmpty(), out, err);
    emit changed();
  });
  watcher->setFuture(QtConcurrent::run([copy, out]() {
    QString err;
    return copy.saveAsZip(out, &err) ? QString() : (err.isEmpty() ? QStringLiteral("unknown error") : err);
  }));
}

}  // namespace uzume::ui
