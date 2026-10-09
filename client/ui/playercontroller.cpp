#include "playercontroller.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStyleHints>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#include "core_options.h"
#include "filelog.h"
#include "firmware_materializer.h"
#include "installroot.h"
#include "libretro_backend.h"
#include "mediacaps.h"
#include "semver.h"
#include "version.h"

namespace framebeam::ui {

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
  // Library sort and "Ready first" are saved per device (3c-2).
  model_.setSortKey(settings_->librarySort());
  model_.setReadyFirst(settings_->libraryReadyFirst());
  connect(&model_, &LibraryModel::sortChanged, this, [this]() {
    settings_->setLibrarySort(model_.sortKey());
    settings_->setLibraryReadyFirst(model_.readyFirst());
  });
  fwCache_ = std::make_unique<FirmwareCache>(QDir(CoreCatalog::systemDirIn(profiles_->baseDir())).filePath(QStringLiteral("firmware")));
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
  connect(provisioner_.get(), &FirmwareProvisioner::finished, this, [this](const FirmwareResult& r) { starter_->onFirmwareFinished(r); });
  connect(coreProv_.get(), &CoreProvisioner::finished, this, [this](const CoreResult& r) { starter_->onCoreFinished(r); });
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
  connect(saves_.get(), &SaveSync::startReady, this,
          [this](const QString& gameId, const QString& saveDir, const QString& note) { starter_->onSaveReady(gameId, saveDir, note); });
  connect(saves_.get(), &SaveSync::startConflict, this, [this](const SaveSync::ConflictView& v) { starter_->onSaveConflict(v); });
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
  {
    CoreCatalog::Deps cd{conn_.get(), profiles_.get(), systems_.get(), coreCache_.get(), emulation_.get(), manifests_, locator_,
                         coreList_, coreVersions_, coreNames_, coreProblems_, fwProblems_, phase_, launchGame_};
    catalog_ = std::make_unique<CoreCatalog>(cd);
    GameDetail::Deps dd{model_, selectedId_, saves_.get(), cache_.get(), conn_.get(), systems_.get(), fwCache_.get(), catalog_.get(),
                        coreVersions_, fwProblems_, phase_, saveReady_, saveNoteStart_, startError_};
    detail_ = std::make_unique<GameDetail>(dd);
    hubPresenter_ = std::make_unique<HubPresenter>(HubPresenter::Deps{conn_.get(), profiles_.get(), lastError_, inviteBusy_});
    GameStarter::Deps sd{conn_.get(), profiles_.get(), saves_.get(), coreProv_.get(), provisioner_.get(), downloader_.get(),
                         library_.get(), emulation_.get(), catalog_.get(), model_, session_, selectedId_, phase_, launchGame_,
                         launchRom_, saveReady_, saveNoteStart_, startError_, pendingSha_, fwOptions_, shareOnStart_, fwProblems_,
                         coreProblems_, coreList_, handshake_, conflict_, options.probeCoreVersions};
    starter_ = std::make_unique<GameStarter>(sd);
    connect(starter_.get(), &GameStarter::selectedGameChanged, this, &PlayerController::selectedGameChanged);
    connect(starter_.get(), &GameStarter::saveConflictChanged, this, &PlayerController::saveConflictChanged);
    connect(starter_.get(), &GameStarter::coreStateRefreshRequested, this, &PlayerController::refreshCoreState);
    connect(starter_.get(), &GameStarter::gameLaunched, this, [this](const QString& gameId) { recordLastPlayed(gameId); });
  }
  if (options.probeCoreVersions) {
    catalog_->probeCores();
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
    // Saves view inside the game: restore / upload / "use the Hub save" load the save into the running core.
    SaveSync::LiveHooks hooks;
    hooks.ready = [this]() { return session_.liveSaveReady(); };
    hooks.flush = [this]() { session_.flushLiveSave(); };
    hooks.accepts = [this](qint64 size) { return session_.liveSaveAccepts(size); };
    hooks.apply = [this](const QByteArray& data) { return session_.restartWithSave(saves_->activeSaveFile(), data); };
    saves_->setLiveHooks(std::move(hooks));
  }
  {
    SaveHistoryController::Env env;
    env.saves = saves_.get();
    env.connection = conn_.get();
    env.gameBusy = [this](const QString& id) {
      return (phase_ != PlayPhase::None || gameActive_ || session_.isActive()) && (launchGame_.id.isEmpty() || launchGame_.id == id);
    };
    env.gameTitle = [this](const QString& id) {
      const auto g = model_.game(id);
      return g ? g->title : QString();
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
  connect(sessions_.get(), &SessionController::watchChanged, this, [this]() {
    // Watching a Session from the Library while a game is in the background shows the game view again (multiview with
    // the paused game); the game stays paused until the user resumes it.
    if (background_ && sessions_->watching()) {
      setBackground(false);
    }
    updateScreen();
  });
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
          [this](const QString& sha, const QString& path) { starter_->onRomReady(sha, path); });

  // Input: gamepad P1 and the keyboard profile are merged into the joypad mask of the game.
  session_.setKeyboardMap(controllers_->keyboardMap());
  connect(controllers_.get(), &ControllersController::keyboardMapChanged, this,
          [this]() { session_.setKeyboardMap(controllers_->keyboardMap()); });
  session_.setHotkeyKeys(controllers_->hotkeyKeys());
  connect(controllers_.get(), &ControllersController::hotkeysChanged, this, [this]() { session_.setHotkeyKeys(controllers_->hotkeyKeys()); });
  connect(controllers_.get(), &ControllersController::libretroMaskChanged, this,
          [this](quint32 mask) { session_.setGamepadMask(mask); });
  session_.setGamepadMask(controllers_->gamepads()->libretroMask());
  connect(emulation_.get(), &EmulationController::frameBeamOptionsChanged, this, &PlayerController::applyFrameBeamOptions);
  // The core choice of a system changed: the new core's options, status and the game detail follow at once.
  connect(emulation_.get(), &EmulationController::coreChoiceChanged, this, [this]() {
    refreshEmulationPage();
    refreshAttention();
    emit selectedGameChanged();
  });
  applyFrameBeamOptions();
  connect(&session_, &GameSession::stateChanged, this, [this]() { emulation_->setGameRunning(session_.isActive()); });

  connect(&session_, &GameSession::started, this, [this]() {
    applyFrameBeamOptions();
    saves_->beginSession();
    phase_ = PlayPhase::None;
    gameActive_ = true;
    gameStartedMs_ = QDateTime::currentMSecsSinceEpoch();
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
  setBackground(false);
  gameActive_ = false;
  if (sessions_) {
    sessions_->gameEnded();  // ends a shared Session
  }
}

void PlayerController::shutdown() {
  background_ = false;  // app close with a game in the background: same path as a game in the foreground
  if (sessions_) {
    sessions_->gameEnded();
  }
  session_.stop();  // unloads the core first so it flushes its save
  if (saves_) {
    saves_->finalSyncBlocking(true);
  }
}

// ---------------------------------------------------------------- Cores / Handshake

void PlayerController::refreshAttention() {
  QHash<QString, QString> bySystem;
  QHash<QString, QString> byGame;
  for (const GameEntry& g : library_->games()) {
    if (!bySystem.contains(g.system)) {
      bySystem.insert(g.system, catalog_->attentionFor(g));
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
  for (const emu::SystemManifest& sysManifest : manifests_.all()) {
    const emu::SystemManifest* mp = catalog_->manifestForSystem(sysManifest.systemId);
    if (mp == nullptr || mp->coreId.isEmpty() || emulation_->hasCoreProbe(mp->coreId)) {
      continue;
    }
    const emu::SystemManifest& m = *mp;
    const emu::CoreLocation loc = catalog_->locateCore(m);
    if (loc.found() && !session_.isActive()) {
      const emu::CoreProbe probe = emu::probeCore(loc.path, catalog_->systemDir(), QDir(profiles_->baseDir()).filePath(QStringLiteral("probe")));
      if (probe.ok) {
        coreNames_.insert(m.coreId, probe.info.name);
        coreVersions_.insert(m.coreId, probe.info.version);
        emulation_->setCoreProbe(m.coreId, probe, true);
        continue;
      }
    }
    emulation_->loadCoreCache(m.coreId);
  }
  emulation_->setSystems(catalog_->systemCards());
  emulation_->setGameRunning(session_.isActive());
}

// ---------------------------------------------------------------- Navigation

QString PlayerController::logFile() const { return QDir::toNativeSeparators(filelog::path()); }

QString PlayerController::userName() const { return conn_->userDisplayName(); }
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
  if ((gameActive_ && !background_) || (sessions_ && sessions_->watching())) {
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

QString PlayerController::deviceFooter() const {
  const HandshakeInfo h = HandshakeInfo::detect();
  return tr("This device: %1 · Player %2 · %3")
      .arg(profiles_->deviceName(), h.playerVersion, platformLabel(h.platform, h.arch));
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
  if (conn_->state() != HubConnection::State::Connected || (gameActive_ && !background_)) {
    return;
  }
  page_ = QStringLiteral("settings");
  updateScreen();
}

void PlayerController::showEmulation() {
  if (conn_->state() != HubConnection::State::Connected || (gameActive_ && !background_)) {
    return;
  }
  refreshEmulationPage();
  page_ = QStringLiteral("emulation");
  updateScreen();
}

void PlayerController::showControllers() {
  if (conn_->state() != HubConnection::State::Connected || (gameActive_ && !background_)) {
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

QVariantList PlayerController::hubs() const { return hubPresenter_->hubs(); }
QVariantMap PlayerController::pairing() const { return hubPresenter_->pairing(); }
QString PlayerController::formatFingerprint(const QString& fp) { return HubPresenter::formatFingerprint(fp); }
QString PlayerController::platformLabel(const QString& platform, const QString& arch) {
  return HubPresenter::platformLabel(platform, arch);
}

QVariantList PlayerController::coreWarnings() const {
  QVariantList list;
  for (const HandshakeProblem& p : conn_->handshakeProblems()) {
    const bool missing = p.code == QLatin1String("core_missing");
    if (!missing && p.code != QLatin1String("core_version_mismatch")) {
      continue;
    }
    QString label = p.coreId;
    if (const emu::CoreProfile* prof = manifests_.profile(p.coreId)) {
      label = coreNames_.value(p.coreId, prof->displayName);
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
    setBackground(false);
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

// Last played is local and kept per Hub (a game id on another Hub is a different game).
void PlayerController::recordLastPlayed(const QString& gameId, qint64 msecs) {
  const qint64 now = msecs > 0 ? msecs : QDateTime::currentMSecsSinceEpoch();
  settings_->setLastPlayed(conn_->hubInfo().hubId, gameId, now);
  model_.noteLastPlayed(gameId, now);
}

void PlayerController::reloadLibrary() {
  libraryState_ = QStringLiteral("loading");
  emit libraryStateChanged();
  library_->reload();
}

void PlayerController::onLibraryLoaded() {
  quietRefresh_ = false;
  model_.setLastPlayed(settings_->lastPlayedAll(conn_->hubInfo().hubId));
  model_.setGames(library_->games(), [this](const GameEntry& g) { return downloader_->status(g); });
  {
    // "42 games · Nintendo DS": the system name shows only when all games share one system.
    QString label;
    bool single = true;
    for (const GameEntry& g : library_->games()) {
      const emu::SystemManifest* man = catalog_->manifestFor(g);
      const QString name = man != nullptr ? man->displayName : g.system.toUpper();
      if (label.isEmpty()) {
        label = name;
      } else if (label != name) {
        single = false;
      }
    }
    model_.setSystemLabel(single ? label : QString());
  }
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

void PlayerController::playSelected() {
  if (background_) {
    if (selectedId_ == backgroundId_) {
      resumeGame();
    } else {
      askQuitAndStart(false);
    }
    return;
  }
  starter_->startSelected(false);
}

void PlayerController::resolveSaveConflict(const QString& action) { starter_->resolveSaveConflict(action); }

QVariantMap PlayerController::selectedGame() const {
  QVariantMap m = detail_->selectedGame();
  if (background_ && !m.isEmpty() && m.value(QStringLiteral("id")).toString() == backgroundId_) {
    // The game runs (paused) in the background: the primary button resumes it instead of starting it again.
    m.insert(QStringLiteral("running"), true);
    m.insert(QStringLiteral("playLabel"), tr("Resume"));
    m.insert(QStringLiteral("canPlay"), true);
    m.insert(QStringLiteral("busy"), false);
    m.insert(QStringLiteral("pillText"), tr("❚❚ Running · paused"));
    m.insert(QStringLiteral("pillTone"), QStringLiteral("warn"));  // the accent chip (3c-5)
  }
  return m;
}

void PlayerController::adoptPreviewGame(const QString& gameId) {
  if (const auto g = model_.game(gameId)) {
    launchGame_ = *g;
  }
}

QVariantMap PlayerController::backgroundGame() const {
  if (!background_) {
    return {};
  }
  return {{QStringLiteral("id"), backgroundId_},
          {QStringLiteral("title"), backgroundTitle_},
          {QStringLiteral("startedMs"), gameStartedMs_},
          {QStringLiteral("pausedMs"), gamePausedMs_}};
}

QVariantMap PlayerController::startConfirm() const {
  QVariantMap m{{QStringLiteral("active"), confirmActive_}};
  if (confirmActive_) {
    const auto g = model_.game(selectedId_);
    m.insert(QStringLiteral("runningTitle"), backgroundTitle_);
    m.insert(QStringLiteral("newTitle"), g ? g->title : QString());
  }
  return m;
}

void PlayerController::setBackground(bool on) {
  const bool wasConfirm = confirmActive_;
  confirmActive_ = false;
  if (on == background_) {
    if (wasConfirm) emit startConfirmChanged();
    return;
  }
  background_ = on;
  if (on) {
    backgroundId_ = launchGame_.id;
    backgroundTitle_ = launchGame_.title;
    gamePausedMs_ = QDateTime::currentMSecsSinceEpoch();
  } else {
    backgroundId_.clear();
    backgroundTitle_.clear();
  }
  session_.setInputBlocked(on);
  emit backgroundGameChanged();
  if (wasConfirm) emit startConfirmChanged();
  emit selectedGameChanged();
}

void PlayerController::askQuitAndStart(bool share) {
  if (!background_ || !model_.game(selectedId_) || phase_ != PlayPhase::None) {
    return;
  }
  confirmActive_ = true;
  confirmShare_ = share;
  emit startConfirmChanged();
}

void PlayerController::cancelQuitAndStart() {
  if (confirmActive_) {
    confirmActive_ = false;
    emit startConfirmChanged();
  }
}

void PlayerController::confirmQuitAndStart() {
  if (!confirmActive_) {
    return;
  }
  const bool share = confirmShare_;
  quitGame();  // saved and synced first (core unloaded, final upload), also clears the confirmation
  starter_->startSelected(share);
}

void PlayerController::playAndShareSelected() {
  if (background_) {
    if (selectedId_ == backgroundId_) {
      resumeGame();
    } else {
      askQuitAndStart(true);
    }
    return;
  }
  if (phase_ != PlayPhase::None || session_.isActive()) {
    return;
  }
  starter_->startSelected(true);
}

void PlayerController::leaveGameView() {
  if (gameActive_ && !background_) {
    // Pause and keep the game loaded (the pause triggers the immediate save sync); a shared Session stays shared.
    page_ = QStringLiteral("library");
    session_.pause();
    setBackground(true);
    if (sessions_->watching()) {
      sessions_->leaveWatch();  // remote surfaces end with the game view
    }
    updateScreen();
  } else if (sessions_->watching()) {
    sessions_->leaveWatch();  // a game in the background stays there
    updateScreen();
  }
}

void PlayerController::resumeGame() {
  if (!gameActive_ || !background_) {
    return;
  }
  setBackground(false);
  session_.resume();
  updateScreen();
}

void PlayerController::requestQuit() {
  if (quitting_ || !gameActive_) {
    return;
  }
  quitting_ = true;
  emit quittingChanged();
  // quitGame() blocks until the core is unloaded: let the "Saving and syncing…" state paint first.
  QTimer::singleShot(80, this, [this]() { quitGame(); });
}

void PlayerController::quitGame() {
  if (quitting_) {
    quitting_ = false;
    emit quittingChanged();
  }
  setBackground(false);
  sessions_->gameEnded();
  session_.stop();  // core unloaded (save flushed), then the final upload
  saves_->finalSync(true);
  gameActive_ = false;
  phase_ = PlayPhase::None;
  updateScreen();
  emit selectedGameChanged();
}

}  // namespace framebeam::ui
