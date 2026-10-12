#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <functional>

#include "corecache.h"
#include "coreprovisioner.h"
#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

// Keeps the cores the user already has locally up to date without a game start (roadmap 0.10.x). check() takes the
// Hub's systems list: for every core with a complete cached package whose Hub version is newer than the newest cached
// one, the Hub's build is downloaded in the background, one core at a time, through its own CoreProvisioner (same
// SHA-256 / size checks and atomic cache writes as the on-demand path at game start, which stays the fallback).
// Cores that were never cached are never fetched. A running game keeps its loaded core: the new build is only staged
// in the cache and the running version is protected from pruning. A failed download is logged and retried later
// (backoff, at most 30 min). Nothing happens while the Hub connection is not up. No dialogs; the UI shows
// current()/lastUpdate().
class CoreUpdater : public QObject {
  Q_OBJECT
 public:
  struct Candidate {
    QString coreId;
    QString name;
    QString version;  // the Hub's version
  };

  CoreUpdater(HubConnection* connection, CoreCache* cache, QObject* parent = nullptr);

  // For tests: platform of the requests (default: CoreCache::currentPlatform()).
  void setPlatform(const QString& platform);
  void setRetryIntervalMs(int ms) { retryBaseMs_ = ms; }
  // Version of a core a running game has loaded (empty = none); never pruned.
  void setInUseProvider(std::function<QString(const QString& coreId)> fn);

  // Cores that would be updated for this systems list (pure; used by check() and tests).
  QList<Candidate> candidates(const QList<SystemInfo>& systems) const;
  // New Hub info: (re)builds the queue and starts downloading. Remembers the list for retries.
  void check(const QList<SystemInfo>& systems);
  // The on-demand download at game start takes over for this core: abort a running background download of it.
  void cancelFor(const QString& coreId);

  bool busy() const { return active_; }
  const Candidate& current() const { return current_; }        // valid while busy()
  const Candidate& lastUpdate() const { return lastUpdate_; }  // last finished update (empty coreId = none yet)
  int failedCount() const { return failures_; }

 signals:
  void statusChanged();
  // A newer build is complete in the cache (staged; used from the next game start).
  void updated(const framebeam::CoreUpdater::Candidate& c, const framebeam::CoreResult& r);

 private:
  void startNext();
  void onFinished(const CoreResult& r);
  void scheduleRetry();
  void reset();

  QPointer<HubConnection> conn_;
  CoreCache* cache_;
  CoreProvisioner prov_;
  QString platform_;
  QList<SystemInfo> systems_;
  QList<Candidate> queue_;
  QSet<QString> failedCores_;  // skipped until the next retry
  Candidate current_;
  Candidate lastUpdate_;
  bool active_ = false;
  int failures_ = 0;
  int retryBaseMs_ = 5 * 60 * 1000;
  QTimer retryTimer_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::CoreUpdater::Candidate)
