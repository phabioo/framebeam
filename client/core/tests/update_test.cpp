// Updater unit tests: SemVer, index validation, selection, protocol compatibility, signature (throwaway keys),
// install root, installer command line, download/stage/apply with file:// index and installer.
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include <openssl/evp.h>

#include "installroot.h"
#include "playersettings.h"
#include "semver.h"
#include "updater.h"
#include "updateindex.h"
#include "updatesig.h"

using namespace framebeam::update;

namespace {

struct KeyPair {
  QByteArray seed;
  QByteArray pub;
};

KeyPair makeKey() {
  KeyPair k;
  k.seed.resize(32);
  for (int i = 0; i < 32; ++i) k.seed[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
  EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                reinterpret_cast<const unsigned char*>(k.seed.constData()), 32);
  size_t len = 32;
  k.pub.resize(32);
  EVP_PKEY_get_raw_public_key(pkey, reinterpret_cast<unsigned char*>(k.pub.data()), &len);
  EVP_PKEY_free(pkey);
  return k;
}

QByteArray signLine(const KeyPair& k, const QByteArray& msg) {
  EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                reinterpret_cast<const unsigned char*>(k.seed.constData()), 32);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey);
  size_t siglen = 64;
  QByteArray sig(64, 0);
  EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &siglen,
                 reinterpret_cast<const unsigned char*>(msg.constData()), static_cast<size_t>(msg.size()));
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pkey);
  return (QStringLiteral("ed25519 ") + keyId(k.pub) + QLatin1Char(' ') + QString::fromLatin1(sig.toBase64()) + QLatin1Char('\n')).toLatin1();
}

QJsonObject artifact(const QString& name, const QByteArray& data, const QString& url, const QString& kind = QStringLiteral("installer")) {
  return {{"platform", "windows-x64"}, {"kind", kind}, {"name", name}, {"size", data.size()},
          {"sha256", QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}, {"url", url}};
}

QJsonObject release(const QString& product, const QString& channel, const QString& version, const QJsonArray& arts,
                    int proto = 1, int minProto = 1) {
  return {{"product", product}, {"channel", channel}, {"version", version},
          {"commit", "0123456789abcdef0123456789abcdef01234567"}, {"published_at", "2026-10-07T10:00:00Z"},
          {"notes_url", "https://github.com/phabioo/framebeam/releases/tag/v" + version},
          {"protocol_version", proto}, {"min_protocol_version", minProto}, {"artifacts", arts}, {"future_field", 42}};
}

QByteArray indexJson(const QJsonArray& releases) {
  return QJsonDocument(QJsonObject{{"schema", 1}, {"generated_at", "2026-10-07T10:00:00Z"}, {"releases", releases}})
      .toJson(QJsonDocument::Compact);
}

QJsonArray oneArt(const QString& v = QStringLiteral("x")) {
  return {artifact("setup-" + v + ".exe", QByteArray("data-") + v.toUtf8(), "https://example.invalid/setup-" + v + ".exe")};
}

Index parseOk(const QByteArray& json) {
  const ParseResult r = parseIndex(json);
  if (!r.ok) qWarning() << r.error;
  return r.index;
}

bool writeBytes(const QString& path, const QByteArray& b) {
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}

}  // namespace

class UpdateTest : public QObject {
  Q_OBJECT

 private slots:
  // ---------------------------------------------------------------- SemVer
  void semverPrecedence() {
    const QStringList ordered = {"0.3.0-dev", "0.3.0-test.9", "0.3.0-test.10", "0.3.0-test.10.1", "0.3.0-test.a",
                                 "0.3.0", "0.3.1", "0.10.0", "1.0.0-alpha", "1.0.0"};
    for (int i = 0; i + 1 < ordered.size(); ++i) {
      QVERIFY2(compareVersions(ordered[i], ordered[i + 1]).value() < 0, qPrintable(ordered[i] + " < " + ordered[i + 1]));
      QVERIFY(compareVersions(ordered[i + 1], ordered[i]).value() > 0);
    }
  }
  void semverBuildMetadataIgnored() {
    QCOMPARE(compareVersions("1.2.3+abc", "1.2.3+def").value(), 0);
    QCOMPARE(SemVer::parse("1.2.3-rc.1+build")->toString(), QString("1.2.3-rc.1"));
  }
  void semverRejectsInvalid() {
    for (const char* v : {"", "dev", "1.2", "1.2.3.4", "01.2.3", "1.2.3-", "1.2.3-01", "v1.2.3", "1.2.3-a..b", " 1.2.3"}) {
      QVERIFY2(!SemVer::parse(QString::fromLatin1(v)), v);
    }
    QVERIFY(!compareVersions("dev", "1.0.0"));
  }

  // ---------------------------------------------------------------- index
  void indexValid() {
    const ParseResult r = parseIndex(indexJson({release("player", "beta", "0.3.0-test.5", oneArt()),
                                                release("hub", "stable", "0.3.0", oneArt())}));
    QVERIFY(r.ok);
    QCOMPARE(r.index.releases.size(), 2);
    QVERIFY(r.index.skipped.isEmpty());  // unknown field "future_field" is ignored
  }
  void indexUnknownSchema() {
    const QByteArray j = R"({"schema":2,"releases":[]})";
    const ParseResult r = parseIndex(j);
    QVERIFY(!r.ok);
    QVERIFY(!parseIndex("not json").ok);
    QVERIFY(!parseIndex(R"({"schema":1})").ok);
  }
  void indexDuplicateRejectsAll() {
    const ParseResult r = parseIndex(indexJson({release("player", "beta", "0.3.0-test.5", oneArt()),
                                                release("player", "beta", "0.3.0-test.5+x", oneArt())}));
    QVERIFY(!r.ok);
    QVERIFY(r.index.releases.isEmpty());
    // same version in different channel/product is fine
    QVERIFY(parseIndex(indexJson({release("player", "beta", "0.3.0", oneArt()), release("player", "stable", "0.3.0", oneArt()),
                                  release("hub", "stable", "0.3.0", oneArt())})).ok);
  }
  void indexInvalidEntriesSkipped() {
    QJsonObject badSha = release("player", "beta", "0.3.0-test.1", oneArt());
    QJsonArray arts{artifact("a.exe", "x", "https://e.invalid/a.exe")};
    QJsonObject a0 = arts[0].toObject();
    a0["sha256"] = "ABC";
    badSha["artifacts"] = QJsonArray{a0};
    QJsonObject httpUrl = release("player", "beta", "0.3.0-test.2", QJsonArray{artifact("a.exe", "x", "http://e.invalid/a.exe")});
    QJsonObject badVer = release("player", "beta", "latest", oneArt());
    QJsonObject badProduct = release("tool", "test", "0.3.0-test.3", oneArt());
    QJsonObject testChannel = release("player", "test", "0.3.0-test.10", oneArt());
    QJsonObject badChannel = release("player", "dev", "0.3.0-test.4", oneArt());
    QJsonObject badProto = release("player", "beta", "0.3.0-test.6", oneArt(), 1, 2);
    QJsonObject noArt = release("player", "beta", "0.3.0-test.7", {});
    QJsonObject badPath = release("player", "beta", "0.3.0-test.8", QJsonArray{artifact("../evil.exe", "x", "https://e.invalid/a.exe")});
    QJsonObject good = release("player", "beta", "0.3.0-test.9", oneArt());
    const ParseResult r = parseIndex(indexJson({badSha, httpUrl, badVer, badProduct, badChannel, badProto, noArt, badPath, testChannel, good}));
    QVERIFY(r.ok);
    QCOMPARE(r.index.releases.size(), 1);
    QCOMPARE(r.index.skipped.size(), 9);  // "test" is no valid channel any more
    QCOMPARE(r.index.releases[0].version, QString("0.3.0-test.9"));
  }
  void indexFileUrlOnlyWhenAllowed() {
    const QByteArray j = indexJson({release("player", "beta", "0.3.0-test.1", QJsonArray{artifact("a.exe", "x", "file:///tmp/a.exe")})});
    QCOMPARE(parseIndex(j, false).index.releases.size(), 0);
    QCOMPARE(parseIndex(j, true).index.releases.size(), 1);
  }

  // ---------------------------------------------------------------- selection
  void selectionChannels() {
    const Index idx = parseOk(indexJson({release("player", "beta", "0.3.0-test.7", oneArt("t7")),
                                         release("player", "stable", "0.3.0", oneArt("s")),
                                         release("player", "beta", "0.4.0-test.1", oneArt("t41"))}));
    SelectionInput in;
    in.currentVersion = "0.3.0-dev";
    in.channel = Channel::Stable;
    Selection s = selectRelease(idx, in);
    QCOMPARE(s.status, Selection::Status::Available);
    QCOMPARE(s.release.version, QString("0.3.0"));  // stable never sees test
    in.channel = Channel::Beta;
    s = selectRelease(idx, in);
    QCOMPARE(s.release.version, QString("0.4.0-test.1"));  // highest of test + stable
    QCOMPARE(s.artifact.name, QString("setup-t41.exe"));
  }
  void selectionDevIsOff() {
    const Index idx = parseOk(indexJson({release("player", "stable", "0.3.0", oneArt())}));
    SelectionInput in;
    in.currentVersion = "0.3.0-dev";
    in.channel = channelFromCompiled("dev");
    QCOMPARE(in.channel, Channel::Off);
    QCOMPARE(selectRelease(idx, in).status, Selection::Status::Disabled);
    in.channel = Channel::Stable;
    in.currentVersion = "dev";  // not SemVer: updater disabled
    QCOMPARE(selectRelease(idx, in).status, Selection::Status::Disabled);
  }
  void selectionNoDowngradeAndEqual() {
    const Index idx = parseOk(indexJson({release("player", "stable", "0.3.0", oneArt()), release("player", "beta", "0.3.1-test.2", oneArt("b"))}));
    SelectionInput in;
    in.channel = Channel::Beta;
    in.currentVersion = "0.3.0";
    QCOMPARE(selectRelease(idx, in).release.version, QString("0.3.1-test.2"));
    in.currentVersion = "0.3.1-test.2";
    QCOMPARE(selectRelease(idx, in).status, Selection::Status::UpToDate);
    in.currentVersion = "0.5.0";  // running newer than everything: no automatic downgrade
    QCOMPARE(selectRelease(idx, in).status, Selection::Status::UpToDate);
    in.currentVersion = "0.3.1-test.2+local";  // build metadata ignored
    QCOMPARE(selectRelease(idx, in).status, Selection::Status::UpToDate);
  }
  void selectionProductPlatformKind() {
    QJsonArray zipOnly{artifact("p.zip", "z", "https://e.invalid/p.zip", "zip")};
    QJsonObject wrongPlatform = artifact("x.exe", "x", "https://e.invalid/x.exe");
    wrongPlatform["platform"] = "linux-amd64";
    const Index idx = parseOk(indexJson({release("hub", "stable", "9.0.0", oneArt()),
                                         release("player", "stable", "0.4.0", zipOnly),
                                         release("player", "stable", "0.5.0", QJsonArray{wrongPlatform})}));
    SelectionInput in;
    in.channel = Channel::Stable;
    in.currentVersion = "0.3.0";
    QCOMPARE(selectRelease(idx, in).status, Selection::Status::UpToDate);  // no installer for windows-x64
  }
  void protocolCompatibility() {
    Release r;
    r.protocolVersion = 2;
    r.minProtocolVersion = 2;
    QVERIFY(protocolCompatible(r, std::nullopt));  // no Hub known
    QVERIFY(!protocolCompatible(r, HubProtocol{1, 1}));  // Hub older than candidate's min
    r.minProtocolVersion = 1;
    QVERIFY(protocolCompatible(r, HubProtocol{1, 1}));
    QVERIFY(protocolCompatible(r, HubProtocol{2, 1}));
    r.protocolVersion = 1;
    QVERIFY(!protocolCompatible(r, HubProtocol{3, 2}));  // candidate older than the Hub's min
  }
  void selectionIncompatibleIsShownNotChosen() {
    const Index idx = parseOk(indexJson({release("player", "stable", "0.4.0", oneArt("a"), 3, 3),
                                         release("player", "stable", "0.3.5", oneArt("b"), 1, 1)}));
    SelectionInput in;
    in.channel = Channel::Stable;
    in.currentVersion = "0.3.0";
    in.hub = HubProtocol{1, 1};
    Selection s = selectRelease(idx, in);
    QCOMPARE(s.status, Selection::Status::Available);
    QCOMPARE(s.release.version, QString("0.3.5"));  // compatible one wins although lower
    const Index only = parseOk(indexJson({release("player", "stable", "0.4.0", oneArt("a"), 3, 3)}));
    s = selectRelease(only, in);
    QCOMPARE(s.status, Selection::Status::Incompatible);
    QCOMPARE(s.release.version, QString("0.4.0"));
    const QJsonObject j = selectionToJson(s, in, {});
    QCOMPARE(j["status"].toString(), QString("incompatible"));
    QCOMPARE(j["release"].toObject()["version"].toString(), QString("0.4.0"));
  }

  // ---------------------------------------------------------------- signature
  void signatureGoodWrongTampered() {
    const KeyPair k = makeKey();
    const KeyPair other = makeKey();
    const QByteArray idx = indexJson({release("player", "stable", "0.3.0", oneArt())});
    const QByteArray sig = signLine(k, idx);
    QVERIFY(verifyIndexSignature(idx, sig, {k.pub}).ok);
    QVERIFY(verifyIndexSignature(idx, sig, {other.pub, k.pub}).ok);
    // wrong key
    SigCheck c = verifyIndexSignature(idx, sig, {other.pub});
    QVERIFY(!c.ok);
    QVERIFY(c.error.contains("not trusted"));
    // same key id but different key material cannot happen; a signature made with another key but claiming k's id fails
    QByteArray forged = signLine(other, idx);
    forged.replace(keyId(other.pub).toLatin1(), keyId(k.pub).toLatin1());
    QVERIFY(!verifyIndexSignature(idx, forged, {k.pub}).ok);
    // tampered bytes
    QByteArray bad = idx;
    bad[bad.size() / 2] = static_cast<char>(bad[bad.size() / 2] ^ 1);
    c = verifyIndexSignature(bad, sig, {k.pub});
    QVERIFY(!c.ok);
    QVERIFY(c.error.contains("does not match"));
    // formats
    QVERIFY(!verifyIndexSignature(idx, "garbage", {k.pub}).ok);
    QVERIFY(!verifyIndexSignature(idx, sig + sig, {k.pub}).ok);
    QVERIFY(!verifyIndexSignature(idx, sig, {}).ok);
    QVERIFY(!verifyIndexSignature(idx, QByteArray("rsa ") + sig.mid(8), {k.pub}).ok);
  }
  void trustedKeys() {
    const QList<QByteArray> d = defaultTrustedKeys();
    QCOMPARE(d.size(), 1);
    QCOMPARE(d[0].size(), 32);
    QCOMPARE(keyId(d[0]).size(), 16);
    QString err;
    QCOMPARE(parsePublicKeys({"AAAA", ""}, &err).size(), 0);
    QVERIFY(!err.isEmpty());
  }

  // ---------------------------------------------------------------- install root / installer command
  void installRootBinLayout() {
    QTemporaryDir t;
    QVERIFY(QDir().mkpath(t.filePath("app/bin")));
    // bin without launcher in the parent: stays flat
    QCOMPARE(installRootFor(t.filePath("app/bin")), QDir(t.filePath("app/bin")).absolutePath());
    QVERIFY(writeBytes(t.filePath("app/framebeam_player.exe"), "x"));
    QCOMPARE(installRootFor(t.filePath("app/bin")), QDir(t.filePath("app")).absolutePath());
    QCOMPARE(installRootFor(t.filePath("app")), QDir(t.filePath("app")).absolutePath());
    QVERIFY(QDir().mkpath(t.filePath("app/tools")));
    QCOMPARE(installRootFor(t.filePath("app/tools")), QDir(t.filePath("app/tools")).absolutePath());
    QVERIFY(!hasUninstaller(t.filePath("app")));
    QVERIFY(writeBytes(t.filePath("app/unins000.exe"), "x"));
    QVERIFY(hasUninstaller(t.filePath("app")));
    QVERIFY(directoryWritable(t.filePath("app")));
    QVERIFY(!directoryWritable(t.filePath("nope")));
  }
  void installerCommandLine() {
    InstallerCommand c = installerCommand("C:/x/setup.exe", true);
    QCOMPARE(c.program, QString("C:/x/setup.exe"));
    QCOMPARE(c.args, (QStringList{"/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/UPDATE", "/CURRENTUSER"}));
    c = installerCommand("C:/x/setup.exe", false);
    QCOMPARE(c.args.last(), QString("/ALLUSERS"));
    QVERIFY(!c.args.contains("/CURRENTUSER"));
  }

  // ---------------------------------------------------------------- settings
  void channelAliasAndOldSettings() {
    QCOMPARE(parseChannel("test"), std::optional<Channel>(Channel::Beta));
    QCOMPARE(parseChannel("beta"), std::optional<Channel>(Channel::Beta));
    QCOMPARE(channelName(Channel::Beta), QString("beta"));
    QCOMPARE(channelFromCompiled("test"), Channel::Beta);
    QTemporaryDir t;
    QVERIFY(QDir().mkpath(t.filePath("settings")));
    QVERIFY(writeBytes(t.filePath("settings/player.json"), R"({"update_channel":"test","other":1})"));
    framebeam::PlayerSettings s(t.path());
    QCOMPARE(s.updateChannel(), QString("beta"));
    QVERIFY(s.setUpdateAutoInstall(true));  // any save rewrites the old value
    QJsonObject o;
    {
      // Closed before the next save: on Windows QSaveFile cannot replace an open file.
      QFile f(t.filePath("settings/player.json"));
      QVERIFY(f.open(QIODevice::ReadOnly));
      o = QJsonDocument::fromJson(f.readAll()).object();
    }
    QCOMPARE(o["update_channel"].toString(), QString("beta"));
    QCOMPARE(o["other"].toInt(), 1);
    QVERIFY(s.setUpdateChannel("test"));
    QCOMPARE(s.updateChannel(), QString("beta"));
  }
  void settingsRoundTrip() {
    QTemporaryDir t;
    {
      framebeam::PlayerSettings s(t.path());
      QCOMPARE(s.updateChannel(), QString());
      QVERIFY(!s.updateAutoInstall());
      QVERIFY(!s.setUpdateChannel("dev"));
      QVERIFY(s.setUpdateChannel("beta"));
      QVERIFY(s.setUpdateAutoInstall(false));
    }
    framebeam::PlayerSettings s(t.path());
    QCOMPARE(s.updateChannel(), QString("beta"));
    QCOMPARE(s.updateAutoInstall(), std::optional<bool>(false));
    QVERIFY(s.setUpdateChannel(""));
    QCOMPARE(framebeam::PlayerSettings(t.path()).updateChannel(), QString());
  }

  // ---------------------------------------------------------------- download / stage / apply
  void managerStagesAndApplies() {
    Env env;
    const QByteArray data = "installer-bytes-0.3.1";
    env.publish(data, "0.3.1-test.3", /*sizeOverride*/ -1, /*shaOverride*/ QString());
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateChannel("beta");
    QStringList launched;
    auto mp = env.manager(settings);
    UpdateManager& m = *mp;
    m.setLauncher([&](const QString& prog, const QStringList& args) { launched << prog << args.join(' '); return true; });
    QSignalSpy quit(&m, &UpdateManager::quitRequested);
    m.checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Ready, 5000);  // test + auto: staged in the background
    QVERIFY(launched.isEmpty());  // not applied during the running session
    const QString staged = QDir(stagingRoot(env.dir.filePath("data"))).filePath("0.3.1-test.3/" + env.installerName);
    QVERIFY(QFileInfo::exists(staged));
    QCOMPARE(m.availableVersion(), QString("0.3.1-test.3"));

    // Next start: a verified staged installer is applied before the main window.
    auto m2p = env.manager(settings);
    UpdateManager& m2 = *m2p;
    m2.setLauncher([&](const QString& prog, const QStringList& args) { launched << prog << args.join(' '); return true; });
    QSignalSpy quit2(&m2, &UpdateManager::quitRequested);
    QVERIFY(m2.applyStagedAtStart());
    QCOMPARE(quit2.count(), 1);
    QCOMPARE(launched.size(), 2);
    QCOMPARE(launched[0], staged);
    QVERIFY(launched[1].startsWith("/SILENT /SUPPRESSMSGBOXES /NORESTART /UPDATE /"));
  }
  void managerBusyBlocksApply() {
    Env env;
    env.publish("installer-bytes", "0.3.1-test.3");
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateChannel("beta");
    int launches = 0;
    bool busy = true;
    auto mp = env.manager(settings);
    UpdateManager& m = *mp;
    m.setLauncher([&](const QString&, const QStringList&) { ++launches; return true; });
    m.setBusyProvider([&]() { return busy; });
    m.checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Ready, 5000);
    QVERIFY(!m.applyStagedAtStart());
    m.installNow();
    QCOMPARE(launches, 0);
    QVERIFY(m.statusText().contains("game"));
    busy = false;
    m.installNow();
    QCOMPARE(launches, 1);
  }
  void managerStableNeedsConfirmation() {
    Env env;
    env.publish("installer-bytes", "0.3.1", -1, QString(), "stable");
    framebeam::PlayerSettings settings(env.dir.filePath("data"));  // compiled channel stable
    int launches = 0;
    auto mp = env.manager(settings, "stable");
    UpdateManager& m = *mp;
    m.setLauncher([&](const QString&, const QStringList&) { ++launches; return true; });
    QVERIFY(!m.autoInstallEffective());
    m.checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Available, 5000);
    QVERIFY(!QDir(stagingRoot(env.dir.filePath("data"))).exists("0.3.1"));  // nothing downloaded without confirmation
    QVERIFY(!m.applyStagedAtStart());
    QCOMPARE(launches, 0);
    m.installNow();  // user confirmed
    QTRY_COMPARE_WITH_TIMEOUT(launches, 1, 5000);
    QCOMPARE(m.state(), UpdateManager::State::Applying);
  }
  void managerStableIgnoresAutoSettingAtStart() {
    Env env;
    env.publish("installer-bytes", "0.3.1", -1, QString(), "stable");
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateAutoInstall(true);  // stable never installs without confirmation
    auto mp = env.manager(settings, "stable");
    UpdateManager& m = *mp;
    QVERIFY(!m.autoInstallEffective());
    m.checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Available, 5000);
  }
  void managerRejectsSizeAndShaMismatch() {
    for (int mode = 0; mode < 2; ++mode) {
      Env env;
      env.publish("installer-bytes", "0.3.1-test.3", mode == 0 ? 5 : -1, mode == 1 ? QString(64, 'a') : QString());
      framebeam::PlayerSettings settings(env.dir.filePath("data"));
      settings.setUpdateChannel("beta");
      int launches = 0;
      auto mp = env.manager(settings);
    UpdateManager& m = *mp;
      m.setLauncher([&](const QString&, const QStringList&) { ++launches; return true; });
      m.checkNow();
      QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Error, 5000);
      QVERIFY(!QFileInfo::exists(QDir(stagingRoot(env.dir.filePath("data"))).filePath("0.3.1-test.3/" + env.installerName)));
      QVERIFY(!m.applyStagedAtStart());
      QCOMPARE(launches, 0);
    }
  }
  void managerRejectsBadIndexSignature() {
    Env env;
    env.publish("installer-bytes", "0.3.1-test.3");
    QByteArray idx = readAll(env.dir.filePath("updates-index.json"));
    idx.replace("0.3.1-test.3", "0.3.9-test.3");  // tampered after signing
    QVERIFY(writeBytes(env.dir.filePath("updates-index.json"), idx));
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateChannel("beta");
    auto mp = env.manager(settings);
    UpdateManager& m = *mp;
    m.checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Error, 5000);
    QVERIFY(m.statusText().contains("signature"));
  }
  // ---------------------------------------------------------------- resolved default channel
  void defaultChannelResolvesStableWhenPromoted() {
    Env env;
    env.publishPromoted("0.3.0-dev", true);
    {
      framebeam::PlayerSettings settings(env.dir.filePath("data"));
      auto mp = env.manager(settings);
      QCOMPARE(mp->effectiveChannel(), Channel::Beta);  // not resolved yet
      QSignalSpy spy(mp.get(), &UpdateManager::changed);
      mp->checkNow();
      QTRY_COMPARE_WITH_TIMEOUT(settings.updateChannelDefault(), QString("stable"), 5000);
      QCOMPARE(mp->effectiveChannel(), Channel::Stable);
      QVERIFY(!mp->autoInstallSetting());
      QVERIFY(settings.updateChannel().isEmpty());
    }
    framebeam::PlayerSettings reloaded(env.dir.filePath("data"));  // persisted
    QCOMPARE(reloaded.updateChannelDefault(), QString("stable"));
    QCOMPARE(env.manager(reloaded)->effectiveChannel(), Channel::Stable);
  }
  void defaultChannelResolvesBetaAndStaysAfterPromotion() {
    Env env;
    env.publishPromoted("0.3.0-dev", false);
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    auto mp = env.manager(settings);
    mp->checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(settings.updateChannelDefault(), QString("beta"), 5000);
    QCOMPARE(mp->effectiveChannel(), Channel::Beta);
    QVERIFY(mp->autoInstallSetting());
    env.publishPromoted("0.3.0-dev", true);  // promoted later: never re-resolved
    mp->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(!mp->busy() && mp->state() != UpdateManager::State::Checking, 5000);
    QCOMPARE(settings.updateChannelDefault(), QString("beta"));
    QCOMPARE(mp->effectiveChannel(), Channel::Beta);
  }
  void defaultChannelExplicitWins() {
    Env env;
    env.publishPromoted("0.3.0-dev", true);
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateChannel("beta");
    auto mp = env.manager(settings);
    mp->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(mp->state() != UpdateManager::State::Checking && mp->state() != UpdateManager::State::Idle, 5000);
    QVERIFY(settings.updateChannelDefault().isEmpty());  // not resolved at all
    QCOMPARE(mp->effectiveChannel(), Channel::Beta);
    // An already stored default is overridden by an explicit choice too.
    QVERIFY(settings.setUpdateChannelDefault("stable"));
    QCOMPARE(mp->effectiveChannel(), Channel::Beta);
    settings.setUpdateChannel("stable");
    QCOMPARE(mp->effectiveChannel(), Channel::Stable);
  }
  void defaultChannelIgnoredForStableAndDevBuilds() {
    Env env;
    env.publishPromoted("0.3.0-dev", true);
    for (const QString compiled : {QStringLiteral("stable"), QStringLiteral("dev")}) {
      framebeam::PlayerSettings settings(env.dir.filePath("data-" + compiled));
      auto mp = env.manager(settings, compiled);
      mp->checkNow();
      QTRY_VERIFY_WITH_TIMEOUT(mp->state() != UpdateManager::State::Checking && mp->state() != UpdateManager::State::Idle, 5000);
      QVERIFY(settings.updateChannelDefault().isEmpty());
      QCOMPARE(mp->effectiveChannel(), compiled == "stable" ? Channel::Stable : Channel::Off);
    }
    framebeam::PlayerSettings s2(env.dir.filePath("data-x"));
    QVERIFY(s2.setUpdateChannelDefault("beta"));
    QCOMPARE(env.manager(s2, "stable")->effectiveChannel(), Channel::Stable);
    QCOMPARE(env.manager(s2, "dev")->effectiveChannel(), Channel::Off);
    QVERIFY(!s2.setUpdateChannelDefault("bogus"));
  }
  void managerDevChannelIsOff() {
    Env env;
    env.publish("installer-bytes", "0.3.1-test.3");
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    auto mp = env.manager(settings, "dev");
    UpdateManager& m = *mp;
    QCOMPARE(m.effectiveChannel(), Channel::Off);
    m.checkNow();
    QCOMPARE(m.state(), UpdateManager::State::Disabled);
    QVERIFY(!m.applyStagedAtStart());
  }
  void stagedReplayAndTamperRejected() {
    Env env;
    env.publish("installer-bytes", "0.3.1-test.3");
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateChannel("beta");
    auto mp = env.manager(settings);
    UpdateManager& m = *mp;
    m.checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m.state(), UpdateManager::State::Ready, 5000);
    const QString base = env.dir.filePath("data");
    const QString inst = QDir(stagingRoot(base)).filePath("0.3.1-test.3/" + env.installerName);
    QVERIFY(QFileInfo::exists(inst));
    QVERIFY(loadVerifiedStaged(base, "0.3.0-dev", env.keys, true));
    QVERIFY(writeBytes(inst, "installer-byteX"));
    QVERIFY(!loadVerifiedStaged(base, "0.3.0-dev", env.keys, true));
  }
  void stagedReplayAndWrongKeyRejected() {
    Env env;
    env.publish("installer-bytes", "0.3.1-test.3");
    framebeam::PlayerSettings settings(env.dir.filePath("data"));
    settings.setUpdateChannel("beta");
    auto mp = env.manager(settings);
    mp->checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(mp->state(), UpdateManager::State::Ready, 5000);
    const QString base = env.dir.filePath("data");
    QVERIFY(!loadVerifiedStaged(base, "0.3.0-dev", {makeKey().pub}, true));  // wrong trust keys (also prunes)
    auto m2p = env.manager(settings);  // fresh manager (state Idle): stages again
    m2p->checkNow();
    QTRY_COMPARE_WITH_TIMEOUT(m2p->state(), UpdateManager::State::Ready, 5000);
    QVERIFY(loadVerifiedStaged(base, "0.3.0-dev", env.keys, true).has_value());
    // The staged version must be strictly newer than the running one (replay of an old signed release).
    QVERIFY(!loadVerifiedStaged(base, "0.3.1-test.3", env.keys, true));
    QVERIFY(!loadVerifiedStaged(base, "0.9.0", env.keys, true));
  }

 private:
  static QByteArray readAll(const QString& p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }

  // Throwaway key, signed file:// index and installer in a temp dir.
  struct Env {
    QTemporaryDir dir;
    KeyPair key = makeKey();
    QList<QByteArray> keys{key.pub};
    QString installerName;
    QString channel;

    void publish(const QByteArray& installer, const QString& version, qint64 sizeOverride = -1,
                 const QString& shaOverride = QString(), const QString& chan = QStringLiteral("beta")) {
      channel = chan;
      installerName = "framebeam-player-" + version + "-windows-x64-setup.exe";
      const QString path = dir.filePath(installerName);
      QVERIFY(writeBytes(path, installer));
      QJsonObject art = artifact(installerName, installer, QUrl::fromLocalFile(path).toString());
      if (sizeOverride >= 0) art["size"] = static_cast<double>(sizeOverride);
      if (!shaOverride.isEmpty()) art["sha256"] = shaOverride;
      const QByteArray idx = indexJson({release("player", chan, version, QJsonArray{art}),
                                        release("hub", "beta", "9.9.9-test.1", oneArt("hub"))});
      QVERIFY(writeBytes(dir.filePath("updates-index.json"), idx));
      QVERIFY(writeBytes(dir.filePath("updates-index.json.sig"), signLine(key, idx)));
    }
    void publishPromoted(const QString& version, bool stableListed) {
      publish("installer-bytes", version);
      QJsonArray rel{release("player", "beta", version, QJsonArray{}), release("hub", "beta", "9.9.9", oneArt("hub"))};
      if (stableListed) rel.append(release("player", "stable", version, oneArt("p")));
      const QByteArray idx = indexJson(rel);
      QVERIFY(writeBytes(dir.filePath("updates-index.json"), idx));
      QVERIFY(writeBytes(dir.filePath("updates-index.json.sig"), signLine(key, idx)));
    }
    std::unique_ptr<UpdateManager> manager(framebeam::PlayerSettings& s, const QString& compiled = QStringLiteral("beta")) {
      UpdateManager::Config c;
      c.baseDir = dir.filePath("data");
      c.indexUrl = QUrl::fromLocalFile(dir.filePath("updates-index.json")).toString();
      c.currentVersion = "0.3.0-dev";
      c.compiledChannel = compiled;
      c.trustedKeys = keys;
      c.installRoot = dir.path();
      c.forceApplySupport = true;
      return std::make_unique<UpdateManager>(c, &s);
    }
  };
};

QTEST_GUILESS_MAIN(UpdateTest)
#include "update_test.moc"
