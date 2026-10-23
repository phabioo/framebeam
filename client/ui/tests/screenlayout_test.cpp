// Screen layouts of the game view (0.6 D12): geometry only, no Qt Quick.
#include <QtTest>

#include "inputmapping.h"
#include "screenlayout.h"
#include "system_manifest.h"

using namespace uzume;
using namespace uzume::ui;

namespace {
emu::DisplayProfile ndsProfile() {
  emu::DisplayProfile p;
  p.layout = QStringLiteral("vertical");
  p.gap = 0;
  p.screens = {{QStringLiteral("top"), 256, 192, false}, {QStringLiteral("bottom"), 256, 192, true}};
  return p;
}
emu::DisplayProfile threeDsProfile() {  // top 400x240, bottom 320x240 centred (manifests/systems/3ds.json)
  emu::DisplayProfile p;
  p.layout = QStringLiteral("vertical");
  p.screens = {{QStringLiteral("top"), 400, 240, false, QStringLiteral("center")},
               {QStringLiteral("bottom"), 320, 240, true, QStringLiteral("center")}};
  return p;
}
}  // namespace

class ScreenLayoutTest : public QObject {
  Q_OBJECT
 private slots:
  void layoutsFollowScreenCount() {
    QCOMPARE(screenLayoutsFor(2),
             (QStringList{QStringLiteral("stacked"), QStringLiteral("side"), QStringLiteral("large"), QStringLiteral("top"), QStringLiteral("bottom")}));
    QVERIFY(screenLayoutsFor(3).isEmpty());
    QVERIFY(screenLayoutsFor(1).isEmpty());
    QVERIFY(screenLayoutsFor(0).isEmpty());
    QVERIFY(isScreenLayout(QStringLiteral("side")));
    QVERIFY(!isScreenLayout(QStringLiteral("bogus")));
  }

  void everyOfferedLayoutIsKnownAndLabelled() {
    for (const QString& l : screenLayoutsFor(2)) {
      QVERIFY(isScreenLayout(l));
      QVERIFY(!screenLayoutLabel(l).isEmpty());
    }
    QCOMPARE(screenLayoutLabel(QStringLiteral("side")), QStringLiteral("Side by side"));
    QCOMPARE(screenLayoutLabel(QStringLiteral("large")), QStringLiteral("Large top / small bottom"));
    QVERIFY(layoutSupportsSwap(QStringLiteral("stacked")) && layoutSupportsSwap(QStringLiteral("side")) && layoutSupportsSwap(QStringLiteral("large")));
    QVERIFY(!layoutSupportsSwap(QStringLiteral("top")) && !layoutSupportsSwap(QStringLiteral("bottom")));
  }

  void nextLayoutCyclesAndWraps() {
    const QStringList l = screenLayoutsFor(2);
    QCOMPARE(nextScreenLayout(l, QStringLiteral("stacked")), QStringLiteral("side"));
    QCOMPARE(nextScreenLayout(l, QStringLiteral("top")), QStringLiteral("bottom"));
    QCOMPARE(nextScreenLayout(l, QStringLiteral("bottom")), QStringLiteral("stacked"));  // wraps
    QCOMPARE(nextScreenLayout(l, QStringLiteral("bogus")), QStringLiteral("stacked"));
    QVERIFY(nextScreenLayout({}, QStringLiteral("stacked")).isEmpty());
  }

  void largeLayoutGeometry() {
    // NDS: the top screen at 2x on the left (512x384), the bottom one at 1x on its right, aligned to the bottom edge.
    const auto pl = placeScreens(ndsProfile(), QStringLiteral("large"), QSizeF(768, 384), true, true);
    QCOMPARE(pl.content, QRectF(0, 0, 768, 384));
    QCOMPARE(pl.targets[0], QRectF(0, 0, 512, 384));
    QCOMPARE(pl.targets[1], QRectF(512, 192, 256, 192));
    // 3DS: 800x480 + 320x240 = 1120x480.
    const auto p3 = placeScreens(threeDsProfile(), QStringLiteral("large"), QSizeF(1120, 480), true, true);
    QCOMPARE(p3.targets[0], QRectF(0, 0, 800, 480));
    QCOMPARE(p3.targets[1], QRectF(800, 240, 320, 240));
    // Scales with the area, keeps the 2:1 relation.
    const auto big = placeScreens(ndsProfile(), QStringLiteral("large"), QSizeF(1536, 1000), false);
    QVERIFY(std::abs(big.targets[0].width() / big.targets[1].width() - 2.0) < 1e-6);
    QVERIFY(std::abs(big.targets[1].bottom() - big.targets[0].bottom()) < 1e-6);
    QVERIFY(QRectF(0, 0, 1536, 1000).contains(big.content));
  }

  void bottomOnlyHidesTop() {
    for (const emu::DisplayProfile& p : {ndsProfile(), threeDsProfile()}) {
      const auto pl = placeScreens(p, QStringLiteral("bottom"), QSizeF(800, 600), false);
      QVERIFY(pl.targets[0].isEmpty());
      QVERIFY(!pl.targets[1].isEmpty());
      QVERIFY(QRectF(0, 0, 800, 600).contains(pl.content));
      QCOMPARE(pl.content, pl.targets[1]);
      // The whole bottom screen is the touch surface: its centre is the middle of the lower half of the frame.
      const auto mid = touchToFrameFor(p, pl, pl.targets[1].center(), false);
      QVERIFY(mid.has_value());
      QVERIFY(std::abs(mid->y() - 0.75) < 1e-9);
      QVERIFY(!touchToFrameFor(p, pl, QPointF(-5, -5), false).has_value());
      QVERIFY(touchToFrameFor(p, pl, QPointF(-5, -5), true).has_value());
    }
  }

  void swapExchangesThePositions() {
    const emu::DisplayProfile p = ndsProfile();
    const auto st = placeScreens(p, QStringLiteral("stacked"), QSizeF(512, 768), true, true, AspectMode::Core, true);
    QVERIFY(st.targets[1].bottom() <= st.targets[0].top() + 0.5);  // bottom above top
    QCOMPARE(st.targets[1], QRectF(0, 0, 512, 384));
    const auto sd = placeScreens(p, QStringLiteral("side"), QSizeF(1024, 384), true, true, AspectMode::Core, true);
    QVERIFY(sd.targets[1].right() <= sd.targets[0].left() + 0.5);  // bottom on the left
    const auto lg = placeScreens(p, QStringLiteral("large"), QSizeF(768, 384), true, true, AspectMode::Core, true);
    QCOMPARE(lg.targets[1], QRectF(0, 0, 512, 384));  // the bottom screen is the large one
    QCOMPARE(lg.targets[0], QRectF(512, 192, 256, 192));
    // Single-screen layouts ignore it.
    for (const QString& l : {QStringLiteral("top"), QStringLiteral("bottom")}) {
      QCOMPARE(placeScreens(p, l, QSizeF(800, 600), false, false, AspectMode::Core, true).targets, placeScreens(p, l, QSizeF(800, 600), false).targets);
    }
    // 3DS keeps its unequal widths, centred, also swapped.
    const auto s3 = placeScreens(threeDsProfile(), QStringLiteral("stacked"), QSizeF(400, 480), false, false, AspectMode::Core, true);
    QCOMPARE(s3.targets[1], QRectF(40, 0, 320, 240));
    QCOMPARE(s3.targets[0], QRectF(0, 240, 400, 240));
  }

  void touchFollowsEveryLayoutAndSwap() {
    for (const emu::DisplayProfile& p : {ndsProfile(), threeDsProfile()}) {
      for (const QString& layout : screenLayoutsFor(2)) {
        for (const bool swap : {false, true}) {
          for (const AspectMode mode : {AspectMode::Core, AspectMode::Ratio43, AspectMode::Stretch}) {
            const auto pl = placeScreens(p, layout, QSizeF(1280, 720), false, false, mode, swap);
            if (layout == QLatin1String("top")) {  // touch screen hidden: no touch at all
              QVERIFY(!touchToFrameFor(p, pl, QPointF(640, 360), true).has_value());
              continue;
            }
            const QRectF t = pl.targets[1];
            QVERIFY2(!t.isEmpty(), qPrintable(layout));
            // Centre -> middle of the lower half; the corners of the screen -> the corners of the touch rectangle.
            const auto mid = touchToFrameFor(p, pl, t.center(), false);
            QVERIFY2(mid.has_value(), qPrintable(layout));
            QVERIFY(std::abs(mid->y() - 0.75) < 1e-9);
            QVERIFY(std::abs(mid->x() - 0.5) < 1e-9);
            const auto br = touchToFrameFor(p, pl, t.bottomRight() - QPointF(0.01, 0.01), false);
            QVERIFY(br.has_value() && br->y() > 0.99);
            // The top screen is never a touch.
            if (!pl.targets[0].isEmpty() && !pl.targets[0].intersects(t)) {
              QVERIFY(!touchToFrameFor(p, pl, pl.targets[0].center(), false).has_value());
            }
          }
        }
      }
    }
  }

  void integerAndAspectInteractWithNewLayouts() {
    const emu::DisplayProfile p = ndsProfile();
    // Strict integer: every screen of the large layout lands on whole multiples of the source size.
    for (const QSizeF area : {QSizeF(800, 500), QSizeF(1600, 900), QSizeF(2000, 1000)}) {
      const auto pl = placeScreens(p, QStringLiteral("large"), area, true, true);
      if (pl.content.isEmpty()) continue;
      const qreal f = pl.targets[1].width() / 256.0;
      QVERIFY(std::abs(f - std::round(f)) < 1e-9);
      QVERIFY(std::abs(pl.targets[0].width() - 2 * pl.targets[1].width()) < 1e-9);
    }
    // Bottom only, strict integer: 256x192 in 700x500 -> 2x.
    QCOMPARE(placeScreens(p, QStringLiteral("bottom"), QSizeF(700, 500), true, true).content.size(), QSizeF(512, 384));
    // 4:3 on the 3DS: the 400x240 top screen becomes 320x240 like the bottom one, large = 640+320 wide.
    const auto r = placeScreens(threeDsProfile(), QStringLiteral("large"), QSizeF(960, 480), false, false, AspectMode::Ratio43);
    QCOMPARE(r.targets[0].size(), QSizeF(640, 480));
    QCOMPARE(r.targets[1].size(), QSizeF(320, 240));
    // Stretch fills the whole area for the single-screen layouts too.
    QCOMPARE(placeScreens(p, QStringLiteral("bottom"), QSizeF(900, 300), false, false, AspectMode::Stretch).content, QRectF(0, 0, 900, 300));
  }

  void singleScreenSystemsIgnoreNewLayouts() {
    emu::DisplayProfile one;
    one.layout = QStringLiteral("single");
    one.screens = {{QStringLiteral("screen"), 160, 144, false}};
    for (const QString& l : {QStringLiteral("large"), QStringLiteral("bottom"), QStringLiteral("top")}) {
      const auto pl = placeScreens(one, l, QSizeF(480, 432), true, true, AspectMode::Core, true);
      QCOMPARE(pl.targets.size(), 1);
      QCOMPARE(pl.content.size(), QSizeF(480, 432));
    }
  }

  void pictureOptionsOnTwoScreens() {
    const emu::DisplayProfile p = threeDsProfile();
    const QSizeF area(1000, 800);
    // Core: the 3DS screens keep their different widths.
    auto pl = placeScreens(p, QStringLiteral("stacked"), area, false);
    QVERIFY(pl.targets[0].width() > pl.targets[1].width());
    // 4:3: every screen is 4:3, layout as before (stacked, top above bottom).
    pl = placeScreens(p, QStringLiteral("stacked"), area, false, false, AspectMode::Ratio43);
    for (const QRectF& t : pl.targets) QVERIFY(std::abs(t.width() / t.height() - 4.0 / 3.0) < 0.01);
    QVERIFY(pl.targets[0].bottom() <= pl.targets[1].top() + 0.5);
    // Stretch: the arrangement fills the whole area (non-uniform).
    pl = placeScreens(p, QStringLiteral("side"), area, true, true, AspectMode::Stretch);
    QCOMPARE(pl.content, QRectF(0, 0, 1000, 800));
    QVERIFY(pl.targets[0].left() >= -0.5);
    QVERIFY(pl.targets[1].right() <= 1000.5);
    // Strict integer: 256x384 stack in 600x800 -> 2x, centred.
    pl = placeScreens(ndsProfile(), QStringLiteral("stacked"), QSizeF(600, 800), true, true);
    QCOMPARE(pl.content.size(), QSizeF(512, 768));
    pl = placeScreens(ndsProfile(), QStringLiteral("stacked"), QSizeF(1280, 744), true, true);
    QCOMPARE(pl.content.size(), QSizeF(256, 384));
  }

  void touchUnderStretchAndFourThree() {
    const emu::DisplayProfile p = ndsProfile();
    for (const AspectMode mode : {AspectMode::Stretch, AspectMode::Ratio43}) {
      const auto pl = placeScreens(p, QStringLiteral("side"), QSizeF(1024, 600), false, false, mode);
      // Centre of the touch screen maps to the centre of the lower screen in the frame; corners map to its corners.
      const auto mid = touchToFrameFor(p, pl, pl.targets[1].center(), false);
      QVERIFY(mid.has_value());
      QVERIFY(std::abs(mid->x() - 0.5) < 0.01);
      QVERIFY(std::abs(mid->y() - 0.75) < 0.01);
      const auto tl = touchToFrameFor(p, pl, pl.targets[1].topLeft() + QPointF(0.1, 0.1), false);
      QVERIFY(tl.has_value());
      QVERIFY(tl->x() < 0.01 && std::abs(tl->y() - 0.5) < 0.01);
      QVERIFY(!touchToFrameFor(p, pl, pl.targets[0].center(), false).has_value());
    }
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

  void unequalWidthsStackCentred() {
    const auto p = threeDsProfile();
    QCOMPARE(p.frameSize(), QSize(400, 480));
    // Source rectangle follows the resolution factor (2x: 800x960, bottom at (80, 480)).
    QCOMPARE(screenSourceRect(p, QSizeF(800, 960), 1), QRectF(80, 480, 640, 480));
    // Stacked placement: the bottom screen is horizontally centred under the wider top screen.
    const auto pl = placeScreens(p, QStringLiteral("stacked"), QSizeF(400, 480), false);
    QCOMPARE(pl.targets[0], QRectF(0, 0, 400, 240));
    QCOMPARE(pl.targets[1], QRectF(40, 240, 320, 240));
  }

  void touchMapsOnlyBottomRectangleOfUnequalWidths() {
    const auto p = threeDsProfile();
    const auto pl = placeScreens(p, QStringLiteral("stacked"), QSizeF(400, 480), false);
    // Centre of the bottom screen -> (0.5, 0.75) of the 400x480 frame.
    const auto mid = touchToFrameFor(p, pl, pl.targets[1].center(), false);
    QVERIFY(mid.has_value());
    QVERIFY(qAbs(mid->x() - 0.5) < 1e-9 && qAbs(mid->y() - 0.75) < 1e-9);
    // Left of the bottom screen but inside the frame (x = 20): not a touch; with clamp it sticks to the screen edge.
    QVERIFY(!touchToFrameFor(p, pl, QPointF(20, 360), false).has_value());
    const auto edge = touchToFrameFor(p, pl, QPointF(20, 360), true);
    QVERIFY(edge.has_value());
    QVERIFY(qAbs(edge->x() - 40.0 / 400) < 1e-9);
    // Top screen: no touch.
    QVERIFY(!touchToFrameFor(p, pl, QPointF(200, 100), false).has_value());
    // Without split drawing (whole frame image): the same rectangle.
    const auto whole = touchToFrame(p, QRectF(0, 0, 400, 480), QPointF(360, 480), false);
    QVERIFY(whole.has_value());
    QVERIFY(qAbs(whole->x() - 0.9) < 1e-9 && qAbs(whole->y() - 1.0) < 1e-9);
    QVERIFY(!touchToFrame(p, QRectF(0, 0, 400, 480), QPointF(39, 360), false).has_value());
  }

  void singleScreenSystemsFromManifests() {  // gb/gbc 160x144, gba 240x160 (data-only systems)
    emu::ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    for (const auto& [id, size] : {std::pair{"gb", QSize(160, 144)}, std::pair{"gbc", QSize(160, 144)}, std::pair{"gba", QSize(240, 160)}}) {
      const emu::SystemManifest* m = reg.find(QString::fromLatin1(id));
      QVERIFY(m);
      QCOMPARE(m->display.screens.size(), 1);
      QCOMPARE(m->display.frameSize(), size);
      QCOMPARE(m->display.touchScreenIndex(), -1);
      QVERIFY(screenLayoutsFor(m->display.screens.size()).isEmpty());  // no layout switch
      for (const QString& layout : {QStringLiteral("stacked"), QStringLiteral("side"), QStringLiteral("large"), QStringLiteral("top"), QStringLiteral("bottom")}) {
        const auto pl = placeScreens(m->display, layout, QSizeF(800, 600), true);
        QCOMPARE(pl.targets.size(), 1);
        QVERIFY(QRectF(0, 0, 800, 600).contains(pl.content));
        QVERIFY(qAbs(pl.content.width() / pl.content.height() - double(size.width()) / size.height()) < 0.02);
      }
      const auto pl = placeScreens(m->display, QStringLiteral("stacked"), QSizeF(800, 600), false);
      QVERIFY(!touchToFrameFor(m->display, pl, pl.content.center(), false).has_value());  // no touch screen
      QCOMPARE(screenSourceRect(m->display, QSizeF(size), 0), QRectF(QPointF(0, 0), QSizeF(size)));
    }
  }

  void remoteFramesGetTheSameLayouts() {
    const emu::DisplayProfile p = profileForRemoteFrame(QSizeF(256, 384));
    for (const QString& l : screenLayoutsFor(2)) {
      const auto pl = placeScreens(p, l, QSizeF(900, 500), false, false, AspectMode::Core, true);
      QCOMPARE(pl.targets.size(), 2);
      QVERIFY(QRectF(0, 0, 900, 500).contains(pl.content));
    }
    QCOMPARE(placeScreens(p, QStringLiteral("bottom"), QSizeF(900, 500), false).targets[0], QRectF());
  }

  void remoteFrameProfile() {
    QCOMPARE(profileForRemoteFrame(QSizeF(256, 384)).screens.size(), 2);
    QCOMPARE(profileForRemoteFrame(QSizeF(640, 480)).screens.size(), 1);
    QCOMPARE(profileForRemoteFrame(QSizeF(256, 385)).screens.size(), 1);
  }
};

QTEST_GUILESS_MAIN(ScreenLayoutTest)
#include "screenlayout_test.moc"
