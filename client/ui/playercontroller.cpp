#include "playercontroller.h"

#include <QCoreApplication>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStyleHints>
#include <QSysInfo>
#include <QUrl>
#include <algorithm>

#include "core_options.h"
#include "filelog.h"
#include "firmware_materializer.h"
#include "installroot.h"
#include "libretro_backend.h"
#include "mediacaps.h"
#include "version.h"

namespace framebeam::ui {

namespace {

QString trimmedScheme(const QString& address) {
  QString a = address;
  a.remove(QRegularExpression(QStringLiteral("^https?://")));
  return a;
}

QVariantMap checkItem(const QString& label, const QString& state, const QString& meta = QString()) {
  return {{QStringLiteral("label"), label}, {QStringLiteral("state"), state}, {QStringLiteral("meta"), meta}};
}

}  // namespace

PlayerController::PlayerController(const Options& options, QObject* parent)
    : QObject(parent), options_(options), locator_() {
  qRegisterMetaType<framebeam::RomStatus>("framebeam::RomStatus");

  profiles_ = std::make_unique<ProfileStore>(options.dataDir);
  if (options.memoryCredentials) {
    credentials_ = std::make_unique<MemoryCredentialStore>();
  } else {
    credentials_ = createDefaultCredentialStore();
  }
  conn_ = std::make_unique<HubConnection>(profiles_.get(), credentials_.get());
  library_ = std::make_unique<HubLibrary>(conn_.get());
  cache_ = std::make_unique<RomCache>(profiles_->romCacheDir());
  downloader_ = std::make_unique<RomDownloader>(conn_.get(), cache_.get());
  saves_ = std::make_unique<SaveSync>(conn_.get(), profiles_.get());
  settings_ = std::make_unique<PlayerSettings>(profiles_->baseDir());
  fwCache_ = std::make_unique<FirmwareCache>(QDir(systemDir()).filePath(QStringLiteral("firmware")));
  systems_ = std::make_unique<HubSystems>(conn_.get());
  provisioner_ = std::make_unique<FirmwareProvisioner>(conn_.get(), fwCache_.get());
  coreCache_ = std::make_unique<CoreCache>(profiles_->coreCacheDir());
  coreProv_ = std::make_unique<CoreProvisioner>(conn_.get(), coreCache_.get());
  locator_.setCache(coreCache_->root(), CoreCache::currentPlatform());
  uploader_ = std::make_unique<GameUploader>(conn_.get());
  upload_ = {{QStringLiteral("active"), false}, {QStringLiteral("fileName"), QString()}, {QStringLiteral("progress"), 0.0},
             {QStringLiteral("message"), QString()}, {QStringLiteral("isError"), false}};
  connect(systems_.get(), &HubSystems::stateChanged, this, [this]() {
    if (systems_->state() == HubSystems::State::Ready) {
      fwProblems_.clear();  // fresh registry from the Hub: validation problems are re-evaluated
      coreProblems_.clear();
    }
    refreshAttention();
    emit selectedGameChanged();
  });
  connect(provisioner_.get(), &FirmwareProvisioner::finished, this, &PlayerController::onFirmwareFinished);
  connect(coreProv_.get(), &CoreProvisioner::finished, this, &PlayerController::onCoreFinished);
  connect(uploader_.get(), &GameUploader::progress, this, [this](qint64 sent, qint64 total) {
    upload_.insert(QStringLiteral("progress"), total > 0 ? static_cast<double>(sent) / static_cast<double>(total) : 0.0);
    emit uploadChanged();
  });
  connect(uploader_.get(), &GameUploader::finished, this, &PlayerController::onUploadFinished);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  if (QGuiApplication::styleHints() != nullptr) {
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
      if (settings_->appearance() == PlayerSettings::Appearance::System) {
        emit appearanceChanged();
      }
    });
  }
#endif
  connect(saves_.get(), &SaveSync::startReady, this, &PlayerController::onSaveReady);
  connect(saves_.get(), &SaveSync::startConflict, this, &PlayerController::onSaveConflict);
  connect(saves_.get(), &SaveSync::resolveFailed, this, [this](const QString& msg) {
    conflict_.insert(QStringLiteral("busy"), false);
    conflict_.insert(QStringLiteral("error"), msg);
    emit saveConflictChanged();
  });
  connect(saves_.get(), &SaveSync::startFailed, this, [this](const QString&, const QString& msg) {
    phase_ = PlayPhase::None;
    startError_ = msg;
    emit selectedGameChanged();
  });
  connect(saves_.get(), &SaveSync::kindChanged, this, [this](const QString& gameId, SaveSync::Kind k) {
    model_.setSyncKind(gameId, SaveSync::kindName(k));
    if (gameId == selectedId_) {
      emit selectedGameChanged();
    }
  });

  QString err;
  if (!manifests_.loadBuiltin(&err)) {
    qWarning().noquote() << "Manifests not loaded:" << err;
  }
  emulation_ = std::make_unique<EmulationController>(profiles_->baseDir(), &manifests_);
  controllers_ = std::make_unique<ControllersController>(profiles_->baseDir());
  controllers_->start(options.enableGamepads, options.gamepadPollMs);
  {
    // System-specific labels of the Controllers page come from the first manifest with a touch screen.
    const QList<emu::SystemManifest> all = manifests_.all();
    for (const emu::SystemManifest& m : all) {
      const bool touch = std::any_of(m.display.screens.cbegin(), m.display.screens.cend(), [](const emu::ScreenSpec& s) { return s.touch; });
      if (touch || &m == &all.last()) {
        controllers_->setSystemLabels(m.inputLabel, m.touchLabel);
        break;
      }
    }
  }
  if (options.probeCoreVersions) {
    probeCores();
  }
  HandshakeInfo info = HandshakeInfo::detect();
  info.cores = coreList_;
  info.gamepad = controllers_->gamepadAvailable();  // input.gamepad: SDL gamepads usable on this Player
  applyMediaCapabilities(&info);  // h264_encode/h264_decode/encoders as libavcodec can really open them
  handshake_ = info;
  conn_->setHandshakeInfo(info);

  sessions_ = std::make_unique<SessionController>(conn_.get(), profiles_.get(), &session_);
  sessions_->setPlayerSettings(settings_.get());  // diagnostics overlay states live in settings/player.json (0.6 D5)
  saves_->setSettings(settings_.get());
  {
    SaveHistoryController::Env env;
    env.saves = saves_.get();
    env.connection = conn_.get();
    env.gameBusy = [this](const QString& id) {
      return (phase_ != PlayPhase::None || gameActive_ || session_.isActive()) && (launchGame_.id.isEmpty() || launchGame_.id == id);
    };
    env.localFileName = [this](const QString& id) {
      const auto g = model_.game(id);
      return g ? SaveStore::expectedSaveName(g->romSha256 + QLatin1Char('.') + RomCache::extensionFromFilename(g->romFilename)) : QString();
    };
    history_ = std::make_unique<SaveHistoryController>(env);
    connect(this, &PlayerController::selectedGameChanged, history_.get(), [this]() {
      history_->setGame(selectedId_);
      history_->onGameStateChanged();
    });
    connect(sessions_->socket(), &HubSocket::saveUpdated, this, [this](const SaveUpdate& u) { saves_->handleSaveUpdate(u); });
    connect(saves_.get(), &SaveSync::saveChangedElsewhere, history_.get(), &SaveHistoryController::onSaveUpdated);
  }
  {
    UpdatesController::Options uo;
    uo.baseDir = profiles_->baseDir();
    uo.indexUrl = options.updateIndexUrl;
    uo.installRoot = update::installRootFor(QCoreApplication::applicationDirPath());
    updates_ = std::make_unique<UpdatesController>(uo, settings_.get());
    updates_->setProviders(
        [this]() -> std::optional<update::HubProtocol> {
          if (conn_->state() != HubConnection::State::Connected || conn_->hubInfo().protocolVersion <= 0) {
            return std::nullopt;
          }
          return update::HubProtocol{conn_->hubInfo().protocolVersion, conn_->hubInfo().minProtocolVersion};
        },
        [this]() { return sessionBusy(); });
    connect(updates_.get(), &UpdatesController::quitRequested, this, [this]() {
      shutdown();
      QCoreApplication::quit();
    });
  }
  connect(sessions_.get(), &SessionController::watchChanged, this, &PlayerController::updateScreen);
  connect(conn_.get(), &HubConnection::stateChanged, this, &PlayerController::onConnectionState);
  connect(conn_.get(), &HubConnection::errorOccurred, this, [this](const QString& code, const QString& message) {
    lastError_ = friendlyError(code, message);
    inviteBusy_ = false;
    emit hubsChanged();
    emit pairingChanged();
  });
  connect(library_.get(), &HubLibrary::loaded, this, &PlayerController::onLibraryLoaded);
  connect(library_.get(), &HubLibrary::loadFailed, this, [this](const QString&, const QString& message) {
    if (quietRefresh_) {
      quietRefresh_ = false;  // periodic refresh failed: keep showing the last Library
      return;
    }
    libraryState_ = QStringLiteral("error");
    libraryError_ = message;
    emit libraryStateChanged();
  });
  // Live updates (0.6): while the Library is shown it is refreshed quietly (new games, ROM and sync state) and the
  // core / firmware state is re-evaluated, e.g. after the core cache changed outside the Player.
  liveTimer_ = new QTimer(this);
  liveTimer_->setInterval(30000);
  connect(liveTimer_, &QTimer::timeout, this, [this]() {
    if (conn_->state() != HubConnection::State::Connected || screen_ == QLatin1String("game") || phase_ != PlayPhase::None) {
      return;
    }
    refreshCoreState();
    if (screen_ == QLatin1String("library") && libraryState_ == QLatin1String("ready") && !library_->isLoading()) {
      quietRefresh_ = true;
      library_->reload();
    }
  });
  liveTimer_->start();
  connect(library_.get(), &HubLibrary::cleared, this, [this]() {
    model_.clear();
    emit selectedGameChanged();
  });
  connect(downloader_.get(), &RomDownloader::statusChanged, this,
          [this](const QString& sha, const RomStatus& st) { onRomStatus(sha, st); });
  connect(downloader_.get(), &RomDownloader::progress, this, [this](const QString& sha, qint64 got, qint64 total) {
    RomStatus st;
    st.state = RomState::Downloading;
    st.receivedBytes = got;
    st.totalBytes = total;
    onRomStatus(sha, st);
  });
  connect(downloader_.get(), &RomDownloader::romReady, this,
          [this](const QString& sha, const QString& path) { onRomReady(sha, path); });

  // Input: gamepad P1 and the keyboard profile are merged into the joypad mask of the game.
  session_.setKeyboardMap(controllers_->keyboardMap());
  connect(controllers_.get(), &ControllersController::keyboardMapChanged, this,
          [this]() { session_.setKeyboardMap(controllers_->keyboardMap()); });
  connect(controllers_.get(), &ControllersController::libretroMaskChanged, this,
          [this](quint32 mask) { session_.setGamepadMask(mask); });
  session_.setGamepadMask(controllers_->gamepads()->libretroMask());
  connect(emulation_.get(), &EmulationController::frameBeamOptionsChanged, this, &PlayerController::applyFrameBeamOptions);
  applyFrameBeamOptions();
  connect(&session_, &GameSession::stateChanged, this, [this]() { emulation_->setGameRunning(session_.isActive()); });

  connect(&session_, &GameSession::started, this, [this]() {
    applyFrameBeamOptions();
    saves_->beginSession();
    phase_ = PlayPhase::None;
    gameActive_ = true;
    sessions_->gameStarted(launchGame_.id, launchGame_.title);
    if (shareOnStart_) {
      shareOnStart_ = false;
      sessions_->shareSession();
    }
    updateScreen();
    emit selectedGameChanged();
  });
  connect(&session_, &GameSession::stateChanged, this, [this]() {
    if (gameActive_ && session_.state() == GameSession::Paused) {
      saves_->finalSync(false);  // pause = immediate sync of a changed save
    }
  });
  connect(&session_, &GameSession::startFailed, this, [this](const QString& msg) {
    saves_->finalSync(true);
    phase_ = PlayPhase::None;
    shareOnStart_ = false;
    endGameContext();
    startError_ = tr("The emulator could not start: %1").arg(msg);
    updateScreen();
    emit selectedGameChanged();
  });
  connect(&session_, &GameSession::errorChanged, this, [this]() {
    // Runtime error in the game: back to the Library, error shown in the detail pane.
    if (gameActive_ && session_.state() == GameSession::Failed) {
      saves_->finalSync(true);
      endGameContext();
      startError_ = tr("The emulator was terminated: %1").arg(session_.errorText());
      updateScreen();
      emit selectedGameChanged();
    }
  });
}

PlayerController::~PlayerController() { shutdown(); }

void PlayerController::endGameContext() {
  gameActive_ = false;
  if (sessions_) {
    sessions_->gameEnded();  // ends a shared Session
  }
}

void PlayerController::shutdown() {
  if (sessions_) {
    sessions_->gameEnded();
  }
  session_.stop();  // unloads the core first so it flushes its save
  if (saves_) {
    saves_->finalSyncBlocking(true);
  }
}

// ---------------------------------------------------------------- Cores / Handshake

void PlayerController::probeCores() {
  // Pragmatic: the core version is only available in the core info after loadCore(). We load the core once
  // without a game (that also yields its options for the Emulation page), read name/version and unload it again.
  // If that fails, we report only the core_id (empty version) instead of guessing.
  for (const emu::SystemManifest& m : manifests_.all()) {
    const emu::CoreLocation loc = locateCore(m);
    if (!loc.found()) {
      continue;
    }
    const bool known = std::any_of(coreList_.cbegin(), coreList_.cend(), [&](const CoreInfo& c) { return c.id == m.coreId; });
    if (known) {
      continue;
    }
    CoreInfo ci;
    ci.id = m.coreId;
    const emu::CoreProbe probe = emu::probeCore(loc.path, systemDir(), QDir(profiles_->baseDir()).filePath(QStringLiteral("probe")));
    if (probe.ok) {
      ci.version = probe.info.version;
      coreNames_.insert(m.coreId, probe.info.name);
      coreVersions_.insert(m.coreId, probe.info.version);
      emulation_->setCoreProbe(m.coreId, probe, true);
    }
    coreList_.append(ci);
  }
}

emu::CoreLocation PlayerController::locateCore(const emu::SystemManifest& man) const {
  QString version;
  hubOffersCore(man, &version);
  return locator_.locate(man, version);
}

bool PlayerController::hubOffersCore(const emu::SystemManifest& man, QString* version) const {
  if (conn_->state() != HubConnection::State::Connected || !conn_->hubHasFeature(QStringLiteral("cores_v1")) || !systems_->supported() ||
      CoreCache::currentPlatform().isEmpty()) {
    return false;
  }
  const auto sys = systems_->system(man.systemId);
  if (!sys || sys->corePackageVersion.isEmpty() || sys->preferredCoreId != man.coreId) {
    return false;
  }
  if (version != nullptr) {
    *version = sys->corePackageVersion;
  }
  return true;
}

bool PlayerController::coreUsable(const emu::SystemManifest& man, emu::CoreLocation* out) const {
  const emu::CoreLocation loc = locateCore(man);
  if (out != nullptr) {
    *out = loc;
  }
  if (!loc.found()) {
    return false;
  }
  // A cached core must match its package.json (size + SHA-256, memoized); the locator only checks the size.
  return loc.source != QLatin1String("cache") || !coreCache_->libraryPath(man.coreId, loc.version, CoreCache::currentPlatform()).isEmpty();
}

QString PlayerController::coreProblemText(const QString& reason) {
  if (reason == QLatin1String("incompatible")) return tr("Core incompatible");
  if (reason == QLatin1String("untrusted")) return tr("Core untrusted");
  if (reason == QLatin1String("not_on_hub") || reason == QLatin1String("not_cached_on_hub")) return tr("Core missing");
  return tr("Core download failed");
}

void PlayerController::coreStatus(const emu::SystemManifest& man, QString* text, QString* tone, QString* hint) const {
  emu::CoreLocation loc;
  const bool usable = coreUsable(man, &loc);
  hint->clear();
  if (usable) {
    *text = QString();
    *tone = QStringLiteral("ok");
    return;
  }
  if (phase_ == PlayPhase::Core && launchGame_.system == man.systemId) {
    *text = tr("Loading core…");
    *tone = QStringLiteral("neutral");
    return;
  }
  const QString problem = coreProblems_.value(man.coreId);
  if (!problem.isEmpty()) {
    *text = coreProblemText(problem);
    *tone = QStringLiteral("error");
    if (problem == QLatin1String("not_on_hub")) {
      *hint = tr("The Hub has no package of this core. Ask the Hub admin to select a core version on the Systems & Cores page.");
    } else if (problem == QLatin1String("not_cached_on_hub")) {
      *hint = tr("The Hub has not downloaded the core files yet. Ask the Hub admin to check the core source.");
    } else if (problem == QLatin1String("untrusted")) {
      *hint = tr("The core does not match the Hub's signed core index and was not installed. Ask the Hub admin to sync or import the core index again.");
    } else if (problem == QLatin1String("incompatible")) {
      *hint = tr("The Hub has this core only for other platforms than %1.").arg(CoreCache::currentPlatform());
    } else {
      *hint = tr("The core could not be downloaded or verified. Play to try again.");
    }
    return;
  }
  if (hubOffersCore(man)) {
    *text = tr("Core from Hub · download at start");
    *tone = QStringLiteral("neutral");
    return;
  }
  *text = tr("Core missing");
  *tone = QStringLiteral("error");
  *hint = tr("Not found. Connect to a Hub that provides cores, set %1 or place the library in %2.")
              .arg(emu::CoreLocator::environmentVariableFor(man.coreId),
                   loc.tried.isEmpty() ? QString() : QDir::toNativeSeparators(loc.tried.last()));
}

// "Needs attention" (D14) for the Library: core missing/incompatible/untrusted or firmware missing, per system once.
QString PlayerController::attentionFor(const GameEntry& game) const {
  const emu::SystemManifest* man = manifestFor(game);
  if (man == nullptr) {
    return {};
  }
  if (!coreUsable(*man)) {
    QString text, tone, hint;
    coreStatus(*man, &text, &tone, &hint);
    if (tone == QLatin1String("error")) {
      return text;
    }
  }
  SystemInfo sys;
  if (nativeFirmware(*man, &sys)) {
    QList<FirmwareProblem> problems = FirmwareProvisioner::missingOnHub(sys, wantedFirmwareIds(*man));
    for (const FirmwareProblem& p : fwProblems_) {
      if (p.reason == QLatin1String("invalid_hash") || p.reason == QLatin1String("invalid_size")) {
        problems.append(p);
      }
    }
    if (!problems.isEmpty()) {
      return tr("Firmware missing");
    }
  }
  return {};
}

void PlayerController::refreshAttention() {
  QHash<QString, QString> bySystem;
  QHash<QString, QString> byGame;
  for (const GameEntry& g : library_->games()) {
    if (!bySystem.contains(g.system)) {
      bySystem.insert(g.system, attentionFor(g));
    }
    const QString a = bySystem.value(g.system);
    if (!a.isEmpty()) {
      byGame.insert(g.id, a);
    }
  }
  model_.setAttention(byGame);
}

void PlayerController::refreshCoreState() {
  refreshAttention();
  if (!session_.isActive()) {
    refreshEmulationPage();
  }
  emit selectedGameChanged();
}

// Emulation page: system cards (core, readiness, firmware) and the core options (loaded once without a game or from
// the cache of the last capture; not possible while a game runs because only one core can be loaded per process).
void PlayerController::refreshEmulationPage() {
  for (const emu::SystemManifest& m : manifests_.all()) {
    if (emulation_->hasCoreProbe(m.coreId)) {
      continue;
    }
    const emu::CoreLocation loc = locateCore(m);
    if (loc.found() && !session_.isActive()) {
      const emu::CoreProbe probe = emu::probeCore(loc.path, systemDir(), QDir(profiles_->baseDir()).filePath(QStringLiteral("probe")));
      if (probe.ok) {
        coreNames_.insert(m.coreId, probe.info.name);
        coreVersions_.insert(m.coreId, probe.info.version);
        emulation_->setCoreProbe(m.coreId, probe, true);
        continue;
      }
    }
    emulation_->loadCoreCache(m.coreId);
  }
  emulation_->setSystems(systemCards());
  emulation_->setGameRunning(session_.isActive());
}

QVariantList PlayerController::systemCards() {
  QVariantList cards;
  for (const emu::SystemManifest& m : manifests_.all()) {
    const emu::CoreLocation loc = locateCore(m);
    QString version = coreVersions_.value(m.coreId);
    if (version.isEmpty()) {
      if (const emu::CoreProbe* p = emulation_->coreProbe(m.coreId)) version = p->info.version;
    }
    QString readyText;
    QString readyTone;
    if (coreUsable(m)) {
      readyText = loc.source == QLatin1String("cache") ? tr("Ready · core from the Hub") : tr("Ready · included in the Player");
      readyTone = QStringLiteral("ok");
    } else {
      QString hint;
      coreStatus(m, &readyText, &readyTone, &hint);
    }
    // Firmware (firmware path from the Hub registry, architecture 05): built-in BIOS, from the Hub, or required/missing.
    QString fwText = tr("Built-in BIOS");
    QString fwTone = QStringLiteral("neutral");
    SystemInfo sys;
    if (nativeFirmware(m, &sys)) {
      const QStringList wanted = wantedFirmwareIds(m);
      QList<FirmwareProblem> problems = FirmwareProvisioner::missingOnHub(sys, wanted);
      for (const FirmwareProblem& p : fwProblems_) {
        if (p.reason == QLatin1String("invalid_hash") || p.reason == QLatin1String("invalid_size")) problems.append(p);
      }
      if (!problems.isEmpty()) {
        fwText = tr("Firmware required/missing");
        fwTone = QStringLiteral("error");
      } else {
        fwText = tr("Firmware from Hub · verified");
        fwTone = QStringLiteral("ok");
      }
    }
    cards.append(QVariantMap{{QStringLiteral("id"), m.systemId},
                             {QStringLiteral("name"), m.displayName},
                             {QStringLiteral("coreName"), coreLabel(m, loc)},
                             {QStringLiteral("coreVersion"), version},
                             {QStringLiteral("readyText"), readyText},
                             {QStringLiteral("readyTone"), readyTone},
                             {QStringLiteral("firmwareText"), fwText},
                             {QStringLiteral("firmwareTone"), fwTone}});
  }
  return cards;
}

QString PlayerController::coreLabel(const emu::SystemManifest& m, const emu::CoreLocation&) const {
  const QString probed = coreNames_.value(m.coreId);
  if (!probed.isEmpty()) {
    return probed;
  }
  return m.coreDisplayName.isEmpty() ? m.coreId : m.coreDisplayName;
}

QString PlayerController::systemDir() const {
  return QDir(profiles_->baseDir()).filePath(QStringLiteral("system"));
}

// ---------------------------------------------------------------- Navigation

QString PlayerController::logFile() const { return QDir::toNativeSeparators(filelog::path()); }

QString PlayerController::deviceName() const { return profiles_->deviceName(); }
QString PlayerController::playerVersion() const { return HandshakeInfo::detect().playerVersion; }
QString PlayerController::platformText() const {
  const HandshakeInfo h = HandshakeInfo::detect();
  return tr("%1 · Protocol v%2").arg(platformLabel(h.platform, h.arch)).arg(kProtocolVersion);
}

void PlayerController::openLogFolder() {
  const QString p = filelog::path();
  if (!p.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
}

bool PlayerController::sessionBusy() const {
  return gameActive_ || session_.isActive() || (sessions_ && sessions_->watching());
}

void PlayerController::startup() {
  if (options_.enableUpdates && updates_) {
    updates_->manager()->start();
  }
  if (!profiles_->autoConnect() || profiles_->lastHubId().isEmpty()) {
    return;
  }
  if (profiles_->profile(profiles_->lastHubId())) {
    connectProfile(profiles_->lastHubId());
  }
}

QString PlayerController::screen() const { return screen_; }

void PlayerController::updateScreen() {
  using S = HubConnection::State;
  QString next;
  const S s = conn_->state();
  if (gameActive_ || (sessions_ && sessions_->watching())) {
    next = QStringLiteral("game");
  } else if (s == S::Connected) {
    next = page_;
  } else if (s == S::NeedsTrustConfirmation || s == S::NeedsPairing || s == S::AwaitingApproval || s == S::Denied ||
             s == S::Expired) {
    next = QStringLiteral("pairing");
  } else if ((s == S::Identifying || s == S::Authenticating) && pairingFlow_) {
    next = QStringLiteral("pairing");
  } else {
    next = QStringLiteral("connection");
  }
  if (next != screen_) {
    screen_ = next;
    emit screenChanged();
  }
}

void PlayerController::onConnectionState(HubConnection::State s) {
  using S = HubConnection::State;
  lastError_.clear();
  inviteBusy_ = false;
  if (s != S::Connected) {
    page_ = QStringLiteral("library");
    fwProblems_.clear();
  }

  if (s == S::NeedsTrustConfirmation || s == S::NeedsPairing || s == S::AwaitingApproval || s == S::Denied ||
      s == S::Expired) {
    pairingFlow_ = true;
  } else if (s == S::Disconnected || s == S::Connected || s == S::Unreachable || s == S::Incompatible ||
             s == S::CertificateChanged || s == S::UserDisabled) {
    pairingFlow_ = false;
  }
  if (s == S::Connected) {
    if (!resumePage_.isEmpty()) {
      page_ = resumePage_;  // reconnect after "Edit Hub": back where the user was
      resumePage_.clear();
    }
    quietRefresh_ = false;
    libraryState_ = QStringLiteral("loading");
    libraryError_.clear();
    emit libraryStateChanged();
    emit hubChanged();
    library_->reload();
    systems_->reload();  // only with firmware_v1
  } else if (s == S::Disconnected) {
    selectedId_.clear();
    startError_.clear();
    emit selectedGameChanged();
    emit hubChanged();
  }
  updateScreen();
  emit hubsChanged();
  emit pairingChanged();
}

// ---------------------------------------------------------------- 3a Connection

bool PlayerController::autoConnect() const { return profiles_->autoConnect(); }

void PlayerController::setAutoConnect(bool on) {
  if (profiles_->autoConnect() == on) {
    return;
  }
  profiles_->setAutoConnect(on);
  emit autoConnectChanged();
}

QString PlayerController::platformLabel(const QString& platform, const QString& arch) {
  QString p = platform;
  if (p == QLatin1String("windows")) p = QStringLiteral("Windows");
  else if (p == QLatin1String("macos")) p = QStringLiteral("macOS");
  else if (p == QLatin1String("linux")) p = QStringLiteral("Linux");
  QString a = arch;
  if (a == QLatin1String("x86_64")) a = QStringLiteral("x86-64");
  else if (a == QLatin1String("arm64") || a == QLatin1String("aarch64")) a = QStringLiteral("ARM64");
  return a.isEmpty() ? p : p + QLatin1Char(' ') + a;
}

QString PlayerController::deviceFooter() const {
  const HandshakeInfo h = HandshakeInfo::detect();
  return tr("This device: %1 · Player %2 · %3")
      .arg(profiles_->deviceName(), h.playerVersion, platformLabel(h.platform, h.arch));
}

QVariantMap PlayerController::hubCard(const HubProfile& p) const {
  using S = HubConnection::State;
  QVariantMap m;
  m.insert(QStringLiteral("hubId"), p.hubId);
  m.insert(QStringLiteral("name"), p.name.isEmpty() ? trimmedScheme(p.address) : p.name);
  m.insert(QStringLiteral("saved"), true);
  {
    QString host;
    int port = 0;
    ProfileStore::splitAddress(p.address, &host, &port);
    m.insert(QStringLiteral("host"), host);
    m.insert(QStringLiteral("port"), port > 0 ? QString::number(port) : QStringLiteral("8443"));
  }
  m.insert(QStringLiteral("isLast"), p.hubId == profiles_->lastHubId());
  const QString last = p.lastConnected.isValid() ? tr("last %1").arg(p.lastConnected.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                                                 : tr("never connected");
  m.insert(QStringLiteral("detail"), trimmedScheme(p.address) + QStringLiteral(" · ") + last);

  const bool current = conn_->state() != S::Disconnected &&
                       (conn_->address() == p.address || (conn_->profile() && conn_->profile()->hubId == p.hubId));
  QString status = QStringLiteral("idle");
  QString text = tr("Ready");
  QString tone = QStringLiteral("neutral");
  QString message;
  if (current) {
    switch (conn_->state()) {
      case S::Identifying:
      case S::Authenticating:
        status = QStringLiteral("connecting");
        text = tr("Connecting…");
        break;
      case S::CertificateChanged:
        status = QStringLiteral("certChanged");
        text = tr("Certificate changed");
        tone = QStringLiteral("error");
        message = tr("The Hub's certificate changed. The connection is blocked and nothing was sent to the Hub. "
                     "Compare the new fingerprint with the one on the Hub's web Settings page. Trust it only if they match.");
        m.insert(QStringLiteral("expectedFingerprint"), formatFingerprint(conn_->expectedFingerprint().isEmpty() ? p.pinnedFingerprint : conn_->expectedFingerprint()));
        m.insert(QStringLiteral("observedFingerprint"), formatFingerprint(conn_->observedFingerprint()));
        m.insert(QStringLiteral("observedFingerprintRaw"), conn_->observedFingerprint());
        break;
      case S::Incompatible: {
        status = QStringLiteral("incompatible");
        const bool hubOld = conn_->incompatibleReason() == HubConnection::IncompatibleReason::HubTooOld;
        text = hubOld ? tr("Hub too old") : tr("Player too old");
        tone = QStringLiteral("warn");
        const HubInfo& hi = conn_->hubInfo();
        message = hubOld ? tr("Hub speaks protocol v%1, Player requires at least v%2").arg(hi.protocolVersion).arg(kMinProtocolVersion)
                         : tr("Hub requires at least protocol v%1, Player speaks v%2").arg(hi.minProtocolVersion).arg(kProtocolVersion);
        break;
      }
      case S::UserDisabled:
        status = QStringLiteral("userDisabled");
        text = tr("User disabled");
        tone = QStringLiteral("error");
        message = tr("This user is disabled on the Hub. Ask the Hub admin to enable it again. "
                     "The Player does not retry on its own; use Retry once it is enabled.");
        break;
      case S::Unreachable:
        status = QStringLiteral("unreachable");
        text = tr("Not reachable");
        tone = QStringLiteral("error");
        message = conn_->errorMessage();
        break;
      default:
        break;
    }
  }
  m.insert(QStringLiteral("status"), status);
  m.insert(QStringLiteral("statusText"), text);
  m.insert(QStringLiteral("tone"), tone);
  m.insert(QStringLiteral("message"), message);
  m.insert(QStringLiteral("current"), current);
  m.insert(QStringLiteral("connected"), current && conn_->state() == S::Connected);
  return m;
}

QVariantList PlayerController::hubs() const {
  using S = HubConnection::State;
  QVariantList list;
  bool matched = false;
  for (const HubProfile& p : profiles_->profiles()) {
    QVariantMap card = hubCard(p);
    matched = matched || card.value(QStringLiteral("current")).toBool();
    list.append(card);
  }
  const S s = conn_->state();
  if (!matched && (s == S::Unreachable || s == S::Incompatible || s == S::CertificateChanged || s == S::UserDisabled)) {
    // Attempt with an address not saved yet: show as a card with the result.
    HubProfile p;
    p.address = conn_->address();
    p.name = conn_->hubInfo().name;
    QVariantMap card = hubCard(p);
    card.insert(QStringLiteral("saved"), false);
    card.insert(QStringLiteral("hubId"), QString());
    card.insert(QStringLiteral("detail"), trimmedScheme(p.address));
    list.append(card);
  }
  return list;
}

void PlayerController::addHub(const QString& address) {
  notice_.clear();
  resumePage_.clear();
  if (address.trimmed().isEmpty()) {
    notice_ = tr("Please enter a hub address, e.g. hub.local:8443.");
    emit hubsChanged();
    return;
  }
  conn_->connectToAddress(address, options_.allowHttp);
  // Set after the call: disconnecting the old connection (e.g. after Unreachable) would otherwise reset the flag.
  lastAttemptPairing_ = true;
  pairingFlow_ = conn_->state() == HubConnection::State::Identifying;
  updateScreen();
  emit hubsChanged();
  emit pairingChanged();
}

void PlayerController::connectProfile(const QString& hubId) {
  notice_.clear();
  lastAttemptPairing_ = false;
  pairingFlow_ = false;
  conn_->connectToProfile(hubId);
  updateScreen();
  emit hubsChanged();
}

void PlayerController::retryConnection() {
  conn_->retry();
  using S = HubConnection::State;
  // Paired profile (credential present): never show the pairing status screen.
  const std::optional<HubProfile> prof = conn_->profile();
  const bool paired = prof.has_value() && !prof->credentialRef.isEmpty();
  if (lastAttemptPairing_ && !paired && conn_->state() == S::Identifying) {
    pairingFlow_ = true;  // like addHub: show the status screen again (retry disconnects internally and resets the flag)
    updateScreen();
    emit hubsChanged();
    emit pairingChanged();
  }
}

void PlayerController::removeHub(const QString& hubId) {
  resumePage_.clear();
  if (hubId.isEmpty()) {
    conn_->disconnectFromHub();
  } else {
    if (conn_->profile() && conn_->profile()->hubId == hubId) {
      endRunningWork();  // the current Hub: end Sessions and secure pending saves before the connection goes
    }
    conn_->removeProfile(hubId);
  }
  notice_.clear();
  updateScreen();
  emit hubsChanged();
}

QStringList PlayerController::validateHubAddress(const QString& host, const QString& port) const {
  return ProfileStore::validateHubAddress(host, port);
}

bool PlayerController::editHub(const QString& hubId, const QString& host, const QString& port) {
  if (!ProfileStore::validateHubAddress(host, port).isEmpty() || !profiles_->profile(hubId)) {
    return false;
  }
  const bool active = conn_->state() != HubConnection::State::Disconnected && conn_->profile() && conn_->profile()->hubId == hubId;
  if (active) {
    endRunningWork();  // Sessions end and pending saves are secured before the connection goes
  }
  if (!profiles_->updateHubAddress(hubId, host, port.trimmed().toInt())) {
    return false;
  }
  if (active) {
    resumePage_ = page_;
    conn_->disconnectFromHub();
    connectProfile(hubId);  // pinned fingerprint of the profile still applies: another certificate is never accepted silently
  } else {
    emit hubsChanged();
  }
  return true;
}

// ---------------------------------------------------------------- 3b Pairing

QString PlayerController::formatFingerprint(const QString& fp) {
  QString hex;
  for (const QChar c : fp) {
    if (c.isDigit() || (c.toLower() >= QLatin1Char('a') && c.toLower() <= QLatin1Char('f'))) {
      hex.append(c.toUpper());
    }
  }
  QStringList pairs;
  for (qsizetype i = 0; i + 1 < hex.size(); i += 2) {
    pairs.append(hex.mid(i, 2));
  }
  if (pairs.isEmpty()) {
    return fp;
  }
  const qsizetype half = (pairs.size() + 1) / 2;
  const QString first = pairs.mid(0, half).join(QLatin1Char(':'));
  const QString second = pairs.mid(half).join(QLatin1Char(':'));
  return second.isEmpty() ? first : first + QLatin1Char('\n') + second;
}

QVariantMap PlayerController::pairing() const {
  using S = HubConnection::State;
  const S s = conn_->state();
  const HubInfo& hi = conn_->hubInfo();
  const HandshakeInfo h = HandshakeInfo::detect();
  QVariantMap m;
  QString phase = QStringLiteral("identifying");
  switch (s) {
    case S::NeedsTrustConfirmation: phase = QStringLiteral("trust"); break;
    case S::NeedsPairing: phase = QStringLiteral("needsPairing"); break;
    case S::AwaitingApproval: phase = QStringLiteral("awaiting"); break;
    case S::Denied: phase = QStringLiteral("denied"); break;
    case S::Expired: phase = QStringLiteral("expired"); break;
    case S::Authenticating: phase = QStringLiteral("authenticating"); break;
    default: break;
  }
  const bool tls = conn_->address().startsWith(QLatin1String("https://"));
  QString fp = conn_->observedFingerprint();
  if (fp.isEmpty() && conn_->profile()) {
    fp = conn_->profile()->pinnedFingerprint;
  }
  m.insert(QStringLiteral("phase"), phase);
  m.insert(QStringLiteral("address"), trimmedScheme(conn_->address()));
  m.insert(QStringLiteral("hubName"), hi.name);
  m.insert(QStringLiteral("hubVersion"), hi.hubVersion);
  m.insert(QStringLiteral("protocol"), hi.protocolVersion);
  m.insert(QStringLiteral("hubKnown"), !hi.hubId.isEmpty());
  m.insert(QStringLiteral("tls"), tls);
  m.insert(QStringLiteral("fingerprint"), formatFingerprint(fp));
  m.insert(QStringLiteral("deviceName"), profiles_->deviceName());
  m.insert(QStringLiteral("platform"), platformLabel(h.platform, h.arch));
  m.insert(QStringLiteral("playerVersion"), h.playerVersion);
  m.insert(QStringLiteral("error"), lastError_);
  m.insert(QStringLiteral("inviteBusy"), inviteBusy_);
  return m;
}

void PlayerController::redeemInvite(const QString& code, const QString& displayName) {
  const QString name = displayName.trimmed();
  QString problem;
  if (code.trimmed().isEmpty()) {
    problem = tr("Please enter the invite code.");
  } else if (name.isEmpty()) {
    problem = tr("Please enter a display name.");
  } else if (name.size() > 32) {
    problem = tr("The display name can have at most 32 characters.");
  }
  if (!problem.isEmpty()) {
    lastError_ = problem;
    emit pairingChanged();
    return;
  }
  lastError_.clear();
  inviteBusy_ = true;
  emit pairingChanged();
  conn_->redeemInvite(code, name);
}

QString PlayerController::friendlyError(const QString& code, const QString& message) const {
  if (code == QLatin1String("invite_invalid")) {
    return tr("This invite code is not valid. It may have expired, been used already or been revoked.");
  }
  if (code == QLatin1String("display_name_taken")) {
    return tr("This display name is already taken on the Hub. Please choose another one.");
  }
  if (code == QLatin1String("rate_limited")) {
    return tr("Too many attempts. Please wait a moment and try again.");
  }
  if (code == QLatin1String("not_found") && inviteBusy_) {
    return tr("This Hub does not support invite codes.");
  }
  if (code == QLatin1String("invalid_input")) {
    return tr("Please enter the invite code and a display name of 1 to 32 characters.");
  }
  return message;
}

void PlayerController::confirmTrust() { conn_->confirmTrust(); }
void PlayerController::rejectTrust() { conn_->rejectTrust(); }
void PlayerController::trustChangedCertificate(const QString& observedFingerprint) {
  conn_->confirmCertificateChange(observedFingerprint);
}
void PlayerController::cancelCertificateChange() {
  if (conn_->state() == HubConnection::State::CertificateChanged) {
    conn_->disconnectFromHub();
  }
}
void PlayerController::requestPairing() { conn_->requestPairing(); }
void PlayerController::cancelPairing() { conn_->cancelPairing(); }
void PlayerController::leavePairing() { conn_->disconnectFromHub(); }

// ---------------------------------------------------------------- Settings, upload, warnings

QString PlayerController::appearance() const { return PlayerSettings::appearanceName(settings_->appearance()); }

void PlayerController::setAppearance(const QString& name) {
  const PlayerSettings::Appearance a = PlayerSettings::parseAppearance(name, settings_->appearance());
  if (a == settings_->appearance()) {
    return;
  }
  if (!settings_->setAppearance(a)) {
    qWarning() << "Player settings could not be written";
  }
  emit appearanceChanged();
}

bool PlayerController::darkMode() const {
  switch (settings_->appearance()) {
    case PlayerSettings::Appearance::Dark: return true;
    case PlayerSettings::Appearance::Light: return false;
    case PlayerSettings::Appearance::System: break;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    return hints->colorScheme() != Qt::ColorScheme::Light;  // Unknown -> dark
  }
#endif
  return true;  // Qt 6.4: no color scheme API
}

void PlayerController::showLibrary() {
  page_ = QStringLiteral("library");
  updateScreen();
}

void PlayerController::showSettings() {
  if (conn_->state() != HubConnection::State::Connected || gameActive_) {
    return;
  }
  page_ = QStringLiteral("settings");
  updateScreen();
}

void PlayerController::showEmulation() {
  if (conn_->state() != HubConnection::State::Connected || gameActive_) {
    return;
  }
  refreshEmulationPage();
  page_ = QStringLiteral("emulation");
  updateScreen();
}

void PlayerController::showControllers() {
  if (conn_->state() != HubConnection::State::Connected || gameActive_) {
    return;
  }
  page_ = QStringLiteral("controllers");
  updateScreen();
}

// System of the game being launched, else of the selected game, else of the first known system.
QString PlayerController::currentSystemId() const {
  if (!launchGame_.system.isEmpty()) {
    return launchGame_.system;
  }
  if (const auto g = model_.game(selectedId_); g && !g->system.isEmpty()) {
    return g->system;
  }
  const QList<emu::SystemManifest> all = manifests_.all();
  return all.isEmpty() ? QString() : all.first().systemId;
}

bool PlayerController::fullscreenOnStart() const {
  const QString sys = currentSystemId();
  return emulation_->frameBeamValue(QString::fromLatin1(EmulationController::kFullscreenKey), sys, launchGame_.id) ==
         QLatin1String("on");
}

void PlayerController::applyFrameBeamOptions() {
  // Default multiview: applied immediately (not while the user changes it in a running Session view).
  if (sessions_ && !gameActive_) {
    const QString sys = currentSystemId();
    sessions_->setMultiviewMode(emulation_->frameBeamValue(QString::fromLatin1(EmulationController::kMultiviewKey), sys));
  }
  emit emulationSettingsChanged();
}

bool PlayerController::canUpload() const {
  return conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QStringLiteral("uploads_v1"));
}

QStringList PlayerController::uploadFilters() const {
  QStringList exts;
  for (const emu::SystemManifest& m : manifests_.all()) {
    for (const QString& e : m.extensions) {
      exts.append(QStringLiteral("*") + e);
    }
  }
  QStringList filters;
  if (!exts.isEmpty()) {
    filters.append(tr("Game ROMs (%1)").arg(exts.join(QLatin1Char(' '))));
  }
  filters.append(tr("All files (*)"));
  return filters;
}

QVariantList PlayerController::coreWarnings() const {
  QVariantList list;
  for (const HandshakeProblem& p : conn_->handshakeProblems()) {
    const bool missing = p.code == QLatin1String("core_missing");
    if (!missing && p.code != QLatin1String("core_version_mismatch")) {
      continue;
    }
    QString label = p.coreId;
    for (const emu::SystemManifest& m : manifests_.all()) {
      if (m.coreId == p.coreId) {
        label = coreLabel(m, emu::CoreLocation{});
      }
    }
    if (label.isEmpty()) {
      label = tr("Core");
    }
    const QString text = missing ? tr("%1 was not found on this device. Games for its system cannot start here.").arg(label)
                                 : tr("%1 has a different version than this Hub expects. Games can still start, "
                                      "but may behave differently.").arg(label);
    list.append(QVariantMap{{QStringLiteral("code"), p.code},
                            {QStringLiteral("coreId"), p.coreId},
                            {QStringLiteral("text"), text},
                            {QStringLiteral("detail"), p.detail}});
  }
  return list;
}

void PlayerController::uploadRom(const QString& source) {
  if (!canUpload() || uploader_->busy()) {
    return;
  }
  const QString path = source.startsWith(QLatin1String("file:")) ? QUrl(source).toLocalFile() : source;
  upload_ = {{QStringLiteral("active"), true},
             {QStringLiteral("fileName"), QFileInfo(path).fileName()},
             {QStringLiteral("progress"), 0.0},
             {QStringLiteral("message"), QString()},
             {QStringLiteral("isError"), false}};
  emit uploadChanged();
  uploader_->upload(path);
}

void PlayerController::dismissUploadMessage() {
  if (upload_.value(QStringLiteral("active")).toBool() || upload_.value(QStringLiteral("message")).toString().isEmpty()) {
    return;
  }
  upload_.insert(QStringLiteral("message"), QString());
  emit uploadChanged();
}

void PlayerController::onUploadFinished(const UploadResult& r) {
  using K = UploadResult::Kind;
  QString message;
  bool isError = true;
  switch (r.kind) {
    case K::Created:
      message = tr("Uploaded “%1” to the Hub.").arg(r.game.title);
      isError = false;
      pendingSelectId_ = r.game.id;
      reloadLibrary();
      break;
    case K::Duplicate:
      message = tr("Already in the library.");
      isError = false;
      if (!r.existingGameId.isEmpty()) {
        if (model_.rowOfGame(r.existingGameId) >= 0) {
          selectGame(r.existingGameId);
        } else {
          pendingSelectId_ = r.existingGameId;
          reloadLibrary();
        }
      }
      break;
    case K::Forbidden:
      message = tr("Uploads are not allowed for your user on this Hub.");
      break;
    case K::TooLarge:
      message = tr("The file is too large for this Hub.");
      break;
    case K::Rejected:
      message = r.errorMessage.isEmpty() ? tr("The Hub rejected this file (unsupported type?).")
                                         : tr("The Hub rejected this file: %1").arg(r.errorMessage);
      break;
    case K::Failed:
      if (r.errorCode == QLatin1String("file_unreadable")) {
        message = tr("The file cannot be read.");
      } else if (r.errorCode == QLatin1String("cancelled")) {
        message = tr("Upload cancelled.");
      } else if (r.errorCode == QLatin1String("not_connected")) {
        message = tr("Not connected to a Hub.");
      } else {
        message = tr("Upload failed: %1").arg(r.errorMessage.isEmpty() ? r.errorCode : r.errorMessage);
      }
      break;
  }
  upload_ = {{QStringLiteral("active"), false},
             {QStringLiteral("fileName"), r.fileName},
             {QStringLiteral("progress"), r.kind == K::Created ? 1.0 : 0.0},
             {QStringLiteral("message"), message},
             {QStringLiteral("isError"), isError}};
  emit uploadChanged();
}

void PlayerController::recheckFirmware() {
  fwProblems_.clear();
  startError_.clear();
  systems_->reload();
  emit selectedGameChanged();
}

// ---------------------------------------------------------------- 3c Library

QString PlayerController::hubName() const { return conn_->hubInfo().name; }
QString PlayerController::hubAddress() const { return trimmedScheme(conn_->address()); }

// Ends the running game/Session and secures pending save uploads; needs the connection, so call before disconnecting.
void PlayerController::endRunningWork() {
  if (gameActive_) {
    session_.stop();
    saves_->finalSyncBlocking(true);
    gameActive_ = false;
  }
}

void PlayerController::switchHub() {
  resumePage_.clear();
  endRunningWork();
  conn_->disconnectFromHub();
  updateScreen();
}

// Settings -> Hubs: same path as the connection screen (disconnect, then connect to the stored profile).
void PlayerController::switchToHub(const QString& hubId) {
  if (hubId.isEmpty() || (conn_->state() == HubConnection::State::Connected && conn_->profile() && conn_->profile()->hubId == hubId)) {
    return;
  }
  switchHub();
  connectProfile(hubId);
}

void PlayerController::reloadLibrary() {
  libraryState_ = QStringLiteral("loading");
  emit libraryStateChanged();
  library_->reload();
}

void PlayerController::onLibraryLoaded() {
  quietRefresh_ = false;
  model_.setGames(library_->games(), [this](const GameEntry& g) { return downloader_->status(g); });
  refreshAttention();
  QStringList ids;
  for (const GameEntry& g : library_->games()) {
    ids.append(g.id);
  }
  saves_->refreshKinds(ids);
  for (const QString& id : ids) {
    model_.setSyncKind(id, SaveSync::kindName(saves_->kind(id)));
  }
  libraryState_ = QStringLiteral("ready");
  libraryError_.clear();
  emit libraryStateChanged();
  if (!pendingSelectId_.isEmpty()) {
    if (model_.rowOfGame(pendingSelectId_) >= 0) {
      selectedId_ = pendingSelectId_;
      startError_.clear();
    }
    pendingSelectId_.clear();
  }
  if (selectedId_.isEmpty() || model_.rowOfGame(selectedId_) < 0) {
    selectedId_ = model_.rowCount() > 0 ? model_.data(model_.index(0), LibraryModel::GameIdRole).toString() : QString();
  }
  emit selectedGameChanged();
}

void PlayerController::selectGame(const QString& gameId) {
  if (selectedId_ == gameId) {
    return;
  }
  selectedId_ = gameId;
  startError_.clear();
  emit selectedGameChanged();
}

void PlayerController::onRomStatus(const QString& sha, const RomStatus& st) {
  model_.setStatus(sha, st);
  if (!pendingSha_.isEmpty() && pendingSha_ == sha && (st.state == RomState::HashMismatch || st.state == RomState::Failed)) {
    pendingSha_.clear();
    phase_ = PlayPhase::None;
    startError_ = st.state == RomState::HashMismatch
                      ? tr("The download does not match the expected SHA-256 and was discarded. Please download again.")
                      : tr("Download failed: %1").arg(st.errorMessage.isEmpty() ? st.errorCode : st.errorMessage);
  }
  const auto g = model_.game(selectedId_);
  if (g && g->romSha256 == sha) {
    emit selectedGameChanged();
  }
}

const emu::SystemManifest* PlayerController::manifestFor(const GameEntry& game) const {
  const QString ext = QStringLiteral(".") + RomCache::extensionFromFilename(game.romFilename);
  if (const auto* m = manifests_.forExtension(ext)) {
    return m;
  }
  return manifests_.find(game.system);
}

QStringList PlayerController::wantedFirmwareIds(const emu::SystemManifest& man) const {
  QStringList ids;
  for (const emu::FirmwareFile& f : man.firmware.files) {
    ids.append(f.id);
  }
  return ids;
}

bool PlayerController::nativeFirmware(const emu::SystemManifest& man, SystemInfo* system) const {
  // Without firmware_v1 (older hub) or without registry data the core runs on its built-in firmware.
  if (!systems_->supported() || man.firmware.sysfileOption.isEmpty()) {
    return false;
  }
  const auto info = systems_->system(man.systemId);
  if (!info || !info->nativeFirmware()) {
    return false;
  }
  if (system != nullptr) {
    *system = *info;
  }
  return true;
}

QVariantMap PlayerController::selectedGame() const {
  QVariantMap m;
  const auto game = model_.game(selectedId_);
  if (!game) {
    return m;
  }
  const RomStatus st = model_.status(selectedId_).value_or(RomStatus{});
  const QString kind = LibraryModel::stateKind(st.state);
  const emu::SystemManifest* man = manifestFor(*game);
  const QString ext = RomCache::extensionFromFilename(game->romFilename);

  m.insert(QStringLiteral("id"), game->id);
  m.insert(QStringLiteral("saveSlot"), saves_->slotFor(game->id));
  m.insert(QStringLiteral("title"), game->title);
  m.insert(QStringLiteral("monogram"), LibraryModel::monogram(game->title));
  m.insert(QStringLiteral("systemName"), man ? man->displayName : game->system.toUpper());
  m.insert(QStringLiteral("stateKind"), kind);
  m.insert(QStringLiteral("sha"), game->romSha256);
  m.insert(QStringLiteral("sizeText"), tr("%1 (%2 bytes)").arg(LibraryModel::formatSize(game->romSize)).arg(game->romSize));
  m.insert(QStringLiteral("cachePath"), cache_->finalPath(game->romSha256, ext));
  const qint64 total = st.totalBytes > 0 ? st.totalBytes : game->romSize;
  m.insert(QStringLiteral("progress"), total > 0 ? static_cast<double>(st.receivedBytes) / static_cast<double>(total) : 0.0);

  // ROM row
  QString romText;
  QString romTone = QStringLiteral("neutral");
  if (kind == QLatin1String("ready")) {
    romText = tr("Cached locally · verified");
    romTone = QStringLiteral("ok");
  } else if (kind == QLatin1String("download")) {
    romText = tr("Download needed · %1").arg(LibraryModel::formatSize(game->romSize));
  } else if (kind == QLatin1String("downloading")) {
    romText = tr("Downloading %1 %").arg(static_cast<int>(m.value(QStringLiteral("progress")).toDouble() * 100));
  } else if (kind == QLatin1String("mismatch")) {
    romText = tr("Hash mismatch · reload");
    romTone = QStringLiteral("error");
  } else if (kind == QLatin1String("failed")) {
    romText = tr("Download failed");
    romTone = QStringLiteral("error");
  } else {
    romText = tr("Verifying…");
  }
  m.insert(QStringLiteral("romText"), romText);
  m.insert(QStringLiteral("romTone"), romTone);

  // Core / firmware
  bool coreOk = false;
  bool coreProvisionable = false;  // missing locally, but the Hub offers it: provisioned at game start
  QString coreTone = QStringLiteral("ok");
  bool fwOk = true;
  bool fwChecking = false;
  bool fwBlocked = false;
  QString fwTone = QStringLiteral("neutral");
  QString coreText;
  QString coreHint;
  QString fwText = tr("Not required");
  QString fwHint;
  if (man == nullptr) {
    coreText = tr("no system manifest for .%1").arg(ext);
  } else {
    const emu::CoreLocation loc = locateCore(*man);
    const QString label = coreLabel(*man, loc);
    coreOk = coreUsable(*man);
    if (coreOk) {
      coreText = tr("%1 · ready").arg(label);
    } else {
      QString st;
      coreStatus(*man, &st, &coreTone, &coreHint);
      coreText = tr("%1 · %2").arg(label, st);
      coreProvisionable = hubOffersCore(*man) && phase_ == PlayPhase::None;
    }
    SystemInfo sys;
    if (systems_->supported() && systems_->state() == HubSystems::State::Loading && !systems_->system(man->systemId)) {
      fwText = tr("Checking…");
      fwChecking = true;
    } else if (systems_->supported() && systems_->state() == HubSystems::State::Failed) {
      fwText = tr("Status unavailable");
      fwTone = QStringLiteral("warn");
      fwHint = tr("The firmware status could not be read from the Hub; the built-in firmware is used.");
    } else if (nativeFirmware(*man, &sys)) {
      const QStringList wanted = wantedFirmwareIds(*man);
      QList<FirmwareProblem> problems = FirmwareProvisioner::missingOnHub(sys, wanted);
      for (const FirmwareProblem& p : fwProblems_) {
        if (p.reason == QLatin1String("invalid_hash") || p.reason == QLatin1String("invalid_size")) {
          problems.append(p);
        }
      }
      if (!problems.isEmpty()) {
        fwOk = false;
        fwBlocked = true;
        fwText = tr("Firmware required/missing");
        QStringList parts;
        for (const FirmwareProblem& p : problems) {
          const QString name = p.displayName.isEmpty() ? p.fileId : p.displayName;
          const QString why = p.reason == QLatin1String("missing_on_hub") ? tr("missing on the Hub")
                              : p.reason == QLatin1String("invalid_hash") ? tr("does not match the Hub's SHA-256")
                                                                          : tr("has the wrong size");
          parts.append(QStringLiteral("%1 (%2)").arg(name, why));
        }
        fwHint = tr("%1. The Hub admin can provide the files on the Systems page; then check again.").arg(parts.join(QStringLiteral(", ")));
      } else {
        bool allCached = true;
        for (const FirmwareFileInfo& f : sys.firmware) {
          if (f.present && wanted.contains(f.id) && !fwCache_->probe(sys.id, f.sha256, f.size)) {
            allCached = false;
          }
        }
        fwText = allCached ? tr("From Hub · cached") : tr("From Hub · download at start");
        fwTone = allCached ? QStringLiteral("ok") : QStringLiteral("neutral");
      }
    } else if (systems_->supported()) {
      fwText = tr("Not required");
    }
  }
  m.insert(QStringLiteral("coreText"), coreText);
  m.insert(QStringLiteral("coreTone"), coreOk ? QStringLiteral("ok") : coreTone);
  m.insert(QStringLiteral("coreHint"), coreHint);
  m.insert(QStringLiteral("firmwareText"), fwText);
  m.insert(QStringLiteral("firmwareTone"), fwOk ? fwTone : QStringLiteral("error"));
  m.insert(QStringLiteral("firmwareHint"), fwHint);
  m.insert(QStringLiteral("firmwareBlocked"), fwBlocked);

  // Save row (sync state)
  {
    const QString sk = model_.syncKind(selectedId_);
    QString saveText;
    QString saveTone = QStringLiteral("neutral");
    QString saveHint;
    if (conn_->state() == HubConnection::State::Connected && !saves_->hubSupportsSaves()) {
      saveText = SaveSync::unsupportedNote();
    } else if (sk == QLatin1String("synced")) {
      saveText = tr("Synced");
      saveTone = QStringLiteral("ok");
    } else if (sk == QLatin1String("pending")) {
      saveText = tr("Sync pending");
      saveTone = QStringLiteral("warn");
      saveHint = tr("Local changes are uploaded to this Hub as soon as it is reachable.");
    } else if (sk == QLatin1String("conflict")) {
      saveText = tr("Conflict");
      saveTone = QStringLiteral("warn");
      saveHint = tr("Decide when starting the game or on the Saves page in the Hub.");
    } else {
      saveText = tr("No save yet");
    }
    m.insert(QStringLiteral("saveText"), saveText);
    m.insert(QStringLiteral("saveTone"), saveTone);
    m.insert(QStringLiteral("saveHint"), saveHint);
    m.insert(QStringLiteral("syncKind"), sk);
  }

  // Start checklist
  const bool busy = phase_ != PlayPhase::None;
  const bool romReady = kind == QLatin1String("ready");
  QVariantList list;
  list.append(checkItem(tr("Game data from hub"), QStringLiteral("done")));
  if (man != nullptr && nativeFirmware(*man)) {
    const bool fwDone = phase_ == PlayPhase::Rom || phase_ == PlayPhase::Launching;
    list.append(checkItem(tr("Firmware from Hub verified"),
                          fwDone ? QStringLiteral("done") : (phase_ == PlayPhase::Firmware ? QStringLiteral("active")
                                                             : (fwOk ? QStringLiteral("pending") : QStringLiteral("error"))),
                          QString()));
  }
  list.append(checkItem(romReady ? tr("ROM verified from cache") : tr("Download and verify ROM"),
                        romReady ? QStringLiteral("done") : (phase_ == PlayPhase::Rom ? QStringLiteral("active") : QStringLiteral("pending")),
                        romReady ? QString() : romText));
  list.append(checkItem(tr("Save checked with Hub"),
                        phase_ == PlayPhase::Launching ? (saveReady_ ? QStringLiteral("done") : QStringLiteral("active")) : QStringLiteral("pending"),
                        saveNoteStart_));
  list.append(checkItem(tr("Core ready"),
                        coreOk ? QStringLiteral("done")
                               : (phase_ == PlayPhase::Core ? QStringLiteral("active")
                                                            : (coreProvisionable ? QStringLiteral("pending") : QStringLiteral("error"))),
                        coreOk ? QString() : coreText));
  list.append(checkItem(tr("Emulator starting"), (phase_ == PlayPhase::Launching && saveReady_) ? QStringLiteral("active") : QStringLiteral("pending")));
  m.insert(QStringLiteral("checklist"), list);

  QString error = startError_;
  m.insert(QStringLiteral("error"), error);
  m.insert(QStringLiteral("busy"), busy);
  m.insert(QStringLiteral("canPlay"), !busy && man != nullptr && (coreOk || coreProvisionable) && fwOk && kind != QLatin1String("validating") &&
                                          kind != QLatin1String("downloading") && kind != QLatin1String("unknown") && !fwChecking);
  QString label = tr("Play");
  if (busy) {
    label = phase_ == PlayPhase::Launching ? tr("Starting…")
            : phase_ == PlayPhase::Core    ? tr("Loading core…")
            : phase_ == PlayPhase::Firmware ? tr("Checking firmware…")
                                            : tr("Downloading…");
  } else if (kind == QLatin1String("download")) {
    label = tr("Download and play");
  } else if (kind == QLatin1String("mismatch") || kind == QLatin1String("failed")) {
    label = tr("Reload and play");
  }
  m.insert(QStringLiteral("playLabel"), label);

  // Status pill of the detail pane (3c): same priority as the tile status line.
  {
    QString pill = tr("Ready to play");
    QString tone = QStringLiteral("ok");
    const QString attention = model_.attentionText(game->id);
    if (kind == QLatin1String("mismatch")) {
      pill = tr("Hash mismatch");
      tone = QStringLiteral("error");
    } else if (!attention.isEmpty()) {
      pill = attention;
      tone = QStringLiteral("warn");
    } else if (kind == QLatin1String("failed")) {
      pill = tr("Download failed");
      tone = QStringLiteral("error");
    } else if (kind == QLatin1String("download")) {
      pill = tr("Download required");
      tone = QStringLiteral("neutral");
    } else if (kind == QLatin1String("downloading")) {
      pill = tr("Downloading");
      tone = QStringLiteral("neutral");
    } else if (kind != QLatin1String("ready")) {
      pill = tr("Verifying");
      tone = QStringLiteral("neutral");
    } else if (!coreOk && !coreProvisionable) {
      pill = tr("Core missing");
      tone = QStringLiteral("warn");
    }
    m.insert(QStringLiteral("pillText"), pill);
    m.insert(QStringLiteral("pillTone"), tone);
    m.insert(QStringLiteral("coreLabelText"), man != nullptr ? coreLabel(*man, locateCore(*man)) : QString());
    m.insert(QStringLiteral("coreVersionText"), man != nullptr ? coreVersions_.value(man->coreId) : QString());
  }
  return m;
}

void PlayerController::playSelected() { startSelected(false); }

void PlayerController::startSelected(bool share) {
  shareOnStart_ = false;
  const auto game = model_.game(selectedId_);
  if (!game || phase_ != PlayPhase::None || session_.isActive()) {
    return;
  }
  startError_.clear();
  const emu::SystemManifest* man = manifestFor(*game);
  if (man == nullptr) {
    startError_ = tr("There is no system manifest for this game.");
    emit selectedGameChanged();
    return;
  }
  emu::CoreLocation loc;
  const bool usable = coreUsable(*man, &loc);
  QString hubVersion;
  const bool offered = hubOffersCore(*man, &hubVersion);
  // Provision when the core is missing (or a cached one is damaged), or when the Hub serves another version than the
  // cached one. Explicit/env/app-dir cores are the user's choice and are never replaced.
  const bool versionDiffers = usable && loc.source == QLatin1String("cache") && loc.version != hubVersion;
  if (offered && (!usable || versionDiffers)) {
    shareOnStart_ = share;
    launchGame_ = *game;
    phase_ = PlayPhase::Core;
    emit selectedGameChanged();
    coreProv_->prepare(man->coreId, hubVersion);
    return;
  }
  if (!usable) {
    emit selectedGameChanged();  // detail pane shows the core status and hint
    return;
  }
  shareOnStart_ = share;
  continueStartAfterCore(*game, *man);
}

void PlayerController::onCoreFinished(const CoreResult& result) {
  // D15: whatever happens next, Library tiles, detail pane and Emulation page see the new core state at once.
  const auto refresh = qScopeGuard([this]() { refreshCoreState(); });
  if (phase_ != PlayPhase::Core) {
    return;
  }
  const auto game = model_.game(selectedId_);
  const emu::SystemManifest* man = game ? manifestFor(*game) : nullptr;
  if (!game || man == nullptr || game->id != launchGame_.id) {
    phase_ = PlayPhase::None;
    shareOnStart_ = false;
    emit selectedGameChanged();
    return;
  }
  if (result.ok) {
    coreProblems_.remove(man->coreId);
    // The core arrived after the handshake data was built: report it and fill the Emulation page on the next refresh.
    const bool known = std::any_of(coreList_.cbegin(), coreList_.cend(), [&](const CoreInfo& c) { return c.id == man->coreId && !c.version.isEmpty(); });
    if (!known && options_.probeCoreVersions) {
      coreList_.removeIf([&](const CoreInfo& c) { return c.id == man->coreId; });
      probeCores();
      handshake_.cores = coreList_;
      conn_->setHandshakeInfo(handshake_);
    }
    continueStartAfterCore(*game, *man);
    return;
  }
  coreProblems_.insert(man->coreId, result.problem);
  emu::CoreLocation loc;
  if (result.problem != QLatin1String("untrusted") && coreUsable(*man, &loc)) {
    // Another version of the core is cached (or env/legacy): the game still starts with it (version mismatch is only a warning).
    continueStartAfterCore(*game, *man);
    return;
  }
  phase_ = PlayPhase::None;
  shareOnStart_ = false;
  startError_ = tr("%1. The game was not started.").arg(coreProblemText(result.problem));
  emit selectedGameChanged();
}

void PlayerController::continueStartAfterCore(const GameEntry& gameRef, const emu::SystemManifest& manRef) {
  const GameEntry* game = &gameRef;
  const emu::SystemManifest* man = &manRef;
  phase_ = PlayPhase::None;  // the core step (if any) is over; the firmware/ROM steps set their own phase
  fwOptions_.clear();
  SystemInfo sys;
  if (nativeFirmware(*man, &sys)) {
    // Mode native: the files must be available and valid before anything else happens (also blocks the launch).
    const QStringList wanted = wantedFirmwareIds(*man);
    const QList<FirmwareProblem> missing = FirmwareProvisioner::missingOnHub(sys, wanted);
    if (!missing.isEmpty()) {
      emit selectedGameChanged();  // the detail pane shows "Firmware required/missing"
      return;
    }
    launchGame_ = *game;
    phase_ = PlayPhase::Firmware;
    emit selectedGameChanged();
    provisioner_->prepare(sys, wanted);
    return;
  }
  // Built-in firmware (default, old hubs): nothing is downloaded, the core is told explicitly.
  fwOptions_ = emu::firmwareCoreOptions(man->firmware, false, {});
  beginRomPhase(*game);
}

void PlayerController::beginRomPhase(const GameEntry& game) {
  pendingSha_ = game.romSha256;
  phase_ = PlayPhase::Rom;
  emit selectedGameChanged();
  downloader_->ensureRom(game);
}

void PlayerController::onFirmwareFinished(const FirmwareResult& result) {
  if (phase_ != PlayPhase::Firmware) {
    return;
  }
  const auto game = model_.game(selectedId_);
  const emu::SystemManifest* man = game ? manifestFor(*game) : nullptr;
  if (!game || man == nullptr || game->id != launchGame_.id) {
    phase_ = PlayPhase::None;
    emit selectedGameChanged();
    return;
  }
  if (!result.ok) {
    phase_ = PlayPhase::None;
    shareOnStart_ = false;
    QStringList names;
    for (const FirmwareProblem& p : result.problems) {
      names.append(p.displayName.isEmpty() ? p.fileId : p.displayName);
      if (p.reason == QLatin1String("invalid_hash") || p.reason == QLatin1String("invalid_size") || p.reason == QLatin1String("missing_on_hub")) {
        fwProblems_.append(p);  // blocks until the Hub reports other data (Check again)
      }
    }
    startError_ = tr("Firmware required/missing: %1. The game was not started.").arg(names.join(QStringLiteral(", ")));
    emit selectedGameChanged();
    return;
  }
  QStringList written;
  QString err;
  if (!emu::materializeFirmware(man->firmware, result.pathsById, systemDir(), &written, &err)) {
    phase_ = PlayPhase::None;
    shareOnStart_ = false;
    startError_ = tr("Firmware could not be prepared: %1").arg(err);
    emit selectedGameChanged();
    return;
  }
  fwOptions_ = emu::firmwareCoreOptions(man->firmware, true, written);
  fwProblems_.clear();
  beginRomPhase(*game);
}

void PlayerController::onRomReady(const QString& sha, const QString& path) {
  if (pendingSha_.isEmpty() || pendingSha_ != sha) {
    return;
  }
  pendingSha_.clear();
  RomStatus st;
  st.state = RomState::Ready;
  st.localPath = path;
  model_.setStatus(sha, st);
  for (const GameEntry& g : library_->games()) {
    if (g.romSha256 == sha && g.id == selectedId_) {
      launch(g, path);
      return;
    }
  }
  phase_ = PlayPhase::None;
  emit selectedGameChanged();
}

void PlayerController::launch(const GameEntry& game, const QString& romPath) {
  const emu::SystemManifest* man = manifestFor(game);
  if (man == nullptr || profiles_->hubDir(conn_->hubInfo().hubId).isEmpty()) {
    phase_ = PlayPhase::None;
    startError_ = tr("Cannot start the game (system or hub directory unknown).");
    emit selectedGameChanged();
    return;
  }
  phase_ = PlayPhase::Launching;
  launchGame_ = game;
  launchRom_ = romPath;
  saveReady_ = false;
  saveNoteStart_.clear();
  emit selectedGameChanged();
  // Start sync with the Hub first (before the core loads); the game starts in onSaveReady().
  saves_->prepareStart(game.id, romPath, {game.romSha256, QFileInfo(game.romFilename).completeBaseName()});
}

void PlayerController::onSaveReady(const QString& gameId, const QString& saveDir, const QString& note) {
  if (phase_ != PlayPhase::Launching || launchGame_.id != gameId) {
    return;
  }
  saveNoteStart_ = note;
  saveReady_ = true;
  if (!conflict_.isEmpty()) {
    conflict_.clear();
    emit saveConflictChanged();
  }
  const emu::SystemManifest* man = manifestFor(launchGame_);
  if (man == nullptr) {
    phase_ = PlayPhase::None;
    emit selectedGameChanged();
    return;
  }
  const emu::CoreLocation loc = locateCore(*man);
  GameSession::LaunchConfig cfg;
  cfg.title = launchGame_.title;
  cfg.corePath = loc.path;
  cfg.gamePath = launchRom_;
  cfg.systemDir = systemDir();
  cfg.saveDir = saveDir;
  // Manifest defaults < user overrides (game > system/core > global) < firmware mode of the Hub (builtin | native +
  // files) < manifest-locked options (screen layout, OSD off; the render mode is a user choice, default software): FrameBeam stays in control of those.
  cfg.coreOptions = emu::launchCoreOptions(*man, emulation_->launchOverrides(man->systemId, launchGame_.id), fwOptions_);
  cfg.display = man->display;
  {
    const auto fb = [&](const char* key) { return emulation_->frameBeamValue(QString::fromLatin1(key), man->systemId, launchGame_.id); };
    cfg.speedUpRatio = fb(EmulationController::kSpeedUpRatioKey).toDouble();
    cfg.speedUpOnStart = fb(EmulationController::kSpeedUpOnStartKey) == QLatin1String("true");
    cfg.speedUpAudio = fb(EmulationController::kSpeedUpAudioKey) != QLatin1String("false");
  }
  emit selectedGameChanged();
  session_.start(cfg);
}

QString PlayerController::formatWhen(const QDateTime& when) {
  if (!when.isValid()) {
    return tr("unknown time");
  }
  const QDateTime local = when.toLocalTime();
  return local.date() == QDate::currentDate() ? tr("today, %1").arg(local.toString(QStringLiteral("HH:mm")))
                                              : local.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

void PlayerController::onSaveConflict(const SaveSync::ConflictView& v) {
  const SaveConflictInfo& c = v.conflict;
  const auto side = [](const QString& title, const QString& when, const QString& base) {
    return QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("when"), when}, {QStringLiteral("base"), base}};
  };
  QVariantMap m;
  m.insert(QStringLiteral("active"), true);
  m.insert(QStringLiteral("busy"), false);
  m.insert(QStringLiteral("error"), QString());
  m.insert(QStringLiteral("id"), c.id);
  m.insert(QStringLiteral("gameTitle"), launchGame_.title);
  m.insert(QStringLiteral("hub"),
           side(tr("Current checkpoint · %1").arg(c.hubDeviceName),
                tr("%1 · Rev %2").arg(formatWhen(QDateTime::fromString(c.hubCreatedAt, Qt::ISODate)), QString::number(c.hubRevision)),
                tr("Hash %1").arg(c.hubSha256.left(8))));
  m.insert(QStringLiteral("local"),
           side(tr("Local · %1").arg(v.localDeviceName), tr("%1 · sync pending").arg(formatWhen(v.localModified)),
                tr("Base: Rev %1 · hash %2").arg(v.localBaseRevision).arg(v.localSha256.left(8))));
  conflict_ = m;
  emit saveConflictChanged();
}

void PlayerController::resolveSaveConflict(const QString& action) {
  if (conflict_.isEmpty() || conflict_.value(QStringLiteral("busy")).toBool()) {
    return;
  }
  conflict_.insert(QStringLiteral("busy"), true);
  conflict_.insert(QStringLiteral("error"), QString());
  emit saveConflictChanged();
  saves_->resolveConflict(action == QLatin1String("use_hub")     ? SaveSync::Resolution::UseHub
                          : action == QLatin1String("use_local") ? SaveSync::Resolution::UseLocal
                                                                 : SaveSync::Resolution::DecideLater);
}

void PlayerController::playAndShareSelected() {
  if (phase_ != PlayPhase::None || session_.isActive()) {
    return;
  }
  startSelected(true);
}

void PlayerController::leaveGameView() {
  if (gameActive_) {
    quitGame();
  } else {
    sessions_->leaveWatch();
    updateScreen();
  }
}

void PlayerController::quitGame() {
  sessions_->gameEnded();
  session_.stop();  // core unloaded (save flushed), then the final upload
  saves_->finalSync(true);
  gameActive_ = false;
  phase_ = PlayPhase::None;
  updateScreen();
  emit selectedGameChanged();
}

}  // namespace framebeam::ui
