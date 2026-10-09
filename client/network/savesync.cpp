#include "savesync.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <algorithm>
#include <memory>

namespace framebeam {

Q_LOGGING_CATEGORY(lcSaveSync, "framebeam.savesync")

SaveSync::SaveSync(HubConnection* connection, ProfileStore* profiles, QObject* parent)
    : QObject(parent), conn_(connection), profiles_(profiles), api_(connection, this) {
  qRegisterMetaType<framebeam::SaveSync::ConflictView>();
  qRegisterMetaType<framebeam::SaveSync::Kind>();
  pollTimer_.setInterval(timing_.pollMs);
  connect(&pollTimer_, &QTimer::timeout, this, &SaveSync::poll);
  retryTimer_.setSingleShot(true);
  connect(&retryTimer_, &QTimer::timeout, this, &SaveSync::retryPending);
  connect(conn_, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s == HubConnection::State::Connected) {
      retryBackoffMs_ = 0;
      retryPending();
    } else {
      retryTimer_.stop();
    }
  });
}

QString SaveSync::kindName(Kind k) {
  switch (k) {
    case Kind::Synced: return QStringLiteral("synced");
    case Kind::Pending: return QStringLiteral("pending");
    case Kind::Conflict: return QStringLiteral("conflict");
    default: return QStringLiteral("none");
  }
}

void SaveSync::setTiming(const Timing& t) {
  timing_ = t;
  pollTimer_.setInterval(std::max(5, t.pollMs));
}

bool SaveSync::hubSupportsSaves() const {
  return conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QString::fromLatin1(kSavesFeature));
}

bool SaveSync::available() const {
  return hubSupportsSaves() && SaveStore::isSafeId(conn_->hubUserId());
}

QString SaveSync::note() const {
  if (conn_->state() != HubConnection::State::Connected) {
    return {};
  }
  if (!hubSupportsSaves()) {
    return unsupportedNote();
  }
  return available() ? QString() : tr("Save sync is off: Hub user unknown");
}

bool SaveSync::hubSupportsSavesV2() const {
  return hubSupportsSaves() && conn_->hubHasFeature(QString::fromLatin1(kSavesV2Feature));
}

bool SaveSync::hubSupportsSavesV3() const {
  return hubSupportsSavesV2() && conn_->hubHasFeature(QString::fromLatin1(kSavesV3Feature));
}

bool SaveSync::hubSupportsSavesV4() const {
  return hubSupportsSavesV2() && conn_->hubHasFeature(QString::fromLatin1(kSavesV4Feature));
}

QString SaveSync::slotFor(const QString& gameId) const {
  const QString hubId = conn_->hubId();
  if (settings_ != nullptr) {
    return settings_->saveSlot(hubId, gameId);
  }
  return memorySlots_.value(hubId + QLatin1Char('/') + gameId, QStringLiteral("default"));
}

bool SaveSync::setSlot(const QString& gameId, const QString& slot) {
  if (!SaveStore::isValidSlotName(slot) || gameId.isEmpty()) {
    return false;
  }
  const QString hubId = conn_->hubId();
  if (settings_ != nullptr) {
    if (!settings_->setSaveSlot(hubId, gameId, slot)) {
      return false;
    }
  } else {
    memorySlots_.insert(hubId + QLatin1Char('/') + gameId, slot);
  }
  refreshKinds({gameId});
  return true;
}

QString SaveSync::slotDirFor(const QString& gameId, const QString& slot) const {
  return SaveStore::slotDir(*profiles_, conn_->hubId(), conn_->hubUserId(), gameId, slot);
}

bool SaveSync::sameHub(const QString& hubId) const {
  return conn_->state() == HubConnection::State::Connected && !hubId.isEmpty() && conn_->hubId() == hubId;
}

QString SaveSync::noteForOffline() const { return tr("Hub not reachable. Playing with the local save; sync is pending."); }

void SaveSync::setKind(const QString& gameId, const SyncState& st, bool hasFile) {
  Kind k = Kind::None;
  if (!st.conflictId.isEmpty()) {
    k = Kind::Conflict;
  } else if (st.pending) {
    k = Kind::Pending;
  } else if (!st.lastSyncedSha256.isEmpty() && hasFile) {
    k = Kind::Synced;
  }
  if (kinds_.value(gameId, Kind::None) != k) {
    kinds_.insert(gameId, k);
    emit kindChanged(gameId, k);
  }
}

void SaveSync::persist() {
  if (a_.dir.isEmpty()) {
    return;
  }
  if (!SaveStore::saveState(a_.dir, a_.st)) {
    qCWarning(lcSaveSync) << "sync.json could not be written";
  }
  setKind(a_.gameId, a_.st, QFileInfo(a_.file).isFile());
}

void SaveSync::refreshKinds(const QStringList& gameIds) {
  const bool on = available();
  for (const QString& id : gameIds) {
    Kind k = Kind::None;
    if (on) {
      const QString dir = slotDirFor(id, slotFor(id));
      if (!dir.isEmpty() && QFileInfo::exists(SaveStore::stateFilePath(dir))) {
        const SyncState st = SaveStore::loadState(dir);
        const bool hasFile = !SaveStore::findSaveFile(dir, QString()).isEmpty();
        if (!st.conflictId.isEmpty()) {
          k = Kind::Conflict;
        } else if (st.pending) {
          k = Kind::Pending;
        } else if (!st.lastSyncedSha256.isEmpty() && hasFile) {
          k = Kind::Synced;
        }
      }
    }
    if (kinds_.value(id, Kind::None) != k) {
      kinds_.insert(id, k);
      emit kindChanged(id, k);
    }
  }
}

// ---------------------------------------------------------------- Start sync

void SaveSync::prepareStart(const QString& gameId, const QString& romPath, const QStringList& romBasenames) {
  ++gen_;
  pollTimer_.stop();
  session_ = false;
  dialogOpen_ = false;
  a_ = Active{};
  a_.gameId = gameId;
  a_.romPath = romPath;
  a_.hubId = conn_->hubId();
  a_.userId = conn_->hubUserId();
  a_.slot = slotFor(gameId);
  a_.dir = SaveStore::slotDir(*profiles_, a_.hubId, a_.userId, gameId, a_.slot);
  const quint64 gen = gen_;
  if (a_.dir.isEmpty()) {
    QTimer::singleShot(0, this, [this, gen, gameId]() {
      if (gen == gen_) {
        emit startFailed(gameId, tr("The save directory for this game is unknown."));
      }
    });
    return;
  }
  QDir().mkpath(a_.dir);
  a_.expectedName = SaveStore::expectedSaveName(romPath);
  QStringList warnings;
  a_.file = SaveStore::findSaveFile(a_.dir, a_.expectedName, &warnings);
  for (const QString& w : warnings) {
    qCWarning(lcSaveSync) << w;
  }
  a_.st = SaveStore::loadState(a_.dir);
  a_.st.slot = a_.slot;  // pre-0.4 sync.json files have no slot choice: they belong to "default"
  if (a_.file.isEmpty()) {
    if (a_.slot == QLatin1String("default") &&
        SaveStore::migrateLegacy(SaveStore::legacyDir(*profiles_, a_.hubId), romBasenames, a_.dir, a_.expectedName)) {
      qCInfo(lcSaveSync) << "Legacy save copied into the per-user save directory";
      a_.st = SyncState{};
      a_.st.pending = true;  // counts as an unsynced local change (base 0)
      a_.file = QDir(a_.dir).filePath(a_.expectedName);
      persist();
    } else {
      a_.file = QDir(a_.dir).filePath(a_.expectedName);
    }
  }
  if (!available()) {
    QTimer::singleShot(0, this, [this, gen]() {
      if (gen == gen_) {
        emit startReady(a_.gameId, a_.dir, note());
      }
    });
    return;
  }
  startSync();
}

void SaveSync::startSync() {
  const quint64 gen = ++gen_;
  api_.getSlot(a_.gameId, a_.st.slot, [this, gen](const SaveApiResult& r) { onSlot(gen, r); });
}

void SaveSync::readyToPlay(const QString& note) {
  dialogOpen_ = false;
  setKind(a_.gameId, a_.st, QFileInfo(a_.file).isFile());
  emit startReady(a_.gameId, a_.dir, note);
}

void SaveSync::onSlot(quint64 gen, const SaveApiResult& r) {
  if (gen != gen_) {
    return;
  }
  using K = SaveApiResult::Kind;
  const bool hasFile = QFileInfo(a_.file).isFile();
  switch (r.kind) {
    case K::Ok:
      onHubSlot(gen, *r.slot);
      return;
    case K::NotFound: {
      if (!hasFile) {
        a_.st.baseRevision = 0;
        a_.st.lastSyncedSha256.clear();
        a_.st.pending = false;
        a_.st.conflictId.clear();
        readyToPlay(QString());  // start empty
        return;
      }
      a_.st.baseRevision = 0;
      a_.st.lastSyncedSha256.clear();
      uploadAtStart(gen, SaveStore::sha256OfFile(a_.file));
      return;
    }
    case K::Offline: {
      if (hasFile) {
        const QString l = SaveStore::sha256OfFile(a_.file);
        if (l != a_.st.lastSyncedSha256) {
          a_.st.pending = true;
          persist();
        }
      }
      readyToPlay(noteForOffline());
      return;
    }
    default:
      a_.st.lastError = r.errorCode;
      persist();
      readyToPlay(tr("Save sync is not available (%1). Playing with the local save.").arg(r.errorCode));
      return;
  }
}

void SaveSync::onHubSlot(quint64 gen, const SaveSlotInfo& slot) {
  const int h = slot.current.revision;
  std::optional<SaveConflictInfo> own;
  for (const SaveConflictInfo& c : slot.openConflicts) {
    const bool mine = conn_->deviceId().isEmpty() || c.securedDeviceId == conn_->deviceId();
    if (c.isOpen() && mine && (!own || c.id == a_.st.conflictId)) {
      own = c;
    }
  }
  if (own) {
    showConflict(*own);
    return;
  }
  if (!a_.st.conflictId.isEmpty()) {
    a_.st.conflictId.clear();  // resolved elsewhere (e.g. in the Hub web interface)
    persist();
  }
  const bool hasFile = QFileInfo(a_.file).isFile();
  if (!hasFile) {
    downloadAndApply(gen, false, QString());
    return;
  }
  const QString l = SaveStore::sha256OfFile(a_.file);
  if (l == a_.st.lastSyncedSha256) {
    if (h > a_.st.baseRevision) {
      downloadAndApply(gen, true, QString());
    } else {
      a_.st.pending = false;
      persist();
      readyToPlay(QString());
    }
    return;
  }
  uploadAtStart(gen, l);
}

void SaveSync::downloadAndApply(quint64 gen, bool backupLocal, const QString&) {
  api_.getContent(a_.gameId, a_.st.slot, [this, gen, backupLocal](const SaveApiResult& r) {
    if (gen != gen_) {
      return;
    }
    using K = SaveApiResult::Kind;
    if (r.kind == K::Ok) {
      if (backupLocal) {
        SaveStore::backupFile(a_.file);
      }
      if (!SaveStore::atomicWrite(a_.file, r.content)) {
        a_.st.lastError = QStringLiteral("write_failed");
        persist();
        readyToPlay(tr("The Hub save could not be written locally."));
        return;
      }
      a_.st.baseRevision = r.contentRevision;
      a_.st.lastSyncedSha256 = SaveStore::sha256Of(r.content);
      a_.st.pending = false;
      a_.st.conflictId.clear();
      a_.st.lastError.clear();
      persist();
      readyToPlay(QString());
      return;
    }
    if (r.kind == K::Offline) {
      if (QFileInfo(a_.file).isFile()) {
        a_.st.pending = true;
        persist();
      }
      readyToPlay(noteForOffline());
      return;
    }
    a_.st.lastError = r.errorCode;
    persist();
    readyToPlay(tr("Save sync is not available (%1). Playing with the local save.").arg(r.errorCode));
  });
}

void SaveSync::uploadAtStart(quint64 gen, const QString& sha) {
  QFile f(a_.file);
  if (!f.open(QIODevice::ReadOnly)) {
    readyToPlay(QString());
    return;
  }
  const QByteArray data = f.readAll();
  f.close();
  a_.st.pending = true;  // persisted before the attempt: survives a crash or timeout
  persist();
  api_.putSave(a_.gameId, a_.st.slot, data, a_.st.baseRevision, QStringLiteral("checkpoint"),
               [this, gen, sha](const SaveApiResult& r) {
                 if (gen != gen_) {
                   return;
                 }
                 using K = SaveApiResult::Kind;
                 switch (r.kind) {
                   case K::Ok:
                     a_.st.baseRevision = r.slot->current.revision;
                     a_.st.lastSyncedSha256 = sha;
                     a_.st.pending = false;
                     a_.st.conflictId.clear();
                     a_.st.lastError.clear();
                     persist();
                     emit uploaded(a_.gameId, a_.st.baseRevision);
                     readyToPlay(QString());
                     return;
                   case K::Conflict:
                     showConflict(*r.conflict);
                     return;
                   case K::Offline:
                     readyToPlay(noteForOffline());
                     return;
                   default:
                     a_.st.lastError = r.errorCode;
                     persist();
                     readyToPlay(tr("The save could not be uploaded (%1). Playing with the local save.").arg(r.errorCode));
                     return;
                 }
               });
}

void SaveSync::showConflict(const SaveConflictInfo& c) {
  a_.conflict = c;
  a_.st.conflictId = c.id;
  a_.st.pending = true;
  persist();
  dialogOpen_ = true;
  ConflictView v;
  v.conflict = c;
  v.gameId = a_.gameId;
  v.localDeviceName = profiles_->deviceName();
  const QFileInfo fi(a_.file);
  v.localModified = fi.exists() ? fi.lastModified() : QDateTime();
  v.localSha256 = SaveStore::sha256OfFile(a_.file);
  v.localBaseRevision = c.securedBaseRevision;
  emit startConflict(v);
}

void SaveSync::resolveConflict(Resolution res) {
  if (!dialogOpen_ || !a_.conflict) {
    return;
  }
  const SaveConflictInfo c = *a_.conflict;
  if (res == Resolution::DecideLater) {
    a_.st.conflictId = c.id;
    a_.st.pending = true;
    persist();
    a_.pausedByConflict = true;
    readyToPlay(tr("Conflict open: changes stay on this device until you decide."));
    return;
  }
  const quint64 gen = ++gen_;
  const auto fail = [this](const SaveApiResult& r) {
    using K = SaveApiResult::Kind;
    if (r.kind == K::Stale) {
      dialogOpen_ = false;
      startSync();  // the Hub changed meanwhile: reconcile again, dialog shows the new state
      return;
    }
    emit resolveFailed(r.kind == K::Offline ? tr("Hub not reachable. Nothing was changed.")
                                            : tr("The conflict could not be resolved (%1). Nothing was changed.").arg(r.errorCode));
  };
  if (res == Resolution::UseLocal) {
    api_.resolve(a_.gameId, a_.st.slot, c.id, QStringLiteral("use_local"), c.hubRevision, [this, gen, fail](const SaveApiResult& r) {
      if (gen != gen_) {
        return;
      }
      if (!r.ok()) {
        fail(r);
        return;
      }
      const SaveCheckpoint cur = r.slot->current;
      if (!QFileInfo(a_.file).isFile()) {
        // The local file is gone: the promoted checkpoint is now the Hub's current. Fetch it as the local save
        // instead of starting with an empty directory.
        a_.st.conflictId.clear();
        a_.st.baseRevision = cur.revision;
        a_.st.lastSyncedSha256.clear();
        a_.st.pending = false;
        persist();
        api_.getContent(a_.gameId, a_.st.slot, [this, gen, cur](const SaveApiResult& cr) {
          if (gen != gen_) {
            return;
          }
          if (cr.ok() && SaveStore::sha256Of(cr.content) == cur.sha256 && SaveStore::atomicWrite(a_.file, cr.content)) {
            a_.st.baseRevision = cr.contentRevision;
            a_.st.lastSyncedSha256 = cur.sha256;
            a_.st.pending = false;
            a_.st.lastError.clear();
            persist();
            readyToPlay(QString());
          } else {
            dialogOpen_ = false;
            startSync();  // reconcile again: Hub slot without a local file -> download (or offline note)
          }
        });
        return;
      }
      const QString l = SaveStore::sha256OfFile(a_.file);
      a_.st.baseRevision = cur.revision;
      a_.st.lastSyncedSha256 = cur.sha256;
      a_.st.pending = l != cur.sha256;  // the local file kept changing: still unsynced
      a_.st.conflictId.clear();
      a_.st.lastError.clear();
      persist();
      readyToPlay(QString());
    });
    return;
  }
  // Use Hub version: download and verify first, then resolve against that revision, then replace the local file.
  api_.getContent(a_.gameId, a_.st.slot, [this, gen, fail, c](const SaveApiResult& cr) {
    if (gen != gen_) {
      return;
    }
    if (!cr.ok()) {
      fail(cr);
      return;
    }
    // The local file must be backed up BEFORE anything changes; if that fails nothing is changed.
    if (QFileInfo(a_.file).isFile()) {
      const QString bak = backupHook_ ? backupHook_(a_.file, QStringLiteral("local")) : SaveStore::backupFile(a_.file, QStringLiteral("local"));
      if (bak.isEmpty()) {
        a_.st.lastError = QStringLiteral("backup_failed");
        persist();
        emit resolveFailed(tr("Could not back up the local save; nothing was changed."));
        return;
      }
    }
    const quint64 gen2 = ++gen_;
    api_.resolve(a_.gameId, a_.st.slot, c.id, QStringLiteral("use_hub"), cr.contentRevision,
                 [this, gen2, fail, cr](const SaveApiResult& r) {
                   if (gen2 != gen_) {
                     return;
                   }
                   if (!r.ok()) {
                     fail(r);
                     return;
                   }
                   if (!SaveStore::atomicWrite(a_.file, cr.content)) {
                     a_.st.lastError = QStringLiteral("write_failed");
                     persist();
                     emit resolveFailed(tr("The Hub save could not be written locally."));
                     return;
                   }
                   a_.st.baseRevision = r.slot->current.revision;
                   a_.st.lastSyncedSha256 = SaveStore::sha256Of(cr.content);
                   a_.st.pending = false;
                   a_.st.conflictId.clear();
                   a_.st.lastError.clear();
                   persist();
                   readyToPlay(QString());
                 });
  });
}

// ---------------------------------------------------------------- Session: checkpoints and final sync

void SaveSync::beginSession() {
  if (a_.gameId.isEmpty()) {
    return;
  }
  session_ = true;
  a_.pausedByConflict = !a_.st.conflictId.isEmpty();
  const QString found = SaveStore::findSaveFile(a_.dir, a_.expectedName);
  if (!found.isEmpty()) {
    a_.file = found;
  }
  const QFileInfo fi(a_.file);
  a_.lastMtime = fi.exists() ? fi.lastModified() : QDateTime();
  a_.lastSize = fi.exists() ? fi.size() : -1;
  a_.lastHash = fi.exists() ? SaveStore::sha256OfFile(a_.file) : QString();
  a_.dirty = fi.exists() && a_.st.pending && !a_.pausedByConflict;
  a_.sinceChange.start();
  if (available()) {
    pollTimer_.start();
  }
}

void SaveSync::poll() {
  if (!session_ || a_.gameId.isEmpty()) {
    return;
  }
  if (!QFileInfo::exists(a_.file)) {
    const QString found = SaveStore::findSaveFile(a_.dir, a_.expectedName);
    if (!found.isEmpty()) {
      a_.file = found;
    }
  }
  const QFileInfo fi(a_.file);
  if (fi.exists() && (fi.lastModified() != a_.lastMtime || fi.size() != a_.lastSize)) {
    a_.lastMtime = fi.lastModified();
    a_.lastSize = fi.size();
    const QString h = SaveStore::sha256OfFile(a_.file);
    if (!h.isEmpty() && h != a_.lastHash) {
      a_.lastHash = h;
      if (h != a_.st.lastSyncedSha256) {
        a_.dirty = true;
        a_.sinceChange.restart();
      } else {
        a_.dirty = false;
      }
    }
  }
  pump();
}

void SaveSync::pump() {
  if (!session_ || a_.uploading) {
    return;
  }
  if (a_.finalRequested != 0) {
    const bool end = a_.finalRequested == 2;
    uploadCurrent(end ? QStringLiteral("final_session_end") : QStringLiteral("final"),
                  [this](UploadOutcome o) { finishFinal(o == UploadOutcome::Clean || o == UploadOutcome::Uploaded); });
    return;
  }
  if (!a_.dirty || a_.pausedByConflict || !available() || !sameHub(a_.hubId)) {
    return;
  }
  if (a_.sinceChange.elapsed() < timing_.debounceMs) {
    return;
  }
  if (a_.uploadedOnce && a_.sinceUpload.elapsed() < timing_.minIntervalMs) {
    return;
  }
  if (a_.failedOnce && a_.sinceFail.elapsed() < a_.backoffMs) {
    return;
  }
  uploadCurrent(QStringLiteral("checkpoint"), [](UploadOutcome) {});
}

void SaveSync::uploadCurrent(const QString& reason, std::function<void(UploadOutcome)> done) {
  QFile f(a_.file);
  if (!f.open(QIODevice::ReadOnly)) {
    done(UploadOutcome::Clean);  // no save file (yet)
    return;
  }
  const QByteArray data = f.readAll();
  f.close();
  const QString sha = SaveStore::sha256Of(data);
  a_.lastHash = sha;  // this is what we observed
  if (sha == a_.st.lastSyncedSha256) {
    if (a_.st.pending) {
      a_.st.pending = false;
      persist();
    }
    a_.dirty = false;
    done(UploadOutcome::Clean);
    return;
  }
  if (!a_.st.pending) {
    a_.st.pending = true;  // persisted before the attempt
    persist();
  }
  if (a_.pausedByConflict) {
    done(UploadOutcome::Conflict);
    return;
  }
  if (!available() || !sameHub(a_.hubId)) {
    done(UploadOutcome::Offline);
    return;
  }
  a_.uploading = true;
  const quint64 gen = gen_;
  api_.putSave(a_.gameId, a_.st.slot, data, a_.st.baseRevision, reason, [this, gen, sha, done](const SaveApiResult& r) {
    if (gen != gen_) {
      return;
    }
    a_.uploading = false;
    using K = SaveApiResult::Kind;
    UploadOutcome out = UploadOutcome::Failed;
    if (r.kind == K::Ok) {
      a_.st.baseRevision = r.slot->current.revision;
      a_.st.lastSyncedSha256 = sha;
      a_.st.pending = a_.lastHash != sha && !a_.lastHash.isEmpty();  // changed again meanwhile
      a_.st.lastError.clear();
      a_.dirty = a_.st.pending;
      a_.backoffMs = 0;
      a_.failedOnce = false;
      a_.uploadedOnce = true;
      a_.sinceUpload.restart();
      persist();
      emit uploaded(a_.gameId, a_.st.baseRevision);
      out = UploadOutcome::Uploaded;
    } else if (r.kind == K::Conflict) {
      a_.st.conflictId = r.conflict->id;
      a_.st.pending = true;
      a_.pausedByConflict = true;
      persist();
      out = UploadOutcome::Conflict;
    } else {
      a_.failedOnce = true;
      a_.backoffMs = std::min(timing_.retryMaxMs, std::max(timing_.retryBaseMs, a_.backoffMs * 2));
      a_.sinceFail.restart();
      a_.st.lastError = r.errorCode;
      persist();
      out = r.kind == K::Offline ? UploadOutcome::Offline : UploadOutcome::Failed;
    }
    done(out);
    if (a_.finalRequested != 0) {
      pump();
    }
  });
}

void SaveSync::finalSync(bool sessionEnd) {
  if (!session_ || a_.gameId.isEmpty()) {
    QTimer::singleShot(0, this, [this]() { emit finalSyncFinished(QString(), true); });
    return;
  }
  // Pick up the latest state of the file (the core just flushed it).
  const QString found = SaveStore::findSaveFile(a_.dir, a_.expectedName);
  if (!found.isEmpty()) {
    a_.file = found;
  }
  a_.finalRequested = sessionEnd ? 2 : std::max(a_.finalRequested, 1);
  if (sessionEnd) {
    pollTimer_.stop();
  }
  pump();
}

void SaveSync::finishFinal(bool ok) {
  const bool end = a_.finalRequested == 2;
  const QString id = a_.gameId;
  a_.finalRequested = 0;
  if (end) {
    pollTimer_.stop();
    session_ = false;
    a_ = Active{};
  }
  emit finalSyncFinished(id, ok);
}

bool SaveSync::finalSyncBlocking(bool sessionEnd) {
  if (!session_) {
    return true;
  }
  QEventLoop loop;
  bool done = false;
  bool ok = true;
  const QMetaObject::Connection c = connect(this, &SaveSync::finalSyncFinished, &loop, [&](const QString&, bool o) {
    done = true;
    ok = o;
    loop.quit();
  });
  QTimer timeout;
  timeout.setSingleShot(true);
  connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  timeout.start(timing_.finalTimeoutMs);
  finalSync(sessionEnd);
  if (!done) {
    loop.exec();
  }
  disconnect(c);
  if (!done) {
    // Timeout: the change stays pending (already persisted before the attempt) and is retried later.
    ++gen_;
    a_.finalRequested = 0;
    a_.uploading = false;
    if (sessionEnd) {
      pollTimer_.stop();
      session_ = false;
      a_ = Active{};
    }
    return false;
  }
  return ok;
}

// ---------------------------------------------------------------- Pending retry (only the current Hub)

void SaveSync::scheduleRetry() {
  retryBackoffMs_ = std::min(timing_.retryMaxMs, std::max(timing_.retryBaseMs, retryBackoffMs_ * 2));
  retryTimer_.start(retryBackoffMs_);
}

void SaveSync::retryPending() {
  if (retryRunning_ || !available()) {
    return;
  }
  const QString base = SaveStore::userSavesDir(*profiles_, conn_->hubId(), conn_->hubUserId());
  QList<RetryItem> items;
  const auto consider = [&](const QString& gameId, const QString& slot, const QString& dir) {
    if (session_ && gameId == a_.gameId && slot == a_.slot) {
      return;  // the running game is handled by its own watcher
    }
    const SyncState st = SaveStore::loadState(dir);
    if (st.pending && st.conflictId.isEmpty()) {
      items.append({gameId, slot, dir});
    }
  };
  for (const QString& name : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString dir = QDir(base).filePath(name);
    consider(name, QStringLiteral("default"), dir);
    for (const QString& slot : SaveStore::localSlots(dir)) {
      if (slot != QLatin1String("default")) {
        consider(name, slot, SaveStore::slotDirIn(dir, slot));
      }
    }
  }
  if (items.isEmpty()) {
    retryBackoffMs_ = 0;
    return;
  }
  retryRunning_ = true;
  retryNext(items);
}

void SaveSync::retryNext(QList<RetryItem> items) {
  if (items.isEmpty() || !available()) {
    retryRunning_ = false;
    retryBackoffMs_ = 0;
    return;
  }
  const QString hubId = conn_->hubId();
  const RetryItem item = items.takeFirst();
  const QString dir = item.dir;
  const QString gameId = item.gameId;
  const QString slot = item.slot;
  const bool chosen = slot == slotFor(gameId);  // the library badge shows the chosen slot only
  SyncState st = SaveStore::loadState(dir);
  st.slot = slot;
  const QString file = SaveStore::findSaveFile(dir, QString());
  const QByteArray data = [&]() {
    QFile f(file);
    return (!file.isEmpty() && f.open(QIODevice::ReadOnly)) ? f.readAll() : QByteArray();
  }();
  if (file.isEmpty()) {
    st.pending = false;
    SaveStore::saveState(dir, st);
    if (chosen) {
      setKind(gameId, st, false);
    }
    retryNext(items);
    return;
  }
  const QString sha = SaveStore::sha256Of(data);
  if (sha == st.lastSyncedSha256) {
    st.pending = false;
    SaveStore::saveState(dir, st);
    if (chosen) {
      setKind(gameId, st, true);
    }
    retryNext(items);
    return;
  }
  api_.putSave(gameId, slot, data, st.baseRevision, QStringLiteral("checkpoint"),
               [this, hubId, dir, gameId, chosen, sha, st, items](const SaveApiResult& r) mutable {
                 using K = SaveApiResult::Kind;
                 if (!sameHub(hubId)) {
                   retryRunning_ = false;  // never continue against another Hub
                   return;
                 }
                 if (r.kind == K::Ok) {
                   st.baseRevision = r.slot->current.revision;
                   st.lastSyncedSha256 = sha;
                   st.pending = false;
                   st.lastError.clear();
                   SaveStore::saveState(dir, st);
                   if (chosen) {
                     setKind(gameId, st, true);
                   }
                   emit uploaded(gameId, st.baseRevision);
                 } else if (r.kind == K::Conflict) {
                   st.conflictId = r.conflict->id;
                   SaveStore::saveState(dir, st);
                   if (chosen) {
                     setKind(gameId, st, true);
                   }
                 } else if (r.kind == K::Offline) {
                   retryRunning_ = false;
                   scheduleRetry();
                   return;
                 } else {
                   st.lastError = r.errorCode;
                   SaveStore::saveState(dir, st);
                 }
                 retryNext(items);
               });
}

// ---------------------------------------------------------------- Save comfort (saves_v2): restore, snapshots, push

QString SaveSync::pendingReason(const QString& gameId, const QString& slot) const {
  if (isRunning(gameId, slot)) {
    return tr("This game is running on this Player.");
  }
  const QString dir = slotDirFor(gameId, slot);
  if (dir.isEmpty()) {
    return tr("The save directory for this game is unknown.");
  }
  const SyncState st = SaveStore::loadState(dir);
  if (!st.conflictId.isEmpty()) {
    return tr("This slot has an open save conflict. Resolve it first.");
  }
  if (st.pending) {
    return tr("This slot has local changes that are not uploaded yet.");
  }
  const QString file = SaveStore::findSaveFile(dir, QString());
  if (!file.isEmpty() && SaveStore::sha256OfFile(file) != st.lastSyncedSha256) {
    return tr("This slot has local changes that are not uploaded yet.");
  }
  return {};
}

void SaveSync::restoreVersion(const QString& gameId, const QString& slot, int version, int expectedRevision,
                              const QString& localFileName, RestoreCallback cb) {
  using RK = RestoreResult::Outcome;
  const auto finish = [this, cb](RK kind, const QString& message, int revision = 0, bool local = false) {
    RestoreResult r;
    r.kind = kind;
    r.message = message;
    r.revision = revision;
    r.localUpdated = local;
    QTimer::singleShot(0, this, [cb, r]() { cb(r); });
  };
  if (!hubSupportsSavesV2() || !available()) {
    finish(RK::Failed, tr("The Hub does not support restoring saves."));
    return;
  }
  if (const QString why = pendingReason(gameId, slot); !why.isEmpty()) {
    finish(RK::Blocked, why);
    return;
  }
  const QString hubId = conn_->hubId();
  api_.restore(gameId, slot, version, expectedRevision, [=, this](const SaveApiResult& r) {
    using K = SaveApiResult::Kind;
    if (!sameHub(hubId)) {
      cb({RK::Failed, tr("The Hub connection changed."), 0, false});
      return;
    }
    switch (r.kind) {
      case K::Ok:
        break;
      case K::Stale:
        cb({RK::Stale, tr("The save changed on the Hub in the meantime. Nothing was restored."), 0, false});
        return;
      case K::NotFound:
        cb({RK::NotFound, tr("That version no longer exists on the Hub."), 0, false});
        return;
      case K::Offline:
        cb({RK::Offline, tr("Hub not reachable. Nothing was changed."), 0, false});
        return;
      default:
        cb({RK::Failed, tr("The version could not be restored (%1). Nothing was changed.").arg(r.errorCode.isEmpty() ? QString::number(r.status) : r.errorCode),
            0, false});
        return;
    }
    const int newRevision = r.slot->current.revision;
    // The Hub restored. Replace the local save of the slot by the new checkpoint (download); nothing unsynced exists here.
    const QString dir = slotDirFor(gameId, slot);
    if (dir.isEmpty() || (SaveStore::findSaveFile(dir, QString()).isEmpty() && localFileName.isEmpty())) {
      cb({RK::Ok, QString(), newRevision, false});  // no local file: the next start downloads the checkpoint
      return;
    }
    api_.getContent(gameId, slot, [=, this](const SaveApiResult& cr) {
      if (!sameHub(hubId) || pendingReason(gameId, slot).isEmpty() == false) {
        cb({RK::Ok, QString(), newRevision, false});  // something started meanwhile: leave the local save to the next sync
        return;
      }
      if (!cr.ok()) {
        cb({RK::Ok, tr("Restored on the Hub; the local save will be updated at the next start."), newRevision, false});
        return;
      }
      QDir().mkpath(dir);
      QString file = SaveStore::findSaveFile(dir, localFileName);
      if (file.isEmpty()) {
        file = QDir(dir).filePath(localFileName);
      }
      if (QFileInfo(file).isFile() && SaveStore::backupFile(file, QStringLiteral("restore")).isEmpty()) {
        cb({RK::Ok, tr("Restored on the Hub; the local save could not be backed up and stays unchanged until the next start."),
            newRevision, false});
        return;
      }
      if (!SaveStore::atomicWrite(file, cr.content)) {
        cb({RK::Ok, tr("Restored on the Hub; the local save could not be written."), newRevision, false});
        return;
      }
      SyncState st = SaveStore::loadState(dir);
      st.slot = slot;
      st.baseRevision = cr.contentRevision;
      st.lastSyncedSha256 = SaveStore::sha256Of(cr.content);
      st.pending = false;
      st.conflictId.clear();
      st.lastError.clear();
      SaveStore::saveState(dir, st);
      if (slot == slotFor(gameId)) {
        setKind(gameId, st, true);
      }
      cb({RK::Ok, QString(), cr.contentRevision, true});
    });
  });
}

void SaveSync::uploadSaveFile(const QString& gameId, const QString& slot, const QString& filePath, int expectedRevision,
                              const QString& localFileName, RestoreCallback cb) {
  using RK = RestoreResult::Outcome;
  const auto finish = [this, cb](RK kind, const QString& message) {
    RestoreResult r;
    r.kind = kind;
    r.message = message;
    QTimer::singleShot(0, this, [cb, r]() { cb(r); });
  };
  if (!hubSupportsSavesV4() || !available()) {
    finish(RK::Failed, tr("The Hub does not support uploading save files."));
    return;
  }
  if (const QString why = pendingReason(gameId, slot); !why.isEmpty()) {
    finish(RK::Blocked, why);
    return;
  }
  const QFileInfo info(filePath);
  if (!info.isFile()) {
    finish(RK::Failed, tr("The file could not be read."));
    return;
  }
  if (info.size() == 0) {
    finish(RK::Failed, tr("The file is empty. Nothing was uploaded."));
    return;
  }
  if (info.size() > kMaxSaveBytes) {
    finish(RK::Failed, tr("The file is larger than %1 MiB. Nothing was uploaded.").arg(kMaxSaveBytes / (1024 * 1024)));
    return;
  }
  QFile in(filePath);
  if (!in.open(QIODevice::ReadOnly)) {
    finish(RK::Failed, tr("The file could not be read."));
    return;
  }
  const QByteArray data = in.readAll();
  in.close();
  if (data.isEmpty()) {
    finish(RK::Failed, tr("The file is empty. Nothing was uploaded."));
    return;
  }
  const QString hubId = conn_->hubId();
  api_.uploadFile(gameId, slot, data, expectedRevision, [=, this](const SaveApiResult& r) {
    using K = SaveApiResult::Kind;
    if (!sameHub(hubId)) {
      cb({RK::Failed, tr("The Hub connection changed."), 0, false});
      return;
    }
    switch (r.kind) {
      case K::Ok:
        break;
      case K::Stale:
        cb({RK::Stale, tr("The save changed on the Hub in the meantime. Nothing was uploaded."), 0, false});
        return;
      case K::NotFound:
        cb({RK::NotFound, tr("The game or slot was not found on the Hub."), 0, false});
        return;
      case K::Offline:
        cb({RK::Offline, tr("Hub not reachable. Nothing was changed."), 0, false});
        return;
      default:
        if (r.errorCode == QLatin1String("payload_too_large")) {
          cb({RK::Failed, tr("The file is too large for the Hub (limit %1 MiB). Nothing was uploaded.").arg(kMaxSaveBytes / (1024 * 1024)), 0, false});
        } else {
          cb({RK::Failed, tr("The file could not be uploaded (%1). Nothing was changed.").arg(r.errorCode.isEmpty() ? QString::number(r.status) : r.errorCode),
              0, false});
        }
        return;
    }
    const int newRevision = r.slot->current.revision;
    // The Hub took the file. Make it the local save of the slot, too (backup first); nothing unsynced exists here.
    const QString dir = slotDirFor(gameId, slot);
    if (dir.isEmpty() || (SaveStore::findSaveFile(dir, QString()).isEmpty() && localFileName.isEmpty())) {
      cb({RK::Ok, QString(), newRevision, false});  // no local file: the next start downloads the checkpoint
      return;
    }
    if (!pendingReason(gameId, slot).isEmpty()) {
      cb({RK::Ok, tr("Uploaded to the Hub; the local save will be updated at the next start."), newRevision, false});
      return;
    }
    QDir().mkpath(dir);
    QString file = SaveStore::findSaveFile(dir, localFileName);
    if (file.isEmpty()) {
      file = QDir(dir).filePath(localFileName);
    }
    if (QFileInfo(file).isFile() && SaveStore::backupFile(file, QStringLiteral("upload")).isEmpty()) {
      cb({RK::Ok, tr("Uploaded to the Hub; the local save could not be backed up and stays unchanged until the next start."),
          newRevision, false});
      return;
    }
    if (!SaveStore::atomicWrite(file, data)) {
      cb({RK::Ok, tr("Uploaded to the Hub; the local save could not be written."), newRevision, false});
      return;
    }
    SyncState st = SaveStore::loadState(dir);
    st.slot = slot;
    st.baseRevision = newRevision;
    st.lastSyncedSha256 = SaveStore::sha256Of(data);
    st.pending = false;
    st.conflictId.clear();
    st.lastError.clear();
    SaveStore::saveState(dir, st);
    if (slot == slotFor(gameId)) {
      setKind(gameId, st, true);
    }
    cb({RK::Ok, QString(), newRevision, true});
  });
}

namespace {
SaveSnapshotResult snapshotResultOf(const SaveApiResult& r) {
  using K = SaveApiResult::Kind;
  using SK = SaveSnapshotResult::Outcome;
  SaveSnapshotResult out;
  switch (r.kind) {
    case K::Ok:
      out.kind = SK::Ok;
      out.version = *r.snapshot;
      break;
    case K::NotFound:
      out.kind = SK::NotFound;
      out.message = QObject::tr("There is no save on the Hub yet, so there is nothing to snapshot.");
      break;
    case K::Offline:
      out.kind = SK::Offline;
      out.message = QObject::tr("Hub not reachable. No snapshot was created.");
      break;
    default:
      out.kind = SK::Failed;
      out.message = QObject::tr("The snapshot could not be created (%1).").arg(r.errorCode.isEmpty() ? QString::number(r.status) : r.errorCode);
      break;
  }
  return out;
}
}  // namespace

void SaveSync::createSnapshot(const QString& gameId, const QString& slot, const QString& label, SnapshotCallback cb) {
  if (!hubSupportsSavesV2() || !available()) {
    QTimer::singleShot(0, this, [cb]() {
      SnapshotResult r;
      r.message = tr("The Hub does not support snapshots.");
      cb(r);
    });
    return;
  }
  api_.createSnapshot(gameId, slot, label, [cb](const SaveApiResult& r) { cb(snapshotResultOf(r)); });
}

void SaveSync::snapshotActive(const QString& label, SnapshotCallback cb) {
  if (!session_ || a_.gameId.isEmpty() || !hubSupportsSavesV2() || !available()) {
    QTimer::singleShot(0, this, [cb]() {
      SnapshotResult r;
      r.kind = SnapshotResult::Outcome::Blocked;
      r.message = tr("No snapshot is possible right now.");
      cb(r);
    });
    return;
  }
  const QString gameId = a_.gameId;
  const QString slot = a_.slot;
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn = connect(this, &SaveSync::finalSyncFinished, this, [this, conn, gameId, slot, label, cb](const QString& id, bool ok) {
    if (!id.isEmpty() && id != gameId) {
      return;
    }
    disconnect(*conn);
    if (id.isEmpty() || !ok) {
      SnapshotResult r;
      r.kind = SnapshotResult::Outcome::Failed;
      r.message = tr("The current save could not be uploaded, so no snapshot was created.");
      cb(r);
      return;
    }
    createSnapshot(gameId, slot, label, cb);
  });
  finalSync(false);  // existing final-sync path: uploads a changed save immediately
}

void SaveSync::handleSaveUpdate(const SaveUpdate& u) {
  // The Hub web interface reports the nil UUID with device_name "Hub web interface": never "this device".
  const bool nilDevice = u.deviceId == QLatin1String("00000000-0000-0000-0000-000000000000");
  if (!nilDevice && !conn_->deviceId().isEmpty() && u.deviceId == conn_->deviceId()) {
    return;  // our own change
  }
  emit saveChangedElsewhere(u, isRunning(u.gameId, u.slot));
}

}  // namespace framebeam
