#include "gamedetail.h"

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

namespace {

QVariantMap checkItem(const QString& label, const QString& state, const QString& meta = QString()) {
  return {{QStringLiteral("label"), label}, {QStringLiteral("state"), state}, {QStringLiteral("meta"), meta}};
}

}  // namespace

GameDetail::GameDetail(const Deps& d, QObject* parent)
    : QObject(parent),
      model_(d.model),
      selectedId_(d.selectedId),
      saves_(d.saves),
      cache_(d.cache),
      conn_(d.conn),
      systems_(d.systems),
      fwCache_(d.fwCache),
      catalog_(d.catalog),
      coreVersions_(d.coreVersions),
      fwProblems_(d.fwProblems),
      phase_(d.phase),
      saveReady_(d.saveReady),
      saveNoteStart_(d.saveNoteStart),
      startError_(d.startError) {}

QVariantMap GameDetail::selectedGame() const {
  QVariantMap m;
  const auto game = model_.game(selectedId_);
  if (!game) {
    return m;
  }
  const RomStatus st = model_.status(selectedId_).value_or(RomStatus{});
  const QString kind = LibraryModel::stateKind(st.state);
  const emu::SystemManifest* man = catalog_->manifestFor(*game);
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
    const emu::CoreLocation loc = catalog_->locateCore(*man);
    const QString label = catalog_->coreLabel(*man, loc);
    coreOk = catalog_->coreUsable(*man);
    if (coreOk) {
      coreText = tr("%1 · ready").arg(label);
    } else {
      QString st;
      catalog_->coreStatus(*man, &st, &coreTone, &coreHint);
      coreText = tr("%1 · %2").arg(label, st);
      coreProvisionable = catalog_->hubOffersCore(*man) && phase_ == PlayPhase::None;
    }
    SystemInfo sys;
    if (systems_->supported() && systems_->state() == HubSystems::State::Loading && !systems_->system(man->systemId)) {
      fwText = tr("Checking…");
      fwChecking = true;
    } else if (systems_->supported() && systems_->state() == HubSystems::State::Failed) {
      fwText = tr("Status unavailable");
      fwTone = QStringLiteral("warn");
      fwHint = tr("The firmware status could not be read from the Hub; the built-in firmware is used.");
    } else if (catalog_->nativeFirmware(*man, &sys)) {
      const QStringList wanted = catalog_->wantedFirmwareIds(*man);
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
    if (man != nullptr && man->saveSource == QLatin1String("none")) {
      saveText = tr("Local only");
      saveHint = SaveSync::localOnlyNote();
    } else if (conn_->state() == HubConnection::State::Connected && !saves_->hubSupportsSaves()) {
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
  if (man != nullptr && catalog_->nativeFirmware(*man)) {
    const bool fwDone = phase_ == PlayPhase::Rom || phase_ == PlayPhase::Launching;
    list.append(checkItem(tr("Firmware from Hub verified"),
                          fwDone ? QStringLiteral("done") : (phase_ == PlayPhase::Firmware ? QStringLiteral("active")
                                                             : (fwOk ? QStringLiteral("pending") : QStringLiteral("error"))),
                          QString()));
  }
  list.append(checkItem(romReady ? tr("ROM verified from cache") : tr("Download and verify ROM"),
                        romReady ? QStringLiteral("done") : (phase_ == PlayPhase::Rom ? QStringLiteral("active") : QStringLiteral("pending")),
                        romReady ? QString() : romText));
  list.append(checkItem(man != nullptr && man->saveSource == QLatin1String("none")
                            ? tr("Local save folder prepared") : tr("Save checked with Hub"),
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
    m.insert(QStringLiteral("coreLabelText"), man != nullptr ? catalog_->coreLabel(*man, catalog_->locateCore(*man)) : QString());
    m.insert(QStringLiteral("coreVersionText"), man != nullptr ? coreVersions_.value(man->coreId) : QString());
    m.insert(QStringLiteral("coreExperimental"), man != nullptr && man->experimental);
    m.insert(QStringLiteral("coreNotice"), man != nullptr ? catalog_->coreNoticeFor(man->systemId, game->id) : QString());
  }
  return m;
}

}  // namespace framebeam::ui
