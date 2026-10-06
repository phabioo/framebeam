#include "sessioncontroller.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace framebeam::ui {

namespace {

QVariantMap row(const QString& surface, const QString& name, bool available, const QStringList& values) {
  return {{QStringLiteral("surface"), surface},
          {QStringLiteral("name"), name},
          {QStringLiteral("available"), available},
          {QStringLiteral("values"), values},
          {QStringLiteral("text"), name + QStringLiteral("  ") + values.join(QStringLiteral(" · "))}};
}

QString mbit(double kbps) { return QStringLiteral("%1 Mbit/s").arg(kbps / 1000.0, 0, 'f', 1); }

QString linkLabel(const QString& type) {
  if (type == QLatin1String("host") || type == QLatin1String("srflx")) {
    return QStringLiteral("WebRTC direct (%1)").arg(type);
  }
  if (type == QLatin1String("relay")) {
    return QStringLiteral("WebRTC relay");
  }
  return QStringLiteral("WebRTC connecting");
}

}  // namespace

SessionController::SessionController(HubConnection* conn, ProfileStore* profiles, GameSession* game, QObject* parent)
    : QObject(parent), conn_(conn), profiles_(profiles), game_(game), api_(conn), socket_(conn) {
  qRegisterMetaType<framebeam::SessionInfo>();
  qRegisterMetaType<framebeam::SessionSignal>();
  loadSettings();

  connect(conn_, &HubConnection::stateChanged, this, &SessionController::onConnectionState);
  connect(&socket_, &HubSocket::stateChanged, this, [this]() { emit linkChanged(); });
  connect(&socket_, &HubSocket::helloAcked, this, &SessionController::onHelloAck);
  connect(&socket_, &HubSocket::sessionUpdated, this, &SessionController::onSessionUpdated);
  connect(&socket_, &HubSocket::sessionInvited, this, &SessionController::onSessionUpdated);
  connect(&socket_, &HubSocket::sessionEnded, this, &SessionController::onSessionEnded);
  connect(&socket_, &HubSocket::viewerJoined, this, &SessionController::onViewerJoined);
  connect(&socket_, &HubSocket::viewerLeft, this, &SessionController::onViewerLeft);
  connect(&socket_, &HubSocket::signalReceived, this, &SessionController::onSignal);
  connect(&socket_, &HubSocket::hubError, this, [](const QString&, const QString&) {});

  connect(&host_, &SessionHost::signalOut, &socket_, &HubSocket::sendSignal);
  connect(&viewer_, &SessionViewer::signalOut, &socket_, &HubSocket::sendSignal);
  connect(&host_, &SessionHost::errorOccurred, this, [this](const QString& m) { say(m, true); });
  connect(&host_, &SessionHost::viewerConnected, this, [this]() { emit shareChanged(); });
  connect(&viewer_, &SessionViewer::frameReady, this, [this](const QImage& img) {
    remoteFrame_ = img;
    ++remoteFrameNr_;
    emit remoteFrameChanged();
  });
  connect(&viewer_, &SessionViewer::closed, this, [this]() {
    closeWatch(tr("The connection to the Session was lost."), true);
  });
  connect(&viewer_, &SessionViewer::errorOccurred, this, [this](const QString& m) { say(m, true); });

  // Local game: frames and audio feed the host only while shared (SessionHost drops them otherwise).
  connect(game_, &GameSession::frameChanged, this, [this]() {
    if (shared_) {
      host_.pushFrame(game_->frame());
    }
  });
  connect(game_, &GameSession::audioChunk, this, [this](const QByteArray& pcm, int rate) {
    if (shared_) {
      host_.pushAudio(pcm, rate);
    }
  });

  minuteTimer_.setInterval(30000);
  connect(&minuteTimer_, &QTimer::timeout, this, [this]() {
    if (!sessions_.isEmpty()) {
      emit sessionsChanged();
    }
  });
  minuteTimer_.start();
  statsTimer_.setInterval(1000);
  connect(&statsTimer_, &QTimer::timeout, this, [this]() {
    const quint64 nr = game_->frameNumber();
    localFps_ = nr >= lastLocalFrameNr_ ? static_cast<double>(nr - lastLocalFrameNr_) : 0.0;
    lastLocalFrameNr_ = nr;
    refreshDiagnostics();
  });
  statsTimer_.start();
  audioTimer_.setInterval(10);
  connect(&audioTimer_, &QTimer::timeout, this, &SessionController::pumpAudio);
  messageTimer_.setSingleShot(true);
  messageTimer_.setInterval(10000);
  connect(&messageTimer_, &QTimer::timeout, this, &SessionController::dismissMessage);
  usersAge_.invalidate();
}

SessionController::~SessionController() {
  socket_.stop();
  host_.close();
  viewer_.close();
}

// ---------------------------------------------------------------- settings

void SessionController::loadSettings() {
  QFile f(QDir(profiles_->baseDir()).filePath(QStringLiteral("player-settings.json")));
  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }
  const QString v = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("session_visibility")).toString();
  if (v == QLatin1String("private") || v == QLatin1String("hub_users") || v == QLatin1String("invite_only")) {
    visibility_ = v;
  }
}

void SessionController::saveSettings() const {
  QDir().mkpath(profiles_->baseDir());
  QSaveFile f(QDir(profiles_->baseDir()).filePath(QStringLiteral("player-settings.json")));
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("session_visibility"), visibility_}}).toJson(QJsonDocument::Indented));
    f.commit();
  }
}

// ---------------------------------------------------------------- link / lists

QString SessionController::hubLink() const {
  switch (socket_.state()) {
    case HubSocket::State::Open: return QStringLiteral("online");
    case HubSocket::State::Connecting: return QStringLiteral("connecting");
    default: return QStringLiteral("offline");
  }
}

bool SessionController::available() const {
  return conn_->state() == HubConnection::State::Connected && conn_->hubHasFeature(QString::fromLatin1(kSessionsFeature));
}

void SessionController::onConnectionState(HubConnection::State s) {
  using S = HubConnection::State;
  if (s == S::Connected) {
    if (available()) {
      socket_.start();
    }
  } else if (s != S::Identifying && s != S::Authenticating) {
    socket_.stop();
    closeShare(QString());
    closeWatch(QString(), false);
    sessions_.clear();
    users_.clear();
    emit sessionsChanged();
  }
  emit linkChanged();
}

void SessionController::onHelloAck() {
  presence();
  refreshSessions();
}

void SessionController::presence() { socket_.sendPresence(hasLocalGame() ? QStringLiteral("in_game") : QStringLiteral("online"), gameId_); }

QString SessionController::visibilityLabel(const QString& v) {
  if (v == QLatin1String("private")) return tr("Private");
  if (v == QLatin1String("invite_only")) return tr("Invite only");
  return tr("Hub users");
}

QVariantList SessionController::sessions() const {
  QList<SessionInfo> list = sessions_.values();
  std::sort(list.begin(), list.end(), [](const SessionInfo& a, const SessionInfo& b) { return a.createdAt > b.createdAt; });
  QVariantList out;
  const QDateTime now = QDateTime::currentDateTimeUtc();
  for (const SessionInfo& s : list) {
    const qint64 mins = QDateTime::fromString(s.createdAt, Qt::ISODate).secsTo(now) / 60;
    const QString meta = s.invited ? tr("inviting you")
                                   : visibilityLabel(s.visibility) + QStringLiteral(" · ") +
                                         (mins < 1 ? tr("just started") : tr("for %1 min").arg(mins));
    out.append(QVariantMap{{QStringLiteral("sessionId"), s.sessionId},
                           {QStringLiteral("title"), s.owner.displayName + QStringLiteral(" · ") + s.gameTitle},
                           {QStringLiteral("who"), s.owner.displayName},
                           {QStringLiteral("game"), s.gameTitle},
                           {QStringLiteral("meta"), meta},
                           {QStringLiteral("invited"), s.invited},
                           {QStringLiteral("visibility"), s.visibility}});
  }
  return out;
}

void SessionController::refreshSessions() {
  if (!available()) {
    return;
  }
  const quint64 gen = sessionEventGen_;
  api_.list([this, gen](const SessionApiResult& r) {
    if (!r.ok() || gen != sessionEventGen_) {
      return;  // a live event arrived after the request was sent: the list may be older than it, drop the result
    }
    sessions_.clear();
    for (const SessionInfo& s : r.sessions) {
      if (!s.isOwner) {
        sessions_.insert(s.sessionId, s);
      }
    }
    emit sessionsChanged();
  });
}

void SessionController::onSessionUpdated(const SessionInfo& s) {
  ++sessionEventGen_;
  if (shared_ && s.sessionId == own_.sessionId) {
    applyOwnSession(s);
    return;
  }
  if (s.isOwner) {
    return;
  }
  sessions_.insert(s.sessionId, s);
  if (watching_ && watched_.sessionId == s.sessionId) {
    watched_ = s;
  }
  emit sessionsChanged();
}

void SessionController::onSessionEnded(const SessionEnded& e) {
  ++sessionEventGen_;
  if (shared_ && e.sessionId == own_.sessionId) {
    QString note = tr("The Session ended.");
    if (e.reason == QLatin1String("replaced")) note = tr("The Session was replaced by a newer one.");
    else if (e.reason == QLatin1String("owner_disconnected")) note = tr("The Session ended because the Hub lost this device.");
    else if (e.reason == QLatin1String("device_revoked")) note = tr("The Session ended: this device was revoked.");
    closeShare(note);
    return;
  }
  if (sessions_.remove(e.sessionId) > 0) {
    emit sessionsChanged();
  }
  if ((watching_ || joining_) && watched_.sessionId == e.sessionId) {
    closeWatch(e.reason == QLatin1String("no_longer_visible") ? tr("You can no longer watch this Session.")
                                                              : tr("The Session ended."),
               false);
  }
}

// ---------------------------------------------------------------- own Session

int SessionController::viewerCount() const {
  return shared_ ? std::max(own_.viewerCount, static_cast<int>(own_.viewers.size())) : 0;
}

QString SessionController::participantsTitle() const {
  return visibility_ == QLatin1String("invite_only") ? tr("INVITED · %1").arg(participants().size())
                                                     : tr("WATCHING · %1").arg(participants().size());
}

QVariantList SessionController::participants() const {
  QVariantList out;
  if (!shared_) {
    return out;
  }
  QStringList watchingNames;
  for (const SessionViewerEntry& v : own_.viewers) {
    watchingNames << v.displayName;
    out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("viewer")},
                           {QStringLiteral("id"), v.viewerId},
                           {QStringLiteral("name"), v.displayName},
                           {QStringLiteral("online"), true},
                           {QStringLiteral("status"), tr("watching")},
                           {QStringLiteral("action"), tr("Remove")}});
  }
  if (visibility_ == QLatin1String("invite_only")) {
    for (const SessionInviteEntry& i : own_.invites) {
      if (i.state == QLatin1String("joined") || watchingNames.contains(i.displayName)) {
        continue;
      }
      QString status = i.state == QLatin1String("declined") ? tr("declined") : tr("invited");
      if (!i.online) {
        status += QStringLiteral(" · ") + tr("offline");
      }
      out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("invite")},
                             {QStringLiteral("id"), i.userId},
                             {QStringLiteral("name"), i.displayName},
                             {QStringLiteral("online"), i.online},
                             {QStringLiteral("status"), status},
                             {QStringLiteral("action"), tr("Withdraw")}});
    }
  }
  return out;
}

void SessionController::applyOwnSession(const SessionInfo& s) {
  own_ = s;
  visibility_ = s.visibility;
  emit shareChanged();
}

// A REST answer to a request sent before the user changed the visibility again carries the older visibility:
// take everything else from it but keep the newer local choice.
void SessionController::applyOwnSession(const SessionInfo& s, quint64 requestVisGen) {
  if (requestVisGen == visGen_) {
    applyOwnSession(s);
    return;
  }
  SessionInfo copy = s;
  copy.visibility = visibility_;
  applyOwnSession(copy);
}

QString SessionController::errorText(const SessionApiResult& r, const QString& what) const {
  using K = SessionApiResult::Kind;
  switch (r.kind) {
    case K::Full: return tr("The Session is full (up to 4 viewers).");
    case K::Forbidden: return tr("You are not allowed to watch this Session.");
    case K::NotFound:
    case K::Ended: return tr("The Session has ended.");
    case K::CapabilityMissing: return tr("%1: this device has no usable H.264 codec.").arg(what);
    case K::Offline: return tr("%1: the Hub is not reachable.").arg(what);
    default: return tr("%1 failed (%2).").arg(what, r.errorCode.isEmpty() ? QString::number(r.status) : r.errorCode);
  }
}

void SessionController::shareSession() {
  if (shared_ || shareBusy_ || !hasLocalGame() || !socket_.isOpen()) {
    if (!socket_.isOpen()) {
      say(tr("The Hub is not reachable, so the Session cannot be shared yet."), true);
    }
    return;
  }
  shareBusy_ = true;
  emit shareChanged();
  api_.publish(gameId_, visibility_, [this](const SessionApiResult& r) {
    shareBusy_ = false;
    if (!r.ok() || !r.session) {
      pendingViewers_.clear();
      pendingHostSignals_.clear();
      say(errorText(r, tr("Sharing the Session")), true);
      emit shareChanged();
      return;
    }
    own_ = *r.session;
    shared_ = true;
    saveSettings();
    host_.open(own_.sessionId, socket_.helloAck().iceServers);
    for (const QString& v : std::exchange(pendingViewers_, {})) {
      host_.addViewer(v);
    }
    for (const SessionSignal& s : std::exchange(pendingHostSignals_, {})) {
      host_.handleSignal(s);
    }
    host_.pushFrame(game_->frame());
    emit shareChanged();
  });
}

void SessionController::closeShare(const QString& note) {
  const bool was = shared_ || shareBusy_;
  host_.close();
  shared_ = false;
  shareBusy_ = false;
  own_ = SessionInfo();
  pendingViewers_.clear();
  pendingHostSignals_.clear();
  userResults_.clear();
  if (!note.isEmpty()) {
    say(note, false);
  }
  if (was) {
    emit shareChanged();
    emit userResultsChanged();
  }
}

void SessionController::stopSharing() {
  if (!shared_) {
    return;
  }
  const QString id = own_.sessionId;
  closeShare(QString());
  api_.end(id, [this](const SessionApiResult& r) {
    if (!r.ok() && r.kind != SessionApiResult::Kind::Ended && r.kind != SessionApiResult::Kind::NotFound) {
      say(errorText(r, tr("Ending the Session")), true);
    }
  });
}

void SessionController::setVisibility(const QString& v) {
  if (v != QLatin1String("private") && v != QLatin1String("hub_users") && v != QLatin1String("invite_only")) {
    return;
  }
  if (v == visibility_) {
    return;
  }
  const QString before = visibility_;
  visibility_ = v;
  const quint64 gen = ++visGen_;
  saveSettings();
  emit shareChanged();
  if (shared_) {
    api_.setVisibility(own_.sessionId, v, [this, before, gen](const SessionApiResult& r) {
      if (r.ok() && r.session) {
        applyOwnSession(*r.session, gen);
      } else if (shared_ && gen == visGen_) {
        visibility_ = before;
        saveSettings();
        say(errorText(r, tr("Changing the visibility")), true);
        emit shareChanged();
      }
    });
  }
}

void SessionController::searchUsers(const QString& text) {
  userQuery_ = text.trimmed();
  const auto filter = [this]() {
    userResults_.clear();
    if (!userQuery_.isEmpty()) {
      QStringList taken;
      for (const SessionInviteEntry& i : own_.invites) {
        taken << i.userId;
      }
      for (const HubUser& u : users_) {
        if (u.id == conn_->hubUserId() || taken.contains(u.id) || !u.displayName.contains(userQuery_, Qt::CaseInsensitive)) {
          continue;
        }
        userResults_.append(QVariantMap{
            {QStringLiteral("id"), u.id},
            {QStringLiteral("name"), u.displayName},
            {QStringLiteral("online"), u.online},
            {QStringLiteral("hint"), u.online ? tr("online") : tr("offline · receives the invite as long as the Session is running")}});
        if (userResults_.size() >= 6) {
          break;
        }
      }
    }
    emit userResultsChanged();
  };
  if (userQuery_.isEmpty() || (usersAge_.isValid() && usersAge_.elapsed() < 10000)) {
    filter();
    return;
  }
  api_.listUsers([this, filter](const SessionApiResult& r) {
    if (r.ok()) {
      users_ = r.users;
      usersAge_.restart();
    }
    filter();
  });
}

void SessionController::invite(const QString& userId) {
  if (!shared_) {
    return;
  }
  const quint64 gen = visGen_;
  api_.invite(own_.sessionId, userId, [this, gen](const SessionApiResult& r) {
    if (r.ok() && r.session && shared_) {
      applyOwnSession(*r.session, gen);
      searchUsers(userQuery_);
    } else if (!r.ok()) {
      say(errorText(r, tr("Inviting")), true);
    }
  });
}

void SessionController::withdrawInvite(const QString& userId) {
  if (!shared_) {
    return;
  }
  const QString id = own_.sessionId;
  api_.withdrawInvite(id, userId, [this, id](const SessionApiResult& r) {
    if (!r.ok()) {
      say(errorText(r, tr("Withdrawing the invite")), true);
    } else if (shared_ && own_.sessionId == id) {
      const quint64 gen = visGen_;
      api_.get(id, [this, gen](const SessionApiResult& g) {
        if (g.ok() && g.session && shared_) {
          applyOwnSession(*g.session, gen);
        }
      });
    }
  });
}

void SessionController::removeViewer(const QString& viewerId) {
  if (!shared_) {
    return;
  }
  host_.removeViewer(viewerId);  // media path is cut immediately
  api_.removeViewer(own_.sessionId, viewerId, [this](const SessionApiResult& r) {
    if (!r.ok() && r.kind != SessionApiResult::Kind::NotFound) {
      say(errorText(r, tr("Removing the viewer")), true);
    }
  });
}

void SessionController::onViewerJoined(const ViewerJoined& v) {
  if (shared_ && v.sessionId == own_.sessionId) {
    host_.addViewer(v.viewerId);
  } else if (shareBusy_) {
    pendingViewers_.append(v.viewerId);
  }
}

void SessionController::onViewerLeft(const ViewerLeft& v) {
  if (shared_ && v.sessionId == own_.sessionId) {
    host_.removeViewer(v.viewerId);
    emit shareChanged();
    return;
  }
  if ((watching_ || joining_) && v.sessionId == watched_.sessionId && (v.viewerId == viewerId_ || joining_)) {
    QString note = tr("You left the Session.");
    if (v.reason == QLatin1String("removed")) note = tr("You were removed from the Session.");
    else if (v.reason == QLatin1String("revoked")) note = tr("You can no longer watch this Session.");
    else if (v.reason == QLatin1String("disconnected")) note = tr("The Session connection was lost.");
    closeWatch(note, false);
  }
}

void SessionController::onSignal(const SessionSignal& s) {
  if (shared_ && host_.hasViewer(s.viewerId)) {
    host_.handleSignal(s);
  } else if (viewer_.isOpen() && s.viewerId == viewerId_) {
    viewer_.handleSignal(s);
  } else if (joining_ && s.sessionId == watched_.sessionId) {
    earlySignals_.append(s);  // the owner may offer before the join response arrives
  } else if (shareBusy_) {
    pendingHostSignals_.append(s);
  }
}

// ---------------------------------------------------------------- watching

void SessionController::watch(const QString& sessionId) {
  if (joining_ || !sessions_.contains(sessionId)) {
    return;
  }
  if (!socket_.isOpen()) {
    say(tr("The Hub is not reachable, so the Session cannot be joined yet."), true);
    return;
  }
  if (watching_) {
    closeWatch(QString(), true);
  }
  watched_ = sessions_.value(sessionId);
  joining_ = true;
  earlySignals_.clear();
  emit watchChanged();
  api_.join(sessionId, [this, sessionId](const SessionApiResult& r) {
    if (!joining_ || watched_.sessionId != sessionId) {
      if (r.ok() && r.join) {  // ended while joining: leave again
        api_.removeViewer(sessionId, r.join->viewerId, [](const SessionApiResult&) {});
      }
      return;
    }
    joining_ = false;
    if (!r.ok() || !r.join) {
      earlySignals_.clear();
      say(errorText(r, tr("Joining the Session")), true);
      emit watchChanged();
      return;
    }
    viewerId_ = r.join->viewerId;
    viewer_.open(sessionId, viewerId_, r.join->iceServers.isEmpty() ? socket_.helloAck().iceServers : r.join->iceServers);
    watching_ = true;
    swapped_ = false;
    tab_ = QStringLiteral("multiview");
    remoteAudio_.start(48000);
    audioClock_.restart();
    audioDueFrames_ = 0;
    audioTimer_.start();
    for (const SessionSignal& s : std::exchange(earlySignals_, {})) {
      viewer_.handleSignal(s);
    }
    applyAudioRouting();
    emit watchChanged();
    emit viewChanged();
  });
}

void SessionController::decline(const QString& sessionId) {
  api_.decline(sessionId, [this, sessionId](const SessionApiResult& r) {
    if (r.ok() || r.kind == SessionApiResult::Kind::NotFound || r.kind == SessionApiResult::Kind::Ended) {
      if (sessions_.remove(sessionId) > 0) {
        emit sessionsChanged();
      }
    } else {
      say(errorText(r, tr("Declining")), true);
    }
  });
}

void SessionController::leaveWatch() { closeWatch(QString(), true); }

void SessionController::closeWatch(const QString& note, bool callHub) {
  const bool was = watching_ || joining_;
  if (callHub && !viewerId_.isEmpty()) {
    api_.removeViewer(watched_.sessionId, viewerId_, [](const SessionApiResult&) {});
  }
  viewer_.close();
  audioTimer_.stop();
  remoteAudio_.stop();
  watching_ = false;
  joining_ = false;
  viewerId_.clear();
  earlySignals_.clear();
  remoteFrame_ = QImage();
  swapped_ = false;
  if (was) {
    emit remoteFrameChanged();
    applyAudioRouting();
    emit watchChanged();
    emit viewChanged();
  }
  if (!note.isEmpty()) {
    say(note, false);
  }
}

// ---------------------------------------------------------------- views / audio

QString SessionController::effectiveTab() const {
  if (!hasLocalGame() && tab_ == QLatin1String("session")) {
    return QStringLiteral("multiview");
  }
  return tab_;
}

QString SessionController::tab() const { return effectiveTab(); }

void SessionController::setTab(const QString& t) {
  if ((t != QLatin1String("session") && t != QLatin1String("multiview") && t != QLatin1String("diagnostics")) || t == tab_) {
    return;
  }
  tab_ = t;
  emit viewChanged();
}

void SessionController::setMultiviewMode(const QString& m) {
  if ((m != QLatin1String("pip") && m != QLatin1String("side")) || m == mode_) {
    return;
  }
  mode_ = m;
  emit viewChanged();
}

void SessionController::swapSurfaces() {
  swapped_ = !swapped_;
  emit viewChanged();
}

QString SessionController::audioFocus() const {
  if (!hasLocalGame()) {
    return QStringLiteral("remote");
  }
  if (!watching_) {
    return QStringLiteral("local");
  }
  return focusPref_;
}

void SessionController::audioHere(const QString& surface) {
  if (surface != QLatin1String("local") && surface != QLatin1String("remote")) {
    return;
  }
  focusPref_ = surface;
  applyAudioRouting();
  emit viewChanged();
}

void SessionController::applyAudioRouting() {
  game_->setAudioMuted(audioFocus() != QLatin1String("local"));  // exactly one audible surface
}

void SessionController::pumpAudio() {
  const qint64 target = audioClock_.elapsed() * 48;
  const qint64 n = std::min<qint64>(target - audioDueFrames_, 4800);
  if (n <= 0) {
    return;
  }
  audioDueFrames_ += n;
  const QByteArray pcm = viewer_.pullAudio(static_cast<int>(n));  // always drained, only played if focused
  if (audioFocus() == QLatin1String("remote")) {
    remoteAudio_.push(pcm);
  }
}

void SessionController::gameStarted(const QString& gameId, const QString& title) {
  gameId_ = gameId;
  gameTitle_ = title;
  focusPref_ = QStringLiteral("local");
  tab_ = watching_ ? QStringLiteral("multiview") : QStringLiteral("session");
  lastLocalFrameNr_ = 0;
  localFps_ = 0;
  applyAudioRouting();
  presence();
  emit gameChanged();
  emit viewChanged();
}

void SessionController::gameEnded() {
  if (gameId_.isEmpty()) {
    return;
  }
  stopSharing();
  gameId_.clear();
  gameTitle_.clear();
  game_->setAudioMuted(false);
  applyAudioRouting();
  presence();
  emit gameChanged();
  emit viewChanged();
}

// ---------------------------------------------------------------- messages / diagnostics

void SessionController::say(const QString& text, bool error) {
  message_ = text;
  messageIsError_ = error;
  messageTimer_.start();
  emit messageChanged();
}

void SessionController::dismissMessage() {
  if (!message_.isEmpty()) {
    message_.clear();
    emit messageChanged();
  }
}

void SessionController::setStatsOverride(const SessionStats* local, const SessionStats* remote) {
  override_ = local != nullptr || remote != nullptr;
  overrideLocalSet_ = local != nullptr;
  overrideRemoteSet_ = remote != nullptr;
  if (local) overrideLocal_ = *local;
  if (remote) overrideRemote_ = *remote;
  refreshDiagnostics();
}

QVariantList SessionController::formatDiagnostics(const QString& localName, bool hasLocal, bool shared, double localFps,
                                                  const SessionStats& l, const QString& remoteName, bool hasRemote,
                                                  const SessionStats& r) {
  QVariantList out;
  if (hasLocal) {
    const bool enc = shared && l.active && !l.encoderName.isEmpty();
    QStringList v;
    v << QStringLiteral("%1 fps").arg(enc ? l.fps : localFps, 0, 'f', 1);
    v << (enc ? QStringLiteral("encoder %1").arg(l.encoderName) : QStringLiteral("encoder off (not shared)"));
    v << (enc ? QStringLiteral("H.264") : QStringLiteral("codec n/a"));
    v << (enc ? mbit(l.videoBitrateKbps) : QStringLiteral("bitrate n/a"));
    v << (enc ? QStringLiteral("Opus %1 kbit/s").arg(l.audioBitrateKbps, 0, 'f', 0) : QStringLiteral("Opus n/a"));
    out.append(row(QStringLiteral("local"), localName, true, v));
  }
  if (hasRemote) {
    QStringList v;
    v << QStringLiteral("%1 fps").arg(r.fps, 0, 'f', 1);
    v << linkLabel(r.connectionType);
    v << (r.rttMs ? QStringLiteral("RTT %1 ms").arg(*r.rttMs, 0, 'f', 0) : QStringLiteral("RTT n/a"));
    v << mbit(r.videoBitrateKbps);
    v << (r.packetLossPercent ? QStringLiteral("loss %1 %").arg(*r.packetLossPercent, 0, 'f', 1) : QStringLiteral("loss n/a"));
    out.append(row(QStringLiteral("remote"), remoteName, true, v));
  }
  return out;
}

void SessionController::refreshDiagnostics() {
  const bool hasRemote = watching_ || (override_ && overrideRemoteSet_);
  const bool hasLocal = hasLocalGame() || (override_ && overrideLocalSet_);
  const SessionStats local = override_ && overrideLocalSet_ ? overrideLocal_ : host_.stats();
  const SessionStats remote = override_ && overrideRemoteSet_ ? overrideRemote_ : viewer_.stats();
  const bool shared = shared_ || (override_ && overrideLocalSet_ && local.active);
  const QString localName = tr("You · %1").arg(gameTitle_.isEmpty() ? tr("local game") : gameTitle_);
  const QString remoteName = watched_.owner.displayName.isEmpty() ? tr("Session") : watched_.owner.displayName;
  const QVariantList rows = formatDiagnostics(localName, hasLocal, shared, localFps_, local, remoteName, hasRemote, remote);
  if (rows != rows_) {
    rows_ = rows;
    emit diagnosticsChanged();
  }
}

}  // namespace framebeam::ui
