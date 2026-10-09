#pragma once
// GameView: Qt Quick item that draws the current frame of a GameSession scaled (nearest-neighbour,
// integer scale when possible) and forwards keyboard and mouse (touch on the lower screen).

#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

#include "gamesession.h"
#include "screenlayout.h"

namespace framebeam::ui {

class GameView : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(framebeam::ui::GameSession* session READ session WRITE setSession NOTIFY sessionChanged)
  Q_PROPERTY(bool integerScale READ integerScale WRITE setIntegerScale NOTIFY integerScaleChanged)
  Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY frameRectChanged)
  // In-game screen layout (0.6 D12): "stacked" (as delivered by the core) | "side" | "top"; applies to this game only.
  Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY layoutChanged)
 public:
  explicit GameView(QQuickItem* parent = nullptr);
  ~GameView() override;

  GameSession* session() const { return session_; }
  void setSession(GameSession* s);
  bool integerScale() const { return integerScale_; }
  void setIntegerScale(bool on);
  QRectF frameRect() const { return frameRect_; }
  QString layout() const { return layout_; }
  void setLayout(const QString& layout);
  // Where each screen is drawn right now (item coordinates); empty rects for hidden screens. Tests.
  ScreenPlacement placement() const { return placement_; }

  // Physical pixel size of a logical size at the given devicePixelRatio (rounded up).
  static QSize physicalSize(const QSizeF& logical, qreal dpr);

 signals:
  void sessionChanged();
  void integerScaleChanged();
  void frameRectChanged();
  void layoutChanged();
  void escapePressed();

 protected:
  void itemChange(ItemChange change, const ItemChangeData& value) override;
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
  void reportSize();  // physical pixels the frame needs here -> GameSession readback limit
  bool splitDrawing() const;  // screens are redrawn individually (side by side, top only, or a non-stacked source)
  void touch(const QPointF& pos, bool press, bool clamp);

  QPointer<GameSession> session_;
  QImage frame_;
  bool frameDirty_ = false;
  bool integerScale_ = true;
  bool lastDown_ = false;  // the single-node texture was last created for a downscaled draw (filtered + mipmapped)
  bool touching_ = false;
  QRectF frameRect_;
  QString layout_ = QLatin1String(kLayoutStacked);
  ScreenPlacement placement_;
};

}  // namespace framebeam::ui
