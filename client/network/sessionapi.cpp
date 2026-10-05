#include "sessionapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

namespace framebeam {

namespace {
QString enc(const QString& s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }
}  // namespace

SessionApi::SessionApi(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {}

QString SessionApi::sessionPath(const QString& sessionId) { return QStringLiteral("/sessions/") + enc(sessionId); }

void SessionApi::request(const QByteArray& method, const QString& path, const QJsonObject& body, bool hasBody, Expect expect,
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
      SessionApiResult r;
      r.errorCode = QStringLiteral("not_connected");
      cb(r);
    });
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, expect, cb = std::move(cb)]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    using K = SessionApiResult::Kind;
    SessionApiResult res;
    res.status = r.status;
    res.errorCode = r.apiErrorCode;
    res.errorMessage = r.apiErrorMessage;
    if (r.networkError || r.certMismatch) {
      res.kind = K::Offline;
      res.errorCode = QStringLiteral("unreachable");
      res.errorMessage = r.errorString;
    } else if (r.status == 401) {
      if (conn_) {
        conn_->noteUnauthorized();
      }
      res.kind = K::Offline;
    } else if (r.status >= 500 || r.status == 429) {
      res.kind = K::Offline;
    } else if (r.status == 404) {
      res.kind = K::NotFound;
    } else if (r.status == 403) {
      res.kind = K::Forbidden;
    } else if (r.status == 410) {
      res.kind = K::Ended;
    } else if (r.status == 409) {
      if (r.apiErrorCode == QLatin1String("session_full")) {
        res.kind = K::Full;
      } else if (r.apiErrorCode == QLatin1String("capability_missing")) {
        res.kind = K::CapabilityMissing;
      } else {
        res.kind = K::Rejected;
      }
    } else if (!r.ok()) {
      res.kind = K::Rejected;
    } else {
      res.kind = K::Ok;
      switch (expect) {
        case Expect::None:
          break;
        case Expect::Session:
          res.session = parseSession(r.json());
          if (!res.session) {
            res.kind = K::BadResponse;
          }
          break;
        case Expect::SessionList:
          for (const QJsonValue& v : r.json().value(QStringLiteral("sessions")).toArray()) {
            if (const auto s = parseSession(v.toObject())) {
              res.sessions.append(*s);
            }
          }
          break;
        case Expect::Join:
          res.join = parseJoinInfo(r.json());
          if (!res.join) {
            res.kind = K::BadResponse;
          }
          break;
        case Expect::Users:
          for (const QJsonValue& v : r.json().value(QStringLiteral("users")).toArray()) {
            if (const auto u = parseHubUser(v.toObject())) {
              res.users.append(*u);
            }
          }
          break;
      }
    }
    cb(res);
  });
}

void SessionApi::list(Callback cb) { request("GET", QStringLiteral("/sessions"), {}, false, Expect::SessionList, std::move(cb)); }

void SessionApi::get(const QString& sessionId, Callback cb) {
  request("GET", sessionPath(sessionId), {}, false, Expect::Session, std::move(cb));
}

void SessionApi::publish(const QString& gameId, const QString& visibility, Callback cb) {
  request("POST", QStringLiteral("/sessions"),
          {{QStringLiteral("game_id"), gameId}, {QStringLiteral("visibility"), visibility}}, true, Expect::Session, std::move(cb));
}

void SessionApi::setVisibility(const QString& sessionId, const QString& visibility, Callback cb) {
  request("PATCH", sessionPath(sessionId), {{QStringLiteral("visibility"), visibility}}, true, Expect::Session, std::move(cb));
}

void SessionApi::end(const QString& sessionId, Callback cb) {
  request("DELETE", sessionPath(sessionId), {}, false, Expect::None, std::move(cb));
}

void SessionApi::invite(const QString& sessionId, const QString& userId, Callback cb) {
  request("PUT", sessionPath(sessionId) + QStringLiteral("/invites/") + enc(userId), {}, false, Expect::Session, std::move(cb));
}

void SessionApi::withdrawInvite(const QString& sessionId, const QString& userId, Callback cb) {
  request("DELETE", sessionPath(sessionId) + QStringLiteral("/invites/") + enc(userId), {}, false, Expect::None, std::move(cb));
}

void SessionApi::decline(const QString& sessionId, Callback cb) {
  request("POST", sessionPath(sessionId) + QStringLiteral("/decline"), {}, false, Expect::None, std::move(cb));
}

void SessionApi::join(const QString& sessionId, Callback cb) {
  request("POST", sessionPath(sessionId) + QStringLiteral("/join"), {}, false, Expect::Join, std::move(cb));
}

void SessionApi::removeViewer(const QString& sessionId, const QString& viewerId, Callback cb) {
  request("DELETE", sessionPath(sessionId) + QStringLiteral("/viewers/") + enc(viewerId), {}, false, Expect::None, std::move(cb));
}

void SessionApi::listUsers(Callback cb) { request("GET", QStringLiteral("/users"), {}, false, Expect::Users, std::move(cb)); }

}  // namespace framebeam
