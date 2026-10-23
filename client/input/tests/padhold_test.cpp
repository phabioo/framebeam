// Hold-Select hotkey sheet: pure hold detection (PadHoldDetector). No SDL, no timers, injected clock.
#include <QtTest>

#include "padhold.h"

using namespace uzume::input;

namespace {
const QString kBack = QStringLiteral("pad:back");
const QString kRb = QStringLiteral("pad:rightshoulder");
const QString kA = QStringLiteral("pad:a");
QSet<QString> held(std::initializer_list<QString> l) { return QSet<QString>(l.begin(), l.end()); }
}  // namespace

class PadHoldTest : public QObject {
  Q_OBJECT
 private slots:
  void showsAfterTheHoldTimeAndHidesOnRelease() {
    PadHoldDetector d;
    d.setModifier(kBack);
    QVERIFY(!d.update({}, 0));
    QVERIFY(!d.update(held({kBack}), 100));
    QCOMPARE(d.msUntilShow(100), 400);
    QCOMPARE(d.msUntilShow(300), 200);
    QVERIFY(!d.update(held({kBack}), 499));
    QVERIFY(d.update(held({kBack}), 500));
    QCOMPARE(d.msUntilShow(500), -1);
    QVERIFY(d.update(held({kBack}), 900));
    QVERIFY(!d.update({}, 950));
    QVERIFY(!d.visible());
  }
  void anotherButtonBeforeTheHoldTimeNeverShowsTheSheet() {
    PadHoldDetector d;
    d.setModifier(kBack);
    d.update(held({kBack}), 0);
    d.update(held({kBack, kRb}), 100);  // a combo fires
    QCOMPARE(d.msUntilShow(100), -1);
    QVERIFY(!d.update(held({kBack}), 900));  // RB released, Select still held: stays hidden
    QVERIFY(!d.update(held({kBack}), 2000));
    d.update({}, 2100);
    d.update(held({kBack}), 2200);  // a new hold starts clean
    QVERIFY(d.update(held({kBack}), 2600));
  }
  void anotherButtonWhileShownHidesIt() {
    PadHoldDetector d;
    d.setModifier(kBack);
    d.update(held({kBack}), 0);
    QVERIFY(d.update(held({kBack}), 400));
    QVERIFY(!d.update(held({kBack, kA}), 450));
    QVERIFY(!d.update(held({kBack}), 500));  // not shown again until Select is released
    QVERIFY(!d.update(held({kBack}), 5000));
  }
  void aButtonHeldBeforeTheModifierBlocks() {
    PadHoldDetector d;
    d.setModifier(kBack);
    d.update(held({kA}), 0);
    d.update(held({kA, kBack}), 50);
    QVERIFY(!d.update(held({kBack}), 1000));  // A released, but the hold began with A down
    QVERIFY(!d.update(held({kBack}), 2000));
  }
  void modifierChangeAndEmptyModifier() {
    PadHoldDetector d;
    QVERIFY(!d.update(held({kBack}), 0));  // no modifier configured
    QVERIFY(!d.update(held({kBack}), 5000));
    d.setModifier(kBack);
    d.update(held({kBack}), 6000);
    QVERIFY(d.update(held({kBack}), 6400));
    d.setModifier(QStringLiteral("pad:leftstick"));  // rebinding resets
    QVERIFY(!d.visible());
    d.setHoldMs(100);
    d.update(held({QStringLiteral("pad:leftstick")}), 7000);
    QVERIFY(d.update(held({QStringLiteral("pad:leftstick")}), 7100));
  }
};

QTEST_APPLESS_MAIN(PadHoldTest)
#include "padhold_test.moc"
