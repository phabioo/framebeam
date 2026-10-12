// Input mapping chain end to end against the FrameBeam test core (no emulator): FrameBeam input -> system profile ->
// libretro joypad mask -> LibretroBackend -> core. ZR must arrive as R2 (bit 13) in the 3ds profile and as R in nds.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "inputmap.h"
#include "libretro_backend.h"
#include "system_manifest.h"
#include "test_core_layout.h"

using namespace framebeam::emu;
namespace tc = framebeam::testcore;
namespace fi = framebeam::input;

class TestCoreInputTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;

  QImage frameWithInputs(const QString& profile, const QStringList& inputs) {
    QFile g(m_dir.filePath(QStringLiteral("game.nds")));
    if (!g.open(QIODevice::WriteOnly)) return {};
    g.write("x");
    g.close();
    LibretroBackend be;
    QString err;
    be.setSystemDirectory(m_dir.filePath(QStringLiteral("system")));
    be.setSaveDirectory(m_dir.filePath(QStringLiteral("save")));
    if (!be.loadCore(QStringLiteral(FB_TEST_CORE_PATH), &err) || !be.loadGame(g.fileName(), &err)) return {};
    quint32 mask = 0;
    for (const QString& id : inputs) mask |= fi::inputBit(id);
    be.setJoypadState(0, fi::libretroMaskFor(profile, mask));
    for (int i = 0; i < 2; ++i)
      if (!be.runFrame()) return {};
    const QImage img = be.videoFrame();
    be.unloadCore();
    return img;
  }
  static bool pressed(const QImage& img, int id) {
    return (img.pixel(id * tc::kCellWidth + 8, tc::kStripY0 + 8) & 0xFFFFFFu) == 0u;
  }
  static QList<int> pressedIds(const QImage& img) {
    QList<int> out;
    for (int id = 0; id < 16; ++id)
      if (pressed(img, id)) out.append(id);
    return out;
  }

 private slots:
  void initTestCase() {  // loading the manifests registers the input tables
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
  }
  void init() {
    qputenv("FB_TEST_CORE_FLAVOR", "generic");
    qunsetenv("FB_TEST_CORE_NO_BITMASK");
  }
  void threeDsZrIsR2AndZlIsL2() {
    const QImage img = frameWithInputs(QStringLiteral("3ds"), {QStringLiteral("zr")});
    QVERIFY(!img.isNull());
    QCOMPARE(pressedIds(img), QList<int>{13});
    QCOMPARE(pressedIds(frameWithInputs(QStringLiteral("3ds"), {QStringLiteral("zl")})), QList<int>{12});
  }
  void gameBoyAdvanceShouldersAndGameBoyHaveNone() {
    QCOMPARE(pressedIds(frameWithInputs(QStringLiteral("gba"), {QStringLiteral("zl"), QStringLiteral("r")})), (QList<int>{10, 11}));
    QCOMPARE(pressedIds(frameWithInputs(QStringLiteral("gb"), {QStringLiteral("l"), QStringLiteral("a"), QStringLiteral("lup")})), (QList<int>{4, 8}));
  }
  void ndsZrIsR() {
    QCOMPARE(pressedIds(frameWithInputs(QStringLiteral("nds"), {QStringLiteral("zr")})), QList<int>{11});
    QCOMPARE(pressedIds(frameWithInputs(QStringLiteral("nds"), {QStringLiteral("a"), QStringLiteral("start")})), (QList<int>{3, 8}));
  }
};

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  TestCoreInputTest t;
  return QTest::qExec(&t, argc, argv);
}
#include "test_core_input_test.moc"
