// Input profile tables registered from the built-in system manifests (no SDL).
#include <QtTest>

#include "controllerprofiles.h"
#include "inputmap.h"
#include "system_manifest.h"

using namespace uzume;
using namespace uzume::input;

namespace {
constexpr quint32 kB = 1u << 0, kY = 1u << 1, kSelect = 1u << 2, kStart = 1u << 3, kUp = 1u << 4, kDown = 1u << 5, kLeft = 1u << 6,
                  kRight = 1u << 7, kA = 1u << 8, kX = 1u << 9, kL = 1u << 10, kR = 1u << 11, kL2 = 1u << 12, kR2 = 1u << 13;

// The tables as they were hard-coded before the maps moved into the manifests (reference for "masks unchanged").
QHash<QString, quint32> legacyNds() {
  QHash<QString, quint32> h = {{"b", kB}, {"y", kY}, {"select", kSelect}, {"start", kStart}, {"up", kUp}, {"down", kDown},
                               {"left", kLeft}, {"right", kRight}, {"a", kA}, {"x", kX}, {"l", kL}, {"r", kR}};
  h.insert("zl", kL);
  h.insert("zr", kR);
  h.insert("lup", kUp);
  h.insert("ldown", kDown);
  h.insert("lleft", kLeft);
  h.insert("lright", kRight);
  return h;
}
QHash<QString, quint32> legacy3ds() {
  QHash<QString, quint32> h = {{"b", kB}, {"y", kY}, {"select", kSelect}, {"start", kStart}, {"up", kUp}, {"down", kDown},
                               {"left", kLeft}, {"right", kRight}, {"a", kA}, {"x", kX}, {"l", kL}, {"r", kR}};
  h.insert("zl", kL2);
  h.insert("zr", kR2);
  h.insert("cup", kLibretroCStickUp);
  h.insert("cdown", kLibretroCStickDown);
  h.insert("cleft", kLibretroCStickLeft);
  h.insert("cright", kLibretroCStickRight);
  h.insert("lup", kLibretroCirclePadUp);
  h.insert("ldown", kLibretroCirclePadDown);
  h.insert("lleft", kLibretroCirclePadLeft);
  h.insert("lright", kLibretroCirclePadRight);
  return h;
}
quint32 legacyMask(const QHash<QString, quint32>& t, quint32 in) {
  quint32 out = 0;
  const auto& inputs = uzumeInputs();
  for (int i = 0; i < inputs.size(); ++i)
    if (in & (1u << i)) out |= t.value(inputs.at(i).id, 0u);
  return out;
}
}  // namespace

class InputProfileTest : public QObject {
  Q_OBJECT
  static quint32 bit(const char* id) { return inputBit(QString::fromLatin1(id)); }

 private slots:
  void initTestCase() {
    emu::ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
  }

  void ndsAndThreeDsMasksAreUnchanged() {
    const auto nds = legacyNds();
    const auto t3 = legacy3ds();
    const int n = uzumeInputs().size();
    QVERIFY(n <= 24);
    for (quint32 mask = 0; mask < (1u << n); mask += 37) {  // spread over the whole input space
      QCOMPARE(libretroMaskFor(QStringLiteral("nds"), mask), legacyMask(nds, mask));
      QCOMPARE(libretroMaskFor(QStringLiteral("3ds"), mask), legacyMask(t3, mask));
    }
    for (int i = 0; i < n; ++i) {  // every single input
      QCOMPARE(libretroMaskFor(QStringLiteral("nds"), 1u << i), legacyMask(nds, 1u << i));
      QCOMPARE(libretroMaskFor(QStringLiteral("3ds"), 1u << i), legacyMask(t3, 1u << i));
    }
  }

  void unknownProfileFallsBackToNds() {
    QCOMPARE(libretroMaskFor(QString(), bit("zl") | bit("lup")), kL | kUp);
    QCOMPARE(libretroMaskFor(QStringLiteral("nope"), bit("zr")), kR);
    QVERIFY(inputProfileMaps(QStringLiteral("nope"), QStringLiteral("zl")));  // nds maps it
    QVERIFY(!inputProfileMaps(QStringLiteral("nope"), QStringLiteral("cup")));
  }

  void gameBoyProfile() {
    const QString gb = QStringLiteral("gb");
    QCOMPARE(libretroMaskFor(gb, bit("a") | bit("b") | bit("start") | bit("select")), kA | kB | kStart | kSelect);
    QCOMPARE(libretroMaskFor(gb, bit("up") | bit("lleft") | bit("lright")), kUp | kLeft | kRight);  // left stick = second D-pad
    QCOMPARE(libretroMaskFor(gb, bit("l") | bit("r") | bit("x") | bit("y") | bit("zl") | bit("cup")), 0u);
    QVERIFY(inputProfileMaps(gb, QStringLiteral("a")));
    QVERIFY(!inputProfileMaps(gb, QStringLiteral("l")));
    QVERIFY(!inputProfileMaps(gb, QStringLiteral("x")));
    QVERIFY(!inputProfileLabel(gb, QStringLiteral("lup")).isEmpty());
    QVERIFY(inputProfileLabel(gb, QStringLiteral("a")).isEmpty());
  }

  void gameBoyAdvanceProfile() {
    const QString gba = QStringLiteral("gba");
    QCOMPARE(libretroMaskFor(gba, bit("l") | bit("r")), kL | kR);
    QCOMPARE(libretroMaskFor(gba, bit("zl") | bit("zr")), kL | kR);
    QCOMPARE(libretroMaskFor(gba, bit("a") | bit("b") | bit("start") | bit("select") | bit("down") | bit("ldown")), kA | kB | kStart | kSelect | kDown);
    QCOMPARE(libretroMaskFor(gba, bit("x") | bit("y") | bit("cdown")), 0u);
    const Bindings b{{QStringLiteral("zl"), {keyToken(Qt::Key_E)}}, {QStringLiteral("a"), {keyToken(Qt::Key_J)}}};
    QCOMPARE(keyMapFor(gba, b).value(Qt::Key_E), kL);
    QCOMPARE(keyMapFor(gba, b).value(Qt::Key_J), kA);
    QCOMPARE(keyMapFor(QStringLiteral("gb"), b).value(Qt::Key_E, 0u), 0u);
  }

  void conflictsAndInvalidMapsAreErrors() {
    QString err;
    // Same name, same map: fine (gb and gbc share the profile).
    emu::ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const auto* gb = reg.find(QStringLiteral("gb"));
    QVERIFY(gb);
    QVERIFY(registerInputProfile(gb->inputProfile, gb->inputMap, gb->inputLabels, &err));
    // Same name, different map.
    QMap<QString, QString> other = gb->inputMap;
    other.insert(QStringLiteral("a"), QStringLiteral("b"));
    QVERIFY(!registerInputProfile(gb->inputProfile, other, gb->inputLabels, &err));
    QVERIFY(err.contains(QStringLiteral("conflicting")));
    QCOMPARE(libretroMaskFor(gb->inputProfile, bit("a")), kA);  // table untouched
    QVERIFY(!registerInputProfile(QStringLiteral("t1"), {{QStringLiteral("a"), QStringLiteral("turbo")}}, {}, &err));
    QVERIFY(!registerInputProfile(QStringLiteral("t2"), {{QStringLiteral("nope"), QStringLiteral("a")}}, {}, &err));
    QVERIFY(!registerInputProfile(QString(), {}, {}, &err));
  }

  void targetNames() {
    QCOMPARE(libretroTargetBit(QStringLiteral("b")), 1u << 0);
    QCOMPARE(libretroTargetBit(QStringLiteral("r3")), 1u << 15);
    QCOMPARE(libretroTargetBit(QStringLiteral("c_stick_up")), kLibretroCStickUp);
    QCOMPARE(libretroTargetBit(QStringLiteral("circle_pad_right")), kLibretroCirclePadRight);
    QCOMPARE(libretroTargetBit(QStringLiteral("zl")), 0u);
  }
};

QTEST_GUILESS_MAIN(InputProfileTest)
#include "inputprofile_test.moc"
