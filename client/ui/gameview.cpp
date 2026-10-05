#include "gameview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>

namespace framebeam::ui {

GameView::GameView(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setFlag(ItemIsFocusScope, false);
  setActiveFocusOnTab(true);
  setAcceptedMouseButtons(Qt::LeftButton);
}

void GameView::setSession(GameSession* s) {
  if (session_ == s) {
    return;
  }
  if (session_) {
    session_->disconnect(this);
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
  const QRectF r = fitFrame(fs, size(), integerScale_);
  if (r != frameRect_) {
    frameRect_ = r;
    emit frameRectChanged();
  }
}

void GameView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  updateFrameRect();
  update();
}

QSGNode* GameView::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  auto* node = static_cast<QSGImageNode*>(old);
  if (frame_.isNull() || window() == nullptr || frameRect_.isEmpty()) {
    delete node;
    return nullptr;
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
  const auto p = touchToFrame(session_->displayProfile(), frameRect_, pos, clamp);
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
    const auto p = touchToFrame(session_->displayProfile(), frameRect_, e->position(), true);
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
