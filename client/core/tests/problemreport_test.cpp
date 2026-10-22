// "Report a problem": the report builder (attachments, selection, redaction, multipart body, zip). Synthetic data only.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "diagbundle.h"
#include "problemreport.h"

using namespace uzume;

namespace {
void writeFile(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(data);
}

// Relative to the real clock: the log files get their modification time from the file system.
const QDateTime kNow = QDateTime::currentDateTime();

QByteArray logLine(const QDateTime& t, const QByteArray& text) {
  return t.toString(Qt::ISODateWithMs).toUtf8() + " [I] " + text + "\n";
}

ProblemReport::Sources sources(const QString& dir) {
  ProblemReport::Sources s;
  s.dataDir = dir;
  s.now = kNow;
  s.playerVersion = "0.17.0";
  s.os = "Windows 11";
  s.systemInfo = QJsonObject{
      {"player", QJsonObject{{"version", "0.17.0"}, {"channel", "beta"}}},
      {"system", QJsonObject{{"os", "Windows 11 Pro"}, {"gpu", "Test GPU"}, {"data_dir", "C:/Users/max/Uzume"}, {"arch", "x86_64"}}},
      {"emulation", QJsonObject{{"gl_renderer", "Test GL"}, {"gl_version", "4.6 Test 551.23"}, {"cores", QJsonArray{"melondsds"}}}},
      {"media", QJsonObject{{"encoders_available", QJsonArray{"libopenh264"}}}},
      {"hubs", QJsonArray{QJsonObject{{"name", "Home server"}, {"address", "hub.lan"}}}},
      {"controllers", QJsonObject{{"connected", QJsonArray{"Pad"}}}}};
  return s;
}

const QStringList kSecrets = {"S3cretPassw0rd", "tok-ABCDEF123456", "hunter2hunter2"};

QMap<QString, QByteArray> multipartFiles(const QByteArray& body, const QByteArray& contentType, QByteArray* firstPartHeaders = nullptr,
                                         QJsonObject* meta = nullptr) {
  const QByteArray delim = "--" + contentType.mid(contentType.indexOf("boundary=") + 9);
  QMap<QString, QByteArray> files;
  qsizetype pos = body.indexOf(delim);
  bool first = true;
  while (pos >= 0) {
    pos += delim.size();
    if (body.mid(pos, 2) == "--") break;
    pos += 2;
    const qsizetype hdrEnd = body.indexOf("\r\n\r\n", pos);
    const qsizetype next = body.indexOf("\r\n" + delim, hdrEnd);
    const QByteArray headers = body.mid(pos, hdrEnd - pos);
    const QByteArray content = body.mid(hdrEnd + 4, next - hdrEnd - 4);
    if (first) {
      if (firstPartHeaders) *firstPartHeaders = headers;
      if (meta) *meta = QJsonDocument::fromJson(content).object();
      first = false;
    } else {
      const qsizetype fi = headers.indexOf("filename=\"") + 10;
      files.insert(QString::fromUtf8(headers.mid(fi, headers.indexOf('"', fi) - fi)), content);
    }
    pos = next + 2;
  }
  return files;
}
}  // namespace

class ProblemReportTest : public QObject {
  Q_OBJECT
 private:
  static const ProblemReport::Attachment* att(const ProblemReport& r, const QString& id) {
    for (const auto& a : r.attachments()) {
      if (a.id == id) return &a;
    }
    return nullptr;
  }

 private slots:
  void defaultsFollowTheDesign() {
    QTemporaryDir t;
    writeFile(t.filePath("logs/player.log"), logLine(kNow.addSecs(-60), "hub: connected"));
    writeFile(t.filePath("settings/player.json"), "{\"appearance\":\"dark\"}");
    const ProblemReport r = ProblemReport::collect(sources(t.path()));
    QVERIFY(att(r, "logs")->included);
    QVERIFY(att(r, "settings")->included);
    QVERIFY(!att(r, "connection")->available);  // until a check is set
    QVERIFY(!att(r, "system")->included);       // unchecked by default
    QVERIFY(att(r, "system")->available);
    QCOMPARE(r.includedFileNames(), (QStringList{"player-logs.txt", "settings.json"}));
  }

  void logsOfTheLastThreeDaysAreRedacted() {
    QTemporaryDir t;
    QByteArray log;
    log += logLine(kNow.addDays(-4), "too old line");
    log += logLine(kNow.addDays(-2), "two days ago password=S3cretPassw0rd host 10.0.0.2");
    log += logLine(kNow.addSecs(-30), "now Authorization: Bearer abc.def.ghijklmn");
    log += "continuation without a timestamp\n";
    writeFile(t.filePath("logs/player.log"), log);
    // A rotated file that was last written four days ago is left out as a whole.
    writeFile(t.filePath("logs/player.3.log"), logLine(kNow.addDays(-5), "ancient"));
    {
      QFile old(t.filePath("logs/player.3.log"));
      QVERIFY(old.open(QIODevice::ReadWrite));
      QVERIFY(old.setFileTime(kNow.addDays(-4), QFileDevice::FileModificationTime));
    }
    const ProblemReport r = ProblemReport::collect(sources(t.path()));
    const QString text = QString::fromUtf8(att(r, "logs")->data);
    QVERIFY(text.startsWith("=== player.log ==="));
    QVERIFY(text.contains("two days ago"));
    QVERIFY(text.contains("10.0.0.2"));  // addresses stay
    QVERIFY(text.contains("continuation without a timestamp"));
    QVERIFY(!text.contains("too old line"));
    QVERIFY(!text.contains("ancient"));
    for (const QString& s : kSecrets) QVERIFY2(!text.contains(s), qPrintable(s));
    QVERIFY(!text.contains("abc.def.ghijklmn"));
  }

  void missingLogsHideTheRow() {
    QTemporaryDir t;
    const ProblemReport r = ProblemReport::collect(sources(t.path()));
    QVERIFY(!att(r, "logs")->available);
    QVERIFY(!att(r, "logs")->included);
    QVERIFY(!const_cast<ProblemReport&>(r).setIncluded("logs", true));  // an absent attachment cannot be switched on
  }

  void logSizeCapKeepsTheNewestLines() {
    QTemporaryDir t;
    QByteArray log;
    for (int i = 0; i < 400; ++i) log += logLine(kNow.addSecs(-1000 + i), "line number " + QByteArray::number(i));
    writeFile(t.filePath("logs/player.log"), log);
    ProblemReport::Sources s = sources(t.path());
    s.maxLogBytes = 2048;
    const ProblemReport r = ProblemReport::collect(s);
    const QByteArray d = att(r, "logs")->data;
    QVERIFY(d.size() <= 2048);
    QVERIFY(d.contains(diag::kCutMarker));
    QVERIFY(d.contains("line number 399"));
    QVERIFY(!d.contains("line number 0\n"));
  }

  void settingsLoseSecretKeysAndPlantedSecrets() {
    QTemporaryDir t;
    writeFile(t.filePath("settings/player.json"),
              "{\"appearance\":\"dark\",\"hub_password\":\"S3cretPassw0rd\",\"nested\":{\"deviceToken\":\"tok-ABCDEF123456\","
              "\"note\":\"url https://u:hunter2hunter2@hub.lan/\",\"libraryFolders\":[\"D:/Games\"]}}");
    const QByteArray out = ProblemReport::redactedSettings(t.path());
    const QString text = QString::fromUtf8(out);
    for (const QString& s : kSecrets) QVERIFY2(!text.contains(s), qPrintable(s));
    QVERIFY(text.contains("\"appearance\": \"dark\""));
    QVERIFY(text.contains("D:/Games"));
    QVERIFY(QJsonDocument::fromJson(out).isObject());
    // No settings file: an empty object, not an error.
    QTemporaryDir empty;
    QCOMPARE(QJsonDocument::fromJson(ProblemReport::redactedSettings(empty.path())).object().size(), 0);
  }

  void systemInfoKeepsOnlyWhatSupportNeeds() {
    const QByteArray out = ProblemReport::systemInfoJson(sources("x").systemInfo);
    const QString text = QString::fromUtf8(out);
    QVERIFY(text.contains("Windows 11 Pro"));
    QVERIFY(text.contains("Test GPU"));
    QVERIFY(text.contains("4.6 Test 551.23"));
    QVERIFY(!text.contains("data_dir"));
    QVERIFY(!text.contains("C:/Users/max"));
    QVERIFY(!text.contains("Home server"));
    QVERIFY(!text.contains("Pad"));
  }

  void connectionCheckIsOptionalAndRedacted() {
    QTemporaryDir t;
    ProblemReport r = ProblemReport::collect(sources(t.path()));
    QVERIFY(!att(r, "connection")->available);
    const QDateTime when(QDate(2026, 10, 22), QTime(18, 43), Qt::LocalTime);
    r.setConnectionCheckJson("{\"relay\":\"ok\",\"turn_credential\":\"hunter2hunter2\",\"host\":\"hub.lan\"}", when);
    QVERIFY(att(r, "connection")->available);
    QVERIFY(att(r, "connection")->included);
    QCOMPARE(att(r, "connection")->time, when);
    QVERIFY(r.includedFileNames().contains("connection-check.json"));
    QVERIFY(!att(r, "connection")->data.contains("hunter2hunter2"));
    QVERIFY(att(r, "connection")->data.contains("hub.lan"));
    r.setConnectionCheckJson({}, {});  // an empty document removes it
    QVERIFY(!att(r, "connection")->available);
    QVERIFY(!r.includedFileNames().contains("connection-check.json"));
  }

  void selectionDecidesWhatIsSent() {
    QTemporaryDir t;
    writeFile(t.filePath("logs/player.log"), logLine(kNow.addSecs(-5), "LOGMARK"));
    writeFile(t.filePath("settings/player.json"), "{\"appearance\":\"light\"}");
    ProblemReport r = ProblemReport::collect(sources(t.path()));
    r.setDescription("  The Session froze.  ");
    r.setLinkedEvent("Session ended unexpectedly", QDateTime(QDate(2026, 10, 22), QTime(18, 41), Qt::LocalTime));
    QVERIFY(r.setIncluded("system", true));
    QVERIFY(r.setIncluded("logs", false));
    QVERIFY(!r.setIncluded("nope", true));

    QByteArray ctype, headers;
    QJsonObject meta;
    const QByteArray body = r.multipartBody(&ctype);
    const auto files = multipartFiles(body, ctype, &headers, &meta);
    QVERIFY(ctype.startsWith("multipart/form-data; boundary="));
    QVERIFY(headers.contains("name=\"meta\""));  // meta first
    QCOMPARE(meta.value("description").toString(), QString("The Session froze."));
    QCOMPARE(meta.value("player_version").toString(), QString("0.17.0"));
    QCOMPARE(meta.value("os").toString(), QString("Windows 11"));
    QCOMPARE(meta.value("linked_event").toObject().value("title").toString(), QString("Session ended unexpectedly"));
    QVERIFY(QDateTime::fromString(meta.value("linked_event").toObject().value("time").toString(), Qt::ISODate).isValid());
    QCOMPARE(QStringList(files.keys()), (QStringList{"settings.json", "system-info.json"}));
    QVERIFY(!body.contains("LOGMARK"));
    QCOMPARE(r.includedBytes(), files.value("settings.json").size() + files.value("system-info.json").size());
  }

  void withoutLinkedEventMetaHasNone() {
    QTemporaryDir t;
    ProblemReport r = ProblemReport::collect(sources(t.path()));
    r.setDescription("x");
    QVERIFY(!r.metaJson().contains("linked_event"));
    r.setLinkedEvent("  ", QDateTime::currentDateTime());
    QVERIFY(!r.metaJson().contains("linked_event"));
  }

  void zipHasReportTextAndTheSelectedFiles() {
    QTemporaryDir t;
    writeFile(t.filePath("logs/player.log"), logLine(kNow.addSecs(-5), "LOGMARK password=S3cretPassw0rd"));
    writeFile(t.filePath("settings/player.json"), "{\"appearance\":\"light\",\"token\":\"tok-ABCDEF123456\"}");
    ProblemReport r = ProblemReport::collect(sources(t.path()));
    r.setDescription("Froze after 4 hours. api_key=hunter2hunter2");
    r.setLinkedEvent("Session ended unexpectedly", QDateTime(QDate(2026, 10, 22), QTime(18, 41), Qt::LocalTime));
    r.setIncluded("settings", false);
    const QString zip = t.filePath("out/report.zip");
    QDir().mkpath(t.filePath("out"));
    QString err;
    QVERIFY2(r.saveAsZip(zip, &err), qPrintable(err));
    QMap<QString, QByteArray> e;
    QVERIFY2(diag::readZip(zip, &e, &err), qPrintable(err));
    QCOMPARE(QStringList(e.keys()), (QStringList{"player-logs.txt", "report.txt"}));
    const QString text = QString::fromUtf8(e.value("report.txt"));
    QVERIFY(text.contains("Froze after 4 hours."));
    QVERIFY(text.contains("Linked to: Session ended unexpectedly"));
    QVERIFY(text.contains("Player: 0.17.0"));
    QVERIFY(!text.contains("hunter2hunter2"));
    QVERIFY(e.value("player-logs.txt").contains("LOGMARK"));
    QVERIFY(!e.value("player-logs.txt").contains("S3cretPassw0rd"));
    QVERIFY(ProblemReport::zipFileName(QDateTime(QDate(2026, 10, 22), QTime(1, 2, 3), Qt::UTC)).endsWith("20261022T010203Z.zip"));
  }

  void descriptionRules() {
    QTemporaryDir t;
    ProblemReport r = ProblemReport::collect(sources(t.path()));
    QVERIFY(!r.hasDescription());
    r.setDescription("   \n");
    QVERIFY(!r.hasDescription());
    r.setDescription("ok");
    QVERIFY(r.hasDescription());
    QVERIFY(!r.descriptionTooLong());
    r.setDescription(QString(ProblemReport::kMaxDescription + 1, QLatin1Char('a')));
    QVERIFY(r.descriptionTooLong());
  }

  void tooLargeWhenTheAttachmentsExceedTheHubLimit() {
    QTemporaryDir t;
    ProblemReport r = ProblemReport::collect(sources(t.path()));
    QVERIFY(!r.tooLarge());
    r.setConnectionCheckJson(QByteArray(static_cast<qsizetype>(ProblemReport::kMaxAttachmentBytes) + 1, 'x'), QDateTime::currentDateTime());
    QVERIFY(r.tooLarge());
    r.setIncluded("connection", false);
    QVERIFY(!r.tooLarge());
  }
};

QTEST_GUILESS_MAIN(ProblemReportTest)
#include "problemreport_test.moc"
