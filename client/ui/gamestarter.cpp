#include "gamestarter.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <algorithm>

#include "core_options.h"
#include "firmware_materializer.h"
#include "libretro_backend.h"
#include "semver.h"
#include "version.h"

namespace framebeam::ui {

GameStarter::GameStarter(const Deps& d, QObject* parent)
    : QObject(parent),
      conn_(d.conn),
      profiles_(d.profiles),
      saves_(d.saves),
      coreProv_(d.coreProv),
      provisioner_(d.provisioner),
      downloader_(d.downloader),
      library_(d.library),
      emulation_(d.emulation),
      catalog_(d.catalog),
      model_(d.model),
      session_(d.session),
      selectedId_(d.selectedId),
      phase_(d.phase),
      launchGame_(d.launchGame),
      launchRom_(d.launchRom),
      saveReady_(d.saveReady),
      saveNoteStart_(d.saveNoteStart),
      startError_(d.startError),
      pendingSha_(d.pendingSha),
      fwOptions_(d.fwOptions),
      shareOnStart_(d.shareOnStart),
      fwProblems_(d.fwProblems),
      coreProblems_(d.coreProblems),
      coreList_(d.coreList),
      handshake_(d.handshake),
      conflict_(d.conflict),
      probeCoreVersions_(d.probeCoreVersions) {}

void GameStarter::startSelected(bool share) {
  shareOnStart_ = false;
  const auto game = model_.game(selectedId_);
  if (!game || phase_ != PlayPhase::None || session_.isActive()) {
    return;
  }
  startError_.clear();
  const emu::SystemManifest* man = catalog_->manifestFor(*game);
  if (man == nullptr) {
    startError_ = tr("There is no system manifest for this game.");
    emit selectedGameChanged();
    return;
  }
  emu::CoreLocation loc;
  const bool usable = catalog_->coreUsable(*man, &loc);
  QString hubVersion;
  const bool offered = catalog_->hubOffersCore(*man, &hubVersion);
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

void GameStarter::onCoreFinished(const CoreResult& result) {
  // D15: whatever happens next, Library tiles, detail pane and Emulation page see the new core state at once.
  const auto refresh = qScopeGuard([this]() { refreshCoreState(); });
  if (phase_ != PlayPhase::Core) {
    return;
  }
  const auto game = model_.game(selectedId_);
  const emu::SystemManifest* man = game ? catalog_->manifestFor(*game) : nullptr;
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
    if (!known && probeCoreVersions_) {
      coreList_.removeIf([&](const CoreInfo& c) { return c.id == man->coreId; });
      catalog_->probeCores();
      handshake_.cores = coreList_;
      conn_->setHandshakeInfo(handshake_);
    }
    continueStartAfterCore(*game, *man);
    return;
  }
  coreProblems_.insert(man->coreId, result.problem);
  emu::CoreLocation loc;
  QString blockedText;
  if (result.problem != QLatin1String("untrusted") && catalog_->coreUsable(*man, &loc)) {
    // Another version of the core is cached (or env/legacy): the game still starts with it, unless the cached core's
    // major version differs from the one the Hub expects (ADR 0017). Explicit/env/app-dir cores are never blocked.
    QString hubVersion;
    catalog_->hubOffersCore(*man, &hubVersion);
    if (loc.source != QLatin1String("cache") ||
        update::coreVersionVerdict(loc.version, hubVersion) != update::CoreVersionVerdict::Block) {
      continueStartAfterCore(*game, *man);
      return;
    }
    blockedText = tr("The cached core %1 %2 is not compatible with version %3 the Hub expects. The game was not started.")
                      .arg(man->coreId, loc.version, hubVersion);
  }
  phase_ = PlayPhase::None;
  shareOnStart_ = false;
  startError_ = blockedText.isEmpty() ? tr("%1. The game was not started.").arg(catalog_->coreProblemText(result.problem)) : blockedText;
  emit selectedGameChanged();
}

void GameStarter::continueStartAfterCore(const GameEntry& gameRef, const emu::SystemManifest& manRef) {
  const GameEntry* game = &gameRef;
  const emu::SystemManifest* man = &manRef;
  phase_ = PlayPhase::None;  // the core step (if any) is over; the firmware/ROM steps set their own phase
  fwOptions_.clear();
  SystemInfo sys;
  if (catalog_->nativeFirmware(*man, &sys)) {
    // Mode native: the files must be available and valid before anything else happens (also blocks the launch).
    const QStringList wanted = catalog_->wantedFirmwareIds(*man);
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

void GameStarter::beginRomPhase(const GameEntry& game) {
  pendingSha_ = game.romSha256;
  phase_ = PlayPhase::Rom;
  emit selectedGameChanged();
  downloader_->ensureRom(game);
}

void GameStarter::onFirmwareFinished(const FirmwareResult& result) {
  if (phase_ != PlayPhase::Firmware) {
    return;
  }
  const auto game = model_.game(selectedId_);
  const emu::SystemManifest* man = game ? catalog_->manifestFor(*game) : nullptr;
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
  if (!emu::materializeFirmware(man->firmware, result.pathsById, catalog_->systemDir(), &written, &err)) {
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

void GameStarter::onRomReady(const QString& sha, const QString& path) {
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

void GameStarter::launch(const GameEntry& game, const QString& romPath) {
  const emu::SystemManifest* man = catalog_->manifestFor(game);
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

void GameStarter::onSaveReady(const QString& gameId, const QString& saveDir, const QString& note) {
  if (phase_ != PlayPhase::Launching || launchGame_.id != gameId) {
    return;
  }
  saveNoteStart_ = note;
  saveReady_ = true;
  if (!conflict_.isEmpty()) {
    conflict_.clear();
    emit saveConflictChanged();
  }
  const emu::SystemManifest* man = catalog_->manifestFor(launchGame_);
  if (man == nullptr) {
    phase_ = PlayPhase::None;
    emit selectedGameChanged();
    return;
  }
  const emu::CoreLocation loc = catalog_->locateCore(*man);
  GameSession::LaunchConfig cfg;
  cfg.title = launchGame_.title;
  cfg.corePath = loc.path;
  cfg.gamePath = launchRom_;
  cfg.systemDir = catalog_->systemDir();
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

QString GameStarter::formatWhen(const QDateTime& when) {
  if (!when.isValid()) {
    return tr("unknown time");
  }
  const QDateTime local = when.toLocalTime();
  return local.date() == QDate::currentDate() ? tr("today, %1").arg(local.toString(QStringLiteral("HH:mm")))
                                              : local.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

void GameStarter::onSaveConflict(const SaveSync::ConflictView& v) {
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

void GameStarter::resolveSaveConflict(const QString& action) {
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

}  // namespace framebeam::ui
