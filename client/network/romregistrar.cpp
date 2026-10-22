#include "romregistrar.h"

#include <QJsonDocument>
#include <QNetworkReply>

namespace uzume {

RomRegistrar::RomRegistrar(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  qRegisterMetaType<uzume::RegisterOutcome>("uzume::RegisterOutcome");
  if (connection) {
    connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
      if (s != HubConnection::State::Connected) {
        queue_.clear();  // never carried over to another Hub
      }
    });
  }
}

bool RomRegistrar::supported() const {
  return conn_ && conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QStringLiteral("local_roms_v1"));
}

QJsonObject RomRegistrar::requestBody(const LocalRomFile& f) {
  QJsonObject o{{QStringLiteral("sha256"), f.sha256},
                {QStringLiteral("size"), static_cast<double>(f.size)},
                {QStringLiteral("filename"), f.filename()}};
  if (!f.system.isEmpty()) o.insert(QStringLiteral("system"), f.system);
  if (!f.gameCode.isEmpty()) {
    o.insert(QStringLiteral("game_code"), f.gameCode);
    if (f.gameVersion >= 0) o.insert(QStringLiteral("game_version"), f.gameVersion);
  }
  return o;
}

QList<LocalRomFile> RomRegistrar::filesToRegister(const QList<LocalRomFile>& scanned, const QList<GameEntry>& games,
                                                  const QSet<QString>& ignoredShas) {
  QSet<QString> known = ignoredShas;
  for (const GameEntry& g : games) {
    known.insert(g.romSha256);
    for (const RomFileEntry& r : g.roms) known.insert(r.sha256);
  }
  QList<LocalRomFile> out;
  for (const LocalRomFile& f : scanned) {
    if (known.contains(f.sha256)) continue;
    known.insert(f.sha256);
    out.append(f);
  }
  return out;
}

void RomRegistrar::registerFiles(const QList<LocalRomFile>& files) {
  if (!supported()) return;
  QSet<QString> queued;
  for (const LocalRomFile& q : std::as_const(queue_)) queued.insert(q.sha256);
  for (const LocalRomFile& f : files) {
    if (f.sha256.size() != 64 || queued.contains(f.sha256)) continue;  // one file per hash is enough
    queued.insert(f.sha256);
    queue_.append(f);
  }
  if (!inFlight_) {
    okCount_ = 0;
    failCount_ = 0;
    next();
  }
}

void RomRegistrar::next() {
  if (queue_.isEmpty()) {
    inFlight_ = false;
    emit batchFinished(okCount_, failCount_);
    return;
  }
  const LocalRomFile f = queue_.takeFirst();
  const QByteArray body = QJsonDocument(requestBody(f)).toJson(QJsonDocument::Compact);
  QNetworkReply* reply = conn_ ? conn_->authorizedSend("POST", QStringLiteral("/games/register"), body, {}, "application/json") : nullptr;
  if (reply == nullptr) {
    queue_.clear();
    inFlight_ = false;
    return;
  }
  inFlight_ = true;
  connect(reply, &QNetworkReply::finished, this, [this, reply, f]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    RegisterOutcome out;
    out.sha256 = f.sha256;
    if (r.status == 401 && conn_) conn_->noteUnauthorized();
    if (r.ok()) {
      if (const auto g = parseGame(r.json())) {
        out.ok = true;
        out.created = r.status == 201;
        out.game = *g;
      } else {
        out.errorCode = QStringLiteral("invalid_response");
      }
    } else {
      out.errorCode = r.networkError ? QStringLiteral("unreachable") : r.apiErrorCode;
      out.errorMessage = r.networkError ? r.errorString : r.apiErrorMessage;
    }
    out.ok ? ++okCount_ : ++failCount_;
    emit registered(out);
    if (!conn_ || conn_->state() != HubConnection::State::Connected) {
      queue_.clear();
    }
    next();
  });
}

void RomRegistrar::unregisterGame(const QString& gameId) {
  QNetworkReply* reply =
      conn_ ? conn_->authorizedSend("DELETE", QStringLiteral("/games/") + gameId + QStringLiteral("/registration"), {}) : nullptr;
  if (reply == nullptr) {
    emit unregistered(gameId, false, tr("Not connected to a Hub"));
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, gameId]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (r.status == 401 && conn_) conn_->noteUnauthorized();
    emit unregistered(gameId, r.ok(), r.networkError ? r.errorString : r.apiErrorMessage);
  });
}

}  // namespace uzume
