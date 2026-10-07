#include "sessioncommands.h"

#include <QImage>
#include <QPainter>
#include <QTextStream>
#include <cmath>

#include "mediacaps.h"

namespace framebeam {

namespace {
QTextStream& out() {
  static QTextStream s(stdout);
  return s;
}
QTextStream& err() {
  static QTextStream s(stderr);
  return s;
}
constexpr int kW = 256;
constexpr int kH = 384;
constexpr int kRate = 32768;
}  // namespace

SessionCommands::SessionCommands(HubConnection* conn, HubLibrary* library, Finish finish, QObject* parent)
    : QObject(parent), conn_(conn), library_(library), finish_(std::move(finish)) {
  api_ = std::make_unique<SessionApi>(conn_);
  socket_ = std::make_unique<HubSocket>(conn_);
  ticker_.setInterval(1000);
  source_.setInterval(16);
  sink_.setInterval(10);
  retry_.setInterval(500);
  connect(socket_.get(), &HubSocket::connectionError, this,
          [](const QString& c, const QString& m) { err() << "WSS: " << c << " " << m << "\n"; });
  connect(socket_.get(), &HubSocket::hubError, this, [](const QString& c, const QString& m) { err() << "Hub: " << c << " " << m << "\n"; });
}

void SessionCommands::fail(const QString& msg) {
  err() << "ERROR " << msg << "\n";
  err().flush();
  ticker_.stop();
  source_.stop();
  sink_.stop();
  retry_.stop();
  finish_(1);
}

void SessionCommands::startSocket(std::function<void()> onReady) {
  connect(socket_.get(), &HubSocket::helloAcked, this, [onReady = std::move(onReady)]() { onReady(); }, Qt::SingleShotConnection);
  socket_->start();
}

// ---------------------------------------------------------------- share

bool SessionCommands::share(const QStringList& args) {
  bool synthetic = false;
  for (qsizetype i = 0; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--synthetic")) synthetic = true;
    else if (args[i] == QLatin1String("--game") && i + 1 < args.size()) gameId_ = args[++i];
    else if (args[i] == QLatin1String("--visibility") && i + 1 < args.size()) visibility_ = args[++i];
    else if (args[i] == QLatin1String("--seconds") && i + 1 < args.size()) seconds_ = args[++i].toInt();
    else if (args[i] == QLatin1String("--force-relay")) forceRelay_ = true;
    else {
      err() << "Unknown option " << args[i] << "\n";
      return false;
    }
  }
  if (!synthetic) {
    err() << "session-share currently needs --synthetic (no running core in the CLI)\n";
    return false;
  }
  if (gameId_.isEmpty()) {
    if (library_->games().isEmpty()) {
      err() << "No game in the library and no --game given\n";
      return false;
    }
    gameId_ = library_->games().first().id;
  }
  host_ = std::make_unique<SessionHost>();
  if (forceRelay_) {
    host_->setForceRelay(true);
  }
  connect(host_.get(), &SessionHost::errorOccurred, this, [](const QString& m) { err() << "Host: " << m << "\n"; });
  connect(host_.get(), &SessionHost::viewerConnected, this, [](const QString& v) {
    out() << "VIEWER-CONNECTED " << v << "\n";
    out().flush();
  });
  startSocket([this]() { publish(); });
  return true;
}

void SessionCommands::publish() {
  api_->publish(gameId_, visibility_, [this](const SessionApiResult& r) {
    if (!r.ok() || !r.session) {
      fail(QStringLiteral("publish: %1 (HTTP %2) %3").arg(r.errorCode).arg(r.status).arg(r.errorMessage));
      return;
    }
    sessionId_ = r.session->sessionId;
    host_->open(sessionId_, socket_->helloAck().iceServers, socket_->helloAck().turnServers);
    connect(host_.get(), &SessionHost::signalOut, socket_.get(), &HubSocket::sendSignal);
    connect(socket_.get(), &HubSocket::viewerJoined, host_.get(), [this](const ViewerJoined& v) {
      if (v.sessionId == sessionId_) {
        out() << "VIEWER-JOINED " << v.viewerId << " " << v.displayName << "\n";
        out().flush();
        host_->addViewer(v.viewerId);
      }
    });
    connect(socket_.get(), &HubSocket::viewerLeft, host_.get(), [this](const ViewerLeft& v) {
      if (v.sessionId == sessionId_) {
        host_->removeViewer(v.viewerId);
      }
    });
    connect(socket_.get(), &HubSocket::signalReceived, host_.get(), [this](const SessionSignal& s) { host_->handleSignal(s); });
    connect(socket_.get(), &HubSocket::sessionEnded, this, [this](const SessionEnded& e) {
      if (!ending_ && e.sessionId == sessionId_ && e.reason != QLatin1String("no_longer_visible")) {
        host_->close();
        out() << "SHARE-ENDED " << e.reason << "\n";
        out().flush();
        source_.stop();
        ticker_.stop();
        finish_(0);
      }
    });
    connect(&source_, &QTimer::timeout, this, &SessionCommands::feedSynthetic);
    connect(&ticker_, &QTimer::timeout, this, &SessionCommands::shareTick);
    source_.start();
    ticker_.start();
    out() << "SHARING session=" << sessionId_ << " game=" << gameId_ << " visibility=" << visibility_ << "\n";
    out().flush();
  });
}

void SessionCommands::feedSynthetic() {
  QImage img(kW, kH, QImage::Format_RGB32);
  img.fill(QColor(200, 60, 40));
  QPainter p(&img);
  p.fillRect((frameNo_ * 4) % (kW - 16), (frameNo_ * 3) % (kH - 16), 16, 16, Qt::white);
  p.end();
  ++frameNo_;
  host_->pushFrame(img);
  const int frames = kRate / 60;
  QByteArray pcm(frames * 4, 0);
  auto* s = reinterpret_cast<qint16*>(pcm.data());
  for (int i = 0; i < frames; ++i) {
    const auto v = static_cast<qint16>(12000.0 * std::sin(phase_));
    phase_ += 2.0 * M_PI * 440.0 / kRate;
    s[2 * i] = s[2 * i + 1] = v;
  }
  host_->pushAudio(pcm, kRate);
}

void SessionCommands::shareTick() {
  ++elapsed_;
  const SessionStats st = host_->stats();
  if (!st.connectionType.isEmpty()) {
    lastConnection_ = st.connectionType;
  }
  out() << "STATS host viewers=" << st.viewers << " encoder=" << (st.encoderName.isEmpty() ? QStringLiteral("-") : st.encoderName)
        << " fps=" << QString::number(st.fps, 'f', 1) << " video_kbps=" << QString::number(st.videoBitrateKbps, 'f', 0)
        << " audio_kbps=" << QString::number(st.audioBitrateKbps, 'f', 0) << " rtt_ms="
        << (st.rttMs ? QString::number(*st.rttMs, 'f', 1) : QStringLiteral("n/a")) << " link="
        << (st.connectionType.isEmpty() ? QStringLiteral("-") : QString(st.connectionType).replace(QLatin1Char(' '), QLatin1Char('_')))
        << " target_kbps=" << QString::number(st.targetBitrateKbps, 'f', 0) << "\n";
  out().flush();
  if (seconds_ > 0 && elapsed_ >= seconds_) {
    source_.stop();
    ticker_.stop();
    host_->close();
    ending_ = true;  // our own DELETE: its session_ended event must not race the SHARE-DONE line
    out() << "CONNECTION " << (lastConnection_.isEmpty() ? QStringLiteral("unknown") : lastConnection_) << "\n";
    api_->end(sessionId_, [this](const SessionApiResult& r) {
      out() << "SHARE-DONE " << (r.ok() ? "ended" : r.errorCode) << "\n";
      out().flush();
      finish_(0);
    });
  }
}

// ---------------------------------------------------------------- watch

bool SessionCommands::watch(const QStringList& args) {
  for (qsizetype i = 0; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--first")) first_ = true;
    else if (args[i] == QLatin1String("--session") && i + 1 < args.size()) sessionId_ = args[++i];
    else if (args[i] == QLatin1String("--seconds") && i + 1 < args.size()) seconds_ = args[++i].toInt();
    else if (args[i] == QLatin1String("--force-relay")) forceRelay_ = true;
    else {
      err() << "Unknown option " << args[i] << "\n";
      return false;
    }
  }
  if (!first_ && sessionId_.isEmpty()) {
    err() << "session-watch needs --session <id> or --first\n";
    return false;
  }
  if (seconds_ <= 0) {
    seconds_ = 10;
  }
  viewer_ = std::make_unique<SessionViewer>();
  if (forceRelay_) {
    viewer_->setForceRelay(true);
  }
  connect(viewer_.get(), &SessionViewer::errorOccurred, this, [](const QString& m) { err() << "Viewer: " << m << "\n"; });
  connect(viewer_.get(), &SessionViewer::frameReady, this, [this](const QImage&) { ++framesSeen_; });
  // The owner may offer before the join response reaches us: keep early signals until the viewer exists.
  connect(socket_.get(), &HubSocket::signalReceived, this, [this](const SessionSignal& s) {
    if (viewer_->isOpen()) {
      viewer_->handleSignal(s);
    } else if (joining_) {
      earlySignals_.append(s);
    }
  });
  connect(socket_.get(), &HubSocket::sessionEnded, this, [this](const SessionEnded& e) {
    if (e.sessionId == sessionId_ && e.reason != QLatin1String("no_longer_visible")) {
      ended_ = true;
      viewer_->close();
      out() << "SESSION-ENDED " << e.reason << "\n";
      out().flush();
    }
  });
  connect(socket_.get(), &HubSocket::viewerLeft, this, [this](const ViewerLeft& v) {
    if (v.viewerId == viewerId_) {
      ended_ = true;
      viewer_->close();
      out() << "VIEWER-LEFT " << v.reason << "\n";
      out().flush();
    }
  });
  startSocket([this]() { findAndJoin(); });
  return true;
}

void SessionCommands::findAndJoin() {
  if (!first_) {
    join(sessionId_);
    return;
  }
  // The share process may not have published yet: poll the Session list.
  connect(&retry_, &QTimer::timeout, this, [this]() {
    if (joining_) {
      return;
    }
    if (++findAttempts_ > 60) {
      retry_.stop();
      fail(QStringLiteral("no joinable Session found"));
      return;
    }
    api_->list([this](const SessionApiResult& r) {
      if (joining_ || !r.ok()) {
        return;
      }
      for (const SessionInfo& s : r.sessions) {
        if (!s.isOwner) {
          retry_.stop();
          join(s.sessionId);
          return;
        }
      }
    });
  });
  retry_.start();
}

void SessionCommands::join(const QString& sessionId) {
  joining_ = true;
  sessionId_ = sessionId;
  api_->join(sessionId, [this](const SessionApiResult& r) {
    if (!r.ok() || !r.join) {
      fail(QStringLiteral("join: %1 (HTTP %2) %3").arg(r.errorCode).arg(r.status).arg(r.errorMessage));
      return;
    }
    viewerId_ = r.join->viewerId;
    connect(viewer_.get(), &SessionViewer::signalOut, socket_.get(), &HubSocket::sendSignal);
    viewer_->open(sessionId_, viewerId_, r.join->iceServers.isEmpty() ? socket_->helloAck().iceServers : r.join->iceServers,
                  r.join->turnServers.isEmpty() ? socket_->helloAck().turnServers : r.join->turnServers);
    joining_ = false;
    const QList<SessionSignal> early = std::exchange(earlySignals_, {});
    for (const SessionSignal& s : early) {
      viewer_->handleSignal(s);
    }
    out() << "WATCHING session=" << sessionId_ << " viewer=" << viewerId_ << "\n";
    out().flush();
    connect(&sink_, &QTimer::timeout, this, [this]() { viewer_->pullAudio(480); });  // stands in for the audio output
    connect(&ticker_, &QTimer::timeout, this, &SessionCommands::watchTick);
    sink_.start();
    ticker_.start();
  });
}

void SessionCommands::watchTick() {
  ++elapsed_;
  const SessionStats st = viewer_->stats();
  if (!st.connectionType.isEmpty()) {
    lastConnection_ = st.connectionType;
  }
  out() << "STATS viewer frames=" << st.videoFrames << " audio_frames=" << st.audioFrames << " fps=" << QString::number(st.fps, 'f', 1)
        << " video_kbps=" << QString::number(st.videoBitrateKbps, 'f', 0) << " rtt_ms="
        << (st.rttMs ? QString::number(*st.rttMs, 'f', 1) : QStringLiteral("n/a")) << " link=" << (st.connectionType.isEmpty() ? QStringLiteral("-") : QString(st.connectionType).replace(QLatin1Char(' '), QLatin1Char('_'))) << " loss="
        << (st.packetLossPercent ? QString::number(*st.packetLossPercent, 'f', 2) + QStringLiteral("%") : QStringLiteral("n/a"))
        << " size=" << st.width << "x" << st.height << "\n";
  out().flush();
  if (elapsed_ < seconds_ && !ended_) {
    return;
  }
  ticker_.stop();
  sink_.stop();
  const SessionStats fin = viewer_->stats();
  out() << "CONNECTION " << (lastConnection_.isEmpty() ? QStringLiteral("unknown") : lastConnection_) << "\n";
  const bool good = fin.videoFrames >= 30 && fin.audioFrames > 0 && viewer_->audioPeak() > 0;
  out() << (good ? "WATCH-OK" : "WATCH-FAIL") << " frames=" << fin.videoFrames << " audio_frames=" << fin.audioFrames
        << " audio_peak=" << viewer_->audioPeak() << " size=" << fin.width << "x" << fin.height << "\n";
  out().flush();
  const QString sid = sessionId_, vid = viewerId_;
  viewer_->close();
  api_->removeViewer(sid, vid, [this, good](const SessionApiResult&) { finish_(good ? 0 : 1); });
}

}  // namespace framebeam
