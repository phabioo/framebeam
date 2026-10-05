#include "savesync.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <algorithm>

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
      const QString dir = SaveStore::gameDir(*profiles_, conn_->hubId(), conn_->hubUserId(), id);
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
  a_.dir = SaveStore::gameDir(*profiles_, a_.hubId, a_.userId, gameId);
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
  if (a_.file.isEmpty()) {
    if (SaveStore::migrateLegacy(SaveStore::legacyDir(*profiles_, a_.hubId), romBasenames, a_.dir, a_.expectedName)) {
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
      const QString l = SaveStore::sha256OfFile(a_.file);
      a_.st.baseRevision = r.slot->current.revision;
      a_.st.lastSyncedSha256 = r.slot->current.sha256;
      a_.st.pending = l != r.slot->current.sha256;  // the local file kept changing: still unsynced
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
                   SaveStore::backupFile(a_.file, QStringLiteral("local"));
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
  QStringList dirs;
  for (const QString& name : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (session_ && name == a_.gameId) {
      continue;  // the running game is handled by its own watcher
    }
    const QString dir = QDir(base).filePath(name);
    const SyncState st = SaveStore::loadState(dir);
    if (st.pending && st.conflictId.isEmpty()) {
      dirs.append(dir);
    }
  }
  if (dirs.isEmpty()) {
    retryBackoffMs_ = 0;
    return;
  }
  retryRunning_ = true;
  retryNext(dirs);
}

void SaveSync::retryNext(QStringList dirs) {
  if (dirs.isEmpty() || !available()) {
    retryRunning_ = false;
    retryBackoffMs_ = 0;
    return;
  }
  const QString hubId = conn_->hubId();
  const QString dir = dirs.takeFirst();
  const QString gameId = QFileInfo(dir).fileName();
  SyncState st = SaveStore::loadState(dir);
  const QString file = SaveStore::findSaveFile(dir, QString());
  const QByteArray data = [&]() {
    QFile f(file);
    return (!file.isEmpty() && f.open(QIODevice::ReadOnly)) ? f.readAll() : QByteArray();
  }();
  if (file.isEmpty()) {
    st.pending = false;
    SaveStore::saveState(dir, st);
    setKind(gameId, st, false);
    retryNext(dirs);
    return;
  }
  const QString sha = SaveStore::sha256Of(data);
  if (sha == st.lastSyncedSha256) {
    st.pending = false;
    SaveStore::saveState(dir, st);
    setKind(gameId, st, true);
    retryNext(dirs);
    return;
  }
  api_.putSave(gameId, st.slot, data, st.baseRevision, QStringLiteral("checkpoint"),
               [this, hubId, dir, gameId, sha, st, dirs](const SaveApiResult& r) mutable {
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
                   setKind(gameId, st, true);
                   emit uploaded(gameId, st.baseRevision);
                 } else if (r.kind == K::Conflict) {
                   st.conflictId = r.conflict->id;
                   SaveStore::saveState(dir, st);
                   setKind(gameId, st, true);
                 } else if (r.kind == K::Offline) {
                   retryRunning_ = false;
                   scheduleRetry();
                   return;
                 } else {
                   st.lastError = r.errorCode;
                   SaveStore::saveState(dir, st);
                 }
                 retryNext(dirs);
               });
}

}  // namespace framebeam
