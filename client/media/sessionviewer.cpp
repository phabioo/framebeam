#include "sessionviewer.h"

#include <QLoggingCategory>
#include <QMutexLocker>
#include <algorithm>
#include <cstring>
#include <rtc/rtc.hpp>

#include "opusdepacketizer.h"
#include "rtcutil.h"

namespace framebeam {

namespace {
Q_LOGGING_CATEGORY(lcViewer, "framebeam.sessionviewer")

QString stateName(rtc::PeerConnection::State s) {
  using S = rtc::PeerConnection::State;
  switch (s) {
    case S::New: return QStringLiteral("new");
    case S::Connecting: return QStringLiteral("connecting");
    case S::Connected: return QStringLiteral("connected");
    case S::Disconnected: return QStringLiteral("disconnected");
    case S::Failed: return QStringLiteral("failed");
    case S::Closed: return QStringLiteral("closed");
  }
  return QStringLiteral("new");
}

constexpr int kBytesPerMs = 48 * 4;  // 48 kHz stereo int16
}  // namespace

// RTCP receiving session that also exposes the loss derived from RTP sequence numbers (RFC 3550 A.3).
class LossReceivingSession : public rtc::RtcpReceivingSession {
 public:
  void incoming(rtc::message_vector& messages, const rtc::message_callback& send) override {
    rtc::RtcpReceivingSession::incoming(messages, send);
    std::lock_guard<std::mutex> lock(statMutex_);
    if (mReceived > 0) {
      expected_ = static_cast<int64_t>(mCycles) + mMaxSeq - mBaseSeq + 1;
      received_ = mReceived;
    }
  }
  // nullopt until enough packets were seen.
  std::optional<double> lossPercent() {
    std::lock_guard<std::mutex> lock(statMutex_);
    if (expected_ < 50) {
      return std::nullopt;
    }
    const int64_t lost = std::max<int64_t>(0, expected_ - received_);
    return 100.0 * static_cast<double>(lost) / static_cast<double>(expected_);
  }

 private:
  std::mutex statMutex_;
  int64_t expected_ = 0;
  int64_t received_ = 0;
};

SessionViewer::SessionViewer(QObject* parent) : QObject(parent), bridge_(std::make_shared<ThreadBridge>(this)) {
  statsTimer_.setInterval(1000);
  connect(&statsTimer_, &QTimer::timeout, this, &SessionViewer::updateStats);
}

SessionViewer::~SessionViewer() {
  bridge_->detach();
  teardown();
}

void SessionViewer::open(const QString& sessionId, const QString& viewerId, const QStringList& iceServers) {
  close();
  sessionId_ = sessionId;
  viewerId_ = viewerId;
  iceServers_ = iceServers;
  open_ = true;
  remoteSet_ = false;
  pendingCandidates_.clear();
  totalFrames_ = totalAudioFrames_ = totalVideoBytes_ = totalAudioBytes_ = 0;
  lastFrames_ = lastVideoBytes_ = lastAudioBytes_ = 0;
  decodeErrors_ = pliCount_ = 0;
  audioPeak_ = 0;
  pliSent_ = false;
  stats_ = SessionStats();
  link_ = ViewerLinkStats{viewerId, QStringLiteral("new"), std::nullopt, QStringLiteral("unknown")};
  {
    QMutexLocker lock(&audioMutex_);
    audioBuf_.clear();
    audioPrimed_ = false;
  }
  if (!decoder_.open()) {
    emit errorOccurred(QStringLiteral("No H.264 decoder available"));
  }
  opus_.open();
  statsClock_.restart();
  statsTimer_.start();
}

void SessionViewer::teardown() {
  statsTimer_.stop();
  if (pc_) {
    try {
      pc_->resetCallbacks();
      if (video_) video_->resetCallbacks();
      if (audio_) audio_->resetCallbacks();
      pc_->close();
    } catch (const std::exception& e) {
      qCWarning(lcViewer) << "Closing PeerConnection:" << e.what();
    }
  }
  video_.reset();
  audio_.reset();
  videoSession_.reset();
  pc_.reset();
  decoder_.close();
}

void SessionViewer::close() {
  const bool wasConnected = connected_;
  teardown();
  open_ = false;
  connected_ = false;
  remoteSet_ = false;
  pcState_ = QStringLiteral("closed");
  link_.state = pcState_;
  if (wasConnected) {
    emit connectedChanged(false);
  }
}

// ---------------------------------------------------------------- signaling

void SessionViewer::handleSignal(const SessionSignal& s) {
  if (!open_ || s.viewerId != viewerId_ || s.sessionId != sessionId_) {
    return;
  }
  try {
    if (s.kind == QLatin1String("offer")) {
      if (pc_) {
        return;  // renegotiation is not part of the PoC
      }
      pc_ = std::make_shared<rtc::PeerConnection>(makeRtcConfig(iceServers_));
      const auto bridge = bridge_;
      pc_->onTrack(guarded("onTrack", [this, bridge](std::shared_ptr<rtc::Track> track) {
        const std::string type = track->description().type();
        if (type == "video") {
          auto depack = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::LongStartSequence);
          auto session = std::make_shared<LossReceivingSession>();
          depack->addToChain(session);
          track->setMediaHandler(depack);
          track->onFrame(guarded("onFrame", [this, bridge](rtc::binary data, rtc::FrameInfo) {
            QByteArray ba(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
            bridge->post([this, ba = std::move(ba)]() mutable { onVideoFrame(std::move(ba)); });
          }));
          bridge->post([this, track, session]() {
            if (open_ && pc_) {
              video_ = track;
              videoSession_ = session;
            }
          });
        } else if (type == "audio") {
          auto depack = std::make_shared<OpusRtpDepacketizer>();
          depack->addToChain(std::make_shared<rtc::RtcpReceivingSession>());
          track->setMediaHandler(depack);
          track->onFrame(guarded("onFrame", [this, bridge](rtc::binary data, rtc::FrameInfo) {
            QByteArray ba(reinterpret_cast<const char*>(data.data()), static_cast<qsizetype>(data.size()));
            bridge->post([this, ba = std::move(ba)]() mutable { onAudioFrame(std::move(ba)); });
          }));
          bridge->post([this, track]() {
            if (open_ && pc_) {
              audio_ = track;
            }
          });
        }
      }));
      pc_->onLocalDescription(guarded("onLocalDescription", [this, bridge](rtc::Description d) {
        SessionSignal out;
        out.kind = QString::fromStdString(d.typeString());
        out.sdp = QString::fromStdString(std::string(d));
        bridge->post([this, out]() mutable {
          out.sessionId = sessionId_;
          out.viewerId = viewerId_;
          emit signalOut(out);
        });
      }));
      pc_->onLocalCandidate(guarded("onLocalCandidate", [this, bridge](rtc::Candidate c) {
        SessionSignal out;
        out.kind = QStringLiteral("candidate");
        out.candidate = QString::fromStdString(c.candidate());
        out.mid = QString::fromStdString(c.mid());
        bridge->post([this, out]() mutable {
          out.sessionId = sessionId_;
          out.viewerId = viewerId_;
          emit signalOut(out);
        });
      }));
      pc_->onStateChange(guarded("onStateChange", [this, bridge](rtc::PeerConnection::State st) {
        bridge->post([this, st]() { onPcState(static_cast<int>(st)); });
      }));
      // The answer is produced automatically (onLocalDescription) once the offer is applied.
      pc_->setRemoteDescription(rtc::Description(s.sdp.toStdString(), rtc::Description::Type::Offer));
      remoteSet_ = true;
      const QList<SessionSignal> pending = std::exchange(pendingCandidates_, {});
      for (const SessionSignal& c : pending) {
        handleSignal(c);
      }
    } else if (s.kind == QLatin1String("candidate")) {
      if (!pc_ || !remoteSet_) {
        pendingCandidates_.append(s);
        return;
      }
      pc_->addRemoteCandidate(rtc::Candidate(s.candidate.toStdString(), s.mid.toStdString()));
    }
  } catch (const std::exception& e) {
    emit errorOccurred(QStringLiteral("Signal: ") + QString::fromUtf8(e.what()));
  }
}

void SessionViewer::onPcState(int state) {
  if (!open_ || !pc_) {
    return;
  }
  const auto st = static_cast<rtc::PeerConnection::State>(state);
  pcState_ = stateName(st);
  link_.state = pcState_;
  if (st == rtc::PeerConnection::State::Connected) {
    if (!connected_) {
      connected_ = true;
      emit connectedChanged(true);
      requestKeyframe();
    }
  } else if (st == rtc::PeerConnection::State::Failed || st == rtc::PeerConnection::State::Closed ||
             st == rtc::PeerConnection::State::Disconnected) {
    if (connected_) {
      connected_ = false;
      emit connectedChanged(false);
    }
    if (st != rtc::PeerConnection::State::Disconnected) {
      emit closed(pcState_);
    }
  }
}

// ---------------------------------------------------------------- media

void SessionViewer::requestKeyframe() {
  if (!video_) {
    return;
  }
  if (pliSent_ && pliClock_.elapsed() < 500) {
    return;
  }
  pliSent_ = true;
  pliClock_.restart();
  ++pliCount_;
  try {
    video_->requestKeyframe();
  } catch (const std::exception& e) {
    qCWarning(lcViewer) << "PLI:" << e.what();
  }
}

void SessionViewer::onVideoFrame(QByteArray data) {
  if (!open_) {
    return;
  }
  totalVideoBytes_ += data.size();
  std::vector<QImage> frames;
  if (!decoder_.decode(reinterpret_cast<const uint8_t*>(data.constData()), static_cast<size_t>(data.size()), frames)) {
    ++decodeErrors_;
    requestKeyframe();
  }
  for (QImage& f : frames) {
    ++totalFrames_;
    width_ = f.width();
    height_ = f.height();
    emit frameReady(f);
  }
}

void SessionViewer::onAudioFrame(QByteArray data) {
  if (!open_) {
    return;
  }
  totalAudioBytes_ += data.size();
  const QByteArray pcm = opus_.decode(reinterpret_cast<const uint8_t*>(data.constData()), static_cast<size_t>(data.size()));
  if (pcm.isEmpty()) {
    return;
  }
  ++totalAudioFrames_;
  const auto* s = reinterpret_cast<const int16_t*>(pcm.constData());
  int peak = audioPeak_.load();
  for (qsizetype i = 0; i < pcm.size() / 2; ++i) {
    peak = std::max(peak, std::abs(static_cast<int>(s[i])));
  }
  audioPeak_ = peak;
  QMutexLocker lock(&audioMutex_);
  audioBuf_.append(pcm);
  const int maxBytes = kJitterMaxMs * kBytesPerMs;
  if (audioBuf_.size() > maxBytes) {  // too far behind: drop the oldest audio down to the target
    audioBuf_.remove(0, audioBuf_.size() - kJitterTargetMs * kBytesPerMs);
  }
}

QByteArray SessionViewer::pullAudio(int frames) {
  QByteArray out(frames * 4, 0);
  QMutexLocker lock(&audioMutex_);
  if (!audioPrimed_) {
    if (audioBuf_.size() < kJitterTargetMs * kBytesPerMs) {
      return out;
    }
    audioPrimed_ = true;
  }
  const qsizetype n = std::min<qsizetype>(out.size(), audioBuf_.size());
  std::memcpy(out.data(), audioBuf_.constData(), static_cast<size_t>(n));
  audioBuf_.remove(0, n);
  if (n < out.size()) {
    audioPrimed_ = false;  // underrun: refill to the target before playing on
  }
  return out;
}

int SessionViewer::bufferedAudioMs() const {
  QMutexLocker lock(&audioMutex_);
  return static_cast<int>(audioBuf_.size() / kBytesPerMs);
}

// ---------------------------------------------------------------- stats

void SessionViewer::updateStats() {
  const double secs = std::max(0.001, statsClock_.restart() / 1000.0);
  SessionStats s;
  s.active = connected_;
  s.fps = static_cast<double>(totalFrames_ - lastFrames_) / secs;
  s.videoBitrateKbps = static_cast<double>(totalVideoBytes_ - lastVideoBytes_) * 8.0 / 1000.0 / secs;
  s.audioBitrateKbps = static_cast<double>(totalAudioBytes_ - lastAudioBytes_) * 8.0 / 1000.0 / secs;
  lastFrames_ = totalFrames_;
  lastVideoBytes_ = totalVideoBytes_;
  lastAudioBytes_ = totalAudioBytes_;
  s.width = width_;
  s.height = height_;
  s.codec = QStringLiteral("H264 + Opus");
  s.connectionType = QStringLiteral("unknown");
  if (connected_ && pc_) {
    s.rttMs = rttOf(*pc_);
    s.connectionType = connectionTypeOf(*pc_);
  }
  if (videoSession_) {
    s.packetLossPercent = videoSession_->lossPercent();
  }
  s.viewers = 1;
  link_.rttMs = s.rttMs;
  link_.connectionType = s.connectionType;
  stats_ = s;
}

SessionStats SessionViewer::stats() const {
  SessionStats s = stats_;
  s.active = connected_;
  s.videoFrames = totalFrames_;
  s.audioFrames = totalAudioFrames_;
  s.decodeErrors = decodeErrors_;
  s.keyframeRequests = pliCount_;
  s.width = width_;
  s.height = height_;
  s.codec = QStringLiteral("H264 + Opus");
  return s;
}

ViewerLinkStats SessionViewer::link() const { return link_; }

}  // namespace framebeam
