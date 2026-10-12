// PluginHubClient against the fake Hub: feature gate plugins_v1, list parsing, download with the SHA-256 pin.
// Dummy bytes only (no real plugin).
#include <QCryptographicHash>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "credentialstore.h"
#include "fakehub.h"
#include "hubconnection.h"
#include "pluginhub.h"
#include "profilestore.h"

using namespace framebeam;
using State = HubConnection::State;

namespace {
QString shaOf(const QByteArray& d) { return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex()); }
const QByteArray kFile(4096, 'p');
const char* kId = "net.test.plugin";
}  // namespace

class PluginHubTest : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<ProfileStore> profiles_;
  std::unique_ptr<MemoryCredentialStore> creds_;
  std::unique_ptr<FakeHub> hub_;
  std::unique_ptr<HubConnection> conn_;
  std::unique_ptr<PluginHubClient> client_;

  void connectHub(const QStringList& features) {
    dir_ = std::make_unique<QTemporaryDir>();
    profiles_ = std::make_unique<ProfileStore>(dir_->filePath(QStringLiteral("data")));
    creds_ = std::make_unique<MemoryCredentialStore>();
    hub_ = std::make_unique<FakeHub>(QStringLiteral("a"));
    hub_->features = features;
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
    client_ = std::make_unique<PluginHubClient>(conn_.get());
  }

  static QJsonObject entry(const QString& version, const QByteArray& file, const QString& origin = QStringLiteral("unverified")) {
    return QJsonObject{{"plugin_id", kId},
                       {"version", version},
                       {"name", "Test Plugin"},
                       {"description", "A test"},
                       {"api", 1},
                       {"origin", origin},
                       {"sha256", shaOf(file)},
                       {"size", file.size()},
                       {"permissions", QJsonArray{"memory.read", "ui"}},
                       {"requires", QJsonObject{{"net.test.core", "^1.0"}}},
                       {"provides", QJsonArray{"test.iface"}},
                       {"games", QJsonArray{QJsonObject{{"system", "nds"}}}},
                       {"created_at", "2026-10-12T10:00:00Z"}};
  }

  PluginDownloadResult download(const QString& version, const QString& sha, qint64 size = 0) {
    QSignalSpy spy(client_.get(), &PluginHubClient::downloadFinished);
    client_->download(kId, version, sha, size);
    if (!spy.wait(8000) && spy.isEmpty()) {
      QTest::qFail("no result", __FILE__, __LINE__);
      return {};
    }
    return spy.takeFirst().at(0).value<PluginDownloadResult>();
  }

 private slots:
  void cleanup() {
    client_.reset();
    conn_.reset();
    hub_.reset();
    creds_.reset();
    profiles_.reset();
    dir_.reset();
  }

  void listIsParsed() {
    connectHub({QStringLiteral("saves_v1"), QStringLiteral("plugins_v1")});
    QVERIFY(client_->supported());
    hub_->pluginList.append(entry("1.0.0", kFile));
    hub_->pluginList.append(entry("1.1.0", kFile + "x", "official"));
    client_->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(client_->state() == PluginHubClient::State::Ready, 8000);
    QCOMPARE(client_->plugins().size(), 2);
    const HubPluginInfo p = client_->plugins().at(1);
    QCOMPARE(p.id, QString(kId));
    QCOMPARE(p.version, QString("1.1.0"));
    QCOMPARE(p.origin, QString("official"));
    QCOMPARE(p.name, QString("Test Plugin"));
    QCOMPARE(p.permissions, (QStringList{"memory.read", "ui"}));
    QCOMPARE(p.provides, QStringList{"test.iface"});
    QCOMPARE(p.requires_.value("net.test.core").toString(), QString("^1.0"));
    QCOMPARE(p.games.size(), 1);
    QCOMPARE(p.sha256, shaOf(kFile + "x"));
    QCOMPARE(p.size, kFile.size() + 1);
  }

  void withoutTheFeatureNothingIsRequested() {
    connectHub({QStringLiteral("saves_v1")});
    QVERIFY(!client_->supported());
    client_->refresh();
    QCOMPARE(client_->state(), PluginHubClient::State::Idle);
    const PluginDownloadResult r = download("1.0.0", shaOf(kFile));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QString("not_supported"));
    QCOMPARE(hub_->pluginListRequests, 0);
    QCOMPARE(hub_->pluginFileDownloads, 0);
  }

  void downloadChecksThePin() {
    connectHub({QStringLiteral("saves_v1"), QStringLiteral("plugins_v1")});
    hub_->pluginFiles.insert(QString(kId) + "/1.0.0", kFile);
    const PluginDownloadResult ok = download("1.0.0", shaOf(kFile), kFile.size());
    QVERIFY2(ok.ok, qPrintable(ok.problem));
    QCOMPARE(ok.bytes, kFile);
    // Upper case pin is the same hash
    QVERIFY(download("1.0.0", shaOf(kFile).toUpper()).ok);
    // Wrong pin: the bytes are not handed out
    const PluginDownloadResult bad = download("1.0.0", shaOf(kFile + "other"));
    QVERIFY(!bad.ok);
    QCOMPARE(bad.problem, QString("invalid_hash"));
    QVERIFY(bad.bytes.isEmpty());
  }

  void sizeMismatchIsRejected() {
    connectHub({QStringLiteral("saves_v1"), QStringLiteral("plugins_v1")});
    hub_->pluginFiles.insert(QString(kId) + "/1.0.0", kFile);
    const PluginDownloadResult r = download("1.0.0", shaOf(kFile), kFile.size() - 1);  // announced smaller
    QVERIFY(!r.ok);
    QVERIFY(r.bytes.isEmpty());
    const PluginDownloadResult r2 = download("1.0.0", shaOf(kFile), kFile.size() + 10);  // announced larger
    QVERIFY(!r2.ok);
    QCOMPARE(r2.problem, QString("invalid_size"));
  }

  void unknownVersionAndBadArguments() {
    connectHub({QStringLiteral("saves_v1"), QStringLiteral("plugins_v1")});
    const PluginDownloadResult r = download("9.9.9", shaOf(kFile));
    QVERIFY(!r.ok);
    QCOMPARE(r.problem, QString("not_on_hub"));
    // Never builds a request from a hostile id or version
    QSignalSpy spy(client_.get(), &PluginHubClient::downloadFinished);
    client_->download("../../etc/passwd", "1.0.0", shaOf(kFile));
    client_->download(kId, "../1.0.0", shaOf(kFile));
    client_->download(kId, "1.0.0", "short");
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, 4000);
    for (const auto& a : spy) QVERIFY(!a.at(0).value<PluginDownloadResult>().ok);
    QCOMPARE(hub_->pluginFileDownloads, 0);
  }

  void connectionLossIsADownloadFailure() {
    connectHub({QStringLiteral("saves_v1"), QStringLiteral("plugins_v1")});
    hub_->pluginFiles.insert(QString(kId) + "/1.0.0", kFile);
    hub_.reset();
    const PluginDownloadResult r = download("1.0.0", shaOf(kFile));
    QVERIFY(!r.ok);
    QVERIFY(r.bytes.isEmpty());
  }
};

QTEST_MAIN(PluginHubTest)
#include "pluginhub_test.moc"
