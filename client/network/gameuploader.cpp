#include "gameuploader.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

namespace framebeam {

GameUploader::GameUploader(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  qRegisterMetaType<framebeam::UploadResult>("framebeam::UploadResult");
}

void GameUploader::failLater(const QString& fileName, const QString& code, const QString& message) {
  UploadResult r;
  r.fileName = fileName;
  r.errorCode = code;
  r.errorMessage = message;
  QTimer::singleShot(0, this, [this, r]() { emit finished(r); });
}

bool GameUploader::upload(const QString& filePath, const QString& title) {
  if (busy()) {
    return false;
  }
  const QFileInfo info(filePath);
  fileName_ = info.fileName();
  auto* file = new QFile(filePath);
  if (!info.isFile() || !file->open(QIODevice::ReadOnly)) {
    delete file;
    failLater(fileName_, QStringLiteral("file_unreadable"), QStringLiteral("The file cannot be read"));
    return true;
  }
  if (fileName_.isEmpty() || fileName_.size() > 255) {
    delete file;
    failLater(fileName_, QStringLiteral("invalid_file_name"), QStringLiteral("Invalid file name"));
    return true;
  }
  QUrlQuery q;
  q.addQueryItem(QStringLiteral("filename"), fileName_);
  const QString t = title.trimmed();
  if (!t.isEmpty()) {
    q.addQueryItem(QStringLiteral("title"), t.left(200));
  }
  const QString path = QStringLiteral("/games?") + q.query(QUrl::FullyEncoded);
  QNetworkReply* reply = conn_ ? conn_->authorizedSendStream("POST", path, file) : nullptr;
  if (reply == nullptr) {
    delete file;
    failLater(fileName_, QStringLiteral("not_connected"), QStringLiteral("Not connected to a Hub"));
    return true;
  }
  file->setParent(reply);  // lives exactly as long as the request
  reply_ = reply;
  const QString fileName = fileName_;
  connect(reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) { emit progress(sent, total); });
  connect(reply, &QNetworkReply::finished, this, [this, reply, fileName]() {
    const HttpResult r = HubHttp::resultOf(reply);
    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError && r.status == 0 && cancelled_;
    reply->deleteLater();
    reply_.clear();
    UploadResult res;
    res.fileName = fileName;
    res.status = r.status;
    res.errorCode = r.apiErrorCode;
    res.errorMessage = r.apiErrorMessage;
    if (aborted) {
      res.errorCode = QStringLiteral("cancelled");
      cancelled_ = false;
    } else if (r.networkError) {
      res.errorCode = QStringLiteral("unreachable");
      res.errorMessage = r.errorString;
    } else if (r.status == 201) {
      const auto g = parseGame(r.json());
      if (g) {
        res.kind = UploadResult::Kind::Created;
        res.game = *g;
      } else {
        res.errorCode = QStringLiteral("invalid_response");
      }
    } else if (r.status == 409) {
      res.kind = UploadResult::Kind::Duplicate;
      res.existingGameId = r.json().value(QStringLiteral("existing_game_id")).toString();
    } else if (r.status == 403) {
      res.kind = UploadResult::Kind::Forbidden;
    } else if (r.status == 413) {
      res.kind = UploadResult::Kind::TooLarge;
    } else if (r.status == 400) {
      res.kind = UploadResult::Kind::Rejected;
    } else if (r.status == 401 && conn_) {
      conn_->noteUnauthorized();
    }
    emit finished(res);
  });
  return true;
}

void GameUploader::cancel() {
  if (reply_) {
    cancelled_ = true;
    reply_->abort();
  }
}

}  // namespace framebeam
