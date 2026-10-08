// Save sync against the fake Hub: start sync cases (D4), conflicts, pending retry, checkpoints, final sync.
// Only dummy files; no real saves or ROMs.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "profilestore.h"
#include "savesync.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
const QString kGame = QStringLiteral("game-1");
const QString kRom = QStringLiteral("/cache/rom1.nds");  // -> save file rom1.sav
const QString kHubId = QStringLiteral("hub-test-1");
const QString kUser = QStringLiteral("u_test_1");

QByteArray readFile(const QString& p) {
  QFile f(p);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
void writeFile(const QString& p, const QByteArray& d) {
  QDir().mkpath(QFileInfo(p).absolutePath());
  QFile f(p);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(d);
}
}  // namespace

class SaveSyncTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<SaveSync> sync_;
  bool noFeature_ = false;

  QString gdir() const { return SaveStore::gameDir(*profiles_, kHubId, kUser, kGame); }
  QString saveFile() const { return gdir() + QStringLiteral("/rom1.sav"); }
  int puts() const {
    int n = 0;
    for (const FakeRequest& r : hub_->requests) {
      n += r.method == "PUT" ? 1 : 0;
    }
    return n;
  }
  QByteArray lastPutReason() const {
    for (int i = hub_->requests.size() - 1; i >= 0; --i) {
      if (hub_->requests.at(i).method == "PUT") {
        return hub_->requests.at(i).headers.value(QStringLiteral("x-framebeam-sync-reason"));
      }
    }
    return {};
  }

  void connectAll() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->path());
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->features = {QStringLiteral("saves_v1"), QStringLiteral("saves_v2")};
    if (noFeature_) {
      hub_->features.clear();
    }
    QVERIFY(hub_->start());
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
    conn_->connectToAddress(hub_->address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
    hub_->decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
    sync_ = std::make_unique<SaveSync>(conn_.get(), profiles_.get());
    SaveSync::Timing t;
    t.pollMs = 20;
    t.debounceMs = 150;
    t.minIntervalMs = 500;
    t.retryBaseMs = 100;
    t.retryMaxMs = 400;
    t.finalTimeoutMs = 2000;
    sync_->setTiming(t);
  }

  // Runs prepareStart and waits for startReady or startConflict. Returns "ready" | "conflict".
  QString start() {
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QSignalSpy conflict(sync_.get(), &SaveSync::startConflict);
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")});
    (void)QTest::qWaitFor([&]() { return ready.count() + conflict.count() > 0; }, 8000);
    return conflict.count() > 0 ? QStringLiteral("conflict") : (ready.count() > 0 ? QStringLiteral("ready") : QString());
  }
  // After a conflict dialog: resolve and wait for ready.
  QString resolve(SaveSync::Resolution r) {
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QSignalSpy conflict(sync_.get(), &SaveSync::startConflict);
    QSignalSpy failed(sync_.get(), &SaveSync::resolveFailed);
    sync_->resolveConflict(r);
    (void)QTest::qWaitFor([&]() { return ready.count() + conflict.count() + failed.count() > 0; }, 8000);
    return failed.count() > 0 ? QStringLiteral("failed") : (conflict.count() > 0 ? QStringLiteral("conflict") : QStringLiteral("ready"));
  }

 private slots:
  void init() {
    noFeature_ = false;
    qRegisterMetaType<framebeam::SaveSync::Kind>();
    connectAll();
  }
  void cleanup() {
    sync_.reset();
    conn_.reset();
    hub_.reset();
    profiles_.reset();
    creds_.reset();
    dir_.reset();
  }

  void noHubSlotNoLocalFileStartsEmpty() {
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(puts(), 0);
    QVERIFY(!QFileInfo::exists(saveFile()));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::None);
  }

  void noHubSlotLocalFileUploadsWithBase0() {
    writeFile(saveFile(), "local-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(puts(), 1);
    QCOMPARE(hub_->saves.value(kGame).revision, 1);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-1"));
    QCOMPARE(hub_->requests.last().headers.value(QStringLiteral("x-framebeam-base-revision")), QByteArray("0"));
    const SyncState st = SaveStore::loadState(gdir());
    QCOMPARE(st.baseRevision, 1);
    QVERIFY(!st.pending);
    QCOMPARE(st.lastSyncedSha256, SaveStore::sha256Of("local-1"));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
    // No secret in sync.json
    const QByteArray raw = readFile(SaveStore::stateFilePath(gdir()));
    QVERIFY(!raw.contains("fba_") && !raw.contains("fbd_") && !raw.toLower().contains("token"));
  }

  void hubSlotNoLocalFileDownloads() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-1"));
    QCOMPARE(SaveStore::loadState(gdir()).baseRevision, 1);
    QCOMPARE(puts(), 0);
  }

  void localUnchangedHubNewerDownloadsWithBackup() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-2"));
    QCOMPARE(readFile(saveFile() + QStringLiteral(".bak")), QByteArray("hub-1"));
    QCOMPARE(SaveStore::loadState(gdir()).baseRevision, 2);
    QCOMPARE(puts(), 0);
  }

  void localChangedHubUnchangedUploads() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-2");
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(hub_->saves.value(kGame).revision, 2);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-2"));
    QCOMPARE(SaveStore::loadState(gdir()).baseRevision, 2);
  }

  void idempotentReuploadDoesNotCreateRevisionOrConflict() {
    hub_->setHubSave(kGame, "same");
    writeFile(saveFile(), "same");  // identical content, but no sync.json (e.g. lost state)
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(puts(), 1);
    QCOMPARE(hub_->saves.value(kGame).revision, 1);
    QVERIFY(hub_->saves.value(kGame).conflicts.isEmpty());
    QCOMPARE(SaveStore::loadState(gdir()).baseRevision, 1);
    // Repeating after the state is in sync does not upload again
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(puts(), 1);
  }

  void staleBaseLeadsToConflictAndKeepsEverything() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");  // other device
    QSignalSpy conflict(sync_.get(), &SaveSync::startConflict);
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")});
    QTRY_COMPARE_WITH_TIMEOUT(conflict.count(), 1, 8000);
    const auto v = conflict.at(0).at(0).value<SaveSync::ConflictView>();
    QCOMPARE(v.conflict.hubRevision, 2);
    QCOMPARE(v.conflict.hubDeviceName, QStringLiteral("Laptop Office"));
    QCOMPARE(v.localBaseRevision, 1);
    QCOMPARE(v.localSha256, SaveStore::sha256Of("local-offline"));
    QVERIFY(!v.localDeviceName.isEmpty() && v.localModified.isValid());
    QVERIFY(sync_->hasPendingConflictDialog());
    // Hub current unchanged, upload secured, conflict recorded; local file untouched
    QCOMPARE(hub_->saves.value(kGame).revision, 2);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("hub-2"));
    QCOMPARE(hub_->saves.value(kGame).conflicts.size(), 1);
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.conflictId.isEmpty());
    QVERIFY(st.pending);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Conflict);
  }

  void conflictDecideLaterKeepsLocalAndPausesUploads() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    QCOMPARE(resolve(SaveSync::Resolution::DecideLater), QStringLiteral("ready"));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Conflict);
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
    // Session: uploads paused while the conflict is open, even for changes
    const int putsBefore = puts();
    sync_->beginSession();
    writeFile(saveFile(), "local-offline-more");
    QTest::qWait(700);
    QCOMPARE(puts(), putsBefore);
    sync_->finalSyncBlocking(true);
    QCOMPARE(puts(), putsBefore);
    QVERIFY(SaveStore::loadState(gdir()).pending);
    // Next start: dialog again from the persisted/Hub conflict, without a new upload
    QCOMPARE(start(), QStringLiteral("conflict"));
    QCOMPARE(puts(), putsBefore);
    QCOMPARE(hub_->saves.value(kGame).conflicts.size(), 1);
  }

  void conflictUseHubReplacesLocalWithBackup() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    QCOMPARE(resolve(SaveSync::Resolution::UseHub), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-2"));
    QStringList backups = QDir(gdir()).entryList({QStringLiteral("rom1.sav.local-*.bak")});
    QCOMPARE(backups.size(), 1);
    QCOMPARE(readFile(gdir() + QLatin1Char('/') + backups.first()), QByteArray("local-offline"));
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending && st.conflictId.isEmpty());
    QCOMPARE(st.baseRevision, 2);
    QCOMPARE(hub_->saves.value(kGame).conflicts.first().status, QStringLiteral("resolved_hub"));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
  }

  void useLocalWithMissingLocalFileDownloadsPromotedCheckpoint() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    QVERIFY(QFile::remove(saveFile()));  // local file vanished while the dialog was open
    QCOMPARE(resolve(SaveSync::Resolution::UseLocal), QStringLiteral("ready"));
    QCOMPARE(hub_->saves.value(kGame).revision, 3);
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));  // the secured upload, now the Hub current
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending && st.conflictId.isEmpty());
    QCOMPARE(st.baseRevision, 3);
    QCOMPARE(st.lastSyncedSha256, SaveStore::sha256Of("local-offline"));
  }

  void useHubAbortsWhenBackupFails() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    sync_->setBackupHook([](const QString&, const QString&) { return QString(); });  // backup fails
    QCOMPARE(resolve(SaveSync::Resolution::UseHub), QStringLiteral("failed"));
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));  // not replaced
    QCOMPARE(hub_->saves.value(kGame).conflicts.first().status, QStringLiteral("open"));  // resolve never called
    QCOMPARE(hub_->saves.value(kGame).revision, 2);
    QVERIFY(!SaveStore::loadState(gdir()).conflictId.isEmpty());
    QVERIFY(sync_->hasPendingConflictDialog());
    // After the problem is gone the same dialog can be resolved
    sync_->setBackupHook({});
    QCOMPARE(resolve(SaveSync::Resolution::UseHub), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-2"));
  }

  void conflictUseLocalAdoptsLocal() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    QCOMPARE(resolve(SaveSync::Resolution::UseLocal), QStringLiteral("ready"));
    QCOMPARE(hub_->saves.value(kGame).revision, 3);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-offline"));
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending && st.conflictId.isEmpty());
    QCOMPARE(st.baseRevision, 3);
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
  }

  void staleResolveRerunsStartSyncAndShowsDialogAgain() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    hub_->setHubSave(kGame, "hub-3");  // changes while the dialog is open
    QCOMPARE(resolve(SaveSync::Resolution::UseLocal), QStringLiteral("conflict"));
    QCOMPARE(hub_->saves.value(kGame).revision, 3);  // nothing was changed by the stale resolution
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("hub-3"));
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
  }

  void hubUnreachableStartsLocalAndRetriesLater() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-2");
    hub_->failSaveRequests = 1;  // slot request fails (503)
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), QByteArray("local-2"));
    SyncState st = SaveStore::loadState(gdir());
    QVERIFY(st.pending);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Pending);
    // Retry (pending sync, same Hub)
    QSignalSpy up(sync_.get(), &SaveSync::uploaded);
    sync_->retryPending();
    QTRY_COMPARE_WITH_TIMEOUT(up.count(), 1, 5000);
    st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending);
    QCOMPARE(st.baseRevision, 2);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-2"));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
  }

  void pendingRetriedWithBackoffWhileConnected() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-2");
    hub_->failSaveRequests = 2;  // slot GET and the first retry fail
    QCOMPARE(start(), QStringLiteral("ready"));
    QSignalSpy up(sync_.get(), &SaveSync::uploaded);
    sync_->retryPending();  // fails (offline) -> schedules a retry with backoff
    QTRY_COMPARE_WITH_TIMEOUT(up.count(), 1, 5000);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-2"));
  }

  void pendingOfAnotherHubIsNeverSentToThisHub() {
    const QString otherDir = SaveStore::gameDir(*profiles_, QStringLiteral("hub-other"), kUser, QStringLiteral("game-x"));
    writeFile(otherDir + QStringLiteral("/x.sav"), "other-hub-save");
    SyncState st;
    st.pending = true;
    QVERIFY(SaveStore::saveState(otherDir, st));
    // Same hub, other user: also separated
    const QString otherUser = SaveStore::gameDir(*profiles_, kHubId, QStringLiteral("u_other"), QStringLiteral("game-y"));
    writeFile(otherUser + QStringLiteral("/y.sav"), "other-user-save");
    QVERIFY(SaveStore::saveState(otherUser, st));
    sync_->retryPending();
    QTest::qWait(300);
    QCOMPARE(puts(), 0);
    QVERIFY(SaveStore::loadState(otherDir).pending);
    QVERIFY(SaveStore::loadState(otherUser).pending);
  }

  void legacySaveIsCopiedNotMoved() {
    const QString legacy = SaveStore::legacyDir(*profiles_, kHubId) + QStringLiteral("/rom1.sav");
    writeFile(legacy, "legacy-save");
    QCOMPARE(start(), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), QByteArray("legacy-save"));
    QVERIFY(QFileInfo::exists(legacy));  // old file stays
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("legacy-save"));
    QCOMPARE(hub_->requests.last().headers.value(QStringLiteral("x-framebeam-base-revision")), QByteArray("0"));
  }

  void hubWithoutSavesFeatureDisablesSync() {
    cleanup();
    noFeature_ = true;
    connectAll();
    QVERIFY(!sync_->available());
    QCOMPARE(sync_->note(), QStringLiteral("Hub does not support save sync"));
    writeFile(saveFile(), "local");
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    sync_->prepareStart(kGame, kRom, {});
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(ready.at(0).at(2).toString(), QStringLiteral("Hub does not support save sync"));
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/games/")), 0);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/saves")), 0);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::None);
  }

  void autoCheckpointDebounceIntervalAndHashSkip() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    sync_->beginSession();
    writeFile(saveFile(), "play-1");
    QTest::qWait(20);
    QCOMPARE(puts(), 0);  // debounce not over yet
    QTRY_COMPARE_WITH_TIMEOUT(puts(), 1, 3000);
    QCOMPARE(lastPutReason(), QByteArray("checkpoint"));
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-1"));
    // Next change: only after the minimum interval (500 ms since the last upload)
    writeFile(saveFile(), "play-22");
    QTest::qWait(250);
    QCOMPARE(puts(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(puts(), 2, 3000);
    // Same content again (new mtime, same hash): no upload
    writeFile(saveFile(), "play-22");
    QTest::qWait(900);
    QCOMPARE(puts(), 2);
    sync_->finalSyncBlocking(true);
    QCOMPARE(puts(), 2);  // nothing changed: no final upload either
  }

  void finalSyncUploadsImmediatelyOnPauseAndStop() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    sync_->beginSession();
    writeFile(saveFile(), "play-1");
    QSignalSpy fin(sync_.get(), &SaveSync::finalSyncFinished);
    sync_->finalSync(false);  // pause
    QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 3000);
    QVERIFY(fin.at(0).at(1).toBool());
    QCOMPARE(lastPutReason(), QByteArray("final"));
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-1"));
    QVERIFY(sync_->sessionActive());
    writeFile(saveFile(), "play-2");
    QVERIFY(sync_->finalSyncBlocking(true));  // stop/exit
    QCOMPARE(lastPutReason(), QByteArray("final_session_end"));
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-2"));
    QVERIFY(!sync_->sessionActive());
    QVERIFY(!SaveStore::loadState(gdir()).pending);
  }

  void finalSyncFailureLeavesPending() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    sync_->beginSession();
    writeFile(saveFile(), "play-1");
    hub_->failSaveRequests = 1;
    QVERIFY(!sync_->finalSyncBlocking(true));
    QVERIFY(SaveStore::loadState(gdir()).pending);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("hub-1"));
    // Retried at the next opportunity
    QSignalSpy up(sync_.get(), &SaveSync::uploaded);
    sync_->retryPending();
    QTRY_COMPARE_WITH_TIMEOUT(up.count(), 1, 5000);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-1"));
  }

  // ---------------------------------------------------------------- saves_v2: restore, snapshots, push, slots

  void restoreReplacesLocalSaveAndRespectsPending() {
    hub_->setHubSave(kGame, "cp-1");
    hub_->addHistory(kGame, QStringLiteral("default"), "old-content", QStringLiteral("session_end"), QStringLiteral("Boss"));
    QCOMPARE(start(), QStringLiteral("ready"));  // downloads cp-1
    QCOMPARE(readFile(saveFile()), QByteArray("cp-1"));
    QVERIFY(sync_->hubSupportsSavesV2());
    QVERIFY(sync_->pendingReason(kGame, QStringLiteral("default")).isEmpty());

    // Pending local changes block the restore; nothing is sent
    writeFile(saveFile(), "unsynced-local");
    QVERIFY(!sync_->pendingReason(kGame, QStringLiteral("default")).isEmpty());
    SaveSync::RestoreResult res;
    bool done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QCOMPARE(hub_->restoreCount, 0);
    QCOMPARE(readFile(saveFile()), QByteArray("unsynced-local"));

    // Stale expected revision: 409, nothing changes
    writeFile(saveFile(), "cp-1");  // back to the synced content
    done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 7, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Stale);
    QCOMPARE(hub_->restoreCount, 0);
    QCOMPARE(readFile(saveFile()), QByteArray("cp-1"));

    // Restore: Hub checkpoint + local save replaced (download), backup of the old local file kept
    done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY2(res.ok(), qPrintable(res.message));
    QVERIFY(res.localUpdated);
    QCOMPARE(res.revision, 2);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("old-content"));
    QCOMPARE(readFile(saveFile()), QByteArray("old-content"));
    QCOMPARE(QDir(gdir()).entryList({QStringLiteral("*.restore-*.bak")}).size(), 1);
    const SyncState st = SaveStore::loadState(gdir());
    QCOMPARE(st.baseRevision, 2);
    QVERIFY(!st.pending);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
    QCOMPARE(hub_->saves.value(kGame).history.last().reason, QStringLiteral("before_restore"));

    // Not while the game runs
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")});
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QTRY_COMPARE(ready.count(), 1);
    sync_->beginSession();
    done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 2, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QCOMPARE(hub_->restoreCount, 1);
    QVERIFY(sync_->finalSyncBlocking(true));
  }

  void snapshotInGameUploadsChangedSaveFirst() {
    hub_->setHubSave(kGame, "cp-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    sync_->beginSession();
    writeFile(saveFile(), "play-1");
    SaveSync::SnapshotResult res;
    bool done = false;
    sync_->snapshotActive(QStringLiteral("Before boss"), [&](const SaveSync::SnapshotResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY2(res.ok(), qPrintable(res.message));
    QCOMPARE(res.version.label, QStringLiteral("Before boss"));
    QCOMPARE(res.version.reason, QStringLiteral("manual_snapshot"));
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-1"));  // uploaded first
    QCOMPARE(hub_->saves.value(kGame).history.last().content, QByteArray("play-1"));
    QCOMPARE(lastPutReason(), QByteArray("final"));
    QVERIFY(sync_->sessionActive());
    QVERIFY(sync_->finalSyncBlocking(true));

    // Snapshot of an unknown slot: not found, clear message
    done = false;
    sync_->createSnapshot(kGame, QStringLiteral("nothing"), QString(), [&](const SaveSync::SnapshotResult& r) { res = r; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(res.kind, SaveSnapshotResult::Outcome::NotFound);
  }

  void saveUpdatedPushIsReportedAndOwnDeviceIgnored() {
    hub_->setHubSave(kGame, "cp-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    QSignalSpy spy(sync_.get(), &SaveSync::saveChangedElsewhere);
    SaveUpdate u;
    u.gameId = kGame;
    u.slot = QStringLiteral("default");
    u.revision = 5;
    u.sha256 = QString(64, QLatin1Char('a'));
    u.deviceId = QStringLiteral("other-device");
    u.deviceName = QStringLiteral("Laptop");
    u.reason = QStringLiteral("checkpoint");
    sync_->handleSaveUpdate(u);  // game not running here
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(1).toBool());
    sync_->beginSession();
    u.deviceId = QStringLiteral("00000000-0000-0000-0000-000000000000");  // Hub web interface, any reason string
    u.deviceName = QStringLiteral("Hub web interface");
    u.reason = QStringLiteral("conflict_resolution");
    sync_->handleSaveUpdate(u);
    QCOMPARE(spy.count(), 2);
    QVERIFY(spy.at(1).at(1).toBool());  // running here with this slot
    u.slot = QStringLiteral("boss");
    sync_->handleSaveUpdate(u);
    QVERIFY(!spy.at(2).at(1).toBool());  // other slot
    u.deviceId = conn_->deviceId();
    sync_->handleSaveUpdate(u);
    QCOMPARE(spy.count(), 3);  // own device ignored
    QVERIFY(sync_->finalSyncBlocking(true));
  }

  void slotSelectionIsPersistedAndUsedBySync() {
    PlayerSettings settings(dir_->path());
    sync_->setSettings(&settings);
    QCOMPARE(sync_->slotFor(kGame), QStringLiteral("default"));
    QVERIFY(!sync_->setSlot(kGame, QStringLiteral("Bad Slot")));
    QVERIFY(sync_->setSlot(kGame, QStringLiteral("boss")));
    QCOMPARE(PlayerSettings(dir_->path()).saveSlot(kHubId, kGame), QStringLiteral("boss"));

    const QString sdir = SaveStore::slotDir(*profiles_, kHubId, kUser, kGame, QStringLiteral("boss"));
    QVERIFY(sdir != gdir());
    writeFile(sdir + QStringLiteral("/rom1.sav"), "boss-local");
    writeFile(saveFile(), "default-local");  // other slot, must stay untouched
    QCOMPARE(start(), QStringLiteral("ready"));  // uploads the boss slot's file at start
    QCOMPARE(hub_->saves.value(FakeHub::slotKey(kGame, QStringLiteral("boss"))).content, QByteArray("boss-local"));
    QVERIFY(!hub_->saves.contains(kGame));
    QCOMPARE(sync_->activeSaveFile(), sdir + QStringLiteral("/rom1.sav"));
    QCOMPARE(sync_->activeSlot(), QStringLiteral("boss"));
    QCOMPARE(SaveStore::loadState(sdir).slot, QStringLiteral("boss"));

    // checkpoint + final sync go to the chosen slot
    sync_->beginSession();
    writeFile(sdir + QStringLiteral("/rom1.sav"), "boss-play");
    QVERIFY(sync_->finalSyncBlocking(true));
    QCOMPARE(hub_->saves.value(FakeHub::slotKey(kGame, QStringLiteral("boss"))).content, QByteArray("boss-play"));
    QVERIFY(!hub_->saves.contains(kGame));
    QCOMPARE(readFile(saveFile()), QByteArray("default-local"));

    // Pending retry also covers non-default slots
    writeFile(sdir + QStringLiteral("/rom1.sav"), "boss-offline");
    SyncState st = SaveStore::loadState(sdir);
    st.pending = true;
    SaveStore::saveState(sdir, st);
    QSignalSpy up(sync_.get(), &SaveSync::uploaded);
    sync_->retryPending();
    QTRY_COMPARE_WITH_TIMEOUT(up.count(), 1, 5000);
    QCOMPARE(hub_->saves.value(FakeHub::slotKey(kGame, QStringLiteral("boss"))).content, QByteArray("boss-offline"));
  }
};

QTEST_GUILESS_MAIN(SaveSyncTest)
#include "savesync_test.moc"
