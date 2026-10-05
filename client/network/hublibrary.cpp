#include "hublibrary.h"

#include <QJsonArray>
#include <QNetworkReply>

namespace framebeam {

HubLibrary::HubLibrary(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s != HubConnection::State::Connected) {
      ++generation_;
      loading_ = false;
      if (!games_.isEmpty()) {
        games_.clear();
        emit cleared();
      }
    }
  });
}

std::optional<GameEntry> HubLibrary::gameByRomSha(const QString& sha256) const {
  for (const GameEntry& g : games_) {
    if (g.romSha256 == sha256) {
      return g;
    }
  }
  return std::nullopt;
}

void HubLibrary::reload() {
  QNetworkReply* reply = conn_ ? conn_->authorizedGet(QStringLiteral("/games")) : nullptr;
  if (reply == nullptr) {
    emit loadFailed(QStringLiteral("not_connected"), QStringLiteral("Not connected to a hub"));
    return;
  }
  loading_ = true;
  const quint64 gen = ++generation_;
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (gen != generation_) {
      return;
    }
    loading_ = false;
    if (r.status == 401 && conn_) {
      conn_->noteUnauthorized();
    }
    if (!r.ok()) {
      emit loadFailed(r.networkError ? QStringLiteral("unreachable") : r.apiErrorCode,
                      r.networkError ? r.errorString : r.apiErrorMessage);
      return;
    }
    QList<GameEntry> list;
    skipped_ = 0;
    const QJsonArray arr = r.json().value(QStringLiteral("games")).toArray();
    for (const QJsonValue& v : arr) {
      if (const auto g = parseGame(v.toObject())) {
        list.append(*g);
      } else {
        ++skipped_;
      }
    }
    games_ = list;
    emit loaded();
  });
}

}  // namespace framebeam
