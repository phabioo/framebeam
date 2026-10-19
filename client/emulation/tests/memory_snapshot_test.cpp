// MemoryWatchSet/MemorySnapshot: pure behaviour on a synthetic map, then against the FrameBeam test core (synthetic
// 3DS-like memory map, EmulationRunner and LibretroBackend).
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <vector>

#include "emulation_runner.h"
#include "libretro_backend.h"
#include "memory_snapshot.h"
#include "test_core_layout.h"

using namespace framebeam::emu;
namespace mem = framebeam::testcore::mem;

namespace {
uint32_t rd32(const MemorySnapshot& s, uint64_t addr, bool* ok = nullptr) {
  uint8_t b[4] = {};
  const bool r = s.read(addr, b, 4);
  if (ok) *ok = r;
  return b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
}
}  // namespace

class MemorySnapshotTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;

  EmulationRunner::StartRequest request() const {
    EmulationRunner::StartRequest req;
    req.corePath = qEnvironmentVariable("FRAMEBEAM_MELONDS_DS_CORE");
    req.gamePath = QStringLiteral(FB_TEST_ROM_PATH);
    req.systemDir = m_dir.filePath(QStringLiteral("system"));
    req.saveDir = m_dir.filePath(QStringLiteral("save"));
    return req;
  }

 private slots:
  // ---- pure
  void nothingWatchedPublishesNothing() {
    std::vector<uint8_t> ram(256, 1);
    auto map = MemoryMap::fromBuffer(ram.data(), ram.size());
    MemoryWatchSet w;
    QVERIFY(w.empty());
    w.capture(map, 1);
    QVERIFY(!w.latest());
    QCOMPARE(w.generation(), uint64_t{0});
    w.setRanges("a", {{0, 16}});
    QVERIFY(!w.empty());
    w.clear("a");
    QVERIFY(w.empty());
  }

  void captureIsImmutableAndMerged() {
    std::vector<uint8_t> ram(0x1000);
    for (size_t i = 0; i < ram.size(); ++i) ram[i] = static_cast<uint8_t>(i);
    auto map = MemoryMap::fromBuffer(ram.data(), ram.size());
    MemoryWatchSet w;
    w.setRanges("a", {{0x10, 0x10}, {0x100, 8}});
    w.setRanges("b", {{0x18, 0x10}, {0x20, 0x10}});  // overlaps/adjoins a's first range
    w.capture(map, 7);
    auto s1 = w.latest();
    QVERIFY(s1);
    QCOMPARE(s1->frame(), uint64_t{7});
    QCOMPARE(w.generation(), uint64_t{1});
    uint8_t buf[0x20];
    QVERIFY(s1->read(0x10, buf, 0x20));  // spans the merged 0x10..0x30
    QCOMPARE(buf[0], uint8_t{0x10});
    QCOMPARE(buf[0x1F], uint8_t{0x2F});
    QVERIFY(s1->read(0x100, buf, 8));
    QVERIFY(!s1->read(0x30, buf, 1));   // outside
    QVERIFY(!s1->read(0x2F, buf, 2));   // crosses the end of a merged range
    QVERIFY(!s1->read(0x0F, buf, 2));   // crosses the start
    ram[0x10] = 0xEE;
    w.capture(map, 8);
    QCOMPARE(w.generation(), uint64_t{2});
    s1->read(0x10, buf, 1);
    QCOMPARE(buf[0], uint8_t{0x10});  // the old snapshot did not change
    w.latest()->read(0x10, buf, 1);
    QCOMPARE(buf[0], uint8_t{0xEE});
  }

  void invalidRanges() {
    std::vector<uint8_t> ram(0x2000, 5);
    auto map = MemoryMap::fromBuffer(ram.data(), ram.size(), 0x1000);
    MemoryWatchSet w;
    w.setRanges("a", {{0x0F00, 0x200}, {0x1000, 0x10}, {0x2F00, 0x200}, {0x100000, kMaxRangeBytes + 1}, {0x500000, 0}});
    w.capture(map, 1);
    auto s = w.latest();
    uint8_t buf[0x400];
    QVERIFY(s->read(0x1000, buf, 0x100));    // the mapped part of a partly unmapped range is valid
    QVERIFY(!s->read(0x0F00, buf, 0x200));   // the whole range is not
    QVERIFY(!s->read(0x0FFF, buf, 2));
    QVERIFY(!s->read(0x2F00, buf, 0x200));   // end unmapped
    QVERIFY(s->read(0x2F00, buf, 0x100));
    QVERIFY(!s->read(0x100000, buf, 4));     // over the per-range limit
    QVERIFY(!s->read(0x3000, buf, 1));       // never registered
  }

  void totalLimit() {
    std::vector<uint8_t> ram(static_cast<size_t>(kMaxRangeBytes), 1);
    auto map = MemoryMap::fromBuffer(ram.data(), ram.size(), 0);
    MemoryWatchSet w;
    std::vector<WatchRange> r;
    for (uint64_t i = 0; i < 5; ++i) r.push_back({i * 0x10000000ull, kMaxRangeBytes});  // 5 x 4 MiB > 16 MiB, one mapped
    w.setRanges("a", r);
    w.capture(map, 1);
    uint8_t b;
    QVERIFY(w.latest()->read(0, &b, 1));
    QVERIFY(!w.latest()->read(4 * 0x10000000ull, &b, 1));  // fifth exceeds the budget
  }

  // ---- test core
  void testCoreBackend() {
    LibretroBackend be;
    QString err;
    be.setSystemDirectory(m_dir.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dir.filePath(QStringLiteral("save")));
    const auto req = request();
    QVERIFY2(be.loadCore(req.corePath, &err) && be.loadGame(req.gamePath, &err), qPrintable(err));
    const MemoryMap* mm = be.memoryMap();
    QVERIFY(mm);
    QCOMPARE(mm->regions().size(), size_t{2});
    QVERIFY(be.runFrame());
    const auto s = mm->resolve(mem::kHeapAddr);
    QVERIFY(s.ptr);
    QCOMPARE(s.len, mem::kRegionSize);
    be.unloadGame();
    QVERIFY(!be.memoryMap());
    be.unloadCore();
  }

  // The test core re-sends SET_MEMORY_MAPS from retro_run at frame 30 with an extra "late" region: frame-exact on the backend.
  void testCoreLateMapBackend() {
    LibretroBackend be;
    QString err;
    be.setSystemDirectory(m_dir.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dir.filePath(QStringLiteral("save")));
    const auto req = request();
    QVERIFY2(be.loadCore(req.corePath, &err) && be.loadGame(req.gamePath, &err), qPrintable(err));
    const uint64_t gen0 = be.memoryMapGeneration();
    for (int i = 0; i < mem::kLateMapFrame - 1; ++i) QVERIFY(be.runFrame());
    QCOMPARE(be.memoryMapGeneration(), gen0);
    QCOMPARE(be.memoryMap()->regions().size(), size_t{2});
    QVERIFY(!be.memoryMap()->resolve(mem::kLateAddr).ptr);
    QVERIFY(be.runFrame());  // frame 30: the core re-sends the map
    QVERIFY(be.memoryMapGeneration() > gen0);
    const MemoryMap* mm = be.memoryMap();
    QVERIFY(mm);
    QCOMPARE(mm->regions().size(), size_t{3});
    const auto s = mm->resolve(mem::kLateAddr);
    QVERIFY(s.ptr);
    QCOMPARE(s.len, mem::kLateSize);
    be.unloadGame();
    be.unloadCore();
  }

  // Runner + plugin-facing snapshots: the late address is unreadable first, then readable, and the published region list
  // gains the region (memoryRegionsChanged).
  void testCoreLateMapRunner() {
    qputenv("FB_TEST_CORE_LATE_MAP_FRAME", "120");  // about 2 s at 60 fps: time to observe the state before
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QSignalSpy started(&runner, &EmulationRunner::started);
    QSignalSpy changed(&runner, &EmulationRunner::memoryRegionsChanged);
    auto watch = runner.memoryWatch();
    watch->setRanges("late", {{mem::kHeapAddr, 0x20}, {mem::kLateAddr, 16}});
    runner.start(request());
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(watch->latest() != nullptr, 10000);
    // Timing-independent "before": the worker publishes the regions, emits memoryRegionsChanged and only then captures
    // the next snapshot (emulation_runner.cpp, after runFrame). So a snapshot and region list taken first, with the count
    // still 0 when read afterwards, predate the re-send. If the machine stalled and it already fired, skip this part.
    const auto beforeRegions = runner.memoryRegions();
    const auto beforeSnap = watch->latest();
    uint8_t b[4];
    if (changed.count() == 0) {
      QVERIFY(!beforeSnap->read(mem::kLateAddr, b, 4));  // not mapped yet
      QCOMPARE(beforeRegions.size(), size_t{2});
    } else {
      QWARN("late map was re-sent before the 'before' state could be observed; skipping that part");
    }

    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 20000);
    const auto regions = runner.memoryRegions();
    QCOMPARE(regions.size(), size_t{3});
    QCOMPARE(QString::fromStdString(regions[2].addrspace), QStringLiteral("late"));
    QCOMPARE(regions[2].start, mem::kLateAddr);
    bool ok = false;
    QTRY_VERIFY_WITH_TIMEOUT(watch->latest()->read(mem::kLateAddr, b, 4), 10000);
    QCOMPARE(rd32(*watch->latest(), mem::kLateAddr, &ok), mem::kLateMagic);
    QVERIFY(ok);
    runner.stop();
    qunsetenv("FB_TEST_CORE_LATE_MAP_FRAME");
  }

  void testCoreSnapshots() {
    EmulationRunner runner(std::make_unique<LibretroBackend>());
    QSignalSpy started(&runner, &EmulationRunner::started);
    QSignalSpy failed(&runner, &EmulationRunner::startFailed);
    auto watch = runner.memoryWatch();
    QVERIFY(watch);
    QVERIFY(runner.memoryRegions().empty());
    watch->setRanges("test", {{mem::kHeapAddr, 0x20}});
    runner.setSnapshotInterval(0);
    QCOMPARE(runner.snapshotInterval(), 1);
    runner.start(request());
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 20000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(runner.memoryRegions().size() >= 2);  // 3 once the test core re-sends its map at frame 30
    QCOMPARE(QString::fromStdString(runner.memoryRegions()[0].addrspace), QStringLiteral("heap"));

    QTRY_VERIFY_WITH_TIMEOUT(watch->latest() != nullptr, 10000);
    auto a = watch->latest();
    bool ok = false;
    QCOMPARE(rd32(*a, mem::kHeapAddr, &ok), mem::kMagic);
    QVERIFY(ok);
    const uint32_t fa = rd32(*a, mem::kHeapAddr + mem::kFrameOff);
    const uint64_t gen = watch->generation();
    QTRY_VERIFY_WITH_TIMEOUT(watch->generation() > gen + 2, 10000);
    auto b = watch->latest();
    QVERIFY(rd32(*b, mem::kHeapAddr + mem::kFrameOff) > fa);
    QCOMPARE(rd32(*a, mem::kHeapAddr + mem::kFrameOff), fa);  // immutable

    // Pointer following: read the party pointer, then register the target range and read the block.
    const uint32_t partyAddr = rd32(*b, mem::kHeapAddr + mem::kPartyPtrOff);
    QCOMPARE(partyAddr, mem::kPartyAddr);
    QVERIFY(!b->read(partyAddr, &ok, 1));  // not watched yet
    const uint32_t blockLen = 4 + mem::kPartySlots * mem::kPartySlotSize;
    watch->setRanges("test", {{mem::kHeapAddr, 0x20}, {partyAddr, blockLen}});
    QTRY_VERIFY_WITH_TIMEOUT(watch->latest()->read(partyAddr, &ok, 1), 10000);
    auto c = watch->latest();
    QCOMPARE(rd32(*c, partyAddr), mem::kPartySlots);
    uint8_t marker = 0;
    QVERIFY(c->read(partyAddr + 4 + 3 * mem::kPartySlotSize, &marker, 1));
    QCOMPARE(marker, static_cast<uint8_t>(mem::kSlotMarker + 3));
    uint8_t buf[8];
    QVERIFY(!c->read(mem::kLinearAddr + mem::kRegionSize - 4, buf, 8));  // not watched / crosses the region end
    QVERIFY(!c->read(0x20000000, buf, 4));                               // unmapped

    // A range into unmapped space reads as invalid; every second frame with interval 2.
    watch->setRanges("other", {{0x30000000, 16}});
    runner.setSnapshotInterval(2);
    const uint64_t g1 = watch->generation();
    QTest::qWait(300);
    QVERIFY(watch->generation() > g1);
    QVERIFY(!watch->latest()->read(0x30000000, buf, 4));

    watch->clear("test");
    watch->clear("other");
    QVERIFY(watch->empty());
    // A capture that was already past the "anything watched?" check on the emulation thread when clear() ran may still
    // publish once (under load that is a whole extra generation). Let it settle: the generation must hold still for 150 ms,
    // then it must stay put for another 200 ms (nothing watched: no further capture).
    uint64_t g2 = watch->generation();
    int stableMs = 0;
    for (int waited = 0; stableMs < 150 && waited < 10000; waited += 25) {
      QTest::qWait(25);
      const uint64_t now = watch->generation();
      stableMs = now == g2 ? stableMs + 25 : 0;
      g2 = now;
    }
    QVERIFY2(stableMs >= 150, "the generation never settled after clear()");
    QTest::qWait(200);
    QCOMPARE(watch->generation(), g2);

    runner.stop();
    QVERIFY(runner.memoryRegions().empty());
  }
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  MemorySnapshotTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "memory_snapshot_test.moc"
