// Diagnostics overlay model (0.6 D5-D11): formatting and persisted states, without Qt Quick.
#include <QtTest>

#include "diagnosticsmodel.h"
#include "gpuencodebridge.h"

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

  // ADR 0019: the host line names the GPU-direct path, or says that it was tried and is off. Without either, the string is
  // exactly the one above.
  void hostLineNamesTheGpuPath() {
    SessionStats s;
    s.encoderName = QStringLiteral("h264_nvenc");
    s.videoBitrateKbps = 6000;
    s.targetBitrateKbps = 6500;
    s.fps = 60;
    const QString base = QStringLiteral("Encoder h264_nvenc · H.264 · 6.0 / target 6.5 Mbit/s · 60.0 fps");
    QCOMPARE(DiagnosticsModel::hostLine(s), base);
    s.gpuInput = true;
    QCOMPARE(DiagnosticsModel::hostLine(s), base + QStringLiteral(" · GPU-direct"));
    s.gpuInput = false;
    s.gpuInputFailed = true;
    QCOMPARE(DiagnosticsModel::hostLine(s), base + QStringLiteral(" · readback (GPU-direct off)"));
    s.gpuInput = true;  // cannot be both in practice; GPU-direct wins
    QCOMPARE(DiagnosticsModel::hostLine(s), base + QStringLiteral(" · GPU-direct"));
    SessionStats idle;  // no encoder yet: the suffixes do not apply
    idle.gpuInputFailed = true;
    QCOMPARE(DiagnosticsModel::hostLine(idle), QStringLiteral("Encoder starts with the first viewer"));
  }

  // The frame row gets the copy into the Session encode texture only while frames are captured.
  void frameRowShowsTheGpuCopy() {
    EmulationDiagnostics d = sw();
    d.hwRequested = d.hwActive = true;
    d.api = QStringLiteral("OpenGL 4.6 Core");
    d.gpu = QStringLiteral("Example GPU · Driver 1.2.3");
    d.frameMs = 9.4;
    d.emuMs = 7.1;
    d.readbackMs = 0.4;
    d.readbacksPerSec = 60;
    const QString base = QStringLiteral("9.4 ms · emu 7.1 · readback 0.4 (60/s)");
    QCOMPARE(DiagnosticsModel::emulationMap(d).value("frame").toString(), base);
    d.gpuCopyMs = 0.3;
    QCOMPARE(DiagnosticsModel::emulationMap(d).value("frame").toString(), base);  // copy time without captures: unchanged
    d.gpuCopiesPerSec = 60;
    QCOMPARE(DiagnosticsModel::emulationMap(d).value("frame").toString(), base + QStringLiteral(" · GPU copy 0.3 (60/s)"));
    d.gpuCopiesPerSec = 29.6;
    QVERIFY(DiagnosticsModel::emulationMap(d).value("frame").toString().endsWith(QStringLiteral(" · GPU copy 0.3 (30/s)")));
    d.fps = 0;  // paused: the dash stays
    QCOMPARE(DiagnosticsModel::emulationMap(d).value("frame").toString(), QStringLiteral("—"));
  }

  // planGpuEncode over all 32 combinations: written out by cases, not by the formula of the implementation.
  void planGpuEncodeTruthTable() {
    int rows = 0, keep = 0, create = 0, wanted = 0, emptyShare = 0, maxShare = 0;
    for (int bits = 0; bits < 32; ++bits) {
      const bool shared = bits & 16, hw = bits & 8, running = bits & 4, allowed = bits & 2, active = bits & 1;
      const GpuEncodePlan p = planGpuEncode(shared, hw, running, allowed, active);
      const QString row = QStringLiteral("shared=%1 hw=%2 running=%3 allowed=%4 active=%5").arg(shared).arg(hw).arg(running).arg(allowed).arg(active);
      ++rows;
      bool expectKeep = false, expectCreate = false;
      QSize expectSize;  // not shared: no share size at all
      if (shared) {
        expectSize = active ? QSize() : kShareEncodeMax;  // GPU-direct active: nothing needs reading back for the encoder
        if (hw && allowed) {
          expectKeep = true;
          expectCreate = running;  // the bridge exists only while the encoder runs ...
        }
      }
      QVERIFY2(p.keep == expectKeep, qPrintable(row));
      QVERIFY2(p.create == expectCreate, qPrintable(row));
      QVERIFY2(p.wanted == expectCreate, qPrintable(row));  // ... and produces frames exactly then
      QVERIFY2(p.shareSize == expectSize, qPrintable(row));
      keep += p.keep;
      create += p.create;
      wanted += p.wanted;
      (p.shareSize.isEmpty() ? emptyShare : maxShare) += 1;
    }
    QCOMPARE(rows, 32);
    QCOMPARE(keep, 4);
    QCOMPARE(create, 2);
    QCOMPARE(wanted, 2);
    QCOMPARE(emptyShare, 24);  // 16 unshared + 8 shared with GPU-direct active
    QCOMPARE(maxShare, 8);
    QCOMPARE(kShareEncodeMax, QSize(1280, 1920));
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
