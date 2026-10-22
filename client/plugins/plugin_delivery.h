#pragma once
// Plugin delivery from the Hub: downloads a plugin file with its SHA-256 pin, hands the bytes to the PluginManager
// (zip validation, signature, atomic install under hub/<id>/<version>/) and reports the result. Nothing is installed
// when the download fails or does not match the pin; a failed update leaves the previous copy as it was.
#include <QHash>
#include <QObject>
#include <functional>

#include "plugin_manager.h"
#include "pluginhub.h"

namespace uzume::plugins {

struct DeliveryResult {
  bool ok = false;
  QString id;
  QString version;
  QString sha256;
  QString dir;                    // the Hub copy's directory (when ok)
  QString origin;                 // "official" | "unverified" (when ok)
  QString problem;                // not_supported | not_on_hub | invalid_hash | invalid_size | too_large | download_failed | invalid_plugin
  QString error;                  // clear English message when !ok
  QString signatureNote;          // present but invalid signature (the plugin is then "unverified")
  QStringList addedPermissions;   // the update asks for new permissions: the plugin was turned off
  bool downloaded = false;        // false: the exact copy was already installed
};

class PluginDelivery : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(const DeliveryResult&)>;

  PluginDelivery(PluginManager* manager, PluginHubClient* hub, QObject* parent = nullptr);

  // The directory of the Hub copy (id, version) installed from exactly the file with `sha256`; empty if not present.
  QString presentHubCopy(const QString& pluginId, const QString& version, const QString& sha256) const;

  // For runs: makes sure that exact Hub copy is present (downloads it if missing) and calls `done` (always
  // asynchronously, on the thread of this object) with its directory. A local copy never counts. Concurrent calls for the
  // same (id, version, sha256) share one download.
  void ensureHubCopy(const QString& pluginId, const QString& version, const QString& sha256, Callback done);

  // Settings -> Plugins: installs or updates an offered version; the pin comes from the Hub's list
  // (hub()->plugins()). Emits finished().
  void install(const QString& pluginId, const QString& version);

 signals:
  void finished(const uzume::plugins::DeliveryResult& result);  // result of install()

 private:
  void fetch(const QString& pluginId, const QString& version, const QString& sha256, qint64 size, Callback done);
  void onDownloaded(const PluginDownloadResult& r);
  static QString keyOf(const QString& id, const QString& version, const QString& sha);

  PluginManager* manager_;
  PluginHubClient* hub_;
  QHash<QString, QList<Callback>> pending_;
};

}  // namespace uzume::plugins

Q_DECLARE_METATYPE(uzume::plugins::DeliveryResult)
