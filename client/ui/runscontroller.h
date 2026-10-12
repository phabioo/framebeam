#pragma once
// RunsController: the Runs screen of the Player (docs/design/plugins.md 4j-2). Owns the RunClient of the active Hub and
// presents it to QML: invite cards, runs by group, one run in detail (participants, status actions, event log), the
// "Start a run" dialog and "Play run", which first makes sure the exact Hub copy of every plugin of the run is on this
// device and then asks the PlayerController to start the participant's game with the run context.
#include <QAbstractItemModel>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "hubconnection.h"
#include "hublibrary.h"
#include "keyedrowsmodel.h"
#include "pluginscontroller.h"
#include "runclient.h"

namespace framebeam::ui {

class RunsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Provided by the PlayerController")

  // The sidebar entry is shown: the Hub offers runs (runs_v1) and an installed plugin asks for run.state.
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)
  Q_PROPERTY(int pendingInvites READ pendingInvites NOTIFY runsChanged)  // sidebar badge
  Q_PROPERTY(bool loaded READ loaded NOTIFY runsChanged)                 // the list from the Hub arrived
  // Rows {runId, headline, subText, joinedText} / {runId, name, status, statusText, tone, subText, timeText, action,
  // participants: [{name, color}], mine}. "active" holds Starting, Active and Paused runs.
  Q_PROPERTY(QAbstractItemModel* invites READ invites CONSTANT)
  Q_PROPERTY(QAbstractItemModel* active READ active CONSTANT)
  Q_PROPERTY(QAbstractItemModel* finished READ finished CONSTANT)
  Q_PROPERTY(QString activeTitle READ activeTitle NOTIFY runsChanged)  // "ACTIVE · 1 · PAUSED · 1"
  Q_PROPERTY(int finishedCount READ finishedCount NOTIFY runsChanged)
  // One run in detail. detail: {runId, name, status, statusText, tone, line, pluginsText, canPlay, playLabel, canStart,
  // canPause, canResume, canFinish, canAbandon, canLeave, canInvite, mine: bool, over: bool, note}.
  Q_PROPERTY(QString openRunId READ openRunId NOTIFY detailChanged)
  Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
  Q_PROPERTY(QAbstractItemModel* participants READ participants CONSTANT)  // {userId, name, color, gameTitle, statusText, me}
  Q_PROPERTY(QAbstractItemModel* events READ events CONSTANT)              // {seq, timeText, author, text, sourceText}
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)  // a request or the plugin preparation runs
  Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)
  // Games of the library the participant may bring: {id, title, system}; refilled by prepareGames().
  Q_PROPERTY(QAbstractItemModel* games READ games CONSTANT)
  // Start a run dialog.
  Q_PROPERTY(QAbstractItemModel* startPlugins READ startPlugins CONSTANT)  // {id, name, version, description, checked}
  Q_PROPERTY(QAbstractItemModel* startUsers READ startUsers CONSTANT)      // {id, name, online, checked}
  Q_PROPERTY(QString startError READ startError NOTIFY startStateChanged)
  Q_PROPERTY(bool startBusy READ startBusy NOTIFY startStateChanged)
  Q_PROPERTY(bool startUsersLoading READ startUsersLoading NOTIFY startStateChanged)

 public:
  RunsController(RunClient* client, PluginsController* plugins, HubLibrary* library, HubConnection* connection, QObject* parent = nullptr);

  bool available() const;
  int pendingInvites() const { return client_->pendingInviteCount(); }
  bool loaded() const { return client_->loaded(); }
  QAbstractItemModel* invites() { return &invites_; }
  QAbstractItemModel* active() { return &active_; }
  QAbstractItemModel* finished() { return &finished_; }
  QString activeTitle() const { return activeTitle_; }
  int finishedCount() const { return finished_.count(); }
  QString openRunId() const { return openRunId_; }
  QVariantMap detail() const { return detail_; }
  QAbstractItemModel* participants() { return &participants_; }
  QAbstractItemModel* events() { return &events_; }
  QString message() const { return message_; }
  bool messageIsError() const { return messageIsError_; }
  bool busy() const { return busy_; }
  QString busyText() const { return busyText_; }
  QAbstractItemModel* games() { return &games_; }
  QAbstractItemModel* startPlugins() { return &startPlugins_; }
  QAbstractItemModel* startUsers() { return &startUsers_; }
  QString startError() const { return startError_; }
  bool startBusy() const { return startBusy_; }
  bool startUsersLoading() const { return startUsersLoading_; }

  RunClient* client() { return client_; }

  // The run context for a game start: the bridge of fb.run and the pins of the plugins (null bridge: unknown run).
  RunLaunch launchFor(const QString& runId);

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void openRun(const QString& runId);
  Q_INVOKABLE void closeRun();
  // Fills `games` with the library games that fit the run's plugins (empty runId: every game); returns the count.
  Q_INVOKABLE int prepareGames(const QString& runId);
  Q_INVOKABLE void joinRun(const QString& runId, const QString& gameId);
  Q_INVOKABLE void declineRun(const QString& runId);
  Q_INVOKABLE void leaveRun(const QString& runId);
  // status: "active" (start / resume), "paused", "finished", "abandoned".
  Q_INVOKABLE void setRunStatus(const QString& runId, const QString& status);
  Q_INVOKABLE void playRun(const QString& runId);
  Q_INVOKABLE void dismissMessage();

  // Start a run (and invite more players into an open run).
  Q_INVOKABLE void beginStart();  // loads the users and the offered plugins
  Q_INVOKABLE void toggleStartPlugin(const QString& pluginId);
  Q_INVOKABLE void toggleStartUser(const QString& userId);
  Q_INVOKABLE void startRun(const QString& name, const QString& gameId);
  Q_INVOKABLE void inviteSelected(const QString& runId);
  Q_INVOKABLE void cancelStart();

 signals:
  void availableChanged();
  void runsChanged();
  void detailChanged();
  void messageChanged();
  void busyChanged();
  void startStateChanged();
  // Everything for the run is ready: start `gameId` with the run context (PlayerController).
  void launchRequested(const QString& runId, const QString& gameId);
  // A run was created (the Start dialog closes and the detail opens).
  void runCreated(const QString& runId);

 private:
  void scheduleRebuild();
  void rebuild();
  void rebuildDetail();
  void rebuildEvents();
  void rebuildStartPlugins();
  void rebuildStartUsers();
  void setMessage(const QString& text, bool error);
  void setBusy(bool busy, const QString& text = {});
  QString pluginName(const QString& pluginId) const;
  QString pluginsText(const RunInfo& run) const;
  QString gameTitle(const QString& gameId) const;
  QString userName(const RunInfo& run, const QString& userId) const;
  QString errorText(const RunResult& r, const QString& what) const;
  QString hubEventText(const RunEvent& e) const;
  QString timeText(const QString& iso) const;
  QVariantMap rowFor(const RunInfo& run) const;
  void ensureCopies(const RunInfo& run, std::function<void(bool ok, const QString& error)> done);
  QStringList gameSystemsFor(const RunInfo* run) const;  // empty = no constraint

  QPointer<RunClient> client_;
  PluginsController* plugins_;
  HubLibrary* library_;
  QPointer<HubConnection> conn_;
  KeyedRowsModel invites_{{QStringLiteral("runId"), QStringLiteral("headline"), QStringLiteral("subText"), QStringLiteral("joinedText")}, QStringLiteral("runId")};
  static QStringList runRoles() {
    return {QStringLiteral("runId"),  QStringLiteral("name"),     QStringLiteral("status"),  QStringLiteral("statusText"), QStringLiteral("tone"),
            QStringLiteral("subText"), QStringLiteral("timeText"), QStringLiteral("action"), QStringLiteral("participants")};
  }
  KeyedRowsModel active_{runRoles(), QStringLiteral("runId")};
  KeyedRowsModel finished_{runRoles(), QStringLiteral("runId")};
  KeyedRowsModel participants_{{QStringLiteral("userId"), QStringLiteral("name"), QStringLiteral("color"), QStringLiteral("gameTitle"), QStringLiteral("statusText"),
                                QStringLiteral("me")},
                               QStringLiteral("userId")};
  KeyedRowsModel events_{{QStringLiteral("seq"), QStringLiteral("timeText"), QStringLiteral("author"), QStringLiteral("text"), QStringLiteral("sourceText")},
                         QStringLiteral("seq")};
  KeyedRowsModel games_{{QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("system")}, QStringLiteral("id")};
  KeyedRowsModel startPlugins_{{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("version"), QStringLiteral("description"), QStringLiteral("checked")},
                               QStringLiteral("id")};
  KeyedRowsModel startUsers_{{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("online"), QStringLiteral("checked")}, QStringLiteral("id")};

  QString activeTitle_;
  QString openRunId_;
  QVariantMap detail_;
  QString message_;
  bool messageIsError_ = false;
  bool busy_ = false;
  QString busyText_;
  QString startError_;
  bool startBusy_ = false;
  bool startUsersLoading_ = false;
  QSet<QString> startPluginIds_;
  QSet<QString> startUserIds_;
  QList<HubUser> users_;
  bool rebuildQueued_ = false;
  bool lastAvailable_ = false;
  quint64 playGen_ = 0;
};

}  // namespace framebeam::ui
