// readGameIdentity on synthetic header files (no real game data).
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "game_identity.h"

using namespace framebeam::emu;

namespace {
void put(QByteArray& b, int off, const QByteArray& v) {
  if (b.size() < off + v.size()) b.resize(off + v.size());
  b.replace(off, v.size(), v);
}
QByteArray le(quint64 v, int n) {
  QByteArray r;
  for (int i = 0; i < n; ++i) r.append(static_cast<char>((v >> (8 * i)) & 0xFF));
  return r;
}
}  // namespace

class GameIdentityTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;
  QString write(const QString& name, const QByteArray& data) {
    QFile f(m_dir.filePath(name));
    [&] { QVERIFY(f.open(QIODevice::WriteOnly)); }();
    f.write(data);
    return f.fileName();
  }

 private slots:
  void nds() {
    QByteArray b(0x200, '\0');
    put(b, 0x0C, "ABCD");
    const auto id = readGameIdentity(write("a.nds", b), "nds_header");
    QCOMPARE(id.code, QStringLiteral("ABCD"));
    QCOMPARE(id.kind, QStringLiteral("nds-gamecode"));
  }

  void ndsInvalid() {
    QByteArray b(0x200, '\0');  // NUL bytes in the code
    QVERIFY(readGameIdentity(write("b.nds", b), "nds_header").code.isEmpty());
    QVERIFY(readGameIdentity(write("short.nds", QByteArray(8, 'x')), "nds_header").kind.isEmpty());
  }

  void bareNcch() {
    QByteArray b(0x200, '\0');
    put(b, 0x100, "NCCH");
    put(b, 0x118, le(0x0004000000123400ull, 8));
    const auto id = readGameIdentity(write("a.cxi", b), "3ds_ncsd");
    QCOMPARE(id.code, QStringLiteral("0004000000123400"));
    QCOMPARE(id.kind, QStringLiteral("3ds-titleid"));
  }

  void ncsdPartitionZero() {
    QByteArray b(0x4000, '\0');
    put(b, 0x100, "NCSD");
    put(b, 0x120, le(0x10, 4));  // partition 0 at 0x10 * 0x200 = 0x2000
    put(b, 0x2000 + 0x100, "NCCH");
    put(b, 0x2000 + 0x118, le(0x000400000ABCDEF0ull, 8));
    const auto id = readGameIdentity(write("a.3ds", b), "3ds_ncsd");
    QCOMPARE(id.code, QStringLiteral("000400000ABCDEF0"));  // uppercase hex, zero padded
  }

  void invalid3ds() {
    QVERIFY(readGameIdentity(write("zero.3ds", QByteArray(0x400, '\0')), "3ds_ncsd").code.isEmpty());
    QByteArray b(0x400, '\0');
    put(b, 0x100, "NCSD");
    put(b, 0x120, le(0x1000000, 4));  // partition beyond the file
    QVERIFY(readGameIdentity(write("trunc.3ds", b), "3ds_ncsd").code.isEmpty());
    put(b, 0x120, le(0, 4));
    QVERIFY(readGameIdentity(write("nopart.3ds", b), "3ds_ncsd").code.isEmpty());
  }

  void gba() {
    QByteArray b(0x100, '\0');
    put(b, 0xA0, "SYNTH TITLE\0");
    put(b, 0xAC, "ABCE");
    put(b, 0xB0, "01");
    b[0xBC] = 3;
    const auto id = readGameIdentity(write("a.gba", b), "gba_header");
    QCOMPARE(id.code, QStringLiteral("ABCE"));
    QCOMPARE(id.kind, QStringLiteral("gba-gamecode"));
    QCOMPARE(id.title, QStringLiteral("SYNTH TITLE"));
    QCOMPARE(id.maker, QStringLiteral("01"));
    QCOMPARE(id.version, 3);
  }

  void gbaInvalid() {
    QVERIFY(readGameIdentity(write("zero.gba", QByteArray(0x100, '\0')), "gba_header").kind.isEmpty());  // NUL code
    QVERIFY(readGameIdentity(write("short.gba", QByteArray(0x80, 'A')), "gba_header").kind.isEmpty());
  }

  // Synthetic GB header: title, flags and a valid header checksum at 0x14D.
  static QByteArray gbRom(const QByteArray& title, quint8 cgbFlag, quint8 oldLicensee, const QByteArray& newLicensee, quint8 version,
                          bool goodChecksum = true) {
    QByteArray b(0x150, '\0');
    put(b, 0x134, title);
    b[0x143] = static_cast<char>(cgbFlag);
    put(b, 0x144, newLicensee);
    b[0x14B] = static_cast<char>(oldLicensee);
    b[0x14C] = static_cast<char>(version);
    quint8 sum = 0;
    for (int i = 0x134; i <= 0x14C; ++i) sum = static_cast<quint8>(sum - static_cast<quint8>(b[i]) - 1);
    b[0x14D] = static_cast<char>(goodChecksum ? sum : sum + 1);
    return b;
  }

  void gbTitleAndLicensee() {
    auto id = readGameIdentity(write("a.gb", gbRom("SYNTHGAMEABCDE", 0x00, 0x33, "AB", 2)), "gb_header");
    QCOMPARE(id.kind, QStringLiteral("gb-title"));
    QCOMPARE(id.code, QStringLiteral("SYNTHGAMEABCDE"));  // up to 16 bytes without the CGB flag (byte 0x143 is the flag itself)
    QCOMPARE(id.maker, QStringLiteral("AB"));               // old licensee 0x33 -> new licensee code
    QCOMPARE(id.version, 2);
    id = readGameIdentity(write("b.gb", gbRom("SYNTH", 0x00, 0x01, "ZZ", 0)), "gb_header");
    QCOMPARE(id.code, QStringLiteral("SYNTH"));
    QCOMPARE(id.maker, QStringLiteral("01"));  // old licensee byte as hex
  }

  void gbcTitleIsElevenBytes() {
    // With the CGB flag set, bytes 0x13F..0x142 hold the manufacturer code, not the title.
    const auto id = readGameIdentity(write("a.gbc", gbRom("LONGTITLE12ABCD", 0x80, 0x33, "01", 1)), "gb_header");
    QCOMPARE(id.code, QStringLiteral("LONGTITLE12"));
    QCOMPARE(readGameIdentity(write("b.gbc", gbRom("LONGTITLE12ABCD", 0xC0, 0x33, "01", 1)), "gb_header").code, QStringLiteral("LONGTITLE12"));
  }

  void gbInvalid() {
    QVERIFY(readGameIdentity(write("bad.gb", gbRom("SYNTH", 0, 0, "00", 0, false)), "gb_header").kind.isEmpty());  // checksum mismatch
    QVERIFY(readGameIdentity(write("zero.gb", QByteArray(0x150, '\0')), "gb_header").kind.isEmpty());              // empty title
    QVERIFY(readGameIdentity(write("short.gb", QByteArray(0x100, 'x')), "gb_header").kind.isEmpty());
  }

  void unknownSystemAndMissingFile() {
    QByteArray b(0x200, 'A');
    QVERIFY(readGameIdentity(write("c.bin", b), "nds").kind.isEmpty());  // a system id is no reader
    QVERIFY(readGameIdentity(write("d.bin", b), QString()).kind.isEmpty());
    QVERIFY(readGameIdentity(write("e.bin", b), "unknown_reader").kind.isEmpty());
    QVERIFY(readGameIdentity(m_dir.filePath("missing.nds"), "nds_header").kind.isEmpty());
    QVERIFY(readGameIdentity(QString(), "nds_header").kind.isEmpty());
  }
};

QTEST_GUILESS_MAIN(GameIdentityTest)
#include "game_identity_test.moc"
