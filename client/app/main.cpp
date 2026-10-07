#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtQuickControls2/QQuickStyle>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#endif

#include "filelog.h"
#include "playercontroller.h"
#include "version.h"

namespace {

std::atomic<int> g_qmlWarnings{0};
QtMessageHandler g_previousHandler = nullptr;

// Smoke test: count QML warnings while loading (failure), print everything else unchanged.
void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  if (type == QtWarningMsg || type == QtCriticalMsg) {
    const bool qml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:/")) ||
                     (ctx.category != nullptr && QByteArrayView(ctx.category).startsWith("qt.qml"));
    if (qml) {
      ++g_qmlWarnings;
    }
  }
  if (g_previousHandler != nullptr) {
    g_previousHandler(type, ctx, msg);
  }
}

// `--version-json`: one JSON line on stdout, before any Qt/GUI initialisation. On Windows the line goes to the
// inherited stdout handle (CI redirects it to a file) and falls back to the parent console.
void printToStdout(const std::string& text) {
#ifdef Q_OS_WIN
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == nullptr || h == INVALID_HANDLE_VALUE) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
      h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
  }
  if (h != nullptr && h != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    FlushFileBuffers(h);  // fails harmlessly on consoles
    return;
  }
#endif
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);
}

#ifdef Q_OS_WIN
// Named mutex: every Player instance holds a handle; the installer (/UPDATE) waits until no process holds it.
// Instances are not limited to one.
HANDLE g_instanceMutex = nullptr;
void holdInstanceMutex() { g_instanceMutex = CreateMutexW(nullptr, FALSE, L"FrameBeamPlayer"); }
#endif

}  // namespace

int main(int argc, char* argv[]) {
  // Info options are answered here, before QGuiApplication, the mutex, updates and the data directory.
  // QCommandLineParser must not see --version/--help: on Windows a GUI application shows them in a message box
  // that blocks forever when nobody clicks (CI package smoke check).
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version-json") == 0) {
      printToStdout(framebeam::playerVersionJson() + "\n");
      return 0;
    }
    if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
      printToStdout("FrameBeam Player " + std::string(framebeam::playerVersion()) + "\n");
      return 0;
    }
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "-?") == 0) {
      printToStdout(
          "Usage: framebeam_player [options]\n"
          "  --version            Print the version and exit.\n"
          "  --version-json       Print version, channel, commit and protocol versions as JSON and exit.\n"
          "  --smoke-test         Load QML and exit with code 0 (code 1 on QML warnings).\n"
          "  --data-dir <path>    Data directory (profiles, cache, saves) instead of the default.\n"
          "  --dev-allow-http     Development only: also allow HTTP hubs outside localhost.\n");
      return 0;
    }
  }
#ifdef Q_OS_WIN
  SetCurrentProcessExplicitAppUserModelID(L"FrameBeam.Player");  // same id as the installer shortcuts
#endif
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("FrameBeam Player"));
  QGuiApplication::setApplicationVersion(QString::fromUtf8(framebeam::playerVersion().data(),
                                                           static_cast<qsizetype>(framebeam::playerVersion().size())));

  QCommandLineParser parser;
  const QCommandLineOption smoke(QStringLiteral("smoke-test"),
                                 QStringLiteral("Load QML and exit with code 0 (code 1 on QML warnings)."));
  const QCommandLineOption dataDir(QStringLiteral("data-dir"),
                                   QStringLiteral("Data directory (profiles, cache, saves) instead of AppData."),
                                   QStringLiteral("path"));
  const QCommandLineOption devHttp(QStringLiteral("dev-allow-http"),
                                   QStringLiteral("Development only: also allow HTTP hubs outside localhost."));
  parser.addOption(smoke);
  parser.addOption(dataDir);
  parser.addOption(devHttp);
  parser.process(app);
  const bool smokeMode = parser.isSet(smoke);
#ifdef Q_OS_WIN
  if (!smokeMode) {
    holdInstanceMutex();
  }
#endif

  QTemporaryDir smokeDir;  // smoke test without --data-dir does not touch real user data
  framebeam::ui::PlayerController::Options opts;
  opts.dataDir = parser.value(dataDir);
  opts.allowHttp = parser.isSet(devHttp);
  opts.enableUpdates = !smokeMode;
  if (smokeMode) {
    if (opts.dataDir.isEmpty() && smokeDir.isValid()) {
      opts.dataDir = smokeDir.path();
    }
    opts.memoryCredentials = true;
    opts.probeCoreVersions = false;
    g_previousHandler = qInstallMessageHandler(messageHandler);
  }

  QQuickStyle::setStyle(QStringLiteral("Basic"));
  // Log file <data>/logs/player.log (the GUI exe has no console). Lines before the data directory is known are
  // buffered. Not in smoke mode (temporary data directory).
  if (!smokeMode) {
    framebeam::filelog::install();
  }
  framebeam::ui::PlayerController controller(opts);
  if (!smokeMode) {
    framebeam::filelog::setDirectory(controller.profileStore()->baseDir());
  }
  // Beta channel + automatic install: a verified staged installer from the last run is applied before the
  // main window appears (never during a game: none can be running yet). The installer waits for this process.
  if (!smokeMode && controller.updates()->manager()->applyStagedAtStart()) {
    return 0;
  }

  QQmlApplicationEngine engine;
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
  engine.setInitialProperties({{QStringLiteral("player"), QVariant::fromValue(&controller)}});
  engine.load(QUrl(QStringLiteral("qrc:/qt/qml/FrameBeam/Player/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  if (smokeMode) {
    QTimer::singleShot(400, &app, &QCoreApplication::quit);  // wait briefly: polish/bindings only run in the event loop
    const int rc = app.exec();
    return g_qmlWarnings.load() > 0 ? 1 : rc;
  }
  controller.startup();
  return app.exec();
}
