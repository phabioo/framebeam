#include "hubsystems.h"

#include <QJsonArray>
#include <QNetworkReply>

namespace framebeam {

HubSystems::HubSystems(HubConnection* connection, QObject* parent) : QObject(parent), conn_(connection) {
  connect(connection, &HubConnection::stateChanged, this, [this](HubConnection::State s) {
    if (s != HubConnection::State::Connected) {
      ++generation_;
      const bool changed = state_ != State::Idle;
      state_ = State::Idle;
      systems_.clear();
      error_.clear();
      if (changed) {
        emit stateChanged();
      }
    }
  });
}

bool HubSystems::supported() const {
  return conn_ && conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QStringLiteral("firmware_v1"));
}

std::optional<SystemInfo> HubSystems::system(const QString& id) const {
  for (const SystemInfo& s : systems_) {
    if (s.id == id) {
      return s;
    }
  }
  return std::nullopt;
}

void HubSystems::reload() {
  if (!supported()) {
    return;
  }
  QNetworkReply* reply = conn_->authorizedGet(QStringLiteral("/systems"));
  if (reply == nullptr) {
    return;
  }
  state_ = State::Loading;
  error_.clear();
  emit stateChanged();
  const quint64 gen = ++generation_;
  connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
    const HttpResult r = HubHttp::resultOf(reply);
    reply->deleteLater();
    if (gen != generation_) {
      return;
    }
    if (r.status == 401 && conn_) {
      conn_->noteUnauthorized();
    }
    if (!r.ok()) {
      state_ = State::Failed;
      error_ = r.networkError ? r.errorString : r.apiErrorMessage;
      emit stateChanged();
      return;
    }
    QList<SystemInfo> list;
    for (const QJsonValue& v : r.json().value(QStringLiteral("systems")).toArray()) {
      if (const auto s = parseSystemInfo(v.toObject())) {
        list.append(*s);
      }
    }
    systems_ = list;
    state_ = State::Ready;
    emit stateChanged();
  });
}

}  // namespace framebeam
