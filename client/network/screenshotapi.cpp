#include "screenshotapi.h"

#include <QJsonArray>
#include <QTimer>
#include <QUrl>

namespace uzume {

namespace {
QString enc(const QString& s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }
}  // namespace

std::optional<ScreenshotInfo> parseScreenshot(const QJsonObject& o) {
  ScreenshotInfo s;
  s.id = o.value(QStringLiteral("id")).toString();
  if (s.id.isEmpty()) return std::nullopt;
  s.gameId = o.value(QStringLiteral("game_id")).toString();
  s.runId = o.value(QStringLiteral("run_id")).toString();  // null -> empty
  s.userId = o.value(QStringLiteral("user_id")).toString();
  s.userName = o.value(QStringLiteral("user_name")).toString();
  s.createdAt = o.value(QStringLiteral("created_at")).toString();
  s.width = o.value(QStringLiteral("width")).toInt();
  s.height = o.value(QStringLiteral("height")).toInt();
  s.sizeBytes = static_cast<qint64>(o.value(QStringLiteral("size_bytes")).toDouble());
  return s;
}

ScreenshotApi::ScreenshotApi(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {}

void ScreenshotApi::request(const QByteArray& method, const QString& path, const QByteArray& body, const QByteArray& contentType,
                            Expect expect, Callback cb) {
  QNetworkReply* reply = nullptr;
  if (conn_) {
    reply = method == "GET" ? conn_->authorizedGet(path) : conn_->authorizedSend(method, path, body, {}, contentType);
  }
  if (reply == nullptr) {
    QTimer::singleShot(0, this, [cb = std::move(cb)]() {
      ScreenshotResult r;
      r.errorCode = QStringLiteral("not_connected");
      cb(r);
    });
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, expect, cb = std::move(cb)]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    ScreenshotResult res;
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
      if (expect == Expect::One) {
        if (const auto s = parseScreenshot(r.json())) {
          res.screenshot = *s;
        } else {
          res.ok = false;
          res.errorCode = QStringLiteral("bad_response");
        }
      } else if (expect == Expect::List) {
        const QJsonObject o = r.json();
        for (const QJsonValue& v : o.value(QStringLiteral("screenshots")).toArray()) {
          if (const auto s = parseScreenshot(v.toObject())) res.screenshots.append(*s);
        }
        res.totalBytes = static_cast<qint64>(o.value(QStringLiteral("total_bytes")).toDouble());
      } else if (expect == Expect::Bytes) {
        res.content = r.body;
        if (res.content.isEmpty()) {
          res.ok = false;
          res.errorCode = QStringLiteral("bad_response");
        }
      }
    }
    cb(res);
  });
}

void ScreenshotApi::upload(const QString& gameId, const QString& runId, const QByteArray& png, Callback cb) {
  QString path = QStringLiteral("/games/") + enc(gameId) + QStringLiteral("/screenshots");
  if (!runId.isEmpty()) path += QStringLiteral("?run_id=") + enc(runId);
  request("POST", path, png, "image/png", Expect::One, std::move(cb));
}

void ScreenshotApi::listGame(const QString& gameId, Callback cb) {
  request("GET", QStringLiteral("/games/") + enc(gameId) + QStringLiteral("/screenshots"), {}, {}, Expect::List, std::move(cb));
}

void ScreenshotApi::listRun(const QString& runId, Callback cb) {
  request("GET", QStringLiteral("/runs/") + enc(runId) + QStringLiteral("/screenshots"), {}, {}, Expect::List, std::move(cb));
}

void ScreenshotApi::content(const QString& screenshotId, Callback cb) {
  request("GET", QStringLiteral("/screenshots/") + enc(screenshotId) + QStringLiteral("/content"), {}, {}, Expect::Bytes, std::move(cb));
}

void ScreenshotApi::remove(const QString& screenshotId, Callback cb) {
  request("DELETE", QStringLiteral("/screenshots/") + enc(screenshotId), {}, {}, Expect::None, std::move(cb));
}

}  // namespace uzume
