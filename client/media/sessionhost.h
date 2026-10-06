#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <vector>

#include "mediastats.h"
#include "opuscodec.h"
#include "sessiontypes.h"
#include "videoencoder.h"

namespace rtc {
class PeerConnection;
class Track;
class RtpPacketizationConfig;
}  // namespace rtc

namespace framebeam {

class ThreadBridge;

// Owner side of a Session (ADR 0006 D5): one encoder for all viewers (running only while at least one viewer is
// there), per viewer a PeerConnection with a sendonly H.264 and a sendonly Opus track. Signaling is decoupled:
// wire addViewer()/removeViewer()/handleSignal()/close() to the Hub messages and signalOut() to HubSocket.
// Not thread-safe: use and feed it from one thread (the thread it lives in; use queued connections otherwise).
class SessionHost : public QObject {
  Q_OBJECT
 public:
  struct Options {
    int fps = 60;
    int videoBitrate = 2'000'000;  // fixed in phase 4
    int audioBitrate = 96'000;
    QStringList encoderOrder;      // empty: ADR 0006 D5 preference
  };

  explicit SessionHost(QObject* parent = nullptr);
  ~SessionHost() override;

  void setOptions(const Options& o) { options_ = o; }
  // Session published: signaling and viewers refer to this id.
  void open(const QString& sessionId, const QStringList& iceServers);
  // Session ended or stopped sharing: closes every viewer immediately and stops the encoder.
  void close();
  bool isOpen() const { return open_; }

  int viewerCount() const { return static_cast<int>(viewers_.size()); }
  bool hasViewer(const QString& viewerId) const { return viewers_.contains(viewerId); }
  bool encoderRunning() const { return encoderRunning_; }
  QString encoderName() const { return encoder_.name(); }
  // Last full second, aggregated over viewers; cheap.
  SessionStats stats() const;
  QList<ViewerLinkStats> viewerLinks() const;

  // Video: raw libretro frame (XRGB8888/RGB565, stride in bytes) or a QImage (any format, converted). The first
  // frame after a viewer joined fixes the encoder resolution; a size change reopens the encoder.
  void pushFrame(const uint8_t* data, int width, int height, int stride, RawPixelFormat format);
  void pushFrame(const QImage& image);
  // Audio: interleaved int16 stereo at the core rate (any rate), e.g. straight from the emulation callback.
  void pushAudio(const QByteArray& pcm, int sampleRate);

 public slots:
  void addViewer(const QString& viewerId);     // `viewer_joined`: creates the PeerConnection and sends the offer
  void removeViewer(const QString& viewerId);  // `viewer_left`: closes the PeerConnection immediately
  void handleSignal(const framebeam::SessionSignal& signal);  // answer / candidate from a viewer

 signals:
  void signalOut(const framebeam::SessionSignal& signal);
  void viewerConnected(const QString& viewerId);  // media path established
  void viewerClosed(const QString& viewerId, const QString& reason);  // PeerConnection closed (removed, failed, ...)
  void encoderRunningChanged(bool running);
  void errorOccurred(const QString& message);  // e.g. no encoder can be opened

 private:
  struct Viewer {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video;
    std::shared_ptr<rtc::Track> audio;
    QList<SessionSignal> pendingCandidates;
    bool remoteSet = false;
    bool connected = false;
    QString state = QStringLiteral("new");
  };

  void dropViewer(const QString& viewerId, const QString& reason);
  void onPcState(const QString& viewerId, int state);
  void startEncoder(int width, int height);
  void stopEncoder();
  void encodeAndSend(const uint8_t* data, int width, int height, int stride, RawPixelFormat format);
  void sendVideo(const EncodedVideoPacket& p);
  void sendAudio(const std::vector<uint8_t>& packet);
  void updateStats();
  void applyRemoteCandidate(Viewer& v, const SessionSignal& s);

  Options options_;
  QString sessionId_;
  QStringList iceServers_;
  bool open_ = false;
  QHash<QString, Viewer> viewers_;
  std::shared_ptr<ThreadBridge> bridge_;

  VideoEncoder encoder_;
  OpusFramer opus_;
  bool encoderRunning_ = false;
  bool encoderFailed_ = false;
  QElapsedTimer clock_;
  int64_t lastPts_ = -1;
  int sendErrors_ = 0;  // sendFrame exceptions (log rate limit)
  qint64 lastEncodeNs_ = 0;
  qint64 audioFramesSent_ = 0;
  int width_ = 0;
  int height_ = 0;

  QTimer statsTimer_;
  QElapsedTimer statsClock_;
  qint64 statFrames_ = 0, statVideoBytes_ = 0, statAudioBytes_ = 0;
  qint64 totalFrames_ = 0, totalVideoBytes_ = 0, totalAudioBytes_ = 0;
  qint64 lastStatFrames_ = 0, lastStatVideoBytes_ = 0, lastStatAudioBytes_ = 0;
  int keyframeRequests_ = 0;
  SessionStats stats_;
  QList<ViewerLinkStats> links_;
};

}  // namespace framebeam
