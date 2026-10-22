#include "uifonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLatin1String>
#include <QStringList>

namespace uzume::ui {

namespace {
const char* const kFiles[] = {
    "IBMPlexSans-Light.ttf",   "IBMPlexSans-Regular.ttf", "IBMPlexSans-Medium.ttf", "IBMPlexSans-SemiBold.ttf",
    "IBMPlexMono-Regular.ttf", "IBMPlexMono-Medium.ttf",  "IBMPlexMono-SemiBold.ttf",
};
}  // namespace

bool loadDesignFonts() {
  static bool loaded = false;
  if (!loaded) {
    for (const char* file : kFiles) {
      QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Uzume/App/fonts/") + QLatin1String(file));
    }
    loaded = true;
  }
  const QStringList families = QFontDatabase::families();
  const bool sans = families.contains(QStringLiteral("IBM Plex Sans"));
  const bool mono = families.contains(QStringLiteral("IBM Plex Mono"));
  if (sans) {
    QFont f = QGuiApplication::font();
    if (f.family() != QLatin1String("IBM Plex Sans")) {
      f.setFamily(QStringLiteral("IBM Plex Sans"));
      QGuiApplication::setFont(f);
    }
  }
  return sans && mono;
}

}  // namespace uzume::ui
