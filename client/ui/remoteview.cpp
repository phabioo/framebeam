#include "remoteview.h"

#include <QPainter>

namespace framebeam::ui {

RemoteView::RemoteView(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(false); }

void RemoteView::setController(SessionController* c) {
  if (controller_ == c) {
    return;
  }
  if (controller_) {
    controller_->disconnect(this);
  }
  controller_ = c;
  if (controller_) {
    connect(controller_, &SessionController::remoteFrameChanged, this, [this](const QString& id) {
      if (id == surfaceId_) {
        update();
      }
    });
  }
  update();
  emit controllerChanged();
}

void RemoteView::setSurfaceId(const QString& id) {
  if (surfaceId_ == id) {
    return;
  }
  surfaceId_ = id;
  update();
  emit surfaceIdChanged();
}

void RemoteView::paint(QPainter* p) {
  p->fillRect(boundingRect(), Qt::black);
  if (!controller_) {
    return;
  }
  const QImage img = controller_->remoteFrame(surfaceId_);
  if (img.isNull() || width() <= 0 || height() <= 0) {
    return;
  }
  const double s = std::min(width() / img.width(), height() / img.height());
  const QSizeF size(img.width() * s, img.height() * s);
  const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
  p->setRenderHint(QPainter::SmoothPixmapTransform, false);
  p->drawImage(target, img);
}

}  // namespace framebeam::ui
