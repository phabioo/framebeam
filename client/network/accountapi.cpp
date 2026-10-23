#include "accountapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

namespace uzume {

namespace {

QDateTime parseTime(const QJsonValue& v) {
  return v.isString() ? QDateTime::fromString(v.toString(), Qt::ISODate) : QDateTime();
}

}  // namespace

std::optional<AccountInfo> parseAccount(const QJsonObject& o) {
  AccountInfo a;
  a.userId = o.value(QStringLiteral("user_id")).toString();
  a.username = o.value(QStringLiteral("username")).toString();
  a.displayName = o.value(QStringLiteral("display_name")).toString();
  a.role = o.value(QStringLiteral("role")).toString();
  a.hasPassword = o.value(QStringLiteral("has_password")).toBool(false);
  a.passwordRequired = o.value(QStringLiteral("password_required")).toBool(false);
  a.mustChangePassword = o.value(QStringLiteral("must_change_password")).toBool(false);
  a.confirmedUntil = parseTime(o.value(QStringLiteral("confirmed_until")));
  a.confirmLockedUntil = parseTime(o.value(QStringLiteral("confirm_locked_until")));
  a.playSeconds = qMax<qint64>(0, o.value(QStringLiteral("play_seconds")).toVariant().toLongLong());
  a.sessions = qMax(0, o.value(QStringLiteral("sessions")).toInt(0));
  a.lastPlayedAt = parseTime(o.value(QStringLiteral("last_played_at")));
  if (a.userId.isEmpty()) {
    return std::nullopt;
  }
  return a;
}

std::optional<AccountDeviceInfo> parseAccountDevice(const QJsonObject& o) {
  AccountDeviceInfo d;
  d.deviceId = o.value(QStringLiteral("device_id")).toString();
  d.name = o.value(QStringLiteral("name")).toString();
  d.platform = o.value(QStringLiteral("platform")).toString();
  d.arch = o.value(QStringLiteral("arch")).toString();
  d.appVersion = o.value(QStringLiteral("app_version")).toString();
  d.createdAt = parseTime(o.value(QStringLiteral("created_at")));
  d.lastSeenAt = parseTime(o.value(QStringLiteral("last_seen_at")));
  d.signInKind = o.value(QStringLiteral("sign_in_kind")).toString();
  d.status = o.value(QStringLiteral("status")).toString();
  d.installId = o.value(QStringLiteral("install_id")).toString();
  d.current = o.value(QStringLiteral("current")).toBool(false);
  if (d.deviceId.isEmpty()) {
    return std::nullopt;
  }
  return d;
}

AccountApi::AccountApi(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {}

void AccountApi::request(const QByteArray& method, const QString& path, const QJsonObject& body, bool hasBody, Expect expect,
                         Callback cb) {
  QNetworkReply* reply = nullptr;
  if (conn_) {
    if (method == "GET") {
      reply = conn_->authorizedGet(path);
    } else {
      reply = conn_->authorizedSend(method, path, hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray(), {},
                                    "application/json");
    }
  }
  if (reply == nullptr) {
    QTimer::singleShot(0, this, [cb = std::move(cb)]() {
      AccountApiResult r;
      r.errorCode = QStringLiteral("not_connected");
      cb(r);
    });
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, expect, cb = std::move(cb)]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    AccountApiResult res;
    res.status = r.status;
    res.errorMessage = r.apiErrorMessage;
    res.retryAfter = r.retryAfterSec;
    if (r.networkError || r.certMismatch) {
      res.errorCode = QStringLiteral("unreachable");
      res.errorMessage = r.errorString;
    } else if (r.status == 401 && r.apiErrorCode != QLatin1String("invalid_credentials")) {
      // The access token is stale: renew it; the caller may try again.
      if (conn_) {
        conn_->noteUnauthorized();
      }
      res.errorCode = r.apiErrorCode.isEmpty() ? QStringLiteral("unauthorized") : r.apiErrorCode;
    } else if (!r.ok()) {
      res.errorCode = r.apiErrorCode.isEmpty() ? QStringLiteral("request_failed") : r.apiErrorCode;
    } else {
      res.ok = true;
      const QJsonObject o = r.json();
      switch (expect) {
        case Expect::None:
          break;
        case Expect::Account:
          if (const auto a = parseAccount(o)) {
            res.account = *a;
          } else {
            res.ok = false;
            res.errorCode = QStringLiteral("bad_response");
          }
          break;
        case Expect::Devices:
          for (const QJsonValue& v : o.value(QStringLiteral("devices")).toArray()) {
            if (const auto d = parseAccountDevice(v.toObject())) {
              res.devices.append(*d);
            }
          }
          break;
        case Expect::SignedOut:
          res.signedOut = o.value(QStringLiteral("signed_out")).toInt(0);
          break;
      }
    }
    cb(res);
  });
}

void AccountApi::getAccount(Callback cb) {
  request("GET", QStringLiteral("/account"), {}, false, Expect::Account, std::move(cb));
}

void AccountApi::updateDisplayName(const QString& displayName, Callback cb) {
  request("PATCH", QStringLiteral("/account"), QJsonObject{{QStringLiteral("display_name"), displayName.trimmed()}}, true,
          Expect::Account, std::move(cb));
}

void AccountApi::setPassword(const QString& currentPassword, const QString& newPassword, Callback cb) {
  QJsonObject body{{QStringLiteral("new_password"), newPassword}};
  if (!currentPassword.isEmpty()) {
    body.insert(QStringLiteral("current_password"), currentPassword);
  }
  request("PUT", QStringLiteral("/account/password"), body, true, Expect::None, std::move(cb));
}

void AccountApi::removePassword(const QString& currentPassword, Callback cb) {
  request("DELETE", QStringLiteral("/account/password"), QJsonObject{{QStringLiteral("current_password"), currentPassword}}, true,
          Expect::None, std::move(cb));
}

void AccountApi::listDevices(Callback cb) {
  request("GET", QStringLiteral("/account/devices"), {}, false, Expect::Devices, std::move(cb));
}

void AccountApi::signOutDevice(const QString& deviceId, Callback cb) {
  request("DELETE", QStringLiteral("/account/devices/") + QString::fromLatin1(QUrl::toPercentEncoding(deviceId)), {}, false,
          Expect::None, std::move(cb));
}

void AccountApi::signOutAll(bool includeCurrent, Callback cb) {
  request("POST", QStringLiteral("/account/devices/sign-out-all"), QJsonObject{{QStringLiteral("include_current"), includeCurrent}},
          true, Expect::SignedOut, std::move(cb));
}

}  // namespace uzume
