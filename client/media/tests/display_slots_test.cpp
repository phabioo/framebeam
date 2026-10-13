// Zero-copy display ring (ADR 0027, design D4): the pure slot state machine and the grow-only size rule. No GPU, no GL,
// no COM; runs on every platform.
#include <QtTest>

#include "displayslots.h"
#include "processguard.h"

using framebeam::DisplaySlots;
using State = framebeam::DisplaySlots::State;

class DisplaySlotsTest : public QObject {
  Q_OBJECT
 private slots:
  void startsFree() {
    DisplaySlots s;
    for (int i = 0; i < DisplaySlots::kCount; ++i) {
      QCOMPARE(s.state(i), State::Free);
    }
    QCOMPARE(s.newestReady().slot, -1);
    QVERIFY(s.readyOlderThan(100).empty());
    QCOMPARE(s.skipped(), quint64(0));
  }

  void writeThenPublish() {
    DisplaySlots s;
    const int a = s.beginWrite();
    QCOMPARE(a, 0);
    QCOMPARE(s.state(a), State::Writing);
    QCOMPARE(s.newestReady().slot, -1);  // not visible while writing
    const quint64 seq = s.endWrite(a, 160, 144);
    QCOMPARE(seq, quint64(1));
    QCOMPARE(s.state(a), State::Ready);
    const DisplaySlots::Frame f = s.newestReady();
    QCOMPARE(f.slot, a);
    QCOMPARE(f.width, 160);
    QCOMPARE(f.height, 144);
    QCOMPARE(f.sequence, seq);
  }

  void abortFreesTheSlotWithoutPublishing() {
    DisplaySlots s;
    const int a = s.beginWrite();
    QVERIFY(s.abortWrite(a));
    QCOMPARE(s.state(a), State::Free);
    QCOMPARE(s.newestReady().slot, -1);
    QCOMPARE(s.beginWrite(), a);  // reused
  }

  void skipsWhenNoSlotIsFree() {
    DisplaySlots s;
    const int a = s.beginWrite();
    const int b = s.beginWrite();
    const int c = s.beginWrite();
    QVERIFY(a >= 0 && b >= 0 && c >= 0 && a != b && b != c && a != c);
    QCOMPARE(s.beginWrite(), -1);
    QCOMPARE(s.beginWrite(), -1);
    QCOMPARE(s.skipped(), quint64(2));
    // Ready and Shown slots count as used too.
    s.endWrite(a, 8, 8);
    s.endWrite(b, 8, 8);
    s.endWrite(c, 8, 8);
    QCOMPARE(s.beginWrite(), -1);
    QVERIFY(s.takeShown(s.newestReady().slot));
    QCOMPARE(s.beginWrite(), -1);
    QCOMPARE(s.skipped(), quint64(4));
  }

  void newestWinsAndOlderOnesAreRecycled() {
    DisplaySlots s;
    const int a = s.beginWrite();
    const int b = s.beginWrite();
    const quint64 sa = s.endWrite(a, 256, 192);
    const quint64 sb = s.endWrite(b, 256, 160);
    QVERIFY(sb > sa);
    const DisplaySlots::Frame f = s.newestReady();
    QCOMPARE(f.slot, b);
    QCOMPARE(f.height, 160);
    const std::vector<int> older = s.readyOlderThan(f.sequence);
    QCOMPARE(older.size(), size_t(1));
    QCOMPARE(older[0], a);
    QVERIFY(s.recycle(a));
    QCOMPARE(s.state(a), State::Free);
    QVERIFY(s.readyOlderThan(f.sequence).empty());
    QVERIFY(s.takeShown(b));
    QCOMPARE(s.state(b), State::Shown);
    QCOMPARE(s.newestReady().slot, -1);  // a shown slot is not pending
  }

  void olderPendingAreListedOldestFirst() {
    DisplaySlots s;
    const int a = s.beginWrite();
    const int b = s.beginWrite();
    const int c = s.beginWrite();
    s.endWrite(c, 1, 1);  // sequence 1 in slot 2
    s.endWrite(a, 1, 1);  // 2 in slot 0
    s.endWrite(b, 1, 1);  // 3 in slot 1
    const DisplaySlots::Frame f = s.newestReady();
    QCOMPARE(f.slot, b);
    const std::vector<int> older = s.readyOlderThan(f.sequence);
    QCOMPARE(older.size(), size_t(2));
    QCOMPARE(older[0], c);
    QCOMPARE(older[1], a);
  }

  void shownSlotIsReleasedAfterTheNextOneIsTaken() {
    DisplaySlots s;
    int a = s.beginWrite();
    s.endWrite(a, 64, 64);
    QVERIFY(s.takeShown(a));
    int b = s.beginWrite();
    QVERIFY(b >= 0 && b != a);
    s.endWrite(b, 64, 64);
    QVERIFY(s.takeShown(b));
    // `a` is the previously shown slot: still Shown until the frame that used it has ended.
    QCOMPARE(s.state(a), State::Shown);
    QVERIFY(s.releaseShown(a));
    QCOMPARE(s.state(a), State::Free);
    QCOMPARE(s.state(b), State::Shown);
    // Three slots in the steady state: shown + pending + writing.
    const int w = s.beginWrite();
    const int p = s.beginWrite();
    QVERIFY(w >= 0 && p >= 0);
    s.endWrite(p, 64, 64);
    QCOMPARE(s.state(w), State::Writing);
    QCOMPARE(s.beginWrite(), -1);  // writing + pending + shown: the next frame is skipped
  }

  void illegalTransitionsAreRefused() {
    DisplaySlots s;
    QVERIFY(!s.endWrite(0, 8, 8));         // Free -> Ready
    QVERIFY(!s.abortWrite(0));             // Free -> Free
    QVERIFY(!s.takeShown(0));              // Free -> Shown
    QVERIFY(!s.recycle(0));                // Free -> Free
    QVERIFY(!s.releaseShown(0));           // Free -> Free
    QVERIFY(!s.endWrite(-1, 8, 8));        // out of range
    QVERIFY(!s.takeShown(DisplaySlots::kCount));
    const int a = s.beginWrite();
    QVERIFY(!s.takeShown(a));              // Writing -> Shown
    QVERIFY(!s.recycle(a));                // Writing -> Free by the consumer
    s.endWrite(a, 8, 8);
    QVERIFY(!s.endWrite(a, 8, 8));         // Ready -> Ready
    QVERIFY(!s.abortWrite(a));             // Ready -> Free by the producer
    QVERIFY(!s.releaseShown(a));           // Ready -> Free as if shown
    QVERIFY(s.takeShown(a));
    QVERIFY(!s.takeShown(a));              // twice
    QVERIFY(!s.recycle(a));                // Shown -> Free by recycling
    QCOMPARE(s.state(a), State::Shown);
  }

  void sizesGrowOnlyInSteps64() {
    int w = 0;
    int h = 0;
    framebeam::growSize(0, 0, 160, 144, &w, &h);
    QCOMPARE(w, 192);
    QCOMPARE(h, 192);
    framebeam::growSize(w, h, 100, 100, &w, &h);  // smaller request: unchanged
    QCOMPARE(w, 192);
    QCOMPARE(h, 192);
    framebeam::growSize(w, h, 256, 192, &w, &h);  // exact multiple stays
    QCOMPARE(w, 256);
    QCOMPARE(h, 192);
    framebeam::growSize(w, h, 257, 10, &w, &h);  // one dimension grows, the other keeps its size
    QCOMPARE(w, 320);
    QCOMPARE(h, 192);
    QCOMPARE(framebeam::roundUpTo64(1), 64);
    QCOMPARE(framebeam::roundUpTo64(64), 64);
    QCOMPARE(framebeam::roundUpTo64(65), 128);
    QCOMPARE(framebeam::roundUpTo64(0), 0);
  }

  void generationSwitchStartsAFreshMachine() {
    // A new generation is a new DisplaySlots: the old one keeps its Shown/Ready slots until the consumer dropped it.
    DisplaySlots oldGen;
    const int a = oldGen.beginWrite();
    oldGen.endWrite(a, 256, 192);
    QVERIFY(oldGen.takeShown(a));
    DisplaySlots newGen;
    QCOMPARE(newGen.beginWrite(), 0);
    QCOMPARE(oldGen.state(a), State::Shown);  // unaffected
  }
};

FB_TEST_MAIN(DisplaySlotsTest)

#include "display_slots_test.moc"
