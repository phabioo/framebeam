#pragma once

#include <QFileSystemWatcher>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

#include "manifest.h"

namespace framebeam::plugins {

struct InstallResult {
  bool ok = false;
  QString id;
  QString error;  // clear English message when !ok
};

// Plugins to start for one game, in start order (dependencies first), and why other plugins were left out.
struct GameResolution {
  QList<PluginInfo> start;
  QMap<QString, QStringList> problems;  // plugin id -> reasons it is not started
};

// Installed plugins on disk: <root>/installed/<id>/, <root>/cache/<id>/, <root>/state.json (enabled flags, linked dev
// folders). All installs of this package are unsigned local files, which only run while developer mode is on.
// Single-threaded (UI thread).
class PluginManager : public QObject {
  Q_OBJECT
 public:
  explicit PluginManager(const QString& rootDir, QObject* parent = nullptr);

  QString rootDir() const { return m_root; }
  void scan();                                   // re-reads installed/ and linked folders
  QList<PluginInfo> list() const;                // sorted by id
  std::optional<PluginInfo> info(const QString& id) const;

  // Validates the manifest in `srcDir`, then copies the folder to installed/<id> (replacing a local copy of that id).
  // A new install is turned off.
  InstallResult installFromFolder(const QString& srcDir);
  // Runs straight from `srcDir` and reloads on file changes (~300 ms debounce). Does not copy.
  InstallResult linkDevFolder(const QString& srcDir);
  // Removes the installed copy and its cache, or only the link of a dev-linked folder (the folder is never deleted).
  bool uninstall(const QString& id);
  bool setEnabled(const QString& id, bool enabled);
  void setDeveloperMode(bool on);
  bool developerMode() const { return m_devMode; }

  // Enabled plugins whose games match, plus (transitively) the plugins they require, in start order. Plugins with
  // problems are listed in `problems` and not started, nor are plugins that depend on them.
  GameResolution resolveForGame(const QString& system, const QString& code) const;
  // Shell context (no game runs): enabled plugins that declare ui.screen or ui.detail, plus what they require.
  GameResolution resolveForShell() const;

 signals:
  void pluginsChanged();                       // installed/linked/enabled set changed
  void pluginChanged(const QString& id);       // files of a dev-linked plugin changed: reload it

 private:
  struct Entry {
    PluginInfo info;
    bool valid = false;
  };
  void loadState();
  void saveState() const;
  void refreshWatcher();
  void onWatchTriggered(const QString& path);
  void flushChanged();
  Entry makeEntry(const QString& dir, const QString& fallbackId, bool devLinked) const;
  GameResolution resolveWhere(const std::function<bool(const Manifest&)>& wanted) const;
  void computeDependencyProblems(QMap<QString, QStringList>& out) const;

  QString m_root;
  bool m_devMode = false;
  QMap<QString, bool> m_enabled;
  QMap<QString, QString> m_linked;   // plugin id -> folder
  QMap<QString, Entry> m_entries;
  QFileSystemWatcher m_watcher;
  QTimer m_debounce;
  QSet<QString> m_dirtyIds;
};

}  // namespace framebeam::plugins
