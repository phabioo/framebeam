#include "sessiontypes.h"

#include <QJsonArray>

namespace framebeam {

namespace {
QString str(const QJsonObject& o, const char* k) { return o.value(QLatin1String(k)).toString(); }
}  // namespace

std::optional<SessionInfo> parseSession(const QJsonObject& o) {
  SessionInfo s;
  s.sessionId = str(o, "session_id");
  s.gameId = str(o, "game_id");
  s.gameTitle = str(o, "game_title");
  const QJsonObject owner = o.value(QStringLiteral("owner")).toObject();
  s.owner = {str(owner, "user_id"), str(owner, "display_name"), str(owner, "device_name")};
  s.visibility = str(o, "visibility");
  s.createdAt = str(o, "created_at");
  s.viewerCount = o.value(QStringLiteral("viewer_count")).toInt(0);
  s.isOwner = o.value(QStringLiteral("is_owner")).toBool(false);
  s.invited = o.value(QStringLiteral("invited")).toBool(false);
  for (const QJsonValue& v : o.value(QStringLiteral("viewers")).toArray()) {
    const QJsonObject e = v.toObject();
    s.viewers.append({str(e, "viewer_id"), str(e, "display_name"), str(e, "device_name")});
  }
  for (const QJsonValue& v : o.value(QStringLiteral("invites")).toArray()) {
    const QJsonObject e = v.toObject();
    s.invites.append({str(e, "user_id"), str(e, "display_name"), str(e, "state"), e.value(QStringLiteral("online")).toBool()});
  }
  if (s.sessionId.isEmpty() || s.gameId.isEmpty() || s.visibility.isEmpty()) {
    return std::nullopt;
  }
  return s;
}

QList<TurnServer> parseTurnServers(const QJsonValue& v) {
  QList<TurnServer> out;
  for (const QJsonValue& e : v.toArray()) {
    const QJsonObject o = e.toObject();
    TurnServer t;
    for (const QJsonValue& u : o.value(QStringLiteral("urls")).toArray()) {
      if (u.isString() && u.toString().startsWith(QLatin1String("turn"))) {
        t.urls.append(u.toString());
      }
    }
    t.username = str(o, "username");
    t.credential = str(o, "credential");
    t.expiresAt = str(o, "expires_at");
    if (!t.urls.isEmpty() && !t.username.isEmpty() && !t.credential.isEmpty()) {
      out.append(t);
    }
  }
  return out;
}

std::optional<SessionJoinInfo> parseJoinInfo(const QJsonObject& o) {
  SessionJoinInfo j;
  j.viewerId = str(o, "viewer_id");
  const QJsonObject p = o.value(QStringLiteral("permissions")).toObject();
  j.permissions.viewVideo = p.value(QStringLiteral("view_video")).toBool(true);
  j.permissions.hearAudio = p.value(QStringLiteral("hear_audio")).toBool(true);
  j.permissions.sendInput = p.value(QStringLiteral("send_input")).toBool(false);
  for (const QJsonValue& v : o.value(QStringLiteral("ice_servers")).toArray()) {
    if (v.isString()) {
      j.iceServers.append(v.toString());
    }
  }
  j.turnServers = parseTurnServers(o.value(QStringLiteral("turn_servers")));
  if (j.viewerId.isEmpty()) {
    return std::nullopt;
  }
  return j;
}

std::optional<HubUser> parseHubUser(const QJsonObject& o) {
  HubUser u{str(o, "id"), str(o, "display_name"), o.value(QStringLiteral("online")).toBool()};
  if (u.id.isEmpty()) {
    return std::nullopt;
  }
  return u;
}

QJsonObject SessionSignal::toPayload() const {
  QJsonObject o{{QStringLiteral("session_id"), sessionId}, {QStringLiteral("viewer_id"), viewerId}, {QStringLiteral("kind"), kind}};
  if (!sdp.isEmpty()) {
    o.insert(QStringLiteral("sdp"), sdp);
  }
  if (!candidate.isEmpty()) {
    o.insert(QStringLiteral("candidate"), candidate);
  }
  if (!mid.isEmpty()) {
    o.insert(QStringLiteral("mid"), mid);
  }
  return o;
}

std::optional<SessionSignal> parseSignal(const QJsonObject& p) {
  SessionSignal s{str(p, "session_id"), str(p, "viewer_id"), str(p, "kind"), str(p, "sdp"), str(p, "candidate"), str(p, "mid")};
  if (s.sessionId.isEmpty() || s.viewerId.isEmpty() ||
      (s.kind != QLatin1String("offer") && s.kind != QLatin1String("answer") && s.kind != QLatin1String("candidate"))) {
    return std::nullopt;
  }
  return s;
}

}  // namespace framebeam
