#include "coreprovisioner.h"

#include <QLoggingCategory>
#include <QNetworkReply>
#include <QTimer>


namespace framebeam {

Q_LOGGING_CATEGORY(lcCores, "framebeam.cores")

CoreProvisioner::CoreProvisioner(HubConnection* connection, CoreCache* cache, QObject* parent)
    : QObject(parent), conn_(connection), cache_(cache), platform_(CoreCache::currentPlatform()) {
  qRegisterMetaType<framebeam::CoreResult>("framebeam::CoreResult");
  connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s != HubConnection::State::Connected && busy_) {
      ++generation_;  // pending replies are aborted by the connection and ignored
      fail(QStringLiteral("download_failed"), QStringLiteral("Connection to the Hub lost"));
    }
  });
}

void CoreProvisioner::prepare(const QString& coreId, const QString& version) {
  if (busy_) {
    return;
  }
  busy_ = true;
  ++generation_;
  result_ = {};
  result_.coreId = coreId;
  result_.version = version;
  pkg_ = {};
  queue_.clear();
  const quint64 gen = generation_;
  // Always asynchronous, so callers see one consistent flow.
  QTimer::singleShot(0, this, [this, gen, coreId, version]() {
    if (gen != generation_ || !busy_) {
      return;
    }
    if (!isValidCoreId(coreId) || !isValidCoreVersion(version) || !isValidCorePlatform(platform_)) {
      fail(QStringLiteral("not_on_hub"), QStringLiteral("invalid core id, version or platform"));
      return;
    }
    QNetworkReply* reply =
        conn_ ? conn_->authorizedGet(QStringLiteral("/cores/%1/packages/%2/%3").arg(coreId, version, platform_)) : nullptr;
    if (reply == nullptr) {
      fail(QStringLiteral("download_failed"), QStringLiteral("Not connected to the Hub"));
      return;
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
      const HttpResult r = HubHttp::resultOf(reply);
      reply->deleteLater();
      if (gen != generation_ || !busy_) {
        return;
      }
      if (r.status == 401 && conn_) {
        conn_->noteUnauthorized();
      }
      onPackage(r);
    });
  });
}

void CoreProvisioner::onPackage(const HttpResult& r) {
  if (r.status == 404) {
    // The Hub does not know this core version for our platform. If it has it for another platform, the core is
    // incompatible with this machine; otherwise it is simply not on the Hub.
    QStringList others;
    for (const QString& p : {QStringLiteral("windows-x64"), QStringLiteral("linux-x64"), QStringLiteral("linux-arm64"),
                             QStringLiteral("macos-x64"), QStringLiteral("macos-arm64")}) {
      if (p != platform_) {
        others.append(p);
      }
    }
    probeOtherPlatforms(generation_, others);
    return;
  }
  if (!r.ok()) {
    fail(QStringLiteral("download_failed"),
         r.networkError ? r.errorString : (r.apiErrorCode.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.apiErrorCode));
    return;
  }
  const auto pkg = parseCorePackage(r.json());
  if (!pkg || pkg->coreId != result_.coreId || pkg->version != result_.version || pkg->platform != platform_) {
    fail(QStringLiteral("download_failed"), QStringLiteral("invalid package metadata from the Hub"));
    return;
  }
  pkg_ = *pkg;
  qCInfo(lcCores) << "package" << pkg_.coreId << pkg_.version << pkg_.platform << "files" << pkg_.files.size();
  startDownloads();
}

void CoreProvisioner::startDownloads() {
  queue_ = pkg_.files;
  next();
}

void CoreProvisioner::probeOtherPlatforms(quint64 gen, QStringList remaining) {
  if (remaining.isEmpty()) {
    fail(QStringLiteral("not_on_hub"), QStringLiteral("the Hub has no package %1 %2 for %3").arg(result_.coreId, result_.version, platform_));
    return;
  }
  const QString p = remaining.takeFirst();
  QNetworkReply* reply = conn_ ? conn_->authorizedGet(QStringLiteral("/cores/%1/packages/%2/%3").arg(result_.coreId, result_.version, p)) : nullptr;
  if (reply == nullptr) {
    fail(QStringLiteral("not_on_hub"), QStringLiteral("the Hub has no package %1 %2 for %3").arg(result_.coreId, result_.version, platform_));
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen, remaining, p]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (gen != generation_ || !busy_) {
      return;
    }
    if (r.ok()) {
      fail(QStringLiteral("incompatible"), QStringLiteral("the Hub has %1 %2 only for other platforms (e.g. %3)").arg(result_.coreId, result_.version, p));
    } else {
      probeOtherPlatforms(gen, remaining);
    }
  });
}

void CoreProvisioner::next() {
  if (queue_.isEmpty()) {
    // Every file is valid in the cache: package.json makes the package usable.
    if (!cache_->writePackage(pkg_)) {
      fail(QStringLiteral("download_failed"), QStringLiteral("cache write failed"));
      return;
    }
    // Every file was just verified (fileValid / store) in this run: no third hash of a big library here.
    result_.libraryPath = cache_->libraryPathJustVerified(pkg_.coreId, pkg_.version, pkg_.platform);
    if (result_.libraryPath.isEmpty()) {
      fail(QStringLiteral("download_failed"), QStringLiteral("cache verification failed"));
      return;
    }
    result_.ok = true;
    // Keep the newest two versions per core plus the one in use; a failed delete never fails the launch.
    if (const int n = cache_->prune(pkg_.coreId, pkg_.platform, 2, pkg_.version); n > 0) {
      qCInfo(lcCores) << "pruned" << n << "old version(s) of" << pkg_.coreId;
    }
    done();
    return;
  }
  const CorePackageFile f = queue_.takeFirst();
  if (cache_->fileValid(pkg_, f)) {
    qCInfo(lcCores) << "cached" << pkg_.coreId << pkg_.version << f.name << f.sha256;
    next();
    return;
  }
  if (!f.available) {
    qCWarning(lcCores) << "not cached on the Hub" << pkg_.coreId << pkg_.version << f.name;
    fail(QStringLiteral("not_cached_on_hub"), f.name);
    return;
  }
  QNetworkReply* reply =
      conn_ ? conn_->authorizedGet(QStringLiteral("/cores/%1/packages/%2/%3/files/%4").arg(pkg_.coreId, pkg_.version, pkg_.platform, f.name))
            : nullptr;
  if (reply == nullptr) {
    fail(QStringLiteral("download_failed"), QStringLiteral("Not connected to the Hub"));
    return;
  }
  const quint64 gen = generation_;
  connect(reply, &QNetworkReply::downloadProgress, reply, [reply, f](qint64 got, qint64) {
    if (got > f.size) {
      reply->abort();  // more than the metadata announced: not the file we validate against
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen, f]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (gen != generation_ || !busy_) {
      return;
    }
    if (r.status == 401 && conn_) {
      conn_->noteUnauthorized();
    }
    if (r.status == 404) {
      qCWarning(lcCores) << "file not available on the Hub" << pkg_.coreId << pkg_.version << f.name;
      fail(QStringLiteral("not_cached_on_hub"), f.name);
      return;
    }
    if (!r.ok()) {
      fail(QStringLiteral("download_failed"),
           r.networkError ? r.errorString : (r.apiErrorCode.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.apiErrorCode));
      return;
    }
    switch (cache_->store(pkg_, f, r.body)) {
      case CoreCache::StoreResult::Ok:
        qCInfo(lcCores) << "downloaded" << pkg_.coreId << pkg_.version << f.name << f.sha256;
        result_.downloaded = true;
        next();
        break;
      case CoreCache::StoreResult::SizeMismatch:
        fail(QStringLiteral("invalid_size"), f.name);
        break;
      case CoreCache::StoreResult::HashMismatch:
        fail(QStringLiteral("invalid_hash"), f.name);
        break;
      default:
        fail(QStringLiteral("download_failed"), QStringLiteral("cache write failed"));
        break;
    }
  });
}

void CoreProvisioner::fail(const QString& reason, const QString& detail) {
  qCWarning(lcCores) << "problem" << result_.coreId << result_.version << reason;
  result_.ok = false;
  result_.libraryPath.clear();
  result_.problem = reason;
  result_.detail = detail;
  done();
}

void CoreProvisioner::done() {
  busy_ = false;
  const CoreResult res = result_;
  emit finished(res);
}

}  // namespace framebeam
