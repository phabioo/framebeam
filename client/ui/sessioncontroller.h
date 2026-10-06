#pragma once
// SessionController: Phase 4 Sessions for the QML UI (ADR 0006 D6). Owns the Hub WSS client (HubSocket), the
// session REST client, the SessionHost (own shared Session) and the SessionViewer (one watched Session) and
// exposes plain properties/actions to QML. Hub logic stays in network/, media in media/.

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "audiooutput.h"
#include "gamesession.h"
#include "hubconnection.h"
#include "hubsocket.h"
#include "mediastats.h"
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
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
  // Watching
  Q_PROPERTY(bool watching READ watching NOTIFY watchChanged)
  Q_PROPERTY(bool joining READ joining NOTIFY watchChanged)
  Q_PROPERTY(QString watchedWho READ watchedWho NOTIFY watchChanged)
  Q_PROPERTY(QString watchedGame READ watchedGame NOTIFY watchChanged)
  Q_PROPERTY(bool hasLocalGame READ hasLocalGame NOTIFY gameChanged)
  Q_PROPERTY(QString localTitle READ localTitle NOTIFY gameChanged)
  // Views: tab "session" | "multiview" | "diagnostics"; mode "pip" | "side"
  Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY viewChanged)
  Q_PROPERTY(QString multiviewMode READ multiviewMode WRITE setMultiviewMode NOTIFY viewChanged)
  Q_PROPERTY(bool swapped READ swapped NOTIFY viewChanged)
  Q_PROPERTY(QString audioFocus READ audioFocus NOTIFY viewChanged)  // effective: "local" | "remote"
  Q_PROPERTY(quint64 remoteFrameNumber READ remoteFrameNumber NOTIFY remoteFrameChanged)
  Q_PROPERTY(QVariantList diagnosticRows READ diagnosticRows NOTIFY diagnosticsChanged)

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
  QString message() const { return message_; }
  bool messageIsError() const { return messageIsError_; }
  bool watching() const { return watching_; }
  bool joining() const { return joining_; }
  QString watchedWho() const { return watched_.owner.displayName; }
  QString watchedGame() const { return watched_.gameTitle; }
  bool hasLocalGame() const { return !gameId_.isEmpty(); }
  QString localTitle() const { return gameTitle_; }
  QString tab() const;
  void setTab(const QString& tab);
  QString multiviewMode() const { return mode_; }
  void setMultiviewMode(const QString& mode);
  bool swapped() const { return swapped_; }
  QString audioFocus() const;
  quint64 remoteFrameNumber() const { return remoteFrameNr_; }
  QImage remoteFrame() const { return remoteFrame_; }
  QVariantList diagnosticRows() const { return rows_; }

  // Local game context (PlayerController): presence, share feed, audio focus.
  void gameStarted(const QString& gameId, const QString& title);
  void gameEnded();  // ends a shared Session too

  // Tests: Hub objects and fake statistics (no media path needed).
  HubSocket* socket() { return &socket_; }
  SessionApi* api() { return &api_; }
  SessionHost* host() { return &host_; }
  SessionViewer* viewer() { return &viewer_; }
  void setStatsOverride(const SessionStats* local, const SessionStats* remote);
  static QVariantList formatDiagnostics(const QString& localName, bool hasLocal, bool shared, double localFps, const SessionStats& local,
                                        const QString& remoteName, bool hasRemote, const SessionStats& remote);

  Q_INVOKABLE void refreshSessions();
  Q_INVOKABLE void shareSession();
  Q_INVOKABLE void stopSharing();
  Q_INVOKABLE void setVisibility(const QString& visibility);
  Q_INVOKABLE void searchUsers(const QString& text);
  Q_INVOKABLE void invite(const QString& userId);
  Q_INVOKABLE void withdrawInvite(const QString& userId);
  Q_INVOKABLE void removeViewer(const QString& viewerId);
  Q_INVOKABLE void watch(const QString& sessionId);  // "Watch Session" and "Join"
  Q_INVOKABLE void decline(const QString& sessionId);
  Q_INVOKABLE void leaveWatch();
  Q_INVOKABLE void swapSurfaces();
  Q_INVOKABLE void audioHere(const QString& surface);  // "local" | "remote"
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
  void remoteFrameChanged();
  void diagnosticsChanged();

 private:
  void onConnectionState(HubConnection::State s);
  void onHelloAck();
  void onSessionUpdated(const SessionInfo& s);
  void onSessionEnded(const SessionEnded& e);
  void onViewerJoined(const ViewerJoined& v);
  void onViewerLeft(const ViewerLeft& v);
  void onSignal(const SessionSignal& s);
  void applyOwnSession(const SessionInfo& s);
  void closeShare(const QString& note);
  void closeWatch(const QString& note, bool callHub);
  void say(const QString& text, bool error);
  void presence();
  void pumpAudio();
  void applyAudioRouting();
  void refreshDiagnostics();
  void loadSettings();
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
  SessionViewer viewer_;
  AudioOutput remoteAudio_;

  QHash<QString, SessionInfo> sessions_;  // visible Sessions (not own)
  quint64 sessionEventGen_ = 0;  // bumped by every live session event; stale GET /sessions results are dropped
  QTimer minuteTimer_;
  QTimer statsTimer_;
  QTimer audioTimer_;
  QTimer messageTimer_;
  QElapsedTimer audioClock_;
  qint64 audioDueFrames_ = 0;

  QString gameId_;
  QString gameTitle_;
  quint64 lastLocalFrameNr_ = 0;
  double localFps_ = 0;

  bool shared_ = false;
  bool shareBusy_ = false;
  QString visibility_ = QStringLiteral("hub_users");
  SessionInfo own_;
  QStringList pendingViewers_;
  QList<SessionSignal> pendingHostSignals_;
  QList<HubUser> users_;
  QElapsedTimer usersAge_;
  QString userQuery_;
  QVariantList userResults_;

  bool watching_ = false;
  bool joining_ = false;
  SessionInfo watched_;
  QString viewerId_;
  QList<SessionSignal> earlySignals_;
  QImage remoteFrame_;
  quint64 remoteFrameNr_ = 0;

  QString tab_ = QStringLiteral("session");
  QString mode_ = QStringLiteral("pip");
  bool swapped_ = false;
  QString focusPref_ = QStringLiteral("local");

  QString message_;
  bool messageIsError_ = false;
  QVariantList rows_;
  bool override_ = false;
  bool overrideLocalSet_ = false, overrideRemoteSet_ = false;
  SessionStats overrideLocal_, overrideRemote_;
};

}  // namespace framebeam::ui
