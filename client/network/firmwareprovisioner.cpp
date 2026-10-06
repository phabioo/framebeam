#include "firmwareprovisioner.h"

#include <QLoggingCategory>
#include <QNetworkReply>
#include <QTimer>

namespace framebeam {

Q_LOGGING_CATEGORY(lcFirmware, "framebeam.firmware")

FirmwareProvisioner::FirmwareProvisioner(HubConnection* connection, FirmwareCache* cache, QObject* parent)
    : QObject(parent), conn_(connection), cache_(cache) {
  qRegisterMetaType<framebeam::FirmwareResult>("framebeam::FirmwareResult");
  connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s != HubConnection::State::Connected && busy_) {
      ++generation_;  // pending replies are aborted by the connection and ignored
      result_.ok = false;
      result_.problems.append({QString(), QString(), QStringLiteral("download_failed"), QStringLiteral("Connection to the Hub lost")});
      done();
    }
  });
}

QList<FirmwareProblem> FirmwareProvisioner::missingOnHub(const SystemInfo& system, const QStringList& wantedIds) {
  QList<FirmwareProblem> out;
  for (const FirmwareFileInfo& f : system.firmware) {
    if (f.required && !f.present && wantedIds.contains(f.id)) {
      out.append({f.id, f.displayName, QStringLiteral("missing_on_hub"), QString()});
    }
  }
  return out;
}

void FirmwareProvisioner::prepare(const SystemInfo& system, const QStringList& wantedIds) {
  if (busy_) {
    return;
  }
  busy_ = true;
  ++generation_;
  system_ = system;
  result_ = {};
  result_.systemId = system.id;
  result_.problems = missingOnHub(system, wantedIds);
  queue_.clear();
  for (const FirmwareFileInfo& f : system.firmware) {
    if (f.present && wantedIds.contains(f.id)) {
      queue_.append(f);
    }
  }
  // Asynchronous even if everything is cached, so callers see one consistent flow.
  const quint64 gen = generation_;
  QTimer::singleShot(0, this, [this, gen]() {
    if (gen == generation_ && busy_) {
      next();
    }
  });
}

void FirmwareProvisioner::next() {
  if (queue_.isEmpty()) {
    done();
    return;
  }
  const FirmwareFileInfo f = queue_.takeFirst();
  QString cached;
  if (cache_->lookup(system_.id, f.sha256, f.size, &cached)) {
    qCInfo(lcFirmware) << "cached" << system_.id << f.id << f.sha256;
    result_.pathsById.insert(f.id, cached);
    next();
    return;
  }
  QNetworkReply* reply =
      conn_ ? conn_->authorizedGet(QStringLiteral("/systems/%1/firmware/%2").arg(system_.id, f.id)) : nullptr;
  const auto problem = [this, f](const QString& reason, const QString& detail) {
    qCWarning(lcFirmware) << "problem" << system_.id << f.id << reason;
    if (f.required) {
      result_.problems.append({f.id, f.displayName, reason, detail});
    }
  };
  if (reply == nullptr) {
    problem(QStringLiteral("download_failed"), QStringLiteral("Not connected to the Hub"));
    next();
    return;
  }
  const quint64 gen = generation_;
  connect(reply, &QNetworkReply::downloadProgress, reply, [reply, f](qint64 got, qint64) {
    if (got > f.size) {
      reply->abort();  // more than the Hub announced: not the file we validate against
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen, f, problem]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (gen != generation_ || !busy_) {
      return;
    }
    if (r.status == 401 && conn_) {
      conn_->noteUnauthorized();
    }
    if (!r.ok()) {
      problem(QStringLiteral("download_failed"),
              r.networkError ? r.errorString : (r.apiErrorCode.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.apiErrorCode));
    } else {
      switch (cache_->store(system_.id, f.sha256, f.size, r.body)) {
        case FirmwareCache::StoreResult::Ok:
          qCInfo(lcFirmware) << "downloaded" << system_.id << f.id << f.sha256;
          result_.pathsById.insert(f.id, cache_->path(system_.id, f.sha256));
          break;
        case FirmwareCache::StoreResult::SizeMismatch:
          problem(QStringLiteral("invalid_size"), QStringLiteral("size differs from the Hub registry"));
          break;
        case FirmwareCache::StoreResult::HashMismatch:
          problem(QStringLiteral("invalid_hash"), QStringLiteral("SHA-256 differs from the Hub registry"));
          break;
        default:
          problem(QStringLiteral("download_failed"), QStringLiteral("cache write failed"));
          break;
      }
    }
    next();
  });
}

void FirmwareProvisioner::done() {
  busy_ = false;
  result_.ok = result_.problems.isEmpty();
  const FirmwareResult res = result_;
  emit finished(res);
}

}  // namespace framebeam
