#include "gameuploader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent>
#include <atomic>

#include "romcache.h"
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

namespace uzume {

GameUploader::GameUploader(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  qRegisterMetaType<uzume::UploadResult>("uzume::UploadResult");
}

void GameUploader::failLater(const QString& fileName, const QString& code, const QString& message) {
  UploadResult r;
  r.fileName = fileName;
  r.errorCode = code;
  r.errorMessage = message;
  QTimer::singleShot(0, this, [this, r]() { emit finished(r); });
}

bool GameUploader::resumable() const {
  return conn_ && conn_->hubHasFeature(QStringLiteral("local_roms_v1"));
}

bool GameUploader::upload(const QString& filePath, const QString& title, const QString& sha256, const QString& system) {
  if (busy()) {
    return false;
  }
  if (!resumable()) {
    return uploadLegacy(filePath, title);
  }
  const QFileInfo info(filePath);
  fileName_ = info.fileName();
  if (!info.isFile() || !QFile(filePath).open(QIODevice::ReadOnly)) {
    failLater(fileName_, QStringLiteral("file_unreadable"), QStringLiteral("The file cannot be read"));
    return true;
  }
  if (fileName_.isEmpty() || fileName_.size() > 255) {
    failLater(fileName_, QStringLiteral("invalid_file_name"), QStringLiteral("Invalid file name"));
    return true;
  }
  active_ = true;
  cancelled_ = false;
  path_ = filePath;
  title_ = title.trimmed().left(200);
  system_ = system;
  size_ = info.size();
  offset_ = 0;
  resyncs_ = 0;
  reposted_ = false;
  uploadId_.clear();
  if (RomCache::isValidSha256(sha256)) {
    QTimer::singleShot(0, this, [this, sha256]() { onHashed(sha256); });
    return true;
  }
  hashCancel_ = std::make_shared<std::atomic_bool>(false);
  auto* watcher = new QFutureWatcher<QString>(this);
  connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
    const QString sha = watcher->result();
    watcher->deleteLater();
    onHashed(sha);
  });
  const QString path = filePath;
  const auto cancel = hashCancel_;
  watcher->setFuture(QtConcurrent::run([path, cancel]() {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    QByteArray buf(4 << 20, Qt::Uninitialized);
    while (!f.atEnd()) {
      if (cancel->load()) return QString();
      const qint64 n = f.read(buf.data(), buf.size());
      if (n < 0) return QString();
      h.addData(QByteArrayView(buf.constData(), n));
    }
    return QString::fromLatin1(h.result().toHex());
  }));
  return true;
}

// ---------------------------------------------------------------- resumable upload

namespace {
QJsonObject readState(const QString& file) {
  QFile f(file);
  if (file.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}
void writeState(const QString& file, const QJsonObject& o) {
  if (file.isEmpty()) return;
  QDir().mkpath(QFileInfo(file).absolutePath());
  QSaveFile f(file);
  if (!f.open(QIODevice::WriteOnly)) return;
  f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
  f.commit();
}
}  // namespace

QString GameUploader::storedId() const {
  return stateFile_ ? readState(stateFile_()).value(sha_).toString() : QString();
}
void GameUploader::storeId(const QString& id) const {
  if (!stateFile_) return;
  const QString file = stateFile_();
  QJsonObject o = readState(file);
  o.insert(sha_, id);
  writeState(file, o);
}
void GameUploader::forgetId() const {
  if (!stateFile_) return;
  const QString file = stateFile_();
  QJsonObject o = readState(file);
  if (o.contains(sha_)) {
    o.remove(sha_);
    writeState(file, o);
  }
}

void GameUploader::finishWith(UploadResult r) {
  active_ = false;
  hashCancel_.reset();
  r.fileName = fileName_;
  emit finished(r);
}

void GameUploader::handleError(const HttpResult& r, UploadResult& res) {
  res.status = r.status;
  res.errorCode = r.apiErrorCode;
  res.errorMessage = r.apiErrorMessage;
  if (r.networkError) {
    res.errorCode = QStringLiteral("unreachable");
    res.errorMessage = r.errorString;
  } else if (r.status == 409 && !r.json().value(QStringLiteral("existing_game_id")).toString().isEmpty()) {
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
}

void GameUploader::onHashed(const QString& sha) {
  if (!active_) return;
  if (cancelled_) {
    cancelled_ = false;
    UploadResult r;
    r.errorCode = QStringLiteral("cancelled");
    finishWith(r);
    return;
  }
  if (!RomCache::isValidSha256(sha)) {
    UploadResult r;
    r.errorCode = QStringLiteral("file_unreadable");
    r.errorMessage = QStringLiteral("The file cannot be read");
    finishWith(r);
    return;
  }
  sha_ = sha;
  openSession(true);
}

void GameUploader::openSession(bool tryStored) {
  const QString id = tryStored ? storedId() : QString();
  if (id.isEmpty()) {
    postSession();
    return;
  }
  QNetworkReply* reply = conn_ ? conn_->authorizedGet(QStringLiteral("/uploads/") + id) : nullptr;
  if (reply == nullptr) {
    UploadResult r;
    r.errorCode = QStringLiteral("not_connected");
    finishWith(r);
    return;
  }
  reply_ = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    reply_.clear();
    if (cancelled_) { cancelled_ = false; UploadResult c; c.errorCode = QStringLiteral("cancelled"); finishWith(c); return; }
    const QJsonObject j = r.json();
    if (r.status == 200 && j.value(QStringLiteral("size")).toVariant().toLongLong() == size_) {
      uploadId_ = id;
      offset_ = j.value(QStringLiteral("offset")).toVariant().toLongLong();
      sendChunk();
      return;
    }
    if (r.networkError || r.status >= 500 || r.status == 401) {
      UploadResult res;
      handleError(r, res);
      finishWith(res);
      return;
    }
    forgetId();  // expired or unknown session: start a new one
    postSession();
  });
}

void GameUploader::postSession() {
  QJsonObject body{{QStringLiteral("filename"), fileName_},
                   {QStringLiteral("size"), static_cast<double>(size_)},
                   {QStringLiteral("sha256"), sha_}};
  if (!title_.isEmpty()) body.insert(QStringLiteral("title"), title_);
  if (!system_.isEmpty()) body.insert(QStringLiteral("system"), system_);
  QNetworkReply* reply = conn_ ? conn_->authorizedSend("POST", QStringLiteral("/uploads"), QJsonDocument(body).toJson(QJsonDocument::Compact),
                                                       {}, "application/json")
                               : nullptr;
  if (reply == nullptr) {
    UploadResult r;
    r.errorCode = QStringLiteral("not_connected");
    finishWith(r);
    return;
  }
  reply_ = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    reply_.clear();
    if (cancelled_) { cancelled_ = false; UploadResult c; c.errorCode = QStringLiteral("cancelled"); finishWith(c); return; }
    const QJsonObject j = r.json();
    if (r.status == 200 || r.status == 201) {
      uploadId_ = j.value(QStringLiteral("upload_id")).toString();
      if (uploadId_.isEmpty()) {
        UploadResult res;
        res.errorCode = QStringLiteral("invalid_response");
        finishWith(res);
        return;
      }
      offset_ = j.value(QStringLiteral("offset")).toVariant().toLongLong();
      const qint64 cs = j.value(QStringLiteral("chunk_size")).toVariant().toLongLong();
      chunk_ = cs > 0 ? qMin(cs, kMaxChunk) : kDefaultChunk;
      storeId(uploadId_);
      sendChunk();
      return;
    }
    UploadResult res;
    handleError(r, res);
    finishWith(res);
  });
}

void GameUploader::resyncOffset() {
  QNetworkReply* reply = conn_ ? conn_->authorizedGet(QStringLiteral("/uploads/") + uploadId_) : nullptr;
  if (reply == nullptr) {
    UploadResult r;
    r.errorCode = QStringLiteral("not_connected");
    finishWith(r);
    return;
  }
  reply_ = reply;
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    reply_.clear();
    if (cancelled_) { cancelled_ = false; UploadResult c; c.errorCode = QStringLiteral("cancelled"); finishWith(c); return; }
    if (r.status == 200) {
      offset_ = r.json().value(QStringLiteral("offset")).toVariant().toLongLong();
      sendChunk();
      return;
    }
    if (r.status == 404 && !reposted_) {  // session removed on the Hub: start over once
      reposted_ = true;
      forgetId();
      postSession();
      return;
    }
    UploadResult res;
    handleError(r, res);
    finishWith(res);
  });
}

void GameUploader::sendChunk() {
  if (offset_ < 0 || offset_ >= size_) {
    UploadResult r;
    r.errorCode = QStringLiteral("invalid_response");
    r.errorMessage = QStringLiteral("The server reported an unexpected upload offset");
    finishWith(r);
    return;
  }
  QFile f(path_);
  if (!f.open(QIODevice::ReadOnly) || f.size() != size_ || !f.seek(offset_)) {
    UploadResult r;
    r.errorCode = QStringLiteral("file_unreadable");
    r.errorMessage = QStringLiteral("The file cannot be read or changed during the upload");
    finishWith(r);
    return;
  }
  const qint64 len = qMin(chunk_, size_ - offset_);
  const QByteArray data = f.read(len);
  if (data.size() != len) {
    UploadResult r;
    r.errorCode = QStringLiteral("file_unreadable");
    finishWith(r);
    return;
  }
  const qint64 first = offset_;
  const qint64 last = offset_ + len - 1;
  const HttpHeaders headers{{"Content-Range", QStringLiteral("bytes %1-%2/%3").arg(first).arg(last).arg(size_).toLatin1()}};
  QNetworkReply* reply = conn_ ? conn_->authorizedSend("PUT", QStringLiteral("/uploads/") + uploadId_, data, headers) : nullptr;
  if (reply == nullptr) {
    UploadResult r;
    r.errorCode = QStringLiteral("not_connected");
    finishWith(r);
    return;
  }
  reply_ = reply;
  connect(reply, &QNetworkReply::uploadProgress, this, [this, first](qint64 sent, qint64) { emit progress(first + sent, size_); });
  connect(reply, &QNetworkReply::finished, this, [this, reply, last]() {
    const HttpResult r = HubHttp::resultOf(reply);
    const bool aborted = reply->error() == QNetworkReply::OperationCanceledError && r.status == 0 && cancelled_;
    reply->deleteLater();
    reply_.clear();
    if (aborted || cancelled_) {
      cancelled_ = false;
      UploadResult c;
      c.errorCode = QStringLiteral("cancelled");  // the Hub session and the stored id stay: the next attempt resumes
      finishWith(c);
      return;
    }
    if (r.status == 200) {
      const QJsonObject j = r.json();
      if (last + 1 >= size_) {
        const auto g = parseGame(j.value(QStringLiteral("game")).toObject());
        UploadResult res;
        res.status = 200;
        if (g) {
          res.kind = UploadResult::Kind::Created;
          res.game = *g;
          forgetId();
        } else {
          res.errorCode = QStringLiteral("invalid_response");
        }
        emit progress(size_, size_);
        finishWith(res);
        return;
      }
      emit progress(last + 1, size_);
      const qint64 srv = j.contains(QStringLiteral("offset")) ? j.value(QStringLiteral("offset")).toVariant().toLongLong() : last + 1;
      offset_ = srv > 0 ? srv : last + 1;
      resyncs_ = 0;
      sendChunk();
      return;
    }
    if ((r.status == 409 || r.status == 416) && !r.json().contains(QStringLiteral("existing_game_id")) && resyncs_ < 3) {
      ++resyncs_;  // offset mismatch: ask the Hub where it stands
      resyncOffset();
      return;
    }
    if (r.status == 404 && !reposted_) {
      reposted_ = true;
      forgetId();
      postSession();
      return;
    }
    UploadResult res;
    handleError(r, res);
    if (r.status == 400 || r.status == 413 || r.status == 403) forgetId();
    finishWith(res);
  });
}

bool GameUploader::uploadLegacy(const QString& filePath, const QString& title) {
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
    failLater(fileName_, QStringLiteral("not_connected"), QStringLiteral("Not connected to a server"));
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
  if (hashCancel_) hashCancel_->store(true);
  if (active_ && !reply_) {
    cancelled_ = true;  // during hashing: honoured when it ends
    return;
  }
  if (reply_) {
    cancelled_ = true;
    reply_->abort();
  }
}

}  // namespace uzume
