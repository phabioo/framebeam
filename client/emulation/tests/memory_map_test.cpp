// MemoryMap address resolution (select / disconnect / offset / len, first match, unmapped). Pure, no core.
#include <QtTest>

#include <vector>

#include "memory_map.h"

using namespace framebeam::emu;

namespace {
MemoryMap::Descriptor desc(void* p, uint64_t start, uint64_t len, uint64_t select = 0, uint64_t disconnect = 0,
                           uint64_t offset = 0, const char* space = "") {
  MemoryMap::Descriptor d;
  d.ptr = p;
  d.start = start;
  d.len = len;
  d.select = select;
  d.disconnect = disconnect;
  d.offset = offset;
  d.addrspace = space;
  return d;
}
}  // namespace

class MemoryMapTest : public QObject {
  Q_OBJECT
 private slots:
  void emptyMapIsUnmapped() {
    MemoryMap m;
    QVERIFY(m.empty());
    QVERIFY(m.resolve(0).ptr == nullptr);
  }

  void selectDerivedFromLen() {
    std::vector<uint8_t> a(0x1000), b(0x1000);
    auto m = MemoryMap::fromDescriptors({desc(a.data(), 0x08000000, 0x1000), desc(b.data(), 0x14000000, 0x1000)});
    auto s = m.resolve(0x08000010);
    QCOMPARE(s.ptr, a.data() + 0x10);
    QCOMPARE(s.len, uint64_t{0x1000 - 0x10});
    QCOMPARE(m.resolve(0x14000FFF).ptr, b.data() + 0xFFF);
    QCOMPARE(m.resolve(0x14000FFF).len, uint64_t{1});
    QVERIFY(m.resolve(0x08001000).ptr == nullptr);  // one past the end
    QVERIFY(m.resolve(0x07FFFFFF).ptr == nullptr);
    QVERIFY(m.resolve(0x0).ptr == nullptr);
  }

  void nonPowerOfTwoLenWithoutSelect() {
    std::vector<uint8_t> a(0x1000);
    auto m = MemoryMap::fromDescriptors({desc(a.data(), 0x08000000, 0x01234000)});
    QCOMPARE(m.resolve(0x08000010).ptr, a.data() + 0x10);
    QCOMPARE(m.resolve(0x08000010).len, uint64_t{0x01234000 - 0x10});
    QCOMPARE(m.resolve(0x08000000 + 0x01233FFF).len, uint64_t{1});
    QVERIFY(m.resolve(0x08000000 + 0x01234000).ptr == nullptr);
    QVERIFY(m.resolve(0x07FFFFFF).ptr == nullptr);
  }

  void adjacentNonPowerOfTwoDescriptors() {
    std::vector<uint8_t> a(0x10), b(0x10);
    auto m = MemoryMap::fromDescriptors({desc(a.data(), 0x1000, 0x3000), desc(b.data(), 0x4000, 0x5000)});
    QCOMPARE(m.resolve(0x3FFF).len, uint64_t{1});
    QCOMPARE(m.resolve(0x3FFF).ptr, a.data() + 0x2FFF);
    QCOMPARE(m.resolve(0x4000).ptr, b.data());
    QCOMPARE(m.resolve(0x8FFF).len, uint64_t{1});
    QVERIFY(m.resolve(0x9000).ptr == nullptr);
  }

  void explicitSelectMirrors() {
    std::vector<uint8_t> a(0x1000);
    // 4 KiB at 0x02000000 mirrored every 0x10000 inside a 0x02FF0000 window: select ignores bits 0x1000..0xF000.
    auto m = MemoryMap::fromDescriptors({desc(a.data(), 0x02000000, 0x1000, 0xFFFF0000u & ~0u, 0, 0)});
    QVERIFY(m.resolve(0x02000004).ptr == a.data() + 4);
    QVERIFY(m.resolve(0x02010004).ptr == nullptr);  // select compares bits 16..31
    auto m2 = MemoryMap::fromDescriptors({desc(a.data(), 0x02000000, 0x1000, 0xFF000000u)});
    QVERIFY(m2.resolve(0x02ABC004).ptr == nullptr);  // matches select, but beyond len
    QCOMPARE(m2.resolve(0x02000FFC).ptr, a.data() + 0xFFC);
  }

  void disconnectMasksOutBits() {
    std::vector<uint8_t> a(0x100);
    // Address bit 0x100 is not connected: 0x1000 and 0x1100 reach the same byte.
    auto m = MemoryMap::fromDescriptors({desc(a.data(), 0x1000, 0x100, 0xFFFFFE00u, 0x100)});
    QCOMPARE(m.resolve(0x1010).ptr, a.data() + 0x10);
    QCOMPARE(m.resolve(0x1110).ptr, a.data() + 0x10);
    QCOMPARE(m.resolve(0x1110).len, uint64_t{0xF0});
    QVERIFY(m.resolve(0x1210).ptr == nullptr);
  }

  void offsetIntoHostBuffer() {
    std::vector<uint8_t> a(0x2000);
    auto m = MemoryMap::fromDescriptors({desc(a.data(), 0x4000, 0x1000, 0, 0, 0x800)});
    QCOMPARE(m.resolve(0x4000).ptr, a.data() + 0x800);
    QCOMPARE(m.resolve(0x4010).ptr, a.data() + 0x810);
  }

  void firstMatchWinsAndNullPtrSkipped() {
    std::vector<uint8_t> a(0x1000), b(0x1000), c(0x1000);
    auto m = MemoryMap::fromDescriptors({desc(nullptr, 0x1000, 0x1000), desc(a.data(), 0x1000, 0x1000), desc(b.data(), 0x1000, 0x1000),
                                         desc(c.data(), 0x8000, 0x1000)});
    QCOMPARE(m.resolve(0x1004).ptr, a.data() + 4);
    QCOMPARE(m.resolve(0x8000).ptr, c.data());
  }

  void bufferFallback() {
    std::vector<uint8_t> a(0x500);
    auto m = MemoryMap::fromBuffer(a.data(), a.size(), 0, "system_ram");
    QCOMPARE(m.resolve(0).ptr, a.data());
    QCOMPARE(m.resolve(0x4FF).len, uint64_t{1});
    QVERIFY(m.resolve(0x500).ptr == nullptr);
    const auto r = m.regions();
    QCOMPARE(r.size(), size_t{1});
    QCOMPARE(r[0].len, uint64_t{0x500});
    QCOMPARE(QString::fromStdString(r[0].addrspace), QStringLiteral("system_ram"));
  }

  void regionsReportDescriptors() {
    std::vector<uint8_t> a(0x1000);
    auto d = desc(a.data(), 0x08000000, 0x1000, 0, 0, 0, "heap");
    d.flags = 7;
    const auto r = MemoryMap::fromDescriptors({d}).regions();
    QCOMPARE(r.size(), size_t{1});
    QCOMPARE(r[0].start, uint64_t{0x08000000});
    QCOMPARE(r[0].flags, uint64_t{7});
    QCOMPARE(QString::fromStdString(r[0].addrspace), QStringLiteral("heap"));
  }
};

QTEST_GUILESS_MAIN(MemoryMapTest)
#include "memory_map_test.moc"
