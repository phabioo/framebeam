#include "pluginhub.h"

#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

#include "semver.h"

namespace framebeam {

Q_LOGGING_CATEGORY(lcPluginHub, "framebeam.plugins.hub")

namespace {

bool validId(const QString& id) {
  static const QRegularExpression re(QStringLiteral("^[a-z0-9-]+(\\.[a-z0-9-]+)+$"));
  return re.match(id).hasMatch();
}

bool validHash(const QString& h) {
  static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
  return re.match(h).hasMatch();
}

QStringList strings(const QJsonValue& v) {
  QStringList out;
  for (const QJsonValue& e : v.toArray())
    if (e.isString()) out << e.toString();
  return out;
}

}  // namespace

std::optional<HubPluginInfo> parseHubPluginInfo(const QJsonObject& o) {
  HubPluginInfo p;
  p.id = o.value(QStringLiteral("plugin_id")).toString();
  p.version = o.value(QStringLiteral("version")).toString();
  p.sha256 = o.value(QStringLiteral("sha256")).toString();
  p.origin = o.value(QStringLiteral("origin")).toString();
  if (!validId(p.id) || !update::SemVer::parse(p.version) || !validHash(p.sha256)) return std::nullopt;
  if (p.origin != QLatin1String("official") && p.origin != QLatin1String("unverified")) return std::nullopt;
  p.name = o.value(QStringLiteral("name")).toString();
  if (p.name.isEmpty()) p.name = p.id;
  p.description = o.value(QStringLiteral("description")).toString();
  p.api = o.value(QStringLiteral("api")).toInt(1);
  p.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(0));
  p.permissions = strings(o.value(QStringLiteral("permissions")));
  p.requires_ = o.value(QStringLiteral("requires")).toObject();
  p.provides = strings(o.value(QStringLiteral("provides")));
  p.games = o.value(QStringLiteral("games")).toArray();
  p.createdAt = o.value(QStringLiteral("created_at")).toString();
  return p;
}

PluginHubClient::PluginHubClient(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  qRegisterMetaType<framebeam::PluginDownloadResult>("framebeam::PluginDownloadResult");
  connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s != HubConnection::State::Connected) {
      ++generation_;
      const bool changed = state_ != State::Idle;
      state_ = State::Idle;
      plugins_.clear();
      error_.clear();
      if (changed) emit stateChanged();
    }
  });
}

bool PluginHubClient::supported() const {
  return conn_ && conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QStringLiteral("plugins_v1"));
}

void PluginHubClient::refresh() {
  if (!supported()) return;
  QNetworkReply* reply = conn_->authorizedGet(QStringLiteral("/plugins"));
  if (reply == nullptr) return;
  state_ = State::Loading;
  error_.clear();
  emit stateChanged();
  const quint64 gen = ++generation_;
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (gen != generation_) return;
    if (r.status == 401 && conn_) conn_->noteUnauthorized();
    if (!r.ok()) {
      // The previous list stays: a failed refresh never empties what the user already saw.
      state_ = State::Failed;
      error_ = r.networkError ? r.errorString : (r.apiErrorMessage.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.apiErrorMessage);
      emit stateChanged();
      return;
    }
    QList<HubPluginInfo> list;
    for (const QJsonValue& v : r.json().value(QStringLiteral("plugins")).toArray())
      if (const auto p = parseHubPluginInfo(v.toObject())) list.append(*p);
    plugins_ = list;
    state_ = State::Ready;
    emit stateChanged();
  });
}

void PluginHubClient::download(const QString& pluginId, const QString& version, const QString& sha256, qint64 expectedSize) {
  const auto finishLater = [this](PluginDownloadResult res) {
    QTimer::singleShot(0, this, [this, res]() { emit downloadFinished(res); });  // always asynchronous
  };
  PluginDownloadResult res;
  res.id = pluginId;
  res.version = version;
  res.sha256 = sha256.toLower();
  if (!validId(pluginId) || !update::SemVer::parse(version) || !validHash(sha256.toLower())) {
    res.problem = QStringLiteral("not_on_hub");
    res.detail = QStringLiteral("invalid plugin id, version or hash");
    finishLater(res);
    return;
  }
  if (!supported()) {
    res.problem = QStringLiteral("not_supported");
    res.detail = QStringLiteral("this Hub does not offer plugins");
    finishLater(res);
    return;
  }
  QNetworkReply* reply = conn_->authorizedGet(QStringLiteral("/plugins/%1/%2/file").arg(pluginId, version));
  if (reply == nullptr) {
    res.problem = QStringLiteral("download_failed");
    res.detail = QStringLiteral("Not connected to the Hub");
    finishLater(res);
    return;
  }
  const qint64 cap = expectedSize > 0 ? (std::min)(expectedSize, kMaxPluginDownloadBytes) : kMaxPluginDownloadBytes;
  connect(reply, &QNetworkReply::downloadProgress, reply, [reply, cap](qint64 got, qint64) {
    if (got > cap) reply->abort();  // more than announced: not the file we validate against
  });
  const QString pin = sha256.toLower();
  connect(reply, &QNetworkReply::finished, this, [this, reply, res, pin, expectedSize]() mutable {
    const HttpResult r = HubHttp::resultOf(reply);
    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError;
    reply->deleteLater();
    if (r.status == 401 && conn_) conn_->noteUnauthorized();
    if (r.status == 404) {
      res.problem = QStringLiteral("not_on_hub");
      res.detail = QStringLiteral("the Hub does not offer %1 %2").arg(res.id, res.version);
    } else if (!r.ok()) {
      res.problem = aborted ? QStringLiteral("too_large") : QStringLiteral("download_failed");
      res.detail = r.networkError ? r.errorString : (r.apiErrorCode.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.apiErrorCode);
    } else if (r.body.size() > kMaxPluginDownloadBytes) {
      res.problem = QStringLiteral("too_large");
    } else if (expectedSize > 0 && r.body.size() != expectedSize) {
      res.problem = QStringLiteral("invalid_size");
    } else if (QString::fromLatin1(QCryptographicHash::hash(r.body, QCryptographicHash::Sha256).toHex()) != pin) {
      res.problem = QStringLiteral("invalid_hash");
      res.detail = QStringLiteral("the file does not match the SHA-256 the Hub announced");
    } else {
      res.ok = true;
      res.bytes = r.body;
    }
    if (!res.ok) qCWarning(lcPluginHub) << "download" << res.id << res.version << "failed:" << res.problem;
    emit downloadFinished(res);
  });
}

}  // namespace framebeam
