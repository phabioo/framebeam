#pragma once
// Gemeinsame Hilfen der UI-Tests: Harness (Controller + Main.qml offscreen), QML-Warnungszaehler,
// Screenshots (nur wenn FRAMEBEAM_SHOT_DIR gesetzt ist; nie im Repo).

#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/QQmlExtensionPlugin>
#include <QtTest>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <QGuiApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <memory>
#include <vector>

#include "playercontroller.h"

Q_IMPORT_QML_PLUGIN(FrameBeam_PlayerPlugin)

namespace uitest {

inline std::atomic<int>& warningCount() {
  static std::atomic<int> n{0};
  return n;
}
inline QtMessageHandler& previousHandler() {
  static QtMessageHandler h = nullptr;
  return h;
}
inline void countingHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
  // Diagnose: Warnungen/Fehler sofort und ungepuffert auf stderr, damit sie auch bei einem Absturz
  // (z. B. unter Windows-CI mit gepufferter Pipe) sichtbar bleiben.
  if (type != QtDebugMsg && type != QtInfoMsg) {
    const QByteArray line = msg.toLocal8Bit();
    std::fprintf(stderr, "[uitest] %s: %s\n", type == QtWarningMsg ? "warning" : "error", line.constData());
    std::fflush(stderr);
  }
  // Gezaehlt werden nur QML-/Qt-Quick-Warnungen. Umgebungsbedingte Meldungen anderer Kategorien
  // (Fonts, Multimedia-/Audio-Backend ohne Geraet, Plattform) fuehren bewusst nicht zum Fehlschlag.
  if (type == QtWarningMsg || type == QtCriticalMsg) {
    const bool qml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:/")) ||
                     msg.contains(QLatin1String("QML")) ||
                     (ctx.category != nullptr && (QByteArrayView(ctx.category).startsWith("qt.qml") ||
                                                  QByteArrayView(ctx.category).startsWith("qt.quick")));
    if (qml) {
      ++warningCount();
    }
  }
  if (previousHandler() != nullptr) {
    previousHandler()(type, ctx, msg);
  }
}
#ifdef Q_OS_WIN
inline LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
  std::fprintf(stderr, "[uitest] unbehandelte Ausnahme 0x%08lx an Adresse %p\n", ep->ExceptionRecord->ExceptionCode,
               ep->ExceptionRecord->ExceptionAddress);
  std::fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif
inline void crashSignal(int sig) {
  std::fprintf(stderr, "[uitest] Signal %d (Absturz/Abbruch)\n", sig);
  std::fflush(stderr);
  std::_Exit(3);
}
// Muss vor QGuiApplication laufen: ungepufferte Ausgabe + Absturzmeldung, damit ein Fehlschlag nie stumm bleibt.
inline void prepareProcess() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef Q_OS_WIN
  SetUnhandledExceptionFilter(crashFilter);
#endif
  for (int sig : {SIGSEGV, SIGABRT, SIGILL, SIGFPE}) {
    std::signal(sig, crashSignal);
  }
}

// Eigenes main statt QTEST_MAIN: ungepuffert, Konsolen-Executable, "-v2" (jede Testfunktion wird gemeldet).
#define UITEST_MAIN(TestClass)                                  \
  int main(int argc, char** argv) {                             \
    uitest::prepareProcess();                                   \
    std::vector<char*> args(argv, argv + argc);                 \
    char verbose[] = "-v2";                                     \
    args.push_back(verbose);                                    \
    int n = static_cast<int>(args.size());                      \
    QGuiApplication app(n, args.data());                        \
    TestClass tc;                                               \
    return QTest::qExec(&tc, n, args.data());                   \
  }

// Alle Warnungen/Fehler ab hier zaehlen (QML-Ladefehler, Bindungsfehler, Qt-Warnungen).
inline void installWarningCounter() { previousHandler() = qInstallMessageHandler(countingHandler); }

inline QString sha256Hex(const QByteArray& d) {
  return QString::fromLatin1(QCryptographicHash::hash(d, QCryptographicHash::Sha256).toHex());
}

inline QJsonObject gameJson(const QString& id, const QString& title, const QString& sha, qint64 size,
                            const QString& file = QString()) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("title"), title},
          {QStringLiteral("system"), QStringLiteral("nds")},
          {QStringLiteral("rom"), QJsonObject{{QStringLiteral("sha256"), sha},
                                             {QStringLiteral("size"), size},
                                             {QStringLiteral("filename"), file.isEmpty() ? id + QStringLiteral(".nds") : file}}},
          {QStringLiteral("uploaded_by"), QStringLiteral("u_test_1")},
          {QStringLiteral("added_at"), QStringLiteral("2026-01-01T12:00:00Z")}};
}

inline void saveShot(QQuickWindow* w, const QString& name) {
  const QString dir = qEnvironmentVariable("FRAMEBEAM_SHOT_DIR");
  if (dir.isEmpty() || w == nullptr) {
    return;
  }
  QDir().mkpath(dir);
  QTest::qWait(150);  // Layout/Animationen setzen lassen
  const QImage img = w->grabWindow();
  img.save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

inline bool isAllBlack(const QImage& img) {
  for (int y = 0; y < img.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      if ((line[x] & 0x00ffffff) != 0) {
        return false;
      }
    }
  }
  return true;
}

// Controller + Main.qml in einem Offscreen-Fenster.
struct Harness {
  QTemporaryDir dir;
  std::unique_ptr<framebeam::ui::PlayerController> controller;
  std::unique_ptr<QQmlApplicationEngine> engine;
  QQuickWindow* window = nullptr;

  bool start(bool probeCores = false) {
    framebeam::ui::PlayerController::Options o;
    o.dataDir = dir.path();
    o.memoryCredentials = true;
    o.probeCoreVersions = probeCores;
    o.allowHttp = true;
    controller = std::make_unique<framebeam::ui::PlayerController>(o);
    controller->connection()->setPollIntervalMs(80);
    engine = std::make_unique<QQmlApplicationEngine>();
    engine->addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine->setInitialProperties({{QStringLiteral("player"), QVariant::fromValue(controller.get())}});
    engine->load(QUrl(QStringLiteral("qrc:/qt/qml/FrameBeam/Player/Main.qml")));
    if (engine->rootObjects().isEmpty()) {
      return false;
    }
    window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
    return window != nullptr && QTest::qWaitForWindowExposed(window);
  }
  ~Harness() {
    engine.reset();
    controller.reset();
  }

  QQuickItem* item(const char* objectName) const {
    return window ? window->findChild<QQuickItem*>(QString::fromLatin1(objectName)) : nullptr;
  }
  bool click(const char* objectName) const {
    QQuickItem* it = item(objectName);
    if (it == nullptr || !it->isVisible() || !it->isEnabled()) {
      return false;
    }
    const QPointF p = it->mapToScene(QPointF(it->width() / 2, it->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p.toPoint());
    return true;
  }
};

}  // namespace uitest
