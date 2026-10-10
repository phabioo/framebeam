#include "romdownloader.h"

#include <QFileInfo>
#include <QNetworkReply>
#include <QSet>
#include <QtConcurrent>
#include <QRegularExpression>

namespace framebeam {

namespace {
RomStatus makeStatus(RomState st, qint64 total, qint64 received, const QString& code = {}, const QString& msg = {}) {
  RomStatus s;
  s.state = st;
  s.totalBytes = total;
  s.receivedBytes = received;
  s.errorCode = code;
  s.errorMessage = msg;
  return s;
}
}  // namespace

RomDownloader::RomDownloader(HubConnection* connection, RomCache* cache, QObject* parent)
    : QObject(parent), conn_(connection), cache_(cache) {}

RomDownloader::~RomDownloader() {
  const auto jobs = jobs_;
  for (const auto& job : jobs) {
    job->aborted = true;
    if (job->primeWatcher) {
      QObject::disconnect(job->primeWatcher.get(), nullptr, this, nullptr);
      job->primeWatcher->waitForFinished();
    }
    if (job->reply) {
      QObject::disconnect(job->reply, nullptr, this, nullptr);
      job->reply->abort();
    }
  }
  for (const auto& v : std::as_const(validations_)) {
    QObject::disconnect(v->watcher.get(), nullptr, this, nullptr);
    v->watcher->waitForFinished();
  }
}

RomStatus RomDownloader::status(const GameEntry& game) {
  const QString sha = game.romSha256;
  if (const auto it = jobs_.constFind(sha); it != jobs_.constEnd()) {
    return makeStatus(it.value()->priming ? RomState::Validating : RomState::Downloading, game.romSize,
                      it.value()->received);
  }
  if (validations_.contains(sha)) {
    return makeStatus(RomState::Validating, game.romSize, game.romSize);
  }
  if (const auto it = sticky_.constFind(sha); it != sticky_.constEnd()) {
    return it.value();
  }
  const QString ext = RomCache::extensionFromFilename(game.romFilename);
  switch (cache_->probe(sha, ext, game.romSize)) {
    case RomCache::Probe::Valid: {
      RomStatus s = makeStatus(RomState::Ready, game.romSize, game.romSize);
      s.localPath = cache_->finalPath(sha, ext);
      return s;
    }
    case RomCache::Probe::Unverified:
      startValidation(game, false);
      return makeStatus(RomState::Validating, game.romSize, game.romSize);
    case RomCache::Probe::Missing:
      break;
  }
  return makeStatus(RomState::DownloadNeeded, game.romSize, cache_->partSize(sha, ext));
}

void RomDownloader::startValidation(const GameEntry& game, bool thenDownload) {
  const QString sha = game.romSha256;
  if (const auto it = validations_.constFind(sha); it != validations_.constEnd()) {
    it.value()->thenDownload = it.value()->thenDownload || thenDownload;
    return;
  }
  auto v = std::make_shared<Validation>();
  v->game = game;
  v->ext = RomCache::extensionFromFilename(game.romFilename);
  v->thenDownload = thenDownload;
  v->watcher = std::make_unique<QFutureWatcher<QPair<bool, QString>>>();
  const QString path = cache_->finalPath(sha, v->ext);
  validations_.insert(sha, v);
  // weak: the watcher is owned by the Validation, a shared capture would keep the pair alive forever (PC-4)
  connect(v->watcher.get(), &QFutureWatcher<QPair<bool, QString>>::finished, this, [this, weak = std::weak_ptr<Validation>(v), sha]() {
    const std::shared_ptr<Validation> v = weak.lock();
    if (!v || !v->watcher) return;
    const auto res = v->watcher->result();
    validations_.remove(sha);
    v->watcher.release()->deleteLater();  // not destroyed from inside its own finished emission
    if (res.first && res.second == sha) {
      cache_->markVerified(sha, v->ext);
      RomStatus s = makeStatus(RomState::Ready, v->game.romSize, v->game.romSize);
      s.localPath = cache_->finalPath(sha, v->ext);
      emit statusChanged(sha, s);
      emit romReady(sha, s.localPath);
      return;
    }
    if (res.first) {
      cache_->dropFinal(sha, v->ext);  // corrupted cache file
    }
    if (v->thenDownload) {
      beginDownload(v->game);
    } else {
      emit statusChanged(sha, makeStatus(RomState::DownloadNeeded, v->game.romSize, cache_->partSize(sha, v->ext)));
    }
  });
  v->watcher->setFuture(QtConcurrent::run([path]() {
    QString hex;
    const bool ok = RomCache::sha256OfFile(path, &hex);
    return qMakePair(ok, hex);
  }));
  emit statusChanged(sha, makeStatus(RomState::Validating, game.romSize, game.romSize));
}

QSet<QString> RomDownloader::activeHashes() const {
  QSet<QString> out;
  for (auto it = jobs_.cbegin(); it != jobs_.cend(); ++it) out.insert(it.key());
  for (auto it = validations_.cbegin(); it != validations_.cend(); ++it) out.insert(it.key());
  return out;
}

void RomDownloader::finishJob(const QString& sha, const RomStatus& st) {
  jobs_.remove(sha);
  if (st.state == RomState::HashMismatch || st.state == RomState::Failed) {
    sticky_.insert(sha, st);
  } else {
    sticky_.remove(sha);
  }
  emit statusChanged(sha, st);
  if (st.state == RomState::Ready) {
    cache_->touch(sha, QFileInfo(st.localPath).suffix());  // LRU: counts as used now
    emit romReady(sha, st.localPath);
  }
}

void RomDownloader::ensureRom(const GameEntry& game) {
  const QString sha = game.romSha256;
  if (jobs_.contains(sha) || !RomCache::isValidSha256(sha)) {
    return;
  }
  if (const auto it = validations_.constFind(sha); it != validations_.constEnd()) {
    it.value()->thenDownload = true;
    return;
  }
  sticky_.remove(sha);
  const QString ext = RomCache::extensionFromFilename(game.romFilename);
  switch (cache_->probe(sha, ext, game.romSize)) {
    case RomCache::Probe::Valid: {
      RomStatus s = makeStatus(RomState::Ready, game.romSize, game.romSize);
      s.localPath = cache_->finalPath(sha, ext);
      cache_->touch(sha, ext);  // LRU: counts as used now
      emit statusChanged(sha, s);
      emit romReady(sha, s.localPath);
      return;
    }
    case RomCache::Probe::Unverified:
      startValidation(game, true);
      return;
    case RomCache::Probe::Missing:
      beginDownload(game);
      return;
  }
}

void RomDownloader::beginDownload(const GameEntry& game) {
  const QString sha = game.romSha256;
  if (jobs_.contains(sha)) {
    return;
  }
  auto job = std::make_shared<Job>();
  job->game = game;
  job->ext = RomCache::extensionFromFilename(game.romFilename);
  job->received = cache_->partSize(sha, job->ext);
  job->hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
  jobs_.insert(sha, job);
  startRequest(job.get());
}

void RomDownloader::cancel(const QString& sha256) {
  const auto it = jobs_.constFind(sha256);
  if (it == jobs_.constEnd()) {
    return;
  }
  const std::shared_ptr<Job> job = it.value();
  job->aborted = true;
  if (job->reply) {
    job->reply->abort();  // onFinished cleans up
  }
}

void RomDownloader::startRequest(Job* job) {
  const QString sha = job->game.romSha256;
  const qint64 total = job->game.romSize;
  if (job->file.isOpen()) {
    job->file.close();
  }
  job->opened = false;
  if (job->received > total) {
    cache_->discardPart(sha, job->ext);
    job->received = 0;
  }
  job->hash->reset();
  if (job->received == 0) {
    sendRequest(job);
    return;
  }
  // Read the existing .part portion off-thread into the incremental hash.
  job->priming = true;
  emit statusChanged(sha, makeStatus(RomState::Validating, total, job->received));
  const QString path = cache_->partPath(sha, job->ext);
  const qint64 n = job->received;
  const auto hash = job->hash;
  job->primeWatcher = std::make_unique<QFutureWatcher<bool>>();
  connect(job->primeWatcher.get(), &QFutureWatcher<bool>::finished, this, [this, job, sha]() {
    const std::shared_ptr<Job> keep = jobs_.value(sha);  // finishJob() below drops the map's reference
    if (!keep || keep.get() != job || !job->primeWatcher) return;
    const bool ok = job->primeWatcher->result();
    job->primeWatcher.release()->deleteLater();  // not destroyed from inside its own finished emission
    job->priming = false;
    if (job->aborted) {
      finishJob(sha, makeStatus(RomState::DownloadNeeded, job->game.romSize, cache_->partSize(sha, job->ext)));
      return;
    }
    if (!ok) {  // .part not readable: start over
      cache_->discardPart(sha, job->ext);
      job->received = 0;
      job->hash->reset();
    }
    if (job->received == job->game.romSize && job->received > 0) {
      finishDownload(job);
    } else {
      sendRequest(job);
    }
  });
  job->primeWatcher->setFuture(QtConcurrent::run([path, n, hash]() {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      return false;
    }
    qint64 left = n;
    while (left > 0) {
      const QByteArray chunk = f.read(qMin<qint64>(left, 1 << 20));
      if (chunk.isEmpty()) {
        return false;
      }
      hash->addData(chunk);
      left -= chunk.size();
    }
    return true;
  }));
}

void RomDownloader::sendRequest(Job* job) {
  const QString sha = job->game.romSha256;
  const qint64 total = job->game.romSize;
  HttpHeaders headers;
  if (job->received > 0) {
    headers.append({"Range", "bytes=" + QByteArray::number(job->received) + "-"});
  }
  QNetworkReply* reply = conn_ ? conn_->authorizedGet(QStringLiteral("/roms/") + sha, headers) : nullptr;
  if (reply == nullptr) {
    finishJob(sha, makeStatus(RomState::Failed, total, job->received, QStringLiteral("not_connected"),
                              QStringLiteral("Not connected to a hub")));
    return;
  }
  job->reply = reply;
  emit statusChanged(sha, makeStatus(RomState::Downloading, total, job->received));
  connect(reply, &QNetworkReply::readyRead, this, [this, job, reply]() {
    if (job->reply == reply) {
      onReadyRead(job);
    }
  });
  connect(reply, &QNetworkReply::finished, this, [this, job, reply]() {
    if (job->reply == reply) {
      onFinished(job);
    }
  });
}

void RomDownloader::onReadyRead(Job* job) {
  QNetworkReply* reply = job->reply;
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status != 200 && status != 206) {
    return;  // error body is left for onFinished
  }
  if (!job->opened) {
    const QString partPath = cache_->partPath(job->game.romSha256, job->ext);
    job->file.setFileName(partPath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if (status == 206) {
      const QString range = QString::fromLatin1(reply->rawHeader("Content-Range"));
      static const QRegularExpression re(QStringLiteral("^bytes (\\d+)-"));
      const auto m = re.match(range);
      if (!m.hasMatch() || m.captured(1).toLongLong() != job->received) {
        reply->abort();
        return;
      }
      mode |= QIODevice::Append;
    } else {
      mode |= QIODevice::Truncate;  // server ignores Range: start over
      job->received = 0;
      job->hash->reset();
    }
    if (!job->file.open(mode)) {
      reply->abort();
      return;
    }
    job->opened = true;
  }
  const QByteArray chunk = reply->readAll();
  if (job->file.write(chunk) != chunk.size()) {
    reply->abort();
    return;
  }
  job->hash->addData(chunk);
  job->received += chunk.size();
  emit progress(job->game.romSha256, job->received, job->game.romSize);
}

void RomDownloader::onFinished(Job* job) {
  const std::shared_ptr<Job> keep = jobs_.value(job->game.romSha256);  // job stays alive until the end of this function
  QNetworkReply* reply = job->reply;
  job->reply.clear();
  reply->deleteLater();
  const QString sha = job->game.romSha256;
  const qint64 total = job->game.romSize;
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  if (job->aborted) {
    job->file.close();
    finishJob(sha, makeStatus(RomState::DownloadNeeded, total, cache_->partSize(sha, job->ext)));
    return;
  }
  if (status == 200 || status == 206 || status == 0) {
    if (job->opened) {
      const QByteArray rest = reply->readAll();
      if (!rest.isEmpty()) {
        job->file.write(rest);
        job->received += rest.size();
      }
    } else if (status == 200 && total == 0) {
      job->file.setFileName(cache_->partPath(sha, job->ext));
      if (job->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        job->opened = true;
      }
    }
    job->file.close();
    if (!job->opened || status == 0) {
      const HttpResult r = HubHttp::resultOf(reply);
      const QString code = r.certMismatch ? QStringLiteral("certificate_changed")
                           : status == 0  ? QStringLiteral("unreachable")
                                          : QStringLiteral("bad_response");
      finishJob(sha, makeStatus(RomState::Failed, total, cache_->partSize(sha, job->ext), code, reply->errorString()));
      return;
    }
    if (cache_->partSize(sha, job->ext) != total) {
      finishJob(sha, makeStatus(RomState::Failed, total, cache_->partSize(sha, job->ext), QStringLiteral("incomplete"),
                                QStringLiteral("Download incomplete")));
      return;
    }
    finishDownload(job);
    return;
  }
  job->file.close();
  const HttpResult r = HubHttp::resultOf(reply);
  if (status == 416) {
    if (!job->retriedFull) {
      job->retriedFull = true;
      cache_->discardPart(sha, job->ext);
      job->received = 0;
      startRequest(job);
      return;
    }
  }
  if (status == 401 && conn_) {
    conn_->noteUnauthorized();
  }
  finishJob(sha, makeStatus(RomState::Failed, total, cache_->partSize(sha, job->ext),
                            r.apiErrorCode.isEmpty() ? QStringLiteral("http_%1").arg(status) : r.apiErrorCode,
                            r.apiErrorMessage));
}

void RomDownloader::finishDownload(Job* job) {
  const QString sha = job->game.romSha256;
  const qint64 total = job->game.romSize;
  const std::shared_ptr<Job> keep = jobs_.value(sha);
  if (QString::fromLatin1(job->hash->result().toHex()) != sha) {
    cache_->discardPart(sha, job->ext);
    finishJob(sha, makeStatus(RomState::HashMismatch, total, 0, QStringLiteral("hash_mismatch"),
                              QStringLiteral("SHA-256 of the downloaded ROM does not match")));
    return;
  }
  if (!cache_->commitVerified(sha, job->ext)) {
    finishJob(sha, makeStatus(RomState::Failed, total, cache_->partSize(sha, job->ext), QStringLiteral("io_error"),
                              QStringLiteral("Cache file could not be written")));
    return;
  }
  RomStatus s = makeStatus(RomState::Ready, total, total);
  s.localPath = cache_->finalPath(sha, job->ext);
  finishJob(sha, s);
}

}  // namespace framebeam
