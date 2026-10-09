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
  // ---- Live save: the running core loads a restored / uploaded / chosen save (saves view inside the game) ----
  // Hooks set by the Player (GameSession). All block the calling (GUI) thread until the emulation thread did it.
  struct LiveHooks {
    std::function<bool()> ready;                          // a core is loaded and running or paused (optional)
    std::function<void()> flush;                          // the core's battery save is written to the save file
    std::function<bool(qint64)> accepts;                  // the core's battery save memory has exactly this size
    std::function<bool(const QByteArray&)> apply;         // replaces the battery save by reloading the game from the new file
  };
  void setLiveHooks(LiveHooks hooks) { live_ = std::move(hooks); }
  // This game runs here with this slot and the core can take a new save.
  bool liveApplyAvailable(const QString& gameId, const QString& slot) const {
    return isRunning(gameId, slot) && live_.apply && live_.accepts && (!live_.ready || live_.ready());
  }
  // Why restore / upload are off for a running game even though the core could take the save; empty = allowed.
  QString liveBlockReason(const QString& gameId, const QString& slot) const;

  // ---- Save conflict outside the play flow (Library / in-game saves view) ----
  struct SlotConflict {
    enum class Outcome { Found, None, Offline, Failed };
    Outcome kind = Outcome::Failed;
    QString message;
    ConflictView view;        // both sides: view.conflict (Hub), local device name / modification time
    bool localExists = false;
    qint64 localSize = 0;
    qint64 hubSize = 0;       // size of the Hub's current checkpoint
  };
  using ConflictCallback = std::function<void(const SlotConflict&)>;
  // Reads the open conflict of this device in the slot from the Hub (the game does not start).
  void loadSlotConflict(const QString& gameId, const QString& slot, const QString& localFileName, ConflictCallback cb);
  // Resolves it with the same code as the start dialog (UseHub: local backup first; UseLocal: upload as current). For a
  // running game the local save is replaced live (hooks). The game never starts. `localFileName` names the file if none exists.
  void resolveSlotConflict(const QString& gameId, const QString& slot, Resolution r, const QString& localFileName, RestoreCallback cb);

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

  // Core (id + version) a game is about to start with (ADR 0020 D7).
  struct CoreRef {
    QString id;
    QString version;
    bool valid() const { return !id.isEmpty() && !version.isEmpty(); }
  };
  // Start sync before the core loads. The core's save dir comes with startReady().
  // With a valid `core`: when the save dir was last written by another core id or version, a manual snapshot on the Hub
  // ("Before core change: <old id> <old ver> → <new id> <new ver>") is created before the start sync and the change is
  // reported in the startReady() note; a failed snapshot (or an unreachable Hub with a local save) ends in startFailed()
  // and the record stays unchanged. No record yet (first start): the core is only recorded.
  void prepareStart(const QString& gameId, const QString& romPath, const QStringList& romBasenames, const CoreRef& core = {});
  static QString coreChangeLabel(const CoreRef& from, const CoreRef& to);  // at most 64 bytes (Hub limit)
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
    QString coreNote;        // one-time notice about a core change, prepended to the start note
  };

  void continueStart(quint64 gen);
  void guardCoreChange(quint64 gen, const CoreRef& core);
  void recordCore(const CoreRef& core);
  void failStart(quint64 gen, const QString& message);

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
  std::optional<SaveConflictInfo> ownConflict(const SaveSlotInfo& slot, const QString& preferredId) const;
  ConflictView makeConflictView(const SaveConflictInfo& c, const QString& gameId, const QString& file) const;
  // End of resolveConflict(): the play flow starts the game; a resolution from the saves view calls its callback instead.
  void resolvedOk(const QString& note);
  void resolveError(const QString& message, SaveRestoreResult::Outcome kind = SaveRestoreResult::Outcome::Failed);
  bool writeResolvedSave(const QByteArray& data);
  void resyncActiveFile();
  void restoreVersionLive(const QString& gameId, const QString& slot, int version, int expectedRevision, RestoreCallback cb);
  void uploadSaveFileLive(const QString& gameId, const QString& slot, const QString& filePath, int expectedRevision, RestoreCallback cb);
  // Flush, upload a changed save (final-sync path), then `next(ok, revisionToExpect, message)`; liveOp_ stays set on ok.
  void livePrelude(const QString& gameId, const QString& slot, int expectedRevision,
                   std::function<void(bool, int, const QString&)> next);
  RestoreResult applyLiveContent(const QByteArray& data, int newRevision, const QString& backupTag);
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
  LiveHooks live_;
  bool liveOp_ = false;                     // a live restore / upload runs: no checkpoint uploads meanwhile
  RestoreCallback resolveCb_;               // set while a resolution from the saves view runs
  bool resolveLive_ = false;                // ... for the running game (a_ is its session state)
  QTimer pollTimer_;
  QTimer retryTimer_;
  int retryBackoffMs_ = 0;
  bool retryRunning_ = false;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::SaveSync::ConflictView)
Q_DECLARE_METATYPE(framebeam::SaveSync::Kind)
