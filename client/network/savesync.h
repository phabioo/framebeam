#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <optional>

#include "playersettings.h"
#include "profilestore.h"
#include "saveapi.h"
#include "savestore.h"

namespace framebeam {

struct SaveRestoreResult {
  enum class Outcome { Ok, Blocked, Stale, NotFound, Offline, Failed };
  Outcome kind = Outcome::Failed;
  QString message;  // user-facing; on Ok only a warning (e.g. the local file was not updated), otherwise empty
  int revision = 0;
  bool localUpdated = false;  // the local save file of the slot was replaced by the new checkpoint
  bool ok() const { return kind == Outcome::Ok; }
};
struct SaveSnapshotResult {
  enum class Outcome { Ok, Blocked, NotFound, Offline, Failed };
  Outcome kind = Outcome::Failed;
  QString message;
  SaveHistoryVersion version;
  bool ok() const { return kind == Outcome::Ok; }
};

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

  using RestoreResult = SaveRestoreResult;
  using SnapshotResult = SaveSnapshotResult;
  using RestoreCallback = std::function<void(const RestoreResult&)>;
  using SnapshotCallback = std::function<void(const SnapshotResult&)>;

  // Slot choice per game and Hub profile (ADR 0012 D7), kept in the Player settings (without settings: in memory).
  void setSettings(PlayerSettings* settings) { settings_ = settings; }
  QString slotFor(const QString& gameId) const;
  bool setSlot(const QString& gameId, const QString& slot);  // false: invalid name; refreshes the library state of the game
  QString slotDirFor(const QString& gameId, const QString& slot) const;  // empty without Hub/user or for an invalid slot
  SaveApi* api() { return &api_; }
  // Hub advertises saves_v2 (restore, snapshots, save_updated).
  bool hubSupportsSavesV2() const;
  // Hub advertises saves_v3 (deleting manual snapshots).
  bool hubSupportsSavesV3() const;
  // Hub advertises saves_v4 (upload of a local save file).
  bool hubSupportsSavesV4() const;

  // The game runs (or its Session just ended) with this slot on this Player.
  bool isRunning(const QString& gameId, const QString& slot = QString()) const {
    return session_ && a_.gameId == gameId && (slot.isEmpty() || a_.slot == slot);
  }
  QString activeSlot() const { return a_.slot; }
  // Pending upload, open conflict or unsynced local changes in this slot (restore is blocked then). Empty = none.
  QString pendingReason(const QString& gameId, const QString& slot) const;

  // Restore a history version: refused while the game runs here or while the slot has pending changes. Afterwards the
  // local save of the slot becomes the new checkpoint (download); `localFileName` names the file if none exists yet.
  void restoreVersion(const QString& gameId, const QString& slot, int version, int expectedRevision, const QString& localFileName,
                      RestoreCallback cb);
  // saves_v4: make a local file the Hub's current save of the slot (and the local save). Same block rules as restore;
  // the file must be non-empty and at most kMaxSaveBytes. `expectedRevision` 0 = no save on the Hub yet.
  void uploadSaveFile(const QString& gameId, const QString& slot, const QString& filePath, int expectedRevision,
                      const QString& localFileName, RestoreCallback cb);
  // Snapshot of the Hub's current checkpoint (game not running here).
  void createSnapshot(const QString& gameId, const QString& slot, const QString& label, SnapshotCallback cb);
  // In game: first uploads a changed save through the final-sync path, then creates the snapshot.
  void snapshotActive(const QString& label, SnapshotCallback cb);
  // WSS save_updated from the Hub (other device changed a checkpoint).
  void handleSaveUpdate(const SaveUpdate& update);

  static QString unsupportedNote() { return QObject::tr("Hub does not support save sync"); }
  static QString kindName(Kind k);

  // Test hook: replaces the local backup before overwriting (returns the backup path, empty = failed).
  void setBackupHook(std::function<QString(const QString&, const QString&)> hook) { backupHook_ = std::move(hook); }
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
  // save_updated from another device; runningHere = this game runs on this Player with that slot (next upload conflicts).
  void saveChangedElsewhere(const framebeam::SaveUpdate& update, bool runningHere);

 private:
  enum class UploadOutcome { Clean, Uploaded, Conflict, Offline, Failed };
  struct Active {
    QString gameId, slot, hubId, userId, dir, file, expectedName, romPath;
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
  struct RetryItem {
    QString gameId, slot, dir;
  };
  void retryNext(QList<RetryItem> items);

  HubConnection* conn_;
  ProfileStore* profiles_;
  PlayerSettings* settings_ = nullptr;
  QHash<QString, QString> memorySlots_;  // without settings: hub_id/game_id -> slot
  SaveApi api_;
  Timing timing_;
  std::function<QString(const QString&, const QString&)> backupHook_;
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
