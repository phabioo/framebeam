#include "sessionhost.h"

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <rtc/rtc.hpp>
#include <thread>

#include "opuscodec.h"
#include "rtcutil.h"

namespace framebeam {

namespace {
Q_LOGGING_CATEGORY(lcHost, "framebeam.sessionhost")

constexpr int kVideoPayloadType = 96;
constexpr int kAudioPayloadType = 111;
constexpr int kMaxFragment = 1200;
constexpr uint32_t kOpusClockRate = 48000;
constexpr uint16_t kDiagChannelId = 0;

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

// ---------------------------------------------------------------- encode worker

struct Sink {
  std::shared_ptr<rtc::Track> video;
  std::shared_ptr<rtc::Track> audio;
};
}  // namespace

// Tracks the worker sends to; written by the owner thread, read by the worker.
struct SinkList {
  std::mutex mutex;
  std::vector<Sink> sinks;
  std::vector<Sink> get() {
    std::lock_guard<std::mutex> lock(mutex);
    return sinks;
  }
  void set(std::vector<Sink> s) {
    std::lock_guard<std::mutex> lock(mutex);
    sinks = std::move(s);
  }
};

// Counters of one worker run, readable from any thread.
struct WorkerStats {
  std::atomic<qint64> frames{0}, videoBytes{0}, audioBytes{0}, audioFrames{0}, dropped{0};
  mutable std::mutex mutex;
  QString encoderName;
  int width = 0, height = 0;
};

// Owns the H.264 encoder and the Opus framer and runs them on its own thread. Producers (owner thread) only copy
// data into a bounded queue. Video: at most kMaxPendingVideo frames wait, the oldest is dropped. Audio: ordered,
// bounded to ~1.4 s of PCM (only exceeded if the thread is stuck). Destruction joins the thread (pending work is
// discarded) and so guarantees that no callback or sendFrame() runs afterwards.
class EncodeWorker {
 public:
  static constexpr int kMaxPendingVideo = 2;
  static constexpr qint64 kMaxPendingAudioBytes = 256 * 1024;

  EncodeWorker(const SessionHost::Options& options, std::shared_ptr<SinkList> sinks, std::shared_ptr<WorkerStats> stats,
               std::function<void(QString)> onError, std::function<void()> onOpenFailed)
      : options_(options), sinks_(std::move(sinks)), stats_(std::move(stats)), onError_(std::move(onError)),
        onOpenFailed_(std::move(onOpenFailed)), thread_([this]() { run(); }) {}
  ~EncodeWorker() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
      queue_.clear();
    }
    cv_.notify_all();
    thread_.join();
  }
  EncodeWorker(const EncodeWorker&) = delete;
  EncodeWorker& operator=(const EncodeWorker&) = delete;

  void requestKeyframe() { keyframe_ = true; }

  void pushVideo(const uint8_t* data, int width, int height, int stride, RawPixelFormat format, qint64 ns) {
    Job j;
    j.video = true;
    j.width = width;
    j.height = height;
    j.stride = stride;
    j.format = format;
    j.ns = ns;
    const int bpp = format == RawPixelFormat::Rgb565 ? 2 : 4;
    const size_t bytes = static_cast<size_t>(stride) * static_cast<size_t>(height - 1) + static_cast<size_t>(width) * bpp;
    j.pixels.assign(data, data + bytes);  // copy outside the lock
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) return;
      int pending = 0;
      for (const Job& q : queue_) pending += q.video ? 1 : 0;
      for (auto it = queue_.begin(); pending >= kMaxPendingVideo && it != queue_.end();) {
        if (it->video) {
          it = queue_.erase(it);
          --pending;
          ++stats_->dropped;
        } else {
          ++it;
        }
      }
      queue_.push_back(std::move(j));
    }
    cv_.notify_one();
  }

  void pushAudio(const QByteArray& pcm, int rate) {
    Job j;
    j.video = false;
    j.pcm = pcm;  // implicitly shared, the producer does not modify it afterwards
    j.rate = rate;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stop_) return;
      pendingAudio_ += pcm.size();
      queue_.push_back(std::move(j));
      for (auto it = queue_.begin(); pendingAudio_ > kMaxPendingAudioBytes && it != queue_.end();) {
        if (!it->video) {
          pendingAudio_ -= it->pcm.size();
          // Keep the audio RTP clock in step with wall time: account the dropped PCM (S16 stereo) as elapsed time.
          if (it->rate > 0) droppedAudioSeconds_ += static_cast<double>(it->pcm.size()) / (kOpusChannels * sizeof(int16_t)) / it->rate;
          it = queue_.erase(it);
        } else {
          ++it;
        }
      }
    }
    cv_.notify_one();
  }

 private:
  struct Job {
    bool video = false;
    std::vector<uint8_t> pixels;
    int width = 0, height = 0, stride = 0;
    RawPixelFormat format = RawPixelFormat::Xrgb8888;
    qint64 ns = 0;
    QByteArray pcm;
    int rate = 0;
  };

  void run() {
    opus_.open(options_.audioBitrate);
    for (;;) {
      Job j;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
        if (stop_) break;
        j = std::move(queue_.front());
        queue_.pop_front();
        if (!j.video) {
          pendingAudio_ -= j.pcm.size();
          audioClockOffset_ += droppedAudioSeconds_;
          droppedAudioSeconds_ = 0;
        }
      }
      try {
        if (j.video) {
          encodeVideo(j);
        } else {
          encodeAudio(j);
        }
      } catch (const std::exception& e) {
        qCWarning(lcHost) << "Worker:" << e.what();
      }
    }
    encoder_.close();
    opus_.close();
  }

  void encodeVideo(const Job& j) {
    if (failed_) return;
    if (!encoder_.isOpen() || encoder_.width() != j.width || encoder_.height() != j.height) {
      encoder_.close();  // first frame or resolution change (the first frame is a keyframe again)
      if (!encoder_.open(j.width, j.height, options_.fps, options_.videoBitrate, options_.encoderOrder)) {
        failed_ = true;
        onOpenFailed_();
        return;
      }
      std::lock_guard<std::mutex> lock(stats_->mutex);
      stats_->encoderName = encoder_.name();
      stats_->width = j.width;
      stats_->height = j.height;
    }
    if (keyframe_.exchange(false)) {
      encoder_.requestKeyframe();
    }
    int64_t pts = static_cast<int64_t>(j.ns * options_.fps / 1'000'000'000.0 + 0.5);
    pts = std::max<int64_t>(pts, lastPts_ + 1);
    lastPts_ = pts;
    std::vector<EncodedVideoPacket> packets;
    if (!encoder_.encode(j.pixels.data(), j.stride, j.format, pts, packets)) {
      if (!encodeErrorReported_) {
        encodeErrorReported_ = true;
        onError_(QStringLiteral("H.264 encoding failed"));
      }
      return;
    }
    const std::vector<Sink> sinks = sinks_->get();
    for (const EncodedVideoPacket& p : packets) {
      ++stats_->frames;
      stats_->videoBytes += static_cast<qint64>(p.data.size());
      const double seconds = static_cast<double>(p.pts) / std::max(1, options_.fps);
      for (const Sink& sink : sinks) {
        send(sink.video, p.data.data(), p.data.size(), seconds, "video");
      }
    }
  }

  void encodeAudio(const Job& j) {
    if (!opus_.isOpen()) return;
    std::vector<std::vector<uint8_t>> packets;
    opus_.push(j.pcm, j.rate, packets);
    if (packets.empty()) return;
    const std::vector<Sink> sinks = sinks_->get();
    for (const auto& pk : packets) {
      const double seconds = static_cast<double>(audioFramesSent_++) * 0.02 + audioClockOffset_;
      ++stats_->audioFrames;
      stats_->audioBytes += static_cast<qint64>(pk.size());
      for (const Sink& sink : sinks) {
        send(sink.audio, pk.data(), pk.size(), seconds, "audio");
      }
    }
  }

  void send(const std::shared_ptr<rtc::Track>& track, const uint8_t* data, size_t size, double seconds, const char* what) {
    if (!track || !track->isOpen()) return;
    try {
      track->sendFrame(toBinary(data, size), rtc::FrameInfo(std::chrono::duration<double>(seconds)));
    } catch (const std::exception& e) {
      if (sendErrors_++ % 500 == 0) {  // per-frame failures must not flood the log
        qCWarning(lcHost) << "sendFrame(" << what << "):" << e.what() << "(failures so far:" << sendErrors_ << ")";
      }
    }
  }

  const SessionHost::Options options_;
  std::shared_ptr<SinkList> sinks_;
  std::shared_ptr<WorkerStats> stats_;
  std::function<void(QString)> onError_;
  std::function<void()> onOpenFailed_;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Job> queue_;
  qint64 pendingAudio_ = 0;
  double droppedAudioSeconds_ = 0;  // PCM dropped by pushAudio, not yet added to audioClockOffset_
  bool stop_ = false;
  std::atomic<bool> keyframe_{false};

  // Worker-thread only:
  VideoEncoder encoder_;
  OpusFramer opus_;
  bool failed_ = false;
  bool encodeErrorReported_ = false;
  int64_t lastPts_ = -1;
  qint64 audioFramesSent_ = 0;
  double audioClockOffset_ = 0;  // seconds of audio dropped under overload; keeps audio timestamps aligned with video
  int sendErrors_ = 0;

  std::thread thread_;  // last member: starts after everything else is constructed
};

SessionHost::SessionHost(QObject* parent) : QObject(parent), bridge_(std::make_shared<ThreadBridge>(this)) {
  sinks_ = std::make_shared<SinkList>();
  workerStats_ = std::make_shared<WorkerStats>();
  statsTimer_.setInterval(1000);
  connect(&statsTimer_, &QTimer::timeout, this, &SessionHost::updateStats);
  clock_.start();
}

SessionHost::~SessionHost() {
  bridge_->detach();
  close();  // joins the worker thread
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
    rtc::Configuration cfg = makeRtcConfig(iceServers_);
    cfg.disableAutoNegotiation = true;  // creating the data channel must not trigger an offer of its own; we offer explicitly below
    v.pc = std::make_shared<rtc::PeerConnection>(cfg);
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
            requestKeyframe();
          });
        })));
        // First frame must be an IDR even if the track opens in the middle of a GOP.
        track->onOpen(guarded("onOpen", [this, bridge]() { bridge->post([this]() { requestKeyframe(); }); }));
      }
      track->setMediaHandler(packetizer);
      return track;
    };
    v.video = addTrack(true);
    v.audio = addTrack(false);
    // Negotiated data channel (same SDP flow, no protocol change): establishes SCTP, which libdatachannel needs
    // to measure RTT (PeerConnection::rtt()). Never used for payload.
    rtc::DataChannelInit diagInit;
    diagInit.negotiated = true;
    diagInit.id = kDiagChannelId;
    v.diag = v.pc->createDataChannel("fb-diag", diagInit);

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
  viewerCountAtomic_ = static_cast<int>(viewers_.size());
  publishSinks();
  requestKeyframe();
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
  viewerCountAtomic_ = static_cast<int>(viewers_.size());
  publishSinks();  // the worker stops sending to this viewer before its PeerConnection closes
  QString finalState = QStringLiteral("closed");
  try {
    v.pc->resetCallbacks();
    if (v.video) {
      v.video->resetCallbacks();
    }
    if (v.audio) {
      v.audio->resetCallbacks();
    }
    if (v.diag) {
      v.diag->resetCallbacks();
    }
    v.pc->close();  // immediate: transports are shut down, the media path is cut
    // With SCTP up, libdatachannel finishes the state change to Closed asynchronously; the connection is cut now.
    finalState = v.pc->state() == rtc::PeerConnection::State::Failed ? QStringLiteral("failed") : QStringLiteral("closed");
  } catch (const std::exception& e) {
    qCWarning(lcHost) << "Closing PeerConnection:" << e.what();
  }
  v.video.reset();
  v.audio.reset();
  v.diag.reset();
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
    requestKeyframe();
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

void SessionHost::publishSinks() {
  std::vector<Sink> sinks;
  for (auto it = viewers_.constBegin(); it != viewers_.constEnd(); ++it) {
    sinks.push_back({it->video, it->audio});
  }
  sinks_->set(std::move(sinks));
}

void SessionHost::requestKeyframe() {
  if (worker_) {
    worker_->requestKeyframe();
  } else {
    keyframePending_ = true;
  }
}

void SessionHost::startEncoder() {
  auto stats = std::make_shared<WorkerStats>();
  {
    std::lock_guard<std::mutex> lock(statsMutex_);
    workerStats_ = stats;
  }
  const unsigned gen = ++workerGen_;
  const auto bridge = bridge_;
  worker_ = std::make_unique<EncodeWorker>(
      options_, sinks_, stats,
      [this, bridge](QString msg) { bridge->post([this, msg]() { emit errorOccurred(msg); }); },
      [this, bridge, gen]() {
        bridge->post([this, gen]() {
          if (gen != workerGen_ || !worker_) return;
          encoderFailed_ = true;
          emit errorOccurred(QStringLiteral("No H.264 encoder can be opened"));
          stopEncoder();
        });
      });
  keyframePending_ = false;  // a fresh encoder starts with a keyframe anyway
  lastEncodeNs_ = 0;
  clock_.restart();
  statsClock_.restart();
  lastStatFrames_ = lastStatVideoBytes_ = lastStatAudioBytes_ = 0;
  statsTimer_.start();
  encoderRunning_ = true;
  emit encoderRunningChanged(true);
}

void SessionHost::stopEncoder() {
  if (!encoderRunning_ && !worker_) {
    return;
  }
  statsTimer_.stop();
  worker_.reset();  // joins the worker thread: no encode or send runs after this line
  encoderRunning_ = false;
  {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = SessionStats();
    links_.clear();
    workerStats_ = std::make_shared<WorkerStats>();
  }
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
  if (!open_ || viewers_.isEmpty() || encoderFailed_ || data == nullptr || width <= 0 || height <= 0) {
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
    startEncoder();
  }
  const qint64 nowNs = clock_.nsecsElapsed();
  const qint64 minGapNs = 1'000'000'000LL / (2 * std::max(1, options_.fps));
  if (lastEncodeNs_ != 0 && nowNs - lastEncodeNs_ < minGapNs) {
    return;  // source faster than 2x the target rate: drop before copying
  }
  lastEncodeNs_ = nowNs;
  if (keyframePending_) {
    keyframePending_ = false;
    worker_->requestKeyframe();
  }
  worker_->pushVideo(data, width, height, stride, format, nowNs);  // copies; encoding happens on the worker thread
}

void SessionHost::pushAudio(const QByteArray& pcm, int sampleRate) {
  if (!open_ || viewers_.isEmpty() || !worker_) {
    return;  // the audio encoder starts together with the video encoder (first frame)
  }
  worker_->pushAudio(pcm, sampleRate);
}

QString SessionHost::encoderName() const { return stats().encoderName; }

// ---------------------------------------------------------------- stats

void SessionHost::updateStats() {
  std::shared_ptr<WorkerStats> ws;
  {
    std::lock_guard<std::mutex> lock(statsMutex_);
    ws = workerStats_;
  }
  const double secs = std::max(0.001, statsClock_.restart() / 1000.0);
  const qint64 frames = ws->frames, vBytes = ws->videoBytes, aBytes = ws->audioBytes;
  SessionStats s;
  s.active = encoderRunning_;
  s.fps = static_cast<double>(frames - lastStatFrames_) / secs;
  s.videoBitrateKbps = static_cast<double>(vBytes - lastStatVideoBytes_) * 8.0 / 1000.0 / secs;
  s.audioBitrateKbps = static_cast<double>(aBytes - lastStatAudioBytes_) * 8.0 / 1000.0 / secs;
  lastStatFrames_ = frames;
  lastStatVideoBytes_ = vBytes;
  lastStatAudioBytes_ = aBytes;
  s.codec = QStringLiteral("H264 + Opus");
  s.viewers = static_cast<int>(viewers_.size());
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
  std::lock_guard<std::mutex> lock(statsMutex_);
  stats_ = s;
  links_ = links;
}

SessionStats SessionHost::stats() const {
  std::lock_guard<std::mutex> lock(statsMutex_);
  SessionStats s = stats_;
  s.viewers = viewerCountAtomic_;
  s.active = encoderRunning_;
  if (encoderRunning_ && workerStats_) {
    s.videoFrames = workerStats_->frames;
    s.audioFrames = workerStats_->audioFrames;
    s.droppedFrames = workerStats_->dropped;
    s.codec = QStringLiteral("H264 + Opus");
    std::lock_guard<std::mutex> wl(workerStats_->mutex);
    s.encoderName = workerStats_->encoderName;
    s.width = workerStats_->width;
    s.height = workerStats_->height;
  }
  return s;
}

QList<ViewerLinkStats> SessionHost::viewerLinks() const {
  std::lock_guard<std::mutex> lock(statsMutex_);
  return links_;
}

}  // namespace framebeam
