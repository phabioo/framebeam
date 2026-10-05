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

// Smoke-Test: QML-Warnungen beim Laden zaehlen (Fehlschlag), alles andere unveraendert ausgeben.
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
  const QCommandLineOption smoke(QStringLiteral("smoke-test"),
                                 QStringLiteral("QML laden und mit Code 0 beenden (Code 1 bei QML-Warnungen)."));
  const QCommandLineOption dataDir(QStringLiteral("data-dir"),
                                   QStringLiteral("Datenverzeichnis (Profile, Cache, Saves) statt AppData."),
                                   QStringLiteral("pfad"));
  const QCommandLineOption devHttp(QStringLiteral("dev-allow-http"),
                                   QStringLiteral("Nur Entwicklung: HTTP-Hubs auch ausserhalb von localhost erlauben."));
  parser.addOption(smoke);
  parser.addOption(dataDir);
  parser.addOption(devHttp);
  parser.process(app);
  const bool smokeMode = parser.isSet(smoke);

  QTemporaryDir smokeDir;  // Smoke-Test ohne --data-dir beruehrt keine echten Nutzerdaten
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
    QTimer::singleShot(400, &app, &QCoreApplication::quit);  // kurz warten: Polish/Bindings laufen erst in der Eventloop
    const int rc = app.exec();
    return g_qmlWarnings.load() > 0 ? 1 : rc;
  }
  controller.startup();
  return app.exec();
}
