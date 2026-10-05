#include "gamesession.h"

#include <QDir>

#include "libretro_backend.h"

namespace framebeam::ui {

using emu::EmulationRunner;

GameSession::GameSession(QObject* parent) : QObject(parent) {
  qRegisterMetaType<emu::AvInfo>();
  qRegisterMetaType<emu::CoreInfo>();
  qRegisterMetaType<EmulationRunner::State>();
}

GameSession::~GameSession() { teardown(); }

void GameSession::setState(State s) {
  if (state_ == s) {
    return;
  }
  state_ = s;
  emit stateChanged();
}

void GameSession::fail(const QString& msg) {
  error_ = msg;
  const bool before = !startedEmitted_;
  teardown();
  setState(Failed);
  emit errorChanged();
  if (before) {
    emit startFailed(msg);
  }
}

void GameSession::start(const LaunchConfig& config) {
  teardown();
  setState(Starting);
  error_.clear();
  emit errorChanged();
  frame_ = QImage();
  frameNr_ = 0;
  emit frameChanged();
  startedEmitted_ = false;
  title_ = config.title;
  emit titleChanged();
  display_ = config.display;
  keys_.clear();

  runner_ = std::make_unique<EmulationRunner>(std::make_unique<emu::LibretroBackend>());
  EmulationRunner* r = runner_.get();
  connect(r, &EmulationRunner::started, this, [this](const emu::AvInfo& av, const emu::CoreInfo&) {
    audio_.start(static_cast<int>(av.sampleRate + 0.5));
    startedEmitted_ = true;
    setState(Running);
    emit started();
  });
  connect(r, &EmulationRunner::startFailed, this, [this](const QString& e) { fail(e); });
  connect(r, &EmulationRunner::errorOccurred, this, [this](const QString& e) { fail(e); });
  connect(r, &EmulationRunner::frameReady, this, [this](const QImage& img, quint64 nr) {
    frame_ = img;
    frameNr_ = nr;
    emit frameChanged();
  });
  connect(r, &EmulationRunner::audioReady, this, [this](const QByteArray& pcm, int) { audio_.push(pcm); });
  connect(r, &EmulationRunner::stateChanged, this, [this](EmulationRunner::State s) {
    if (state_ == Failed || state_ == Idle) {
      return;
    }
    if (s == EmulationRunner::State::Paused) {
      setState(Paused);
    } else if (s == EmulationRunner::State::Running && startedEmitted_) {
      setState(Running);
    }
  });

  QDir().mkpath(config.systemDir);
  QDir().mkpath(config.saveDir);
  EmulationRunner::StartRequest req;
  req.corePath = config.corePath;
  req.gamePath = config.gamePath;
  req.systemDir = config.systemDir;
  req.saveDir = config.saveDir;
  req.coreOptions = config.coreOptions;
  r->start(req);
}

void GameSession::teardown() {
  if (runner_) {
    runner_->disconnect(this);
    runner_->stop();
    runner_.reset();
  }
  audio_.stop();
  keys_.clear();
}

void GameSession::pause() {
  if (runner_ && state_ == Running) {
    runner_->pause();
  }
}

void GameSession::resume() {
  if (runner_ && state_ == Paused) {
    runner_->resume();
  }
}

void GameSession::togglePause() {
  if (state_ == Running) {
    pause();
  } else if (state_ == Paused) {
    resume();
  }
}

void GameSession::reset() {
  if (runner_ && (state_ == Running || state_ == Paused)) {
    runner_->reset();
  }
}

void GameSession::stop() {
  const bool wasActive = state_ != Idle;
  teardown();
  setState(Idle);
  if (wasActive) {
    emit finished();
  }
}

bool GameSession::keyEvent(int qtKey, bool pressed) {
  const bool mapped = pressed ? keys_.press(qtKey) : keys_.release(qtKey);
  if (mapped && runner_) {
    runner_->setJoypadState(0, keys_.mask());
  }
  return mapped;
}

void GameSession::releaseAllKeys() {
  keys_.clear();
  if (runner_) {
    runner_->setJoypadState(0, 0);
    runner_->setPointer(0.0, 0.0, false);
  }
}

void GameSession::setPointer(const QPointF& p, bool pressed) {
  if (runner_) {
    runner_->setPointer(p.x(), p.y(), pressed);
  }
}

}  // namespace framebeam::ui
