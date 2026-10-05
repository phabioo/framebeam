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
#include <memory>

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
  // Nur QML-/Szenengraph-Warnungen zaehlen; erwartete App-Logs (z. B. Hub-Fehler) nicht.
  if (type == QtWarningMsg || type == QtCriticalMsg) {
    const bool qml = msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("qrc:/")) ||
                     msg.contains(QLatin1String("QML")) || msg.contains(QLatin1String("Qt Quick")) ||
                     (ctx.category != nullptr && QByteArrayView(ctx.category).startsWith("qt."));
    if (qml) {
      ++warningCount();
    }
  }
  if (previousHandler() != nullptr) {
    previousHandler()(type, ctx, msg);
  }
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
