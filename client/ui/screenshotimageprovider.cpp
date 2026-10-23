#include "screenshotimageprovider.h"

#include <QMetaObject>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>

namespace uzume::ui {

namespace {
qint64 sizeOf(const ScreenshotCache::Entry& e) { return e.png.size() + e.image.sizeInBytes(); }

class ScreenshotResponse : public QQuickImageResponse {
  Q_OBJECT
 public:
  explicit ScreenshotResponse(const QSize& requested) : requested_(requested) {}
  QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }
  QString errorString() const override { return error_; }
  // In the thread of the response (the QML image thread): scales the image, then reports it.
  Q_INVOKABLE void deliver(const QImage& image, const QString& error) {
    image_ = image;
    if (!image_.isNull() && requested_.width() > 0 && requested_.height() > 0 &&
        (image_.width() > requested_.width() || image_.height() > requested_.height())) {
      image_ = image_.scaled(requested_, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    error_ = image_.isNull() ? (error.isEmpty() ? QStringLiteral("no image") : error) : QString();
    emit finished();
  }

 private:
  QSize requested_;
  QImage image_;
  QString error_;
};
}  // namespace

void ScreenshotCache::touch(const QString& id) {
  lru_.removeAll(id);
  lru_.append(id);
}

void ScreenshotCache::store(const QString& id, const Entry& e) {
  if (const auto it = entries_.constFind(id); it != entries_.cend()) {
    bytes_ -= sizeOf(*it);
  }
  entries_.insert(id, e);
  bytes_ += sizeOf(e);
  touch(id);
  // Least recently used first, but the entry just stored stays.
  while (bytes_ > kBudgetBytes && lru_.size() > 1) {
    const QString oldest = lru_.takeFirst();
    if (const auto it = entries_.constFind(oldest); it != entries_.cend()) {
      bytes_ -= sizeOf(*it);
      entries_.erase(it);
    }
  }
}

void ScreenshotCache::request(const QString& id, Done done) {
  if (const auto it = entries_.constFind(id); it != entries_.cend()) {
    touch(id);
    const Entry e = *it;
    done(e);
    return;
  }
  const bool first = !waiting_.contains(id);
  waiting_[id].append(std::move(done));
  if (!first) return;  // one request per image, the others wait for it
  if (!fetcher_) {
    auto waiters = waiting_.take(id);
    for (const Done& d : waiters) d(Entry());
    return;
  }
  fetcher_(id, [this, id](const QByteArray& png) {
    Entry e;
    if (!png.isEmpty()) {
      e.image = QImage::fromData(png, "PNG");
      if (!e.image.isNull()) {
        e.png = png;
        store(id, e);
      } else {
        e = Entry();
      }
    }
    auto waiters = waiting_.take(id);
    for (const Done& d : waiters) d(e);
  });
}

void ScreenshotCache::png(const QString& id, std::function<void(const QByteArray&)> done) {
  if (const auto it = entries_.constFind(id); it != entries_.cend()) {
    touch(id);
    done(it->png);
    return;
  }
  request(id, [done = std::move(done)](const Entry& e) { done(e.png); });
}

void ScreenshotCache::drop(const QString& id) {
  if (const auto it = entries_.constFind(id); it != entries_.cend()) {
    bytes_ -= sizeOf(*it);
    entries_.erase(it);
  }
  lru_.removeAll(id);
}

void ScreenshotCache::clear() {
  entries_.clear();
  lru_.clear();
  bytes_ = 0;
}

QQuickImageResponse* ScreenshotImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
  auto* response = new ScreenshotResponse(requestedSize);
  QPointer<ScreenshotResponse> weak(response);
  QPointer<ScreenshotCache> cache = cache_;
  if (cache.isNull() || id.isEmpty()) {
    QMetaObject::invokeMethod(response, [response]() { response->deliver(QImage(), QStringLiteral("no cache")); }, Qt::QueuedConnection);
    return response;
  }
  // The cache lives on the GUI thread: ask there and hand the answer back to the thread of the response.
  QMetaObject::invokeMethod(
      cache.data(),
      [cache, weak, id]() {
        if (cache.isNull()) return;
        cache->request(id, [weak](const ScreenshotCache::Entry& e) {
          if (weak.isNull()) return;
          const QImage image = e.image;
          QMetaObject::invokeMethod(weak.data(), [weak, image]() { if (!weak.isNull()) weak->deliver(image, QString()); }, Qt::QueuedConnection);
        });
      },
      Qt::QueuedConnection);
  return response;
}

}  // namespace uzume::ui

#include "screenshotimageprovider.moc"
