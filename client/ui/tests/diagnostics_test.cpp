// Diagnostics overlay model (0.6 D5-D11): formatting and persisted states, without Qt Quick.
#include <QtTest>

#include "diagnosticsmodel.h"

using namespace framebeam;
using namespace framebeam::ui;

namespace {
EmulationDiagnostics sw() {
  EmulationDiagnostics d;
  d.valid = true;
  d.core = QStringLiteral("melonDS DS 1.4.0");
  d.frameSize = QSize(256, 384);
  d.baseSize = QSize(256, 384);
  d.screens = 2;
  d.cpuThreads = 8;
  d.fps = 59.9;
  d.targetFps = 59.83;
  d.frameMs = 6.2;
  d.emuMs = 6.2;
  d.audioActive = true;
  d.audioBufferMs = 82;
  return d;
}
}  // namespace

class DiagnosticsTest : public QObject {
  Q_OBJECT
 private slots:
  void softwareRows() {
    const QVariantMap m = DiagnosticsModel::emulationMap(sw());
    QCOMPARE(m.value("renderer").toString(), QStringLiteral("Software"));
    QCOMPARE(m.value("rendererSub").toString(), QStringLiteral("CPU · 8 threads"));
    QVERIFY(!m.value("fallback").toBool());
    QVERIFY(m.value("frame").toString().contains(QStringLiteral("no readback")));
    QVERIFY(m.value("fps").toString().startsWith(QStringLiteral("59.9 / 59.83")));
    QVERIFY(m.value("audio").toString().contains(QStringLiteral("0 underruns")));
  }

  void hardwareRows() {
    EmulationDiagnostics d = sw();
    d.hwRequested = d.hwActive = true;
    d.api = QStringLiteral("OpenGL 4.6 Core");
    d.gpu = QStringLiteral("Example GPU · Driver 1.2.3");
    d.frameSize = QSize(768, 1152);
    d.readbackMs = 1.4;
    const QVariantMap m = DiagnosticsModel::emulationMap(d);
    QCOMPARE(m.value("renderer").toString(), d.api);
    QCOMPARE(m.value("rendererSub").toString(), d.gpu);
    QVERIFY(m.value("resolution").toString().startsWith(QStringLiteral("3×")));
    QVERIFY(m.value("frame").toString().contains(QStringLiteral("readback 1.4")));
  }

  void fallbackRows() {
    EmulationDiagnostics d = sw();
    d.hwRequested = true;
    d.requestedScale = 3;
    d.fallbackReason = QStringLiteral("no context");
    const QVariantMap m = DiagnosticsModel::emulationMap(d);
    QVERIFY(m.value("fallback").toBool());
    QVERIFY(m.value("fallbackText").toString().contains(QStringLiteral("no context")));
    QVERIFY(m.value("resolutionSub").toString().contains(QStringLiteral("3× requested")));
  }

  void underrunsAreShown() {
    EmulationDiagnostics d = sw();
    d.underruns = 1;
    QVERIFY(DiagnosticsModel::emulationMap(d).value("audio").toString().contains(QStringLiteral("1 underrun")));
    QVERIFY(!DiagnosticsModel::emulationMap(d).value("audioSub").toString().isEmpty());
    d.audioActive = false;
    QCOMPARE(DiagnosticsModel::emulationMap(d).value("audio").toString(), QStringLiteral("No audio output"));
  }

  void pausedShowsDashes() {
    EmulationDiagnostics d = sw();
    d.fps = 0;
    const QVariantMap m = DiagnosticsModel::emulationMap(d);
    QCOMPARE(m.value("frame").toString(), QStringLiteral("—"));
    QCOMPARE(m.value("summary").toString(), QStringLiteral("not running"));
  }

  void connectionPills() {
    using P = DiagnosticsModel;
    QCOMPARE(P::connectionPill(QString()).text, QStringLiteral("Connecting"));
    QCOMPARE(P::connectionPill(QStringLiteral("direct (host)")).tone, QStringLiteral("ok"));
    QCOMPARE(P::connectionPill(QStringLiteral("relay (udp)")).text, QStringLiteral("Relayed (TURN)"));
    QCOMPARE(P::connectionPill(QStringLiteral("relay (tcp)")).text, QStringLiteral("Relayed (TURN/TCP)"));
    QVERIFY(P::isRelayed(QStringLiteral("relay (udp)")));
    QVERIFY(!P::isRelayed(QStringLiteral("direct (srflx)")));
  }

  void hostAndViewerLines() {
    SessionStats s;
    QCOMPARE(DiagnosticsModel::hostLine(s), QStringLiteral("Encoder starts with the first viewer"));
    s.encoderName = QStringLiteral("h264_nvenc");
    s.videoBitrateKbps = 6000;
    s.targetBitrateKbps = 6500;
    s.fps = 60;
    QCOMPARE(DiagnosticsModel::hostLine(s), QStringLiteral("Encoder h264_nvenc · H.264 · 6.0 / target 6.5 Mbit/s · 60.0 fps"));
    QCOMPARE(DiagnosticsModel::hostSendingLine(1), QStringLiteral("Sending to 1 viewer · adaptive bitrate"));
    QCOMPARE(DiagnosticsModel::decoderLine(QStringLiteral("h264"), 3900.0, 59.7),
             QStringLiteral("Decoder h264 · H.264 · 3.9 Mbit/s · 59.7 fps"));
    QCOMPARE(DiagnosticsModel::decoderLine(QString(), std::nullopt, std::nullopt), QStringLiteral("Decoder — · H.264 · — · —"));
  }

  void linkLineAndTurn() {
    QCOMPARE(DiagnosticsModel::linkLine(48.0, 0.4, QStringLiteral("direct (host)"), QString()), QStringLiteral("RTT 48 ms · Loss 0.4 %"));
    QCOMPARE(DiagnosticsModel::linkLine(std::nullopt, std::nullopt, QStringLiteral("relay (udp)"), QStringLiteral("hub.example.com:3478")),
             QStringLiteral("RTT — · Loss — · via hub.example.com:3478"));
    TurnServer t;
    t.urls = {QStringLiteral("turn:hub.example.com:3478?transport=udp")};
    QCOMPARE(DiagnosticsModel::turnEndpoint({t}), QStringLiteral("hub.example.com:3478"));
    QVERIFY(DiagnosticsModel::turnEndpoint({}).isEmpty());
  }

  void streamingSummaryCountsRelayed() {
    DiagnosticsModel m;
    QCOMPARE(m.participantCount(), 0);
    const auto ok = DiagnosticsModel::participant(QStringLiteral("A"), QStringLiteral("viewer"), {QStringLiteral("Direct"), QStringLiteral("ok")}, {}, {});
    const auto rl = DiagnosticsModel::participant(QStringLiteral("B"), QStringLiteral("viewer"), {QStringLiteral("Relayed (TURN)"), QStringLiteral("warn")}, {}, {});
    m.setStreaming({QVariantMap{{"participants", QVariantList{ok, rl}}}});
    QCOMPARE(m.participantCount(), 2);
    QCOMPARE(m.streamingSummary(), QStringLiteral("2 · 1 relayed"));
  }

  void statesToggle() {
    DiagnosticsModel m;
    QSignalSpy spy(&m, &DiagnosticsModel::statesChanged);
    QVERIFY(!m.isOpen());
    m.toggle();
    QVERIFY(m.isOpen());
    QVERIFY(m.emulationOpen());
    m.toggleEmulation();
    QVERIFY(!m.emulationOpen());
    QCOMPARE(spy.count(), 2);
  }
};

QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "diagnostics_test.moc"
