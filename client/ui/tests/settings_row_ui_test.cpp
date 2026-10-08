// SettingsRow in isolation (player.md "SettingsRow"): control column alignment, the changed-dot slot, description clamp with
// More / Less and the option list, segment -> select fallback, disabled reason, narrow layout. Offscreen, no hub.
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>

#include "testsupport.h"

namespace {
const char* kPageQml = R"(
import QtQuick
import QtQuick.Layouts
import FrameBeam.Player

Item {
    id: page
    width: 676
    height: 1600
    ColumnLayout {
        id: col
        width: page.width
        spacing: 0
        SettingsRow { objectName: "rToggle"; Layout.fillWidth: true; Layout.minimumWidth: 0; idKey: "t"; controlName: "cT"; toggleName: "tT"
                      label: "Threaded Software Renderer"; values: [{value: "disabled", label: "disabled"}, {value: "enabled", label: "enabled"}]; current: "enabled" }
        SettingsRow { objectName: "rSeg"; Layout.fillWidth: true; Layout.minimumWidth: 0; idKey: "s"; controlName: "cS"; changed: true
                      label: "Interpolation"; values: [{value: "none", label: "None"}, {value: "linear", label: "Linear"}, {value: "cosine", label: "Cosine"}, {value: "cubic", label: "Cubic"}]; current: "linear" }
        SettingsRow { objectName: "rSel"; Layout.fillWidth: true; Layout.minimumWidth: 0; idKey: "x"; controlName: "cX"
                      label: "Microphone Input Mode"; meta: "Channel: beta"; values: [{value: "a", label: "Silence"}, {value: "b", label: "Microphone"}, {value: "c", label: "White noise"}, {value: "d", label: "Host microphone input"}]; current: "b" }
        SettingsRow { objectName: "rBtn"; Layout.fillWidth: true; Layout.minimumWidth: 0; resetMode: "none"; ctrl: "buttons"
                      label: "Log file"; description: "/home/someone/.local/share/framebeam/logs/player.log"
                      buttons: [{name: "b1", text: "Copy path"}, {name: "b2", text: "Open folder"}] }
        SettingsRow { objectName: "rVal"; Layout.fillWidth: true; Layout.minimumWidth: 0; resetMode: "none"; ctrl: "value"; controlName: "cV"
                      label: "Version"; valueText: "0.7.12"; description: "Windows x86-64 · Protocol v1" }
        SettingsRow { objectName: "rList"; Layout.fillWidth: true; Layout.minimumWidth: 0; idKey: "l"; controlName: "cL"
                      label: "Microphone Input Mode"
                      description: "Select the microphone input.\n- Silence: No input.\n- Microphone: Use the host microphone.\n- White noise: Random noise."
                      values: [{value: "a", label: "Silence"}, {value: "b", label: "Microphone"}, {value: "c", label: "White noise"}]; current: "a" }
        SettingsRow { objectName: "rLong"; Layout.fillWidth: true; Layout.minimumWidth: 0; idKey: "g"; controlName: "cG"
                      label: "Render Mode"; badge: "applies on next start"
                      description: "Software is fastest and most accurate. OpenGL (Classic) can scale up 3D graphics but is slower on weak hardware. OpenGL (Compute) needs a recent GPU and is the most demanding mode of the three, but it produces the best looking image."
                      values: [{value: "a", label: "Software"}, {value: "b", label: "OpenGL"}]; current: "a" }
        SettingsRow { objectName: "rDis"; Layout.fillWidth: true; Layout.minimumWidth: 0; idKey: "d"; controlName: "cD"; toggleName: "tD"
                      label: "Threaded Software Renderer"; disabledReason: "Software renderer only. Render Mode is OpenGL (Compute)."
                      values: [{value: "disabled", label: "disabled"}, {value: "enabled", label: "enabled"}]; current: "enabled" }
    }
}
)";
}  // namespace

class SettingsRowTest : public QObject {
  Q_OBJECT

  QQmlEngine* engine_ = nullptr;
  QQuickWindow* win_ = nullptr;
  QQuickItem* page_ = nullptr;

  QQuickItem* row(const char* name) const { return page_->findChild<QQuickItem*>(QString::fromLatin1(name)); }
  static QQuickItem* itemProp(QQuickItem* r, const char* prop) { return r->property(prop).value<QQuickItem*>(); }
  static QRectF scene(QQuickItem* it) { return it->mapRectToScene(QRectF(0, 0, it->width(), it->height())); }
  QQuickItem* control(const char* rowName) const { return itemProp(row(rowName), "controlColumn"); }
  // Right edge of the visible control (a toggle or button row is narrower than its column and sits at the right).
  QQuickItem* visibleControl(const char* rowName) const {
    QQuickItem* loader = itemProp(row(rowName), "controlLoader");
    return loader != nullptr ? loader->property("item").value<QQuickItem*>() : nullptr;
  }
  void setPageWidth(int w) {
    page_->setWidth(w);
    QQuickTest::qWaitForPolish(win_);
    QTest::qWait(30);
    QQuickTest::qWaitForPolish(win_);
  }
  void clickItem(QQuickItem* it) {
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    QTest::mouseClick(win_, Qt::LeftButton, Qt::NoModifier, p.toPoint());
    QTest::qWait(30);
    QQuickTest::qWaitForPolish(win_);
  }

 private slots:
  void initTestCase() {
    uitest::installWarningCounter();
    uitest::warningCount() = 0;
    engine_ = new QQmlEngine(this);
    engine_->addImportPath(QStringLiteral("qrc:/qt/qml"));
    win_ = new QQuickWindow;
    win_->resize(1000, 1700);
    QQmlComponent c(engine_);
    c.setData(kPageQml, QUrl(QStringLiteral("qrc:/test/page.qml")));
    QObject* o = c.create();
    QVERIFY2(o != nullptr, qPrintable(c.errorString()));
    page_ = qobject_cast<QQuickItem*>(o);
    QVERIFY(page_ != nullptr);
    page_->setParentItem(win_->contentItem());
    win_->show();
    QVERIFY(QTest::qWaitForWindowExposed(win_));
    QQuickTest::qWaitForPolish(win_);
  }
  void cleanupTestCase() {
    delete win_;
    QCOMPARE(uitest::warningCount().load(), 0);
  }

  // Grid 14 | flex | 280 | 72: every control type ends on the same x; select and segment are 280 x 32 on the same left edge.
  void controlsEndOnTheSameX() {
    for (const int w : {676, 836}) {
      setPageWidth(w);
      const qreal expectedRight = scene(page_).right() - 72;
      for (const char* r : {"rToggle", "rSeg", "rSel", "rBtn", "rVal", "rList", "rLong"}) {
        QQuickItem* col = control(r);
        QVERIFY2(col != nullptr, r);
        QCOMPARE(scene(col).right(), expectedRight);
        QCOMPARE(col->width(), 280.0);
        QQuickItem* vis = visibleControl(r);
        QVERIFY2(vis != nullptr, r);
        QVERIFY2(qAbs(scene(vis).right() - expectedRight) < 0.6, r);  // right-aligned in its column
      }
      QCOMPARE(scene(visibleControl("rSeg")).left(), scene(visibleControl("rSel")).left());
      QCOMPARE(visibleControl("rSeg")->width(), 280.0);
      QCOMPARE(visibleControl("rSeg")->height(), 32.0);
      QCOMPARE(visibleControl("rSel")->width(), 280.0);
      QCOMPARE(visibleControl("rSel")->height(), 32.0);
      QQuickItem* tg = page_->findChild<QQuickItem*>(QStringLiteral("tT"));
      QVERIFY(tg != nullptr);
      QCOMPARE(tg->width(), 40.0);
      QCOMPARE(tg->height(), 22.0);
      // One center line: the control column is 32 high and starts where the label line starts.
      QCOMPARE(control("rSeg")->height(), 32.0);
      QCOMPARE(scene(control("rSeg")).top(), scene(itemProp(row("rSeg"), "labelItem")).top() - 6);
    }
  }

  // The changed dot has its own 14 px slot: the label x is the same with and without the dot; Reset / Default fill the 72 column.
  void changedDotNeverShiftsTheLabel() {
    setPageWidth(676);
    QQuickItem* seg = row("rSeg");
    QQuickItem* toggle = row("rToggle");
    QCOMPARE(scene(itemProp(seg, "labelItem")).left(), scene(itemProp(toggle, "labelItem")).left());
    QCOMPARE(scene(itemProp(seg, "labelItem")).left() - scene(seg).left(), 14.0);
    QVERIFY(itemProp(seg, "dotItem")->isVisible());
    QVERIFY(!itemProp(toggle, "dotItem")->isVisible());
    QQuickItem* resetCol = itemProp(seg, "resetColumn");
    QCOMPARE(resetCol->width(), 72.0);
    QCOMPARE(scene(resetCol).right(), scene(seg).right());
    QVERIFY(page_->findChild<QQuickItem*>(QStringLiteral("optionReset_s"))->isVisible());
    QVERIFY(!page_->findChild<QQuickItem*>(QStringLiteral("optionOrigin_s"))->isVisible());
    QVERIFY(page_->findChild<QQuickItem*>(QStringLiteral("optionOrigin_t"))->isVisible());
    // resetMode "none" keeps the column empty but present.
    QVERIFY(!itemProp(row("rBtn"), "resetColumn")->isVisible());
  }

  // Description: 2 lines, then "More"; with an "Option: explanation" list it says "More · n options" and expands to two columns.
  void descriptionMoreLess() {
    setPageWidth(676);
    QQuickItem* longRow = row("rLong");
    QQuickItem* intro = itemProp(longRow, "descriptionItem");
    QVERIFY(intro->height() <= 36.5);  // 2 lines x 18
    QQuickItem* more = page_->findChild<QQuickItem*>(QStringLiteral("descMore_g"));
    QVERIFY(more != nullptr && more->isVisible());
    QCOMPARE(longRow->property("moreLabel").toString(), QStringLiteral("More"));
    const qreal collapsedHeight = longRow->height();
    clickItem(more);
    QVERIFY(longRow->property("expanded").toBool());
    QCOMPARE(longRow->property("moreLabel").toString(), QStringLiteral("Less"));
    QVERIFY(intro->height() > 36.5);
    QVERIFY(longRow->height() > collapsedHeight);
    QVERIFY(!page_->findChild<QQuickItem*>(QStringLiteral("optionList_g"))->isVisible());  // no list in this text
    clickItem(more);
    QVERIFY(!longRow->property("expanded").toBool());
    QCOMPARE(longRow->height(), collapsedHeight);

    QQuickItem* listRow = row("rList");
    QCOMPARE(listRow->property("moreLabel").toString(), QStringLiteral("More · 3 options"));
    QQuickItem* box = page_->findChild<QQuickItem*>(QStringLiteral("optionList_l"));
    QVERIFY(box != nullptr && !box->isVisible());
    QCOMPARE(itemProp(listRow, "descriptionItem")->property("text").toString(), QStringLiteral("Select the microphone input."));
    QQuickItem* moreList = page_->findChild<QQuickItem*>(QStringLiteral("descMore_l"));
    QVERIFY(moreList->isVisible());
    clickItem(moreList);
    QVERIFY(box->isVisible());
    // Two columns: all keys on one x, all explanations on another x to the right of it.
    QList<QQuickItem*> cells;
    for (QQuickItem* c : box->childItems()) {
      for (QQuickItem* cc : c->childItems()) {
        if (cc->property("text").isValid()) cells.append(cc);
      }
    }
    QCOMPARE(cells.size(), 6);
    QCOMPARE(cells[0]->property("text").toString(), QStringLiteral("Silence"));
    QCOMPARE(cells[1]->property("text").toString(), QStringLiteral("No input."));
    QCOMPARE(scene(cells[0]).left(), scene(cells[2]).left());
    QCOMPARE(scene(cells[0]).left(), scene(cells[4]).left());
    QCOMPARE(scene(cells[1]).left(), scene(cells[3]).left());
    QVERIFY(scene(cells[1]).left() > scene(cells[0]).right());
    QVERIFY(scene(box).right() <= scene(listRow).right() + 0.5);
    QCOMPARE(listRow->property("moreLabel").toString(), QStringLiteral("Less"));
    clickItem(moreList);
    QVERIFY(!box->isVisible());
  }

  // Segment only for at most 4 options whose labels fit their cell; otherwise a select. Rule per option at render time.
  void segmentFallsBackToSelect() {
    setPageWidth(676);
    QCOMPARE(row("rSeg")->property("kind").toString(), QStringLiteral("segment"));  // 4 short labels
    QCOMPARE(row("rSel")->property("kind").toString(), QStringLiteral("select"));   // 4 labels, "Host microphone input" does not fit 58 px
    QCOMPARE(row("rList")->property("kind").toString(), QStringLiteral("segment")); // 3 labels fit 81 px
    QCOMPARE(row("rLong")->property("kind").toString(), QStringLiteral("segment"));
    QCOMPARE(row("rToggle")->property("kind").toString(), QStringLiteral("toggle"));
    QCOMPARE(row("rBtn")->property("kind").toString(), QStringLiteral("buttons"));
    QCOMPARE(row("rVal")->property("kind").toString(), QStringLiteral("value"));
    // Same values, five options: never a segment.
    QQuickItem* seg = row("rSeg");
    seg->setProperty("values", QVariantList{QVariantMap{{"value", "1"}, {"label", "One"}}, QVariantMap{{"value", "2"}, {"label", "Two"}},
                                            QVariantMap{{"value", "3"}, {"label", "Three"}}, QVariantMap{{"value", "4"}, {"label", "Four"}},
                                            QVariantMap{{"value", "5"}, {"label", "Five"}}});
    QQuickTest::qWaitForPolish(win_);
    QCOMPARE(seg->property("kind").toString(), QStringLiteral("select"));
    QVERIFY(page_->findChild<QQuickItem*>(QStringLiteral("cS"))->metaObject()->indexOfSignal("picked(QString)") >= 0);
    QCOMPARE(scene(visibleControl("rSeg")).left(), scene(visibleControl("rSel")).left());
    QCOMPARE(visibleControl("rSeg")->height(), 32.0);
  }

  // Disabled rows show the reason under the description and dim the control to 40 % without taking input.
  void disabledReason() {
    setPageWidth(676);
    QQuickItem* dis = row("rDis");
    QQuickItem* reason = page_->findChild<QQuickItem*>(QStringLiteral("disabledReason_d"));
    QVERIFY(reason != nullptr && reason->isVisible());
    QCOMPARE(reason->property("text").toString(), QStringLiteral("⊘ Software renderer only. Render Mode is OpenGL (Compute)."));
    QQuickItem* loader = itemProp(dis, "controlLoader");
    QVERIFY(!loader->isEnabled());
    QCOMPARE(loader->opacity(), 0.4);
    QVERIFY(!page_->findChild<QQuickItem*>(QStringLiteral("disabledReason_t"))->isVisible());
    QVERIFY(itemProp(row("rToggle"), "controlLoader")->isEnabled());
    QCOMPARE(itemProp(row("rToggle"), "controlLoader")->opacity(), 1.0);
    QVERIFY(scene(reason).right() <= scene(dis).right());
  }

  // Meta and badge get their natural width on a wide row (never clipped to "Channel: ..."); the label keeps the rest.
  void metaAndBadgeKeepTheirNaturalWidth() {
    setPageWidth(676);
    int checked = 0;
    QList<QQuickItem*> stack{page_};
    while (!stack.isEmpty()) {
      QQuickItem* it = stack.takeLast();
      stack.append(it->childItems());
      const QString t = it->property("text").toString();
      if (!it->isVisible() || (t != QStringLiteral("Channel: beta") && t != QStringLiteral("applies on next start"))) continue;
      QQuickItem* box = it->parentItem();
      QVERIFY2(box->width() + 0.5 >= it->implicitWidth(), qPrintable(t));
      QVERIFY2(it->width() + 0.5 >= it->implicitWidth(), qPrintable(t));
      ++checked;
    }
    QCOMPARE(checked, 2);
  }

  // Narrow page column (960 px window): the control moves under the text; nothing leaves the column.
  void narrowRowsStayInsideTheColumn() {
    for (const int w : {356, 280}) {
      setPageWidth(w);
      const QRectF colRect = scene(page_);
      for (const char* r : {"rToggle", "rSeg", "rSel", "rBtn", "rVal", "rList", "rLong", "rDis"}) {
        QQuickItem* rr = row(r);
        QVERIFY2(rr != nullptr, r);
        for (QQuickItem* it : {rr, itemProp(rr, "controlColumn"), itemProp(rr, "resetColumn"), itemProp(rr, "textColumn"),
                               itemProp(rr, "labelItem"), visibleControl(r)}) {
          QVERIFY2(it != nullptr, r);
          const QRectF rect = scene(it);
          QVERIFY2(rect.left() >= colRect.left() - 0.5 && rect.right() <= colRect.right() + 0.5,
                   qPrintable(QStringLiteral("%1 at %2: %3..%4 outside %5..%6").arg(QString::fromLatin1(r)).arg(w).arg(rect.left()).arg(rect.right()).arg(colRect.left()).arg(colRect.right())));
        }
        // The control sits below the text, not beside it.
        QVERIFY2(scene(itemProp(rr, "controlColumn")).top() >= scene(itemProp(rr, "textColumn")).bottom(), r);
      }
    }
  }
};

UITEST_MAIN(SettingsRowTest)
#include "settings_row_ui_test.moc"
