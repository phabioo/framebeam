#pragma once
// image://screenshots/<screenshotId>: the screenshots of the game page. The PNG comes from the connected server (authenticated by
// the app's own connection, never by a URL), is decoded once and kept in a small in-memory cache (least recently used, bounded
// in bytes). The provider is asynchronous: QML asks from its image thread, the cache lives on the GUI thread.
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQuickAsyncImageProvider>
#include <QSize>
#include <QString>
#include <functional>

namespace uzume::ui {

class ScreenshotCache : public QObject {
  Q_OBJECT
 public:
  struct Entry {
    QByteArray png;
    QImage image;
    bool isNull() const { return image.isNull(); }
  };
  using Done = std::function<void(const Entry&)>;
  // Loads the PNG of a screenshot from the server; calls `done` with the bytes (empty on failure).
  using Fetcher = std::function<void(const QString& id, std::function<void(const QByteArray&)> done)>;

  static constexpr qint64 kBudgetBytes = 64ll * 1024 * 1024;

  explicit ScreenshotCache(QObject* parent = nullptr) : QObject(parent) {}
  void setFetcher(Fetcher fetcher) { fetcher_ = std::move(fetcher); }
  // GUI thread. `done` gets the entry (a null entry when the image could not be loaded); it may run at once on a cache hit.
  void request(const QString& id, Done done);
  // The PNG of a screenshot, for "Save as...": from the cache or the server.
  void png(const QString& id, std::function<void(const QByteArray&)> done);
  void drop(const QString& id);
  void clear();
  int size() const { return static_cast<int>(entries_.size()); }
  qint64 bytes() const { return bytes_; }
  bool contains(const QString& id) const { return entries_.contains(id); }

 private:
  void store(const QString& id, const Entry& e);
  void touch(const QString& id);

  Fetcher fetcher_;
  QHash<QString, Entry> entries_;
  QList<QString> lru_;  // oldest first
  QHash<QString, QList<Done>> waiting_;
  qint64 bytes_ = 0;
};

class ScreenshotImageProvider : public QQuickAsyncImageProvider {
 public:
  explicit ScreenshotImageProvider(ScreenshotCache* cache) : cache_(cache) {}
  QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

 private:
  QPointer<ScreenshotCache> cache_;
};

}  // namespace uzume::ui
