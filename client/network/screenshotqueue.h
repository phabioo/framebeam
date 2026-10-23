#pragma once
// ScreenshotQueue: the screenshots taken in a game that are not on the server yet. A capture is first written to
// <dir>/<uuid>.png with a small <uuid>.json (game_id, run_id, taken_at, hub_id, user_id) and only then uploaded; both files are
// deleted after the server answered 201 and never before. An upload that fails because nobody answered (offline, server error,
// signed out) stays and is tried again by retryAll(); the server refusing the image for good (400 invalid_image, 413
// payload_too_large) drops it with a logged reason. Files of another server or person are left alone (local data is separated
// by server ID and user ID).
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "screenshotapi.h"

namespace uzume {

class ScreenshotQueue : public QObject {
  Q_OBJECT
 public:
  ScreenshotQueue(ScreenshotApi* api, const QString& dir, QObject* parent = nullptr);

  // Writes the PNG and its sidecar; returns the id of the entry, empty (and `error` set) when the files could not be written.
  QString enqueue(const QByteArray& png, const QString& gameId, const QString& runId, const QString& hubId, const QString& userId,
                  QString* error = nullptr);
  // Uploads one entry now (no-op while it is already on its way).
  void upload(const QString& id);
  // Tries every waiting entry of this server and person, oldest first, one after the other; stops at the first transient
  // failure (the server is not there: the next connection retries).
  void retryAll(const QString& hubId, const QString& userId);
  // Entries waiting for this server and person.
  int count(const QString& hubId, const QString& userId) const;
  QStringList ids(const QString& hubId, const QString& userId) const;
  QString dir() const { return dir_; }

 signals:
  // `info` is the server's record. The local files are gone.
  void uploaded(const QString& id, const uzume::ScreenshotInfo& info);
  // Not uploaded, the entry stays: errorCode "unreachable" | "not_connected" | server code.
  void failed(const QString& id, const QString& errorCode);
  // The server refused the image for good; the entry was removed. `reason` is the logged text.
  void dropped(const QString& id, const QString& reason);

 private:
  struct Entry {
    QString id;
    QString gameId;
    QString runId;
    QString takenAt;
    QString hubId;
    QString userId;
  };
  QList<Entry> entries(const QString& hubId, const QString& userId) const;
  void remove(const QString& id);
  void pump();

  ScreenshotApi* api_;
  QString dir_;
  QSet<QString> inflight_;
  QStringList retry_;
  bool retrying_ = false;
  QString retryHub_;
  QString retryUser_;
};

}  // namespace uzume
