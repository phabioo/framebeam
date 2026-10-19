#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QSocketNotifier>
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
#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "diagbundle.h"
#include "filelog.h"
#include "pluginimageprovider.h"
#include "profilestore.h"
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

#ifdef Q_OS_UNIX
// SIGTERM / SIGHUP / SIGINT (kill, systemd stop, logout, Ctrl+C) take the normal quit path, so a running game's save is
// flushed and synced like after "Quit". The handler only writes a byte into a socket pair (async-signal-safe); a
// QSocketNotifier turns it into QCoreApplication::quit() on the GUI thread. Windows handles logoff through Qt.
int g_signalFd[2] = {-1, -1};

void onTerminationSignal(int) {
  const char byte = 1;
  const ssize_t ignored = ::write(g_signalFd[0], &byte, 1);
  (void)ignored;
}

void installTerminationSignals(QObject* parent) {
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) != 0) {
    return;
  }
  struct sigaction sa;
  sa.sa_handler = onTerminationSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  for (const int sig : {SIGTERM, SIGHUP, SIGINT}) {
    sigaction(sig, &sa, nullptr);
  }
  auto* notifier = new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, parent);
  QObject::connect(notifier, &QSocketNotifier::activated, parent, [notifier]() {
    char byte = 0;
    const ssize_t ignored = ::read(g_signalFd[1], &byte, 1);
    (void)ignored;
    notifier->setEnabled(false);  // one request is enough; further signals during shutdown are ignored
    qInfo() << "Termination signal received, quitting";
    QCoreApplication::quit();
  });
}
#endif

#ifdef Q_OS_WIN
// Named mutex: every Player instance holds a handle; the installer (/UPDATE) waits until no process holds it.
// Instances are not limited to one.
HANDLE g_instanceMutex = nullptr;
void holdInstanceMutex() { g_instanceMutex = CreateMutexW(nullptr, FALSE, L"FrameBeamPlayer"); }
#endif

// `--collect-logs <out.zip> [--data-dir <path>]`: writes the diagnostics bundle without the GUI (also when the Player
// does not start). Offline: reads the data directory only, never connects to a Hub. Refuses to overwrite a file.
int collectLogsCli(int argc, char* argv[], const QString& out) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("FrameBeam Player"));
  QString dataDir;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--data-dir") == 0) dataDir = QString::fromLocal8Bit(argv[i + 1]);
  }
  if (dataDir.isEmpty()) dataDir = framebeam::ProfileStore::defaultBaseDir();
  if (QFileInfo::exists(out)) {
    printToStdout("framebeam_player: " + out.toStdString() + " already exists, not overwriting\n");
    return 1;
  }
  QString err;
  if (!framebeam::diag::writeBundle(out, framebeam::diag::offlineInfo(dataDir), dataDir, {}, &err)) {
    printToStdout("framebeam_player: could not write the bundle: " + err.toStdString() + "\n");
    return 1;
  }
  printToStdout("Diagnostics bundle written to " + QDir::toNativeSeparators(out).toStdString() + "\n");
  return 0;
}

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
    if (std::strcmp(argv[i], "--collect-logs") == 0) {
      if (i + 1 >= argc) {
        printToStdout("framebeam_player: --collect-logs needs an output file (<out.zip>)\n");
        return 2;
      }
      return collectLogsCli(argc, argv, QString::fromLocal8Bit(argv[i + 1]));
    }
    if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "-?") == 0) {
      printToStdout(
          "Usage: framebeam_player [options]\n"
          "  --version            Print the version and exit.\n"
          "  --version-json       Print version, channel, commit and protocol versions as JSON and exit.\n"
          "  --smoke-test         Load QML and exit with code 0 (code 1 on QML warnings).\n"
          "  --collect-logs <out.zip>  Write the diagnostics bundle (logs, system info; no passwords or tokens) and exit.\n"
          "  --data-dir <path>    Data directory (profiles, cache, saves) instead of the default.\n"
          "  --dev-allow-http     Development only: also allow HTTP hubs outside localhost.\n"
          "  --setup-local-hub    Continue \"Set up a Hub on this PC\" (the installer starts the Player with it).\n");
      return 0;
    }
  }
#ifdef Q_OS_WIN
  SetCurrentProcessExplicitAppUserModelID(L"FrameBeam.Player");  // same id as the installer shortcuts
#endif
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("FrameBeam Player"));
#ifdef Q_OS_UNIX
  installTerminationSignals(&app);
#endif
  {
    QIcon icon;
    for (const int s : {16, 24, 32, 48, 64, 128, 256, 512}) {
      icon.addFile(QStringLiteral(":/framebeam/app/player-%1.png").arg(s), QSize(s, s));
    }
    QGuiApplication::setWindowIcon(icon);
  }
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
  const QCommandLineOption setupLocalHub(QStringLiteral("setup-local-hub"),
                                         QStringLiteral("Continue \"Set up a Hub on this PC\" after the Hub was installed."));
  parser.addOption(setupLocalHub);
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
  engine.addImageProvider(QStringLiteral("fbplugin"), new framebeam::ui::PluginImageProvider(controller.plugins()->imageSources()));
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
  if (parser.isSet(setupLocalHub)) {
    controller.localHub()->startAfterInstall();  // after the MSI installed the Hub: connect, create the admin, pair
  }
  return app.exec();
}
