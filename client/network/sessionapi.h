#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>

#include "hubconnection.h"
#include "sessiontypes.h"

namespace framebeam {

struct SessionApiResult {
  enum class Kind {
    Ok,
    NotFound,           // 404 session_not_found / not_found
    Forbidden,          // 403 session_forbidden (ACL)
    Full,               // 409 session_full
    CapabilityMissing,  // 409 capability_missing (no H.264 encode/decode)
    Ended,              // 410 session_ended
    Offline,            // no connection, 5xx, 429, token problem: retry later
    Rejected,           // other 4xx
    BadResponse         // malformed body
  };
  Kind kind = Kind::Offline;
  int status = 0;
  QString errorCode;
  QString errorMessage;
  std::optional<SessionInfo> session;  // publish, get, update, invite
  QList<SessionInfo> sessions;         // list
  std::optional<SessionJoinInfo> join; // join
  QList<HubUser> users;                // listUsers
  bool ok() const { return kind == Kind::Ok; }
};

// Hub session API (ADR 0006 D2, tag `sessions`, handshake feature `sessions_v1`). Thin async client over the
// authenticated HubConnection; the callback runs on the event loop (never synchronously) and is dropped if the
// SessionApi is destroyed.
class SessionApi : public QObject {
  Q_OBJECT
 public:
  using Callback = std::function<void(const SessionApiResult&)>;
  explicit SessionApi(HubConnection* connection, QObject* parent = nullptr);

  void list(Callback cb);
  void get(const QString& sessionId, Callback cb);
  void publish(const QString& gameId, const QString& visibility, Callback cb);
  void setVisibility(const QString& sessionId, const QString& visibility, Callback cb);
  void end(const QString& sessionId, Callback cb);
  void invite(const QString& sessionId, const QString& userId, Callback cb);
  void withdrawInvite(const QString& sessionId, const QString& userId, Callback cb);
  void decline(const QString& sessionId, Callback cb);
  void join(const QString& sessionId, Callback cb);
  // Owner removes a viewer or the viewer leaves.
  void removeViewer(const QString& sessionId, const QString& viewerId, Callback cb);
  void listUsers(Callback cb);

 private:
  enum class Expect { None, Session, SessionList, Join, Users };
  void request(const QByteArray& method, const QString& path, const QJsonObject& body, bool hasBody, Expect expect, Callback cb);
  static QString sessionPath(const QString& sessionId);

  QPointer<HubConnection> conn_;
};

}  // namespace framebeam
