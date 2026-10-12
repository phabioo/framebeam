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
    put(b, 0x1E, le(3, 1));
    const auto id = readGameIdentity(write("a.nds", b), "nds");
    QCOMPARE(id.code, QStringLiteral("ABCD"));
    QCOMPARE(id.kind, QStringLiteral("nds-gamecode"));
    QCOMPARE(id.version, 3);
  }

  void ndsInvalid() {
    QByteArray b(0x200, '\0');  // NUL bytes in the code
    QVERIFY(readGameIdentity(write("b.nds", b), "nds").code.isEmpty());
    QVERIFY(readGameIdentity(write("short.nds", QByteArray(8, 'x')), "nds").kind.isEmpty());
  }

  void bareNcch() {
    QByteArray b(0x200, '\0');
    put(b, 0x100, "NCCH");
    put(b, 0x118, le(0x0004000000123400ull, 8));
    put(b, 0x112, le(0x0102, 2));
    const auto id = readGameIdentity(write("a.cxi", b), "3ds");
    QCOMPARE(id.version, 0x0102);
    QCOMPARE(id.code, QStringLiteral("0004000000123400"));
    QCOMPARE(id.kind, QStringLiteral("3ds-titleid"));
  }

  void ncsdPartitionZero() {
    QByteArray b(0x4000, '\0');
    put(b, 0x100, "NCSD");
    put(b, 0x120, le(0x10, 4));  // partition 0 at 0x10 * 0x200 = 0x2000
    put(b, 0x2000 + 0x100, "NCCH");
    put(b, 0x2000 + 0x118, le(0x000400000ABCDEF0ull, 8));
    const auto id = readGameIdentity(write("a.3ds", b), "3ds");
    QCOMPARE(id.code, QStringLiteral("000400000ABCDEF0"));  // uppercase hex, zero padded
  }

  void invalid3ds() {
    QVERIFY(readGameIdentity(write("zero.3ds", QByteArray(0x400, '\0')), "3ds").code.isEmpty());
    QByteArray b(0x400, '\0');
    put(b, 0x100, "NCSD");
    put(b, 0x120, le(0x1000000, 4));  // partition beyond the file
    QVERIFY(readGameIdentity(write("trunc.3ds", b), "3ds").code.isEmpty());
    put(b, 0x120, le(0, 4));
    QVERIFY(readGameIdentity(write("nopart.3ds", b), "3ds").code.isEmpty());
  }

  void unknownSystemAndMissingFile() {
    QByteArray b(0x200, 'A');
    QVERIFY(readGameIdentity(write("c.bin", b), "gba").kind.isEmpty());
    QVERIFY(readGameIdentity(m_dir.filePath("missing.nds"), "nds").kind.isEmpty());
    QVERIFY(readGameIdentity(QString(), "nds").kind.isEmpty());
  }
};

QTEST_GUILESS_MAIN(GameIdentityTest)
#include "game_identity_test.moc"
