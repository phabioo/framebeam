#include "pluginhub.h"

#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <algorithm>

#include "semver.h"

namespace uzume {

Q_LOGGING_CATEGORY(lcPluginHub, "uzume.plugins.hub")

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

QString parseRunSettings(const QJsonValue& v, QList<RunSetting>& out) {
  static const QRegularExpression seg(QStringLiteral("^[a-z][a-z0-9_]{0,31}$"));
  static const QRegularExpression i18n(QStringLiteral("^[A-Za-z0-9_.-]{1,64}$"));
  out.clear();
  if (!v.isArray()) return QStringLiteral("run_settings must be an array");
  const QJsonArray arr = v.toArray();
  if (arr.isEmpty() || arr.size() > 32) return QStringLiteral("run_settings must have 1 to 32 entries");
  QList<RunSetting> list;
  QSet<QString> keys;
  for (int i = 0; i < arr.size(); ++i) {
    if (!arr.at(i).isObject()) return QStringLiteral("run_settings[%1] must be an object").arg(i);
    const QJsonObject o = arr.at(i).toObject();
    RunSetting s;
    const QJsonValue kv = o.value(QStringLiteral("key"));
    if (!kv.isString()) return QStringLiteral("run_settings[%1]: 'key' is required").arg(i);
    s.key = kv.toString();
    const QString at = QStringLiteral("run_settings '%1'").arg(s.key.left(64));
    const QStringList parts = s.key.split(QLatin1Char('.'));
    if (parts.size() > 2) return QStringLiteral("%1: key may have at most 2 dot-separated segments").arg(at);
    for (const QString& p : parts)
      if (!seg.match(p).hasMatch()) return QStringLiteral("%1: invalid key (each segment must match [a-z][a-z0-9_]{0,31})").arg(at);
    if (keys.contains(s.key)) return QStringLiteral("%1: duplicate key").arg(at);
    keys.insert(s.key);
    const QJsonValue tv = o.value(QStringLiteral("type"));
    s.type = tv.toString();
    if (!tv.isString() || (s.type != QLatin1String("boolean") && s.type != QLatin1String("enum")))
      return QStringLiteral("%1: type must be \"boolean\" or \"enum\"").arg(at);
    const QJsonValue lv = o.value(QStringLiteral("label"));
    if (!lv.isString() || !i18n.match(lv.toString()).hasMatch()) return QStringLiteral("%1: 'label' must be an i18n key ([A-Za-z0-9_.-], 1 to 64 characters)").arg(at);
    s.label = lv.toString();
    const QJsonValue hv = o.value(QStringLiteral("hint"));
    if (!hv.isUndefined() && !hv.isNull()) {
      if (!hv.isString() || !i18n.match(hv.toString()).hasMatch()) return QStringLiteral("%1: 'hint' must be an i18n key ([A-Za-z0-9_.-], 1 to 64 characters)").arg(at);
      s.hint = hv.toString();
    }
    const QJsonValue ov = o.value(QStringLiteral("options"));
    const QJsonValue dv = o.value(QStringLiteral("default"));
    if (s.type == QLatin1String("boolean")) {
      if (!ov.isUndefined() && !ov.isNull()) return QStringLiteral("%1: 'options' is only allowed for enum").arg(at);
      if (!dv.isBool()) return QStringLiteral("%1: 'default' must be true or false").arg(at);
      s.def = dv.toBool();
    } else {
      if (!ov.isArray() || ov.toArray().size() < 2 || ov.toArray().size() > 8) return QStringLiteral("%1: enum needs 2 to 8 options").arg(at);
      QSet<QString> vals;
      for (const QJsonValue& e : ov.toArray()) {
        const QJsonObject eo = e.toObject();
        if (!e.isObject() || !eo.value(QStringLiteral("value")).isString() || !seg.match(eo.value(QStringLiteral("value")).toString()).hasMatch())
          return QStringLiteral("%1: option 'value' must match [a-z][a-z0-9_]{0,31}").arg(at);
        if (!eo.value(QStringLiteral("label")).isString() || !i18n.match(eo.value(QStringLiteral("label")).toString()).hasMatch())
          return QStringLiteral("%1: option 'label' must be an i18n key ([A-Za-z0-9_.-], 1 to 64 characters)").arg(at);
        const QString val = eo.value(QStringLiteral("value")).toString();
        if (vals.contains(val)) return QStringLiteral("%1: duplicate option value '%2'").arg(at, val);
        vals.insert(val);
        s.options.append({val, eo.value(QStringLiteral("label")).toString()});
      }
      if (!dv.isString() || !vals.contains(dv.toString())) return QStringLiteral("%1: 'default' must be one of the option values").arg(at);
      s.def = dv.toString();
    }
    list.append(s);
  }
  // No key may be a segment-prefix of another ("rules" and "rules.dupes").
  for (const QString& a : std::as_const(keys))
    for (const QString& b : std::as_const(keys))
      if (b.startsWith(a + QLatin1Char('.'))) return QStringLiteral("run_settings: key '%1' is a prefix of key '%2'").arg(a, b);
  out = list;
  return {};
}

QJsonObject buildRunSettings(const QList<RunSetting>& settings, const QVariantMap& values) {
  QJsonObject root;
  for (const RunSetting& s : settings) {
    const QVariant v = values.contains(s.key) ? values.value(s.key) : s.def;
    const QJsonValue jv = s.type == QLatin1String("boolean") ? QJsonValue(v.toBool()) : QJsonValue(v.toString());
    const QStringList parts = s.key.split(QLatin1Char('.'));
    if (parts.size() == 1) {
      root.insert(parts.at(0), jv);
    } else {
      QJsonObject sub = root.value(parts.at(0)).toObject();
      sub.insert(parts.at(1), jv);
      root.insert(parts.at(0), sub);
    }
  }
  return root;
}

QString runSettingText(const QJsonObject& strings, const QString& lang, const QString& key) {
  for (const QString& l : {lang, QStringLiteral("en")}) {
    const QJsonValue v = strings.value(l).toObject().value(key);
    if (v.isString() && !v.toString().isEmpty()) return v.toString();
  }
  return key;
}

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
  const QJsonValue rs = o.value(QStringLiteral("run_settings"));
  if (!rs.isUndefined() && !rs.isNull()) {
    if (const QString e = parseRunSettings(rs, p.runSettings); !e.isEmpty()) {
      qCWarning(lcPluginHub) << "plugin" << p.id << p.version << "has invalid run_settings, ignored:" << e;
      p.runSettings.clear();
    }
  }
  if (!p.runSettings.isEmpty()) p.runSettingsStrings = o.value(QStringLiteral("run_settings_strings")).toObject();
  p.createdAt = o.value(QStringLiteral("created_at")).toString();
  return p;
}

PluginHubClient::PluginHubClient(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  qRegisterMetaType<uzume::PluginDownloadResult>("uzume::PluginDownloadResult");
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
    res.detail = QStringLiteral("this server does not offer plugins");
    finishLater(res);
    return;
  }
  QNetworkReply* reply = conn_->authorizedGet(QStringLiteral("/plugins/%1/%2/file").arg(pluginId, version));
  if (reply == nullptr) {
    res.problem = QStringLiteral("download_failed");
    res.detail = QStringLiteral("Not connected to the server");
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
      res.detail = QStringLiteral("the server does not offer %1 %2").arg(res.id, res.version);
    } else if (!r.ok()) {
      res.problem = aborted ? QStringLiteral("too_large") : QStringLiteral("download_failed");
      res.detail = r.networkError ? r.errorString : (r.apiErrorCode.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.apiErrorCode);
    } else if (r.body.size() > kMaxPluginDownloadBytes) {
      res.problem = QStringLiteral("too_large");
    } else if (expectedSize > 0 && r.body.size() != expectedSize) {
      res.problem = QStringLiteral("invalid_size");
    } else if (QString::fromLatin1(QCryptographicHash::hash(r.body, QCryptographicHash::Sha256).toHex()) != pin) {
      res.problem = QStringLiteral("invalid_hash");
      res.detail = QStringLiteral("the file does not match the SHA-256 the server announced");
    } else {
      res.ok = true;
      res.bytes = r.body;
    }
    if (!res.ok) qCWarning(lcPluginHub) << "download" << res.id << res.version << "failed:" << res.problem;
    emit downloadFinished(res);
  });
}

}  // namespace uzume
