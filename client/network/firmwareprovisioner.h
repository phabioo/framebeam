#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include "firmwarecache.h"
#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

struct FirmwareProblem {
  QString fileId;
  QString displayName;
  QString reason;  // "missing_on_hub" | "download_failed" | "invalid_size" | "invalid_hash"
  QString detail;  // never contains file contents
};

struct FirmwareResult {
  QString systemId;
  bool ok = false;                  // every required file is available and validated
  QMap<QString, QString> pathsById;  // file id -> path in the firmware cache (validated files only)
  QList<FirmwareProblem> problems;
};

// Makes the firmware files of a system available in the cache (GET /systems/{system}/firmware/{file}). Only for
// firmware mode "native"; in "builtin" nothing is downloaded (the caller does not call prepare()). Files are checked
// against the size and SHA-256 the Hub reports before they enter the cache. Only ids and hashes are logged.
class FirmwareProvisioner : public QObject {
  Q_OBJECT
 public:
  FirmwareProvisioner(HubConnection* connection, FirmwareCache* cache, QObject* parent = nullptr);

  bool busy() const { return busy_; }
  // wantedIds: file ids the core can use (from the manifest). Required files that the Hub lacks or that fail
  // validation make the result not ok; optional ones are skipped silently.
  void prepare(const SystemInfo& system, const QStringList& wantedIds);

  // Without network: required files the Hub reports as missing (for the detail pane).
  static QList<FirmwareProblem> missingOnHub(const SystemInfo& system, const QStringList& wantedIds);

 signals:
  void finished(const framebeam::FirmwareResult& result);

 private:
  void next();
  void done();

  QPointer<HubConnection> conn_;
  FirmwareCache* cache_;
  bool busy_ = false;
  quint64 generation_ = 0;
  SystemInfo system_;
  QList<FirmwareFileInfo> queue_;
  FirmwareResult result_;
};

}  // namespace framebeam

Q_DECLARE_METATYPE(framebeam::FirmwareResult)
