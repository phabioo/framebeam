#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

struct UploadResult {
  enum class Kind {
    Created,    // 201: game created (`game`)
    Duplicate,  // 409: ROM already in the library (`existingGameId`)
    Forbidden,  // 403: uploads are not allowed for this user (uploads_disabled)
    TooLarge,   // 413
    Rejected,   // 400: the Hub rejected the file (unknown type, invalid name, ...)
    Failed      // network error, not connected, unreadable file, ...
  };
  Kind kind = Kind::Failed;
  GameEntry game;
  QString existingGameId;
  QString fileName;
  QString errorCode;
  QString errorMessage;
  int status = 0;
};

// ROM upload, one at a time; the file is never read into memory as a whole.
//  - Hub with `local_roms_v1`: resumable chunked upload (POST /uploads, PUT chunks with Content-Range, GET offset to
//    resume). The upload id is persisted per SHA-256 (stateFile), so an interrupted upload resumes after a restart.
//  - older Hub: the one-shot streamed POST /games.
class GameUploader : public QObject {
  Q_OBJECT
 public:
  explicit GameUploader(HubConnection* connection, QObject* parent = nullptr);

  bool busy() const { return reply_ != nullptr || active_; }
  // Starts the upload. The result always arrives via finished() (also for immediate errors, queued).
  // False if an upload is already running.
  // `sha256` (optional) skips hashing when the caller knows it (local ROM index); `system` is passed to the Hub as hint.
  bool upload(const QString& filePath, const QString& title = QString(), const QString& sha256 = QString(),
              const QString& system = QString());
  void cancel();
  // Where upload ids are persisted (JSON file, sha256 -> upload id). Called when an upload starts, so it can follow
  // the active Hub. Without a provider nothing is persisted.
  void setStateFileProvider(std::function<QString()> provider) { stateFile_ = std::move(provider); }
  static constexpr qint64 kDefaultChunk = 8ll * 1024 * 1024;
  static constexpr qint64 kMaxChunk = 64ll * 1024 * 1024;
  bool resumable() const;  // the connected Hub offers local_roms_v1

 signals:
  void progress(qint64 sent, qint64 total);
  void finished(const framebeam::UploadResult& result);

 private:
  void failLater(const QString& fileName, const QString& code, const QString& message);
  bool uploadLegacy(const QString& filePath, const QString& title);
  // resumable
  void onHashed(const QString& sha);
  void openSession(bool tryStored);
  void postSession();
  void sendChunk();
  void resyncOffset();
  void finishWith(UploadResult r);
  void handleError(const HttpResult& r, UploadResult& res);
  QString storedId() const;
  void storeId(const QString& id) const;
  void forgetId() const;

  QPointer<HubConnection> conn_;
  QPointer<QNetworkReply> reply_;
  bool cancelled_ = false;
  QString fileName_;

  std::function<QString()> stateFile_;
  bool active_ = false;  // resumable upload in progress (including hashing)
  QString path_, title_, system_, sha_, uploadId_;
  qint64 size_ = 0, offset_ = 0, chunk_ = kDefaultChunk;
  int resyncs_ = 0;
  bool reposted_ = false;
  std::shared_ptr<std::atomic_bool> hashCancel_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::UploadResult)
