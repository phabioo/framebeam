#pragma once
// GameView: Qt Quick item that draws the current frame of a GameSession scaled (nearest-neighbour,
// integer scale when possible) and forwards keyboard and mouse (touch on the lower screen).

#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include "gamesession.h"

namespace framebeam::ui {

class GameView : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(framebeam::ui::GameSession* session READ session WRITE setSession NOTIFY sessionChanged)
  Q_PROPERTY(bool integerScale READ integerScale WRITE setIntegerScale NOTIFY integerScaleChanged)
  Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY frameRectChanged)
 public:
  explicit GameView(QQuickItem* parent = nullptr);

  GameSession* session() const { return session_; }
  void setSession(GameSession* s);
  bool integerScale() const { return integerScale_; }
  void setIntegerScale(bool on);
  QRectF frameRect() const { return frameRect_; }

 signals:
  void sessionChanged();
  void integerScaleChanged();
  void frameRectChanged();
  void escapePressed();

 protected:
  QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
  void keyPressEvent(QKeyEvent* e) override;
  void keyReleaseEvent(QKeyEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseUngrabEvent() override;

 private:
  void onFrame();
  void updateFrameRect();
  void touch(const QPointF& pos, bool press, bool clamp);

  QPointer<GameSession> session_;
  QImage frame_;
  bool frameDirty_ = false;
  bool integerScale_ = true;
  bool touching_ = false;
  QRectF frameRect_;
};

}  // namespace framebeam::ui
