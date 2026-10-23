#include "personalapi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

namespace uzume {

namespace {
QString enc(const QString& s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }
}  // namespace

std::optional<CollectionInfo> parseCollection(const QJsonObject& o) {
  CollectionInfo c;
  c.id = o.value(QStringLiteral("id")).toString();
  c.name = o.value(QStringLiteral("name")).toString();
  c.createdAt = o.value(QStringLiteral("created_at")).toString();
  for (const QJsonValue& v : o.value(QStringLiteral("game_ids")).toArray()) {
    if (v.isString() && !v.toString().isEmpty()) c.gameIds.append(v.toString());
  }
  if (c.id.isEmpty()) {
    return std::nullopt;
  }
  return c;
}

PersonalApi::PersonalApi(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {}

void PersonalApi::request(const QByteArray& method, const QString& path, const QJsonObject& body, bool hasBody, Expect expect,
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
      PersonalResult r;
      r.errorCode = QStringLiteral("not_connected");
      cb(r);
    });
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, expect, cb = std::move(cb)]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    PersonalResult res;
    res.status = r.status;
    res.errorMessage = r.apiErrorMessage;
    if (r.networkError || r.certMismatch) {
      res.errorCode = QStringLiteral("unreachable");
      res.errorMessage = r.errorString;
    } else if (r.status == 401) {
      if (conn_) conn_->noteUnauthorized();
      res.errorCode = r.apiErrorCode.isEmpty() ? QStringLiteral("unauthorized") : r.apiErrorCode;
    } else if (!r.ok()) {
      res.errorCode = r.apiErrorCode.isEmpty() ? QStringLiteral("request_failed") : r.apiErrorCode;
    } else {
      res.ok = true;
      if (expect == Expect::List) {
        for (const QJsonValue& v : r.json().value(QStringLiteral("collections")).toArray()) {
          if (const auto c = parseCollection(v.toObject())) res.collections.append(*c);
        }
      } else if (expect == Expect::One) {
        if (const auto c = parseCollection(r.json())) {
          res.collection = *c;
        } else {
          res.ok = false;
          res.errorCode = QStringLiteral("bad_response");
        }
      }
    }
    cb(res);
  });
}

void PersonalApi::listCollections(Callback cb) { request("GET", QStringLiteral("/collections"), {}, false, Expect::List, std::move(cb)); }

void PersonalApi::setFavorite(const QString& gameId, bool favorite, Callback cb) {
  request(favorite ? "PUT" : "DELETE", QStringLiteral("/games/") + enc(gameId) + QStringLiteral("/favorite"), {}, false, Expect::None,
          std::move(cb));
}

void PersonalApi::createCollection(const QString& name, const QStringList& gameIds, Callback cb) {
  QJsonObject body{{QStringLiteral("name"), name.trimmed()}};
  if (!gameIds.isEmpty()) {
    body.insert(QStringLiteral("game_ids"), QJsonArray::fromStringList(gameIds));
  }
  request("POST", QStringLiteral("/collections"), body, true, Expect::One, std::move(cb));
}

void PersonalApi::renameCollection(const QString& id, const QString& name, Callback cb) {
  request("PATCH", QStringLiteral("/collections/") + enc(id), QJsonObject{{QStringLiteral("name"), name.trimmed()}}, true, Expect::One,
          std::move(cb));
}

void PersonalApi::deleteCollection(const QString& id, Callback cb) {
  request("DELETE", QStringLiteral("/collections/") + enc(id), {}, false, Expect::None, std::move(cb));
}

void PersonalApi::setInCollection(const QString& id, const QString& gameId, bool member, Callback cb) {
  request(member ? "PUT" : "DELETE", QStringLiteral("/collections/") + enc(id) + QStringLiteral("/games/") + enc(gameId), {}, false,
          Expect::None, std::move(cb));
}

}  // namespace uzume
