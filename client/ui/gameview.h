#pragma once
// GameView: Qt Quick item that draws the current frame of a GameSession scaled (nearest-neighbour,
// integer scale when possible) and forwards keyboard and mouse (touch on the lower screen).

#include <QImage>
#include <QPointer>
#include <QRectF>
#include <optional>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickItem>

class QSGTexture;

#include "gamesession.h"
#include "screenlayout.h"

namespace uzume::ui {

class GameView : public QQuickItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(uzume::ui::GameSession* session READ session WRITE setSession NOTIFY sessionChanged)
  Q_PROPERTY(bool integerScale READ integerScale WRITE setIntegerScale NOTIFY integerScaleChanged)
  // Picture options (0.19): strict whole-number scaling and "core" | "4:3" | "stretch".
  Q_PROPERTY(bool strictInteger READ strictInteger WRITE setStrictInteger NOTIFY pictureOptionsChanged)
  Q_PROPERTY(QString aspectRatio READ aspectRatio WRITE setAspectRatio NOTIFY pictureOptionsChanged)
  Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY frameRectChanged)
  // In-game screen layout (0.6 D12, 0.19): "stacked" (as delivered by the core) | "side" | "large" | "top" | "bottom";
  // applies to this game only. swap draws the second screen where the first would be (layoutSupportsSwap).
  Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY layoutChanged)
  Q_PROPERTY(bool swap READ swap WRITE setSwap NOTIFY layoutChanged)
  // Always draws the CPU frame (never the GPU texture of the game): the second window of the bottom screen, because the
  // zero-copy texture has a single consumer window. Makes the session keep the readback running while this view lives.
  Q_PROPERTY(bool cpuOnly READ cpuOnly WRITE setCpuOnly NOTIFY cpuOnlyChanged)
 public:
  explicit GameView(QQuickItem* parent = nullptr);
  ~GameView() override;

  GameSession* session() const { return session_; }
  void setSession(GameSession* s);
  bool integerScale() const { return integerScale_; }
  void setIntegerScale(bool on);
  bool strictInteger() const { return strictInteger_; }
  void setStrictInteger(bool on);
  QString aspectRatio() const { return aspectRatio_; }
  void setAspectRatio(const QString& value);
  QRectF frameRect() const { return frameRect_; }
  QString layout() const { return layout_; }
  void setLayout(const QString& layout);
  bool swap() const { return swap_; }
  void setSwap(bool on);
  bool cpuOnly() const { return cpuOnly_; }
  void setCpuOnly(bool on);
  // Where each screen is drawn right now (item coordinates); empty rects for hidden screens. Tests.
  ScreenPlacement placement() const { return placement_; }

  // Item position -> normalized position in the whole frame for the touch screen under the current layout (nullopt: not on it,
  // or the touch screen is hidden). What a mouse press or touch sends to the core.
  std::optional<QPointF> touchTarget(const QPointF& itemPos, bool clamp) const;

  // Physical pixel size of a logical size at the given devicePixelRatio (rounded up).
  static QSize physicalSize(const QSizeF& logical, qreal dpr);

 signals:
  void sessionChanged();
  void integerScaleChanged();
  void pictureOptionsChanged();
  void frameRectChanged();
  void layoutChanged();
  void cpuOnlyChanged();
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
  // Render thread: the node tree drawing `tex` (zero-copy display, the image of size `content` in its top-left corner).
  QSGNode* paintGpu(QSGNode* old, QSGTexture* tex, const QSize& content, qreal dpr);
  void touch(const QPointF& pos, bool press, bool clamp);

  QPointer<GameSession> session_;
  QImage frame_;
  QSize lastFrameSize_;  // size of the last non-null frame (the frame size while the session has dropped its image)
  bool frameDirty_ = false;
  bool integerScale_ = true;
  bool strictInteger_ = false;
  QString aspectRatio_ = QStringLiteral("core");
  AspectMode aspect_ = AspectMode::Core;
  bool lastGpu_ = false;   // the node tree was last built from the GPU texture (render thread)
  bool lastDown_ = false;  // the single-node texture was last created for a downscaled draw (filtered + mipmapped)
  bool touching_ = false;
  QRectF frameRect_;
  QString layout_ = QLatin1String(kLayoutStacked);
  bool swap_ = false;
  bool cpuOnly_ = false;
  ScreenPlacement placement_;
};

}  // namespace uzume::ui
