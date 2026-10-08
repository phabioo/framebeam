#pragma once
// SessionController: Sessions for the QML UI (ADR 0006 D6, ADR 0012 D8). Owns the Hub WSS client (HubSocket), the
// session REST client, the SessionHost (own shared Session) and one SessionViewer per watched remote Session
// (up to kMaxSurfaces surfaces in total, the local game counts as one) and exposes plain properties/actions to
// QML. A surface is identified by "local" or by the Session id of a remote Session. Hub logic stays in network/,
// media in media/.

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "audiooutput.h"
#include "diagnosticsmodel.h"
#include "gamesession.h"
#include "hubconnection.h"
#include "hubsocket.h"
#include "mediastats.h"
#include "playersettings.h"
#include "profilestore.h"
#include "sessionapi.h"
#include "sessionhost.h"
#include "sessionviewer.h"

namespace framebeam::ui {

class SessionController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  // Hub link: "online" | "connecting" | "offline" (WSS); a running media path does not depend on it.
  Q_PROPERTY(QString hubLink READ hubLink NOTIFY linkChanged)
  Q_PROPERTY(bool available READ available NOTIFY linkChanged)  // Hub advertises sessions_v1
  // 3c: Sessions of others [{sessionId, title, meta, invited, visibility}]
  Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
  // 3g: own Session
  Q_PROPERTY(bool shared READ shared NOTIFY shareChanged)
  Q_PROPERTY(bool shareBusy READ shareBusy NOTIFY shareChanged)
  Q_PROPERTY(QString visibility READ visibility NOTIFY shareChanged)
  Q_PROPERTY(int viewerCount READ viewerCount NOTIFY shareChanged)
  Q_PROPERTY(QVariantList participants READ participants NOTIFY shareChanged)
  Q_PROPERTY(QString participantsTitle READ participantsTitle NOTIFY shareChanged)
  Q_PROPERTY(QVariantList userResults READ userResults NOTIFY userResultsChanged)
  Q_PROPERTY(QString userSearchHint READ userSearchHint NOTIFY userResultsChanged)  // empty-state text of the invite search
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
  // Watching
  Q_PROPERTY(bool watching READ watching NOTIFY watchChanged)
  Q_PROPERTY(bool joining READ joining NOTIFY watchChanged)
  Q_PROPERTY(QString watchedWho READ watchedWho NOTIFY watchChanged)  // first remote surface
  Q_PROPERTY(QString watchedGame READ watchedGame NOTIFY watchChanged)
  // Multiview surfaces (ADR 0012 D8): ids ("local" | Session id) in creation order (changes only when the set
  // changes), display order (index 0 is the main surface), info {id: {kind, name, meta}}.
  Q_PROPERTY(QStringList surfaceIds READ surfaceIds NOTIFY surfacesChanged)
  Q_PROPERTY(QStringList surfaceOrder READ surfaceOrder NOTIFY surfacesChanged)
  Q_PROPERTY(QVariantMap surfaceInfo READ surfaceInfo NOTIFY surfacesChanged)
  Q_PROPERTY(int surfaceCount READ surfaceCount NOTIFY surfacesChanged)
  Q_PROPERTY(int maxSurfaces READ maxSurfaces CONSTANT)
  Q_PROPERTY(bool canAddSurface READ canAddSurface NOTIFY watchChanged)  // fewer than four surfaces and no join running
  Q_PROPERTY(QStringList shownSessionIds READ shownSessionIds NOTIFY surfacesChanged)  // remote Sessions on a surface
  // Connection pill per remote surface {sessionId: {text, tone}} (D9): "Direct" | "Relayed (TURN)" | "Connecting" ...
  Q_PROPERTY(QVariantMap surfaceLinks READ surfaceLinks NOTIFY surfaceLinksChanged)
  Q_PROPERTY(QStringList availableLayouts READ availableLayouts NOTIFY surfacesChanged)  // "pip" | "side" (2) | "grid" (3, 4)
  Q_PROPERTY(QString mainSurface READ mainSurface NOTIFY surfacesChanged)
  Q_PROPERTY(bool hasLocalGame READ hasLocalGame NOTIFY gameChanged)
  Q_PROPERTY(QString localTitle READ localTitle NOTIFY gameChanged)
  // Views: tab "session" | "multiview" | "diagnostics"; layout "pip" | "side" | "grid" (a tile layout follows the surface count; availableLayouts is empty below two surfaces)
  Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY viewChanged)
  Q_PROPERTY(QString multiviewMode READ multiviewMode WRITE setMultiviewMode NOTIFY viewChanged)
  Q_PROPERTY(QString audioFocus READ audioFocus NOTIFY viewChanged)  // effective surface id: "local" | Session id
  // In-game screen layout (0.6 D12): "stacked" | "side" | "top"; applies to the running game and the remote pictures, reset
  // to "stacked" when a game starts. Which layouts exist comes from the system (GameSession::screenLayouts).
  Q_PROPERTY(QString screenLayout READ screenLayout WRITE setScreenLayout NOTIFY viewChanged)
  // Diagnostics overlay (3t-3y): states, formatted values.
  Q_PROPERTY(framebeam::ui::DiagnosticsModel* diagnostics READ diagnostics CONSTANT)
  // 3g: "Lena is relayed via the hub (TURN) · may lag slightly." (empty when nobody is relayed)
  Q_PROPERTY(QString relayHint READ relayHint NOTIFY shareChanged)

 public:
  SessionController(HubConnection* conn, ProfileStore* profiles, GameSession* game, QObject* parent = nullptr);
  ~SessionController() override;

  QString hubLink() const;
  bool available() const;
  QVariantList sessions() const;
  bool shared() const { return shared_; }
  bool shareBusy() const { return shareBusy_; }
  QString visibility() const { return visibility_; }
  int viewerCount() const;
  QVariantList participants() const;
  QString participantsTitle() const;
  QVariantList userResults() const { return userResults_; }
  QString userSearchHint() const { return userHint_; }
  QString message() const { return message_; }
  bool messageIsError() const { return messageIsError_; }
  static constexpr int kMaxSurfaces = 4;
  bool watching() const { return !remotes_.empty(); }
  bool joining() const { return joining_; }
  QString watchedWho() const { return remotes_.empty() ? QString() : remotes_.front()->info.owner.displayName; }
  QString watchedGame() const { return remotes_.empty() ? QString() : remotes_.front()->info.gameTitle; }
  QStringList surfaceIds() const;
  QStringList surfaceOrder() const { return order_; }
  QVariantMap surfaceInfo() const;
  QVariantMap surfaceLinks() const { return surfaceLinks_; }
  int surfaceCount() const { return static_cast<int>(order_.size()); }
  int maxSurfaces() const { return kMaxSurfaces; }
  bool canAddSurface() const { return !joining_ && surfaceCount() < kMaxSurfaces; }
  QStringList shownSessionIds() const;
  QStringList availableLayouts() const;
  QString mainSurface() const { return order_.isEmpty() ? QString() : order_.first(); }
  bool hasLocalGame() const { return !gameId_.isEmpty(); }
  QString localTitle() const { return gameTitle_; }
  QString tab() const;
  void setTab(const QString& tab);
  QString multiviewMode() const;
  void setMultiviewMode(const QString& mode);
  QString audioFocus() const;
  // Newest decoded frame / frame counter of a remote surface (empty / 0 if the Session is not shown).
  QImage remoteFrame(const QString& sessionId) const;
  quint64 remoteFrameNumber(const QString& sessionId) const;
  // Frames of silence/audio handed to the audio output on behalf of a surface (tests: muted surfaces stay at 0).
  qint64 audioFedFrames(const QString& surface) const { return fedFrames_.value(surface); }
  QString screenLayout() const { return screenLayout_; }
  void setScreenLayout(const QString& layout);
  DiagnosticsModel* diagnostics() const { return diagnostics_; }
  QString relayHint() const { return relayHint_; }
  // The Player's settings object (owned by the PlayerController): persists the open/closed states of the overlay.
  void setPlayerSettings(PlayerSettings* settings);  // diagnostics states + session visibility (migrates the legacy player-settings.json)

  // Local game context (PlayerController): presence, share feed, audio focus.
  void gameStarted(const QString& gameId, const QString& title);
  void gameEnded();  // ends a shared Session too

  // Tests: Hub objects and fake statistics (no media path needed).
  HubSocket* socket() { return &socket_; }
  SessionApi* api() { return &api_; }
  SessionHost* host() { return &host_; }
  SessionViewer* viewer(const QString& sessionId);  // nullptr if the Session is not shown
  // Fake statistics: local (nullptr keeps the real ones) and remote per Session id (missing ids keep the real ones).
  void setStatsOverride(const SessionStats* local, const QHash<QString, SessionStats>& remotes);
  // Fake host-side viewer links (tests/screenshots); an empty list with `on` = true means "no viewer links".
  void refreshDiagnostics();  // also runs every 500 ms while the overlay is open; tests and screenshots call it directly
  void setLinksOverride(bool on, const QList<ViewerLinkStats>& links);

  Q_INVOKABLE void refreshSessions();
  Q_INVOKABLE void shareSession();
  Q_INVOKABLE void stopSharing();
  Q_INVOKABLE void setVisibility(const QString& visibility);
  Q_INVOKABLE void searchUsers(const QString& text);
  Q_INVOKABLE void invite(const QString& userId);
  Q_INVOKABLE void withdrawInvite(const QString& userId);
  Q_INVOKABLE void removeViewer(const QString& viewerId);
  Q_INVOKABLE void watch(const QString& sessionId);  // "Watch Session", "Join" and "Add": adds a surface (max four)
  Q_INVOKABLE void decline(const QString& sessionId);
  Q_INVOKABLE void leaveWatch();                        // leaves every remote Session
  Q_INVOKABLE void removeSurface(const QString& sessionId);  // "Remove": leaves that Session only
  Q_INVOKABLE void makeMain(const QString& surface);    // "Swap": the surface takes the main position
  Q_INVOKABLE void audioHere(const QString& surface);   // "local" | Session id
  Q_INVOKABLE void dismissMessage();

 signals:
  void linkChanged();
  void sessionsChanged();
  void shareChanged();
  void userResultsChanged();
  void messageChanged();
  void watchChanged();
  void gameChanged();
  void viewChanged();
  void surfacesChanged();
  void remoteFrameChanged(const QString& sessionId);
  void surfaceLinksChanged();

 private:
  void onConnectionState(HubConnection::State s);
  void onHelloAck();
  void onSessionUpdated(const SessionInfo& s);
  void onSessionEnded(const SessionEnded& e);
  void onViewerJoined(const ViewerJoined& v);
  void onViewerLeft(const ViewerLeft& v);
  void onSignal(const SessionSignal& s);
  void applyOwnSession(const SessionInfo& s);
  void applyOwnSession(const SessionInfo& s, quint64 requestVisGen);
  quint64 requestVisToken() const;
  void closeShare(const QString& note);
  struct Remote;
  Remote* remote(const QString& sessionId) const;
  void closeRemote(const QString& sessionId, const QString& note, bool callHub);
  void cancelJoin();
  void say(const QString& text, bool error);
  void presence();
  void pumpAudio();
  void applyAudioRouting();
  void updateLinks();
  QList<ViewerLinkStats> viewerLinks() const;
  PlayerSettings* settings_ = nullptr;  // owned by the PlayerController
  void saveSettings() const;
  static QString visibilityLabel(const QString& v);
  QString effectiveTab() const;
  QString errorText(const SessionApiResult& r, const QString& what) const;

  HubConnection* conn_;
  ProfileStore* profiles_;
  GameSession* game_;
  SessionApi api_;
  HubSocket socket_;
  SessionHost host_;
  AudioOutput remoteAudio_;

  QHash<QString, SessionInfo> sessions_;  // visible Sessions (not own)
  int visInFlight_ = 0;          // visibility PATCHes not answered yet (a session_update meanwhile must not revert the choice)
  quint64 visGen_ = 0;           // bumped by every local visibility change (stale REST answers keep the newer one)
  quint64 sessionEventGen_ = 0;  // bumped by every live session event; stale GET /sessions results are dropped
  QTimer minuteTimer_;
  QTimer diagTimer_;  // 500 ms: link pills of the side panel, and the diagnostics values while the overlay is open
  QTimer audioTimer_;
  QTimer messageTimer_;
  QElapsedTimer audioClock_;
  qint64 audioDueFrames_ = 0;

  QString gameId_;
  QString gameTitle_;

  bool shared_ = false;
  bool shareBusy_ = false;
  QString visibility_ = QStringLiteral("hub_users");
  SessionInfo own_;
  QList<ViewerJoined> pendingViewers_;
  QList<SessionSignal> pendingHostSignals_;
  QList<HubUser> users_;
  QElapsedTimer usersAge_;
  QString userQuery_;
  QVariantList userResults_;
  QString userHint_;
  bool usersLoaded_ = false;

  // One watched remote Session = one viewer join = one SessionViewer.
  struct Remote {
    SessionInfo info;
    QString viewerId;
    SessionViewer* viewer = nullptr;  // owned (parent: the controller), deleted with deleteLater
    QImage frame;
    quint64 frameNr = 0;
  };
  std::vector<std::unique_ptr<Remote>> remotes_;  // creation order
  QStringList order_;                             // display order of the surfaces; first = main
  bool joining_ = false;
  SessionInfo joinInfo_;
  QList<SessionSignal> earlySignals_;
  QHash<QString, qint64> fedFrames_;

  QString tab_ = QStringLiteral("session");
  QString mode_ = QStringLiteral("pip");  // chosen layout: "pip" | "side" | "grid"
  QString focusPref_ = QStringLiteral("local");

  QString message_;
  bool messageIsError_ = false;
  QString screenLayout_ = QStringLiteral("stacked");
  DiagnosticsModel* diagnostics_ = nullptr;  // owned (parent: this)
  QString relayHint_;
  QVariantMap linkByViewer_;  // viewerId -> {text, tone}
  QVariantMap surfaceLinks_;  // remote Session id -> {text, tone}
  bool linksOverrideOn_ = false;
  QList<ViewerLinkStats> overrideLinks_;
  bool override_ = false;
  bool overrideLocalSet_ = false;
  SessionStats overrideLocal_;
  QHash<QString, SessionStats> overrideRemotes_;
};

}  // namespace framebeam::ui
