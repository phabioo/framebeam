// FrameBeam Player launcher (Windows): {app}\framebeam_player.exe starts {app}\bin\framebeam_player.exe with the
// same arguments, inherits the std handles, waits and returns the exit code of the child.
// Deliberately tiny: no Qt, no C++ runtime features beyond the static CRT.
#include <windows.h>
#include <shobjidl.h>

#include <cwchar>
#include <string>

namespace {

// Skips the program name (first token, optionally quoted) of a command line and returns the rest.
const wchar_t* argumentsOf(const wchar_t* cmd) {
  const wchar_t* p = cmd;
  if (*p == L'"') {
    ++p;
    while (*p != L'\0' && *p != L'"') ++p;
    if (*p == L'"') ++p;
  } else {
    while (*p != L'\0' && *p != L' ' && *p != L'\t') ++p;
  }
  while (*p == L' ' || *p == L'\t') ++p;
  return p;
}

}  // namespace

int wmain() {
  SetCurrentProcessExplicitAppUserModelID(L"FrameBeam.Player");

  wchar_t self[MAX_PATH * 4] = {};
  const DWORD n = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(sizeof(self) / sizeof(self[0])));
  if (n == 0 || n >= sizeof(self) / sizeof(self[0])) return 1;
  std::wstring dir(self, n);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return 1;
  dir.resize(slash);

  const std::wstring child = dir + L"\\bin\\framebeam_player.exe";
  std::wstring cmd = L"\"" + child + L"\"";
  const wchar_t* rest = argumentsOf(GetCommandLineW());
  if (*rest != L'\0') {
    cmd += L" ";
    cmd += rest;
  }

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(child.c_str(), &cmd[0], nullptr, nullptr, TRUE /* inherit std handles */, 0, nullptr,
                      (dir + L"\\bin").c_str(), &si, &pi)) {
    const DWORD err = GetLastError();
    wchar_t msg[512];
    _snwprintf(msg, sizeof(msg) / sizeof(msg[0]), L"FrameBeam Player could not be started (%lu): %ls\n", err, child.c_str());
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
      WriteConsoleW(h, msg, static_cast<DWORD>(wcslen(msg)), &written, nullptr);
    }
    return 1;
  }
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}
