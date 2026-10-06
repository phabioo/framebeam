#include <QTemporaryDir>
#include <QtTest>

#include "controllerprofiles.h"

using namespace framebeam;

class ControllerProfilesTest : public QObject {
  Q_OBJECT
 private slots:
  void builtinProfilesAreReadOnlyDefaults() {
    QTemporaryDir dir;
    ControllerProfiles p(dir.path());
    QCOMPARE(p.all().size(), 2);
    const auto pad = p.find(QString::fromLatin1(ControllerProfiles::kBuiltinGamepadId));
    QVERIFY(pad && pad->builtin);
    QCOMPARE(pad->name, QStringLiteral("Standard Gamepad"));
    QCOMPARE(pad->bindings.value(QStringLiteral("a")), QStringList{QStringLiteral("pad:b")});  // east button = NDS A
    const auto kb = p.find(QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
    QVERIFY(kb && kb->builtin);
    QCOMPARE(kb->name, QStringLiteral("Keyboard · Standard"));
    QCOMPARE(kb->bindings.value(QStringLiteral("a")), QStringList{keyToken(Qt::Key_X)});
    // read-only
    const QString id = pad->id;
    QVERIFY(!p.rename(id, QStringLiteral("x")));
    QVERIFY(!p.remove(id));
    QVERIFY(!p.setBinding(id, QStringLiteral("a"), {}));
    QVERIFY(!p.resetToDefault(id));
    QVERIFY(!QFile::exists(p.filePath()));  // nothing written until something changes
    // Every FrameBeam input has a default in both built-in profiles.
    for (const InputDef& in : frameBeamInputs()) {
      QVERIFY2(pad->bindings.contains(in.id), qPrintable(in.id));
      QVERIFY2(kb->bindings.contains(in.id), qPrintable(in.id));
    }
  }

  void roundTrip() {
    QTemporaryDir dir;
    QString id;
    {
      ControllerProfiles p(dir.path());
      id = p.duplicate(QString::fromLatin1(ControllerProfiles::kBuiltinGamepadId), QStringLiteral("Mine"));
      QVERIFY(!id.isEmpty());
      QVERIFY(p.setBinding(id, QStringLiteral("a"), {padToken(QStringLiteral("x")), padToken(QStringLiteral("lefttrigger"))}));
      QVERIFY(p.setBinding(id, QStringLiteral("b"), {}));  // not mapped
      QVERIFY(p.assign(QStringLiteral("guid-1"), id));
      QVERIFY(p.assign(QString::fromLatin1(ControllerProfiles::kKeyboardDevice), QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId)));
    }
    ControllerProfiles p(dir.path());
    QCOMPARE(p.all().size(), 3);
    const auto mine = p.find(id);
    QVERIFY(mine && !mine->builtin);
    QCOMPARE(mine->name, QStringLiteral("Mine"));
    QCOMPARE(mine->kind, QStringLiteral("gamepad"));
    QCOMPARE(mine->bindings.value(QStringLiteral("a")), (QStringList{QStringLiteral("pad:x"), QStringLiteral("pad:lefttrigger")}));
    QVERIFY(mine->bindings.value(QStringLiteral("b")).isEmpty());
    QVERIFY(mine->bindings.contains(QStringLiteral("b")));
    QCOMPARE(p.assignedProfileId(QStringLiteral("guid-1"), QStringLiteral("gamepad")), id);
    // Unassigned device -> built-in of its kind.
    QCOMPARE(p.assignedProfileId(QStringLiteral("other"), QStringLiteral("gamepad")), QString::fromLatin1(ControllerProfiles::kBuiltinGamepadId));
    // A profile of the wrong kind is not used for a device.
    QCOMPARE(p.assignedProfileId(QStringLiteral("guid-1"), QStringLiteral("keyboard")), QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
  }

  void renameResetDelete() {
    QTemporaryDir dir;
    ControllerProfiles p(dir.path());
    const QString id = p.duplicate(QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
    QCOMPARE(p.find(id)->name, QStringLiteral("Keyboard · Standard copy"));
    const QString id2 = p.duplicate(QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
    QCOMPARE(p.find(id2)->name, QStringLiteral("Keyboard · Standard copy 2"));  // unique names
    QVERIFY(p.rename(id, QStringLiteral("  WASD  ")));
    QCOMPARE(p.find(id)->name, QStringLiteral("WASD"));
    QVERIFY(!p.rename(id, QStringLiteral("   ")));
    QVERIFY(p.setBinding(id, QStringLiteral("a"), {keyToken(Qt::Key_J)}));
    QVERIFY(p.resetToDefault(id));
    QCOMPARE(p.find(id)->bindings, ControllerProfiles::builtinProfile(QStringLiteral("keyboard")).bindings);
    QVERIFY(p.assign(QString::fromLatin1(ControllerProfiles::kKeyboardDevice), id));
    QVERIFY(p.remove(id));
    QVERIFY(!p.find(id));
    QCOMPARE(p.assignedProfileId(QString::fromLatin1(ControllerProfiles::kKeyboardDevice), QStringLiteral("keyboard")),
             QString::fromLatin1(ControllerProfiles::kBuiltinKeyboardId));
  }

  void tokens() {
    QCOMPARE(keyFromToken(keyToken(Qt::Key_X)).value_or(-1), static_cast<int>(Qt::Key_X));
    QVERIFY(!keyFromToken(QStringLiteral("pad:a")));
    QVERIFY(isPadToken(padToken(QStringLiteral("a"))));
    QCOMPARE(tokenLabel(padToken(QStringLiteral("leftshoulder"))), QStringLiteral("LB"));
    QCOMPARE(inputIndex(QStringLiteral("start")), 6);
    QCOMPARE(inputIndex(QStringLiteral("lid")), -1);
  }

  void corruptedFileYieldsBuiltinOnly() {
    QTemporaryDir dir;
    QDir().mkpath(dir.path() + QStringLiteral("/settings"));
    QFile f(dir.path() + QStringLiteral("/settings/controllers.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[[[");
    f.close();
    QCOMPARE(ControllerProfiles(dir.path()).all().size(), 2);
  }
};

QTEST_APPLESS_MAIN(ControllerProfilesTest)
#include "controllerprofiles_test.moc"
