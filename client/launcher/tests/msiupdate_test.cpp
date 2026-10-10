#include <QtTest>
#include <string>
#include <vector>

#include "msiupdate.h"

using namespace framebeam::launcher;

class MsiUpdateTest : public QObject {
  Q_OBJECT
 private slots:
  void parsesRequest() {
    MsiUpdateRequest r;
    const std::vector<std::wstring> argv{L"l.exe", L"--apply-msi-update", L"C:\\t\\fb.msi", L"user", L"C:\\p\\framebeam_player.exe",
                                         L"--setup-local-hub"};
    QVERIFY(parseMsiUpdate(argv, &r));
    QVERIFY(r.msi == L"C:\\t\\fb.msi");
    QVERIFY(r.scope == L"user");
    QVERIFY(r.relaunchExe == L"C:\\p\\framebeam_player.exe");
    QCOMPARE(r.relaunchArgs.size(), size_t(1));
    QVERIFY(r.relaunchArgs[0] == L"--setup-local-hub");
  }
  void rejectsOtherCalls() {
    MsiUpdateRequest r;
    QVERIFY(!parseMsiUpdate({L"l.exe", L"--smoke-test"}, &r));
    QVERIFY(!parseMsiUpdate({L"l.exe", L"--apply-msi-update", L"a.msi", L"user"}, &r));  // incomplete
    QVERIFY(!parseMsiUpdate({L"l.exe"}, &r));
  }
  void msiexecParametersPerScope() {
    QVERIFY(msiexecParameters(L"a b.msi", L"user") == L"/i \"a b.msi\" /qn /norestart MSIINSTALLPERUSER=1 ALLUSERS=2");
    QVERIFY(msiexecParameters(L"a.msi", L"machine") == L"/i \"a.msi\" /qn /norestart ALLUSERS=1");
    QVERIFY(msiexecParameters(L"a.msi", L"machine-hub") == L"/i \"a.msi\" /qn /norestart ALLUSERS=1 INSTALL_HUB=1");
    QVERIFY(msiexecParameters(L"a.msi", L"setup-hub") ==
            L"/i \"a.msi\" /qn /norestart ALLUSERS=1 INSTALL_HUB=1 NETWORK_SHARING=0");
    QVERIFY(msiexecParameters(L"a.msi", L"bogus").empty());
  }
  void elevation() {
    QVERIFY(!scopeNeedsElevation(L"user"));
    QVERIFY(scopeNeedsElevation(L"machine"));
    QVERIFY(scopeNeedsElevation(L"setup-hub"));
  }
  void quoting() { QVERIFY(quoteArgs({L"C:\\a b\\p.exe", L"--x"}) == L"\"C:\\a b\\p.exe\" \"--x\""); }
};

QTEST_GUILESS_MAIN(MsiUpdateTest)
#include "msiupdate_test.moc"
