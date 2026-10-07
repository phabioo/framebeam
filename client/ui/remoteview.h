#pragma once
// RemoteView: draws the current frame of one watched Session (SessionController::remoteFrame(surfaceId)) scaled
// with the aspect ratio kept (nearest-neighbour).

#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

#include "sessioncontroller.h"

namespace framebeam::ui {

class RemoteView : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(framebeam::ui::SessionController* controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(QString surfaceId READ surfaceId WRITE setSurfaceId NOTIFY surfaceIdChanged)  // Session id
  // Screen layout (0.6 D12) "stacked" | "side" | "top"; applies to frames with two stacked screens, others stay as they are.
  Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY layoutChanged)
 public:
  explicit RemoteView(QQuickItem* parent = nullptr);
  SessionController* controller() const { return controller_; }
  void setController(SessionController* c);
  QString surfaceId() const { return surfaceId_; }
  void setSurfaceId(const QString& id);
  QString layout() const { return layout_; }
  void setLayout(const QString& layout);
  void paint(QPainter* painter) override;

 signals:
  void controllerChanged();
  void surfaceIdChanged();
  void layoutChanged();

 private:
  QPointer<SessionController> controller_;
  QString surfaceId_;
  QString layout_ = QStringLiteral("stacked");
};

}  // namespace framebeam::ui
