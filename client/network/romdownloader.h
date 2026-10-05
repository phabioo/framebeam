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
  Ready,           // validiert im Cache (localPath)
  DownloadNeeded,  // totalBytes; receivedBytes > 0 = .part vorhanden (Fortsetzung)
  Validating,      // SHA-256-Pruefung laeuft (off-thread): Cache-Datei ohne gueltiges Sidecar bzw. .part-Fortsetzung
  Downloading,     // Fortschritt received/total
  HashMismatch,    // Download passt nicht zum erwarteten SHA-256; .part geloescht
  Failed           // Netzwerk-/Hub-Fehler; .part bleibt fuer die Fortsetzung
};

struct RomStatus {
  RomState state = RomState::DownloadNeeded;
  qint64 totalBytes = 0;
  qint64 receivedBytes = 0;
  QString localPath;
  QString errorCode;
  QString errorMessage;
};

// ROM-Beschaffung fuer den Spielstart: Cache-Treffer nur nach Validierung, sonst Download in .part
// (Range-Fortsetzung), SHA-256-Pruefung, dann atomares Umbenennen.
class RomDownloader : public QObject {
  Q_OBJECT
 public:
  RomDownloader(HubConnection* connection, RomCache* cache, QObject* parent = nullptr);
  ~RomDownloader() override;

  // Blockiert nie auf Hashing: ohne gueltiges Sidecar Zustand Validating, das Ergebnis kommt per statusChanged.
  RomStatus status(const GameEntry& game);
  void ensureRom(const GameEntry& game);    // Treffer -> romReady, sonst Download (setzt .part fort)
  void cancel(const QString& sha256);       // bricht ab, .part bleibt

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
    std::shared_ptr<QCryptographicHash> hash;  // inkrementell ueber den gesamten Inhalt
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
  QHash<QString, RomStatus> sticky_;  // HashMismatch/Failed bis zum naechsten ensureRom
};

}  // namespace framebeam
