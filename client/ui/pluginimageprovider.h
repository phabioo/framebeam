#pragma once
// image://fbplugin/<pluginId>/cache/<path> and image://fbplugin/<pluginId>/asset/<path>: PNG icons of plugins for the
// plugin blocks. Reads only inside the plugin's cache folder or its declared assets; rejects "..", symlinks that leave
// the folder, files larger than 8 MiB and images larger than 4096x4096. Called from QML image threads: the sources are
// kept in a mutex-protected registry that the PluginsController updates.
#include <QHash>
#include <QImage>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <memory>
#include <mutex>

namespace framebeam::ui {

inline constexpr qint64 kMaxPluginImageBytes = 8ll * 1024 * 1024;
inline constexpr int kMaxPluginImageDim = 4096;

struct PluginImageSource {
  QString cacheDir;     // <plugins>/cache/<id>
  QString pluginDir;    // folder the plugin runs from
  QStringList assets;   // manifest "assets": files, or folders whose content is allowed
};

class PluginImageSources {
 public:
  void set(const QHash<QString, PluginImageSource>& sources);
  bool get(const QString& pluginId, PluginImageSource& out) const;

 private:
  mutable std::mutex m_mutex;
  QHash<QString, PluginImageSource> m_sources;
};

class PluginImageProvider : public QQuickImageProvider {
 public:
  explicit PluginImageProvider(std::shared_ptr<PluginImageSources> sources);
  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

  // `id` = "<pluginId>/<cache|asset>/<path>". Returns the absolute file path, or an empty string and sets `error`.
  static QString resolve(const PluginImageSource& src, const QString& id, QString* error);
  // Loads a PNG with the size and dimension limits; a null image and `error` on failure.
  static QImage loadPng(const QString& path, QString* error);

 private:
  std::shared_ptr<PluginImageSources> m_sources;
};

}  // namespace framebeam::ui
