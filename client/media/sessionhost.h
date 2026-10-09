#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "bitratecontroller.h"
#include "mediastats.h"
#include "sessiontypes.h"
#include "videoencoder.h"
#include "viewerreports.h"

struct AVFrame;

namespace rtc {
class PeerConnection;
class Track;
class DataChannel;
class RtpPacketizationConfig;
}  // namespace rtc

namespace framebeam {

class ThreadBridge;
class EncodeWorker;
struct WorkerStats;
struct SinkList;

// Owner side of a Session (ADR 0006 D5): one encoder for all viewers (running only while at least one viewer is
// there), per viewer a PeerConnection with a sendonly H.264 and a sendonly Opus track. Signaling is decoupled:
// wire addViewer()/removeViewer()/handleSignal()/close() to the Hub messages and signalOut() to HubSocket.
//
// Thread ownership: the public API, the PeerConnections, signaling and all Qt state belong to the thread the object
// lives in (the UI thread); use queued connections otherwise. pushFrame()/pushAudio() only copy the data into a
// bounded queue and return. All video/Opus encoding and the RTP sendFrame() calls run on ONE dedicated worker
// thread (EncodeWorker, started with the encoder, joined in stopEncoder()/close()); when it falls behind the oldest
// pending video frame is dropped, audio stays ordered. stats()/encoderName()/encoderRunning() are thread-safe.
//
// GPU-direct input (ADR 0019): on an NVIDIA GPU the local game can hand CUDA frames to pushGpuFrame() instead of the
// readback frames of pushFrame(). h264_nvenc then takes them without a CPU copy. Readback stays the default and the
// safety net: the CPU encoder keeps running until the first CUDA frame was encoded, and any problem on the GPU path
// switches this Session back to readback frames (gpuInputChanged()), never to an error.
class SessionHost : public QObject {
  Q_OBJECT
 public:
  struct Options {
    int fps = 60;
    int videoBitrate = 2'000'000;  // start value of the bitrate adaptation (ADR 0012 D5, AIMD 300..4000 kbit/s)
    int audioBitrate = 96'000;
    QStringList encoderOrder;      // empty: ADR 0006 D5 preference
  };

  explicit SessionHost(QObject* parent = nullptr);
  ~SessionHost() override;

  void setOptions(const Options& o) { options_ = o; }
  // Session published: signaling and viewers refer to this id.
  // `turnServers`: relay credentials from hello_ack / the join response (ADR 0012 D3-D5).
  void open(const QString& sessionId, const QStringList& iceServers, const QList<TurnServer>& turnServers = {});
  // ICE transport policy "relay" for the PeerConnections created from now on (default: FRAMEBEAM_FORCE_RELAY=1).
  void setForceRelay(bool force) { forceRelay_ = force; }
  bool forceRelay() const { return forceRelay_; }
  // Session ended or stopped sharing: closes every viewer immediately and stops the encoder.
  void close();
  bool isOpen() const { return open_; }

  int viewerCount() const { return static_cast<int>(viewers_.size()); }
  bool hasViewer(const QString& viewerId) const { return viewers_.contains(viewerId); }
  bool encoderRunning() const { return encoderRunning_; }
  QString encoderName() const;
  // Last full second, aggregated over viewers; cheap.
  SessionStats stats() const;
  QList<ViewerLinkStats> viewerLinks() const;

  // Video: raw libretro frame (XRGB8888/RGB565, stride in bytes) or a QImage (any format, converted). The first
  // frame after a viewer joined fixes the encoder resolution; a size change reopens the encoder.
  void pushFrame(const uint8_t* data, int width, int height, int stride, RawPixelFormat format);
  void pushFrame(const QImage& image);
  // Audio: interleaved int16 stereo at the core rate (any rate), e.g. straight from the emulation callback.
  void pushAudio(const QByteArray& pcm, int sampleRate);

  // GPU-direct input (ADR 0019): one CUDA frame (AV_PIX_FMT_CUDA, sw_format RGB0) of the local game. Never starts the
  // encoder (readback frames do); ignored unless the encoder runs and GPU input is not off for this Session. Does not
  // look at the driver or the caps (tests push fake frames).
  void pushGpuFrame(std::shared_ptr<AVFrame> frame);
  // GPU input may be tried in this Session: open, not off, configured (no kill switch, no forced encoder), no pinned
  // encoder order, CUDA usable in this process, FFmpeg feeds h264_nvenc with CUDA frames and the encoder opens here.
  bool gpuInputAllowed() const;
  // The encoder runs on GPU frames (confirmed by its first CUDA encode); readback frames are not copied then.
  bool gpuInputActive() const { return encoderRunning_ && gpuActive_ && !gpuInputOff_; }
  // GPU input was tried in this Session and failed (diagnostics: "readback (GPU-direct off)").
  bool gpuInputFailed() const { return gpuInputFailed_; }
  // Off for the rest of this Session: readback frames again. `failure` false = not applicable (silent), true = failed.
  // Idempotent; emits gpuInputChanged().
  void disableGpuInput(const QString& reason, bool failure);
  // Tests: gpuActive_ = true, as if the worker had confirmed GPU mode.
  void simulateGpuActiveForTest() {
    gpuActive_ = true;
    lastGpuNs_ = clock_.nsecsElapsed();
  }

 public slots:
  void addViewer(const QString& viewerId, const QList<TurnServer>& turnServers = {});     // `viewer_joined`: creates the PeerConnection and sends the offer
  void removeViewer(const QString& viewerId);  // `viewer_left`: closes the PeerConnection immediately
  void handleSignal(const framebeam::SessionSignal& signal);  // answer / candidate from a viewer

 signals:
  void signalOut(const framebeam::SessionSignal& signal);
  void viewerConnected(const QString& viewerId);  // media path established
  void viewerClosed(const QString& viewerId, const QString& reason);  // PeerConnection closed (removed, failed, ...)
  void encoderRunningChanged(bool running);
  void rxReportReceived(const QString& viewerId, double loss, double kbps);  // viewer report over fb-diag (tests, diagnostics)
  void errorOccurred(const QString& message);  // e.g. no encoder can be opened
  // GPU input became active or was switched off (UI thread). Read gpuInputActive()/gpuInputFailed().
  void gpuInputChanged();

 private:
  struct Viewer {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video;
    std::shared_ptr<rtc::Track> audio;
    std::shared_ptr<rtc::DataChannel> diag;  // negotiated "fb-diag": brings up SCTP so that rtt() is available
    QList<SessionSignal> pendingCandidates;
    bool remoteSet = false;
    bool connected = false;
    QString state = QStringLiteral("new");
  };

  void dropViewer(const QString& viewerId, const QString& reason);
  void onPcState(const QString& viewerId, int state);
  void startEncoder();
  void stopEncoder();
  void requestKeyframe();
  void publishSinks();
  void updateStats();
  void applyRemoteCandidate(Viewer& v, const SessionSignal& s);
  void onRxReport(const QString& viewerId, const RxReport& report);

  Options options_;
  QString sessionId_;
  QStringList iceServers_;
  QList<TurnServer> turnServers_;
  bool forceRelay_ = false;
  bool relayTcpOnly_ = false;
  BitrateController bitrate_;
  QElapsedTimer adaptClock_;  // monotonic clock of the controller, never restarted
  std::atomic<int> targetKbps_{0};
  bool open_ = false;
  QHash<QString, Viewer> viewers_;
  std::shared_ptr<ThreadBridge> bridge_;

  std::unique_ptr<EncodeWorker> worker_;  // exists while the encoder runs
  std::shared_ptr<SinkList> sinks_;       // tracks the worker sends to (guarded copy of viewers_)
  std::atomic<bool> encoderRunning_{false};
  std::atomic<int> viewerCountAtomic_{0};
  bool encoderFailed_ = false;
  unsigned workerGen_ = 0;
  bool keyframePending_ = false;
  QElapsedTimer clock_;
  qint64 lastEncodeNs_ = 0;

  // GPU-direct input (ADR 0019). Atomic where stats()/gpuInputActive() may be read from other threads.
  std::atomic<bool> gpuActive_{false};       // the worker confirmed GPU mode
  std::atomic<bool> gpuInputOff_{false};     // off for this Session (failed or not applicable)
  std::atomic<bool> gpuInputFailed_{false};  // ... because it failed
  bool gpuConfigured_ = false;               // computed at open(): kill switch and forced encoder
  int gpuSkips_ = 0;                         // watchdog: readback frames since the last GPU frame
  qint64 lastGpuNs_ = 0;  // clock_ time of the last GPU frame (watchdog)

  QTimer statsTimer_;
  QElapsedTimer statsClock_;
  qint64 lastStatFrames_ = 0, lastStatVideoBytes_ = 0, lastStatAudioBytes_ = 0;
  int keyframeRequests_ = 0;
  mutable std::mutex statsMutex_;  // guards stats_, links_, workerStats_ (the pointer)
  std::shared_ptr<WorkerStats> workerStats_;
  SessionStats stats_;
  QList<ViewerLinkStats> links_;
  ViewerReports reports_;  // latest rx report per viewer (UI thread); copied into links_ under statsMutex_
};

}  // namespace framebeam
