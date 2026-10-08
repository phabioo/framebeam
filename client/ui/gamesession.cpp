#include "gamesession.h"

#include <QDir>
#include <QThread>

#include "screenlayout.h"

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
  coreOptions_ = config.coreOptions;
  coreName_.clear();
  targetFps_ = 0;
  preview_ = false;
  audio_.setUnderrunCounting(true);
  keys_.clear();
  fastForward_ = false;
  fastForwardSupported_ = false;
  ffRatio_ = emu::EmulationRunner::normalizeSpeedUpRatio(config.speedUpRatio);
  ffAudio_ = config.speedUpAudio;
  emit fastForwardChanged();

  runner_ = std::make_unique<EmulationRunner>(std::make_unique<emu::LibretroBackend>());
  EmulationRunner* r = runner_.get();
  connect(r, &EmulationRunner::started, this, [this](const emu::AvInfo& av, const emu::CoreInfo& core) {
    coreName_ = core.version.isEmpty() ? core.name : core.name + QLatin1Char(' ') + core.version;
    targetFps_ = av.fps;
    fastForwardSupported_ = runner_ && runner_->supportsFastForward();
    fastForward_ = fastForwardSupported_ && runner_->fastForward();  // speed-up on start
    updateUnderrunCounting();
    emit fastForwardChanged();
    if (!audio_.start(static_cast<int>(av.sampleRate + 0.5))) {
      qCWarning(lcAudio) << "Audio output not available; session runs without sound";
    }
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
  connect(r, &EmulationRunner::audioReady, this, [this](const QByteArray& pcm, int rate) {
    if (!audioMuted_) {
      audio_.push(pcm);
    }
    emit audioChunk(pcm, rate);
  });
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
  req.speedUpRatio = config.speedUpRatio;
  req.speedUpOnStart = config.speedUpOnStart;
  req.speedUpAudio = config.speedUpAudio;
  r->start(req);
  applyJoypad();  // a gamepad button that is already held counts from the first frame
}

void GameSession::teardown() {
  if (runner_) {
    runner_->disconnect(this);
    runner_->stop();
    runner_.reset();
  }
  audio_.stop();
  keys_.clear();
  preview_ = false;
  if (fastForward_ || fastForwardSupported_) {
    fastForward_ = false;
    fastForwardSupported_ = false;
    emit fastForwardChanged();
  }
}

void GameSession::pause() {
  if (runner_ && state_ == Running) {
    runner_->pause();
    audio_.setUnderrunCounting(false);
  }
}

void GameSession::resume() {
  if (runner_ && state_ == Paused) {
    runner_->resume();
    audio_.setUnderrunCounting(!audioMuted_ && !(fastForward_ && !ffAudio_));
  }
}

// Nothing is pushed on purpose while paused, muted or fast-forwarding: the sink running dry is not an underrun.
void GameSession::updateUnderrunCounting() {
  audio_.setUnderrunCounting(!audioMuted_ && state_ != Paused && !(fastForward_ && !ffAudio_));
}

QVariantList GameSession::speedUpRatios() {
  QVariantList l;
  for (const double r : emu::EmulationRunner::speedUpRatios()) l.append(r);
  return l;
}

void GameSession::setFastForwardRatio(double ratio) {
  ratio = emu::EmulationRunner::normalizeSpeedUpRatio(ratio);
  if (ratio == ffRatio_) return;
  ffRatio_ = ratio;
  if (runner_) runner_->setFastForwardRatio(ratio);
  emit fastForwardChanged();
}

void GameSession::setFastForward(bool on) {
  on = on && fastForwardSupported_;
  if (on == fastForward_) return;
  fastForward_ = on;
  if (runner_) runner_->setFastForward(on);
  updateUnderrunCounting();
  emit fastForwardChanged();
}

void GameSession::setPreviewFastForwardSupported(bool supported) {
  fastForwardSupported_ = supported;
  if (!supported) fastForward_ = false;
  emit fastForwardChanged();
}

void GameSession::setAudioMuted(bool muted) {
  audioMuted_ = muted;
  updateUnderrunCounting();
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

void GameSession::applyJoypad() {
  if (runner_) {
    runner_->setJoypadState(0, keys_.mask() | pad_);
  }
}

bool GameSession::isReservedKey(int qtKey) {
  return qtKey == Qt::Key_Space || qtKey == Qt::Key_F3 || qtKey == Qt::Key_F5 || qtKey == Qt::Key_F11 || qtKey == Qt::Key_Escape;
}

QStringList GameSession::screenLayouts() const { return screenLayoutsFor(screenCount()); }

namespace {
// Core option that asks for the internal resolution of the OpenGL renderer ("2", "3", "2x native"): the scale, 0 if none.
int requestedScaleOf(const QMap<QString, QString>& options) {
  for (auto it = options.cbegin(); it != options.cend(); ++it) {
    if (it.key().contains(QLatin1String("opengl_resolution"))) {
      int n = 0;
      for (const QChar c : it.value()) {
        if (!c.isDigit()) break;
        n = n * 10 + c.digitValue();
      }
      return n;
    }
  }
  return 0;
}
}  // namespace

EmulationDiagnostics GameSession::diagnostics() const {
  if (preview_) {
    return previewDiag_;
  }
  EmulationDiagnostics d;
  if (!runner_ || !isActive()) {
    return d;
  }
  d.valid = true;
  d.core = coreName_;
  const emu::RenderInfo ri = runner_->renderInfo();
  d.hwRequested = ri.hwRequested;
  d.hwActive = ri.hwActive;
  d.api = ri.api;
  d.gpu = ri.gpu;
  d.fallbackReason = ri.fallbackReason;
  d.frameSize = frame_.size();
  d.baseSize = display_.frameSize();
  d.screens = screenCount();
  d.requestedScale = requestedScaleOf(coreOptions_);
  d.cpuThreads = QThread::idealThreadCount();
  const auto t = runner_->timing();
  d.targetFps = targetFps_;
  if (t.valid) {
    d.fps = t.fps;
    d.frameMs = t.frameMs;
    d.emuMs = t.emuMs;
    d.readbackMs = t.readbackMs;
  }
  d.totalHistory.reserve(t.history.size());
  d.emuHistory.reserve(t.history.size());
  for (const auto& s : t.history) {
    d.totalHistory.append(s.totalMs);
    d.emuHistory.append(s.emuMs);
  }
  d.audioActive = audio_.isActive();
  d.audioBufferMs = audio_.bufferedMs();
  d.underruns = audio_.underruns();
  return d;
}

void GameSession::setPreview(const QString& title, const QImage& frame, const emu::DisplayProfile& profile,
                             const EmulationDiagnostics& diag) {
  preview_ = true;
  previewDiag_ = diag;
  previewDiag_.valid = true;
  title_ = title;
  display_ = profile;
  frame_ = frame;
  setState(Running);
  emit titleChanged();
  emit frameChanged();
}

bool GameSession::keyEvent(int qtKey, bool pressed) {
  if (isReservedKey(qtKey)) {
    return false;
  }
  const bool mapped = pressed ? keys_.press(qtKey) : keys_.release(qtKey);
  if (mapped) {
    applyJoypad();
  }
  return mapped;
}

void GameSession::releaseAllKeys() {
  keys_.clear();
  if (runner_) {
    applyJoypad();  // a held gamepad button stays pressed
    runner_->setPointer(0.0, 0.0, false);
  }
}

void GameSession::setKeyboardMap(const QHash<int, quint32>& map) {
  keys_.setMap(map);
  applyJoypad();
}

void GameSession::setGamepadMask(quint32 mask) {
  if (mask == pad_) return;
  pad_ = mask;
  applyJoypad();
}

void GameSession::setPointer(const QPointF& p, bool pressed) {
  if (runner_) {
    runner_->setPointer(p.x(), p.y(), pressed);
  }
}

}  // namespace framebeam::ui
