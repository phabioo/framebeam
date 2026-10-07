#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <memory>

#include "bitratecontroller.h"
#include "mediastats.h"
#include "opuscodec.h"
#include "sessiontypes.h"
#include "videodecoder.h"

namespace rtc {
class PeerConnection;
class Track;
class DataChannel;
}  // namespace rtc

namespace framebeam {

class ThreadBridge;
class LossReceivingSession;

// Viewer side of a Session: answers the owner's offer recvonly, depacketizes H.264 and Opus, decodes to QImage
// frames (frameReady) and 48 kHz stereo int16 audio behind a ~60 ms jitter buffer (pullAudio, callable from an
// audio thread). Decode errors trigger a PLI (rate-limited).
class SessionViewer : public QObject {
  Q_OBJECT
 public:
  static constexpr int kJitterTargetMs = 60;
  static constexpr int kJitterMaxMs = 240;

  explicit SessionViewer(QObject* parent = nullptr);
  ~SessionViewer() override;

  // After the REST join: signaling for (sessionId, viewerId) is accepted from now on.
  // `turnServers`: relay credentials from the join response / hello_ack (ADR 0012 D3-D5).
  void open(const QString& sessionId, const QString& viewerId, const QStringList& iceServers,
            const QList<TurnServer>& turnServers = {});
  // ICE transport policy "relay" for the PeerConnection created from now on (default: FRAMEBEAM_FORCE_RELAY=1).
  void setForceRelay(bool force) { forceRelay_ = force; }
  bool forceRelay() const { return forceRelay_; }
  // Leave / Session ended / viewer removed: closes the PeerConnection immediately.
  void close();
  bool isOpen() const { return open_; }
  bool isConnected() const { return connected_; }
  QString viewerId() const { return viewerId_; }

  // Audio output pull API: returns exactly `frames` frames (4 bytes each) of 48 kHz stereo int16; silence while the
  // jitter buffer fills or runs dry. Thread-safe.
  QByteArray pullAudio(int frames);
  int bufferedAudioMs() const;
  // Highest absolute sample value of everything decoded so far (0 = only silence arrived).
  int audioPeak() const { return audioPeak_.load(); }

  SessionStats stats() const;
  ViewerLinkStats link() const;
  void requestKeyframe();  // sends a PLI now (rate-limited to one per 500 ms)

 public slots:
  void handleSignal(const framebeam::SessionSignal& signal);  // offer / candidate from the owner

 signals:
  void signalOut(const framebeam::SessionSignal& signal);
  void frameReady(const QImage& frame);
  void connectedChanged(bool connected);
  void closed(const QString& reason);  // the PeerConnection failed or closed on its own
  void errorOccurred(const QString& message);

 private:
  void onPcState(int state);
  void onVideoFrame(QByteArray data);
  void onAudioFrame(QByteArray data);
  void updateStats();
  void sendRxReport(double videoKbps);
  void teardown();

  QString sessionId_;
  QString viewerId_;
  QStringList iceServers_;
  QList<TurnServer> turnServers_;
  bool forceRelay_ = false;
  bool relayTcpOnly_ = false;
  bool open_ = false;
  unsigned pcGen_ = 0;  // UI thread only; bumped by teardown()
  bool connected_ = false;
  bool remoteSet_ = false;
  QString pcState_ = QStringLiteral("new");
  QList<SessionSignal> pendingCandidates_;
  std::shared_ptr<ThreadBridge> bridge_;
  std::shared_ptr<rtc::PeerConnection> pc_;
  std::shared_ptr<rtc::Track> video_;
  std::shared_ptr<rtc::Track> audio_;
  std::shared_ptr<rtc::DataChannel> diag_;  // negotiated "fb-diag": SCTP for RTT
  std::shared_ptr<LossReceivingSession> videoSession_;

  VideoDecoder decoder_;
  OpusDepacker opus_;
  QElapsedTimer pliClock_;
  bool pliSent_ = false;

  mutable QMutex audioMutex_;
  QByteArray audioBuf_;
  bool audioPrimed_ = false;
  std::atomic<int> audioPeak_{0};

  QTimer statsTimer_;
  QElapsedTimer statsClock_;
  qint64 totalFrames_ = 0, totalAudioFrames_ = 0, totalVideoBytes_ = 0, totalAudioBytes_ = 0;
  qint64 lastFrames_ = 0, lastVideoBytes_ = 0, lastAudioBytes_ = 0;
  int decodeErrors_ = 0;
  int pliCount_ = 0;
  int width_ = 0, height_ = 0;
  int64_t lastExpected_ = 0, lastReceived_ = 0;  // RTP counters at the previous rx report
  SessionStats stats_;
  ViewerLinkStats link_;
};

}  // namespace framebeam
