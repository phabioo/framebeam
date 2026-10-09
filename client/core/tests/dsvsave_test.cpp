// DeSmuME .dsv <-> raw cartridge save (dummy bytes only).
#include <QtTest>

#include "dsvsave.h"

using namespace framebeam;

class DsvSaveTest : public QObject {
  Q_OBJECT
  static QByteArray pattern(int size) {
    QByteArray b(size, 0);
    for (int i = 0; i < size; ++i) b[i] = static_cast<char>((i * 7 + 3) & 0xFF);
    return b;
  }
 private slots:
  void roundTripForEverySaveSize() {
    for (int size : {0x200, 0x2000, 0x8000, 0x10000, 0x40000, 0x80000, 0x100000}) {
      const QByteArray raw = pattern(size);
      QString err;
      const auto d = dsv::rawToDsv(raw, &err);
      QVERIFY2(d.has_value(), qPrintable(err));
      QCOMPARE(d->size(), qsizetype(size + dsv::footerSize()));
      QVERIFY(d->endsWith("|-DESMUME SAVE-|"));
      QVERIFY(d->mid(size, 10).startsWith("|<--Snip"));
      const auto back = dsv::dsvToRaw(*d, &err);
      QVERIFY2(back.has_value(), qPrintable(err));
      QCOMPARE(*back, raw);
    }
  }

  void footerFieldsMatchDeSmuMe() {
    const auto d = dsv::rawToDsv(pattern(0x2000));  // EEPROM 64kbit: type 2, address size 2
    QVERIFY(d);
    const char* f = d->constData() + d->size() - 16 - 24;
    auto u32 = [&](int i) { return quint32(quint8(f[4 * i])) | quint32(quint8(f[4 * i + 1])) << 8 | quint32(quint8(f[4 * i + 2])) << 16 | quint32(quint8(f[4 * i + 3])) << 24; };
    QCOMPARE(u32(0), 0x2000u);
    QCOMPARE(u32(1), 0x2000u);
    QCOMPARE(u32(2), 2u);
    QCOMPARE(u32(3), 2u);
    QCOMPARE(u32(4), 0x2000u);
    QCOMPARE(u32(5), 0u);
  }

  void invalidFootersAreRejected() {
    QString err;
    const QByteArray good = *dsv::rawToDsv(pattern(0x2000));
    QVERIFY(!dsv::dsvToRaw(QByteArray(), &err));
    QVERIFY(!dsv::dsvToRaw(pattern(0x2000), &err));            // a raw save is no .dsv
    QVERIFY(!dsv::dsvToRaw(good.left(good.size() - 1), &err));  // cookie damaged
    QByteArray bad = good;
    bad[bad.size() - 20] = 7;  // version
    QVERIFY(!dsv::dsvToRaw(bad, &err));
    bad = good;
    bad[bad.size() - 16 - 24 + 8] = 9;  // type
    QVERIFY(!dsv::dsvToRaw(bad, &err));
    bad = good;
    bad.insert(10, 'x');  // data size no longer matches the footer
    QVERIFY(!dsv::dsvToRaw(bad, &err));
    QVERIFY(!err.isEmpty());
    bad = good;
    bad[0x2000] = 'X';  // footer text
    QVERIFY(!dsv::dsvToRaw(bad, &err));
    QVERIFY(!dsv::rawToDsv(pattern(1000), &err));  // not a save size
  }
};

QTEST_GUILESS_MAIN(DsvSaveTest)
#include "dsvsave_test.moc"
