#include "gameview.h"

#include <QKeyEvent>
#include <cmath>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGNode>
#include <QSGTexture>

namespace framebeam::ui {

GameView::GameView(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setFlag(ItemIsFocusScope, false);
  setActiveFocusOnTab(true);
  setAcceptedMouseButtons(Qt::LeftButton);
}

GameView::~GameView() {
  if (session_) session_->setViewSize(this, QSize());
}

QSize GameView::physicalSize(const QSizeF& logical, qreal dpr) {
  if (logical.isEmpty() || dpr <= 0) return {};
  return QSize(static_cast<int>(std::ceil(logical.width() * dpr - 1e-6)), static_cast<int>(std::ceil(logical.height() * dpr - 1e-6)));
}

void GameView::reportSize() {
  if (!session_) return;
  const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
  // Needed size = the 1x base frame times the on-screen scale (the source may be several times larger than that
  // and is downscaled to it), so split layouts, where screens are drawn individually, get what they really show.
  const emu::DisplayProfile& prof = session_->displayProfile();
  const QSize base = prof.frameSize();
  qreal scale = 0;  // logical pixels per base pixel
  if (!base.isEmpty()) {
    if (splitDrawing()) {
      for (int i = 0; i < placement_.targets.size() && scale <= 0; ++i) {
        const QRect sr = prof.screenRect(i);
        if (!placement_.targets.at(i).isEmpty() && sr.width() > 0) scale = placement_.targets.at(i).width() / sr.width();
      }
    } else if (!frameRect_.isEmpty()) {
      scale = frameRect_.width() / base.width();
    }
  }
  const QSize px = scale > 0 ? physicalSize(QSizeF(base.width() * scale, base.height() * scale), dpr) : physicalSize(size(), dpr);
  session_->setViewSize(this, px);
}

void GameView::itemChange(ItemChange change, const ItemChangeData& value) {
  QQuickItem::itemChange(change, value);
  if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) reportSize();
}

void GameView::setSession(GameSession* s) {
  if (session_ == s) {
    return;
  }
  if (session_) {
    session_->disconnect(this);
    session_->setViewSize(this, QSize());
  }
  session_ = s;
  frame_ = QImage();
  if (session_) {
    connect(session_, &GameSession::frameChanged, this, &GameView::onFrame);
    frame_ = session_->frame();
  }
  frameDirty_ = true;
  updateFrameRect();
  update();
  emit sessionChanged();
}

void GameView::setIntegerScale(bool on) {
  if (integerScale_ == on) {
    return;
  }
  integerScale_ = on;
  updateFrameRect();
  update();
  emit integerScaleChanged();
}

void GameView::setLayout(const QString& layout) {
  const QString next = isScreenLayout(layout) ? layout : QString::fromLatin1(kLayoutStacked);
  if (layout_ == next) {
    return;
  }
  layout_ = next;
  frameDirty_ = true;
  updateFrameRect();
  update();
  emit layoutChanged();
}

// "Stacked" with a vertical source is the frame as the core delivers it (one texture, the fast and unchanged path).
bool GameView::splitDrawing() const {
  if (!session_ || session_->screenCount() < 2) {
    return false;
  }
  return layout_ != QLatin1String(kLayoutStacked) || session_->displayProfile().layout != QLatin1String("vertical");
}

void GameView::onFrame() {
  if (!session_) {
    return;
  }
  const QSize before = frame_.size();
  frame_ = session_->frame();
  frameDirty_ = true;
  if (frame_.size() != before) {
    updateFrameRect();
  }
  update();
}

void GameView::updateFrameRect() {
  QSizeF fs = frame_.size();
  if (fs.isEmpty() && session_) {
    fs = session_->displayProfile().frameSize();
  }
  QRectF r;
  if (splitDrawing()) {
    placement_ = placeScreens(session_->displayProfile(), layout_, size(), integerScale_);
    r = placement_.content;
  } else {
    r = fitFrame(fs, size(), integerScale_);
    placement_ = ScreenPlacement{};
    if (session_) {
      const emu::DisplayProfile& prof = session_->displayProfile();
      for (int i = 0; i < prof.screens.size(); ++i) {
        const QRect sr = prof.screenRect(i);
        const QSize base = prof.frameSize();
        if (base.isEmpty() || r.isEmpty()) {
          placement_.targets.append(QRectF());
          continue;
        }
        const qreal sx = r.width() / base.width(), sy = r.height() / base.height();
        placement_.targets.append(QRectF(r.x() + sr.x() * sx, r.y() + sr.y() * sy, sr.width() * sx, sr.height() * sy));
      }
      placement_.content = r;
    }
  }
  if (r != frameRect_) {
    frameRect_ = r;
    emit frameRectChanged();
  }
  reportSize();
}

void GameView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  updateFrameRect();
  update();
}

QSGNode* GameView::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  if (frame_.isNull() || window() == nullptr || frameRect_.isEmpty()) {
    delete old;
    return nullptr;
  }
  if (splitDrawing()) {
    // One image node per visible screen, each with its own texture cut out of the frame (a few 100 KB per frame).
    delete old;
    auto* group = new QSGNode;
    const emu::DisplayProfile& prof = session_->displayProfile();
    for (int i = 0; i < placement_.targets.size(); ++i) {
      const QRectF target = placement_.targets.at(i);
      if (target.isEmpty()) {
        continue;
      }
      const QRectF src = screenSourceRect(prof, frame_.size(), i);
      if (src.isEmpty()) {
        continue;
      }
      auto* n = window()->createImageNode();
      n->setFiltering(QSGTexture::Nearest);
      n->setMipmapFiltering(QSGTexture::None);
      n->setOwnsTexture(true);
      n->setTexture(window()->createTextureFromImage(frame_.copy(src.toRect())));
      n->setRect(target);
      group->appendChildNode(n);
    }
    frameDirty_ = false;
    return group;
  }
  auto* node = dynamic_cast<QSGImageNode*>(old);
  if (node == nullptr && old != nullptr) {
    delete old;  // the previous tree was a split group
  }
  if (node == nullptr) {
    node = window()->createImageNode();
    node->setFiltering(QSGTexture::Nearest);
    node->setMipmapFiltering(QSGTexture::None);
    node->setOwnsTexture(true);
    frameDirty_ = true;
  }
  if (frameDirty_) {
    node->setTexture(window()->createTextureFromImage(frame_));
    frameDirty_ = false;
  }
  node->setRect(frameRect_);
  return node;
}

void GameView::keyPressEvent(QKeyEvent* e) {
  if (e->isAutoRepeat()) {
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Escape) {
    emit escapePressed();
    e->accept();
    return;
  }
  if (session_ && session_->keyEvent(e->key(), true)) {
    e->accept();
    return;
  }
  e->ignore();
}

void GameView::keyReleaseEvent(QKeyEvent* e) {
  if (e->isAutoRepeat()) {
    e->accept();
    return;
  }
  if (session_ && session_->keyEvent(e->key(), false)) {
    e->accept();
    return;
  }
  e->ignore();
}

void GameView::focusOutEvent(QFocusEvent* e) {
  QQuickItem::focusOutEvent(e);
  if (session_) {
    session_->releaseAllKeys();
  }
  touching_ = false;
}

void GameView::touch(const QPointF& pos, bool press, bool clamp) {
  if (!session_) {
    return;
  }
  const auto p = splitDrawing() ? touchToFrameFor(session_->displayProfile(), placement_, pos, clamp)
                                : touchToFrame(session_->displayProfile(), frameRect_, pos, clamp);
  if (!p) {
    return;
  }
  touching_ = press;
  session_->setPointer(*p, press);
}

void GameView::mousePressEvent(QMouseEvent* e) {
  forceActiveFocus();
  touching_ = false;
  touch(e->position(), true, false);
  e->setAccepted(true);
}

void GameView::mouseMoveEvent(QMouseEvent* e) {
  if (touching_) {
    touch(e->position(), true, true);
  }
  e->setAccepted(true);
}

void GameView::mouseReleaseEvent(QMouseEvent* e) {
  if (touching_ && session_) {
    const auto p = splitDrawing() ? touchToFrameFor(session_->displayProfile(), placement_, e->position(), true)
                                  : touchToFrame(session_->displayProfile(), frameRect_, e->position(), true);
    if (p) {
      session_->setPointer(*p, false);
    }
  }
  touching_ = false;
  e->setAccepted(true);
}

void GameView::mouseUngrabEvent() {
  if (touching_ && session_) {
    session_->setPointer(QPointF(0, 0), false);
  }
  touching_ = false;
}

}  // namespace framebeam::ui
