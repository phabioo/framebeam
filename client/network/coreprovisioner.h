#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include "corecache.h"
#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

struct CoreResult {
  QString coreId;
  QString version;
  bool ok = false;
  QString libraryPath;  // validated library in the core cache (ok only)
  bool downloaded = false;  // at least one file came from the Hub (false = everything was already cached)
  // Reason when not ok: "not_on_hub" | "incompatible" | "not_cached_on_hub" | "download_failed" | "invalid_size" |
  // "invalid_hash"
  QString problem;
  QString detail;  // never contains file contents
};

// Makes a core package of the Hub available in the core cache (feature cores_v1):
// GET /cores/{core_id}/packages/{version}/{platform} for the platform of this build, then
// GET .../files/{name} for every file that is not yet valid in the cache. Files are checked against the size and
// SHA-256 of the metadata before they enter the cache. Only ids and hashes are logged.
class CoreProvisioner : public QObject {
  Q_OBJECT
 public:
  CoreProvisioner(HubConnection* connection, CoreCache* cache, QObject* parent = nullptr);

  // For tests: platform of the request (default: CoreCache::currentPlatform()).
  void setPlatform(const QString& platform) { platform_ = platform; }
  QString platform() const { return platform_; }

  bool busy() const { return busy_; }
  // version = SystemInfo::corePackageVersion. Result via finished().
  void prepare(const QString& coreId, const QString& version);

 signals:
  void finished(const framebeam::CoreResult& result);

 private:
  void fail(const QString& reason, const QString& detail);
  void probeOtherPlatforms(quint64 gen, QStringList remaining);
  void onPackage(const HttpResult& r);
  void next();
  void done();

  QPointer<HubConnection> conn_;
  CoreCache* cache_;
  QString platform_;
  bool busy_ = false;
  quint64 generation_ = 0;
  CoreResult result_;
  CorePackageInfo pkg_;
  QList<CorePackageFile> queue_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::CoreResult)
