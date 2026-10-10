#include "hubsetup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUuid>

#include "installroot.h"
#include "updatesig.h"

namespace framebeam::update {

Q_LOGGING_CATEGORY(lcHubSetup, "framebeam.update.hubsetup")

std::optional<Artifact> findOwnMsi(const Index& index, const QString& version, Release* releaseOut) {
  for (const Release& r : index.releases) {
    if (r.product != QLatin1String("player") || r.semver.toString() != version) continue;
    if (const auto a = r.artifact(QLatin1String(kPlayerPlatform), QLatin1String(kKindMsi))) {
      if (releaseOut != nullptr) *releaseOut = r;
      return a;
    }
  }
  return std::nullopt;
}

HubSetupInstaller::HubSetupInstaller(const Config& config, QObject* parent) : QObject(parent), config_(config) {
  if (config_.indexUrl.isEmpty()) config_.indexUrl = QString::fromLocal8Bit(qgetenv("FRAMEBEAM_PLAYER_UPDATE_INDEX_URL"));
  if (config_.indexUrl.isEmpty()) config_.indexUrl = QLatin1String(kDefaultIndexUrl);
  if (config_.trustedKeys.isEmpty()) config_.trustedKeys = trustedKeysFromEnvironment();
  if (config_.programFilesDir.isEmpty()) config_.programFilesDir = QString::fromLocal8Bit(qgetenv("ProgramFiles"));
  launcher_ = [](const QString& program, const QStringList& args) { return QProcess::startDetached(program, args); };
}

void HubSetupInstaller::fail(const QString& text) {
  error_ = text;
  state_ = State::Failed;
  qCWarning(lcHubSetup) << "hub setup failed:" << text;
  emit changed();
}

void HubSetupInstaller::cancel() {
  ++gen_;
  if (reply_) reply_->abort();
  if (state_ == State::Fetching || state_ == State::Downloading) {
    state_ = State::Idle;
    emit changed();
  }
}

void HubSetupInstaller::start() {
  if (state_ == State::Fetching || state_ == State::Downloading || state_ == State::Launching) return;
  error_.clear();
  progress_ = 0;
  state_ = State::Fetching;
  emit changed();
  const quint64 gen = ++gen_;
  UpdateManager::fetchIndex(&nam_, QUrl(config_.indexUrl), config_.trustedKeys, this, [this, gen](const FetchedIndex& fi) {
    if (gen != gen_) return;
    onIndex(fi);
  });
}

void HubSetupInstaller::onIndex(const FetchedIndex& fi) {
  if (!fi.ok) {
    fail(tr("The update index could not be loaded: %1").arg(fi.error));
    return;
  }
  const auto art = findOwnMsi(fi.index, config_.currentVersion);
  if (!art) {
    fail(tr("There is no installer with the Hub for FrameBeam Player %1 yet.").arg(config_.currentVersion));
    return;
  }
  if (!config_.indexUrl.startsWith(QLatin1String("file:")) && !art->url.startsWith(QLatin1String("https:"))) {
    fail(tr("The installer URL is not secure."));
    return;
  }
  download(*art);
}

void HubSetupInstaller::download(const Artifact& art) {
  if (art.size > Q_INT64_C(2) * 1024 * 1024 * 1024) {
    fail(tr("The installer is too large."));
    return;
  }
  QString dir = config_.downloadDir;
  if (dir.isEmpty()) dir = QDir::tempPath() + QStringLiteral("/framebeam-hub-setup-") + QUuid::createUuid().toString(QUuid::Id128);
  if (!QDir().mkpath(dir)) {
    fail(tr("The download folder could not be created."));
    return;
  }
  const QString part = QDir(dir).filePath(art.name + QStringLiteral(".part"));
  auto* file = new QFile(part);
  if (!file->open(QIODevice::WriteOnly)) {
    delete file;
    fail(tr("The installer could not be written."));
    return;
  }
  state_ = State::Downloading;
  emit changed();
  QNetworkRequest req{QUrl(art.url)};
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setTransferTimeout(30000);
  QNetworkReply* reply = nam_.get(req);
  reply_ = reply;
  file->setParent(reply);
  const quint64 gen = gen_;
  connect(reply, &QNetworkReply::readyRead, this, [reply, file, art]() {
    const QByteArray chunk = reply->readAll();
    if (file->size() + chunk.size() > art.size) {
      reply->setProperty("tooLarge", true);
      reply->abort();
      return;
    }
    file->write(chunk);
  });
  connect(reply, &QNetworkReply::downloadProgress, this, [this, gen, art](qint64 got, qint64) {
    if (gen != gen_) return;
    progress_ = art.size > 0 ? qBound(0.0, static_cast<double>(got) / static_cast<double>(art.size), 1.0) : 0.0;
    emit changed();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, file, art, gen, dir]() {
    file->flush();
    file->close();
    const QString partPath = file->fileName();
    const bool tooLarge = reply->property("tooLarge").toBool();
    const bool failed = reply->error() != QNetworkReply::NoError;
    const QString err = tooLarge ? QStringLiteral("larger than announced") : reply->errorString();
    reply->deleteLater();
    if (gen != gen_) {
      QFile::remove(partPath);
      return;
    }
    if (failed || tooLarge) {
      QFile::remove(partPath);
      fail(tr("The installer could not be downloaded: %1").arg(err));
      return;
    }
    QString why;
    if (!verifyArtifactFile(partPath, art, &why)) {
      QFile::remove(partPath);
      fail(tr("The downloaded installer was rejected: %1").arg(why));
      return;
    }
    const QString finalPath = QDir(dir).filePath(art.name);
    QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
      fail(tr("The installer could not be saved."));
      return;
    }
    launch(finalPath);
  });
}

void HubSetupInstaller::launch(const QString& msiPath) {
  QString why;
  const QString launcher = copyLauncherToTemp(config_.installRoot, QDir::tempPath(), &why);
  if (launcher.isEmpty()) {
    fail(tr("The installer could not be started: %1").arg(why));
    return;
  }
  const InstallerCommand cmd = msiRelaunchCommand(launcher, msiPath, MsiScope::SetupHub,
                                                  perMachinePlayerExe(config_.programFilesDir), {QStringLiteral("--setup-local-hub")});
  state_ = State::Launching;
  emit changed();
  if (!launcher_(cmd.program, cmd.args)) {
    fail(tr("The installer could not be started."));
    return;
  }
  emit quitRequested();
}

}  // namespace framebeam::update
