#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

#include "hubconnection.h"
#include "hublibrary.h"
#include "hubsocket.h"
#include "sessionapi.h"
#include "sessionhost.h"
#include "sessionviewer.h"

namespace framebeam {

// CLI session-share / session-watch (developer tool and E2E, headless). Needs a Connected HubConnection.
//   session-share [--game <id>] [--visibility private|hub_users|invite_only] [--synthetic] [--seconds N] [--force-relay]
//   session-watch (--session <id> | --first) [--seconds N] [--force-relay]
// Output lines (stdout): SHARING / VIEWER-JOINED / WATCHING / STATS ... / SHARE-DONE / WATCH-OK / WATCH-FAIL; the last
// connection type of the run as `CONNECTION direct (host)|relay (udp)|...|unknown` (--force-relay = FRAMEBEAM_FORCE_RELAY=1).
class SessionCommands : public QObject {
  Q_OBJECT
 public:
  using Finish = std::function<void(int)>;
  SessionCommands(HubConnection* conn, HubLibrary* library, Finish finish, QObject* parent = nullptr);

  // `args` = options after the command name. Returns false on usage errors (message printed).
  bool share(const QStringList& args);
  bool watch(const QStringList& args);

 private:
  void startSocket(std::function<void()> onReady);
  void publish();
  void feedSynthetic();
  void shareTick();
  void findAndJoin();
  void join(const QString& sessionId);
  void watchTick();
  void fail(const QString& msg);

  HubConnection* conn_;
  HubLibrary* library_;
  Finish finish_;
  std::unique_ptr<HubSocket> socket_;
  std::unique_ptr<SessionApi> api_;
  std::unique_ptr<SessionHost> host_;
  std::unique_ptr<SessionViewer> viewer_;
  QTimer ticker_;
  QTimer source_;
  QTimer sink_;
  QTimer retry_;

  QString gameId_;
  QString visibility_ = QStringLiteral("hub_users");
  QString sessionId_;
  QString viewerId_;
  bool first_ = false;
  bool joining_ = false;
  int seconds_ = 0;
  int elapsed_ = 0;
  bool ending_ = false;  // share: the end was initiated locally
  int frameNo_ = 0;
  double phase_ = 0.0;
  int findAttempts_ = 0;
  qint64 framesSeen_ = 0;
  QList<SessionSignal> earlySignals_;
  bool ended_ = false;
  bool forceRelay_ = false;
  QString lastConnection_;  // last known connection type (stats are gone once the PeerConnection closed)
};

}  // namespace framebeam
