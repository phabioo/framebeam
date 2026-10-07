#include <QtTest>
#include <string>
#include <vector>

#include "cmdline.h"

using framebeam::launcher::argumentsOf;
using framebeam::launcher::childCommandLine;

namespace {
// CommandLineToArgvW rules (backslashes only matter before quotes; quotes toggle; "" inside quotes = literal quote).
std::vector<std::wstring> argvOf(const std::wstring& s) {
  std::vector<std::wstring> out;
  size_t i = 0;
  const size_t n = s.size();
  while (true) {
    while (i < n && (s[i] == L' ' || s[i] == L'\t')) ++i;
    if (i >= n) break;
    std::wstring cur;
    bool inQuote = false;
    while (i < n && (inQuote || (s[i] != L' ' && s[i] != L'\t'))) {
      size_t bs = 0;
      while (i < n && s[i] == L'\\') { ++bs; ++i; }
      if (i < n && s[i] == L'"') {
        cur.append(bs / 2, L'\\');
        if (bs % 2 == 1) {
          cur += L'"';
        } else if (inQuote && i + 1 < n && s[i + 1] == L'"') {
          cur += L'"';
          ++i;
        } else {
          inQuote = !inQuote;
        }
        ++i;
      } else {
        cur.append(bs, L'\\');
        if (i < n && (inQuote || (s[i] != L' ' && s[i] != L'\t'))) cur += s[i++];
      }
    }
    out.push_back(cur);
  }
  return out;
}
}  // namespace

class CmdlineTest : public QObject {
  Q_OBJECT
 private slots:
  void versionArgument() {
    QVERIFY(argumentsOf(L"C:\\App\\framebeam_player.exe --version") == L"--version");
    QVERIFY(childCommandLine(L"C:\\App\\bin\\framebeam_player.exe", L"C:\\App\\framebeam_player.exe --version") ==
            L"\"C:\\App\\bin\\framebeam_player.exe\" --version");
  }
  void noArguments() {
    QVERIFY(argumentsOf(L"framebeam_player.exe").empty());
    QVERIFY(argumentsOf(L"\"C:\\Program Files\\FrameBeam\\framebeam_player.exe\"").empty());
    QVERIFY(argumentsOf(L"\"C:\\x y\\p.exe\"   ").empty());
    QVERIFY(childCommandLine(L"C:\\a\\bin\\p.exe", L"p.exe") == L"\"C:\\a\\bin\\p.exe\"");
    QVERIFY(argumentsOf(L"").empty());
  }
  void quotedProgramName() {
    QVERIFY(argumentsOf(L"\"C:\\Program Files\\FrameBeam\\framebeam_player.exe\" --data-dir \"D:\\my data\"") ==
            L"--data-dir \"D:\\my data\"");
    QVERIFY(argumentsOf(L"\"C:\\p.exe\"--x") == L"--x");  // no blank after the closing quote
  }
  void tabsAndSpacesBetween() { QVERIFY(argumentsOf(L"p.exe \t  a  b") == L"a  b"); }
  void argumentsSurviveVerbatim() {
    const std::wstring args =
        L"--data-dir \"C:\\path with spaces\" \"a \\\"quoted\\\" word\" back\\\\slash \"trail\\\\\" \"\" x";
    const std::wstring original = L"\"C:\\Program Files\\FrameBeam\\framebeam_player.exe\" " + args;
    const std::wstring child = childCommandLine(L"C:\\Program Files\\FrameBeam\\bin\\framebeam_player.exe", original);
    auto a = argvOf(original);
    auto b = argvOf(child);
    QVERIFY(!a.empty() && !b.empty());
    QVERIFY(b[0] == L"C:\\Program Files\\FrameBeam\\bin\\framebeam_player.exe");
    a.erase(a.begin());
    b.erase(b.begin());
    QVERIFY(a == b);
    bool found = false;
    for (const auto& x : a) found = found || x == L"back\\\\slash";
    QVERIFY(found);
  }
};

QTEST_GUILESS_MAIN(CmdlineTest)
#include "cmdline_test.moc"
