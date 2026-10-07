#include "savehistorycontroller.h"

#include <QDate>
#include <QDateTime>
#include <QPointer>
#include <QVariantMap>
#include <algorithm>

#include "savestore.h"

namespace framebeam::ui {

namespace {
constexpr int kMaxLabelChars = 64;  // Hub limit of SaveSnapshotRequest.label
}

SaveHistoryController::SaveHistoryController(const Env& env, QObject* parent) : QObject(parent), env_(env) {
  // Library state / game phase changes: the restore block reason and the notice depend on them.
  connect(env_.saves, &SaveSync::kindChanged, this, [this](const QString& id, SaveSync::Kind) {
    if (id == gameId_) {
      recomputeBlock();
    }
  });
  connect(env_.saves, &SaveSync::finalSyncFinished, this, [this](const QString& id, bool) {
    if (id == gameId_ || id.isEmpty()) {
      recomputeBlock();
      refresh();  // the final sync may have added a history version
    }
  });
  connect(env_.connection, &HubConnection::stateChanged, this, [this]() { refresh(); });
}

bool SaveHistoryController::slotsAvailable() const { return env_.saves->available(); }

bool SaveHistoryController::available() const { return slotsAvailable() && env_.saves->hubSupportsSavesV2() && !gameId_.isEmpty(); }

QString SaveHistoryController::slot() const {
  return gameId_.isEmpty() ? QStringLiteral("default") : env_.saves->slotFor(gameId_);
}

bool SaveHistoryController::gameRunning() const { return !gameId_.isEmpty() && env_.gameBusy && env_.gameBusy(gameId_); }

QVariantList SaveHistoryController::slotOptions() const {
  QStringList names{QStringLiteral("default")};
  const auto add = [&names](const QString& n) {
    if (SaveStore::isValidSlotName(n) && !names.contains(n)) {
      names.append(n);
    }
  };
  for (const QString& n : remoteSlots_) {
    add(n);
  }
  if (!gameId_.isEmpty() && env_.saves->available()) {
    const QString dir = env_.saves->slotDirFor(gameId_, QStringLiteral("default"));  // the game dir
    for (const QString& n : SaveStore::localSlots(dir)) {
      add(n);
    }
  }
  add(slot());
  QStringList rest = names.mid(1);
  rest.sort();
  names = QStringList{QStringLiteral("default")} + rest;
  QVariantList out;
  for (const QString& n : names) {
    out.append(QVariantMap{{QStringLiteral("value"), n},
                           {QStringLiteral("label"), n == QLatin1String("default") ? tr("default") : n}});
  }
  return out;
}

QString SaveHistoryController::restoreBlockReason() const { return blockReason_; }

void SaveHistoryController::recomputeBlock() {
  QString why;
  if (gameId_.isEmpty()) {
    why.clear();
  } else if (gameRunning()) {
    why = tr("This game is running on this Player. Quit it to restore a version.");
  } else if (env_.saves->available()) {
    why = env_.saves->pendingReason(gameId_, slot());
  }
  lastBusy_ = gameRunning();
  blockReason_ = why;
  if (!why.isEmpty()) {
    restoreRequest_.clear();
  }
  emit changed();
}

bool SaveHistoryController::canSnapshot() const {
  return available() && !busy_ && (currentRevision_ > 0 || env_.saves->sessionActive());
}

QString SaveHistoryController::notice() const { return env_.saves->sessionActive() ? notice_ : QString(); }

void SaveHistoryController::onGameStateChanged() {
  const bool busyNow = gameRunning();
  if (busyNow != lastBusy_) {
    if (!env_.saves->sessionActive()) {
      notice_.clear();
    }
    message_.clear();
    recomputeBlock();
  }
}

void SaveHistoryController::setGame(const QString& gameId) {
  if (gameId == gameId_) {
    return;
  }
  gameId_ = gameId;
  history_.clear();
  remoteSlots_.clear();
  restoreRequest_.clear();
  currentRevision_ = 0;
  message_.clear();
  messageIsError_ = false;
  if (!env_.saves->sessionActive()) {
    notice_.clear();
  }
  recomputeBlock();
  refresh();
}

void SaveHistoryController::refresh() {
  const quint64 gen = ++gen_;
  pending_ = 0;
  if (gameId_.isEmpty() || !slotsAvailable()) {
    history_.clear();
    remoteSlots_.clear();
    currentRevision_ = 0;
    emit changed();
    return;
  }
  const QString game = gameId_;
  const QString slotName = slot();
  const bool v2 = env_.saves->hubSupportsSavesV2();
  pending_ = v2 ? 3 : 1;
  QPointer<SaveHistoryController> self(this);
  const auto step = [self, gen]() {
    if (self && gen == self->gen_ && self->pending_ > 0) {
      --self->pending_;
      if (self->pending_ == 0) {
        self->recomputeBlock();
      } else {
        emit self->changed();
      }
    }
  };
  SaveApi* api = env_.saves->api();
  api->listSlots([self, gen, game, step](const SaveApiResult& r) {
    if (!self || gen != self->gen_) {
      return;
    }
    if (r.ok()) {
      QStringList names;
      for (const SaveSlotInfo& s : r.slotList) {
        if (s.gameId == game) {
          names.append(s.slot);
        }
      }
      self->remoteSlots_ = names;
    }
    step();
  });
  if (!v2) {
    history_.clear();
    currentRevision_ = 0;
    emit changed();
    return;
  }
  api->getSlot(game, slotName, [self, gen, step](const SaveApiResult& r) {
    if (!self || gen != self->gen_) {
      return;
    }
    self->currentRevision_ = r.ok() ? r.slot->current.revision : 0;
    step();
  });
  api->listHistory(game, slotName, [self, gen, step](const SaveApiResult& r) {
    if (!self || gen != self->gen_) {
      return;
    }
    QVariantList out;
    if (r.ok()) {
      for (const SaveHistoryVersion& v : r.history) {
        out.append(QVariantMap{{QStringLiteral("version"), v.version},
                               {QStringLiteral("versionText"), QStringLiteral("v%1").arg(v.version)},
                               {QStringLiteral("revision"), v.revision},
                               {QStringLiteral("label"), v.label},
                               {QStringLiteral("reason"), v.reason},
                               {QStringLiteral("reasonText"), reasonText(v.reason)},
                               {QStringLiteral("device"), v.deviceName},
                               {QStringLiteral("when"), formatWhen(v.createdAt)}});
      }
    } else if (r.kind == SaveApiResult::Kind::Offline) {
      self->setMessage(tr("Hub not reachable. The save history is unavailable."), true);
    }
    self->history_ = out;
    step();
  });
  emit changed();
}

void SaveHistoryController::selectSlot(const QString& slotName) {
  if (gameId_.isEmpty() || gameRunning() || slotName == slot()) {
    return;
  }
  if (!env_.saves->setSlot(gameId_, slotName)) {
    setMessage(tr("The slot could not be selected."), true);
    return;
  }
  history_.clear();
  currentRevision_ = 0;
  message_.clear();
  recomputeBlock();
  refresh();
}

QString SaveHistoryController::validateSlotName(const QString& name) const {
  if (name.isEmpty()) {
    return tr("Enter a slot name.");
  }
  if (name.size() > 32) {
    return tr("Use at most 32 characters.");
  }
  if (!SaveStore::isValidSlotName(name)) {
    return tr("Use lowercase letters, digits, - and _ only.");
  }
  return {};
}

QString SaveHistoryController::createSlot(const QString& name) {
  if (gameRunning()) {
    return tr("Quit the game before changing the slot.");
  }
  const QString err = validateSlotName(name);
  if (!err.isEmpty()) {
    return err;
  }
  selectSlot(name);
  return {};
}

void SaveHistoryController::requestRestore(int version) {
  if (busy_ || !available()) {
    return;
  }
  recomputeBlock();
  if (!blockReason_.isEmpty()) {
    setMessage(blockReason_, true);
    return;
  }
  for (const QVariant& v : std::as_const(history_)) {
    const QVariantMap m = v.toMap();
    if (m.value(QStringLiteral("version")).toInt() == version) {
      restoreRequest_ = m;
      restoreRequest_.insert(QStringLiteral("slot"), slot());
      emit changed();
      return;
    }
  }
}

void SaveHistoryController::confirmRestore() {
  if (restoreRequest_.isEmpty()) {
    return;
  }
  const int version = restoreRequest_.value(QStringLiteral("version")).toInt();
  restoreRequest_.clear();
  emit changed();
  restore(version);
}

void SaveHistoryController::cancelRestore() {
  if (!restoreRequest_.isEmpty()) {
    restoreRequest_.clear();
    emit changed();
  }
}

void SaveHistoryController::restore(int version) {
  if (busy_ || !available()) {
    return;
  }
  recomputeBlock();
  if (!blockReason_.isEmpty()) {
    setMessage(blockReason_, true);
    return;
  }
  busy_ = true;
  message_.clear();
  emit changed();
  const QString game = gameId_;
  const QString slotName = slot();
  QPointer<SaveHistoryController> self(this);
  env_.saves->restoreVersion(game, slotName, version, currentRevision_, env_.localFileName ? env_.localFileName(game) : QString(),
                             [self, version](const SaveSync::RestoreResult& r) {
                               if (!self) {
                                 return;
                               }
                               self->busy_ = false;
                               using O = SaveRestoreResult::Outcome;
                               if (r.ok()) {
                                 self->setMessage(r.message.isEmpty() ? tr("Version v%1 restored. It is now the current save.").arg(version)
                                                                      : r.message,
                                                  false);
                               } else if (r.kind == O::Stale) {
                                 self->setMessage(tr("The save changed on the Hub in the meantime. The list was refreshed; check it and try again."),
                                                  true);
                               } else {
                                 self->setMessage(r.message, true);
                               }
                               self->recomputeBlock();
                               self->refresh();
                             });
}

void SaveHistoryController::createSnapshot(const QString& label) {
  if (busy_ || !canSnapshot()) {
    return;
  }
  if (label.trimmed().size() > kMaxLabelChars) {
    setMessage(tr("Use at most %1 characters for the label.").arg(kMaxLabelChars), true);
    return;
  }
  busy_ = true;
  message_.clear();
  emit changed();
  QPointer<SaveHistoryController> self(this);
  const auto done = [self](const SaveSnapshotResult& r) {
    if (!self) {
      return;
    }
    self->busy_ = false;
    if (r.ok()) {
      self->setMessage(r.version.label.isEmpty() ? tr("Snapshot v%1 created.").arg(r.version.version)
                                                 : tr("Snapshot v%1 created: %2").arg(r.version.version).arg(r.version.label),
                       false);
    } else {
      self->setMessage(r.message, true);
    }
    self->refresh();
  };
  if (env_.saves->isRunning(gameId_, slot())) {
    env_.saves->snapshotActive(label.trimmed(), done);
  } else {
    env_.saves->createSnapshot(gameId_, slot(), label.trimmed(), done);
  }
}

void SaveHistoryController::createSnapshotInGame(const QString& label) {
  if (busy_ || !env_.saves->sessionActive() || !env_.saves->hubSupportsSavesV2()) {
    return;
  }
  if (label.trimmed().size() > kMaxLabelChars) {
    setMessage(tr("Use at most %1 characters for the label.").arg(kMaxLabelChars), true);
    return;
  }
  busy_ = true;
  message_.clear();
  emit changed();
  QPointer<SaveHistoryController> self(this);
  env_.saves->snapshotActive(label.trimmed(), [self](const SaveSnapshotResult& r) {
    if (!self) {
      return;
    }
    self->busy_ = false;
    if (r.ok()) {
      self->setMessage(r.version.label.isEmpty() ? tr("Snapshot v%1 created.").arg(r.version.version)
                                                 : tr("Snapshot v%1 created: %2").arg(r.version.version).arg(r.version.label),
                       false);
    } else {
      self->setMessage(r.message, true);
    }
    self->refresh();
  });
}

void SaveHistoryController::onSaveUpdated(const SaveUpdate& u, bool runningHere) {
  if (u.gameId == gameId_) {
    refresh();
  }
  if (runningHere) {
    notice_ = tr("The save for this game was changed on %1. Your next upload will become a conflict that you can resolve at the next start.")
                  .arg(u.deviceName.isEmpty() ? (u.deviceId == QLatin1String("00000000-0000-0000-0000-000000000000") ? tr("Hub web interface") : tr("another device")) : u.deviceName);
    emit changed();
  }
}

void SaveHistoryController::dismissMessage() {
  message_.clear();
  emit changed();
}

void SaveHistoryController::dismissNotice() {
  notice_.clear();
  emit changed();
}

void SaveHistoryController::setMessage(const QString& text, bool isError) {
  message_ = text;
  messageIsError_ = isError;
  emit changed();
}

QString SaveHistoryController::reasonText(const QString& reason) {
  if (reason == QLatin1String("session_end")) return tr("Session end");
  if (reason == QLatin1String("device_change")) return tr("Device change");
  if (reason == QLatin1String("before_conflict_resolution")) return tr("Before conflict resolution");
  if (reason == QLatin1String("conflict_upload")) return tr("Conflict upload");
  if (reason == QLatin1String("manual_snapshot")) return tr("Manual snapshot");
  if (reason == QLatin1String("before_restore")) return tr("Before restore");
  return reason;
}

QString SaveHistoryController::formatWhen(const QString& iso) {
  const QDateTime when = QDateTime::fromString(iso, Qt::ISODate);
  if (!when.isValid()) {
    return tr("unknown time");
  }
  const QDateTime local = when.toLocalTime();
  return local.date() == QDate::currentDate() ? tr("today, %1").arg(local.toString(QStringLiteral("HH:mm")))
                                              : local.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

}  // namespace framebeam::ui
