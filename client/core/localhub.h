#pragma once
// The Hub on this PC (0.9 "One installer"): detection, loopback address rules and elevated helper commands.
// The MSI registers the Hub under HKLM\Software\FrameBeam\Hub (`Port` DWORD, `InstallDir` string). For tests and
// development FRAMEBEAM_LOCAL_HUB_PORT (and FRAMEBEAM_LOCAL_HUB_DIR) replace the registry on every OS.

#include <QString>
#include <QStringList>
#include <optional>

namespace framebeam::localhub {

struct Info {
  int port = 0;        // 0 = no local Hub registered
  QString installDir;  // folder of framebeam-hub.exe (may be empty)
  bool present() const { return port > 0; }
};

// "8443" -> 8443; anything that is not a TCP port (1..65535) -> nullopt.
std::optional<int> parsePort(const QString& text);
// Environment override first, then (Windows only) the 64-bit registry view of HKLM.
Info detect();
// The Player can install a Hub from the MSI (Windows only).
bool installSupported();
// https://127.0.0.1:<port>
QString hubUrl(int port);
// The address (as stored in a profile) is exactly the loopback Hub of this PC. Only this address gets the automatic
// trust of its certificate.
bool isLocalHubAddress(const QString& address, int port);
QString hubExe(const QString& installDir);

struct Command {
  QString program;
  QStringList args;
};
// framebeam-hub.exe grant-folder <path> (run elevated).
Command grantFolderCommand(const QString& installDir, const QString& folder);
// Runs a program elevated (UAC) and waits for it. Windows only; returns the exit code, -1 when it could not be
// started or the user declined (error set).
int runElevatedAndWait(const QString& program, const QStringList& args, QString* error = nullptr);

}  // namespace framebeam::localhub
