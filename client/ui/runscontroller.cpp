#include "runscontroller.h"

#include <QDateTime>
#include <QLocale>
#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <memory>

#include "run_session_bridge.h"
#include "semver.h"

namespace framebeam::ui {

Q_LOGGING_CATEGORY(lcRunsUi, "framebeam.runs.ui")

namespace {
QString statusText(const QString& s) {
  if (s == QLatin1String("starting")) return QObject::tr("Starting");
  if (s == QLatin1String("active")) return QObject::tr("Active");
  if (s == QLatin1String("paused")) return QObject::tr("❚❚ Paused");
  if (s == QLatin1String("finished")) return QObject::tr("Finished");
  if (s == QLatin1String("abandoned")) return QObject::tr("Abandoned");
  return s;
}
QString statusTone(const QString& s) {
  if (s == QLatin1String("active") || s == QLatin1String("starting")) return QStringLiteral("warn");  // accent in the Player
  if (s == QLatin1String("finished")) return QStringLiteral("ok");
  return QStringLiteral("neutral");
}
}  // namespace

RunsController::RunsController(RunClient* client, PluginsController* plugins, HubLibrary* library, HubConnection* connection, QObject* parent)
    : QObject(parent), client_(client), plugins_(plugins), library_(library), conn_(connection) {
  connect(client, &RunClient::runsChanged, this, &RunsController::scheduleRebuild);
  connect(client, &RunClient::runChanged, this, &RunsController::scheduleRebuild);
  connect(client, &RunClient::eventAdded, this, [this](const QString& id) {
    if (id == openRunId_) rebuildEvents();
  });
  connect(client, &RunClient::writeFailed, this, [this](const QString&, const QString& kind, const QString&, const QString& msg) {
    setMessage(tr("A run %1 could not be sent: %2").arg(kind == QLatin1String("event") ? tr("event") : kind, msg), true);
  });
  connect(connection, &HubConnection::stateChanged, this, &RunsController::scheduleRebuild);
  connect(plugins, &PluginsController::pluginsChanged, this, &RunsController::scheduleRebuild);
  connect(plugins, &PluginsController::hubPluginsChanged, this, &RunsController::scheduleRebuild);
  connect(library, &HubLibrary::loaded, this, &RunsController::scheduleRebuild);
  rebuild();
}

bool RunsController::available() const { return client_ && client_->supported() && plugins_->hasRunPlugin(); }

void RunsController::refresh() {
  if (client_->supported()) client_->refresh();
}

void RunsController::scheduleRebuild() {
  if (rebuildQueued_) return;
  rebuildQueued_ = true;
  QTimer::singleShot(0, this, [this]() {
    rebuildQueued_ = false;
    rebuild();
  });
}

// ------------------------------------------------------------------ names and texts

QString RunsController::pluginName(const QString& pluginId) const {
  if (const PluginHubClient* hub = plugins_->hubClient()) {
    for (const HubPluginInfo& p : hub->plugins())
      if (p.id == pluginId) return p.name;
  }
  if (const auto info = plugins_->manager()->info(pluginId)) return info->manifest.name;
  for (const plugins::PluginInfo& p : plugins_->manager()->hubCopies())
    if (p.manifest.id == pluginId) return p.manifest.name;
  return pluginId;
}

QString RunsController::pluginsText(const RunInfo& run) const {
  QStringList names;
  for (const RunPluginRef& p : run.plugins) names << pluginName(p.pluginId) + QLatin1Char(' ') + p.version;
  return names.join(QStringLiteral(" & "));
}

QString RunsController::gameTitle(const QString& gameId) const {
  if (gameId.isEmpty()) return {};
  for (const GameEntry& g : library_->games())
    if (g.id == gameId) return g.title;
  return tr("Game not in your library");
}

QString RunsController::userName(const RunInfo& run, const QString& userId) const {
  if (const RunParticipant* p = run.participant(userId)) return p->userName;
  return userId;
}

QString RunsController::timeText(const QString& iso) const {
  const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
  if (!dt.isValid()) return {};
  return QLocale().toString(dt.toLocalTime(), QLocale::ShortFormat);
}

QString RunsController::errorText(const RunResult& r, const QString& what) const {
  if (r.networkError) return tr("%1: the Hub is not reachable.").arg(what);
  if (r.status == 403) return tr("%1: you are not allowed to do that.").arg(what);
  if (r.status == 404) return tr("%1: this run does not exist (any more).").arg(what);
  if (r.status == 409) return tr("%1: the run changed in the meantime. Its current state is shown.").arg(what);
  if (r.status == 429) return tr("%1: too many requests, try again in a moment.").arg(what);
  return r.message.isEmpty() ? tr("%1 failed.").arg(what) : tr("%1: %2").arg(what, r.message);
}

QString RunsController::hubEventText(const RunEvent& e) const {
  const QString who = e.payload.value(QStringLiteral("user_name")).toString();
  if (e.type == QLatin1String("run.created")) return tr("%1 created the run \"%2\"").arg(who, e.payload.value(QStringLiteral("name")).toString());
  if (e.type == QLatin1String("run.joined")) return tr("%1 joined").arg(who);
  if (e.type == QLatin1String("run.declined")) return tr("%1 declined the invite").arg(who);
  if (e.type == QLatin1String("run.left")) return tr("%1 left the run").arg(who);
  if (e.type == QLatin1String("run.invited")) return tr("%1 invited %n player(s)", nullptr, static_cast<int>(e.payload.value(QStringLiteral("invited")).toArray().size())).arg(who);
  if (e.type == QLatin1String("run.status")) {
    const QString to = e.payload.value(QStringLiteral("status")).toString();
    const QString by = e.payload.value(QStringLiteral("by_user_name")).toString();
    if (to == QLatin1String("paused")) return tr("Run paused by %1").arg(by);
    if (to == QLatin1String("active")) return e.payload.value(QStringLiteral("previous")).toString() == QLatin1String("paused") ? tr("Run resumed by %1").arg(by) : tr("Run started by %1").arg(by);
    if (to == QLatin1String("finished")) return tr("Run finished by %1").arg(by);
    if (to == QLatin1String("abandoned")) return tr("Run abandoned by %1").arg(by);
  }
  return e.text.isEmpty() ? e.type : e.text;
}

// ------------------------------------------------------------------ rows

QVariantMap RunsController::rowFor(const RunInfo& run) const {
  const QString me = client_->myUserId();
  const RunParticipant* mine = run.participant(me);
  QVariantList parts;
  int joined = 0;
  for (const RunParticipant& p : run.participants) {
    if (p.status != QLatin1String("joined")) continue;
    ++joined;
    parts.append(QVariantMap{{QStringLiteral("name"), p.userName}, {QStringLiteral("color"), p.color}});
  }
  QString sub = pluginsText(run);
  if (mine != nullptr && !mine->gameId.isEmpty()) sub += QStringLiteral(" · ") + gameTitle(mine->gameId);
  QString status = run.status;
  QString text = statusText(run.status);
  QString tone = statusTone(run.status);
  if (mine != nullptr && mine->status == QLatin1String("left")) {
    text = tr("Left");
    tone = QStringLiteral("neutral");
  }
  const bool canOpen = !run.isOver() && mine != nullptr && mine->status == QLatin1String("joined");
  return {{QStringLiteral("runId"), run.runId},
          {QStringLiteral("name"), run.name},
          {QStringLiteral("status"), status},
          {QStringLiteral("statusText"), text},
          {QStringLiteral("tone"), tone},
          {QStringLiteral("subText"), sub},
          {QStringLiteral("timeText"), timeText(run.updatedAt)},
          {QStringLiteral("action"), canOpen ? tr("Open →") : tr("View")},
          {QStringLiteral("participants"), parts}};
}

void RunsController::rebuild() {
  const bool avail = available();
  const QString me = client_->myUserId();
  QList<QVariantMap> invites, active, finished;
  int activeN = 0, pausedN = 0;
  for (const RunInfo& run : client_->runs()) {
    const RunParticipant* mine = run.participant(me);
    if (mine == nullptr || mine->status == QLatin1String("declined")) continue;
    if (mine->status == QLatin1String("invited")) {
      if (run.isOver()) continue;
      int joined = 0;
      for (const RunParticipant& p : run.participants)
        if (p.status == QLatin1String("joined")) ++joined;
      invites.append({{QStringLiteral("runId"), run.runId},
                      {QStringLiteral("headline"), tr("%1 invites you to \"%2\"").arg(userName(run, run.createdBy), run.name)},
                      {QStringLiteral("subText"), pluginsText(run)},
                      {QStringLiteral("joinedText"), tr("%1 of %2 joined").arg(joined).arg(run.participants.size())}});
      continue;
    }
    if (run.isOver() || mine->status == QLatin1String("left")) {
      finished.append(rowFor(run));
    } else {
      active.append(rowFor(run));
      if (run.status == QLatin1String("paused")) ++pausedN; else ++activeN;
    }
  }
  invites_.setRows(invites);
  active_.setRows(active);
  finished_.setRows(finished);
  QString title;
  if (!active.isEmpty()) {
    title = tr("ACTIVE · %1").arg(activeN);
    if (pausedN > 0) title += tr(" · PAUSED · %1").arg(pausedN);
  }
  activeTitle_ = title;
  rebuildDetail();
  rebuildStartPlugins();
  if (!users_.isEmpty()) rebuildStartUsers();
  if (avail != lastAvailable_) {
    lastAvailable_ = avail;
    emit availableChanged();
  }
  emit runsChanged();
}

void RunsController::rebuildDetail() {
  QVariantMap d;
  QList<QVariantMap> parts;
  const auto run = openRunId_.isEmpty() ? std::nullopt : client_->run(openRunId_);
  if (run) {
    const QString me = client_->myUserId();
    const RunParticipant* mine = run->participant(me);
    const bool joined = mine != nullptr && mine->status == QLatin1String("joined");
    const bool creator = run->createdBy == me;
    const bool over = run->isOver();
    QString line = tr("Created by %1 · %2").arg(userName(*run, run->createdBy), timeText(run->createdAt));
    if (run->status == QLatin1String("paused"))
      line = tr("Paused by %1 · %2 · events still visible, no new ones recorded").arg(userName(*run, run->statusBy), timeText(run->updatedAt));
    else if (over)
      line = tr("%1 by %2 · %3").arg(statusText(run->status), userName(*run, run->statusBy), timeText(run->updatedAt));
    QString note;
    if (!joined && mine != nullptr && mine->status == QLatin1String("invited")) note = tr("You are invited. Join from the Runs list.");
    d = {{QStringLiteral("runId"), run->runId},
         {QStringLiteral("name"), run->name},
         {QStringLiteral("status"), run->status},
         {QStringLiteral("statusText"), statusText(run->status)},
         {QStringLiteral("tone"), statusTone(run->status)},
         {QStringLiteral("line"), line},
         {QStringLiteral("pluginsText"), pluginsText(*run)},
         {QStringLiteral("canPlay"), joined && !over},
         {QStringLiteral("playLabel"), mine != nullptr && !mine->gameId.isEmpty() ? tr("Play %1").arg(gameTitle(mine->gameId)) : tr("Play run")},
         {QStringLiteral("canStart"), joined && creator && run->status == QLatin1String("starting")},
         {QStringLiteral("canPause"), joined && run->status == QLatin1String("active")},
         {QStringLiteral("canResume"), joined && run->status == QLatin1String("paused")},
         {QStringLiteral("canFinish"), joined && creator && !over},
         {QStringLiteral("canAbandon"), joined && creator && !over},
         {QStringLiteral("canLeave"), joined && !creator && !over},
         {QStringLiteral("canInvite"), joined && !over && run->participants.size() < kRunMaxParticipants},
         {QStringLiteral("over"), over},
         {QStringLiteral("note"), note}};
    for (const RunParticipant& p : run->participants) {
      QString st;
      if (p.status == QLatin1String("joined")) st = tr("Joined");
      else if (p.status == QLatin1String("invited")) st = tr("Invited");
      else if (p.status == QLatin1String("declined")) st = tr("Declined");
      else st = tr("Left");
      parts.append({{QStringLiteral("userId"), p.userId},
                    {QStringLiteral("name"), p.userName},
                    {QStringLiteral("color"), p.color},
                    {QStringLiteral("gameTitle"), p.gameId.isEmpty() ? QString() : gameTitle(p.gameId)},
                    {QStringLiteral("statusText"), st},
                    {QStringLiteral("me"), p.userId == me}});
    }
  }
  participants_.setRows(parts);
  if (d != detail_) {
    detail_ = d;
    emit detailChanged();
  }
  rebuildEvents();
}

void RunsController::rebuildEvents() {
  QList<QVariantMap> rows;
  const auto run = openRunId_.isEmpty() ? std::nullopt : client_->run(openRunId_);
  if (run) {
    const QList<RunEvent> all = client_->events(openRunId_, 0, 500);
    for (auto it = all.crbegin(); it != all.crend() && rows.size() < 300; ++it) {  // newest first
      const RunEvent& e = *it;
      QString author;
      QString source;
      QString text = e.text;
      if (e.source == QLatin1String("hub")) {
        author = tr("Hub");
        source = tr("Hub");
        text = hubEventText(e);
      } else {
        author = e.userName;
        source = (e.source == QLatin1String("automatic") ? tr("automatic · %1's Player") : tr("manual · %1")).arg(e.userName);
        if (text.isEmpty()) text = e.type;
      }
      rows.append({{QStringLiteral("seq"), e.seq},
                   {QStringLiteral("timeText"), timeText(e.createdAt)},
                   {QStringLiteral("author"), author},
                   {QStringLiteral("text"), text},
                   {QStringLiteral("sourceText"), source}});
    }
  }
  events_.setRows(rows);
}

// ------------------------------------------------------------------ messages and busy state

void RunsController::setMessage(const QString& text, bool error) {
  if (message_ == text && messageIsError_ == error) return;
  message_ = text;
  messageIsError_ = error;
  emit messageChanged();
}

void RunsController::dismissMessage() { setMessage({}, false); }

void RunsController::setBusy(bool busy, const QString& text) {
  if (busy_ == busy && busyText_ == text) return;
  busy_ = busy;
  busyText_ = text;
  emit busyChanged();
}

// ------------------------------------------------------------------ opening a run

void RunsController::openRun(const QString& runId) {
  openRunId_ = runId.toLower();
  client_->catchUp(openRunId_);
  rebuildDetail();
}

void RunsController::closeRun() {
  openRunId_.clear();
  rebuildDetail();
}

// ------------------------------------------------------------------ games

QStringList RunsController::gameSystemsFor(const RunInfo* run) const {
  QStringList systems;
  if (run == nullptr || plugins_->hubClient() == nullptr) return systems;
  for (const RunPluginRef& ref : run->plugins) {
    for (const HubPluginInfo& p : plugins_->hubClient()->plugins()) {
      if (p.id != ref.pluginId || p.version != ref.version) continue;
      for (const QJsonValue& g : p.games) {
        const QString sys = g.toObject().value(QStringLiteral("system")).toString();
        if (!sys.isEmpty() && !systems.contains(sys)) systems << sys;
      }
    }
  }
  return systems;
}

int RunsController::prepareGames(const QString& runId) {
  const auto run = runId.isEmpty() ? std::nullopt : client_->run(runId.toLower());
  const QStringList systems = gameSystemsFor(run ? &*run : nullptr);
  QList<QVariantMap> rows;
  for (const GameEntry& g : library_->games()) {
    if (!systems.isEmpty() && !systems.contains(g.system)) continue;
    rows.append({{QStringLiteral("id"), g.id}, {QStringLiteral("title"), g.title}, {QStringLiteral("system"), g.system}});
  }
  std::sort(rows.begin(), rows.end(), [](const QVariantMap& a, const QVariantMap& b) {
    return a.value(QStringLiteral("title")).toString().localeAwareCompare(b.value(QStringLiteral("title")).toString()) < 0;
  });
  games_.setRows(rows);
  return static_cast<int>(rows.size());
}

// ------------------------------------------------------------------ actions

void RunsController::joinRun(const QString& runId, const QString& gameId) {
  if (gameId.isEmpty()) {
    setMessage(tr("Pick the game you play in this run."), true);
    return;
  }
  setBusy(true, tr("Joining…"));
  client_->joinRun(runId, gameId, [this, runId](const RunResult& r) {
    setBusy(false);
    if (!r.ok) {
      setMessage(errorText(r, tr("Joining")), true);
      return;
    }
    setMessage({}, false);
    openRun(runId);
    if (r.run) ensureCopies(*r.run, [this](bool, const QString&) { setBusy(false); });  // get the plugins ready in the background
  });
}

void RunsController::declineRun(const QString& runId) {
  setBusy(true, tr("Declining…"));
  client_->declineRun(runId, [this](const RunResult& r) {
    setBusy(false);
    setMessage(r.ok ? QString() : errorText(r, tr("Declining")), !r.ok);
  });
}

void RunsController::leaveRun(const QString& runId) {
  setBusy(true, tr("Leaving…"));
  client_->leaveRun(runId, [this](const RunResult& r) {
    setBusy(false);
    setMessage(r.ok ? QString() : errorText(r, tr("Leaving")), !r.ok);
  });
}

void RunsController::setRunStatus(const QString& runId, const QString& status) {
  setBusy(true);
  client_->setStatus(runId, status, [this, status](const RunResult& r) {
    setBusy(false);
    setMessage(r.ok ? QString() : errorText(r, tr("Changing the run")), !r.ok);
  });
}

void RunsController::ensureCopies(const RunInfo& run, std::function<void(bool, const QString&)> done) {
  if (plugins_->delivery() == nullptr) {
    done(false, tr("This Hub does not offer plugins."));
    return;
  }
  auto pending = std::make_shared<QList<RunPluginRef>>(run.plugins);
  auto step = std::make_shared<std::function<void()>>();
  *step = [this, pending, step, done = std::move(done)]() {
    if (pending->isEmpty()) {
      done(true, {});
      return;
    }
    const RunPluginRef ref = pending->takeFirst();
    setBusy(true, tr("Getting %1 %2 from the Hub…").arg(pluginName(ref.pluginId), ref.version));
    plugins_->delivery()->ensureHubCopy(ref.pluginId, ref.version, ref.sha256, [this, pending, step, done, ref](const plugins::DeliveryResult& res) {
      if (!res.ok) {
        setBusy(false);
        done(false, tr("%1 %2 could not be prepared: %3").arg(pluginName(ref.pluginId), ref.version, res.error));
        return;
      }
      (*step)();
    });
  };
  (*step)();
}

void RunsController::playRun(const QString& runId) {
  if (busy_) return;
  const auto run = client_->run(runId.toLower());
  const RunParticipant* mine = run ? run->participant(client_->myUserId()) : nullptr;
  if (!run || mine == nullptr || mine->status != QLatin1String("joined") || run->isOver()) {
    setMessage(tr("This run cannot be played any more."), true);
    return;
  }
  if (mine->gameId.isEmpty()) {
    setMessage(tr("You have not picked a game for this run."), true);
    return;
  }
  bool inLibrary = false;
  for (const GameEntry& g : library_->games())
    if (g.id == mine->gameId) inLibrary = true;
  if (!inLibrary) {
    setMessage(tr("The game of this run is not in your library."), true);
    return;
  }
  if (plugins_->delivery() == nullptr || !plugins_->hubSupported()) {
    setMessage(tr("The Hub does not offer the plugins of this run."), true);
    return;
  }
  const quint64 gen = ++playGen_;
  const QString game = mine->gameId;
  ensureCopies(*run, [this, runId, game, gen](bool ok, const QString& error) {
    if (gen != playGen_) return;
    setBusy(false);
    if (!ok) {
      setMessage(error, true);
      return;
    }
    setMessage({}, false);
    emit launchRequested(runId.toLower(), game);
  });
}

RunLaunch RunsController::launchFor(const QString& runId) {
  RunLaunch l;
  const auto run = client_->run(runId.toLower());
  if (!run) return l;
  for (const RunPluginRef& p : run->plugins) l.pins.append({p.pluginId, p.version, p.sha256});
  l.bridge = plugins::RunSessionBridge::create(client_, run->runId);
  return l;
}

// ------------------------------------------------------------------ Start a run

void RunsController::rebuildStartPlugins() {
  QList<QVariantMap> rows;
  if (const PluginHubClient* hub = plugins_->hubClient()) {
    QMap<QString, const HubPluginInfo*> best;
    for (const HubPluginInfo& p : hub->plugins()) {
      if (!p.permissions.contains(QStringLiteral("run.state"))) continue;
      const auto it = best.constFind(p.id);
      if (it == best.constEnd() || update::compareVersions(p.version, (*it)->version).value_or(0) > 0) best.insert(p.id, &p);
    }
    for (auto it = best.constBegin(); it != best.constEnd(); ++it)
      rows.append({{QStringLiteral("id"), it.key()},
                   {QStringLiteral("name"), (*it)->name},
                   {QStringLiteral("version"), (*it)->version},
                   {QStringLiteral("description"), (*it)->description},
                   {QStringLiteral("checked"), startPluginIds_.contains(it.key())}});
  }
  startPlugins_.setRows(rows);
}

void RunsController::rebuildStartUsers() {
  QList<QVariantMap> rows;
  const QString me = client_->myUserId();
  // Players that are already in the open run cannot be invited again.
  QSet<QString> skip{me};
  if (const auto run = openRunId_.isEmpty() ? std::nullopt : client_->run(openRunId_))
    for (const RunParticipant& p : run->participants)
      if (p.status == QLatin1String("joined") || p.status == QLatin1String("invited")) skip.insert(p.userId);
  for (const HubUser& u : users_) {
    if (skip.contains(u.id)) continue;
    rows.append({{QStringLiteral("id"), u.id}, {QStringLiteral("name"), u.displayName}, {QStringLiteral("online"), u.online}, {QStringLiteral("checked"), startUserIds_.contains(u.id)}});
  }
  startUsers_.setRows(rows);
}

void RunsController::beginStart() {
  startError_.clear();
  startBusy_ = false;
  startPluginIds_.clear();
  startUserIds_.clear();
  users_.clear();
  startUsersLoading_ = true;
  if (plugins_->hubClient() != nullptr) plugins_->refreshHubPlugins();
  rebuildStartPlugins();
  rebuildStartUsers();
  emit startStateChanged();
  client_->loadUsers([this](bool ok, const QList<HubUser>& users, const QString& error) {
    startUsersLoading_ = false;
    if (ok) {
      users_ = users;
      rebuildStartUsers();
    } else {
      startError_ = tr("The player list could not be loaded: %1").arg(error);
    }
    emit startStateChanged();
  });
  prepareGames(QString());
}

void RunsController::toggleStartPlugin(const QString& pluginId) {
  if (!startPluginIds_.remove(pluginId)) startPluginIds_.insert(pluginId);
  rebuildStartPlugins();
}

void RunsController::toggleStartUser(const QString& userId) {
  if (!startUserIds_.remove(userId)) startUserIds_.insert(userId);
  rebuildStartUsers();
}

void RunsController::cancelStart() {
  startError_.clear();
  startBusy_ = false;
  emit startStateChanged();
}

void RunsController::startRun(const QString& name, const QString& gameId) {
  const QString trimmed = name.trimmed();
  QString problem;
  if (trimmed.isEmpty() || trimmed.size() > 64) problem = tr("Give the run a name (1 to 64 characters).");
  else if (startPluginIds_.isEmpty()) problem = tr("Pick at least one plugin for the run.");
  else if (startPluginIds_.size() > 8) problem = tr("A run can use up to 8 plugins.");
  else if (gameId.isEmpty()) problem = tr("Pick the game you play in this run.");
  else if (startUserIds_.size() + 1 > kRunMaxParticipants) problem = tr("A run has room for %1 players.").arg(kRunMaxParticipants);
  if (!problem.isEmpty()) {
    startError_ = problem;
    emit startStateChanged();
    return;
  }
  QList<RunPluginRef> pins;
  for (const QVariantMap& row : startPlugins_.rows())
    if (startPluginIds_.contains(row.value(QStringLiteral("id")).toString()))
      pins.append({row.value(QStringLiteral("id")).toString(), row.value(QStringLiteral("version")).toString(), {}});
  startBusy_ = true;
  startError_.clear();
  emit startStateChanged();
  client_->createRun(trimmed, pins, gameId, QStringList(startUserIds_.begin(), startUserIds_.end()), QJsonObject(), [this](const RunResult& r) {
    startBusy_ = false;
    if (!r.ok || !r.run) {
      startError_ = errorText(r, tr("Starting the run"));
      emit startStateChanged();
      return;
    }
    emit startStateChanged();
    openRun(r.run->runId);
    ensureCopies(*r.run, [this](bool, const QString&) { setBusy(false); });
    emit runCreated(r.run->runId);
  });
}

void RunsController::inviteSelected(const QString& runId) {
  if (startUserIds_.isEmpty()) {
    startError_ = tr("Pick at least one player.");
    emit startStateChanged();
    return;
  }
  startBusy_ = true;
  startError_.clear();
  emit startStateChanged();
  client_->inviteUsers(runId, QStringList(startUserIds_.begin(), startUserIds_.end()), [this](const RunResult& r) {
    startBusy_ = false;
    startError_ = r.ok ? QString() : errorText(r, tr("Inviting"));
    emit startStateChanged();
    if (r.ok) startUserIds_.clear();
  });
}

}  // namespace framebeam::ui
