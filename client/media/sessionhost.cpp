#include "sessionhost.h"

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <algorithm>
#include <cstring>
#include <rtc/rtc.hpp>

#include "rtcutil.h"

namespace framebeam {

namespace {
Q_LOGGING_CATEGORY(lcHost, "framebeam.sessionhost")

constexpr int kVideoPayloadType = 96;
constexpr int kAudioPayloadType = 111;
constexpr int kMaxFragment = 1200;
constexpr uint32_t kOpusClockRate = 48000;

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
}  // namespace

SessionHost::SessionHost(QObject* parent) : QObject(parent), bridge_(std::make_shared<ThreadBridge>(this)) {
  statsTimer_.setInterval(1000);
  connect(&statsTimer_, &QTimer::timeout, this, &SessionHost::updateStats);
  clock_.start();
}

SessionHost::~SessionHost() {
  bridge_->detach();
  close();
}

void SessionHost::open(const QString& sessionId, const QStringList& iceServers) {
  close();
  sessionId_ = sessionId;
  iceServers_ = iceServers;
  encoderFailed_ = false;
  open_ = true;
}

void SessionHost::close() {
  const QStringList ids = viewers_.keys();
  for (const QString& id : ids) {
    dropViewer(id, QStringLiteral("session_closed"));
  }
  stopEncoder();
  open_ = false;
}

// ---------------------------------------------------------------- viewers / signaling

void SessionHost::addViewer(const QString& viewerId) {
  if (!open_ || viewers_.contains(viewerId)) {
    return;
  }
  Viewer v;
  try {
    v.pc = std::make_shared<rtc::PeerConnection>(makeRtcConfig(iceServers_));
    const auto bridge = bridge_;
    const QString id = viewerId;

    const auto addTrack = [&](bool isVideo) {
      const rtc::SSRC ssrc = QRandomGenerator::global()->generate();
      const char* cname = isVideo ? "fb-video" : "fb-audio";
      std::shared_ptr<rtc::Track> track;
      if (isVideo) {
        rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
        media.addH264Codec(kVideoPayloadType);
        media.addSSRC(ssrc, cname);
        track = v.pc->addTrack(media);
      } else {
        rtc::Description::Audio media("audio", rtc::Description::Direction::SendOnly);
        media.addOpusCodec(kAudioPayloadType);
        media.addSSRC(ssrc, cname);
        track = v.pc->addTrack(media);
      }
      auto cfg = std::make_shared<rtc::RtpPacketizationConfig>(
          ssrc, cname, isVideo ? kVideoPayloadType : kAudioPayloadType,
          isVideo ? rtc::H264RtpPacketizer::ClockRate : kOpusClockRate);
      std::shared_ptr<rtc::RtpPacketizer> packetizer;
      if (isVideo) {
        packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, cfg, kMaxFragment);
      } else {
        packetizer = std::make_shared<rtc::RtpPacketizer>(cfg)  /* non-template base: one Opus frame per packet */;
      }
      packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(cfg));
      packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
      if (isVideo) {
        packetizer->addToChain(std::make_shared<rtc::PliHandler>(guarded("pli", [this, bridge]() {
          bridge->post([this]() {
            ++keyframeRequests_;
            encoder_.requestKeyframe();
          });
        })));
        // First frame must be an IDR even if the track opens in the middle of a GOP.
        track->onOpen(guarded("onOpen", [this, bridge]() { bridge->post([this]() { encoder_.requestKeyframe(); }); }));
      }
      track->setMediaHandler(packetizer);
      return track;
    };
    v.video = addTrack(true);
    v.audio = addTrack(false);

    v.pc->onLocalDescription(guarded("onLocalDescription", [this, bridge, id](rtc::Description d) {
      SessionSignal s;
      s.viewerId = id;
      s.kind = QString::fromStdString(d.typeString());
      s.sdp = QString::fromStdString(std::string(d));
      bridge->post([this, s]() mutable {
        s.sessionId = sessionId_;
        emit signalOut(s);
      });
    }));
    v.pc->onLocalCandidate(guarded("onLocalCandidate", [this, bridge, id](rtc::Candidate c) {
      SessionSignal s;
      s.viewerId = id;
      s.kind = QStringLiteral("candidate");
      s.candidate = QString::fromStdString(c.candidate());
      s.mid = QString::fromStdString(c.mid());
      bridge->post([this, s]() mutable {
        s.sessionId = sessionId_;
        emit signalOut(s);
      });
    }));
    v.pc->onStateChange(guarded("onStateChange", [this, bridge, id](rtc::PeerConnection::State st) {
      bridge->post([this, id, st]() { onPcState(id, static_cast<int>(st)); });
    }));
  } catch (const std::exception& e) {
    emit errorOccurred(QStringLiteral("PeerConnection: ") + QString::fromUtf8(e.what()));
    return;
  }
  Viewer& stored = viewers_.insert(viewerId, std::move(v)).value();
  encoder_.requestKeyframe();
  qCInfo(lcHost) << "Viewer" << viewerId << "added, offering";
  try {
    stored.pc->setLocalDescription(rtc::Description::Type::Offer);  // offer goes out via onLocalDescription
  } catch (const std::exception& e) {
    emit errorOccurred(QStringLiteral("Offer: ") + QString::fromUtf8(e.what()));
    dropViewer(viewerId, QStringLiteral("error"));
  }
}

void SessionHost::removeViewer(const QString& viewerId) { dropViewer(viewerId, QStringLiteral("removed")); }

void SessionHost::dropViewer(const QString& viewerId, const QString& reason) {
  auto it = viewers_.find(viewerId);
  if (it == viewers_.end()) {
    return;
  }
  Viewer v = std::move(it.value());
  viewers_.erase(it);
  QString finalState = QStringLiteral("closed");
  try {
    v.pc->resetCallbacks();
    if (v.video) {
      v.video->resetCallbacks();
    }
    if (v.audio) {
      v.audio->resetCallbacks();
    }
    v.pc->close();  // immediate: transports are shut down, the media path is cut
    finalState = stateName(v.pc->state());
  } catch (const std::exception& e) {
    qCWarning(lcHost) << "Closing PeerConnection:" << e.what();
  }
  v.video.reset();
  v.audio.reset();
  v.pc.reset();
  qCInfo(lcHost) << "Viewer" << viewerId << "closed (" << reason << ")," << viewers_.size() << "left";
  if (viewers_.isEmpty()) {
    stopEncoder();
  }
  emit viewerClosed(viewerId, reason + QLatin1Char(':') + finalState);
}

void SessionHost::onPcState(const QString& viewerId, int state) {
  auto it = viewers_.find(viewerId);
  if (it == viewers_.end()) {
    return;
  }
  const auto st = static_cast<rtc::PeerConnection::State>(state);
  it->state = stateName(st);
  if (st == rtc::PeerConnection::State::Connected && !it->connected) {
    it->connected = true;
    encoder_.requestKeyframe();
    emit viewerConnected(viewerId);
  } else if (st == rtc::PeerConnection::State::Failed || st == rtc::PeerConnection::State::Closed) {
    dropViewer(viewerId, QStringLiteral("connection_") + it->state);
  }
}

void SessionHost::applyRemoteCandidate(Viewer& v, const SessionSignal& s) {
  try {
    v.pc->addRemoteCandidate(rtc::Candidate(s.candidate.toStdString(), s.mid.toStdString()));
  } catch (const std::exception& e) {
    qCWarning(lcHost) << "Remote candidate rejected:" << e.what();
  }
}

void SessionHost::handleSignal(const SessionSignal& s) {
  auto it = viewers_.find(s.viewerId);
  if (it == viewers_.end() || (!sessionId_.isEmpty() && s.sessionId != sessionId_)) {
    return;
  }
  Viewer& v = it.value();
  try {
    if (s.kind == QLatin1String("answer")) {
      v.pc->setRemoteDescription(rtc::Description(s.sdp.toStdString(), rtc::Description::Type::Answer));
      v.remoteSet = true;
      const QList<SessionSignal> pending = std::exchange(v.pendingCandidates, {});
      for (const SessionSignal& c : pending) {
        applyRemoteCandidate(v, c);
      }
    } else if (s.kind == QLatin1String("candidate")) {
      if (v.remoteSet) {
        applyRemoteCandidate(v, s);
      } else {
        v.pendingCandidates.append(s);
      }
    }
  } catch (const std::exception& e) {
    emit errorOccurred(QStringLiteral("Signal: ") + QString::fromUtf8(e.what()));
    dropViewer(s.viewerId, QStringLiteral("error"));
  }
}

// ---------------------------------------------------------------- encoder / media input

void SessionHost::startEncoder(int width, int height) {
  if (!encoder_.open(width, height, options_.fps, options_.videoBitrate, options_.encoderOrder)) {
    encoderFailed_ = true;
    emit errorOccurred(QStringLiteral("No H.264 encoder can be opened"));
    return;
  }
  opus_.open(options_.audioBitrate);
  width_ = width;
  height_ = height;
  lastPts_ = -1;
  lastEncodeNs_ = 0;
  audioFramesSent_ = 0;
  clock_.restart();
  statsClock_.restart();
  statFrames_ = statVideoBytes_ = statAudioBytes_ = 0;
  lastStatFrames_ = lastStatVideoBytes_ = lastStatAudioBytes_ = 0;
  totalFrames_ = totalVideoBytes_ = totalAudioBytes_ = 0;
  statsTimer_.start();
  encoderRunning_ = true;
  emit encoderRunningChanged(true);
}

void SessionHost::stopEncoder() {
  if (!encoderRunning_ && !encoder_.isOpen()) {
    return;
  }
  statsTimer_.stop();
  encoder_.close();
  opus_.close();
  encoderRunning_ = false;
  stats_ = SessionStats();
  links_.clear();
  emit encoderRunningChanged(false);
}

void SessionHost::pushFrame(const QImage& image) {
  if (!open_ || viewers_.isEmpty() || image.isNull()) {
    return;
  }
  if (image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32 ||
      image.format() == QImage::Format_ARGB32_Premultiplied) {
    pushFrame(image.constBits(), image.width(), image.height(), static_cast<int>(image.bytesPerLine()), RawPixelFormat::Xrgb8888);
  } else if (image.format() == QImage::Format_RGB16) {
    pushFrame(image.constBits(), image.width(), image.height(), static_cast<int>(image.bytesPerLine()), RawPixelFormat::Rgb565);
  } else {
    const QImage conv = image.convertToFormat(QImage::Format_RGB32);
    pushFrame(conv.constBits(), conv.width(), conv.height(), static_cast<int>(conv.bytesPerLine()), RawPixelFormat::Xrgb8888);
  }
}

void SessionHost::pushFrame(const uint8_t* data, int width, int height, int stride, RawPixelFormat format) {
  if (!open_ || viewers_.isEmpty() || encoderFailed_ || data == nullptr) {
    return;  // encoding only while somebody watches
  }
  if (!encoderRunning_) {
    bool anyConnected = false;
    for (auto it = viewers_.constBegin(); it != viewers_.constEnd(); ++it) {
      anyConnected = anyConnected || it->connected;
    }
    if (!anyConnected) {
      return;  // the encoder starts when the first viewer has a media path
    }
  }
  if (encoderRunning_ && (width != width_ || height != height_)) {
    stopEncoder();  // resolution change: reopen (the first frame is a keyframe again)
  }
  if (!encoderRunning_) {
    startEncoder(width, height);
    if (!encoderRunning_) {
      return;
    }
  }
  encodeAndSend(data, width, height, stride, format);
}

void SessionHost::encodeAndSend(const uint8_t* data, int, int, int stride, RawPixelFormat format) {
  const qint64 nowNs = clock_.nsecsElapsed();
  const qint64 minGapNs = 1'000'000'000LL / (2 * std::max(1, options_.fps));
  if (lastEncodeNs_ != 0 && nowNs - lastEncodeNs_ < minGapNs) {
    return;  // source faster than 2x the target rate: drop
  }
  lastEncodeNs_ = nowNs;
  int64_t pts = static_cast<int64_t>(nowNs * options_.fps / 1'000'000'000.0 + 0.5);
  pts = std::max<int64_t>(pts, lastPts_ + 1);
  lastPts_ = pts;
  std::vector<EncodedVideoPacket> packets;
  if (!encoder_.encode(data, stride, format, pts, packets)) {
    emit errorOccurred(QStringLiteral("H.264 encoding failed"));
    return;
  }
  for (const EncodedVideoPacket& p : packets) {
    sendVideo(p);
  }
}

void SessionHost::sendVideo(const EncodedVideoPacket& p) {
  ++totalFrames_;
  totalVideoBytes_ += static_cast<qint64>(p.data.size());
  const double seconds = static_cast<double>(p.pts) / std::max(1, options_.fps);
  for (auto it = viewers_.begin(); it != viewers_.end(); ++it) {
    if (!it->video || !it->video->isOpen()) {
      continue;
    }
    try {
      it->video->sendFrame(toBinary(p.data.data(), p.data.size()), rtc::FrameInfo(std::chrono::duration<double>(seconds)));
    } catch (const std::exception& e) {
      qCWarning(lcHost) << "sendFrame:" << e.what();
    }
  }
}

void SessionHost::pushAudio(const QByteArray& pcm, int sampleRate) {
  if (!open_ || viewers_.isEmpty() || !encoderRunning_) {
    return;  // the audio encoder starts together with the video encoder (first frame)
  }
  std::vector<std::vector<uint8_t>> packets;
  opus_.push(pcm, sampleRate, packets);
  for (const auto& pk : packets) {
    sendAudio(pk);
  }
}

void SessionHost::sendAudio(const std::vector<uint8_t>& packet) {
  const double seconds = static_cast<double>(audioFramesSent_++) * 0.02;
  totalAudioBytes_ += static_cast<qint64>(packet.size());
  for (auto it = viewers_.begin(); it != viewers_.end(); ++it) {
    if (!it->audio || !it->audio->isOpen()) {
      continue;
    }
    try {
      it->audio->sendFrame(toBinary(packet.data(), packet.size()), rtc::FrameInfo(std::chrono::duration<double>(seconds)));
    } catch (const std::exception& e) {
      qCWarning(lcHost) << "sendFrame(audio):" << e.what();
    }
  }
}

// ---------------------------------------------------------------- stats

void SessionHost::updateStats() {
  const double secs = std::max(0.001, statsClock_.restart() / 1000.0);
  SessionStats s;
  s.active = encoderRunning_;
  s.fps = static_cast<double>(totalFrames_ - lastStatFrames_) / secs;
  s.videoBitrateKbps = static_cast<double>(totalVideoBytes_ - lastStatVideoBytes_) * 8.0 / 1000.0 / secs;
  s.audioBitrateKbps = static_cast<double>(totalAudioBytes_ - lastStatAudioBytes_) * 8.0 / 1000.0 / secs;
  lastStatFrames_ = totalFrames_;
  lastStatVideoBytes_ = totalVideoBytes_;
  lastStatAudioBytes_ = totalAudioBytes_;
  s.width = width_;
  s.height = height_;
  s.encoderName = encoder_.name();
  s.codec = QStringLiteral("H264 + Opus");
  s.viewers = static_cast<int>(viewers_.size());
  s.videoFrames = totalFrames_;
  s.audioFrames = audioFramesSent_;
  s.keyframeRequests = keyframeRequests_;
  // packet loss is not obtainable on the sending side (RTCP receiver reports are not exposed by libdatachannel)
  QList<ViewerLinkStats> links;
  int rank = -1;
  for (auto it = viewers_.begin(); it != viewers_.end(); ++it) {
    ViewerLinkStats l;
    l.viewerId = it.key();
    l.state = it->state;
    l.connectionType = QStringLiteral("unknown");
    if (it->connected && it->pc) {
      l.rttMs = rttOf(*it->pc);
      l.connectionType = connectionTypeOf(*it->pc);
    }
    if (l.rttMs && (!s.rttMs || *l.rttMs > *s.rttMs)) {
      s.rttMs = l.rttMs;
    }
    const int r = l.connectionType == QLatin1String("relay") ? 3 : l.connectionType == QLatin1String("srflx") ? 2
                  : l.connectionType == QLatin1String("host") ? 1 : 0;
    if (r > rank) {
      rank = r;
      s.connectionType = l.connectionType;
    }
    links.append(l);
  }
  stats_ = s;
  links_ = links;
}

SessionStats SessionHost::stats() const {
  SessionStats s = stats_;
  s.viewers = static_cast<int>(viewers_.size());
  s.active = encoderRunning_;
  if (encoderRunning_) {
    s.videoFrames = totalFrames_;
    s.audioFrames = audioFramesSent_;
    s.encoderName = encoder_.name();
    s.width = width_;
    s.height = height_;
    s.codec = QStringLiteral("H264 + Opus");
  }
  return s;
}

QList<ViewerLinkStats> SessionHost::viewerLinks() const { return links_; }

}  // namespace framebeam
