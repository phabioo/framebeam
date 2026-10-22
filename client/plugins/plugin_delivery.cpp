#include "plugin_delivery.h"

#include <QLoggingCategory>
#include <QTimer>

namespace uzume::plugins {

Q_LOGGING_CATEGORY(lcDelivery, "uzume.plugins.delivery")

namespace {

QString problemText(const PluginDownloadResult& r) {
  if (r.problem == QLatin1String("invalid_hash") || r.problem == QLatin1String("invalid_size"))
    return QStringLiteral("The downloaded file does not match what the Hub announced. Nothing was installed.");
  if (r.problem == QLatin1String("too_large")) return QStringLiteral("The plugin file from the Hub is too large. Nothing was installed.");
  if (r.problem == QLatin1String("not_on_hub")) return QStringLiteral("The Hub does not offer this plugin version (any more).");
  if (r.problem == QLatin1String("not_supported")) return QStringLiteral("This Hub does not offer plugins.");
  return QStringLiteral("Could not download the plugin: %1").arg(r.detail.isEmpty() ? QStringLiteral("connection problem") : r.detail);
}

}  // namespace

PluginDelivery::PluginDelivery(PluginManager* manager, PluginHubClient* hub, QObject* parent)
    : QObject(parent), manager_(manager), hub_(hub) {
  qRegisterMetaType<uzume::plugins::DeliveryResult>("uzume::plugins::DeliveryResult");
  connect(hub_, &PluginHubClient::downloadFinished, this, &PluginDelivery::onDownloaded);
}

QString PluginDelivery::keyOf(const QString& id, const QString& version, const QString& sha) {
  return id + QLatin1Char('|') + version + QLatin1Char('|') + sha.toLower();
}

QString PluginDelivery::presentHubCopy(const QString& pluginId, const QString& version, const QString& sha256) const {
  return manager_->hubCopyDir(pluginId, version, sha256);
}

void PluginDelivery::ensureHubCopy(const QString& pluginId, const QString& version, const QString& sha256, Callback done) {
  const QString dir = manager_->hubCopyDir(pluginId, version, sha256);
  if (!dir.isEmpty()) {
    DeliveryResult r;
    r.ok = true;
    r.id = pluginId;
    r.version = version;
    r.sha256 = sha256.toLower();
    r.dir = dir;
    for (const PluginInfo& c : manager_->hubCopies())
      if (c.path == dir) r.origin = c.origin;
    QTimer::singleShot(0, this, [done, r]() { done(r); });
    return;
  }
  fetch(pluginId, version, sha256, 0, std::move(done));
}

void PluginDelivery::install(const QString& pluginId, const QString& version) {
  const auto emitFail = [this, pluginId, version](const QString& problem, const QString& error) {
    DeliveryResult r;
    r.id = pluginId;
    r.version = version;
    r.problem = problem;
    r.error = error;
    QTimer::singleShot(0, this, [this, r]() { emit finished(r); });
  };
  const HubPluginInfo* offered = nullptr;
  for (const HubPluginInfo& p : hub_->plugins())
    if (p.id == pluginId && p.version == version) offered = &p;
  if (offered == nullptr) {
    emitFail(QStringLiteral("not_on_hub"), QStringLiteral("The Hub does not offer %1 %2.").arg(pluginId, version));
    return;
  }
  fetch(pluginId, version, offered->sha256, offered->size, [this](const DeliveryResult& r) { emit finished(r); });
}

void PluginDelivery::fetch(const QString& pluginId, const QString& version, const QString& sha256, qint64 size, Callback done) {
  const QString key = keyOf(pluginId, version, sha256);
  const bool inFlight = pending_.contains(key);
  pending_[key].append(std::move(done));
  if (!inFlight) hub_->download(pluginId, version, sha256, size);
}

void PluginDelivery::onDownloaded(const PluginDownloadResult& r) {
  const QString key = keyOf(r.id, r.version, r.sha256);
  if (!pending_.contains(key)) return;  // not ours
  const QList<Callback> callbacks = pending_.take(key);
  DeliveryResult out;
  out.id = r.id;
  out.version = r.version;
  out.sha256 = r.sha256;
  if (!r.ok) {
    out.problem = r.problem;
    out.error = problemText(r);
  } else {
    // Validation, signature and the atomic install; on any failure nothing on disk changed.
    const InstallResult ir = manager_->installHubCopy(r.bytes, r.id, r.version, r.sha256);
    if (!ir.ok) {
      out.problem = QStringLiteral("invalid_plugin");
      out.error = QStringLiteral("Could not install: %1").arg(ir.error);
    } else {
      out.ok = true;
      out.downloaded = true;
      out.origin = ir.origin;
      out.signatureNote = ir.signatureNote;
      out.addedPermissions = ir.addedPermissions;
      out.dir = manager_->hubCopyDir(r.id, r.version, r.sha256);
    }
  }
  if (!out.ok) qCWarning(lcDelivery) << "plugin" << r.id << r.version << "not installed:" << out.problem;
  for (const Callback& cb : callbacks) cb(out);
}

}  // namespace uzume::plugins
