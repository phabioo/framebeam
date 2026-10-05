#include "playercontroller.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSysInfo>
#include <algorithm>

#include "libretro_backend.h"
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
  if (options.probeCoreVersions) {
    probeCores();
  }
  HandshakeInfo info = HandshakeInfo::detect();
  info.cores = coreList_;
  conn_->setHandshakeInfo(info);

  connect(conn_.get(), &HubConnection::stateChanged, this, &PlayerController::onConnectionState);
  connect(conn_.get(), &HubConnection::errorOccurred, this, [this](const QString&, const QString& message) {
    lastError_ = message;
    emit hubsChanged();
    emit pairingChanged();
  });
  connect(library_.get(), &HubLibrary::loaded, this, &PlayerController::onLibraryLoaded);
  connect(library_.get(), &HubLibrary::loadFailed, this, [this](const QString&, const QString& message) {
    libraryState_ = QStringLiteral("error");
    libraryError_ = message;
    emit libraryStateChanged();
  });
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

  connect(&session_, &GameSession::started, this, [this]() {
    saves_->beginSession();
    phase_ = PlayPhase::None;
    gameActive_ = true;
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
    gameActive_ = false;
    startError_ = tr("The emulator could not start: %1").arg(msg);
    updateScreen();
    emit selectedGameChanged();
  });
  connect(&session_, &GameSession::errorChanged, this, [this]() {
    // Runtime error in the game: back to the Library, error shown in the detail pane.
    if (gameActive_ && session_.state() == GameSession::Failed) {
      saves_->finalSync(true);
      gameActive_ = false;
      startError_ = tr("The emulator was terminated: %1").arg(session_.errorText());
      updateScreen();
      emit selectedGameChanged();
    }
  });
}

PlayerController::~PlayerController() { shutdown(); }

void PlayerController::shutdown() {
  session_.stop();  // unloads the core first so it flushes its save
  if (saves_) {
    saves_->finalSyncBlocking(true);
  }
}

// ---------------------------------------------------------------- Cores / Handshake

void PlayerController::probeCores() {
  // Pragmatic: the core version is only available in the core info after loadCore(). We load the core once
  // without a game, read name/version and unload it again. If that fails, we report only the core_id
  // (empty version) instead of guessing.
  for (const emu::SystemManifest& m : manifests_.all()) {
    const emu::CoreLocation loc = locator_.locate(m);
    if (!loc.found()) {
      continue;
    }
    const bool known = std::any_of(coreList_.cbegin(), coreList_.cend(), [&](const CoreInfo& c) { return c.id == m.coreId; });
    if (known) {
      continue;
    }
    CoreInfo ci;
    ci.id = m.coreId;
    emu::LibretroBackend be;
    be.setSystemDirectory(systemDir());
    be.setSaveDirectory(QDir(profiles_->baseDir()).filePath(QStringLiteral("probe")));
    QString err;
    if (be.loadCore(loc.path, &err)) {
      ci.version = be.coreInfo().version;
      coreNames_.insert(m.coreId, be.coreInfo().name);
      be.unloadCore();
    }
    coreList_.append(ci);
  }
}

QString PlayerController::coreLabel(const emu::SystemManifest& m, const emu::CoreLocation&) const {
  const QString probed = coreNames_.value(m.coreId);
  if (!probed.isEmpty()) {
    return probed;
  }
  static const QMap<QString, QString> kNames{{QStringLiteral("melonds_ds"), QStringLiteral("melonDS DS")}};
  return kNames.value(m.coreId, m.coreId);
}

QString PlayerController::systemDir() const {
  return QDir(profiles_->baseDir()).filePath(QStringLiteral("system"));
}

// ---------------------------------------------------------------- Navigation

void PlayerController::startup() {
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
  if (gameActive_) {
    next = QStringLiteral("game");
  } else if (s == S::Connected) {
    next = QStringLiteral("library");
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
  if (s == S::NeedsTrustConfirmation || s == S::NeedsPairing || s == S::AwaitingApproval || s == S::Denied ||
      s == S::Expired) {
    pairingFlow_ = true;
  } else if (s == S::Disconnected || s == S::Connected || s == S::Unreachable || s == S::Incompatible ||
             s == S::CertificateChanged) {
    pairingFlow_ = false;
  }
  if (s == S::Connected) {
    libraryState_ = QStringLiteral("loading");
    libraryError_.clear();
    emit libraryStateChanged();
    emit hubChanged();
    library_->reload();
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
        message = tr("The fingerprint no longer matches the saved one. The connection is blocked. "
                     "A new fingerprint is never accepted automatically: verify it outside the Player (e.g. in the "
                     "hub web interface) and add the hub again after removing it.");
        m.insert(QStringLiteral("expectedFingerprint"), formatFingerprint(conn_->expectedFingerprint().isEmpty() ? p.pinnedFingerprint : conn_->expectedFingerprint()));
        m.insert(QStringLiteral("observedFingerprint"), formatFingerprint(conn_->observedFingerprint()));
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
  if (!matched && (s == S::Unreachable || s == S::Incompatible || s == S::CertificateChanged)) {
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
  if (hubId.isEmpty()) {
    conn_->disconnectFromHub();
  } else {
    conn_->removeProfile(hubId);
  }
  notice_.clear();
  updateScreen();
  emit hubsChanged();
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
  return m;
}

void PlayerController::confirmTrust() { conn_->confirmTrust(); }
void PlayerController::rejectTrust() { conn_->rejectTrust(); }
void PlayerController::requestPairing() { conn_->requestPairing(); }
void PlayerController::cancelPairing() { conn_->cancelPairing(); }
void PlayerController::leavePairing() { conn_->disconnectFromHub(); }

// ---------------------------------------------------------------- 3c Library

QString PlayerController::hubName() const { return conn_->hubInfo().name; }
QString PlayerController::hubAddress() const { return trimmedScheme(conn_->address()); }

void PlayerController::switchHub() {
  if (gameActive_) {
    session_.stop();
    saves_->finalSyncBlocking(true);  // needs the connection: before disconnecting
    gameActive_ = false;
  }
  conn_->disconnectFromHub();
  updateScreen();
}

void PlayerController::reloadLibrary() {
  libraryState_ = QStringLiteral("loading");
  emit libraryStateChanged();
  library_->reload();
}

void PlayerController::onLibraryLoaded() {
  model_.setGames(library_->games(), [this](const GameEntry& g) { return downloader_->status(g); });
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
  bool fwOk = true;
  QString coreText;
  QString coreHint;
  QString fwText = tr("Not required");
  QString fwHint;
  if (man == nullptr) {
    coreText = tr("no system manifest for .%1").arg(ext);
  } else {
    const emu::CoreLocation loc = locator_.locate(*man);
    const QString label = coreLabel(*man, loc);
    coreOk = loc.found();
    coreText = coreOk ? tr("%1 · ready").arg(label) : tr("%1 · missing").arg(label);
    if (!coreOk) {
      coreHint = tr("Not found. Set %1 or place the library in %2.")
                     .arg(emu::CoreLocator::environmentVariableFor(man->coreId),
                          loc.tried.isEmpty() ? QString() : QDir::toNativeSeparators(loc.tried.last()));
    }
    if (man->firmware.required) {
      QStringList missing;
      for (const emu::FirmwareFile& f : man->firmware.files) {
        if (f.required && !QFileInfo::exists(QDir(systemDir()).filePath(f.name))) {
          missing.append(f.name);
        }
      }
      fwOk = missing.isEmpty();
      fwText = fwOk ? tr("present") : tr("missing");
      if (!fwOk) {
        fwHint = tr("Firmware missing: %1 in %2").arg(missing.join(QStringLiteral(", ")), QDir::toNativeSeparators(systemDir()));
      }
    }
  }
  m.insert(QStringLiteral("coreText"), coreText);
  m.insert(QStringLiteral("coreTone"), coreOk ? QStringLiteral("ok") : QStringLiteral("error"));
  m.insert(QStringLiteral("coreHint"), coreHint);
  m.insert(QStringLiteral("firmwareText"), fwText);
  m.insert(QStringLiteral("firmwareTone"), fwOk ? QStringLiteral("neutral") : QStringLiteral("error"));
  m.insert(QStringLiteral("firmwareHint"), fwHint);

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
  list.append(checkItem(romReady ? tr("ROM verified from cache") : tr("Download and verify ROM"),
                        romReady ? QStringLiteral("done") : (phase_ == PlayPhase::Rom ? QStringLiteral("active") : QStringLiteral("pending")),
                        romReady ? QString() : romText));
  list.append(checkItem(tr("Save checked with Hub"),
                        phase_ == PlayPhase::Launching ? (saveReady_ ? QStringLiteral("done") : QStringLiteral("active")) : QStringLiteral("pending"),
                        saveNoteStart_));
  list.append(checkItem(tr("Core ready"), coreOk ? QStringLiteral("done") : QStringLiteral("error"),
                        coreOk ? QString() : coreText));
  list.append(checkItem(tr("Emulator starting"), (phase_ == PlayPhase::Launching && saveReady_) ? QStringLiteral("active") : QStringLiteral("pending")));
  m.insert(QStringLiteral("checklist"), list);

  QString error = startError_;
  m.insert(QStringLiteral("error"), error);
  m.insert(QStringLiteral("busy"), busy);
  m.insert(QStringLiteral("canPlay"), !busy && man != nullptr && coreOk && fwOk && kind != QLatin1String("validating") &&
                                          kind != QLatin1String("downloading") && kind != QLatin1String("unknown"));
  QString label = tr("Play");
  if (busy) {
    label = phase_ == PlayPhase::Launching ? tr("Starting…") : tr("Downloading…");
  } else if (kind == QLatin1String("download")) {
    label = tr("Download and play");
  } else if (kind == QLatin1String("mismatch") || kind == QLatin1String("failed")) {
    label = tr("Reload and play");
  }
  m.insert(QStringLiteral("playLabel"), label);
  return m;
}

void PlayerController::playSelected() {
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
  const emu::CoreLocation loc = locator_.locate(*man);
  if (!loc.found()) {
    emit selectedGameChanged();  // detail pane shows the path hint
    return;
  }
  pendingSha_ = game->romSha256;
  phase_ = PlayPhase::Rom;
  emit selectedGameChanged();
  downloader_->ensureRom(*game);
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
  const emu::CoreLocation loc = locator_.locate(*man);
  GameSession::LaunchConfig cfg;
  cfg.title = launchGame_.title;
  cfg.corePath = loc.path;
  cfg.gamePath = launchRom_;
  cfg.systemDir = systemDir();
  cfg.saveDir = saveDir;
  cfg.coreOptions = man->coreOptions;
  cfg.display = man->display;
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

void PlayerController::quitGame() {
  session_.stop();  // core unloaded (save flushed), then the final upload
  saves_->finalSync(true);
  gameActive_ = false;
  phase_ = PlayPhase::None;
  updateScreen();
  emit selectedGameChanged();
}

}  // namespace framebeam::ui
