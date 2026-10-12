// Local ROMs (0.12): library JSON incl. old-Hub fallback, launch-source choice, scanner + registration flow against the
// fake Hub, resumable upload. Dummy bytes and synthetic headers only (no real ROMs).
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "game_identity.h"
#include "gameuploader.h"
#include "hubconnection.h"
#include "hublibrary.h"
#include "localromindex.h"
#include "profilestore.h"
#include "romregistrar.h"
#include "romsource.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }

// Synthetic NDS header: game code at 0x0C, ROM version at 0x1E, then filler (not a real ROM).
QByteArray ndsRom(const QByteArray& code, int version, char fill, int size = 4096) {
  QByteArray b(size, fill);
  b.replace(0, 0x200, QByteArray(0x200, '\0'));
  b.replace(0x0C, 4, code);
  b[0x1E] = static_cast<char>(version);
  return b;
}
QString sha64(char c) { return QString(64, QLatin1Char(c)); }
QJsonObject romJson(const QString& sha, qint64 size, const QString& name, std::optional<bool> stored = std::nullopt) {
  QJsonObject o{{QStringLiteral("sha256"), sha}, {QStringLiteral("size"), size}, {QStringLiteral("filename"), name}};
  if (stored) o.insert(QStringLiteral("stored"), *stored);
  return o;
}
}  // namespace

class LocalRomsTest : public QObject {
  Q_OBJECT
 private:
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;

  void pairAndConnect() {
    conn_->connectToAddress(hub_->address());
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsTrustConfirmation, 8000);
    conn_->confirmTrust();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::NeedsPairing, 8000);
    hub_->decision = FakeHub::Decision::Approve;
    conn_->requestPairing();
    QTRY_VERIFY_WITH_TIMEOUT(conn_->state() == State::Connected, 8000);
  }
  QString writeTemp(const QString& name, const QByteArray& data) {
    const QString p = dir_->filePath(name);
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    return p;
  }
  QString stateFile() const { return dir_->filePath(QStringLiteral("data/uploads.json")); }

 private slots:
  void init() {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->filePath(QStringLiteral("data")));
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->features = {QStringLiteral("saves_v1"), QStringLiteral("uploads_v1"), QStringLiteral("local_roms_v1")};
    QVERIFY(hub_->start());
    conn_ = std::make_unique<HubConnection>(profiles_.get(), creds_.get());
    conn_->setPollIntervalMs(50);
  }
  void cleanup() {
    conn_.reset();
    hub_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  // ------------------------------------------------------------ library JSON

  void parseGameWithLocalRomFields() {
    const QJsonObject g{{QStringLiteral("id"), QStringLiteral("g1")},
                        {QStringLiteral("title"), QStringLiteral("Homebrew")},
                        {QStringLiteral("system"), QStringLiteral("nds")},
                        {QStringLiteral("rom"), romJson(sha64('a'), 100, QStringLiteral("a.nds"), true)},
                        {QStringLiteral("roms"), QJsonArray{romJson(sha64('a'), 100, QStringLiteral("a.nds"), true),
                                                            romJson(sha64('b'), 90, QStringLiteral("a-trimmed.nds"), false)}},
                        {QStringLiteral("on_hub"), true},
                        {QStringLiteral("game_code"), QStringLiteral("ABCD")},
                        {QStringLiteral("game_version"), 2}};
    const auto game = parseGame(g);
    QVERIFY(game);
    QVERIFY(game->onHub);
    QCOMPARE(game->roms.size(), 2);
    QVERIFY(game->roms.at(0).stored);
    QVERIFY(!game->roms.at(1).stored);
    QCOMPARE(game->gameCode, QStringLiteral("ABCD"));
    QCOMPARE(game->gameVersion, 2);
    QVERIFY(game->hasRom(sha64('b')));
    QVERIFY(!game->hasRom(sha64('c')));

    // Local-only game
    QJsonObject local = g;
    local.insert(QStringLiteral("on_hub"), false);
    local.insert(QStringLiteral("rom"), romJson(sha64('a'), 100, QStringLiteral("a.nds"), false));
    QVERIFY(!parseGame(local)->onHub);
  }

  void parseGameFromOldHubAssumesOnHub() {
    const QJsonObject g{{QStringLiteral("id"), QStringLiteral("g1")},
                        {QStringLiteral("title"), QStringLiteral("Old")},
                        {QStringLiteral("system"), QStringLiteral("nds")},
                        {QStringLiteral("rom"), romJson(sha64('a'), 100, QStringLiteral("a.nds"))}};
    const auto game = parseGame(g);
    QVERIFY(game);
    QVERIFY(game->onHub);
    QCOMPARE(game->roms.size(), 1);
    QVERIFY(game->roms.first().stored);
    QCOMPARE(game->roms.first().sha256, sha64('a'));
    QVERIFY(game->gameCode.isEmpty());
    QCOMPARE(game->gameVersion, -1);
  }

  // ------------------------------------------------------------ launch source

  void launchSourceSelection() {
    GameEntry g;
    g.id = QStringLiteral("g1");
    g.romSha256 = sha64('a');
    g.onHub = true;
    g.roms = {{sha64('a'), 100, QStringLiteral("a.nds"), true}, {sha64('b'), 90, QStringLiteral("b.nds"), false}};
    QHash<QString, QString> local;
    const auto lookup = [&](const QString& sha) { return local.value(sha); };

    QCOMPARE(chooseRomSource(g, lookup, false).source, RomSource::Download);
    QCOMPARE(chooseRomSource(g, lookup, true).source, RomSource::Cache);
    // A local file wins over the cache; any of the game's hashes counts
    local.insert(sha64('b'), QStringLiteral("/roms/b.nds"));
    auto c = chooseRomSource(g, lookup, true);
    QCOMPARE(c.source, RomSource::Local);
    QCOMPARE(c.localPath, QStringLiteral("/roms/b.nds"));
    // The primary file is preferred when both exist
    local.insert(sha64('a'), QStringLiteral("/roms/a.nds"));
    QCOMPARE(chooseRomSource(g, lookup, false).localPath, QStringLiteral("/roms/a.nds"));
    // Local-only game: no local file, no cache -> missing (not a download)
    g.onHub = false;
    local.clear();
    QCOMPARE(chooseRomSource(g, lookup, false).source, RomSource::Missing);
    // An unrelated local hash does not match
    local.insert(sha64('c'), QStringLiteral("/roms/c.nds"));
    QCOMPARE(chooseRomSource(g, lookup, false).source, RomSource::Missing);
  }

  // ------------------------------------------------------------ scanner + registration

  void scanRegistersNewFilesAndSkipsKnownOnes() {
    pairAndConnect();
    const QByteArray known = ndsRom("KNWN", 0, 'k');
    const QByteArray fresh = ndsRom("NEWG", 3, 'n');
    writeTemp(QStringLiteral("roms/known.nds"), known);
    writeTemp(QStringLiteral("roms/sub/Fresh Game.nds"), fresh);
    writeTemp(QStringLiteral("roms/readme.txt"), "ignore");

    hub_->games = QJsonObject{{QStringLiteral("games"),
                               QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("g1")},
                                                      {QStringLiteral("title"), QStringLiteral("Known")},
                                                      {QStringLiteral("system"), QStringLiteral("nds")},
                                                      {QStringLiteral("rom"), romJson(shaOf(known), known.size(), QStringLiteral("known.nds"), true)},
                                                      {QStringLiteral("on_hub"), true}}}}};
    HubLibrary library(conn_.get());
    QSignalSpy loaded(&library, &HubLibrary::loaded);
    library.reload();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);

    LocalRomIndex index(dir_->filePath(QStringLiteral("data/hashes.json")));
    LocalScanOptions opt;
    opt.folders = {dir_->filePath(QStringLiteral("roms"))};
    opt.extensionToSystem = {{QStringLiteral(".nds"), QStringLiteral("nds")}};
    opt.identity = [](const QString& path, const QString& system) {
      const auto id = emu::readGameIdentity(path, system);
      return LocalRomIdentity{id.code, id.version};
    };
    QSignalSpy scanned(&index, &LocalRomIndex::scanFinished);
    QVERIFY(index.rescan(opt));
    QTRY_COMPARE_WITH_TIMEOUT(scanned.count(), 1, 5000);
    QCOMPARE(index.files().size(), 2);

    const QList<LocalRomFile> todo = RomRegistrar::filesToRegister(index.files(), library.games(), {});
    QCOMPARE(todo.size(), 1);  // the known file is already on the Hub
    RomRegistrar reg(conn_.get());
    QVERIFY(reg.supported());
    QSignalSpy done(&reg, &RomRegistrar::batchFinished);
    QSignalSpy outcomes(&reg, &RomRegistrar::registered);
    reg.registerFiles(todo);
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
    QCOMPARE(done.first().at(0).toInt(), 1);
    QCOMPARE(hub_->registerBodies.size(), 1);
    const QJsonObject body = hub_->registerBodies.first();
    QCOMPARE(body.value(QStringLiteral("sha256")).toString(), shaOf(fresh));
    QCOMPARE(body.value(QStringLiteral("filename")).toString(), QStringLiteral("Fresh Game.nds"));
    QCOMPARE(body.value(QStringLiteral("size")).toVariant().toLongLong(), qint64(fresh.size()));
    QCOMPARE(body.value(QStringLiteral("system")).toString(), QStringLiteral("nds"));
    QCOMPARE(body.value(QStringLiteral("game_code")).toString(), QStringLiteral("NEWG"));
    QCOMPARE(body.value(QStringLiteral("game_version")).toInt(), 3);
    const auto out = outcomes.first().first().value<RegisterOutcome>();
    QVERIFY(out.ok);
    QVERIFY(out.created);
    QVERIFY(!out.game.onHub);

    // The library now lists the local-only game; a second pass registers nothing
    library.reload();
    QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
    QCOMPARE(library.games().size(), 2);
    QVERIFY(RomRegistrar::filesToRegister(index.files(), library.games(), {}).isEmpty());
    // A file the user removed from the library stays out
    QCOMPARE(RomRegistrar::filesToRegister(index.files(), {}, {shaOf(fresh)}).size(), 1);

    // Remove from library
    QSignalSpy gone(&reg, &RomRegistrar::unregistered);
    reg.unregisterGame(out.game.id);
    QTRY_COMPARE_WITH_TIMEOUT(gone.count(), 1, 5000);
    QVERIFY(gone.first().at(1).toBool());
    QCOMPARE(hub_->unregisteredGames, QStringList{out.game.id});
  }

  void registrarIsInertWithoutFeature() {
    hub_->features = {QStringLiteral("saves_v1")};
    pairAndConnect();
    RomRegistrar reg(conn_.get());
    QVERIFY(!reg.supported());
    reg.registerFiles({LocalRomFile{QStringLiteral("/x/a.nds"), 10, 1, sha64('a'), QStringLiteral("nds"), {}, -1}});
    QVERIFY(!reg.busy());
    QCOMPARE(hub_->registerBodies.size(), 0);
  }

  // ------------------------------------------------------------ resumable upload

  void resumableUploadSendsChunksWithContentRange() {
    pairAndConnect();
    hub_->uploadChunkSize = 1000;
    const QByteArray rom = ndsRom("UPLD", 0, 'u', 2500);
    const QString path = writeTemp(QStringLiteral("up.nds"), rom);
    GameUploader up(conn_.get());
    up.setStateFileProvider([this]() { return stateFile(); });
    QSignalSpy finished(&up, &GameUploader::finished);
    QVERIFY(up.upload(path, QStringLiteral("My Game")));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const auto res = finished.first().first().value<UploadResult>();
    QCOMPARE(res.kind, UploadResult::Kind::Created);
    QCOMPARE(res.game.title, QStringLiteral("My Game"));
    QVERIFY(res.game.onHub);
    QCOMPARE(res.game.romSha256, shaOf(rom));
    QCOMPARE(hub_->putRanges, (QStringList{QStringLiteral("bytes 0-999/2500"), QStringLiteral("bytes 1000-1999/2500"),
                                           QStringLiteral("bytes 2000-2499/2500")}));
    QCOMPARE(hub_->roms.value(shaOf(rom)), rom);
    QCOMPARE(hub_->count(QStringLiteral("/api/v1/games?")), 0);  // not the one-shot upload
    QVERIFY(!up.busy());
    // The persisted id is gone after the upload finished
    QFile f(stateFile());
    if (f.open(QIODevice::ReadOnly)) QVERIFY(!f.readAll().contains(shaOf(rom).toLatin1()));
  }

  void resumableUploadResumesFromServerOffsetAfterRestart() {
    pairAndConnect();
    hub_->uploadChunkSize = 1000;
    hub_->failPutNumber = 2;  // the second chunk fails: 1000 bytes are on the Hub
    const QByteArray rom = ndsRom("RSME", 1, 'r', 3500);
    const QString path = writeTemp(QStringLiteral("resume.nds"), rom);
    {
      GameUploader up(conn_.get());
      up.setStateFileProvider([this]() { return stateFile(); });
      QSignalSpy finished(&up, &GameUploader::finished);
      QVERIFY(up.upload(path));
      QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
      const auto res = finished.first().first().value<UploadResult>();
      QCOMPARE(res.kind, UploadResult::Kind::Failed);
      QCOMPARE(hub_->uploadSessions.size(), 1);
      QCOMPARE(hub_->uploadSessions.first().data.size(), 1000);
    }
    QFile st(stateFile());
    QVERIFY(st.open(QIODevice::ReadOnly));
    QVERIFY(st.readAll().contains(shaOf(rom).toLatin1()));  // upload id persisted per sha256
    st.close();

    // "Restart": a new uploader (new process) with the same state file resumes at the Hub's offset
    hub_->putRanges.clear();
    hub_->failPutNumber = -1;
    GameUploader again(conn_.get());
    again.setStateFileProvider([this]() { return stateFile(); });
    QSignalSpy finished(&again, &GameUploader::finished);
    QVERIFY(again.upload(path));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const auto res = finished.first().first().value<UploadResult>();
    QCOMPARE(res.kind, UploadResult::Kind::Created);
    QCOMPARE(hub_->uploadSessionsCreated, 1);  // same session, not a new one
    // Resumed through GET /uploads/{id} (no chunk_size there): the rest goes in default-sized chunks, starting at the offset
    QCOMPARE(hub_->putRanges, QStringList{QStringLiteral("bytes 1000-3499/3500")});
    QCOMPARE(hub_->roms.value(shaOf(rom)), rom);
  }

  void resumableUploadOfLocalOnlyGameKeepsGameId() {
    pairAndConnect();
    const QByteArray rom = ndsRom("LOCL", 0, 'l', 1500);
    const QString path = writeTemp(QStringLiteral("local.nds"), rom);
    RomRegistrar reg(conn_.get());
    LocalRomFile f{path, rom.size(), 1, shaOf(rom), QStringLiteral("nds"), QStringLiteral("LOCL"), 0};
    QSignalSpy outcomes(&reg, &RomRegistrar::registered);
    reg.registerFiles({f});
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.count(), 1, 5000);
    const QString gameId = outcomes.first().first().value<RegisterOutcome>().game.id;

    GameUploader up(conn_.get());
    QSignalSpy finished(&up, &GameUploader::finished);
    QVERIFY(up.upload(path, QString(), f.sha256, QStringLiteral("nds")));  // hash known from the index: not computed again
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const auto res = finished.first().first().value<UploadResult>();
    QCOMPARE(res.kind, UploadResult::Kind::Created);
    QCOMPARE(res.game.id, gameId);
    QVERIFY(res.game.onHub);
  }

  void resumableUploadReportsDuplicate() {
    pairAndConnect();
    const QByteArray rom = ndsRom("DUPL", 0, 'd', 1200);
    hub_->games = QJsonObject{{QStringLiteral("games"),
                               QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("g9")},
                                                      {QStringLiteral("title"), QStringLiteral("Dup")},
                                                      {QStringLiteral("system"), QStringLiteral("nds")},
                                                      {QStringLiteral("rom"), romJson(shaOf(rom), rom.size(), QStringLiteral("d.nds"), true)},
                                                      {QStringLiteral("on_hub"), true}}}}};
    GameUploader up(conn_.get());
    QSignalSpy finished(&up, &GameUploader::finished);
    QVERIFY(up.upload(writeTemp(QStringLiteral("d.nds"), rom)));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const auto res = finished.first().first().value<UploadResult>();
    QCOMPARE(res.kind, UploadResult::Kind::Duplicate);
    QCOMPARE(res.existingGameId, QStringLiteral("g9"));
    QVERIFY(hub_->putRanges.isEmpty());
  }
};

QTEST_GUILESS_MAIN(LocalRomsTest)
#include "local_roms_test.moc"
