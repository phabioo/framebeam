#include "coreupdater.h"

#include <QLoggingCategory>
#include <algorithm>


namespace framebeam {

Q_DECLARE_LOGGING_CATEGORY(lcCores)

namespace {
constexpr int kMaxRetryMs = 30 * 60 * 1000;
}

CoreUpdater::CoreUpdater(HubConnection* connection, CoreCache* cache, QObject* parent)
    : QObject(parent), conn_(connection), cache_(cache), prov_(connection, cache, this), platform_(CoreCache::currentPlatform()) {
  qRegisterMetaType<framebeam::CoreUpdater::Candidate>("framebeam::CoreUpdater::Candidate");
  retryTimer_.setSingleShot(true);
  connect(&retryTimer_, &QTimer::timeout, this, [this]() {
    failedCores_.clear();
    if (!systems_.isEmpty()) {
      check(systems_);
    }
  });
  connect(&prov_, &CoreProvisioner::finished, this, &CoreUpdater::onFinished);
  connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s != HubConnection::State::Connected) {
      reset();  // offline: no downloads; the next connect brings a fresh systems list
    }
  });
}

void CoreUpdater::setPlatform(const QString& platform) {
  platform_ = platform;
  prov_.setPlatform(platform);
}

void CoreUpdater::setInUseProvider(std::function<QString(const QString&)> fn) { prov_.setProtectedVersionProvider(std::move(fn)); }

QList<CoreUpdater::Candidate> CoreUpdater::candidates(const QList<SystemInfo>& systems) const {
  QList<Candidate> out;
  const auto consider = [&](const SystemCore& c) {
    if (!c.valid() || !isValidCoreId(c.coreId) || !isValidCoreVersion(c.version)) {
      return;
    }
    const QStringList cached = cache_->versions(c.coreId, platform_);  // newest first
    if (cached.isEmpty()) {
      return;  // never used on this device: not fetched speculatively
    }
    if (CoreCache::compareVersions(c.version, cached.first()) <= 0) {
      return;  // cached build is current (or newer than the Hub's)
    }
    for (Candidate& e : out) {
      if (e.coreId == c.coreId) {
        if (CoreCache::compareVersions(c.version, e.version) > 0) {
          e.version = c.version;
        }
        return;
      }
    }
    out.append({c.coreId, c.displayName.isEmpty() ? c.coreId : c.displayName, c.version});
  };
  for (const SystemInfo& s : systems) {
    if (s.cores.isEmpty()) {
      consider(s.defaultCore());
    }
    for (const SystemCore& c : s.cores) {
      consider(c);
    }
  }
  return out;
}

void CoreUpdater::check(const QList<SystemInfo>& systems) {
  systems_ = systems;
  if (!conn_ || conn_->state() != HubConnection::State::Connected || platform_.isEmpty()) {
    return;
  }
  queue_.clear();
  for (const Candidate& c : candidates(systems)) {
    if (active_ && c.coreId == current_.coreId) {
      continue;  // already being fetched
    }
    if (!failedCores_.contains(c.coreId)) {
      queue_.append(c);
    }
  }
  startNext();
}

void CoreUpdater::startNext() {
  if (active_ || queue_.isEmpty()) {
    return;
  }
  current_ = queue_.takeFirst();
  active_ = true;
  qCInfo(lcCores) << "background update" << current_.coreId << "to" << current_.version;
  emit statusChanged();
  prov_.prepare(current_.coreId, current_.version);
}

void CoreUpdater::onFinished(const CoreResult& r) {
  if (!active_) {
    return;
  }
  const Candidate c = current_;
  active_ = false;
  if (r.ok) {
    failures_ = 0;
    lastUpdate_ = c;
    qCInfo(lcCores) << "updated" << c.coreId << "to" << c.version << (r.downloaded ? "(downloaded)" : "(already cached)");
    emit updated(c, r);
  } else {
    ++failures_;
    failedCores_.insert(c.coreId);
    qCWarning(lcCores) << "background update failed" << c.coreId << c.version << r.problem << "- retrying later";
    scheduleRetry();
  }
  emit statusChanged();
  startNext();
}

void CoreUpdater::scheduleRetry() {
  if (retryTimer_.isActive()) {
    return;
  }
  int ms = retryBaseMs_;
  for (int i = 1; i < failures_ && ms < kMaxRetryMs; ++i) {
    ms *= 2;
  }
  retryTimer_.start(std::min(ms, std::max(kMaxRetryMs, retryBaseMs_)));
}

void CoreUpdater::cancelFor(const QString& coreId) {
  queue_.removeIf([&](const Candidate& c) { return c.coreId == coreId; });
  if (active_ && current_.coreId == coreId) {
    prov_.cancel();
    active_ = false;
    qCInfo(lcCores) << "background update of" << coreId << "handed over to the game start";
    scheduleRetry();  // re-check afterwards (nothing left to do if the start downloaded the build)
    emit statusChanged();
    startNext();
  }
}

void CoreUpdater::reset() {
  retryTimer_.stop();
  queue_.clear();
  failedCores_.clear();
  systems_.clear();
  failures_ = 0;
  if (active_) {
    prov_.cancel();
    active_ = false;
    emit statusChanged();
  }
}

}  // namespace framebeam
