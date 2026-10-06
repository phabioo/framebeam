#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtQuickControls2/QQuickStyle>
#include <atomic>

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

}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("FrameBeam Player"));
  QGuiApplication::setApplicationVersion(QString::fromUtf8(framebeam::playerVersion().data(),
                                                           static_cast<qsizetype>(framebeam::playerVersion().size())));

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
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

  QTemporaryDir smokeDir;  // smoke test without --data-dir does not touch real user data
  framebeam::ui::PlayerController::Options opts;
  opts.dataDir = parser.value(dataDir);
  opts.allowHttp = parser.isSet(devHttp);
  if (smokeMode) {
    if (opts.dataDir.isEmpty() && smokeDir.isValid()) {
      opts.dataDir = smokeDir.path();
    }
    opts.memoryCredentials = true;
    opts.probeCoreVersions = false;
    g_previousHandler = qInstallMessageHandler(messageHandler);
  }

  QQuickStyle::setStyle(QStringLiteral("Basic"));
  framebeam::ui::PlayerController controller(opts);

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
