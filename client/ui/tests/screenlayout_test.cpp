// Screen layouts of the game view (0.6 D12): geometry only, no Qt Quick.
#include <QtTest>

#include "screenlayout.h"

using namespace framebeam;
using namespace framebeam::ui;

namespace {
emu::DisplayProfile ndsProfile() {
  emu::DisplayProfile p;
  p.layout = QStringLiteral("vertical");
  p.gap = 0;
  p.screens = {{QStringLiteral("top"), 256, 192, false}, {QStringLiteral("bottom"), 256, 192, true}};
  return p;
}
}  // namespace

class ScreenLayoutTest : public QObject {
  Q_OBJECT
 private slots:
  void layoutsFollowScreenCount() {
    QCOMPARE(screenLayoutsFor(2).size(), 3);
    QVERIFY(screenLayoutsFor(1).isEmpty());
    QVERIFY(screenLayoutsFor(0).isEmpty());
    QVERIFY(isScreenLayout(QStringLiteral("side")));
    QVERIFY(!isScreenLayout(QStringLiteral("bogus")));
  }

  void stackedKeepsOrder() {
    const auto pl = placeScreens(ndsProfile(), QStringLiteral("stacked"), QSizeF(512, 768), true);
    QCOMPARE(pl.targets.size(), 2);
    QVERIFY(pl.targets[0].bottom() <= pl.targets[1].top() + 0.5);
    QCOMPARE(pl.targets[0].width(), pl.targets[1].width());
  }

  void sideBySideIsWide() {
    const auto pl = placeScreens(ndsProfile(), QStringLiteral("side"), QSizeF(1024, 600), false);
    QCOMPARE(pl.targets.size(), 2);
    QVERIFY(pl.targets[1].left() >= pl.targets[0].right() - 0.5);
    QVERIFY(pl.content.width() > pl.content.height());
    QVERIFY(QRectF(0, 0, 1024, 600).contains(pl.content));
  }

  void topOnlyHidesBottom() {
    const auto pl = placeScreens(ndsProfile(), QStringLiteral("top"), QSizeF(800, 600), false);
    QVERIFY(!pl.targets[0].isEmpty());
    QVERIFY(pl.targets[1].isEmpty());
  }

  void sourceRectScalesWithFrame() {
    const auto p = ndsProfile();
    QCOMPARE(screenSourceRect(p, QSizeF(256, 384), 1), QRectF(0, 192, 256, 192));
    QCOMPARE(screenSourceRect(p, QSizeF(512, 768), 1), QRectF(0, 384, 512, 384));
  }

  void touchMapsThroughPlacement() {
    const auto p = ndsProfile();
    const auto pl = placeScreens(p, QStringLiteral("side"), QSizeF(1024, 600), false);
    const auto hit = touchToFrameFor(p, pl, pl.targets[1].center(), false);
    QVERIFY(hit.has_value());
    QVERIFY(qAbs(hit->x() - 0.5) < 0.01);
    QVERIFY(qAbs(hit->y() - 0.75) < 0.01);  // centre of the bottom half of the stacked frame
    const QPointF outside = pl.targets[0].center();
    QVERIFY(!touchToFrameFor(p, pl, outside, false).has_value());
    QVERIFY(touchToFrameFor(p, pl, outside, true).has_value());
    const auto top = placeScreens(p, QStringLiteral("top"), QSizeF(800, 600), false);
    QVERIFY(!touchToFrameFor(p, top, QPointF(400, 300), true).has_value());
  }

  void remoteFrameProfile() {
    QCOMPARE(profileForRemoteFrame(QSizeF(256, 384)).screens.size(), 2);
    QCOMPARE(profileForRemoteFrame(QSizeF(640, 480)).screens.size(), 1);
    QCOMPARE(profileForRemoteFrame(QSizeF(256, 385)).screens.size(), 1);
  }
};

QTEST_GUILESS_MAIN(ScreenLayoutTest)
#include "screenlayout_test.moc"
