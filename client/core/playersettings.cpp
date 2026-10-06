#include "playersettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace framebeam {

namespace {
constexpr const char* kAppearanceKey = "appearance";
}

PlayerSettings::PlayerSettings(const QString& baseDir)
    : path_(QDir(baseDir).filePath(QStringLiteral("settings/player.json"))) {
  QFile f(path_);
  if (f.open(QIODevice::ReadOnly)) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
      raw_ = doc.object();
    }
  }
  appearance_ = parseAppearance(raw_.value(QLatin1String(kAppearanceKey)).toString());
}

QString PlayerSettings::appearanceName(Appearance a) {
  switch (a) {
    case Appearance::Light: return QStringLiteral("light");
    case Appearance::System: return QStringLiteral("system");
    case Appearance::Dark: break;
  }
  return QStringLiteral("dark");
}

PlayerSettings::Appearance PlayerSettings::parseAppearance(const QString& name, Appearance fallback) {
  const QString n = name.trimmed().toLower();
  if (n == QLatin1String("dark")) return Appearance::Dark;
  if (n == QLatin1String("light")) return Appearance::Light;
  if (n == QLatin1String("system")) return Appearance::System;
  return fallback;
}

bool PlayerSettings::setAppearance(Appearance a) {
  appearance_ = a;
  raw_.insert(QLatin1String(kAppearanceKey), appearanceName(a));
  return save();
}

bool PlayerSettings::save() const {
  if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
    return false;
  }
  QSaveFile f(path_);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(QJsonDocument(raw_).toJson(QJsonDocument::Indented));
  return f.commit();
}

}  // namespace framebeam
