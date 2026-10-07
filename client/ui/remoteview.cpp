#include "remoteview.h"

#include <QPainter>

#include "screenlayout.h"

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

void RemoteView::setLayout(const QString& layout) {
  const QString next = isScreenLayout(layout) ? layout : QStringLiteral("stacked");
  if (next == layout_) {
    return;
  }
  layout_ = next;
  update();
  emit layoutChanged();
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
  const emu::DisplayProfile prof = profileForRemoteFrame(img.size());
  if (layout_ != QLatin1String("stacked") && prof.screens.size() == 2) {
    const ScreenPlacement pl = placeScreens(prof, layout_, size(), false);
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int i = 0; i < pl.targets.size(); ++i) {
      if (!pl.targets.at(i).isEmpty()) {
        p->drawImage(pl.targets.at(i), img, screenSourceRect(prof, img.size(), i));
      }
    }
    return;
  }
  const double s = std::min(width() / img.width(), height() / img.height());
  const QSizeF size(img.width() * s, img.height() * s);
  const QRectF target(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
  p->setRenderHint(QPainter::SmoothPixmapTransform, false);
  p->drawImage(target, img);
}

}  // namespace framebeam::ui
