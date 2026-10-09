#include "savehistorycontroller.h"

#include <QClipboard>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QLocale>
#include <QPointer>
#include <QUrl>
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
  const QString active = slot();
  add(active);
  QStringList rest = names.mid(1);
  rest.sort();
  names = QStringList{QStringLiteral("default")} + rest;
  QVariantList out;
  for (const QString& n : names) {
    const int count = n == active ? versionCount() : slotVersionCounts_.value(n, -1);
    out.append(QVariantMap{{QStringLiteral("value"), n},
                           {QStringLiteral("label"), n == QLatin1String("default") ? tr("Main") : n},
                           {QStringLiteral("count"), count}});
  }
  return out;
}

QString SaveHistoryController::slotLabel() const { return slot() == QLatin1String("default") ? tr("Main") : slot(); }

QString SaveHistoryController::lastUpdated() const { return lastRefresh_.isValid() ? lastRefresh_.toString(QStringLiteral("HH:mm")) : QString(); }

QString SaveHistoryController::lastSynced() const { return lastRefresh_.isValid() ? formatWhen(lastRefresh_) : QString(); }

bool SaveHistoryController::canDeleteSnapshot() const { return available() && online() && env_.saves->hubSupportsSavesV3() && !busy_; }

QVariantMap SaveHistoryController::currentMap(const SaveCheckpoint& c) const {
  QString localPath;
  if (!gameId_.isEmpty() && env_.saves->available()) {
    const QString dir = env_.saves->slotDirFor(gameId_, slot());
    const QString name = env_.localFileName ? env_.localFileName(gameId_) : QString();
    localPath = name.isEmpty() ? dir : QDir(dir).filePath(name);
  }
  const QString sha = c.sha256;
  return QVariantMap{{QStringLiteral("revision"), c.revision},
                     {QStringLiteral("revisionText"), QStringLiteral("r%1").arg(c.revision)},
                     {QStringLiteral("device"), c.deviceName},
                     {QStringLiteral("when"), formatWhen(c.createdAt)},
                     {QStringLiteral("reason"), c.reason},
                     {QStringLiteral("reasonText"), reasonText(c.reason)},
                     {QStringLiteral("sha256"), sha},
                     {QStringLiteral("shaShort"), sha.size() > 18 ? sha.left(8) + QStringLiteral("…") + sha.right(8) : sha},
                     {QStringLiteral("size"), c.size},
                     {QStringLiteral("sizeText"), QLocale().formattedDataSize(c.size)},
                     {QStringLiteral("localPath"), localPath}};
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
    uploadRequest_.clear();
  }
  emit changed();
}

bool SaveHistoryController::canSnapshot() const {
  return available() && !busy_ && (currentRevision_ > 0 || env_.saves->sessionActive());
}

bool SaveHistoryController::canUploadFile() const {
  return available() && env_.saves->hubSupportsSavesV4() && !busy_;
}

QStringList SaveHistoryController::saveFileFilters() {
  return {tr("All files (*)"), tr("Save files (*.sav *.srm *.dsv)")};
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
  uploadRequest_.clear();
  deleteRequest_.clear();
  uploadFailure_.clear();
  current_.clear();
  slotVersionCounts_.clear();
  lastRefresh_ = QDateTime();
  offline_ = false;
  currentRevision_ = 0;
  message_.clear();
  messageIsError_ = false;
  if (!env_.saves->sessionActive()) {
    notice_.clear();
  }
  recomputeBlock();
  refresh();
}

void SaveHistoryController::fetchOtherSlotCounts(const QString& game, quint64 gen, const QStringList& names) {
  QPointer<SaveHistoryController> self(this);
  for (const QString& n : names) {
    if (n == slot()) {
      continue;
    }
    env_.saves->api()->listHistory(game, n, [self, gen, n](const SaveApiResult& r) {
      if (!self || gen != self->gen_ || !r.ok()) {
        return;
      }
      self->slotVersionCounts_.insert(n, static_cast<int>(r.history.size()) + 1);  // + the current checkpoint
      emit self->changed();
    });
  }
}

void SaveHistoryController::refresh() {
  const quint64 gen = ++gen_;
  pending_ = 0;
  if (gameId_.isEmpty()) {
    history_.clear();
    remoteSlots_.clear();
    current_.clear();
    currentRevision_ = 0;
    emit changed();
    return;
  }
  if (!slotsAvailable()) {
    // Hub not reachable (or no saves): the cached state of this game stays visible, read-only.
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
        if (!self->offline_) {
          self->lastRefresh_ = QDateTime::currentDateTime();
        }
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
      if (self->env_.saves->hubSupportsSavesV2()) {
        self->fetchOtherSlotCounts(game, gen, names);
      }
    }
    step();
  });
  if (!v2) {
    history_.clear();
    current_.clear();
    currentRevision_ = 0;
    emit changed();
    return;
  }
  api->getSlot(game, slotName, [self, gen, step](const SaveApiResult& r) {
    if (!self || gen != self->gen_) {
      return;
    }
    if (r.kind == SaveApiResult::Kind::Offline) {
      self->offline_ = true;
      self->currentRevision_ = 0;  // nothing can be changed without the Hub; the cached state stays visible
    } else {
      self->offline_ = false;
      self->currentRevision_ = r.ok() ? r.slot->current.revision : 0;
      self->current_ = r.ok() ? self->currentMap(r.slot->current) : QVariantMap();
    }
    step();
  });
  api->listHistory(game, slotName, [self, gen, step](const SaveApiResult& r) {
    if (!self || gen != self->gen_) {
      return;
    }
    if (r.kind == SaveApiResult::Kind::Offline) {
      self->offline_ = true;
    } else {
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
                                 {QStringLiteral("when"), formatWhen(v.createdAt)},
                                 {QStringLiteral("isSnapshot"), v.reason == QLatin1String("manual_snapshot")}});
        }
      }
      self->history_ = out;
    }
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
  current_.clear();
  deleteRequest_.clear();
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
      restoreRequest_.insert(QStringLiteral("currentText"), current_.value(QStringLiteral("revisionText")).toString());
      deleteRequest_.clear();
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

void SaveHistoryController::requestDelete(int version) {
  if (!canDeleteSnapshot()) {
    return;
  }
  for (const QVariant& v : std::as_const(history_)) {
    const QVariantMap m = v.toMap();
    if (m.value(QStringLiteral("version")).toInt() == version && m.value(QStringLiteral("isSnapshot")).toBool()) {
      deleteRequest_ = m;
      deleteRequest_.insert(QStringLiteral("slot"), slot());
      restoreRequest_.clear();
      emit changed();
      return;
    }
  }
}

void SaveHistoryController::cancelDelete() {
  if (!deleteRequest_.isEmpty()) {
    deleteRequest_.clear();
    emit changed();
  }
}

void SaveHistoryController::confirmDelete() {
  if (deleteRequest_.isEmpty()) {
    return;
  }
  const int version = deleteRequest_.value(QStringLiteral("version")).toInt();
  deleteRequest_.clear();
  if (!canDeleteSnapshot()) {
    emit changed();
    return;
  }
  busy_ = true;
  message_.clear();
  emit changed();
  QPointer<SaveHistoryController> self(this);
  env_.saves->api()->deleteSnapshot(gameId_, slot(), version, [self, version](const SaveApiResult& r) {
    if (!self) {
      return;
    }
    self->busy_ = false;
    using K = SaveApiResult::Kind;
    if (r.ok()) {
      self->setMessage(tr("Snapshot v%1 deleted.").arg(version), false);
    } else if (r.kind == K::NotFound) {
      self->setMessage(tr("Snapshot v%1 was already gone. The list was refreshed.").arg(version), true);
    } else if (r.errorCode == QLatin1String("save_not_snapshot")) {
      self->setMessage(tr("Only manual snapshots can be deleted. Nothing was changed."), true);
    } else if (r.kind == K::Offline) {
      self->setMessage(tr("Hub not reachable. Nothing was deleted."), true);
    } else {
      self->setMessage(tr("The Hub refused to delete the snapshot. Nothing was changed."), true);
    }
    self->refresh();
  });
}

void SaveHistoryController::retryUpload() {
  const QString path = uploadFailure_.value(QStringLiteral("path")).toString();
  if (!path.isEmpty()) {
    requestUploadFile(path);
  }
}

void SaveHistoryController::dismissUploadFailure() {
  if (!uploadFailure_.isEmpty()) {
    uploadFailure_.clear();
    emit changed();
  }
}

void SaveHistoryController::copyText(const QString& text) {
  if (QClipboard* cb = QGuiApplication::clipboard()) {
    cb->setText(text);
  }
}

void SaveHistoryController::openSaveFolder() {
  const QString path = current_.value(QStringLiteral("localPath")).toString();
  if (path.isEmpty()) {
    return;
  }
  const QFileInfo info(path);
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
}

void SaveHistoryController::requestUploadFile(const QString& source) {
  if (!canUploadFile()) {
    return;
  }
  recomputeBlock();
  if (!blockReason_.isEmpty()) {
    setMessage(blockReason_, true);
    return;
  }
  const QString path = source.startsWith(QLatin1String("file:")) ? QUrl(source).toLocalFile() : source;
  const QFileInfo info(path);
  if (!info.isFile()) {
    setMessage(tr("The file could not be read."), true);
    return;
  }
  if (info.size() == 0) {
    setMessage(tr("The file is empty. Nothing was uploaded."), true);
    return;
  }
  if (info.size() > kMaxSaveBytes) {
    setMessage(tr("The file is larger than %1 MiB. Nothing was uploaded.").arg(kMaxSaveBytes / (1024 * 1024)), true);
    return;
  }
  message_.clear();
  uploadFailure_.clear();
  uploadRequest_ = QVariantMap{{QStringLiteral("path"), info.absoluteFilePath()},
                               {QStringLiteral("fileName"), info.fileName()},
                               {QStringLiteral("size"), info.size()},
                               {QStringLiteral("sizeText"), QLocale().formattedDataSize(info.size())},
                               {QStringLiteral("slot"), slot()},
                               {QStringLiteral("gameTitle"), env_.gameTitle ? env_.gameTitle(gameId_) : QString()},
                               {QStringLiteral("currentText"), current_.value(QStringLiteral("revisionText")).toString()},
                               {QStringLiteral("nextText"), QStringLiteral("r%1").arg(currentRevision_ + 1)}};
  emit changed();
}

void SaveHistoryController::cancelUploadFile() {
  if (!uploadRequest_.isEmpty()) {
    uploadRequest_.clear();
    emit changed();
  }
}

void SaveHistoryController::confirmUploadFile() {
  if (uploadRequest_.isEmpty()) {
    return;
  }
  const QString path = uploadRequest_.value(QStringLiteral("path")).toString();
  const QString fileName = uploadRequest_.value(QStringLiteral("fileName")).toString();
  uploadRequest_.clear();
  if (busy_ || !canUploadFile()) {
    emit changed();
    return;
  }
  recomputeBlock();
  if (!blockReason_.isEmpty()) {
    setMessage(blockReason_, true);
    return;
  }
  busy_ = true;
  uploading_ = true;
  message_.clear();
  emit changed();
  const QString game = gameId_;
  const QString slotName = slot();
  QPointer<SaveHistoryController> self(this);
  env_.saves->uploadSaveFile(game, slotName, path, currentRevision_, env_.localFileName ? env_.localFileName(game) : QString(),
                             [self, fileName, path](const SaveSync::RestoreResult& r) {
                               if (!self) {
                                 return;
                               }
                               self->busy_ = false;
                               self->uploading_ = false;
                               using O = SaveRestoreResult::Outcome;
                               if (!r.ok()) {
                                 self->uploadFailure_ = QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("fileName"), fileName}};
                               }
                               if (r.ok()) {
                                 self->setMessage(r.message.isEmpty() ? tr("\"%1\" uploaded. It is now the current save.").arg(fileName) : r.message,
                                                  false);
                               } else if (r.kind == O::Stale) {
                                 self->setMessage(tr("The save changed on the Hub in the meantime. The list was refreshed; try again."), true);
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
  if (reason == QLatin1String("before_upload")) return tr("Before upload");
  if (reason == QLatin1String("upload")) return tr("Uploaded");
  if (reason == QLatin1String("checkpoint")) return tr("Auto checkpoint");
  if (reason == QLatin1String("final")) return tr("Game closed");
  if (reason == QLatin1String("final_session_end")) return tr("Session end");
  if (reason == QLatin1String("restore")) return tr("Restored");
  return reason;
}

QString SaveHistoryController::formatWhen(const QString& iso) {
  const QDateTime when = QDateTime::fromString(iso, Qt::ISODate);
  return when.isValid() ? formatWhen(when) : tr("unknown time");
}

// "today 18:42", "yesterday 22:30", "03.10. 21:12" (this year), "03.10.2025 21:12" (older): as drawn in 3c-3.
QString SaveHistoryController::formatWhen(const QDateTime& when) {
  const QDateTime local = when.toLocalTime();
  const QString time = local.toString(QStringLiteral("HH:mm"));
  const QDate today = QDate::currentDate();
  if (local.date() == today) {
    return tr("today %1").arg(time);
  }
  if (local.date() == today.addDays(-1)) {
    return tr("yesterday %1").arg(time);
  }
  return local.date().year() == today.year() ? local.toString(QStringLiteral("dd.MM. HH:mm")) : local.toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

}  // namespace framebeam::ui
