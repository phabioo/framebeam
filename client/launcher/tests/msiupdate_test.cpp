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
    QVERIFY(msiexecParameters(L"C:/u/a b/x.msi", L"machine") == L"/i \"C:\\u\\a b\\x.msi\" /qn /norestart ALLUSERS=1");
  }
  void parsesVerificationOptions() {
    const std::wstring sha(64, L'a');
    MsiUpdateRequest r;
    QVERIFY(parseMsiUpdate({L"l.exe", L"--apply-msi-update", L"--sha256", sha, L"--size", L"123456", L"C:\\t\\fb.msi", L"machine",
                            L"C:\\p\\framebeam_player.exe", L"--x"},
                           &r));
    QVERIFY(r.sha256 == sha);
    QVERIFY(r.hasSize && r.size == 123456ULL);
    QVERIFY(r.msi == L"C:\\t\\fb.msi");
    QVERIFY(r.scope == L"machine");
    QVERIFY(r.relaunchExe == L"C:\\p\\framebeam_player.exe");
    QCOMPARE(r.relaunchArgs.size(), size_t(1));
    QVERIFY(!parseMsiUpdate({L"l.exe", L"--apply-msi-update", L"--size", L"12x", L"a.msi", L"user", L"p.exe"}, &r));
    QVERIFY(!parseMsiUpdate({L"l.exe", L"--apply-msi-update", L"--sha256", sha, L"a.msi", L"user"}, &r));  // incomplete
  }
  void reverificationDecision() {
    const std::wstring sha = L"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    MsiUpdateRequest r;
    r.msi = L"a.msi";
    r.scope = L"machine";
    // elevated scope without an expectation: the launcher must refuse
    QVERIFY(verificationRequired(r));
    QVERIFY(!msiMatches(r, 10, sha));
    r.sha256 = sha;
    r.size = 10;
    r.hasSize = true;
    QVERIFY(msiMatches(r, 10, sha));
    std::wstring upper = sha;
    for (wchar_t& c : upper) {
      if (c >= L'a' && c <= L'f') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    QVERIFY(msiMatches(r, 10, upper));                       // case-insensitive digest
    QVERIFY(!msiMatches(r, 11, sha));                        // size differs
    QVERIFY(!msiMatches(r, 10, std::wstring(64, L'0')));     // swapped file
    QVERIFY(!msiMatches(r, 10, L""));                        // unreadable
    r.sha256 = L"short";
    QVERIFY(!msiMatches(r, 10, L"short"));                   // malformed expectation never matches
    // per-user scope without any expectation is not verified (old callers); a given hash always is
    MsiUpdateRequest u;
    u.scope = L"user";
    QVERIFY(!verificationRequired(u));
    u.sha256 = sha;
    QVERIFY(verificationRequired(u));
  }
  void setupFolderCleanup() {
    MsiUpdateRequest r;
    r.scope = L"setup-hub";
    r.msi = L"C:\\Users\\a\\AppData\\Local\\Temp\\framebeam-hub-setup-0123abcd\\FrameBeam.msi";
    QVERIFY(setupDirToRemove(r) == L"C:\\Users\\a\\AppData\\Local\\Temp\\framebeam-hub-setup-0123abcd");
    r.scope = L"machine";  // a staged update MSI stays in the Player's cache
    QVERIFY(setupDirToRemove(r).empty());
    r.scope = L"setup-hub";
    r.msi = L"C:\\Users\\a\\Downloads\\FrameBeam.msi";  // not our folder: never removed
    QVERIFY(setupDirToRemove(r).empty());
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
