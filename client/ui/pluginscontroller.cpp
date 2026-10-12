#include "pluginscontroller.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QUrl>
#include <algorithm>

#include "game_identity.h"
#include "gamesession.h"
#include "playersettings.h"
#include "plugin_ui.h"

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
  if (settings_ != nullptr) {  // HUD placement and notice muting survive restarts
    hudCorner_ = settings_->pluginHudCorners();
    const QStringList hidden = settings_->pluginHudHidden();
    hudHidden_ = QSet<QString>(hidden.begin(), hidden.end());
    hudAllHidden_ = settings_->pluginHudAllHidden();
    const QHash<QString, QString> mutes = settings_->pluginToastMutes();
    for (auto it = mutes.begin(); it != mutes.end(); ++it) (it.value() == QLatin1String("important") ? mutedImportant_ : mutedAlways_).insert(it.key());
  }

  consoleTimer_.setSingleShot(true);
  consoleTimer_.setInterval(100);  // console model: about 10 Hz at most
  connect(&consoleTimer_, &QTimer::timeout, this, &PluginsController::rebuildConsole);
  connect(host_.get(), &plugins::PluginHost::consoleChanged, this, [this]() { scheduleConsole(); });
  connect(host_.get(), &plugins::PluginHost::statusChanged, this, [this](const QString& id) {
    onStatus(id);
    scheduleConsole();
    rebuildPanelTabs();
  });
  connect(host_.get(), &plugins::PluginHost::uiChanged, this, &PluginsController::onUiChanged);
  connect(manager_.get(), &plugins::PluginManager::pluginsChanged, this, &PluginsController::refreshPlugins);
  // A linked developer folder changed: reload that plugin while the game runs.
  connect(manager_.get(), &plugins::PluginManager::pluginChanged, this, [this](const QString& id) {
    refreshPlugins();
    if ((running_ && runningIds_.contains(id)) || (shellRunning_ && shellIds_.contains(id))) {
      qCInfo(lcPlugins) << "plugin" << id << "reloading (files changed)";
      host_->reload(id);
    }
  });
  if (session_ != nullptr) {
    // Live-save restart: the new runner has a new memory watch set.
    connect(session_, &GameSession::memoryWatchReplaced, this, [this]() {
      if (running_) adapter_->bind(session_->memoryWatch());
    });
    connect(session_, &GameSession::memoryRegionsChanged, this, [this]() { scheduleConsole(); });
    connect(session_, &GameSession::stateChanged, this, [this]() {
      if (inGame_ && !session_->isActive()) gameStopped();
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
        {QStringLiteral("originText"), p.origin == QLatin1String("official") ? tr("✓ Official")
                                       : p.origin == QLatin1String("hub") ? tr("! Hub upload · unverified") : tr("Local file")},
        {QStringLiteral("devLinked"), p.devLinked},
        {QStringLiteral("enabled"), p.enabled},
        {QStringLiteral("hasProblems"), hasProblems},
        {QStringLiteral("problemsText"), problemsText},
        {QStringLiteral("canToggle"), !hasProblems},
        {QStringLiteral("adds"), p.manifest.ui.addsText()},
    });
  }
  plugins_ = out;
  summary_ = list.isEmpty() ? tr("None installed") : (on > 0 ? tr("%1 on").arg(on) : tr("None on"));
  updateImageSources();
  emit pluginsChanged();
  updateShell();  // a toggle, install or Developer mode change can add or remove shell plugins
}

// ---------------------------------------------------------------- game lifecycle

void PluginsController::gameStarted(const QString& system, const QString& romPath, const QString& sha256, const QString& title,
                                    const QString& language) {
  if (inGame_ || running_) stopGame(false);
  inGame_ = true;
  language_ = language.isEmpty() ? QStringLiteral("en") : language;
  if (shellRunning_) {  // the shell set gives way to the game set
    host_->stop();
    shellRunning_ = false;
    shellIds_.clear();
    shellSignature_.clear();
  }
  plugins::GameContext ctx;
  ctx.system = system;
  ctx.code = emu::readGameIdentity(romPath, system).code;
  ctx.sha256 = sha256;
  ctx.title = title;
  ctx.romPath = romPath;
  ctx.language = language_;
  const plugins::GameResolution res = manager_->resolveForGame(system, ctx.code);
  for (auto it = res.problems.begin(); it != res.problems.end(); ++it)
    qCInfo(lcPlugins) << "plugin" << it.key() << "not started:" << it.value().join(QStringLiteral("; "));
  if (res.start.isEmpty()) {
    rebuildUiModels();
    return;
  }

  runningIds_.clear();
  for (const plugins::PluginInfo& p : res.start) runningIds_ << p.manifest.id;
  loggedStatus_.clear();
  clearedAt_.clear();
  selected_ = runningIds_.value(0);
  running_ = true;
  adapter_->bind(session_ != nullptr ? session_->memoryWatch() : nullptr);
  host_->start(ctx, adapter_, res.start);  // asynchronous: the game does not wait for the plugins
  postInitialEvents();
  rebuildUiModels();
  setConsoleAvailable(developerMode());
  scheduleConsole();
}

void PluginsController::gameStopped() { stopGame(true); }

void PluginsController::stopGame(bool restartShell) {
  if (!inGame_ && !running_) return;
  if (running_) {
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
  inGame_ = false;
  mutedSession_.clear();
  rebuildUiModels();
  if (restartShell) updateShell();
}

// Shell context: while no game runs, the enabled plugins with ui.screen or ui.detail run with an empty game.
void PluginsController::updateShell() {
  if (inGame_) return;
  const plugins::GameResolution res = manager_->resolveForShell();
  QStringList ids;
  QStringList sig;
  for (const plugins::PluginInfo& p : res.start) {
    ids << p.manifest.id;
    sig << p.manifest.id + QLatin1Char('@') + p.manifest.version.toString() + QLatin1Char('@') + p.path;
  }
  const QString signature = sig.join(QLatin1Char('|'));
  if (signature == shellSignature_ && shellRunning_ == !ids.isEmpty()) return;
  shellSignature_ = signature;
  shellIds_ = ids;
  if (ids.isEmpty()) {
    if (shellRunning_) host_->stop();
    shellRunning_ = false;
    rebuildUiModels();
    return;
  }
  plugins::GameContext ctx;
  ctx.shell = true;
  ctx.language = language_;
  shellRunning_ = true;
  host_->start(ctx, nullptr, res.start);
  postInitialEvents();
  rebuildUiModels();
}

// Host events carry state, so a restarted plugin set gets the current values again.
void PluginsController::postInitialEvents() {
  if (!detailGame_.isEmpty()) host_->postHostEvent(QStringLiteral("detail.game"), detailGame_);
  if (!tiles_.isEmpty()) host_->postHostEvent(QStringLiteral("tiles"), tiles_);
  for (const QString& id : std::as_const(hudHidden_)) host_->postHostEvent(QStringLiteral("hud.visible"), false, id);
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
    addHostNotice(id);
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
  emit reloadRequested(id);
  if (!((running_ && runningIds_.contains(id)) || (shellRunning_ && shellIds_.contains(id)))) return;
  qCInfo(lcPlugins) << "plugin" << id << "reloading";
  loggedStatus_.remove(id);
  host_->reload(id);
}

void PluginsController::reloadSelected() { reloadPlugin(selected_); }

void PluginsController::clearLog(const QString& id) {
  qint64 latest = QDateTime::currentMSecsSinceEpoch();
  if (running_)
    for (const auto& l : host_->consoleSnapshot(id).log) latest = (std::max)(latest, l.timeMs);
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

// ---------------------------------------------------------------- plugin UI (fb.ui)

QStringList PluginsController::activeIds() const { return inGame_ ? runningIds_ : shellIds_; }

void PluginsController::updateImageSources() {
  QHash<QString, PluginImageSource> src;
  for (const plugins::PluginInfo& p : manager_->list()) src.insert(p.manifest.id, PluginImageSource{p.cacheDir, p.path, p.manifest.assets});
  imageSources_->set(src);
}

QVariantMap PluginsController::slotTree(const QString& pluginId, const QString& slot) const {
  if (previewUi_) return previewUiData_.value(QStringLiteral("trees")).toMap().value(pluginId + QLatin1Char('|') + slot).toMap();
  return host_->uiTree(pluginId, slot);
}

QString PluginsController::pluginIconUrl(const QString& id) const {
  const auto info = manager_->info(id);
  if (info && info->manifest.assets.contains(QStringLiteral("icon.png"))) return QStringLiteral("image://fbplugin/%1/asset/icon.png").arg(id);
  return {};
}

QString PluginsController::originMarker(const QString& origin) const {
  if (origin == QLatin1String("official")) return {};
  return origin == QLatin1String("hub") ? tr("Unverified") : tr("Local");
}

void PluginsController::setPreviewUi(const QVariantMap& ui) {
  previewUi_ = !ui.isEmpty();
  previewUiData_ = ui;
  if (previewUi_) {
    panelTabs_ = ui.value(QStringLiteral("panelTabs")).toList();
    hudWidgets_ = ui.value(QStringLiteral("hudWidgets")).toList();
    toasts_ = ui.value(QStringLiteral("toasts")).toList();
    sidebarEntries_ = ui.value(QStringLiteral("sidebarEntries")).toList();
    emit panelTabsChanged();
    emit hudWidgetsChanged();
    emit toastsChanged();
    emit sidebarEntriesChanged();
    emit tileStripsChanged();
  } else {
    toasts_.clear();
    emit toastsChanged();
    rebuildUiModels();
  }
}

void PluginsController::onUiChanged(const QString& id, const QString& what) {
  if (previewUi_) return;
  if (what == QLatin1String("@toast")) {
    collectToasts();
    return;
  }
  if (!activeIds().contains(id)) return;
  if (what == QLatin1String("@meta")) {
    rebuildPanelTabs();
    rebuildSidebar();
    rebuildHud();
    return;
  }
  emit slotChanged(id, what);
  if (what == QLatin1String("hud")) rebuildHud();
  else if (what == QLatin1String("detail")) rebuildDetail();
  else if (what.startsWith(QLatin1String("tile:"))) emit tileStripsChanged();
  else if (what == QLatin1String("panel") || what == QLatin1String("screen")) {
    rebuildPanelTabs();
    rebuildSidebar();
  }
}

void PluginsController::rebuildUiModels() {
  if (previewUi_) return;
  rebuildPanelTabs();
  rebuildHud();
  rebuildSidebar();
  rebuildDetail();
  rebuildHotkeys();
  emit tileStripsChanged();
}

void PluginsController::rebuildPanelTabs() {
  if (previewUi_) return;
  QVariantList out;
  for (const QString& id : activeIds()) {
    const auto info = manager_->info(id);
    if (!info || !info->manifest.ui.panel) continue;
    const plugins::UiPluginState st = host_->uiState(id);
    const bool paused = st.paused || host_->consoleSnapshot(id).status == PluginStatus::Paused;
    out.append(QVariantMap{{QStringLiteral("pluginId"), id},
                           {QStringLiteral("name"), info->manifest.name},
                           {QStringLiteral("title"), st.titles.value(QStringLiteral("panel"), info->manifest.ui.panelTitle)},
                           {QStringLiteral("icon"), pluginIconUrl(id)},
                           {QStringLiteral("origin"), info->origin},
                           {QStringLiteral("dot"), paused ? QStringLiteral("paused") : st.dot},
                           {QStringLiteral("paused"), paused}});
  }
  if (out == panelTabs_) return;
  panelTabs_ = out;
  emit panelTabsChanged();
}

void PluginsController::rebuildHud() {
  if (previewUi_) return;
  QVariantList out;
  for (const QString& id : activeIds()) {
    const auto info = manager_->info(id);
    if (!info || !info->manifest.ui.hud) continue;
    const QVariantMap tree = host_->uiTree(id, QStringLiteral("hud"));
    if (tree.isEmpty()) continue;
    out.append(QVariantMap{{QStringLiteral("pluginId"), id},
                           {QStringLiteral("name"), info->manifest.name},
                           {QStringLiteral("icon"), pluginIconUrl(id)},
                           {QStringLiteral("corner"), hudCorner_.value(id, QStringLiteral("bottom-left"))},
                           {QStringLiteral("hidden"), hudHidden_.contains(id)},
                           {QStringLiteral("tree"), tree},
                           {QStringLiteral("origin"), info->origin}});
  }
  hudWidgets_ = out;
  emit hudWidgetsChanged();
}

void PluginsController::rebuildSidebar() {
  if (previewUi_) return;
  QVariantList out;
  for (const QString& id : activeIds()) {
    const auto info = manager_->info(id);
    if (!info || !info->manifest.ui.screen) continue;
    const plugins::UiPluginState st = host_->uiState(id);
    out.append(QVariantMap{{QStringLiteral("pluginId"), id},
                           {QStringLiteral("name"), info->manifest.name},
                           {QStringLiteral("title"), st.titles.value(QStringLiteral("screen"), info->manifest.ui.screenTitle)},
                           {QStringLiteral("icon"), pluginIconUrl(id)},
                           {QStringLiteral("badge"), st.badge},
                           {QStringLiteral("origin"), info->origin}});
  }
  if (out == sidebarEntries_) return;
  sidebarEntries_ = out;
  emit sidebarEntriesChanged();
}

void PluginsController::rebuildDetail() {
  if (previewUi_) return;
  QVariantList out;
  for (const QString& id : activeIds()) {
    const auto info = manager_->info(id);
    if (!info || !info->manifest.ui.detail) continue;
    const QVariantMap tree = host_->uiTree(id, QStringLiteral("detail"));
    if (tree.isEmpty()) continue;
    out.append(QVariantMap{{QStringLiteral("pluginId"), id}, {QStringLiteral("name"), info->manifest.name}, {QStringLiteral("origin"), info->origin}, {QStringLiteral("tree"), tree}});
  }
  detailSections_ = out;
  emit detailSectionsChanged();
}

void PluginsController::rebuildHotkeys() {
  if (previewUi_) return;
  QVariantList out;
  for (const QString& id : activeIds()) {
    const auto info = manager_->info(id);
    if (!info) continue;
    for (const plugins::UiHotkey& hk : info->manifest.ui.hotkeys)
      out.append(QVariantMap{{QStringLiteral("pluginId"), id}, {QStringLiteral("id"), hk.id}, {QStringLiteral("label"), hk.label}, {QStringLiteral("key"), hk.key}});
  }
  if (out == hotkeys_) return;
  hotkeys_ = out;
  emit hotkeysChanged();
}

QVariantList PluginsController::tileStrips(const QString& tileId) const {
  if (previewUi_) return previewUiData_.value(QStringLiteral("tileStrips")).toMap().value(tileId).toList();
  QVariantList out;
  const QString slot = QStringLiteral("tile:") + tileId;
  for (const QString& id : activeIds()) {
    const auto info = manager_->info(id);
    if (!info || !info->manifest.ui.tile) continue;
    const QVariantMap tree = host_->uiTree(id, slot);
    if (!tree.isEmpty())
      out.append(QVariantMap{{QStringLiteral("pluginId"), id}, {QStringLiteral("name"), info->manifest.name}, {QStringLiteral("icon"), pluginIconUrl(id)},
                             {QStringLiteral("origin"), info->origin}, {QStringLiteral("tree"), tree}});
  }
  return out;
}

void PluginsController::collectToasts() {
  if (previewUi_) return;
  bool changed = false;
  for (const QVariantMap& t : host_->takeToasts()) {
    const QString id = t.value(QStringLiteral("pluginId")).toString();
    if (mutedAlways_.contains(id) || mutedSession_.contains(id)) continue;
    if (mutedImportant_.contains(id) && t.value(QStringLiteral("tone")).toString() != QLatin1String("important")) continue;
    const auto info = manager_->info(id);
    QVariantMap toast = t;
    toast.insert(QStringLiteral("pluginName"), info ? info->manifest.name : id);
    toast.insert(QStringLiteral("origin"), info ? info->origin : QStringLiteral("local"));
    toasts_.append(toast);
    while (toasts_.size() > 10) toasts_.removeFirst();
    changed = true;
  }
  if (changed) emit toastsChanged();
}

// Host notice "FrameBeam · plugin paused" (not mutable: it is about the Player's own plugin handling).
void PluginsController::addHostNotice(const QString& pluginId) {
  if (previewUi_ || !activeIds().contains(pluginId)) return;
  const auto info = manager_->info(pluginId);
  if (!info) return;
  QVariantList actions{QVariantMap{{QStringLiteral("label"), tr("Restart plugin")}, {QStringLiteral("action"), QStringLiteral("restart")}, {QStringLiteral("value"), pluginId}}};
  if (developerMode())
    actions.append(QVariantMap{{QStringLiteral("label"), tr("Details")}, {QStringLiteral("action"), QStringLiteral("details")}, {QStringLiteral("value"), pluginId}});
  toasts_.append(QVariantMap{{QStringLiteral("toastId"), --hostToastId_},
                             {QStringLiteral("host"), true},
                             {QStringLiteral("pluginId"), pluginId},
                             {QStringLiteral("pluginName"), tr("FrameBeam")},
                             {QStringLiteral("tone"), QStringLiteral("warning")},
                             {QStringLiteral("title"), tr("plugin paused")},
                             {QStringLiteral("text"), tr("%1 was paused after an error. Your game keeps running.").arg(info->manifest.name)},
                             {QStringLiteral("actions"), actions},
                             {QStringLiteral("origin"), QStringLiteral("official")}});
  while (toasts_.size() > 10) toasts_.removeFirst();
  emit toastsChanged();
}

void PluginsController::dismissToast(int toastId) {
  for (int i = 0; i < toasts_.size(); ++i)
    if (toasts_[i].toMap().value(QStringLiteral("toastId")).toInt() == toastId) {
      toasts_.removeAt(i);
      emit toastsChanged();
      return;
    }
}

void PluginsController::muteToasts(const QString& pluginId, const QString& mode) {
  mutedSession_.remove(pluginId);
  mutedAlways_.remove(pluginId);
  mutedImportant_.remove(pluginId);
  QString persisted;
  if (mode == QLatin1String("session")) mutedSession_.insert(pluginId);
  else if (mode == QLatin1String("always")) {
    mutedAlways_.insert(pluginId);
    persisted = mode;
  } else if (mode == QLatin1String("important")) {
    mutedImportant_.insert(pluginId);
    persisted = mode;
  } else if (mode != QLatin1String("off")) return;
  if (settings_ != nullptr) settings_->setPluginToastMute(pluginId, persisted);
  QVariantList kept;
  for (const QVariant& v : std::as_const(toasts_)) {
    const QVariantMap t = v.toMap();
    if (t.value(QStringLiteral("host")).toBool() || t.value(QStringLiteral("pluginId")).toString() != pluginId) {
      kept.append(v);
    } else if (mode == QLatin1String("important") && t.value(QStringLiteral("tone")).toString() == QLatin1String("important")) {
      kept.append(v);
    }
  }
  if (kept.size() != toasts_.size()) {
    toasts_ = kept;
    emit toastsChanged();
  }
}

QString PluginsController::toastMuteMode(const QString& pluginId) const {
  if (mutedAlways_.contains(pluginId)) return QStringLiteral("always");
  if (mutedImportant_.contains(pluginId)) return QStringLiteral("important");
  if (mutedSession_.contains(pluginId)) return QStringLiteral("session");
  return {};
}

void PluginsController::sendAction(const QString& pluginId, const QString& slot, const QString& action, const QVariant& value) {
  emit actionSent(pluginId, slot, action, value);
  if (!activeIds().contains(pluginId) || action.isEmpty()) return;
  host_->sendAction(pluginId, slot, action, value);
}

void PluginsController::triggerHotkey(const QString& pluginId, const QString& hotkeyId) {
  if (!activeIds().contains(pluginId)) return;
  const auto info = manager_->info(pluginId);
  if (!info) return;
  for (const plugins::UiHotkey& hk : info->manifest.ui.hotkeys)
    if (hk.id == hotkeyId) {
      host_->sendAction(pluginId, QStringLiteral("hotkey"), hotkeyId, QVariant());
      return;
    }
}

void PluginsController::setDetailGame(const QString& system, const QString& code, const QString& sha256, const QString& title) {
  QVariantMap g;
  if (!system.isEmpty())
    g = QVariantMap{{QStringLiteral("system"), system}, {QStringLiteral("code"), code}, {QStringLiteral("sha256"), sha256}, {QStringLiteral("title"), title}};
  if (g == detailGame_) return;
  detailGame_ = g;
  host_->clearSlot(QStringLiteral("detail"));  // the host clears the section; the plugin sets a new one on the event
  host_->postHostEvent(QStringLiteral("detail.game"), g.isEmpty() ? QVariant() : QVariant(g));
}

QVariantList PluginsController::pluginsForGame(const QString& system, const QString& code) const {
  QVariantList out;
  if (system.isEmpty()) return out;
  for (const plugins::PluginInfo& p : manager_->list())
    if (p.enabled && p.problems.isEmpty() && manager_->developerMode() && p.manifest.matchesGame(system, code))
      out.append(QVariantMap{{QStringLiteral("id"), p.manifest.id}, {QStringLiteral("name"), p.manifest.name}, {QStringLiteral("origin"), p.origin}});
  return out;
}

QString PluginsController::gameCode(const QString& system, const QString& romPath) const {
  return romPath.isEmpty() || !QFileInfo::exists(romPath) ? QString() : emu::readGameIdentity(romPath, system).code;
}

void PluginsController::setTiles(const QVariantList& tiles) {
  if (tiles == tiles_) return;
  tiles_ = tiles;
  emit tilesSet(tiles_);
  host_->postHostEvent(QStringLiteral("tiles"), tiles_);
}

void PluginsController::setHudCorner(const QString& pluginId, const QString& corner) {
  // The top-right corner belongs to the diagnostics overlay.
  static const QStringList ok = {QStringLiteral("top-left"), QStringLiteral("bottom-left"), QStringLiteral("bottom-right")};
  if (!ok.contains(corner) || hudCorner_.value(pluginId, QStringLiteral("bottom-left")) == corner) return;
  hudCorner_.insert(pluginId, corner);
  if (settings_ != nullptr) settings_->setPluginHudCorner(pluginId, corner);
  rebuildHud();
  if (previewUi_) applyPreviewHud();
}

void PluginsController::hideHud(const QString& pluginId) {
  if (hudHidden_.contains(pluginId)) return;
  hudHidden_.insert(pluginId);
  if (settings_ != nullptr) settings_->setPluginHudHidden(pluginId, true);
  host_->postHostEvent(QStringLiteral("hud.visible"), false, pluginId);
  rebuildHud();
  if (previewUi_) applyPreviewHud();
}

void PluginsController::showHud(const QString& pluginId) {
  if (!hudHidden_.remove(pluginId)) return;
  if (settings_ != nullptr) settings_->setPluginHudHidden(pluginId, false);
  host_->postHostEvent(QStringLiteral("hud.visible"), true, pluginId);
  rebuildHud();
  if (previewUi_) applyPreviewHud();
}

void PluginsController::setHudAllHidden(bool hidden) {
  if (hidden == hudAllHidden_) return;
  hudAllHidden_ = hidden;
  if (settings_ != nullptr) settings_->setPluginHudAllHidden(hidden);
  emit hudAllHiddenChanged();
}

// Preview models carry corner/hidden in the entries; keep them in step with the setters above.
void PluginsController::applyPreviewHud() {
  QVariantList out;
  for (const QVariant& v : std::as_const(hudWidgets_)) {
    QVariantMap w = v.toMap();
    const QString id = w.value(QStringLiteral("pluginId")).toString();
    if (hudCorner_.contains(id)) w.insert(QStringLiteral("corner"), hudCorner_.value(id));
    w.insert(QStringLiteral("hidden"), hudHidden_.contains(id));
    out.append(w);
  }
  hudWidgets_ = out;
  emit hudWidgetsChanged();
}

}  // namespace framebeam::ui
