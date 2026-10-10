// Save sync against the fake Hub: start sync cases (D4), conflicts, pending retry, checkpoints, final sync.
// Only dummy files; no real saves or ROMs.
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "dsvsave.h"
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
  bool noV4_ = false;

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
    if (!noV4_) {
      hub_->features.append(QStringLiteral("saves_v4"));
    }
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
  // prepareStart with a core; waits for startReady / startFailed. Returns "ready" | "failed" | "conflict" (empty = timeout).
  QString startWithCore(const QString& coreId, const QString& version, QString* text = nullptr) {
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QSignalSpy failed(sync_.get(), &SaveSync::startFailed);
    QSignalSpy conflict(sync_.get(), &SaveSync::startConflict);
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")}, SaveSync::CoreRef{coreId, version});
    (void)QTest::qWaitFor([&]() { return ready.count() + failed.count() + conflict.count() > 0; }, 8000);
    if (text != nullptr) {
      *text = failed.count() > 0 ? failed.at(0).at(1).toString() : (ready.count() > 0 ? ready.at(0).at(2).toString() : QString());
    }
    return failed.count() > 0 ? QStringLiteral("failed") : (conflict.count() > 0 ? QStringLiteral("conflict") : (ready.count() > 0 ? QStringLiteral("ready") : QString()));
  }
  int snapshotsOnHub() const {
    int n = 0;
    for (const FakeVersion& v : hub_->saves.value(kGame).history) {
      n += v.reason == QLatin1String("manual_snapshot") ? 1 : 0;
    }
    return n;
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

  // ---- Conflicts outside the play flow, live save --------------------------------------------------------------------

  // A conflict that is "decided later": local "local-offline" vs Hub "hub-2", the game is not running.
  void makeOpenConflict() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    writeFile(saveFile(), "local-offline");
    hub_->setHubSave(kGame, "hub-2");
    QCOMPARE(start(), QStringLiteral("conflict"));
    QCOMPARE(resolve(SaveSync::Resolution::DecideLater), QStringLiteral("ready"));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Conflict);
  }
  SaveSync::SlotConflict loadConflict() {
    SaveSync::SlotConflict out;
    bool done = false;
    sync_->loadSlotConflict(kGame, QStringLiteral("default"), QStringLiteral("rom1.sav"), [&](const SaveSync::SlotConflict& c) { out = c; done = true; });
    (void)QTest::qWaitFor([&]() { return done; }, 8000);
    return out;
  }
  SaveSync::RestoreResult resolveHere(SaveSync::Resolution r) {
    SaveSync::RestoreResult out;
    bool done = false;
    sync_->resolveSlotConflict(kGame, QStringLiteral("default"), r, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& x) { out = x; done = true; });
    (void)QTest::qWaitFor([&]() { return done; }, 8000);
    return out;
  }

  struct LiveCore {  // fake running core: 12 bytes of save memory, `apply` writes the save file like the emulation thread would
    int flushes = 0, applies = 0, resets = 0;
    QByteArray applied;
    qint64 size = 12;
    bool applyOk = true;
    QString file;
  };
  void installLiveCore(LiveCore* core) {
    SaveSync::LiveHooks h;
    h.flush = [core]() { ++core->flushes; };
    h.accepts = [core](qint64 n) { return n == core->size; };
    h.apply = [core](const QByteArray& d) {
      ++core->applies;
      if (!core->applyOk) {
        return false;
      }
      core->applied = d;
      writeFile(core->file, d);
      ++core->resets;
      return true;
    };
    sync_->setLiveHooks(std::move(h));
  }
  // A running game with a synced save of 12 bytes.
  void startRunning(LiveCore* core) {
    hub_->setHubSave(kGame, "hub-cp-1-xxx");  // 12 bytes
    QCOMPARE(start(), QStringLiteral("ready"));
    core->file = saveFile();
    installLiveCore(core);
    sync_->beginSession();
  }

 private slots:
  void init() {
    noFeature_ = false;
    noV4_ = false;
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
    QElapsedTimer sinceFirstUpload;  // started after the upload happened: real elapsed >= measured
    sinceFirstUpload.start();
    QCOMPARE(lastPutReason(), QByteArray("checkpoint"));
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-1"));
    // Next change: only after the minimum interval (500 ms since the last upload)
    writeFile(saveFile(), "play-22");
    QTest::qWait(250);
    // On a slow runner the interval may already have passed; the second upload is then
    // legitimate, so assert "still 1" only while safely below the 500 ms interval.
    if (sinceFirstUpload.elapsed() < 400) {
      QCOMPARE(puts(), 1);
    }
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

  void uploadSaveFileReplacesHubAndLocalSave() {
    hub_->setHubSave(kGame, "cp-1");
    QCOMPARE(start(), QStringLiteral("ready"));
    QVERIFY(sync_->hubSupportsSavesV4());
    const QString dflt = QStringLiteral("default");
    const QString picked = QDir(dir_->path()).filePath(QStringLiteral("picked.srm"));
    writeFile(picked, "uploaded-bytes");
    const auto run = [&](const QString& file, int rev) {
      SaveSync::RestoreResult res;
      bool done = false;
      sync_->uploadSaveFile(kGame, dflt, file, rev, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
      (void)QTest::qWaitFor([&]() { return done; }, 8000);
      return res;
    };

    // Stale expected revision: error, local file untouched
    SaveSync::RestoreResult res = run(picked, 5);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Stale);
    QCOMPARE(hub_->uploadCount, 0);
    QCOMPARE(readFile(saveFile()), QByteArray("cp-1"));

    // Empty file is refused locally, nothing is sent
    const QString empty = QDir(dir_->path()).filePath(QStringLiteral("empty.sav"));
    writeFile(empty, "");
    res = run(empty, 1);
    QVERIFY(!res.ok());
    QVERIFY(!res.message.isEmpty());
    QCOMPARE(hub_->uploadCount, 0);

    // Too large for the Hub (413): error, local file untouched
    hub_->uploadLimit = 4;
    res = run(picked, 1);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Failed);
    QVERIFY(res.message.contains(QStringLiteral("too large")));
    QCOMPARE(readFile(saveFile()), QByteArray("cp-1"));
    hub_->uploadLimit = 64LL * 1024 * 1024;

    // Success: Hub checkpoint, local file replaced, backup, state
    res = run(picked, 1);
    QVERIFY2(res.ok(), qPrintable(res.message));
    QVERIFY(res.localUpdated);
    QCOMPARE(res.revision, 2);
    QCOMPARE(hub_->uploadCount, 1);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("uploaded-bytes"));
    QCOMPARE(hub_->saves.value(kGame).reason, QStringLiteral("upload"));
    QCOMPARE(hub_->saves.value(kGame).history.last().reason, QStringLiteral("before_upload"));
    QCOMPARE(readFile(saveFile()), QByteArray("uploaded-bytes"));
    QCOMPARE(QDir(gdir()).entryList({QStringLiteral("*.upload-*.bak")}).size(), 1);
    const SyncState st = SaveStore::loadState(gdir());
    QCOMPARE(st.baseRevision, 2);
    QVERIFY(!st.pending);
    QCOMPARE(st.lastSyncedSha256, SaveStore::sha256Of(QByteArray("uploaded-bytes")));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);

    // Pending local changes block it
    writeFile(saveFile(), "unsynced-local");
    res = run(picked, 2);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QCOMPARE(hub_->uploadCount, 1);
    QCOMPARE(readFile(saveFile()), QByteArray("unsynced-local"));
    writeFile(saveFile(), "uploaded-bytes");

    // Not while the game runs
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")});
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QTRY_COMPARE(ready.count(), 1);
    sync_->beginSession();
    res = run(picked, 2);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QCOMPARE(hub_->uploadCount, 1);
    QVERIFY(sync_->finalSyncBlocking(true));
  }

  void uploadSaveFileNeedsSavesV4() {
    cleanup();
    noV4_ = true;
    connectAll();
    QVERIFY(!sync_->hubSupportsSavesV4());
    const QString picked = QDir(dir_->path()).filePath(QStringLiteral("picked.srm"));
    writeFile(picked, "x");
    SaveSync::RestoreResult res;
    bool done = false;
    sync_->uploadSaveFile(kGame, QStringLiteral("default"), picked, 0, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(!res.ok());
    QCOMPARE(hub_->uploadCount, 0);
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

  void conflictIsLoadedAndResolvedWithoutStartingTheGame_useHub() {
    makeOpenConflict();
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QSignalSpy conflictSig(sync_.get(), &SaveSync::startConflict);
    const SaveSync::SlotConflict c = loadConflict();
    QCOMPARE(c.kind, SaveSync::SlotConflict::Outcome::Found);
    QCOMPARE(c.view.conflict.hubRevision, 2);
    QCOMPARE(c.view.conflict.hubDeviceName, QStringLiteral("Laptop Office"));
    QVERIFY(c.localExists);
    QCOMPARE(c.localSize, qint64(13));  // "local-offline"
    QCOMPARE(c.hubSize, qint64(5));     // "hub-2"
    QVERIFY(!c.view.localDeviceName.isEmpty() && c.view.localModified.isValid());
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));  // looking changes nothing

    const SaveSync::RestoreResult r = resolveHere(SaveSync::Resolution::UseHub);
    QVERIFY2(r.ok(), qPrintable(r.message));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-2"));
    const QStringList backups = QDir(gdir()).entryList({QStringLiteral("rom1.sav.local-*.bak")});
    QCOMPARE(backups.size(), 1);
    QCOMPARE(readFile(gdir() + QLatin1Char('/') + backups.first()), QByteArray("local-offline"));
    QCOMPARE(hub_->saves.value(kGame).conflicts.first().status, QStringLiteral("resolved_hub"));
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending && st.conflictId.isEmpty());
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
    QCOMPARE(ready.count(), 0);  // the game start flow never ran
    QCOMPARE(conflictSig.count(), 0);
    QVERIFY(!sync_->sessionActive() && !sync_->hasPendingConflictDialog());
    // Resolved: loading again finds none
    QCOMPARE(loadConflict().kind, SaveSync::SlotConflict::Outcome::None);
  }

  void conflictIsResolvedWithoutStartingTheGame_useLocal() {
    makeOpenConflict();
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    const SaveSync::RestoreResult r = resolveHere(SaveSync::Resolution::UseLocal);
    QVERIFY2(r.ok(), qPrintable(r.message));
    QCOMPARE(hub_->saves.value(kGame).revision, 3);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-offline"));
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending && st.conflictId.isEmpty());
    QCOMPARE(st.baseRevision, 3);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
    QCOMPARE(ready.count(), 0);
  }

  void conflictResolutionAbortsOnBackupFailureAndStaleHub() {
    makeOpenConflict();
    sync_->setBackupHook([](const QString&, const QString&) { return QString(); });
    SaveSync::RestoreResult r = resolveHere(SaveSync::Resolution::UseHub);
    QVERIFY(!r.ok());
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
    QCOMPARE(hub_->saves.value(kGame).conflicts.first().status, QStringLiteral("open"));
    QVERIFY(!SaveStore::loadState(gdir()).conflictId.isEmpty());
    sync_->setBackupHook({});
    // The Hub changes between "look" and "decide": stale, nothing changed, the caller loads the conflict again
    hub_->setHubSave(kGame, "hub-3");
    r = resolveHere(SaveSync::Resolution::UseLocal);
    QCOMPARE(r.kind, SaveRestoreResult::Outcome::Stale);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("hub-3"));
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
  }

  void conflictResolvedElsewhereClearsTheLocalMarker() {
    makeOpenConflict();
    hub_->saves[kGame].conflicts[0].status = QStringLiteral("resolved_hub");  // e.g. in the Hub web interface
    QCOMPARE(loadConflict().kind, SaveSync::SlotConflict::Outcome::None);
    QVERIFY(SaveStore::loadState(gdir()).conflictId.isEmpty());
    QVERIFY(sync_->kind(kGame) != SaveSync::Kind::Conflict);
    QCOMPARE(readFile(saveFile()), QByteArray("local-offline"));
  }

  void liveRestoreLoadsTheSaveIntoTheRunningCoreAfterBackup() {
    LiveCore core;
    startRunning(&core);
    hub_->addHistory(kGame, QStringLiteral("default"), "hub-old-1234", QStringLiteral("session_end"));
    QVERIFY(sync_->liveApplyAvailable(kGame, QStringLiteral("default")));
    QVERIFY(sync_->liveBlockReason(kGame, QStringLiteral("default")).isEmpty());
    // The game has progressed (unsynced): it is uploaded first and kept in the history, then the restore happens
    writeFile(saveFile(), "play-newer-xx");
    SaveSync::RestoreResult res;
    bool done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QVERIFY2(res.ok() && res.message.isEmpty() && res.localUpdated, qPrintable(res.message));
    QVERIFY(core.flushes >= 1);
    QCOMPARE(core.applies, 1);
    QCOMPARE(core.applied, QByteArray("hub-old-1234"));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-old-1234"));
    QCOMPARE(hub_->restoreCount, 1);
    // the progress of the game is on the Hub (final upload + before_restore) and in a local backup
    QCOMPARE(hub_->saves.value(kGame).history.last().reason, QStringLiteral("before_restore"));
    QCOMPARE(hub_->saves.value(kGame).history.last().content, QByteArray("play-newer-xx"));
    const QStringList backups = QDir(gdir()).entryList({QStringLiteral("*.restore-*.bak")});
    QCOMPARE(backups.size(), 1);
    QCOMPARE(readFile(gdir() + QLatin1Char('/') + backups.first()), QByteArray("play-newer-xx"));
    const SyncState st = SaveStore::loadState(gdir());
    QVERIFY(!st.pending);
    QCOMPARE(st.baseRevision, 3);
    QCOMPARE(st.lastSyncedSha256, SaveStore::sha256Of("hub-old-1234"));
    // The restored file is not uploaded again as a "change"
    const int putsBefore = puts();
    QTest::qWait(600);
    QCOMPARE(puts(), putsBefore);
    QVERIFY(sync_->sessionActive());
    QVERIFY(sync_->finalSyncBlocking(true));
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("hub-old-1234"));
  }

  void liveRestoreRefusesWhenTheCoreCannotTakeTheSave() {
    LiveCore core;
    startRunning(&core);
    hub_->addHistory(kGame, QStringLiteral("default"), "hub-old-other-size", QStringLiteral("session_end"));  // not 12 bytes
    SaveSync::RestoreResult res;
    bool done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QVERIFY(res.message.contains(QStringLiteral("Quit the game")));
    QCOMPARE(hub_->restoreCount, 0);  // nothing changed on the Hub
    QCOMPARE(core.applies, 0);
    QCOMPARE(readFile(saveFile()), QByteArray("hub-cp-1-xxx"));
    // The core fails after the Hub restored: the local file stays, the message asks for a restart
    hub_->addHistory(kGame, QStringLiteral("default"), "hub-old-1234", QStringLiteral("session_end"));
    core.applyOk = false;
    done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 2, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QVERIFY(res.ok() && !res.localUpdated);
    QVERIFY(res.message.contains(QStringLiteral("Quit the game and start it again")));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-cp-1-xxx"));
    QVERIFY(sync_->finalSyncBlocking(true));
  }

  void liveRestoreIsBlockedByAnOpenConflict() {
    LiveCore core;
    startRunning(&core);
    writeFile(saveFile(), "local-newer-x");
    hub_->setHubSave(kGame, "hub-newer-xx");
    QSignalSpy fin(sync_.get(), &SaveSync::finalSyncFinished);
    sync_->finalSync(false);  // the checkpoint upload runs into a conflict
    QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 8000);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Conflict);
    QVERIFY(!sync_->liveBlockReason(kGame, QStringLiteral("default")).isEmpty());
    SaveSync::RestoreResult res;
    bool done = false;
    sync_->restoreVersion(kGame, QStringLiteral("default"), 1, 2, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QCOMPARE(core.applies, 0);
    // ... and is resolved in the game: "Use the Hub save" backs up the local save and loads the Hub save into the core
    const SaveSync::SlotConflict c = loadConflict();
    QCOMPARE(c.kind, SaveSync::SlotConflict::Outcome::Found);
    QCOMPARE(c.localSize, qint64(13));
    const SaveSync::RestoreResult r = resolveHere(SaveSync::Resolution::UseHub);
    QVERIFY2(r.ok(), qPrintable(r.message));
    QCOMPARE(core.applied, QByteArray("hub-newer-xx"));
    QCOMPARE(readFile(saveFile()), QByteArray("hub-newer-xx"));
    QCOMPARE(QDir(gdir()).entryList({QStringLiteral("rom1.sav.local-*.bak")}).size(), 1);
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
    QVERIFY(sync_->sessionActive());
    // Uploads are free again: the next change is a normal checkpoint on top of the resolved revision
    const int putsBefore = puts();
    writeFile(saveFile(), "play-after-xxx");
    QVERIFY(sync_->finalSyncBlocking(true));
    QCOMPARE(puts(), putsBefore + 1);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("play-after-xxx"));
  }

  void liveConflictKeepLocalNeedsNoCoreChange() {
    LiveCore core;
    startRunning(&core);
    writeFile(saveFile(), "local-newer-x");
    hub_->setHubSave(kGame, "hub-newer-xx");
    QSignalSpy fin(sync_.get(), &SaveSync::finalSyncFinished);
    sync_->finalSync(false);
    QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 1, 8000);
    const SaveSync::RestoreResult r = resolveHere(SaveSync::Resolution::UseLocal);
    QVERIFY2(r.ok(), qPrintable(r.message));
    QCOMPARE(core.applies, 0);  // the running game already has this save
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("local-newer-x"));
    QCOMPARE(readFile(saveFile()), QByteArray("local-newer-x"));
    QCOMPARE(sync_->kind(kGame), SaveSync::Kind::Synced);
    QVERIFY(sync_->finalSyncBlocking(true));
  }

  void liveUploadLoadsTheFileIntoTheRunningCore() {
    LiveCore core;
    startRunning(&core);
    const QString picked = dir_->filePath(QStringLiteral("picked.sav"));
    writeFile(picked, "picked-12-byt");  // 13 bytes: the core takes 12 only
    SaveSync::RestoreResult res;
    bool done = false;
    sync_->uploadSaveFile(kGame, QStringLiteral("default"), picked, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QCOMPARE(res.kind, SaveRestoreResult::Outcome::Blocked);
    QCOMPARE(hub_->uploadCount, 0);
    writeFile(picked, "picked-12-by");
    done = false;
    sync_->uploadSaveFile(kGame, QStringLiteral("default"), picked, 1, QStringLiteral("rom1.sav"), [&](const SaveSync::RestoreResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QVERIFY2(res.ok() && res.message.isEmpty(), qPrintable(res.message));
    QCOMPARE(hub_->uploadCount, 1);
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("picked-12-by"));
    QCOMPARE(core.applied, QByteArray("picked-12-by"));
    QCOMPARE(QDir(gdir()).entryList({QStringLiteral("*.upload-*.bak")}).size(), 1);
    QVERIFY(!SaveStore::loadState(gdir()).pending);
    QVERIFY(sync_->finalSyncBlocking(true));
  }

  void snapshotInGameFlushesTheCoreFirst() {
    LiveCore core;
    startRunning(&core);
    writeFile(saveFile(), "play-1-xxxxxx");
    SaveSync::SnapshotResult res;
    bool done = false;
    sync_->snapshotActive(QString(), [&](const SaveSync::SnapshotResult& r) { res = r; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QVERIFY(res.ok());
    QCOMPARE(core.flushes, 1);
    QVERIFY(sync_->finalSyncBlocking(true));
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

  // ---------------------------------------------------------------- ADR 0020 D7: save snapshot before a core change

  void localOnlySaveSourceNeverTouchesTheHubOrTheFiles() {
    // 3DS (saveSource "none"): the core's save tree lives in the game's save directory; nothing is synced, nothing is
    // imported, replaced or deleted, even when the Hub holds a save for the same game id.
    hub_->setHubSave(kGame, "hub-1");
    const QString tree = gdir() + QStringLiteral("/default/sdmc/Nintendo 3DS/00000000/title.dat");
    writeFile(tree, "dummy-3ds-tree");
    writeFile(saveFile(), "stray-local-file");  // even a file named like a SAVE_RAM save stays untouched
    const int requestsBefore = hub_->requests.size();
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    SaveSync::CoreRef core{QStringLiteral("azahar"), QStringLiteral("2026.10.09")};
    core.saveSource = QStringLiteral("none");
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")}, core);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 8000);
    QVERIFY(ready.at(0).at(2).toString().contains(QStringLiteral("not available for this system yet")));
    QVERIFY(!ready.at(0).at(1).toString().isEmpty());  // the save directory is handed to the core
    sync_->beginSession();
    QVERIFY(!sync_->sessionActive());
    QSignalSpy finished(sync_.get(), &SaveSync::finalSyncFinished);
    sync_->finalSync(true);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
    QVERIFY(finished.at(0).at(1).toBool());
    QCOMPARE(hub_->requests.size(), requestsBefore);  // no request to the Hub at all
    QCOMPARE(readFile(tree), QByteArray("dummy-3ds-tree"));
    QCOMPARE(readFile(saveFile()), QByteArray("stray-local-file"));
    QCOMPARE(puts(), 0);
  }

  void firstStartWithACoreOnlyRecords() {
    hub_->setHubSave(kGame, "hub-1");
    QString text;
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09"), &text), QStringLiteral("ready"));
    QCOMPARE(snapshotsOnHub(), 0);
    QVERIFY(!text.contains(QStringLiteral("Core changed")));
    const SyncState st = SaveStore::loadState(gdir());
    QCOMPARE(st.writerCoreId, QStringLiteral("melondsds"));
    QCOMPARE(st.writerCoreVersion, QStringLiteral("2026.10.09"));
    // Same core and version again: nothing happens.
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    QCOMPARE(snapshotsOnHub(), 0);
  }

  void coreChangeSnapshotsTheHubSaveFirstAndNotifies() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    QString text;
    QCOMPARE(startWithCore(QStringLiteral("desmume"), QStringLiteral("2026.10.08"), &text), QStringLiteral("ready"));
    QCOMPARE(snapshotsOnHub(), 1);
    const FakeVersion snap = hub_->saves.value(kGame).history.last();
    QCOMPARE(snap.label, QString::fromUtf8("Before core change: melondsds 2026.10.09 \xe2\x86\x92 desmume 2026.10.08"));
    QCOMPARE(snap.content, QByteArray("hub-1"));
    QVERIFY2(text.contains(QStringLiteral("melondsds 2026.10.09")) && text.contains(QStringLiteral("desmume 2026.10.08")), qPrintable(text));
    QCOMPARE(SaveStore::loadState(gdir()).writerCoreId, QStringLiteral("desmume"));
    // A local copy of the save is kept as well.
    QVERIFY(!QDir(gdir()).entryList({QStringLiteral("rom1.sav.core-change-*.bak")}).isEmpty());
    // Back to the first core: another change, another snapshot.
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    QCOMPARE(snapshotsOnHub(), 2);
  }

  void newBuildOfTheSameCoreIsAChange() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(startWithCore(QStringLiteral("my-core"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    QCOMPARE(startWithCore(QStringLiteral("my-core"), QStringLiteral("2026.10.10")), QStringLiteral("ready"));
    QCOMPARE(snapshotsOnHub(), 1);
    QVERIFY(hub_->saves.value(kGame).history.last().label.contains(QStringLiteral("my-core 2026.10.09")));
  }

  void failedSnapshotBlocksTheStartAndKeepsTheRecord() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    hub_->failSaveRequests = 1;  // the snapshot request answers 503
    QString text;
    QCOMPARE(startWithCore(QStringLiteral("desmume"), QStringLiteral("2026.10.08"), &text), QStringLiteral("failed"));
    QVERIFY2(text.contains(QStringLiteral("not started")) && text.contains(QStringLiteral("melondsds")), qPrintable(text));
    QCOMPARE(snapshotsOnHub(), 0);
    QCOMPARE(SaveStore::loadState(gdir()).writerCoreId, QStringLiteral("melondsds"));  // unchanged: the old core stays the writer
    QCOMPARE(hub_->saves.value(kGame).content, QByteArray("hub-1"));
    // Next try works.
    QCOMPARE(startWithCore(QStringLiteral("desmume"), QStringLiteral("2026.10.08")), QStringLiteral("ready"));
    QCOMPARE(snapshotsOnHub(), 1);
  }

  void offlineHubBlocksACoreChangeWithALocalSave() {
    hub_->setHubSave(kGame, "hub-1");
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    QVERIFY(QFileInfo::exists(saveFile()));
    hub_.reset();  // Hub gone
    QString text;
    QCOMPARE(startWithCore(QStringLiteral("desmume"), QStringLiteral("2026.10.08"), &text), QStringLiteral("failed"));
    QVERIFY2(text.contains(QStringLiteral("not reachable")) && text.contains(QStringLiteral("not started")), qPrintable(text));
    QCOMPARE(SaveStore::loadState(gdir()).writerCoreId, QStringLiteral("melondsds"));
    // Same core while offline is fine.
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
  }

  void noSaveOnTheHubDoesNotBlockACoreChange() {
    // Local save only (never uploaded): there is nothing to snapshot, the start sync uploads the local save first.
    writeFile(saveFile(), "local-1");
    hub_->failSaveRequests = 0;
    QCOMPARE(startWithCore(QStringLiteral("melondsds"), QStringLiteral("2026.10.09")), QStringLiteral("ready"));
    QCOMPARE(startWithCore(QStringLiteral("desmume"), QStringLiteral("2026.10.08")), QStringLiteral("ready"));
    QCOMPARE(SaveStore::loadState(gdir()).writerCoreId, QStringLiteral("desmume"));
    QVERIFY(hub_->saves.contains(kGame));  // uploaded by the start sync
  }

  void snapshotLabelStaysWithinTheHubLimit() {
    const SaveSync::CoreRef a{QStringLiteral("a-very-long-core-identifier-number-one"), QStringLiteral("2026.10.09.12")};
    const SaveSync::CoreRef b{QStringLiteral("another-very-long-core-identifier-two"), QStringLiteral("2026.10.10.3")};
    const QString label = SaveSync::coreChangeLabel(a, b);
    QVERIFY(label.toUtf8().size() <= 64);
    QVERIFY(label.startsWith(QStringLiteral("Before core change: a-very-long")));
    QCOMPARE(SaveSync::coreChangeLabel({QStringLiteral("x"), QStringLiteral("1")}, {QStringLiteral("y"), QStringLiteral("2")}),
             QString::fromUtf8("Before core change: x 1 \xe2\x86\x92 y 2"));
  }

  // ---------------------------------------------------------------- ADR 0020 D7: DeSmuME core file (.dsv) around the raw save

  static QByteArray dsRaw(char fill) { return QByteArray(0x2000, fill); }  // 64 kbit EEPROM sized dummy save
  static SaveSync::CoreRef desmumeRef() {
    SaveSync::CoreRef r{QStringLiteral("desmume"), QStringLiteral("2026.10.08")};
    r.saveSource = QStringLiteral("core_file");
    r.fileExtension = QStringLiteral(".dsv");
    r.fileFormat = QStringLiteral("desmume_dsv");
    return r;
  }
  static SaveSync::CoreRef melonRef() {
    SaveSync::CoreRef r{QStringLiteral("melondsds"), QStringLiteral("2026.10.09")};
    r.saveSource = QStringLiteral("save_ram");
    r.fileExtension = QStringLiteral(".sav");
    return r;
  }
  QString dsvFile() const { return gdir() + QStringLiteral("/rom1.dsv"); }
  QString startWith(const SaveSync::CoreRef& core, QString* text = nullptr) {
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    QSignalSpy failed(sync_.get(), &SaveSync::startFailed);
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")}, core);
    (void)QTest::qWaitFor([&]() { return ready.count() + failed.count() > 0; }, 8000);
    if (text != nullptr) *text = failed.count() > 0 ? failed.at(0).at(1).toString() : QString();
    return failed.count() > 0 ? QStringLiteral("failed") : (ready.count() > 0 ? QStringLiteral("ready") : QString());
  }

  void desmumeStartWritesTheHubSaveAsDsv() {
    hub_->setHubSave(kGame, dsRaw('a'));
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), dsRaw('a'));  // canonical raw save
    const auto back = dsv::dsvToRaw(readFile(dsvFile()));
    QVERIFY(back.has_value());
    QCOMPARE(*back, dsRaw('a'));  // what DeSmuME loads
    QCOMPARE(puts(), 0);
  }

  void desmumeDsvChangesAreUploadedAsRawNeverAsDsv() {
    hub_->setHubSave(kGame, dsRaw('a'));
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    sync_->beginSession();
    writeFile(dsvFile(), *dsv::rawToDsv(dsRaw('b')));  // the fake core saves
    QVERIFY(sync_->finalSyncBlocking(true));
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('b'));  // raw, no footer
    QCOMPARE(readFile(saveFile()), dsRaw('b'));
    QCOMPARE(SaveStore::loadState(gdir()).lastSyncedSha256, SaveStore::sha256Of(dsRaw('b')));
  }

  void desmumeShortDirHoldsTheDsvAndAdoptsTheOldOne() {
    auto ref = desmumeRef();
    ref.shortDir = true;
    const QString shortDir = SaveStore::shortCoreDir(*profiles_, kHubId, kUser, kGame, QStringLiteral("default"));
    QVERIFY(!shortDir.isEmpty());
    // a .dsv the core wrote to the long directory before this version
    writeFile(dsvFile(), *dsv::rawToDsv(dsRaw('z')));
    QSignalSpy ready(sync_.get(), &SaveSync::startReady);
    sync_->prepareStart(kGame, kRom, {QStringLiteral("rom1")}, ref);
    QVERIFY(QTest::qWaitFor([&]() { return ready.count() > 0; }, 8000));
    QCOMPARE(ready.at(0).at(1).toString(), shortDir);  // the core gets the short dir
    QVERIFY(!QFileInfo::exists(dsvFile()));            // adopted once
    QVERIFY(QFileInfo::exists(shortDir + QStringLiteral("/rom1.dsv")));
    QCOMPARE(readFile(saveFile()), dsRaw('z'));  // the progress of the old .dsv was imported, not lost
    sync_->beginSession();
    writeFile(shortDir + QStringLiteral("/rom1.dsv"), *dsv::rawToDsv(dsRaw('b')));  // the fake core saves
    QVERIFY(sync_->finalSyncBlocking(true));
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('b'));
  }

  void changesFromACrashedSessionAreImportedAtTheNextStart() {
    hub_->setHubSave(kGame, dsRaw('a'));
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    writeFile(dsvFile(), *dsv::rawToDsv(dsRaw('c')));  // progress that was never synced (app crashed)
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('c'));
    QVERIFY(!QDir(gdir()).entryList({QStringLiteral("rom1.sav.core-file-*.bak")}).isEmpty());  // the old raw save is kept
  }

  void unparsableDsvIsNeverUploadedAndNeverLost() {
    hub_->setHubSave(kGame, dsRaw('a'));
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    sync_->beginSession();
    QSignalSpy problem(sync_.get(), &SaveSync::coreSaveProblem);
    const QByteArray junk = "this is not a DeSmuME save";
    writeFile(dsvFile(), junk);
    sync_->finalSyncBlocking(true);
    QCOMPARE(problem.count(), 1);
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('a'));  // Hub unchanged
    QCOMPARE(readFile(dsvFile()), junk);                       // kept locally untouched
    QCOMPARE(readFile(saveFile()), dsRaw('a'));
    // The next start refuses to run over it.
    QString text;
    QCOMPARE(startWith(desmumeRef(), &text), QStringLiteral("failed"));
    QVERIFY2(text.contains(QStringLiteral("untouched")), qPrintable(text));
    QCOMPARE(readFile(dsvFile()), junk);
  }

  void bothFormatsInOneDirectoryNeverFlipTheHubSlot() {
    hub_->setHubSave(kGame, dsRaw('a'));
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    sync_->beginSession();
    writeFile(dsvFile(), *dsv::rawToDsv(dsRaw('b')));
    QVERIFY(sync_->finalSyncBlocking(true));
    // Core change to a SAVE_RAM core: .sav (raw) is the save, the stale .dsv next to it is ignored.
    QCOMPARE(startWith(melonRef()), QStringLiteral("ready"));
    QVERIFY(QFileInfo::exists(dsvFile()));
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('b'));
    sync_->beginSession();
    writeFile(saveFile(), dsRaw('m'));  // melonDS DS persists its SAVE_RAM
    QVERIFY(sync_->finalSyncBlocking(true));
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('m'));
    // Back to DeSmuME: the .dsv is rebuilt from the raw save, the older .dsv content does not come back.
    QCOMPARE(startWith(desmumeRef()), QStringLiteral("ready"));
    QCOMPARE(*dsv::dsvToRaw(readFile(dsvFile())), dsRaw('m'));
    QCOMPARE(hub_->saves.value(kGame).content, dsRaw('m'));
    QCOMPARE(snapshotsOnHub(), 2);  // each core change was snapshotted
  }

  void profiledCoreSyncsExactlyItsFileNotTheNewest() {
    // A foreign newer file in the directory is not picked up by a profiled core.
    writeFile(gdir() + QStringLiteral("/other.bin"), "foreign");
    hub_->setHubSave(kGame, dsRaw('a'));
    QCOMPARE(startWith(melonRef()), QStringLiteral("ready"));
    QCOMPARE(readFile(saveFile()), dsRaw('a'));
    QCOMPARE(puts(), 0);
  }
};

QTEST_GUILESS_MAIN(SaveSyncTest)
#include "savesync_test.moc"
