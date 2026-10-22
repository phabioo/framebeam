#include "adminapi.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace uzume {

namespace {
constexpr qint64 kErrorBodyMax = 64 * 1024;

QString networkMessage(const HttpResult& r) { return r.certMismatch ? QStringLiteral("certificate mismatch") : r.errorString; }
}  // namespace

AdminApi::AdminApi(HubConnection* conn, QObject* parent) : QObject(parent), conn_(conn) {}

bool AdminApi::pathAllowed(const QString& path) {
  if (!path.startsWith(QLatin1String("/admin/")) && !path.startsWith(QLatin1String("/account/"))) return false;
  if (path.contains(QLatin1String("..")) || path.contains(QLatin1Char('\\')) || path.contains(QLatin1String("//")) ||
      path.contains(QLatin1Char('#')) || path.contains(QLatin1Char('\n')) || path.contains(QLatin1Char('\r'))) {
    return false;
  }
  return true;
}

bool AdminApi::methodAllowed(const QString& method) {
  static const QStringList ok{QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PUT"), QStringLiteral("PATCH"),
                              QStringLiteral("DELETE")};
  return ok.contains(method);
}

QString AdminApi::localPath(const QString& fileUrlOrPath) {
  const QUrl u(fileUrlOrPath);
  return u.isLocalFile() ? u.toLocalFile() : fileUrlOrPath;
}

void AdminApi::refuse(Done done, const QString& code, const QString& message) {
  QTimer::singleShot(0, this, [done = std::move(done), code, message]() {
    Result r;
    r.code = code;
    r.message = message;
    if (done) done(r);
  });
}

AdminApi::Result AdminApi::resultOf(QNetworkReply* reply) {
  const HttpResult h = HubHttp::resultOf(reply);
  Result r;
  r.status = h.status;
  r.ok = h.ok();
  r.networkError = h.networkError;
  if (h.networkError) {
    r.code = QStringLiteral("network");
    r.message = networkMessage(h);
    return r;
  }
  if (!h.body.isEmpty()) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(h.body, &pe);
    if (pe.error == QJsonParseError::NoError && !doc.isNull()) r.data = doc.toVariant();
  }
  if (!r.ok) {
    r.code = h.apiErrorCode;
    r.message = h.apiErrorMessage;
    r.retryAfter = h.json().value(QStringLiteral("error")).toObject().value(QStringLiteral("retry_after")).toInt();
    if (r.code.isEmpty()) r.code = QStringLiteral("http_%1").arg(h.status);
  }
  return r;
}

void AdminApi::finish(QNetworkReply* reply, Done done) {
  const Result r = resultOf(reply);
  reply->deleteLater();
  if (r.status == 401 && r.code != QLatin1String("invalid_credentials") && conn_) conn_->noteUnauthorized();
  if (done) done(r);
}

QPointer<QNetworkReply> AdminApi::request(const QString& method, const QString& path, const QVariant& body, Done done) {
  if (!methodAllowed(method)) {
    refuse(std::move(done), QStringLiteral("invalid_method"), tr("Unsupported request method."));
    return {};
  }
  if (!pathAllowed(path)) {
    refuse(std::move(done), QStringLiteral("invalid_path"), tr("Unsupported request path."));
    return {};
  }
  if (conn_ == nullptr) {
    refuse(std::move(done), QStringLiteral("not_connected"), tr("Not connected to a server."));
    return {};
  }
  QByteArray payload;
  const bool hasBody = body.isValid() && !body.isNull();
  if (hasBody) payload = QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact);
  QNetworkReply* reply = (method == QLatin1String("GET") && !hasBody)
                             ? conn_->authorizedGet(path)
                             : conn_->authorizedSend(method.toLatin1(), path, payload, {}, "application/json");
  if (reply == nullptr) {
    refuse(std::move(done), QStringLiteral("not_connected"), tr("Not connected to a server."));
    return {};
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)]() mutable { finish(reply, std::move(done)); });
  return reply;
}

QPointer<QNetworkReply> AdminApi::download(const QString& path, const QString& targetFile, Done done) {
  if (!pathAllowed(path)) {
    refuse(std::move(done), QStringLiteral("invalid_path"), tr("Unsupported request path."));
    return {};
  }
  if (conn_ == nullptr || targetFile.isEmpty()) {
    refuse(std::move(done), targetFile.isEmpty() ? QStringLiteral("invalid_file") : QStringLiteral("not_connected"),
           targetFile.isEmpty() ? tr("No file chosen.") : tr("Not connected to a server."));
    return {};
  }
  QNetworkReply* reply = conn_->authorizedGet(path);
  if (reply == nullptr) {
    refuse(std::move(done), QStringLiteral("not_connected"), tr("Not connected to a server."));
    return {};
  }
  // QSaveFile writes to a temporary file in the target directory and renames it on commit(); cancelWriting() or the
  // destructor remove it, so a failed transfer never leaves a partial file.
  struct State {
    std::unique_ptr<QSaveFile> file;
    QString error;
    qint64 written = 0;
  };
  auto st = std::make_shared<State>();
  st->file = std::make_unique<QSaveFile>(targetFile);
  st->file->setDirectWriteFallback(false);
  if (!st->file->open(QIODevice::WriteOnly)) {
    st->error = st->file->errorString();
    st->file.reset();
  }
  // Error answers (status not 2xx) are small JSON documents: kept in the reply buffer for resultOf(), not written.
  connect(reply, &QNetworkReply::readyRead, this, [reply, st]() {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300) {
      if (reply->bytesAvailable() > kErrorBodyMax) reply->abort();
      return;  // stays buffered
    }
    const QByteArray chunk = reply->readAll();
    if (st->file) {
      if (st->file->write(chunk) != chunk.size()) {
        st->error = st->file->errorString();
        st->file->cancelWriting();
        st->file.reset();
        reply->abort();
      } else {
        st->written += chunk.size();
      }
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply, st, targetFile, done = std::move(done)]() mutable {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 200 && status < 300 && reply->error() == QNetworkReply::NoError) {
      const QByteArray rest = reply->readAll();  // bytes that arrived with the finish
      if (st->file && !rest.isEmpty() && st->file->write(rest) != rest.size()) {
        st->error = st->file->errorString();
        st->file->cancelWriting();
        st->file.reset();
      }
      const bool committed = st->file && st->file->commit();
      if (!committed && st->error.isEmpty() && st->file) st->error = st->file->errorString();
      Result r;
      r.status = status;
      if (committed) {
        r.ok = true;
        r.filePath = targetFile;
      } else {
        r.code = QStringLiteral("file_error");
        r.message = st->error.isEmpty() ? tr("The file could not be written.") : st->error;
      }
      reply->deleteLater();
      if (done) done(r);
      return;
    }
    if (st->file) st->file->cancelWriting();
    st->file.reset();
    Result r = resultOf(reply);
    if (r.ok) {  // 2xx but the transfer broke
      r.ok = false;
      r.code = QStringLiteral("network");
    }
    if (!st->error.isEmpty()) {
      r.code = QStringLiteral("file_error");
      r.message = st->error;
    }
    reply->deleteLater();
    if (r.status == 401 && r.code != QLatin1String("invalid_credentials") && conn_) conn_->noteUnauthorized();
    if (done) done(r);
  });
  return reply;
}

QPointer<QNetworkReply> AdminApi::upload(const QString& method, const QString& path, const QString& sourceFile,
                                         const QString& contentType, Done done) {
  if (!methodAllowed(method) || method == QLatin1String("GET")) {
    refuse(std::move(done), QStringLiteral("invalid_method"), tr("Unsupported request method."));
    return {};
  }
  if (!pathAllowed(path)) {
    refuse(std::move(done), QStringLiteral("invalid_path"), tr("Unsupported request path."));
    return {};
  }
  if (conn_ == nullptr) {
    refuse(std::move(done), QStringLiteral("not_connected"), tr("Not connected to a server."));
    return {};
  }
  auto* file = new QFile(sourceFile);
  if (!file->open(QIODevice::ReadOnly)) {
    const QString msg = file->errorString();
    delete file;
    refuse(std::move(done), QStringLiteral("file_error"), msg);
    return {};
  }
  QNetworkReply* reply = conn_->authorizedSendStream(method.toLatin1(), path, file, {},
                                                     contentType.isEmpty() ? QByteArray("application/octet-stream") : contentType.toLatin1());
  if (reply == nullptr) {
    delete file;
    refuse(std::move(done), QStringLiteral("not_connected"), tr("Not connected to a server."));
    return {};
  }
  file->setParent(reply);  // lives exactly as long as the request
  connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)]() mutable { finish(reply, std::move(done)); });
  return reply;
}

}  // namespace uzume
