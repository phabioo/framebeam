#include "plugin_manager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <utility>
#include <functional>
#include <set>

namespace framebeam::plugins {

using update::SemVer;

namespace {

constexpr qint64 kMaxInstallBytes = 256ll * 1024 * 1024;
constexpr int kMaxInstallFiles = 20000;

// Copies src into dst (symlinks and ".git" are skipped). Returns an error text, empty on success.
QString copyTree(const QString& src, const QString& dst, qint64& bytes, int& files) {
  if (!QDir().mkpath(dst)) return QStringLiteral("cannot create %1").arg(dst);
  const QDir sd(src);
  const QFileInfoList entries = sd.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (const QFileInfo& e : entries) {
    if (e.isSymLink() || e.fileName() == QLatin1String(".git")) continue;
    const QString target = QDir(dst).filePath(e.fileName());
    if (e.isDir()) {
      const QString err = copyTree(e.absoluteFilePath(), target, bytes, files);
      if (!err.isEmpty()) return err;
    } else {
      bytes += e.size();
      if (bytes > kMaxInstallBytes || ++files > kMaxInstallFiles) return QStringLiteral("the plugin folder is too large (limit 256 MiB / 20000 files)");
      if (!QFile::copy(e.absoluteFilePath(), target)) return QStringLiteral("cannot copy %1").arg(e.fileName());
    }
  }
  return {};
}

}  // namespace

PluginManager::PluginManager(const QString& rootDir, QObject* parent) : QObject(parent), m_root(QDir::cleanPath(rootDir)) {
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(300);
  connect(&m_debounce, &QTimer::timeout, this, &PluginManager::flushChanged);
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &PluginManager::onWatchTriggered);
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &PluginManager::onWatchTriggered);
  loadState();
  scan();
}

void PluginManager::loadState() {
  QFile f(m_root + QStringLiteral("/state.json"));
  if (!f.open(QIODevice::ReadOnly)) return;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  const QJsonObject en = o.value(QLatin1String("enabled")).toObject();
  for (auto it = en.begin(); it != en.end(); ++it) m_enabled.insert(it.key(), it.value().toBool());
  const QJsonObject ln = o.value(QLatin1String("linked")).toObject();
  for (auto it = ln.begin(); it != ln.end(); ++it)
    if (it.value().isString()) m_linked.insert(it.key(), it.value().toString());
}

void PluginManager::saveState() const {
  QJsonObject en, ln;
  for (auto it = m_enabled.begin(); it != m_enabled.end(); ++it) en.insert(it.key(), it.value());
  for (auto it = m_linked.begin(); it != m_linked.end(); ++it) ln.insert(it.key(), it.value());
  QJsonObject o;
  o.insert(QStringLiteral("enabled"), en);
  o.insert(QStringLiteral("linked"), ln);
  QDir().mkpath(m_root);
  QSaveFile f(m_root + QStringLiteral("/state.json"));
  if (f.open(QIODevice::WriteOnly)) {
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
  }
}

PluginManager::Entry PluginManager::makeEntry(const QString& dir, const QString& fallbackId, bool devLinked) const {
  Entry e;
  const ManifestResult m = loadManifest(dir);
  e.valid = m.ok;
  if (m.ok) {
    e.info.manifest = m.manifest;
  } else {
    e.info.manifest.id = fallbackId;
    e.info.manifest.name = fallbackId;
    e.info.problems << m.error;
  }
  e.info.path = dir;
  e.info.cacheDir = m_root + QStringLiteral("/cache/") + e.info.manifest.id;
  e.info.enabled = m_enabled.value(e.info.manifest.id, false);
  e.info.devLinked = devLinked;
  e.info.origin = QStringLiteral("local");
  return e;
}

void PluginManager::scan() {
  m_entries.clear();
  const QDir inst(m_root + QStringLiteral("/installed"));
  const QFileInfoList dirs = inst.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo& d : dirs) {
    if (d.fileName().startsWith(QLatin1Char('.'))) continue;
    Entry e = makeEntry(d.absoluteFilePath(), d.fileName(), false);
    m_entries.insert(e.info.manifest.id, e);
  }
  for (auto it = m_linked.begin(); it != m_linked.end(); ++it) {
    Entry e = makeEntry(it.value(), it.key(), true);
    m_entries.insert(e.info.manifest.id, e);  // a linked folder overrides an installed copy of the same id
  }
  refreshWatcher();
}

void PluginManager::computeDependencyProblems(QMap<QString, QStringList>& out) const {
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
    if (!it->valid) continue;
    const Manifest& m = it->info.manifest;
    for (const Dependency& d : m.dependencies) {
      auto dep = m_entries.constFind(d.id);
      if (dep == m_entries.constEnd() || !dep->valid) {
        out[m.id] << QStringLiteral("needs %1, not installed").arg(d.id);
      } else if (!d.range.matches(dep->info.manifest.version)) {
        out[m.id] << QStringLiteral("needs %1 %2 · you have %3").arg(dep->info.manifest.name, d.range.text, dep->info.manifest.version.toString());
      }
    }
  }
  // Cycles: a node is on a cycle when it can reach itself.
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
    if (!it->valid) continue;
    const QString start = it.key();
    QStringList path{start};
    std::set<QString> seen;
    std::function<bool(const QString&)> dfs = [&](const QString& cur) -> bool {
      auto e = m_entries.constFind(cur);
      if (e == m_entries.constEnd() || !e->valid) return false;
      QStringList deps;
      for (const Dependency& d : e->info.manifest.dependencies) deps << d.id;
      deps.sort();
      for (const QString& d : std::as_const(deps)) {
        if (d == start) {
          path << d;
          return true;
        }
        if (seen.count(d)) continue;
        seen.insert(d);
        path << d;
        if (dfs(d)) return true;
        path.removeLast();
      }
      return false;
    };
    seen.insert(start);
    if (dfs(start)) out[start] << QStringLiteral("dependency cycle: %1").arg(path.join(QStringLiteral(" -> ")));
  }
  // A plugin that needs a plugin with problems cannot work either.
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
      if (!it->valid) continue;
      for (const Dependency& d : it->info.manifest.dependencies) {
        auto dep = m_entries.constFind(d.id);
        if (dep == m_entries.constEnd() || !dep->valid) continue;
        const bool depBad = !out.value(d.id).isEmpty();
        const QString msg = QStringLiteral("needs %1, which has problems").arg(dep->info.manifest.name);
        if (depBad && !out.value(it.key()).contains(msg)) {
          out[it.key()] << msg;
          changed = true;
        }
      }
    }
  }
}

QList<PluginInfo> PluginManager::list() const {
  QMap<QString, QStringList> deps;
  computeDependencyProblems(deps);
  QList<PluginInfo> out;
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
    PluginInfo i = it->info;
    i.problems << deps.value(it.key());
    out.append(i);
  }
  return out;
}

std::optional<PluginInfo> PluginManager::info(const QString& id) const {
  for (const PluginInfo& i : list())
    if (i.manifest.id == id) return i;
  return std::nullopt;
}

InstallResult PluginManager::installFromFolder(const QString& srcDir) {
  InstallResult r;
  const QFileInfo sfi(srcDir);
  if (!sfi.isDir()) {
    r.error = QStringLiteral("%1 is not a folder").arg(srcDir);
    return r;
  }
  const ManifestResult m = loadManifest(srcDir);
  if (!m.ok) {
    r.error = m.error;
    return r;
  }
  r.id = m.manifest.id;
  const QString dest = m_root + QStringLiteral("/installed/") + r.id;
  if (QFileInfo(dest).exists() && QFileInfo(dest).canonicalFilePath() == sfi.canonicalFilePath()) {
    r.error = QStringLiteral("this plugin is already installed from that folder");
    return r;
  }
  if (!QDir().mkpath(m_root + QStringLiteral("/installed"))) {
    r.error = QStringLiteral("cannot create the plugin folder");
    return r;
  }
  const QString tmp = m_root + QStringLiteral("/installed/.tmp-") + r.id;
  QDir(tmp).removeRecursively();
  qint64 bytes = 0;
  int files = 0;
  const QString err = copyTree(sfi.absoluteFilePath(), tmp, bytes, files);
  if (!err.isEmpty()) {
    QDir(tmp).removeRecursively();
    r.error = err;
    return r;
  }
  if (!loadManifest(tmp).ok) {  // the copy must be valid too (e.g. entry was a symlink)
    const QString e2 = loadManifest(tmp).error;
    QDir(tmp).removeRecursively();
    r.error = e2;
    return r;
  }
  QDir(dest).removeRecursively();
  if (!QDir().rename(tmp, dest)) {
    QDir(tmp).removeRecursively();
    r.error = QStringLiteral("cannot move the plugin into place");
    return r;
  }
  scan();
  saveState();
  r.ok = true;
  emit pluginsChanged();
  return r;
}

InstallResult PluginManager::linkDevFolder(const QString& srcDir) {
  InstallResult r;
  const QFileInfo sfi(srcDir);
  if (!sfi.isDir()) {
    r.error = QStringLiteral("%1 is not a folder").arg(srcDir);
    return r;
  }
  const ManifestResult m = loadManifest(srcDir);
  if (!m.ok) {
    r.error = m.error;
    return r;
  }
  r.id = m.manifest.id;
  m_linked.insert(r.id, sfi.canonicalFilePath());
  scan();
  saveState();
  r.ok = true;
  emit pluginsChanged();
  return r;
}

bool PluginManager::uninstall(const QString& id) {
  if (m_linked.remove(id)) {
    scan();
    saveState();
    emit pluginsChanged();
    return true;
  }
  const QString dest = m_root + QStringLiteral("/installed/") + id;
  const QFileInfo fi(dest);
  // Only plain ids below installed/ (an id from the UI must never walk out of it).
  if (id.contains(QLatin1Char('/')) || id.contains(QLatin1String("..")) || !fi.isDir()) return false;
  QDir(dest).removeRecursively();
  if (QFileInfo::exists(dest)) {
    // Something is still open or locked (Windows): do not report a half-removed plugin as uninstalled.
    scan();
    return false;
  }
  QDir(m_root + QStringLiteral("/cache/") + id).removeRecursively();  // a cache that cannot be removed does not block
  m_enabled.remove(id);
  scan();
  saveState();
  emit pluginsChanged();
  return true;
}

bool PluginManager::setEnabled(const QString& id, bool enabled) {
  const std::optional<PluginInfo> i = info(id);
  if (!i) return false;
  if (enabled && !i->problems.isEmpty()) return false;  // "can't be turned on until ..." (design 4d-2)
  m_enabled.insert(id, enabled);
  auto it = m_entries.find(id);
  if (it != m_entries.end()) it->info.enabled = enabled;
  saveState();
  emit pluginsChanged();
  return true;
}

void PluginManager::setDeveloperMode(bool on) {
  if (m_devMode == on) return;
  m_devMode = on;
  emit pluginsChanged();
}

GameResolution PluginManager::resolveForGame(const QString& system, const QString& code) const {
  return resolveWhere([&](const Manifest& m) { return m.matchesGame(system, code); });
}

GameResolution PluginManager::resolveForShell() const {
  return resolveWhere([](const Manifest& m) { return m.ui.screen || m.ui.detail; });
}

GameResolution PluginManager::resolveWhere(const std::function<bool(const Manifest&)>& wanted) const {
  GameResolution res;
  QMap<QString, QStringList> problems;
  computeDependencyProblems(problems);
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    if (!it->valid) problems[it.key()] << it->info.problems;

  // Candidates and their transitive requirements.
  QStringList members;
  QStringList queue;
  QStringList candidates;
  for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
    if (it->valid && it->info.enabled && wanted(it->info.manifest)) queue << it.key();
  candidates = queue;
  while (!queue.isEmpty()) {
    const QString id = queue.takeFirst();
    if (members.contains(id)) continue;
    members << id;
    const Entry& e = m_entries[id];
    for (const Dependency& d : e.info.manifest.dependencies)
      if (m_entries.contains(d.id) && m_entries[d.id].valid) queue << d.id;
  }
  members.sort();

  std::set<QString> blocked;
  for (const QString& id : std::as_const(members)) {
    QStringList reasons = problems.value(id);
    if (!m_devMode) reasons << QStringLiteral("unsigned local plugin: turn on Developer mode to run it");
    if (!reasons.isEmpty()) {
      blocked.insert(id);
      res.problems[id] = reasons;
    }
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const QString& id : std::as_const(members)) {
      if (blocked.count(id)) continue;
      for (const Dependency& d : m_entries[id].info.manifest.dependencies) {
        if (blocked.count(d.id)) {
          blocked.insert(id);
          res.problems[id] << QStringLiteral("needs %1, which cannot start").arg(m_entries[d.id].info.manifest.name);
          changed = true;
          break;
        }
      }
    }
  }

  std::set<QString> done;
  std::function<void(const QString&)> visit = [&](const QString& id) {
    if (done.count(id) || blocked.count(id) || !m_entries.contains(id)) return;
    done.insert(id);
    QStringList deps;
    for (const Dependency& d : m_entries[id].info.manifest.dependencies) deps << d.id;
    deps.sort();
    for (const QString& d : std::as_const(deps)) visit(d);
    PluginInfo i = m_entries[id].info;
    res.start.append(i);
  };
  for (const QString& id : std::as_const(candidates)) visit(id);  // only what a startable candidate needs
  return res;
}

// ---- developer-folder watching

void PluginManager::refreshWatcher() {
  const QStringList old = m_watcher.files() + m_watcher.directories();
  if (!old.isEmpty()) m_watcher.removePaths(old);
  int budget = 5000;
  for (auto it = m_linked.begin(); it != m_linked.end(); ++it) {
    if (!QFileInfo(it.value()).isDir()) continue;
    m_watcher.addPath(it.value());
    QDirIterator di(it.value(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (di.hasNext() && budget-- > 0) {
      di.next();
      if (di.fileInfo().isSymLink() || di.filePath().contains(QLatin1String("/.git"))) continue;
      m_watcher.addPath(di.filePath());
    }
  }
}

void PluginManager::onWatchTriggered(const QString& path) {
  for (auto it = m_linked.begin(); it != m_linked.end(); ++it)
    if (path == it.value() || path.startsWith(it.value() + QLatin1Char('/'))) m_dirtyIds.insert(it.key());
  m_debounce.start();  // restarts the 300 ms window
}

void PluginManager::flushChanged() {
  const QSet<QString> ids = std::exchange(m_dirtyIds, {});
  scan();  // re-reads manifests and re-registers the watched files (editors often replace files)
  for (const QString& id : ids) emit pluginChanged(id);
  emit pluginsChanged();
}

}  // namespace framebeam::plugins
