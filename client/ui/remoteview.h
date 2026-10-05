#pragma once
// RemoteView: draws the current frame of the watched Session (SessionController::remoteFrame) scaled with the
// aspect ratio kept (nearest-neighbour).

#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickPaintedItem>

#include "sessioncontroller.h"

namespace framebeam::ui {

class RemoteView : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(framebeam::ui::SessionController* controller READ controller WRITE setController NOTIFY controllerChanged)
 public:
  explicit RemoteView(QQuickItem* parent = nullptr);
  SessionController* controller() const { return controller_; }
  void setController(SessionController* c);
  void paint(QPainter* painter) override;

 signals:
  void controllerChanged();

 private:
  QPointer<SessionController> controller_;
};

}  // namespace framebeam::ui
