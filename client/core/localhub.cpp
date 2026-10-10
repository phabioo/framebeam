#include "localhub.h"

#include <QDir>
#include <QSettings>
#include <QUrl>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace framebeam::localhub {

std::optional<int> parsePort(const QString& text) {
  bool ok = false;
  const int v = text.trimmed().toInt(&ok);
  if (!ok || v < 1 || v > 65535) return std::nullopt;
  return v;
}

Info detect() {
  Info info;
  const QByteArray env = qgetenv("FRAMEBEAM_LOCAL_HUB_PORT");
  if (!env.isEmpty()) {
    if (const auto p = parsePort(QString::fromLocal8Bit(env))) info.port = *p;
    info.installDir = qEnvironmentVariable("FRAMEBEAM_LOCAL_HUB_DIR");
    return info;
  }
#ifdef Q_OS_WIN
  QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\FrameBeam\\Hub"), QSettings::Registry64Format);
  if (const auto p = parsePort(reg.value(QStringLiteral("Port")).toString())) info.port = *p;
  info.installDir = reg.value(QStringLiteral("InstallDir")).toString();
#endif
  return info;
}

bool installSupported() {
#ifdef Q_OS_WIN
  return true;
#else
  return false;
#endif
}

QString hubUrl(int port) { return QStringLiteral("https://127.0.0.1:%1").arg(port); }

bool isLocalHubAddress(const QString& address, int port) {
  if (port <= 0) return false;
  const QUrl u(address, QUrl::StrictMode);
  return u.isValid() && u.scheme() == QLatin1String("https") && u.host() == QLatin1String("127.0.0.1") && u.port() == port &&
         (u.path().isEmpty() || u.path() == QLatin1String("/"));
}

QString hubExe(const QString& installDir) {
  return installDir.isEmpty() ? QString() : QDir(installDir).filePath(QStringLiteral("framebeam-hub.exe"));
}

Command grantFolderCommand(const QString& installDir, const QString& folder) {
  return {hubExe(installDir), {QStringLiteral("grant-folder"), folder}};
}

int runElevatedAndWait(const QString& program, const QStringList& args, QString* error) {
#ifdef Q_OS_WIN
  std::wstring params;
  for (const QString& a : args) {
    if (!params.empty()) params += L' ';
    std::wstring w = a.toStdWString();
    // Quote; a trailing backslash would escape the closing quote, so double it.
    size_t bs = 0;
    while (bs < w.size() && w[w.size() - 1 - bs] == L'\\') ++bs;
    params += L'"' + w + std::wstring(bs, L'\\') + L'"';
  }
  const std::wstring file = QDir::toNativeSeparators(program).toStdWString();
  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  sei.lpVerb = L"runas";
  sei.lpFile = file.c_str();
  sei.lpParameters = params.c_str();
  sei.nShow = SW_HIDE;
  if (!ShellExecuteExW(&sei) || sei.hProcess == nullptr) {
    if (error != nullptr) *error = QStringLiteral("The administrator prompt was declined or the program could not be started.");
    return -1;
  }
  WaitForSingleObject(sei.hProcess, 60000);
  DWORD code = 1;
  GetExitCodeProcess(sei.hProcess, &code);
  CloseHandle(sei.hProcess);
  return static_cast<int>(code);
#else
  Q_UNUSED(program);
  Q_UNUSED(args);
  if (error != nullptr) *error = QStringLiteral("Not supported on this platform.");
  return -1;
#endif
}

}  // namespace framebeam::localhub
