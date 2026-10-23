#include "screenshotqueue.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>

namespace uzume {

ScreenshotQueue::ScreenshotQueue(ScreenshotApi* api, const QString& dir, QObject* parent) : QObject(parent), api_(api), dir_(dir) {}

namespace {
bool writeFile(const QString& path, const QByteArray& data) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  if (f.write(data) != data.size()) return false;
  return f.commit();
}
}  // namespace

QString ScreenshotQueue::enqueue(const QByteArray& png, const QString& gameId, const QString& runId, const QString& hubId,
                                 const QString& userId, QString* error) {
  const auto fail = [&](const QString& what) {
    if (error != nullptr) *error = what;
    return QString();
  };
  if (!QDir().mkpath(dir_)) return fail(QStringLiteral("The folder for pending screenshots could not be created."));
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString base = QDir(dir_).filePath(id);
  if (!writeFile(base + QStringLiteral(".png"), png)) return fail(QStringLiteral("The screenshot could not be written."));
  const QJsonObject meta{{QStringLiteral("game_id"), gameId},
                         {QStringLiteral("run_id"), runId},
                         {QStringLiteral("taken_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                         {QStringLiteral("hub_id"), hubId},
                         {QStringLiteral("user_id"), userId}};
  if (!writeFile(base + QStringLiteral(".json"), QJsonDocument(meta).toJson(QJsonDocument::Compact))) {
    QFile::remove(base + QStringLiteral(".png"));
    return fail(QStringLiteral("The screenshot could not be written."));
  }
  return id;
}

QList<ScreenshotQueue::Entry> ScreenshotQueue::entries(const QString& hubId, const QString& userId) const {
  QList<Entry> out;
  const QDir d(dir_);
  for (const QString& name : d.entryList({QStringLiteral("*.json")}, QDir::Files)) {
    QFile f(d.filePath(name));
    if (!f.open(QIODevice::ReadOnly)) continue;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    Entry e;
    e.id = name.chopped(5);
    e.gameId = o.value(QStringLiteral("game_id")).toString();
    e.runId = o.value(QStringLiteral("run_id")).toString();
    e.takenAt = o.value(QStringLiteral("taken_at")).toString();
    e.hubId = o.value(QStringLiteral("hub_id")).toString();
    e.userId = o.value(QStringLiteral("user_id")).toString();
    if (e.gameId.isEmpty() || e.hubId != hubId || e.userId != userId) continue;
    if (!QFile::exists(d.filePath(e.id + QStringLiteral(".png")))) continue;
    out.append(e);
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.takenAt < b.takenAt; });
  return out;
}

int ScreenshotQueue::count(const QString& hubId, const QString& userId) const { return static_cast<int>(entries(hubId, userId).size()); }

QStringList ScreenshotQueue::ids(const QString& hubId, const QString& userId) const {
  QStringList out;
  for (const Entry& e : entries(hubId, userId)) out.append(e.id);
  return out;
}

void ScreenshotQueue::remove(const QString& id) {
  QFile::remove(QDir(dir_).filePath(id + QStringLiteral(".json")));
  QFile::remove(QDir(dir_).filePath(id + QStringLiteral(".png")));
}

void ScreenshotQueue::upload(const QString& id) {
  if (inflight_.contains(id)) return;
  const QDir d(dir_);
  QFile meta(d.filePath(id + QStringLiteral(".json")));
  QFile png(d.filePath(id + QStringLiteral(".png")));
  if (!meta.open(QIODevice::ReadOnly) || !png.open(QIODevice::ReadOnly)) return;
  const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
  const QString gameId = o.value(QStringLiteral("game_id")).toString();
  const QString runId = o.value(QStringLiteral("run_id")).toString();
  const QByteArray data = png.readAll();
  if (gameId.isEmpty() || data.isEmpty()) return;
  inflight_.insert(id);
  api_->upload(gameId, runId, data, [this, id](const ScreenshotResult& r) {
    inflight_.remove(id);
    if (r.ok) {
      remove(id);
      emit uploaded(id, r.screenshot);
    } else if (r.rejected()) {
      const QString reason = QStringLiteral("HTTP %1 %2").arg(r.status).arg(r.errorCode);
      qWarning().noquote() << "Screenshot" << id << "dropped: the server refused it (" << reason << ")";
      remove(id);
      emit dropped(id, reason);
    } else {
      emit failed(id, r.errorCode);
    }
  });
}

void ScreenshotQueue::retryAll(const QString& hubId, const QString& userId) {
  if (retrying_) return;
  retry_ = ids(hubId, userId);
  retry_.removeIf([&](const QString& id) { return inflight_.contains(id); });
  if (retry_.isEmpty()) return;
  retrying_ = true;
  retryHub_ = hubId;
  retryUser_ = userId;
  pump();
}

void ScreenshotQueue::pump() {
  if (retry_.isEmpty()) {
    retrying_ = false;
    return;
  }
  const QString id = retry_.takeFirst();
  // One at a time: the next entry follows the answer for this one; a transient failure ends the round.
  auto* once = new QObject(this);
  connect(this, &ScreenshotQueue::uploaded, once, [this, once, id](const QString& done) {
    if (done != id) return;
    once->deleteLater();
    pump();
  });
  connect(this, &ScreenshotQueue::dropped, once, [this, once, id](const QString& done) {
    if (done != id) return;
    once->deleteLater();
    pump();
  });
  connect(this, &ScreenshotQueue::failed, once, [this, once, id](const QString& done) {
    if (done != id) return;
    once->deleteLater();
    retry_.clear();
    retrying_ = false;
  });
  if (inflight_.contains(id)) {  // a fresh capture of this entry is on its way: its answer ends this step too
    return;
  }
  upload(id);
  if (!inflight_.contains(id)) {  // the files vanished: nothing will answer
    once->deleteLater();
    pump();
  }
}

}  // namespace uzume
