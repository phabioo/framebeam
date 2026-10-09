// FrameBeam Player launcher (Windows): {app}\framebeam_player.exe starts {app}\bin\framebeam_player.exe with the
// same arguments, passes its std handles on, waits and returns the exit code of the child.
// Deliberately tiny: no Qt, no C++ runtime features beyond the static CRT.
#include <windows.h>
#include <shobjidl.h>

#include <cwchar>
#include <string>
#include <vector>

#include "cmdline.h"
#include "msiupdate.h"

namespace {

// --apply-msi-update (temporary copy of the launcher, see msiupdate.h): wait until no Player instance runs, run
// msiexec (elevated unless per-user), start the Player again, delete this copy.
int applyMsiUpdate(const framebeam::launcher::MsiUpdateRequest& req) {
  using namespace framebeam::launcher;
  // Every Player instance holds the named mutex (app/main.cpp); max. 60 s.
  for (int i = 0; i < 120; ++i) {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"FrameBeamPlayer");
    if (m == nullptr) break;
    CloseHandle(m);
    Sleep(500);
  }
  DWORD code = 1;
  const std::wstring params = msiexecParameters(req.msi, req.scope);
  wchar_t sys[MAX_PATH] = {};
  if (!params.empty() && GetSystemDirectoryW(sys, MAX_PATH) > 0) {
    const std::wstring msiexec = std::wstring(sys) + L"\\msiexec.exe";
    HANDLE proc = nullptr;
    if (scopeNeedsElevation(req.scope)) {
      SHELLEXECUTEINFOW sei = {};
      sei.cbSize = sizeof(sei);
      sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
      sei.lpVerb = L"runas";
      sei.lpFile = msiexec.c_str();
      sei.lpParameters = params.c_str();
      sei.nShow = SW_HIDE;
      if (ShellExecuteExW(&sei)) proc = sei.hProcess;
    } else {
      std::wstring cmd = L"\"" + msiexec + L"\" " + params;
      STARTUPINFOW si = {};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi = {};
      if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        proc = pi.hProcess;
      }
    }
    if (proc != nullptr) {
      WaitForSingleObject(proc, INFINITE);
      GetExitCodeProcess(proc, &code);
      CloseHandle(proc);
    }
  }
  if (code != 0 && code != 3010) {  // 3010 = success, restart required
    wchar_t msg[256];
    _snwprintf(msg, sizeof(msg) / sizeof(msg[0]), L"The FrameBeam update could not be installed (code %lu).", code);
    MessageBoxW(nullptr, msg, L"FrameBeam Player", MB_OK | MB_ICONERROR);
  }
  // Start the Player again (the installed one after success, otherwise the previous one).
  std::vector<std::wstring> relaunch;
  relaunch.push_back(req.relaunchExe);
  relaunch.insert(relaunch.end(), req.relaunchArgs.begin(), req.relaunchArgs.end());
  std::wstring cmd = quoteArgs(relaunch);
  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = {};
  if (CreateProcessW(req.relaunchExe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  // Delete this temporary copy and its folder a moment after exit.
  wchar_t self[MAX_PATH * 4] = {};
  const DWORD n = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(sizeof(self) / sizeof(self[0])));
  if (n > 0 && n < sizeof(self) / sizeof(self[0])) {
    std::wstring dir(self, n);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    std::wstring clean = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & del /f /q \"" + std::wstring(self, n) + L"\" & rmdir \"" + dir + L"\"";
    STARTUPINFOW si2 = {};
    si2.cb = sizeof(si2);
    PROCESS_INFORMATION pi2 = {};
    if (CreateProcessW(nullptr, &clean[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si2, &pi2)) {
      CloseHandle(pi2.hThread);
      CloseHandle(pi2.hProcess);
    }
  }
  return static_cast<int>(code == 3010 ? 0 : code);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  SetCurrentProcessExplicitAppUserModelID(L"FrameBeam.Player");

  {
    int argc = 0;
    LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvw != nullptr) {
      framebeam::launcher::MsiUpdateRequest req;
      const bool msi = framebeam::launcher::parseMsiUpdate(std::vector<std::wstring>(argvw, argvw + argc), &req);
      LocalFree(argvw);
      if (msi) return applyMsiUpdate(req);
    }
  }

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
  if (!usable(out) && !usable(err) && AttachConsole(ATTACH_PARENT_PROCESS)) {
    // Started from a terminal without redirection: borrow its console (no new window) for the child's output.
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);
    err = out;
  }
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
