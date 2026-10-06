#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <optional>

#include "hubconnection.h"
#include "hubprotocol.h"

namespace framebeam {

// Systems registry of the active hub (GET /systems, handshake feature firmware_v1). Cleared on disconnect.
// Without firmware_v1 (older hub) nothing is requested and supported() is false.
class HubSystems : public QObject {
  Q_OBJECT
 public:
  enum class State { Idle, Loading, Ready, Failed };

  explicit HubSystems(HubConnection* connection, QObject* parent = nullptr);

  bool supported() const;  // connected and the hub advertises firmware_v1
  void reload();
  State state() const { return state_; }
  const QList<SystemInfo>& systems() const { return systems_; }
  std::optional<SystemInfo> system(const QString& id) const;
  QString errorMessage() const { return error_; }

 signals:
  void stateChanged();

 private:
  QPointer<HubConnection> conn_;
  QList<SystemInfo> systems_;
  State state_ = State::Idle;
  QString error_;
  quint64 generation_ = 0;
};

}  // namespace framebeam
