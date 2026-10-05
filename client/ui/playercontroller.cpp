#include "playercontroller.h"

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

  QString err;
  if (!manifests_.loadBuiltin(&err)) {
    qWarning().noquote() << "Manifeste nicht geladen:" << err;
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
    phase_ = PlayPhase::None;
    gameActive_ = true;
    updateScreen();
    emit selectedGameChanged();
  });
  connect(&session_, &GameSession::startFailed, this, [this](const QString& msg) {
    phase_ = PlayPhase::None;
    gameActive_ = false;
    startError_ = tr("Der Emulator konnte nicht starten: %1").arg(msg);
    updateScreen();
    emit selectedGameChanged();
  });
  connect(&session_, &GameSession::errorChanged, this, [this]() {
    // Laufzeitfehler im Spiel: zurueck zur Library, Fehler in der Detailspalte.
    if (gameActive_ && session_.state() == GameSession::Failed) {
      gameActive_ = false;
      startError_ = tr("Der Emulator wurde beendet: %1").arg(session_.errorText());
      updateScreen();
      emit selectedGameChanged();
    }
  });
}

PlayerController::~PlayerController() {
  session_.stop();
}

// ---------------------------------------------------------------- Cores / Handshake

void PlayerController::probeCores() {
  // Pragmatisch: Die Core-Version steht nur in der Core-Info nach loadCore(). Wir laden den Core einmal
  // ohne Spiel, lesen Name/Version und entladen ihn wieder. Scheitert das, melden wir nur die core_id
  // (leere Version) statt zu raten.
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

QString PlayerController::saveDirForCurrentHub() const {
  const QString hubDir = profiles_->hubDir(conn_->hubInfo().hubId);
  return hubDir.isEmpty() ? QString() : QDir(hubDir).filePath(QStringLiteral("saves"));
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
  return tr("Dieses Gerät: %1 · Player %2 · %3")
      .arg(profiles_->deviceName(), h.playerVersion, platformLabel(h.platform, h.arch));
}

QVariantMap PlayerController::hubCard(const HubProfile& p) const {
  using S = HubConnection::State;
  QVariantMap m;
  m.insert(QStringLiteral("hubId"), p.hubId);
  m.insert(QStringLiteral("name"), p.name.isEmpty() ? trimmedScheme(p.address) : p.name);
  m.insert(QStringLiteral("saved"), true);
  m.insert(QStringLiteral("isLast"), p.hubId == profiles_->lastHubId());
  const QString last = p.lastConnected.isValid() ? tr("zuletzt %1").arg(p.lastConnected.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")))
                                                 : tr("noch nie verbunden");
  m.insert(QStringLiteral("detail"), trimmedScheme(p.address) + QStringLiteral(" · ") + last);

  const bool current = conn_->state() != S::Disconnected &&
                       (conn_->address() == p.address || (conn_->profile() && conn_->profile()->hubId == p.hubId));
  QString status = QStringLiteral("idle");
  QString text = tr("Bereit zum Verbinden");
  QString tone = QStringLiteral("neutral");
  QString message;
  if (current) {
    switch (conn_->state()) {
      case S::Identifying:
      case S::Authenticating:
        status = QStringLiteral("connecting");
        text = tr("Verbinde…");
        break;
      case S::CertificateChanged:
        status = QStringLiteral("certChanged");
        text = tr("Zertifikat geändert");
        tone = QStringLiteral("error");
        message = tr("Der Fingerprint stimmt nicht mehr mit dem gespeicherten überein. Die Verbindung ist blockiert. "
                     "Ein neuer Fingerprint wird nie automatisch übernommen: Prüfe ihn außerhalb des Players (z. B. im "
                     "Hub-Webinterface) und füge den Hub nach dem Entfernen neu hinzu.");
        m.insert(QStringLiteral("expectedFingerprint"), formatFingerprint(conn_->expectedFingerprint().isEmpty() ? p.pinnedFingerprint : conn_->expectedFingerprint()));
        m.insert(QStringLiteral("observedFingerprint"), formatFingerprint(conn_->observedFingerprint()));
        break;
      case S::Incompatible: {
        status = QStringLiteral("incompatible");
        const bool hubOld = conn_->incompatibleReason() == HubConnection::IncompatibleReason::HubTooOld;
        text = hubOld ? tr("Hub zu alt") : tr("Player zu alt");
        tone = QStringLiteral("warn");
        const HubInfo& hi = conn_->hubInfo();
        message = hubOld ? tr("Hub spricht Protokoll v%1, Player benötigt mindestens v%2").arg(hi.protocolVersion).arg(kMinProtocolVersion)
                         : tr("Hub verlangt mindestens Protokoll v%1, Player spricht v%2").arg(hi.minProtocolVersion).arg(kProtocolVersion);
        break;
      }
      case S::Unreachable:
        status = QStringLiteral("unreachable");
        text = tr("Nicht erreichbar");
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
    // Versuch mit einer noch nicht gespeicherten Adresse: als Karte mit dem Ergebnis zeigen.
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
    notice_ = tr("Bitte eine Hub-Adresse eingeben, z. B. hub.local:8443.");
    emit hubsChanged();
    return;
  }
  conn_->connectToAddress(address, options_.allowHttp);
  // Nach dem Aufruf setzen: ein Trennen der alten Verbindung (z. B. nach Unreachable) setzt das Flag sonst zurueck.
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
  // Gekoppeltes Profil (Credential vorhanden): nie den Pairing-Statusbildschirm zeigen.
  const std::optional<HubProfile> prof = conn_->profile();
  const bool paired = prof.has_value() && !prof->credentialRef.isEmpty();
  if (lastAttemptPairing_ && !paired && conn_->state() == S::Identifying) {
    pairingFlow_ = true;  // wie addHub: Statusbildschirm erneut zeigen (retry trennt intern und setzt das Flag zurueck)
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
                      ? tr("Der Download passt nicht zum erwarteten SHA-256 und wurde verworfen. Bitte erneut laden.")
                      : tr("Download fehlgeschlagen: %1").arg(st.errorMessage.isEmpty() ? st.errorCode : st.errorMessage);
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
  m.insert(QStringLiteral("sizeText"), tr("%1 (%2 Bytes)").arg(LibraryModel::formatSize(game->romSize)).arg(game->romSize));
  m.insert(QStringLiteral("cachePath"), cache_->finalPath(game->romSha256, ext));
  const qint64 total = st.totalBytes > 0 ? st.totalBytes : game->romSize;
  m.insert(QStringLiteral("progress"), total > 0 ? static_cast<double>(st.receivedBytes) / static_cast<double>(total) : 0.0);

  // ROM-Zeile
  QString romText;
  QString romTone = QStringLiteral("neutral");
  if (kind == QLatin1String("ready")) {
    romText = tr("Lokal gecacht · geprüft");
    romTone = QStringLiteral("ok");
  } else if (kind == QLatin1String("download")) {
    romText = tr("Download nötig · %1").arg(LibraryModel::formatSize(game->romSize));
  } else if (kind == QLatin1String("downloading")) {
    romText = tr("Lädt %1 %").arg(static_cast<int>(m.value(QStringLiteral("progress")).toDouble() * 100));
  } else if (kind == QLatin1String("mismatch")) {
    romText = tr("Hash mismatch · neu laden");
    romTone = QStringLiteral("error");
  } else if (kind == QLatin1String("failed")) {
    romText = tr("Download fehlgeschlagen");
    romTone = QStringLiteral("error");
  } else {
    romText = tr("Prüfe…");
  }
  m.insert(QStringLiteral("romText"), romText);
  m.insert(QStringLiteral("romTone"), romTone);

  // Core / Firmware
  bool coreOk = false;
  bool fwOk = true;
  QString coreText;
  QString coreHint;
  QString fwText = tr("nicht benötigt");
  QString fwHint;
  if (man == nullptr) {
    coreText = tr("kein System-Manifest für .%1").arg(ext);
  } else {
    const emu::CoreLocation loc = locator_.locate(*man);
    const QString label = coreLabel(*man, loc);
    coreOk = loc.found();
    coreText = coreOk ? tr("%1 · bereit").arg(label) : tr("%1 · fehlt").arg(label);
    if (!coreOk) {
      coreHint = tr("Nicht gefunden. Setze %1 oder lege die Bibliothek nach %2.")
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
      fwText = fwOk ? tr("vorhanden") : tr("fehlt");
      if (!fwOk) {
        fwHint = tr("Firmware fehlt: %1 in %2").arg(missing.join(QStringLiteral(", ")), QDir::toNativeSeparators(systemDir()));
      }
    }
  }
  m.insert(QStringLiteral("coreText"), coreText);
  m.insert(QStringLiteral("coreTone"), coreOk ? QStringLiteral("ok") : QStringLiteral("error"));
  m.insert(QStringLiteral("coreHint"), coreHint);
  m.insert(QStringLiteral("firmwareText"), fwText);
  m.insert(QStringLiteral("firmwareTone"), fwOk ? QStringLiteral("neutral") : QStringLiteral("error"));
  m.insert(QStringLiteral("firmwareHint"), fwHint);

  // Start-Checkliste
  const bool busy = phase_ != PlayPhase::None;
  const bool romReady = kind == QLatin1String("ready");
  QVariantList list;
  list.append(checkItem(tr("Spieldaten vom Hub"), QStringLiteral("done")));
  list.append(checkItem(romReady ? tr("ROM aus Cache geprüft") : tr("ROM laden und prüfen"),
                        romReady ? QStringLiteral("done") : (phase_ == PlayPhase::Rom ? QStringLiteral("active") : QStringLiteral("pending")),
                        romReady ? QString() : romText));
  list.append(checkItem(tr("Core bereit"), coreOk ? QStringLiteral("done") : QStringLiteral("error"),
                        coreOk ? QString() : coreText));
  list.append(checkItem(tr("Emulator startet"), phase_ == PlayPhase::Launching ? QStringLiteral("active") : QStringLiteral("pending")));
  m.insert(QStringLiteral("checklist"), list);

  QString error = startError_;
  m.insert(QStringLiteral("error"), error);
  m.insert(QStringLiteral("busy"), busy);
  m.insert(QStringLiteral("canPlay"), !busy && man != nullptr && coreOk && fwOk && kind != QLatin1String("validating") &&
                                          kind != QLatin1String("downloading") && kind != QLatin1String("unknown"));
  QString label = tr("Spielen");
  if (busy) {
    label = phase_ == PlayPhase::Launching ? tr("Starte…") : tr("Lädt…");
  } else if (kind == QLatin1String("download")) {
    label = tr("Herunterladen und spielen");
  } else if (kind == QLatin1String("mismatch") || kind == QLatin1String("failed")) {
    label = tr("Neu laden und spielen");
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
    startError_ = tr("Für dieses Spiel gibt es kein System-Manifest.");
    emit selectedGameChanged();
    return;
  }
  const emu::CoreLocation loc = locator_.locate(*man);
  if (!loc.found()) {
    emit selectedGameChanged();  // Detailspalte zeigt den Pfadhinweis
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
  const QString saveDir = saveDirForCurrentHub();
  if (man == nullptr || saveDir.isEmpty()) {
    phase_ = PlayPhase::None;
    startError_ = tr("Spielstart nicht möglich (System oder Hub-Verzeichnis unbekannt).");
    emit selectedGameChanged();
    return;
  }
  const emu::CoreLocation loc = locator_.locate(*man);
  GameSession::LaunchConfig cfg;
  cfg.title = game.title;
  cfg.corePath = loc.path;
  cfg.gamePath = romPath;
  cfg.systemDir = systemDir();
  cfg.saveDir = saveDir;
  cfg.coreOptions = man->coreOptions;
  cfg.display = man->display;
  phase_ = PlayPhase::Launching;
  emit selectedGameChanged();
  session_.start(cfg);
}

void PlayerController::quitGame() {
  session_.stop();
  gameActive_ = false;
  phase_ = PlayPhase::None;
  updateScreen();
  emit selectedGameChanged();
}

}  // namespace framebeam::ui
