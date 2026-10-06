#include "emulationcontroller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace framebeam::ui {

using Level = EmulationSettings::Level;
using Source = EmulationSettings::Source;

const QList<EmulationController::FbOption>& EmulationController::frameBeamOptions() {
  static const QList<FbOption> opts = {
      {QString::fromLatin1(kFullscreenKey),
       QObject::tr("Fullscreen on start"),
       QObject::tr("Opens the game view in fullscreen when a game starts."),
       {{QStringLiteral("off"), QObject::tr("Off")}, {QStringLiteral("on"), QObject::tr("On")}},
       QStringLiteral("off")},
      {QString::fromLatin1(kMultiviewKey),
       QObject::tr("Default multiview"),
       QObject::tr("Layout the multiview opens with."),
       {{QStringLiteral("pip"), QObject::tr("Picture-in-picture")}, {QStringLiteral("side"), QObject::tr("Side-by-side")}},
       QStringLiteral("pip")},
  };
  return opts;
}

EmulationController::EmulationController(const QString& dataDir, const emu::ManifestRegistry* manifests, QObject* parent)
    : QObject(parent), dataDir_(dataDir), manifests_(manifests), settings_(dataDir) {}

QVariantMap EmulationController::system() const {
  for (const QVariant& v : systems_) {
    if (v.toMap().value(QStringLiteral("id")).toString() == selected_) return v.toMap();
  }
  return {};
}

QString EmulationController::coreIdOf(const QString& systemId) const {
  const emu::SystemManifest* m = manifests_ != nullptr ? manifests_->find(systemId) : nullptr;
  return m != nullptr ? m->coreId : QString();
}

QString EmulationController::cachePath(const QString& coreId) const {
  return QDir(dataDir_).filePath(QStringLiteral("cache/core-options/") + coreId + QStringLiteral(".json"));
}

void EmulationController::setSystems(const QVariantList& cards) {
  systems_ = cards;
  if (selected_.isEmpty() || system().isEmpty()) {
    selected_ = cards.isEmpty() ? QString() : cards.first().toMap().value(QStringLiteral("id")).toString();
    emit selectionChanged();
  }
  emit systemsChanged();
  rebuild();
}

void EmulationController::setGameRunning(bool running) {
  if (gameRunning_ == running) return;
  gameRunning_ = running;
  emit gameRunningChanged();
  rebuild();
}

void EmulationController::setCoreProbe(const QString& coreId, const emu::CoreProbe& probe, bool writeCache) {
  if (!probe.ok) return;
  probes_.insert(coreId, probe);
  if (writeCache) {
    const QString path = cachePath(coreId);
    if (QDir().mkpath(QFileInfo(path).absolutePath())) {
      QSaveFile f(path);
      if (f.open(QIODevice::WriteOnly)) {
        f.write(emu::coreProbeToJson(probe));
        f.commit();
      }
    }
  }
  rebuild();
}

bool EmulationController::hasCoreProbe(const QString& coreId) const { return probes_.contains(coreId) && !probes_.value(coreId).options.isEmpty(); }

const emu::CoreProbe* EmulationController::coreProbe(const QString& coreId) const {
  const auto it = probes_.constFind(coreId);
  return it == probes_.cend() ? nullptr : &it.value();
}

void EmulationController::loadCoreCache(const QString& coreId) {
  if (probes_.contains(coreId)) return;
  QFile f(cachePath(coreId));
  if (!f.open(QIODevice::ReadOnly)) return;
  const emu::CoreProbe p = emu::coreProbeFromJson(f.readAll());
  if (p.ok) {
    probes_.insert(coreId, p);
    rebuild();
  }
}

void EmulationController::setLevel(const QString& level) {
  if ((level != QLatin1String("global") && level != QLatin1String("system")) || level == level_) return;
  level_ = level;
  emit levelChanged();
  rebuild();
}

void EmulationController::selectSystem(const QString& id) {
  if (id == selected_) return;
  for (const QVariant& v : systems_) {
    if (v.toMap().value(QStringLiteral("id")).toString() == id) {
      selected_ = id;
      emit selectionChanged();
      rebuild();
      return;
    }
  }
}

Level EmulationController::levelEnum() const { return level_ == QLatin1String("global") ? Level::Global : Level::System; }

QString EmulationController::frameBeamValue(const QString& key, const QString& systemId, const QString& gameId) const {
  QString def;
  for (const FbOption& o : frameBeamOptions()) {
    if (o.key == key) def = o.defaultValue;
  }
  return settings_.resolve(key, systemId, gameId, QString(), def).value;
}

QMap<QString, QString> EmulationController::launchOverrides(const QString& systemId, const QString& gameId) const {
  return settings_.mergedOverrides(systemId, gameId);
}

bool EmulationController::knownOption(const QString& key, QList<emu::CoreOptionValue>* values) const {
  for (const FbOption& o : frameBeamOptions()) {
    if (o.key == key) {
      *values = o.values;
      return true;
    }
  }
  if (level_ == QLatin1String("global")) return false;  // core keys are set per system/core
  const emu::SystemManifest* man = manifests_ != nullptr ? manifests_->find(selected_) : nullptr;
  const emu::CoreProbe* probe = man != nullptr ? coreProbe(man->coreId) : nullptr;
  if (man == nullptr || probe == nullptr || emu::isLockedCoreOption(*man, key)) return false;
  for (const emu::CoreOption& o : probe->options) {
    if (o.key == key && o.visible) {
      *values = o.values;
      return true;
    }
  }
  return false;
}

void EmulationController::setOption(const QString& key, const QString& value) {
  QList<emu::CoreOptionValue> values;
  if (selected_.isEmpty() || !knownOption(key, &values)) return;
  const bool valid = std::any_of(values.cbegin(), values.cend(), [&](const emu::CoreOptionValue& v) { return v.value == value; });
  if (!valid) return;
  settings_.setValue(levelEnum(), scope(), key, value);
  rebuild();
  if (key.startsWith(QLatin1String("framebeam."))) emit frameBeamOptionsChanged();
}

void EmulationController::resetOption(const QString& key) {
  if (selected_.isEmpty()) return;
  settings_.removeValue(levelEnum(), scope(), key);
  rebuild();
  if (key.startsWith(QLatin1String("framebeam."))) emit frameBeamOptionsChanged();
}

QVariantMap EmulationController::row(const QString& key, const QString& label, const QString& description, const QString& category,
                                     const QList<emu::CoreOptionValue>& values, const QString& manifestDefault,
                                     const QString& coreDefault, bool frameBeam) const {
  const bool global = level_ == QLatin1String("global");
  const Level lvl = levelEnum();
  const bool isSet = settings_.hasValue(lvl, scope(), key);
  // Value shown: what the level being edited yields (global: global > default; system: system > global > ... ).
  const EmulationSettings::Resolved r = settings_.resolve(key, global ? QString() : selected_, QString(), manifestDefault, coreDefault);
  QString origin;
  if (isSet) {
    origin = tr("set here");
  } else {
    switch (r.source) {
      case Source::Global: origin = tr("inherited · Global"); break;
      case Source::Manifest: origin = tr("inherited · FrameBeam default"); break;
      case Source::Core: origin = frameBeam ? (global ? tr("default") : tr("inherited · default")) : tr("inherited · core default"); break;
      default: origin = tr("inherited"); break;
    }
  }
  QVariantList vals;
  QString valueLabel = r.value;
  bool known = false;
  for (const emu::CoreOptionValue& v : values) {
    vals.append(QVariantMap{{QStringLiteral("value"), v.value}, {QStringLiteral("label"), v.label.isEmpty() ? v.value : v.label}});
    if (v.value == r.value) {
      valueLabel = v.label.isEmpty() ? v.value : v.label;
      known = true;
    }
  }
  if (!known && !r.value.isEmpty()) {
    // A stored value the core does not offer (any more) is shown as it is, never silently replaced.
    vals.prepend(QVariantMap{{QStringLiteral("value"), r.value}, {QStringLiteral("label"), r.value}});
  }
  return {{QStringLiteral("key"), key},
          {QStringLiteral("label"), label},
          {QStringLiteral("description"), description},
          {QStringLiteral("category"), category},
          {QStringLiteral("values"), vals},
          {QStringLiteral("value"), r.value},
          {QStringLiteral("valueLabel"), valueLabel},
          {QStringLiteral("isSet"), isSet},
          {QStringLiteral("origin"), origin},
          {QStringLiteral("restart"), !frameBeam && gameRunning_}};
}

void EmulationController::rebuild() {
  QVariantList groups;
  lockedCount_ = 0;
  coreNote_.clear();
  if (!selected_.isEmpty()) {
    const bool global = level_ == QLatin1String("global");
    // FrameBeam group
    QVariantList fb;
    for (const FbOption& o : frameBeamOptions()) {
      fb.append(row(o.key, o.label, o.description, QString(), o.values, QString(), o.defaultValue, true));
    }
    groups.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("framebeam")},
                              {QStringLiteral("title"), tr("FrameBeam")},
                              {QStringLiteral("subtitle"), tr("Presentation in the Player")},
                              {QStringLiteral("note"), QString()},
                              {QStringLiteral("options"), fb}});
    // Core group (system level only: the keys belong to the core)
    const emu::SystemManifest* man = manifests_ != nullptr ? manifests_->find(selected_) : nullptr;
    if (man != nullptr && !global) {
      const emu::CoreProbe* probe = coreProbe(man->coreId);
      QString coreName = probe != nullptr && !probe->info.name.isEmpty() ? probe->info.name : QString();
      for (const QVariant& v : systems_) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == selected_ && coreName.isEmpty()) coreName = m.value(QStringLiteral("coreName")).toString();
      }
      QVariantList core;
      if (probe != nullptr) {
        // Categories in the order the core reports them (options without a category last).
        QStringList order;
        for (const emu::CoreOptionCategory& c : probe->categories) order.append(c.key);
        order.append(QString());
        for (const QString& catKey : std::as_const(order)) {
          QString catName;
          for (const emu::CoreOptionCategory& c : probe->categories) {
            if (c.key == catKey) catName = c.description;
          }
          for (const emu::CoreOption& o : probe->options) {
            if (o.categoryKey != catKey && !(catKey.isEmpty() && !order.contains(o.categoryKey))) continue;
            if (!o.visible || o.values.isEmpty()) continue;
            if (emu::isLockedCoreOption(*man, o.key)) {
              ++lockedCount_;
              continue;
            }
            core.append(row(o.key, o.description, o.info, catName, o.values, man->coreOptions.value(o.key, QString()), o.defaultValue, false));
          }
        }
      }
      if (probe == nullptr || probe->options.isEmpty()) {
        coreNote_ = tr("The core has not reported its options yet. They appear after the core was loaded once; "
                       "the Player does that when it can (not while a game is running).");
      }
      groups.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("core")},
                                {QStringLiteral("title"), coreName.isEmpty() ? man->coreId : coreName},
                                {QStringLiteral("subtitle"), tr("from Libretro core options · only options reported by the core")},
                                {QStringLiteral("note"), coreNote_},
                                {QStringLiteral("options"), core}});
    }
  }
  groups_ = groups;
  emit groupsChanged();
}

}  // namespace framebeam::ui
