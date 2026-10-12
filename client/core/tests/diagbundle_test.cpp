#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include "diagbundle.h"
#include "profilestore.h"

using namespace framebeam;

namespace {
void writeFile(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(data);
}
const QStringList kSecrets = {"S3cretPassw0rd", "tok-ABCDEF123456", "turnuser-xyz", "turncred-9876543210", "FB-ABCD-EFGH",
                              "fbd_0123456789abcdef", "abc.def.ghijklmn", "sess-cookie-value", "hunter2hunter2"};
}  // namespace

class DiagBundleTest : public QObject {
  Q_OBJECT
 private slots:
  void redactsPlantedSecrets() {
    const QByteArray in =
        "2026-10-11T10:00:00 [I] hub: connect https://192.168.1.5:8443 password=S3cretPassw0rd\n"
        "Authorization: Bearer abc.def.ghijklmn\n"
        "{\"device_token\":\"tok-ABCDEF123456\",\"name\":\"Hub\"}\n"
        "turn:turnuser-xyz:turncred-9876543210@turn.example.org:3478\n"
        "ice username=turnuser-xyz credential=turncred-9876543210\n"
        "redeem invite code=FB-ABCD-EFGH at https://hub.lan/invite#FB-ABCD-EFGH\n"
        "token fbd_0123456789abcdef issued\n"
        "Cookie: fb_session=sess-cookie-value; fb_csrf=hunter2hunter2\n"
        "https://user:hunter2hunter2@myhub.lan/x status_code=200\n";
    const QString out = QString::fromUtf8(diag::redact(in));
    for (const QString& s : kSecrets) QVERIFY2(!out.contains(s), qPrintable("leaked: " + s));
    QVERIFY(out.contains("192.168.1.5:8443"));
    QVERIFY(out.contains("turn.example.org:3478"));
    QVERIFY(out.contains("myhub.lan"));
    QVERIFY(out.contains("status_code=200"));
    QVERIFY(out.contains("[redacted]"));
  }

  void fileNameIsUtcTimestamp() {
    const QDateTime t(QDate(2026, 10, 11), QTime(9, 8, 7), Qt::UTC);
    QCOMPARE(diag::fileName(t), QString("framebeam-player-diagnostics-20261011T090807Z.zip"));
  }

  void bundleHasEntriesInfoAndRedactedLogs() {
    QTemporaryDir t;
    QVERIFY(t.isValid());
    ProfileStore ps(t.path());
    writeFile(t.filePath("logs/player.log"), "new line password=S3cretPassw0rd host 10.0.0.2\n");
    writeFile(t.filePath("logs/player.1.log"), "old line\n");
    writeFile(t.filePath("logs/plugins/demo.log"), "plugin token=tok-ABCDEF123456\n");
    QJsonObject info = diag::offlineInfo(t.path());
    info.insert("leak", "api_key=hunter2hunter2");
    const QString zip = t.filePath("out.zip");
    QString err;
    QVERIFY2(diag::writeBundle(zip, info, t.path(), {}, &err), qPrintable(err));

    QMap<QString, QByteArray> e;
    QVERIFY2(diag::readZip(zip, &e, &err), qPrintable(err));
    QVERIFY(e.contains("README.txt"));
    QVERIFY(e.contains("player-info.json"));
    QVERIFY(e.contains("logs/player.log"));
    QVERIFY(e.contains("logs/player.1.log"));
    QVERIFY(e.contains("logs/plugins/demo.log"));
    QVERIFY(e["README.txt"].contains("no passwords or"));
    QVERIFY(e["logs/player.log"].contains("10.0.0.2"));
    QVERIFY(!e["logs/player.log"].contains("S3cretPassw0rd"));
    QVERIFY(!e["logs/plugins/demo.log"].contains("tok-ABCDEF123456"));
    QVERIFY(!e["player-info.json"].contains("hunter2hunter2"));

    const QJsonObject o = QJsonDocument::fromJson(e["player-info.json"]).object();
    QCOMPARE(o["info_version"].toInt(), 1);
    QVERIFY(!o["generated_at"].toString().isEmpty());
    for (const char* k : {"player", "system", "hubs", "emulation", "media", "controllers", "settings"}) QVERIFY2(o.contains(k), k);
    const QJsonObject p = o["player"].toObject();
    for (const char* k : {"version", "channel", "commit", "protocol_version"}) QVERIFY2(p.contains(k), k);
    const QJsonObject s = o["system"].toObject();
    for (const char* k : {"os", "arch", "qt_version", "data_dir", "portable", "free_bytes"}) QVERIFY2(s.contains(k), k);
    QVERIFY(!s["portable"].toBool());
    QVERIFY(o["media"].toObject().contains("encoders_available"));
    QVERIFY(o["emulation"].toObject().contains("hw_render"));

    // Readable by an external unzip when one exists.
    const QString unzip = QStandardPaths::findExecutable("unzip");
    if (!unzip.isEmpty()) {
      QProcess pr;
      pr.start(unzip, {"-t", zip});
      QVERIFY(pr.waitForFinished(20000));
      QCOMPARE(pr.exitCode(), 0);
    }
  }

  void sizeCapCutsOldestAtLineBoundary() {
    QTemporaryDir t;
    QVERIFY(t.isValid());
    QByteArray big;
    for (int i = 0; i < 5000; ++i) big += "line " + QByteArray::number(i) + " some padding text to fill the file\n";
    writeFile(t.filePath("logs/player.log"), big);
    writeFile(t.filePath("logs/player.1.log"), "must not fit\n");
    // Make player.log the newest.
    QFile f(t.filePath("logs/player.1.log"));
    QVERIFY(f.open(QIODevice::ReadWrite));
    f.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1), QFileDevice::FileModificationTime);
    f.close();
    diag::Options o;
    o.maxLogBytes = 10000;
    QString err;
    const QString zip = t.filePath("out.zip");
    QVERIFY2(diag::writeBundle(zip, diag::offlineInfo(t.path()), t.path(), o, &err), qPrintable(err));
    QMap<QString, QByteArray> e;
    QVERIFY2(diag::readZip(zip, &e, &err), qPrintable(err));
    const QByteArray log = e["logs/player.log"];
    QVERIFY(log.startsWith(diag::kCutMarker));
    QVERIFY(log.size() <= 10000 + static_cast<int>(qstrlen(diag::kCutMarker)));
    QVERIFY(log.endsWith("fill the file\n"));
    QVERIFY(log.mid(qstrlen(diag::kCutMarker)).startsWith("line "));  // starts at a line boundary
    QVERIFY(!e.contains("logs/player.1.log"));
  }

  void existingFileIsReplacedAndCorruptZipRejected() {
    QTemporaryDir t;
    writeFile(t.filePath("x.zip"), "not a zip");
    QMap<QString, QByteArray> e;
    QVERIFY(!diag::readZip(t.filePath("x.zip"), &e));
    QVERIFY(diag::writeBundle(t.filePath("x.zip"), {}, t.path()));
    QVERIFY(diag::readZip(t.filePath("x.zip"), &e));
    QVERIFY(e.contains("README.txt"));
  }
};

QTEST_GUILESS_MAIN(DiagBundleTest)
#include "diagbundle_test.moc"
