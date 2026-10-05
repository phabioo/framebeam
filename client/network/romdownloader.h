#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <memory>

#include "hubconnection.h"
#include "hubprotocol.h"
#include "romcache.h"

namespace framebeam {

enum class RomState {
  Ready,           // validated in the cache (localPath)
  DownloadNeeded,  // totalBytes; receivedBytes > 0 = .part present (resume)
  Validating,      // SHA-256 check in progress (off-thread): cache file without a valid sidecar or .part resume
  Downloading,     // progress received/total
  HashMismatch,    // download does not match the expected SHA-256; .part deleted
  Failed           // network/hub error; .part is kept for resuming
};

struct RomStatus {
  RomState state = RomState::DownloadNeeded;
  qint64 totalBytes = 0;
  qint64 receivedBytes = 0;
  QString localPath;
  QString errorCode;
  QString errorMessage;
};

// ROM acquisition for game start: cache hit only after validation, otherwise download into .part
// (range resume), SHA-256 check, then atomic rename.
class RomDownloader : public QObject {
  Q_OBJECT
 public:
  RomDownloader(HubConnection* connection, RomCache* cache, QObject* parent = nullptr);
  ~RomDownloader() override;

  // Never blocks on hashing: without a valid sidecar the state is Validating, the result arrives via statusChanged.
  RomStatus status(const GameEntry& game);
  void ensureRom(const GameEntry& game);    // hit -> romReady, otherwise download (resumes .part)
  void cancel(const QString& sha256);       // aborts, .part is kept

 signals:
  void statusChanged(const QString& sha256, const framebeam::RomStatus& status);
  void progress(const QString& sha256, qint64 received, qint64 total);
  void romReady(const QString& sha256, const QString& path);

 private:
  struct Job {
    GameEntry game;
    QString ext;
    QPointer<QNetworkReply> reply;
    QFile file;
    qint64 received = 0;
    bool opened = false;
    bool aborted = false;
    bool retriedFull = false;
    bool priming = false;
    std::shared_ptr<QCryptographicHash> hash;  // incrementally over the entire content
    std::unique_ptr<QFutureWatcher<bool>> primeWatcher;
  };
  struct Validation {
    GameEntry game;
    QString ext;
    bool thenDownload = false;
    std::unique_ptr<QFutureWatcher<QPair<bool, QString>>> watcher;
  };

  void startValidation(const GameEntry& game, bool thenDownload);
  void beginDownload(const GameEntry& game);
  void sendRequest(Job* job);

  void startRequest(Job* job);
  void onReadyRead(Job* job);
  void onFinished(Job* job);
  void finishDownload(Job* job);
  void finishJob(const QString& sha, const RomStatus& st);

  QPointer<HubConnection> conn_;
  RomCache* cache_;
  QHash<QString, std::shared_ptr<Job>> jobs_;
  QHash<QString, std::shared_ptr<Validation>> validations_;
  QHash<QString, RomStatus> sticky_;  // HashMismatch/Failed until the next ensureRom
};

}  // namespace framebeam
