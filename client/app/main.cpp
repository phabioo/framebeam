#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QUrl>
#include <QtQuickControls2/QQuickStyle>

#include "version.h"

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("FrameBeam Player"));
  QGuiApplication::setApplicationVersion(QString::fromUtf8(framebeam::playerVersion().data(),
                                                           static_cast<qsizetype>(framebeam::playerVersion().size())));

  QCommandLineParser parser;
  parser.addHelpOption();
  const QCommandLineOption smoke(QStringLiteral("smoke-test"),
                                 QStringLiteral("QML laden und mit Code 0 beenden."));
  parser.addOption(smoke);
  parser.process(app);

  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QQmlApplicationEngine engine;
  engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
  engine.load(QUrl(QStringLiteral("qrc:/qt/qml/FrameBeam/Player/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  if (parser.isSet(smoke)) {
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
  }
  return app.exec();
}
