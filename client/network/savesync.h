#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <optional>

#include "profilestore.h"
#include "saveapi.h"
#include "savestore.h"

namespace framebeam {

// Save sync state machine of the Player (docs: ADR 0005 / D4). One active game at a time.
// Flow: prepareStart() -> startReady() | startConflict() -> (resolveConflict() -> startReady()) ->
// beginSession() (auto checkpoints) -> finalSync() on pause/stop/exit.
// Local data only below hubs/<hub_id>/users/<user_id>/; uploads and retries only to the Hub of that hub_id.
class SaveSync : public QObject {
  Q_OBJECT
 public:
  enum class Kind { None, Synced, Pending, Conflict };
  Q_ENUM(Kind)
  enum class Resolution { UseHub, UseLocal, DecideLater };
  Q_ENUM(Resolution)

  struct Timing {
    int pollMs = 2000;           // file watcher
    int debounceMs = 12000;      // quiet time after the last change
    int minIntervalMs = 60000;   // minimum distance between two checkpoint uploads
    int retryBaseMs = 5000;      // backoff after a failed upload (doubles)
    int retryMaxMs = 300000;
    int finalTimeoutMs = 10000;  // wait at app exit
  };

  // Both sides of a conflict for the dialog.
  struct ConflictView {
    SaveConflictInfo conflict;
    QString gameId;
    QString localDeviceName;
    QDateTime localModified;
    QString localSha256;
    int localBaseRevision = 0;
  };

  SaveSync(HubConnection* connection, ProfileStore* profiles, QObject* parent = nullptr);

  static QString unsupportedNote() { return QObject::tr("Hub does not support save sync"); }
  static QString kindName(Kind k);

  void setTiming(const Timing& t);
  const Timing& timing() const { return timing_; }

  // Connected, Hub advertises saves_v1 and the Hub user ID is known.
  bool available() const;
  bool hubSupportsSaves() const;
  QString note() const;  // UI note when sync is unavailable (empty if available or not connected)

  // Library state of a game (cached from sync.json; call refreshKinds after the library loaded).
  Kind kind(const QString& gameId) const { return kinds_.value(gameId, Kind::None); }
  void refreshKinds(const QStringList& gameIds);

  // Start sync before the core loads. The core's save dir comes with startReady().
  void prepareStart(const QString& gameId, const QString& romPath, const QStringList& romBasenames);
  void resolveConflict(Resolution r);
  bool hasPendingConflictDialog() const { return dialogOpen_; }

  // While the game runs.
  void beginSession();
  // Immediate upload of a changed save (pause: sessionEnd=false; stop/exit: true, ends the session after the upload).
  void finalSync(bool sessionEnd);
  // For app exit: waits up to timing().finalTimeoutMs; remaining changes stay pending. True if clean or uploaded.
  bool finalSyncBlocking(bool sessionEnd);
  bool sessionActive() const { return session_; }
  QString activeGameId() const { return a_.gameId; }
  QString activeSaveFile() const { return a_.file; }
  QString activeGameDir() const { return a_.dir; }

  // Retries all games with pending changes of the current hub/user (not the active game).
  void retryPending();

 signals:
  void startReady(const QString& gameId, const QString& saveDir, const QString& note);
  void startConflict(const framebeam::SaveSync::ConflictView& view);
  void resolveFailed(const QString& message);  // dialog stays open
  void startFailed(const QString& gameId, const QString& message);
  void kindChanged(const QString& gameId, framebeam::SaveSync::Kind kind);
  void finalSyncFinished(const QString& gameId, bool ok);
  void uploaded(const QString& gameId, int revision);  // after every accepted upload (tests/diagnostics)

 private:
  enum class UploadOutcome { Clean, Uploaded, Conflict, Offline, Failed };
  struct Active {
    QString gameId, hubId, userId, dir, file, expectedName, romPath;
    SyncState st;
    std::optional<SaveConflictInfo> conflict;
    QString lastHash;      // hash of the file at the last observation
    qint64 lastSize = -1;
    QDateTime lastMtime;
    bool dirty = false;
    bool uploading = false;
    bool pausedByConflict = false;
    int backoffMs = 0;
    QElapsedTimer sinceChange, sinceUpload, sinceFail;
    bool uploadedOnce = false, failedOnce = false;
    int finalRequested = 0;  // 0 none, 1 final, 2 final_session_end
  };

  bool sameHub(const QString& hubId) const;
  QString noteForOffline() const;
  void setKind(const QString& gameId, const SyncState& st, bool hasFile);
  void persist();
  void readyToPlay(const QString& note);
  void startSync();  // (re)run reconciliation with the Hub
  void onSlot(quint64 gen, const SaveApiResult& r);
  void onHubSlot(quint64 gen, const SaveSlotInfo& slot);
  void downloadAndApply(quint64 gen, bool backupLocal, const QString& note);
  void uploadAtStart(quint64 gen, const QString& sha);
  void showConflict(const SaveConflictInfo& c);
  void poll();
  void pump();
  void uploadCurrent(const QString& reason, std::function<void(UploadOutcome)> done);
  void finishFinal(bool ok);
  void scheduleRetry();
  void retryNext(QStringList dirs);

  HubConnection* conn_;
  ProfileStore* profiles_;
  SaveApi api_;
  Timing timing_;
  QHash<QString, Kind> kinds_;
  Active a_;
  quint64 gen_ = 0;
  bool dialogOpen_ = false;
  bool session_ = false;
  QTimer pollTimer_;
  QTimer retryTimer_;
  int retryBackoffMs_ = 0;
  bool retryRunning_ = false;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::SaveSync::ConflictView)
Q_DECLARE_METATYPE(framebeam::SaveSync::Kind)
