#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "version.h"

class CoreVersionTest : public QObject {
  Q_OBJECT
 private slots:
  void versionIsSet() { QVERIFY(!framebeam::playerVersion().empty()); }

  void channelIsKnown() {
    const std::string_view c = framebeam::playerChannel();
    QVERIFY(c == "stable" || c == "beta" || c == "dev");
  }

  void versionJsonHasContractKeys() {
    const std::string s = framebeam::playerVersionJson();
    QVERIFY(s.find('\n') == std::string::npos);
    const QJsonObject o = QJsonDocument::fromJson(QByteArray::fromStdString(s)).object();
    QCOMPARE(o.value("product").toString(), QString("player"));
    QCOMPARE(o.value("version").toString().toStdString(), std::string(framebeam::playerVersion()));
    QCOMPARE(o.value("channel").toString().toStdString(), std::string(framebeam::playerChannel()));
    QVERIFY(o.contains("commit"));
    QCOMPARE(o.value("protocol_version").toInt(), 1);
    QCOMPARE(o.value("min_protocol_version").toInt(), 1);
  }
};

QTEST_GUILESS_MAIN(CoreVersionTest)
#include "core_version_test.moc"
