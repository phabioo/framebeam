// FrameBeam Player launcher (Windows): {app}\framebeam_player.exe starts {app}\bin\framebeam_player.exe with the
// same arguments, passes its std handles on, waits and returns the exit code of the child.
// Deliberately tiny: no Qt, no C++ runtime features beyond the static CRT.
#include <windows.h>
#include <shobjidl.h>

#include <cwchar>
#include <string>

#include "cmdline.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  SetCurrentProcessExplicitAppUserModelID(L"FrameBeam.Player");

  wchar_t self[MAX_PATH * 4] = {};
  const DWORD n = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(sizeof(self) / sizeof(self[0])));
  if (n == 0 || n >= sizeof(self) / sizeof(self[0])) return 1;
  std::wstring dir(self, n);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return 1;
  dir.resize(slash);

  const std::wstring child = dir + L"\\bin\\framebeam_player.exe";
  std::wstring cmd = framebeam::launcher::childCommandLine(child, GetCommandLineW());

  // GUI subsystem (no console window, like the Player itself): hand our std handles to the child explicitly,
  // so a redirected stdout (e.g. `--version-json` captured by CI) reaches the caller. Only valid handles.
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  const auto usable = [](HANDLE h) { return h != nullptr && h != INVALID_HANDLE_VALUE; };
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (usable(in) || usable(out) || usable(err)) {
    for (HANDLE h : {in, out, err}) {
      if (usable(h)) SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = usable(in) ? in : nullptr;
    si.hStdOutput = usable(out) ? out : nullptr;
    si.hStdError = usable(err) ? err : nullptr;
  }
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(child.c_str(), &cmd[0], nullptr, nullptr, TRUE /* inherit handles */, 0, nullptr,
                      nullptr /* keep the caller's working directory */, &si, &pi)) {
    const DWORD code = GetLastError();
    wchar_t msg[512];
    _snwprintf(msg, sizeof(msg) / sizeof(msg[0]), L"FrameBeam Player could not be started (%lu):\n%ls", code, child.c_str());
    MessageBoxW(nullptr, msg, L"FrameBeam Player", MB_OK | MB_ICONERROR);
    return 1;
  }
  CloseHandle(pi.hThread);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}
