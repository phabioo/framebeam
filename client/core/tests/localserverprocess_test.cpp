// LocalServerProcess (0.18 package 10): start/ready/stop, port retry, restarts and the cleaned environment, against a
// fake server (a small shell script that speaks the `-child` contract). Skipped on Windows.
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "localserverprocess.h"

using namespace uzume;
using St = LocalServerProcess::State;

namespace {
// Behaviour of the fake server is chosen by files in the data dir:
//   mode (text): normal | silent | busy_once | crash_after_ready
//   counter: lines appended per start
const char* kScript = R"SH(#!/bin/sh
while [ $# -gt 0 ]; do
  case "$1" in
    -data-dir) DATA="$2"; shift 2;;
    -listen) LISTEN="$2"; shift 2;;
    *) shift;;
  esac
done
PORT="${LISTEN##*:}"
echo "$PORT" >> "$DATA/ports"
env | grep '^UZUME_' > "$DATA/env" 2>/dev/null
MODE=normal
[ -f "$DATA/mode" ] && MODE=$(cat "$DATA/mode")
case "$MODE" in
  silent) sleep 30; exit 0;;
  busy_once)
    if [ "$(wc -l < "$DATA/ports")" -le 1 ]; then echo "listen tcp 127.0.0.1:$PORT: bind: address in use" >&2; exit 1; fi;;
  busy_always) echo "address in use" >&2; exit 1;;
  die_early) echo "boom" >&2; exit 3;;
  crash_after_ready) echo "UZUME_READY $PORT"; sleep 0.2; exit 1;;
  port_zero) echo "UZUME_READY 45678"; cat >/dev/null; echo eof > "$DATA/eof"; exit 0;;
esac
echo "some log line"
echo "UZUME_READY $PORT"
cat >/dev/null
echo eof > "$DATA/eof"
exit 0
)SH";
}  // namespace

class LocalServerProcessTest : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  QString exe_;
  QString data_;

  LocalServerProcess::Config cfg() const {
    LocalServerProcess::Config c;
    c.exe = exe_;
    c.dataDir = data_;
    c.readyTimeoutMs = 5000;
    c.stopWaitMs = 5000;
    c.backoffBaseMs = 30;
    return c;
  }
  void mode(const QString& m) {
    QFile f(data_ + "/mode");
    QVERIFY2(f.open(QIODevice::WriteOnly), "mode");
    f.write(m.toUtf8());
  }
  QStringList lines(const QString& name) const {
    QFile f(data_ + "/" + name);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
  }

 private slots:
  void initTestCase() {
#ifdef Q_OS_WIN
    QSKIP("fake server is a shell script");
#endif
    QVERIFY(tmp_.isValid());
    exe_ = tmp_.filePath("fake-server.sh");
    QFile f(exe_);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(kScript);
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
  }
  void init() {
    data_ = tmp_.filePath(QStringLiteral("data-%1").arg(QTest::currentTestFunction()));
    QDir().mkpath(data_);
  }

  void startsReportsReadyAndStopsOnEof() {
    LocalServerProcess p;
    p.setConfig(cfg());
    QSignalSpy ready(&p, &LocalServerProcess::ready);
    QVERIFY(p.start());
    QCOMPARE(p.state(), St::Starting);
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Running, 5000);
    QCOMPARE(ready.count(), 1);
    QVERIFY(p.port() > 0);
    QCOMPARE(lines("ports"), QStringList{QString::number(p.port())});
    p.stop();
    QCOMPARE(p.state(), St::Stopped);
    QCOMPARE(lines("eof"), QStringList{"eof"});  // stdin EOF ended it gracefully
  }

  void storedPortIsUsed() {
    LocalServerProcess p;
    auto c = cfg();
    c.port = LocalServerProcess::freeLoopbackPort();
    QVERIFY(c.port > 0);
    p.setConfig(c);
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Running, 5000);
    QCOMPARE(p.port(), c.port);
    p.stop();
  }

  void reportedPortWins() {
    mode("port_zero");
    LocalServerProcess p;
    p.setConfig(cfg());
    QSignalSpy changed(&p, &LocalServerProcess::portChanged);
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Running, 5000);
    QCOMPARE(p.port(), 45678);
    QCOMPARE(changed.count(), 1);
    p.stop();
  }

  void addressInUseRetriesOnceWithNewPort() {
    mode("busy_once");
    LocalServerProcess p;
    auto c = cfg();
    c.port = LocalServerProcess::freeLoopbackPort();
    p.setConfig(c);
    QSignalSpy changed(&p, &LocalServerProcess::portChanged);
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Running, 5000);
    QVERIFY(p.addressInUseRetried());
    QVERIFY(p.port() != c.port);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(lines("ports").size(), 2);
    p.stop();
  }

  void addressInUseTwiceFails() {
    mode("busy_always");
    LocalServerProcess p;
    p.setConfig(cfg());
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Failed, 5000);
    QVERIFY(!p.error().isEmpty());
    QCOMPARE(lines("ports").size(), 2);
  }

  void exitBeforeReadyFailsWithStderrText() {
    mode("die_early");
    LocalServerProcess p;
    p.setConfig(cfg());
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Failed, 5000);
    QVERIFY2(p.error().contains("boom"), qPrintable(p.error()));
  }

  void readyTimeoutFails() {
    mode("silent");
    LocalServerProcess p;
    auto c = cfg();
    c.readyTimeoutMs = 300;
    c.stopWaitMs = 500;
    p.setConfig(c);
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Failed, 3000);
    QVERIFY(!p.error().isEmpty());
  }

  void crashWhileRunningRestartsThenFails() {
    mode("crash_after_ready");
    LocalServerProcess p;
    p.setConfig(cfg());
    QSignalSpy ready(&p, &LocalServerProcess::ready);
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Failed, 10000);
    QCOMPARE(lines("ports").size(), 4);  // first start + 3 restarts
    QCOMPARE(ready.count(), 4);
  }

  void missingExeFails() {
    LocalServerProcess p;
    auto c = cfg();
    c.exe = data_ + "/nope";
    p.setConfig(c);
    QVERIFY(!p.start());
    QCOMPARE(p.state(), St::Failed);
  }

  void childDoesNotInheritUzumeVariables() {
    qputenv("UZUME_NETWORK_SHARING", "1");
    qputenv("UZUME_LISTEN", ":9");
    LocalServerProcess p;
    p.setConfig(cfg());
    QVERIFY(p.start());
    QTRY_COMPARE_WITH_TIMEOUT(p.state(), St::Running, 5000);
    p.stop();
    qunsetenv("UZUME_NETWORK_SHARING");
    qunsetenv("UZUME_LISTEN");
    QVERIFY2(lines("env").isEmpty(), qPrintable(lines("env").join(',')));
  }

  void cleanedEnvironmentDropsOnlyUzume() {
    const QStringList in{"PATH=/bin", "UZUME_X=1", "uzume_y=2", "HOME=/h"};
    QCOMPARE(LocalServerProcess::cleanedEnvironment(in), (QStringList{"PATH=/bin", "HOME=/h"}));
  }

  void findServerExeHonoursEnvironment() {
    qputenv("UZUME_LOCAL_SERVER_EXE", exe_.toUtf8());
    QCOMPARE(LocalServerProcess::findServerExe(), exe_);
    qunsetenv("UZUME_LOCAL_SERVER_EXE");
  }

  void defaultDataDirIsBelowBase() {
    QCOMPARE(LocalServerProcess::defaultDataDir("/x/data"), QStringLiteral("/x/data/server"));
  }
};

QTEST_MAIN(LocalServerProcessTest)
#include "localserverprocess_test.moc"
