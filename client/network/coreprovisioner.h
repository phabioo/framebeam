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
  // "invalid_hash" | "untrusted" (signed core index check failed, ADR 0012 D6; nothing is installed)
  QString problem;
  QString detail;  // never contains file contents
};

// Makes a core package of the Hub available in the core cache (feature cores_v1):
// GET /cores/{core_id}/packages/{version}/{platform} for the platform of this build, then
// GET .../files/{name} for every file that is not yet valid in the cache. Files are checked against the size and
// SHA-256 of the metadata before they enter the cache. Only ids and hashes are logged.
// With the Hub feature cores_index_v1 the package metadata is first verified against the signed core index
// (GET /cores/index + /cores/index.sig, Ed25519 with the compiled-in keys plus FRAMEBEAM_PLAYER_TRUST_KEYS); a failed
// check ends with problem "untrusted" before any file is downloaded or installed. Without the feature: old behavior
// (size and SHA-256 from the Hub) plus a warning in the log.
class CoreProvisioner : public QObject {
  Q_OBJECT
 public:
  CoreProvisioner(HubConnection* connection, CoreCache* cache, QObject* parent = nullptr);

  // For tests: platform of the request (default: CoreCache::currentPlatform()).
  void setPlatform(const QString& platform) { platform_ = platform; }
  QString platform() const { return platform_; }

  // For tests: trusted public keys (raw 32 bytes) instead of update::trustedKeysFromEnvironment().
  void setTrustedKeys(const QList<QByteArray>& keys) {
    trustedKeys_ = keys;
    keysOverridden_ = true;
  }

  bool busy() const { return busy_; }
  // version = SystemInfo::corePackageVersion. Result via finished().
  void prepare(const QString& coreId, const QString& version);

 signals:
  void finished(const framebeam::CoreResult& result);

 private:
  void fail(const QString& reason, const QString& detail);
  void probeOtherPlatforms(quint64 gen, QStringList remaining);
  void onPackage(const HttpResult& r);
  void verifyIndex(quint64 gen);
  void startDownloads();
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
  QList<QByteArray> trustedKeys_;
  bool keysOverridden_ = false;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::CoreResult)
