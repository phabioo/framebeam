#pragma once
// MSI update mode of the launcher (no Qt, header only, testable on any platform):
//   framebeam_player.exe --apply-msi-update <msi> <scope> <relaunchExe> [relaunchArgs...]
// The Player starts a temporary copy of its launcher in this mode and quits; the copy installs the MSI and starts the
// Player again. The msiexec arguments mirror framebeam::update::msiexecArguments (client/core/installroot.cpp).
#include <string>
#include <vector>

namespace framebeam::launcher {

struct MsiUpdateRequest {
  std::wstring msi;
  std::wstring scope;  // user | machine | machine-hub | setup-hub
  std::wstring relaunchExe;
  std::vector<std::wstring> relaunchArgs;
};

// argv as CommandLineToArgvW delivers it (argv[0] = program). False when this is not an MSI update call or the
// call is incomplete.
inline bool parseMsiUpdate(const std::vector<std::wstring>& argv, MsiUpdateRequest* out) {
  if (argv.size() < 5 || argv[1] != L"--apply-msi-update") return false;
  out->msi = argv[2];
  out->scope = argv[3];
  out->relaunchExe = argv[4];
  out->relaunchArgs.assign(argv.begin() + 5, argv.end());
  return !out->msi.empty() && !out->relaunchExe.empty();
}

inline bool isKnownScope(const std::wstring& scope) {
  return scope == L"user" || scope == L"machine" || scope == L"machine-hub" || scope == L"setup-hub";
}

inline bool scopeNeedsElevation(const std::wstring& scope) { return scope != L"user"; }

// Command line parameters for msiexec.exe (without the program name); empty for an unknown scope.
inline std::wstring msiexecParameters(const std::wstring& msi, const std::wstring& scope) {
  if (!isKnownScope(scope)) return {};
  std::wstring p = L"/i \"" + msi + L"\" /qn /norestart";
  if (scope == L"user") {
    p += L" MSIINSTALLPERUSER=1 ALLUSERS=2";
  } else {
    p += L" ALLUSERS=1";
    if (scope == L"machine-hub" || scope == L"setup-hub") p += L" INSTALL_HUB=1";
    if (scope == L"setup-hub") p += L" NETWORK_SHARING=0";
  }
  return p;
}

// "a" "b c" ... (every argument quoted; the arguments of the Player contain no quotes).
inline std::wstring quoteArgs(const std::vector<std::wstring>& args) {
  std::wstring s;
  for (const std::wstring& a : args) {
    if (!s.empty()) s += L' ';
    s += L'"' + a + L'"';
  }
  return s;
}

}  // namespace framebeam::launcher
