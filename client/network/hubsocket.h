#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <memory>

#include "hubconnection.h"
#include "sessiontypes.h"

class QWebSocket;
class QSslError;

namespace framebeam {

// WSS client of the Hub (`GET /api/v1/ws`, ADR 0006 D3): Bearer upgrade with the same leaf pinning as HubHttp,
// `hello` after connect, typed signals for every Hub message, WebSocket ping every 20 s, automatic reconnect
// with backoff (the token is renewed through HubConnection when the Hub rejects it).
// A running media path does not depend on this socket (Hub failure: the Session lives on until the Hub ends it).
class HubSocket : public QObject {
  Q_OBJECT
 public:
  enum class State { Stopped, Connecting, Open, Backoff };
  Q_ENUM(State)

  explicit HubSocket(HubConnection* connection, QObject* parent = nullptr);
  ~HubSocket() override;

  // Starts (or restarts) the connection; reconnects until stop(). Waits for HubConnection::Connected if needed.
  void start();
  void stop();
  State state() const { return state_; }
  bool isOpen() const { return state_ == State::Open; }
  const HelloAck& helloAck() const { return hello_; }

  void sendSignal(const SessionSignal& s);
  void sendPresence(const QString& state, const QString& gameId = {});

  // Timing (tests): ping interval, backoff start/max, hello_ack timeout.
  void setPingIntervalMs(int ms) { pingIntervalMs_ = ms; }
  void setBackoffMs(int initial, int max) {
    backoffInitialMs_ = initial;
    backoffMaxMs_ = max;
  }

 signals:
  void stateChanged(framebeam::HubSocket::State state);
  void helloAcked(const framebeam::HelloAck& ack);  // connection usable
  void presenceUpdated(const framebeam::PresenceUpdate& update);
  void sessionUpdated(const framebeam::SessionInfo& session);
  void sessionInvited(const framebeam::SessionInfo& session);
  void sessionEnded(const framebeam::SessionEnded& ended);
  void viewerJoined(const framebeam::ViewerJoined& viewer);
  void viewerLeft(const framebeam::ViewerLeft& viewer);
  void signalReceived(const framebeam::SessionSignal& signal);
  void hubError(const QString& code, const QString& message);  // `error` message of the Hub
  void connectionError(const QString& code, const QString& message);  // transport: unreachable, certificate_changed, ...

 private:
  void connectNow();
  void teardown();
  void scheduleReconnect();
  void setState(State s);
  void send(const QString& type, const QJsonObject& payload);
  void onTextMessage(const QString& text);
  void onSslErrors(const QList<QSslError>& errors);
  void onSocketError();
  void onConnected();
  void onPingTimer();

  QPointer<HubConnection> conn_;
  std::unique_ptr<QWebSocket> ws_;
  State state_ = State::Stopped;
  bool wanted_ = false;
  bool certMismatch_ = false;
  HelloAck hello_;
  QTimer reconnectTimer_;
  QTimer pingTimer_;
  QTimer helloTimer_;
  int pingIntervalMs_ = 20000;
  int backoffInitialMs_ = 1000;
  int backoffMaxMs_ = 30000;
  int backoffMs_ = 1000;
  bool reachedServer_ = false;  // TLS/TCP reached the Hub during this attempt
  int upgradeRejects_ = 0;      // consecutive upgrade failures after reaching the Hub
  qint64 lastTrafficMs_ = 0;
  quint64 generation_ = 0;
};

}  // namespace framebeam
