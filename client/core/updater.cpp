#include "updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QUrl>

#include "updatesig.h"

namespace framebeam::update {

Q_LOGGING_CATEGORY(lcUpdate, "framebeam.update")

namespace {

constexpr qint64 kMaxIndexBytes = 2 * 1024 * 1024;
constexpr qint64 kMaxSigBytes = 4096;
constexpr qint64 kMaxInstallerBytes = Q_INT64_C(2) * 1024 * 1024 * 1024;

QNetworkRequest makeRequest(const QUrl& url) {
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setTransferTimeout(30000);
  req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FrameBeam-Player/") + QCoreApplication::applicationVersion());
  return req;
}

bool writeFile(const QString& path, const QByteArray& data) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(data);
  return f.commit();
}

QByteArray readFile(const QString& path, qint64 max = kMaxIndexBytes) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly) || f.size() > max) return {};
  return f.readAll();
}

// GET into memory with a size cap; calls done(ok, bytes, error) once.
void getBytes(QNetworkAccessManager* nam, const QUrl& url, qint64 maxBytes, QObject* context,
              std::function<void(bool, const QByteArray&, const QString&)> done) {
  QNetworkReply* reply = nam->get(makeRequest(url));
  QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 got, qint64) {
    if (got > maxBytes) reply->abort();
  });
  QObject::connect(reply, &QNetworkReply::finished, context, [reply, maxBytes, done]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      done(false, {}, reply->error() == QNetworkReply::OperationCanceledError
                          ? QStringLiteral("response too large") : reply->errorString());
      return;
    }
    const QByteArray data = reply->readAll();
    if (data.size() > maxBytes) {
      done(false, {}, QStringLiteral("response too large"));
      return;
    }
    done(true, data, {});
  });
}

}  // namespace

QString stagingRoot(const QString& baseDir) { return QDir(baseDir).filePath(QStringLiteral("cache/updates")); }

bool verifyArtifactFile(const QString& path, const Artifact& artifact, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error != nullptr) *error = QStringLiteral("installer file is missing");
    return false;
  }
  if (f.size() != artifact.size) {
    if (error != nullptr) *error = QStringLiteral("installer size does not match the signed index");
    return false;
  }
  QCryptographicHash h(QCryptographicHash::Sha256);
  if (!h.addData(&f)) {
    if (error != nullptr) *error = QStringLiteral("installer could not be read");
    return false;
  }
  if (QString::fromLatin1(h.result().toHex()) != artifact.sha256) {
    if (error != nullptr) *error = QStringLiteral("installer checksum does not match the signed index");
    return false;
  }
  return true;
}

std::optional<StagedUpdate> loadVerifiedStaged(const QString& baseDir, const QString& currentVersion,
                                               const QList<QByteArray>& trustedKeys, bool allowFileUrls) {
  const QDir root(stagingRoot(baseDir));
  std::optional<StagedUpdate> best;
  std::optional<SemVer> bestVer;
  QString bestDir;
  const auto cur = SemVer::parse(currentVersion);
  const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& name : dirs) {
    const QDir d(root.filePath(name));
    const QJsonObject staged = QJsonDocument::fromJson(readFile(d.filePath(QStringLiteral("staged.json")), 65536)).object();
    const QString version = staged.value(QLatin1String("version")).toString();
    const QString artifactName = staged.value(QLatin1String("artifact")).toString();
    const auto sv = SemVer::parse(version);
    if (!cur || !sv || SemVer::compare(*sv, *cur) <= 0 || artifactName.isEmpty()) continue;  // replay/old/invalid
    const QByteArray idx = readFile(d.filePath(QStringLiteral("index.json")));
    const QByteArray sig = readFile(d.filePath(QStringLiteral("index.json.sig")), kMaxSigBytes);
    if (idx.isEmpty() || !verifyIndexSignature(idx, sig, trustedKeys).ok) continue;
    const ParseResult pr = parseIndex(idx, allowFileUrls);
    if (!pr.ok) continue;
    const Release* rel = nullptr;
    for (const Release& r : pr.index.releases) {
      if (r.product == QLatin1String("player") && r.semver.toString() == sv->toString()) {
        rel = &r;
        break;
      }
    }
    if (rel == nullptr) continue;
    const auto art = rel->artifact(QLatin1String(kPlayerPlatform), QLatin1String(kKindInstaller));
    if (!art || art->name != artifactName) continue;
    const QString path = d.filePath(art->name);
    if (!verifyArtifactFile(path, *art)) continue;
    if (!bestVer || SemVer::compare(*sv, *bestVer) > 0) {
      bestVer = sv;
      bestDir = name;
      best = StagedUpdate{rel->version, path, rel->notesUrl};
    }
  }
  for (const QString& name : dirs) {
    if (name != bestDir) QDir(root.filePath(name)).removeRecursively();
  }
  return best;
}

// ---------------------------------------------------------------- fetch

void UpdateManager::fetchIndex(QNetworkAccessManager* nam, const QUrl& indexUrl, const QList<QByteArray>& trustedKeys,
                               QObject* context, std::function<void(const FetchedIndex&)> done) {
  const bool fileIndex = indexUrl.scheme() == QLatin1String("file");
  if (!(fileIndex || indexUrl.scheme() == QLatin1String("https"))) {
    FetchedIndex fi;
    fi.error = QStringLiteral("update index URL must be https");
    done(fi);
    return;
  }
  QUrl sigUrl = indexUrl;
  sigUrl.setPath(indexUrl.path() + QStringLiteral(".sig"));
  getBytes(nam, indexUrl, kMaxIndexBytes, context, [=](bool ok, const QByteArray& idx, const QString& err) {
    if (!ok) {
      FetchedIndex fi;
      fi.error = QStringLiteral("index download failed: %1").arg(err);
      done(fi);
      return;
    }
    getBytes(nam, sigUrl, kMaxSigBytes, context, [=](bool sok, const QByteArray& sig, const QString& serr) {
      FetchedIndex fi;
      if (!sok) {
        fi.error = QStringLiteral("signature download failed: %1").arg(serr);
        done(fi);
        return;
      }
      const SigCheck sc = verifyIndexSignature(idx, sig, trustedKeys);
      if (!sc.ok) {
        fi.error = sc.error;  // never use unsigned content
        done(fi);
        return;
      }
      const ParseResult pr = parseIndex(idx, fileIndex);
      if (!pr.ok) {
        fi.error = pr.error;
        done(fi);
        return;
      }
      fi.ok = true;
      fi.indexBytes = idx;
      fi.sigBytes = sig;
      fi.index = pr.index;
      done(fi);
    });
  });
}

// ---------------------------------------------------------------- manager

UpdateManager::UpdateManager(const Config& config, PlayerSettings* settings, QObject* parent)
    : QObject(parent), config_(config), settings_(settings) {
  indexUrl_ = config.indexUrl;
  if (indexUrl_.isEmpty()) indexUrl_ = QString::fromLocal8Bit(qgetenv("FRAMEBEAM_PLAYER_UPDATE_INDEX_URL"));
  if (indexUrl_.isEmpty()) indexUrl_ = QLatin1String(kDefaultIndexUrl);
  if (config_.trustedKeys.isEmpty()) config_.trustedKeys = trustedKeysFromEnvironment();
  timer_.setSingleShot(true);
  launcher_ = [](const QString& program, const QStringList& args) { return QProcess::startDetached(program, args); };
  connect(&timer_, &QTimer::timeout, this, [this]() {
    if (effectiveChannel() != Channel::Off && !busy()) checkNow();
    else scheduleNext();
  });
}

Channel UpdateManager::effectiveChannel() const {
  if (const auto c = parseChannel(settings_->updateChannel()); c && *c != Channel::Off) return *c;
  return channelFromCompiled(config_.compiledChannel);
}

bool UpdateManager::autoInstallSetting() const {
  if (settings_->updateAutoInstall()) return *settings_->updateAutoInstall();
  return effectiveChannel() == Channel::Test;
}

bool UpdateManager::applySupported() const {
  if (config_.forceApplySupport) return true;
#ifdef Q_OS_WIN
  return hasUninstaller(config_.installRoot);
#else
  return false;
#endif
}

QString UpdateManager::applyUnsupportedReason() const {
#ifdef Q_OS_WIN
  if (!hasUninstaller(config_.installRoot)) {
    return QStringLiteral("This copy of the FrameBeam Player was not installed with the installer (portable or development build).");
  }
  return {};
#else
  return QStringLiteral("Automatic installation is only available for the Windows installer version.");
#endif
}

bool UpdateManager::allowFileUrls() const { return indexUrl_.startsWith(QLatin1String("file:")); }

void UpdateManager::setState(State s, const QString& text) {
  state_ = s;
  status_ = text;
  emit changed();
}

void UpdateManager::start(int firstDelayMs) { timer_.start(firstDelayMs); }

void UpdateManager::scheduleNext() {
  const bool test = effectiveChannel() == Channel::Test;
  timer_.start(test ? 60 * 60 * 1000 : 24 * 60 * 60 * 1000);
}

void UpdateManager::settingsChanged() {
  emit changed();
  if (state_ == State::Downloading || state_ == State::Applying) return;
  checkNow();
}

void UpdateManager::checkNow() {
  if (checkRunning_ || state_ == State::Downloading || state_ == State::Applying) return;
  if (!SemVer::parse(config_.currentVersion)) {
    setState(State::Disabled, tr("Updates are off: this build has no release version."));
    return;
  }
  if (effectiveChannel() == Channel::Off) {
    setState(State::Disabled, tr("Updates are off for this development build. Choose a channel to check for updates."));
    return;
  }
  const QUrl url(indexUrl_);
  checkRunning_ = true;
  if (state_ != State::Ready) setState(State::Checking, tr("Checking for updates…"));
  else emit changed();
  fetchIndex(&nam_, url, config_.trustedKeys, this, [this](const FetchedIndex& fi) { onIndex(fi); });
}

void UpdateManager::onIndex(const FetchedIndex& fi) {
  checkRunning_ = false;
  lastCheck_ = QDateTime::currentDateTime();
  scheduleNext();
  if (!fi.ok) {
    lastError_ = fi.error;
    qCWarning(lcUpdate) << "update check failed:" << fi.error;
    if (state_ == State::Ready) {
      emit changed();  // a staged update stays usable
      return;
    }
    setState(State::Error, tr("Update check failed: %1").arg(fi.error));
    return;
  }
  lastError_.clear();
  lastIndex_ = fi;
  SelectionInput in;
  in.channel = effectiveChannel();
  in.currentVersion = config_.currentVersion;
  if (hubProtocol_) in.hub = hubProtocol_();
  selection_ = selectRelease(fi.index, in);
  switch (selection_.status) {
    case Selection::Status::Disabled:
      setState(State::Disabled, selection_.reason);
      return;
    case Selection::Status::UpToDate:
      loadVerifiedStaged(config_.baseDir, config_.currentVersion, config_.trustedKeys, allowFileUrls());  // prune
      setState(State::UpToDate, tr("FrameBeam Player %1 is up to date.").arg(config_.currentVersion));
      return;
    case Selection::Status::Incompatible:
      setState(State::Incompatible,
               tr("FrameBeam Player %1 is available but needs a newer Hub. It is not installed automatically.")
                   .arg(selection_.release.version));
      return;
    case Selection::Status::Available:
      break;
  }
  const auto staged = loadVerifiedStaged(config_.baseDir, config_.currentVersion, config_.trustedKeys, allowFileUrls());
  if (staged && staged->version == selection_.release.version && applySupported()) {
    setState(State::Ready, tr("FrameBeam Player %1 is ready to install.").arg(staged->version));
    return;
  }
  if (!applySupported()) {
    setState(State::Available, tr("FrameBeam Player %1 is available. %2").arg(selection_.release.version, applyUnsupportedReason()));
    return;
  }
  if (autoInstallEffective()) {
    startDownload(false);
    return;
  }
  setState(State::Available, tr("FrameBeam Player %1 is available.").arg(selection_.release.version));
}

void UpdateManager::startDownload(bool applyAfter) {
  const Artifact art = selection_.artifact;
  if (art.size > kMaxInstallerBytes) {
    setState(State::Error, tr("The update is too large."));
    return;
  }
  const QString dir = QDir(stagingRoot(config_.baseDir)).filePath(selection_.release.semver.toString());
  QDir(dir).removeRecursively();
  if (!QDir().mkpath(dir)) {
    setState(State::Error, tr("The update folder could not be created."));
    return;
  }
  auto* file = new QFile(dir + QLatin1Char('/') + art.name + QStringLiteral(".part"));
  if (!file->open(QIODevice::WriteOnly)) {
    delete file;
    setState(State::Error, tr("The update file could not be written."));
    return;
  }
  applyAfterDownload_ = applyAfter;
  progress_ = 0;
  setState(State::Downloading, tr("Downloading FrameBeam Player %1…").arg(selection_.release.version));
  QNetworkReply* reply = nam_.get(makeRequest(QUrl(art.url)));
  download_ = reply;
  file->setParent(reply);
  connect(reply, &QNetworkReply::readyRead, this, [this, reply, file, art]() {
    const QByteArray chunk = reply->readAll();
    if (file->size() + chunk.size() > art.size) {
      reply->setProperty("tooLarge", true);
      reply->abort();
      return;
    }
    file->write(chunk);
  });
  connect(reply, &QNetworkReply::downloadProgress, this, [this, art](qint64 got, qint64) {
    progress_ = art.size > 0 ? qBound(0.0, static_cast<double>(got) / static_cast<double>(art.size), 1.0) : 0.0;
    emit changed();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, file]() {
    file->flush();
    file->close();
    const QString part = file->fileName();
    const bool tooLarge = reply->property("tooLarge").toBool();
    const bool failed = reply->error() != QNetworkReply::NoError;
    const QString err = tooLarge ? QStringLiteral("download is larger than announced") : reply->errorString();
    reply->deleteLater();
    if (failed || tooLarge) {
      QFile::remove(part);
      lastError_ = err;
      setState(State::Error, tr("The update could not be downloaded: %1").arg(err));
      return;
    }
    finishDownload(part);
  });
}

void UpdateManager::finishDownload(const QString& partPath) {
  const Artifact art = selection_.artifact;
  QString why;
  if (!verifyArtifactFile(partPath, art, &why)) {
    QFile::remove(partPath);
    lastError_ = why;
    qCWarning(lcUpdate) << "downloaded installer rejected:" << why;
    setState(State::Error, tr("The downloaded update was rejected: %1").arg(why));
    return;
  }
  const QDir dir = QFileInfo(partPath).absoluteDir();
  const QString finalPath = dir.filePath(art.name);
  QFile::remove(finalPath);
  if (!QFile::rename(partPath, finalPath) ||
      !writeFile(dir.filePath(QStringLiteral("index.json")), lastIndex_.indexBytes) ||
      !writeFile(dir.filePath(QStringLiteral("index.json.sig")), lastIndex_.sigBytes) ||
      !writeFile(dir.filePath(QStringLiteral("staged.json")),
                 QJsonDocument(QJsonObject{{"version", selection_.release.version}, {"artifact", art.name}}).toJson())) {
    setState(State::Error, tr("The update could not be staged."));
    return;
  }
  setState(State::Ready, tr("FrameBeam Player %1 is ready to install.").arg(selection_.release.version));
  if (applyAfterDownload_) {
    applyAfterDownload_ = false;
    installNow();
  }
}

void UpdateManager::installNow() {
  if (state_ == State::Downloading || state_ == State::Applying) return;
  if (!applySupported()) {
    setState(State::Available, tr("This copy cannot update itself. %1").arg(applyUnsupportedReason()));
    return;
  }
  if (busy()) {
    setState(state_, tr("A game or Session is running. Finish it first, then install the update."));
    return;
  }
  const auto staged = loadVerifiedStaged(config_.baseDir, config_.currentVersion, config_.trustedKeys, allowFileUrls());
  if (staged && (selection_.release.version.isEmpty() || staged->version == selection_.release.version)) {
    apply(*staged);
    return;
  }
  if (state_ == State::Available || state_ == State::Ready || state_ == State::Error) {
    if (selection_.status == Selection::Status::Available) {
      startDownload(true);
    }
  }
}

bool UpdateManager::applyStagedAtStart() {
  if (!applySupported() || effectiveChannel() != Channel::Test || !autoInstallSetting() || busy()) return false;
  const auto staged = loadVerifiedStaged(config_.baseDir, config_.currentVersion, config_.trustedKeys, allowFileUrls());
  if (!staged) return false;
  apply(*staged);
  return state_ == State::Applying;
}

void UpdateManager::apply(const StagedUpdate& staged) {
  if (busy()) {
    setState(state_, tr("A game or Session is running. Finish it first, then install the update."));
    return;
  }
  const InstallerCommand cmd = installerCommand(staged.installerPath, directoryWritable(config_.installRoot));
  qCInfo(lcUpdate) << "starting installer for" << staged.version;
  if (!launcher_(cmd.program, cmd.args)) {
    lastError_ = QStringLiteral("installer could not be started");
    setState(State::Error, tr("The installer could not be started."));
    return;
  }
  setState(State::Applying, tr("Installing FrameBeam Player %1…").arg(staged.version));
  emit quitRequested();
}

}  // namespace framebeam::update
