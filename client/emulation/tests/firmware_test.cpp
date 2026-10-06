// Firmware materialization and core options (dummy bytes, no real BIOS/firmware).
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "firmware_materializer.h"
#include "system_manifest.h"

using namespace framebeam::emu;

namespace {
QString writeFile(const QString& path, const QByteArray& d) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  f.open(QIODevice::WriteOnly);
  f.write(d);
  return path;
}
QByteArray readFile(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

class FirmwareTest : public QObject {
  Q_OBJECT
 private slots:
  void ndsManifestDeclaresFiles() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const FirmwareSpec& fw = reg.find(QStringLiteral("nds"))->firmware;
    QCOMPARE(fw.sysfileOption, QStringLiteral("melonds_sysfile_mode"));
    QCOMPARE(fw.sysfileNative, QStringLiteral("native"));
    QCOMPARE(fw.sysfileBuiltin, QStringLiteral("builtin"));
    QVERIFY(fw.fileById(QStringLiteral("bios7")));
    QCOMPARE(fw.fileById(QStringLiteral("bios7"))->name, QStringLiteral("bios7.bin"));
    QCOMPARE(fw.fileById(QStringLiteral("bios9"))->name, QStringLiteral("bios9.bin"));
    QCOMPARE(fw.fileById(QStringLiteral("firmware"))->name, QStringLiteral("firmware.bin"));
    QCOMPARE(fw.fileById(QStringLiteral("firmware"))->coreOption, QStringLiteral("melonds_firmware_nds_path"));
    QVERIFY(!fw.fileById(QStringLiteral("nope")));
  }

  void rejectsPathsInFileNames() {
    QString err;
    const QByteArray base =
        R"({"system_id":"x","display_name":"X","core_id":"c","core_library_basename":"c","extensions":[".x"],
            "display":{"screens":[{"width":1,"height":1}]},"firmware":{"files":[{"name":"%1"}]}})";
    QVERIFY(!ManifestRegistry::parse(QByteArray(base).replace("%1", "../evil.bin"), &err));
    QVERIFY(!ManifestRegistry::parse(QByteArray(base).replace("%1", "a/b.bin"), &err));
    const auto ok = ManifestRegistry::parse(QByteArray(base).replace("%1", "bios.bin"), &err);
    QVERIFY(ok);
    QCOMPARE(ok->firmware.files.first().id, QStringLiteral("bios"));  // default id = name without extension
  }

  void materializesUnderManifestNames() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const FirmwareSpec& fw = reg.find(QStringLiteral("nds"))->firmware;
    QTemporaryDir dir;
    const QString sys = dir.filePath(QStringLiteral("system"));
    const QMap<QString, QString> files{
        {QStringLiteral("bios7"), writeFile(dir.filePath(QStringLiteral("cache/aaa")), QByteArray(16, 'a'))},
        {QStringLiteral("bios9"), writeFile(dir.filePath(QStringLiteral("cache/bbb")), QByteArray(8, 'b'))},
        {QStringLiteral("firmware"), writeFile(dir.filePath(QStringLiteral("cache/ccc")), QByteArray(32, 'c'))}};
    QStringList ids;
    QString err;
    QVERIFY2(materializeFirmware(fw, files, sys, &ids, &err), qPrintable(err));
    QCOMPARE(ids.size(), 3);
    QCOMPARE(readFile(sys + QStringLiteral("/bios7.bin")), QByteArray(16, 'a'));
    QCOMPARE(readFile(sys + QStringLiteral("/bios9.bin")), QByteArray(8, 'b'));
    QCOMPARE(readFile(sys + QStringLiteral("/firmware.bin")), QByteArray(32, 'c'));

    // Replaced content (other Hub / replaced file) is overwritten, identical content is idempotent.
    writeFile(dir.filePath(QStringLiteral("cache/aaa")), QByteArray(16, 'z'));
    QVERIFY(materializeFirmware(fw, files, sys, &ids, &err));
    QCOMPARE(readFile(sys + QStringLiteral("/bios7.bin")), QByteArray(16, 'z'));
  }

  void unknownIdOrMissingSourceFails() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const FirmwareSpec& fw = reg.find(QStringLiteral("nds"))->firmware;
    QTemporaryDir dir;
    QString err;
    QVERIFY(!materializeFirmware(fw, {{QStringLiteral("zzz"), QStringLiteral("/x")}}, dir.path(), nullptr, &err));
    QVERIFY(err.contains(QStringLiteral("zzz")));
    QVERIFY(!materializeFirmware(fw, {{QStringLiteral("bios7"), dir.filePath(QStringLiteral("missing"))}}, dir.path(), nullptr, &err));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("bios7.bin"))));
  }

  void coreOptionsByMode() {
    ManifestRegistry reg;
    QVERIFY(reg.loadBuiltin());
    const FirmwareSpec& fw = reg.find(QStringLiteral("nds"))->firmware;
    const auto builtin = firmwareCoreOptions(fw, false, {QStringLiteral("bios7"), QStringLiteral("firmware")});
    QCOMPARE(builtin.size(), 1);
    QCOMPARE(builtin.value(QStringLiteral("melonds_sysfile_mode")), QStringLiteral("builtin"));
    const auto native = firmwareCoreOptions(fw, true, {QStringLiteral("bios7"), QStringLiteral("bios9"), QStringLiteral("firmware")});
    QCOMPARE(native.value(QStringLiteral("melonds_sysfile_mode")), QStringLiteral("native"));
    QCOMPARE(native.value(QStringLiteral("melonds_firmware_nds_path")), QStringLiteral("firmware.bin"));
    const auto noFw = firmwareCoreOptions(fw, true, {QStringLiteral("bios7")});
    QVERIFY(!noFw.contains(QStringLiteral("melonds_firmware_nds_path")));
    FirmwareSpec none;
    QVERIFY(firmwareCoreOptions(none, true, {}).isEmpty());
  }
};

QTEST_GUILESS_MAIN(FirmwareTest)
#include "firmware_test.moc"
