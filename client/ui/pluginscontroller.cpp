#include "pluginscontroller.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QUrl>
#include <algorithm>

#include "game_identity.h"
#include "gamesession.h"
#include "playersettings.h"

namespace framebeam::ui {

Q_LOGGING_CATEGORY(lcPlugins, "framebeam.plugins")

namespace {

using plugins::LogLevel;
using plugins::PluginStatus;

QString hex(quint64 v, int width = 8) {
  return QStringLiteral("0x%1").arg(v, width, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"));
}

QString levelName(LogLevel l) {
  switch (l) {
    case LogLevel::Debug: return QStringLiteral("debug");
    case LogLevel::Info: return QStringLiteral("info");
    case LogLevel::Warn: return QStringLiteral("warn");
    case LogLevel::Error: return QStringLiteral("error");
  }
  return QStringLiteral("info");
}

QString timeText(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz")); }

// Status dot of the console: running | loading | paused | error (a plugin is only paused by an error today).
QString statusKey(const plugins::ConsoleSnapshot& s) {
  switch (s.status) {
    case PluginStatus::Running: return QStringLiteral("running");
    case PluginStatus::Loading: return QStringLiteral("loading");
    case PluginStatus::Paused: return s.error.isEmpty() ? QStringLiteral("paused") : QStringLiteral("error");
    case PluginStatus::Stopped: return QStringLiteral("stopped");
  }
  return QStringLiteral("stopped");
}

QString statusText(const QString& key) {
  if (key == QLatin1String("running")) return PluginsController::tr("Running");
  if (key == QLatin1String("loading")) return PluginsController::tr("Starting");
  if (key == QLatin1String("paused")) return PluginsController::tr("Paused");
  if (key == QLatin1String("error")) return PluginsController::tr("Paused after an error");
  return PluginsController::tr("Stopped");
}

}  // namespace

PluginsController::PluginsController(PlayerSettings* settings, const QString& dataDir, GameSession* session, QObject* parent)
    : QObject(parent), settings_(settings), session_(session) {
  manager_ = std::make_unique<plugins::PluginManager>(QDir(dataDir).filePath(QStringLiteral("plugins")));
  manager_->setDeveloperMode(settings_ != nullptr && settings_->developerMode());
  host_ = std::make_unique<plugins::PluginHost>();
  adapter_ = std::make_shared<MemoryWatchAdapter>();

  consoleTimer_.setSingleShot(true);
  consoleTimer_.setInterval(100);  // console model: about 10 Hz at most
  connect(&consoleTimer_, &QTimer::timeout, this, &PluginsController::rebuildConsole);
  connect(host_.get(), &plugins::PluginHost::consoleChanged, this, [this]() { scheduleConsole(); });
  connect(host_.get(), &plugins::PluginHost::statusChanged, this, [this](const QString& id) {
    onStatus(id);
    scheduleConsole();
  });
  connect(manager_.get(), &plugins::PluginManager::pluginsChanged, this, &PluginsController::refreshPlugins);
  // A linked developer folder changed: reload that plugin while the game runs.
  connect(manager_.get(), &plugins::PluginManager::pluginChanged, this, [this](const QString& id) {
    refreshPlugins();
    if (running_ && runningIds_.contains(id)) {
      qCInfo(lcPlugins) << "plugin" << id << "reloading (files changed)";
      host_->reload(id);
    }
  });
  if (session_ != nullptr) {
    // Live-save restart: the new runner has a new memory watch set.
    connect(session_, &GameSession::memoryWatchReplaced, this, [this]() {
      if (running_) adapter_->bind(session_->memoryWatch());
    });
    connect(session_, &GameSession::stateChanged, this, [this]() {
      if (running_ && !session_->isActive()) gameStopped();
    });
  }
  refreshPlugins();
}

PluginsController::~PluginsController() {
  // Plugins first, then the adapter (unregisters its ranges), then the manager.
  if (host_) host_->stop();
  if (adapter_) adapter_->clear();
}

bool PluginsController::developerMode() const { return manager_->developerMode(); }

void PluginsController::setDeveloperMode(bool on) {
  if (manager_->developerMode() == on) return;
  if (settings_ != nullptr) settings_->setDeveloperMode(on);
  manager_->setDeveloperMode(on);
  refreshPlugins();
  emit developerModeChanged();
  setConsoleAvailable(on && running_);
  if (on) scheduleConsole();
}

QString PluginsController::localPath(const QString& pathOrUrl) {
  QString p = pathOrUrl.trimmed();
  if (p.startsWith(QLatin1String("file:"))) p = QUrl(p).toLocalFile();
  return p;
}

void PluginsController::setMessage(const QString& text, bool isError) {
  message_ = text;
  messageIsError_ = isError;
  emit messageChanged();
}

void PluginsController::dismissMessage() {
  if (!message_.isEmpty()) setMessage(QString(), false);
}

void PluginsController::installFromFolder(const QString& pathOrUrl) {
  const QString path = localPath(pathOrUrl);
  if (path.isEmpty()) return;
  const auto r = manager_->installFromFolder(path);
  if (r.ok) {
    const auto info = manager_->info(r.id);
    setMessage(tr("Installed %1. It is off until you turn it on.").arg(info ? info->manifest.name : r.id), false);
  } else {
    setMessage(tr("Could not install: %1").arg(r.error), true);
  }
  refreshPlugins();
}

void PluginsController::linkDevFolder(const QString& pathOrUrl) {
  if (!developerMode()) return;
  const QString path = localPath(pathOrUrl);
  if (path.isEmpty()) return;
  const auto r = manager_->linkDevFolder(path);
  if (r.ok) {
    const auto info = manager_->info(r.id);
    setMessage(tr("Linked %1. It reloads when its files change.").arg(info ? info->manifest.name : r.id), false);
  } else {
    setMessage(tr("Could not link: %1").arg(r.error), true);
  }
  refreshPlugins();
}

void PluginsController::uninstall(const QString& id) {
  manager_->uninstall(id);
  refreshPlugins();
}

void PluginsController::setPluginEnabled(const QString& id, bool enabled) {
  if (!manager_->setEnabled(id, enabled)) {
    setMessage(tr("This plugin cannot be turned on until its problems are fixed."), true);
  }
  refreshPlugins();
}

void PluginsController::refreshPlugins() {
  QVariantList out;
  int on = 0;
  const auto list = manager_->list();
  for (const plugins::PluginInfo& p : list) {
    QStringList problems = p.problems;
    const bool hasProblems = !problems.isEmpty();
    if (!hasProblems && p.enabled) ++on;
    if (!hasProblems && !manager_->developerMode()) problems << tr("Runs only while Developer mode is on");
    QString problemsText = problems.join(QStringLiteral(" · "));
    if (!problemsText.isEmpty()) problemsText[0] = problemsText[0].toUpper();
    out.append(QVariantMap{
        {QStringLiteral("id"), p.manifest.id},
        {QStringLiteral("name"), p.manifest.name},
        {QStringLiteral("version"), p.manifest.version.toString()},
        {QStringLiteral("origin"), p.origin},
        {QStringLiteral("originText"), tr("Local file")},
        {QStringLiteral("devLinked"), p.devLinked},
        {QStringLiteral("enabled"), p.enabled},
        {QStringLiteral("hasProblems"), hasProblems},
        {QStringLiteral("problemsText"), problemsText},
        {QStringLiteral("canToggle"), !hasProblems},
    });
  }
  plugins_ = out;
  summary_ = list.isEmpty() ? tr("None installed") : (on > 0 ? tr("%1 on").arg(on) : tr("None on"));
  emit pluginsChanged();
}

// ---------------------------------------------------------------- game lifecycle

void PluginsController::gameStarted(const QString& system, const QString& romPath, const QString& sha256, const QString& title,
                                    const QString& language) {
  if (running_) gameStopped();
  plugins::GameContext ctx;
  ctx.system = system;
  ctx.code = emu::readGameIdentity(romPath, system).code;
  ctx.sha256 = sha256;
  ctx.title = title;
  ctx.romPath = romPath;
  ctx.language = language.isEmpty() ? QStringLiteral("en") : language;
  const plugins::GameResolution res = manager_->resolveForGame(system, ctx.code);
  for (auto it = res.problems.begin(); it != res.problems.end(); ++it)
    qCInfo(lcPlugins) << "plugin" << it.key() << "not started:" << it.value().join(QStringLiteral("; "));
  if (res.start.isEmpty()) return;

  runningIds_.clear();
  for (const plugins::PluginInfo& p : res.start) runningIds_ << p.manifest.id;
  loggedStatus_.clear();
  clearedAt_.clear();
  selected_ = runningIds_.value(0);
  running_ = true;
  adapter_->bind(session_ != nullptr ? session_->memoryWatch() : nullptr);
  host_->start(ctx, adapter_, res.start);  // asynchronous: the game does not wait for the plugins
  setConsoleAvailable(developerMode());
  scheduleConsole();
}

void PluginsController::gameStopped() {
  if (!running_) return;
  host_->stop();
  adapter_->clear();
  adapter_->bind(nullptr);
  for (const QString& id : std::as_const(runningIds_)) qCInfo(lcPlugins) << "plugin" << id << "stopped";
  running_ = false;
  runningIds_.clear();
  setConsoleAvailable(false);
  if (!hasPreview_) {
    console_.clear();
    emit consoleChanged();
  }
}

void PluginsController::onStatus(const QString& id) {
  const auto snap = host_->consoleSnapshot(id);
  const int st = static_cast<int>(snap.status);
  if (loggedStatus_.value(id, -1) == st) return;
  loggedStatus_.insert(id, st);
  if (snap.status == PluginStatus::Running) {
    qCInfo(lcPlugins) << "plugin" << id << "started";
  } else if (snap.status == PluginStatus::Paused) {
    qCWarning(lcPlugins) << "plugin" << id << "paused:" << snap.error.section(QLatin1Char('\n'), 0, 0);
  }
}

// ---------------------------------------------------------------- console

void PluginsController::setConsoleAvailable(bool on) {
  if (hasPreview_) on = true;
  if (consoleAvailable_ == on) return;
  consoleAvailable_ = on;
  emit consoleAvailableChanged();
}

void PluginsController::scheduleConsole() {
  if (hasPreview_ || !running_ || !developerMode()) return;
  if (!consoleTimer_.isActive()) consoleTimer_.start();
}

QString PluginsController::visibleLogText(const QString& id) const {
  const auto snap = host_->consoleSnapshot(id);
  const qint64 cut = clearedAt_.value(id, 0);
  QStringList lines;
  for (const auto& l : snap.log)
    if (l.timeMs > cut) lines << QStringLiteral("%1 %2 %3").arg(timeText(l.timeMs), levelName(l.level).toUpper(), l.text);
  return lines.join(QLatin1Char('\n'));
}

void PluginsController::rebuildConsole() {
  if (hasPreview_ || !running_ || !developerMode()) return;
  QVariantList plist;
  for (const QString& id : std::as_const(runningIds_)) {
    const auto info = manager_->info(id);
    const QString key = statusKey(host_->consoleSnapshot(id));
    plist.append(QVariantMap{{QStringLiteral("id"), id},
                             {QStringLiteral("name"), info ? info->manifest.name : id},
                             {QStringLiteral("status"), key},
                             {QStringLiteral("statusText"), statusText(key)}});
  }
  if (!runningIds_.contains(selected_)) selected_ = runningIds_.value(0);
  const auto snap = host_->consoleSnapshot(selected_);
  const QString key = statusKey(snap);

  QVariantList watched;
  for (const auto& w : snap.watched)
    watched.append(QVariantMap{{QStringLiteral("address"), w.address}, {QStringLiteral("value"), w.value}, {QStringLiteral("label"), w.label}});
  QVariantList events;
  for (auto it = snap.events.crbegin(); it != snap.events.crend() && events.size() < 100; ++it)  // newest first
    events.append(QVariantMap{{QStringLiteral("time"), timeText(it->timeMs)},
                              {QStringLiteral("plugin"), it->plugin},
                              {QStringLiteral("kind"), it->name},
                              {QStringLiteral("text"), it->text}});
  QVariantList log;
  const qint64 cut = clearedAt_.value(selected_, 0);
  for (auto it = snap.log.crbegin(); it != snap.log.crend() && log.size() < 200; ++it) {  // newest first
    if (it->timeMs <= cut) continue;
    log.append(QVariantMap{{QStringLiteral("time"), timeText(it->timeMs)}, {QStringLiteral("level"), levelName(it->level)}, {QStringLiteral("text"), it->text}});
  }
  QVariantList regions;
  if (session_ != nullptr) {
    for (const auto& r : session_->memoryRegions())
      regions.append(QVariantMap{{QStringLiteral("start"), hex(r.start)},
                                 {QStringLiteral("length"), hex(r.len, 1)},
                                 {QStringLiteral("addrspace"), QString::fromStdString(r.addrspace)}});
  }
  console_ = QVariantMap{{QStringLiteral("plugins"), plist},
                         {QStringLiteral("selected"), selected_},
                         {QStringLiteral("status"), key},
                         {QStringLiteral("statusText"), statusText(key)},
                         {QStringLiteral("error"), snap.error},
                         {QStringLiteral("watched"), watched},
                         {QStringLiteral("events"), events},
                         {QStringLiteral("log"), log},
                         {QStringLiteral("regions"), regions},
                         {QStringLiteral("regionsText"), regions.isEmpty() ? tr("The core exposes no memory map") : QString()}};
  emit consoleChanged();
}

void PluginsController::selectConsolePlugin(const QString& id) {
  if (hasPreview_) {
    auto c = preview_;
    c.insert(QStringLiteral("selected"), id);
    console_ = c;
    emit consoleChanged();
    return;
  }
  if (!runningIds_.contains(id) || selected_ == id) return;
  selected_ = id;
  rebuildConsole();
}

void PluginsController::reloadPlugin(const QString& id) {
  if (!running_ || !runningIds_.contains(id)) return;
  qCInfo(lcPlugins) << "plugin" << id << "reloading";
  loggedStatus_.remove(id);
  host_->reload(id);
}

void PluginsController::reloadSelected() { reloadPlugin(selected_); }

void PluginsController::clearLog(const QString& id) {
  qint64 latest = QDateTime::currentMSecsSinceEpoch();
  if (running_)
    for (const auto& l : host_->consoleSnapshot(id).log) latest = std::max(latest, l.timeMs);
  clearedAt_.insert(id, latest);
  if (hasPreview_) {
    auto c = preview_;
    c.insert(QStringLiteral("log"), QVariantList());
    preview_ = c;
    console_ = c;
    emit consoleChanged();
    return;
  }
  rebuildConsole();
}

void PluginsController::copyLog(const QString& id) {
  QString text;
  if (hasPreview_) {
    for (const QVariant& v : preview_.value(QStringLiteral("log")).toList()) {
      const QVariantMap m = v.toMap();
      text += m.value(QStringLiteral("time")).toString() + QLatin1Char(' ') + m.value(QStringLiteral("level")).toString().toUpper() +
              QLatin1Char(' ') + m.value(QStringLiteral("text")).toString() + QLatin1Char('\n');
    }
  } else {
    text = visibleLogText(id);
  }
  if (QGuiApplication::clipboard() != nullptr) QGuiApplication::clipboard()->setText(text);
}

void PluginsController::setPreviewConsole(const QVariantMap& console) {
  hasPreview_ = !console.isEmpty();
  preview_ = console;
  console_ = console;
  consoleAvailable_ = !hasPreview_;  // force the signal below
  setConsoleAvailable(hasPreview_);
  emit consoleChanged();
}

}  // namespace framebeam::ui
