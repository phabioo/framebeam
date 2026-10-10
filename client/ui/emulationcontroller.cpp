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
       QObject::tr("Default Multiview"),
       QObject::tr("Layout the multiview opens with."),
       {{QStringLiteral("pip"), QObject::tr("Picture-in-Picture")},
        {QStringLiteral("side"), QObject::tr("Side-by-Side")},
        {QStringLiteral("grid"), QObject::tr("Grid 2×2")}},
       QStringLiteral("pip")},
      {QString::fromLatin1(kSpeedUpRatioKey),
       QObject::tr("Speed-up speed"),
       QObject::tr("How fast the game runs while speed-up is on."),
       {{QStringLiteral("1.5"), QStringLiteral("1.5×")}, {QStringLiteral("2"), QStringLiteral("2×")}, {QStringLiteral("3"), QStringLiteral("3×")},
        {QStringLiteral("4"), QStringLiteral("4×")}, {QStringLiteral("6"), QStringLiteral("6×")}, {QStringLiteral("8"), QStringLiteral("8×")}},
       QStringLiteral("2"),
       true},
      {QString::fromLatin1(kSpeedUpOnStartKey),
       QObject::tr("Speed-up on start"),
       QObject::tr("The game starts sped up and stays so until you switch speed-up off."),
       {{QStringLiteral("false"), QObject::tr("Off")}, {QStringLiteral("true"), QObject::tr("On")}},
       QStringLiteral("false"),
       true},
      {QString::fromLatin1(kSpeedUpAudioKey),
       QObject::tr("Audio during speed-up"),
       QObject::tr("Plays the sound at a higher pitch while sped up; off mutes it."),
       {{QStringLiteral("false"), QObject::tr("Off")}, {QStringLiteral("true"), QObject::tr("On")}},
       QStringLiteral("true"),
       true},
  };
  return opts;
}

EmulationController::EmulationController(const QString& dataDir, const emu::ManifestRegistry* manifests, QObject* parent)
    : QObject(parent), dataDir_(dataDir), manifests_(manifests), settings_(dataDir) {}

QVariantList EmulationController::systems() const {
  QVariantList out;
  for (const QVariant& v : systems_) {
    QVariantMap m = v.toMap();
    m.insert(QStringLiteral("changedCount"), static_cast<int>(settings_.values(Level::System, m.value(QStringLiteral("id")).toString()).size()));
    out.append(m);
  }
  return out;
}

QVariantList EmulationController::games() const {
  QVariantList out;
  for (const QVariant& v : games_) {
    QVariantMap m = v.toMap();
    QString sysName = m.value(QStringLiteral("systemId")).toString();
    for (const QVariant& c : systems_) {
      if (c.toMap().value(QStringLiteral("id")).toString() == m.value(QStringLiteral("systemId")).toString()) {
        sysName = c.toMap().value(QStringLiteral("name")).toString();
      }
    }
    m.insert(QStringLiteral("systemName"), sysName);
    m.insert(QStringLiteral("label"), QStringLiteral("%1 · %2").arg(m.value(QStringLiteral("title")).toString(), sysName));
    m.insert(QStringLiteral("changedCount"), static_cast<int>(settings_.values(Level::Game, m.value(QStringLiteral("id")).toString()).size()));
    out.append(m);
  }
  return out;
}

QVariantMap EmulationController::game() const {
  for (const QVariant& v : games()) {
    if (v.toMap().value(QStringLiteral("id")).toString() == selectedGame_) return v.toMap();
  }
  return {};
}

int EmulationController::gameOverrideCount() const {
  int n = 0;
  for (const QVariant& v : games_) n += settings_.values(Level::Game, v.toMap().value(QStringLiteral("id")).toString()).isEmpty() ? 0 : 1;
  return n;
}

void EmulationController::setGames(const QVariantList& games) {
  if (games == games_) return;
  games_ = games;
  emit gamesChanged();
  if (!selectedGame_.isEmpty() && game().isEmpty()) {  // the game left the library
    selectedGame_.clear();
    emit gameSelectionChanged();
    rebuild();
  }
}

void EmulationController::selectGame(const QString& gameId) {
  if (gameId == selectedGame_) return;
  const auto it = std::find_if(games_.cbegin(), games_.cend(), [&](const QVariant& v) { return v.toMap().value(QStringLiteral("id")).toString() == gameId; });
  if (it == games_.cend()) return;
  selectedGame_ = gameId;
  const QString sys = it->toMap().value(QStringLiteral("systemId")).toString();
  if (sys != selected_) {
    selected_ = sys;
    emit selectionChanged();
  }
  emit gameSelectionChanged();
  const QString core = effectiveCoreId();
  if (!core.isEmpty()) loadCoreCache(core);
  rebuild();
}

QString EmulationController::scope() const {
  if (level_ == QLatin1String("global")) return {};
  return level_ == QLatin1String("game") ? selectedGame_ : selected_;
}

QString EmulationController::effectiveCoreId() const {
  const QString systemCore = system().value(QStringLiteral("coreId")).toString();
  if (level_ != QLatin1String("game") || selectedGame_.isEmpty()) return systemCore;
  const QString chosen = settings_.value(Level::Game, selectedGame_, QString::fromLatin1(kCoreKey));
  if (chosen.isEmpty()) return systemCore;
  for (const QVariant& c : system().value(QStringLiteral("cores")).toList()) {
    const QString id = c.toMap().value(QStringLiteral("id")).toString();
    if (id == chosen || (manifests_ != nullptr && manifests_->canonicalCoreId(id) == manifests_->canonicalCoreId(chosen))) return id;
  }
  return systemCore;  // a core the Hub no longer serves is ignored (the notice names it)
}

int EmulationController::defaultsChangedCount() const { return static_cast<int>(settings_.values(Level::Global, QString()).size()); }

QVariantMap EmulationController::system() const {
  for (const QVariant& v : systems_) {
    if (v.toMap().value(QStringLiteral("id")).toString() == selected_) return v.toMap();
  }
  return {};
}

std::optional<emu::SystemManifest> EmulationController::selectedManifest() const {
  if (manifests_ == nullptr || selected_.isEmpty()) return std::nullopt;
  const QString coreId = effectiveCoreId();
  if (!coreId.isEmpty()) {
    if (auto m = manifests_->resolve(selected_, coreId)) return m;
  }
  const emu::SystemManifest* base = manifests_->find(selected_);
  return base != nullptr ? std::optional<emu::SystemManifest>(*base) : std::nullopt;
}

QString EmulationController::cachePath(const QString& coreId) const {
  return QDir(dataDir_).filePath(QStringLiteral("cache/core-options/") + coreId + QStringLiteral(".json"));
}

void EmulationController::setSystems(const QVariantList& cards) {
  if (cards == systems_ && !selected_.isEmpty()) {
    return;  // periodic refresh without a change: keep the page as it is
  }
  systems_ = cards;
  if (selected_.isEmpty() || system().isEmpty()) {
    selected_ = cards.isEmpty() ? QString() : cards.first().toMap().value(QStringLiteral("id")).toString();
    emit selectionChanged();
  }
  emit systemsChanged();
  emit gamesChanged();
  rebuild();
}

void EmulationController::setGameRunning(bool running) {
  if (gameRunning_ == running) return;
  gameRunning_ = running;
  restartTouched_ = false;
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

// A successful probe counts as done even when the core reports no options (no re-probe, no cache rewrite).
bool EmulationController::hasCoreProbe(const QString& coreId) const { return probes_.contains(coreId); }

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
  if ((level != QLatin1String("global") && level != QLatin1String("system") && level != QLatin1String("game")) || level == level_) return;
  level_ = level;
  emit levelChanged();
  rebuild();
  emit systemsChanged();
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

Level EmulationController::levelEnum() const {
  return level_ == QLatin1String("global") ? Level::Global : (level_ == QLatin1String("game") ? Level::Game : Level::System);
}

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
  if (key == QLatin1String(kCoreKey)) {
    if (level_ == QLatin1String("game")) values->append({QString(), tr("Use system default")});
    for (const QVariant& c : system().value(QStringLiteral("cores")).toList()) {
      const QVariantMap cm = c.toMap();
      values->append({cm.value(QStringLiteral("id")).toString(), cm.value(QStringLiteral("label")).toString()});
    }
    return values->size() > (level_ == QLatin1String("game") ? 1 : 0);
  }
  const std::optional<emu::SystemManifest> manOpt = selectedManifest();
  const emu::SystemManifest* man = manOpt ? &*manOpt : nullptr;
  const emu::CoreProbe* probe = man != nullptr ? coreProbe(man->coreId) : nullptr;
  if (man == nullptr || probe == nullptr || emu::isLockedCoreOption(*man, key)) return false;
  for (const emu::CoreOption& o : probe->options) {
    if (o.key == key && (o.visible || man->alwaysShownCoreOptions.contains(key))) {
      *values = o.values;
      return true;
    }
  }
  return false;
}

QString EmulationController::defaultValueOf(const QString& key) const {
  const bool global = level_ == QLatin1String("global");
  for (const FbOption& o : frameBeamOptions()) {
    if (o.key == key) return o.defaultValue;
  }
  if (key == QLatin1String(kCoreKey)) return level_ == QLatin1String("game") ? QString() : system().value(QStringLiteral("defaultCoreId")).toString();
  const std::optional<emu::SystemManifest> manOpt = selectedManifest();
  const emu::SystemManifest* man = manOpt ? &*manOpt : nullptr;
  if (man == nullptr) return {};
  QString coreDefault;
  if (const emu::CoreProbe* probe = coreProbe(man->coreId)) {
    for (const emu::CoreOption& o : probe->options) {
      if (o.key == key) coreDefault = o.defaultValue;
    }
  }
  // System level inherits a Global value if the file has one; otherwise manifest, then core default.
  if (level_ == QLatin1String("game") && settings_.hasValue(Level::System, selected_, key)) return settings_.value(Level::System, selected_, key);
  if (!global && settings_.hasValue(Level::Global, QString(), key)) return settings_.value(Level::Global, QString(), key);
  const QString manifestDefault = man->coreOptions.value(key, QString());
  return manifestDefault.isNull() ? coreDefault : manifestDefault;
}

void EmulationController::setOption(const QString& key, const QString& value) {
  QList<emu::CoreOptionValue> values;
  if (selected_.isEmpty() || (level_ == QLatin1String("game") && selectedGame_.isEmpty()) || !knownOption(key, &values)) return;
  const bool valid = std::any_of(values.cbegin(), values.cend(), [&](const emu::CoreOptionValue& v) { return v.value == value; });
  if (!valid) return;
  if (value == defaultValueOf(key)) {
    settings_.removeValue(levelEnum(), scope(), key);  // back to the default: nothing stays stored, no "changed" mark
  } else {
    settings_.setValue(levelEnum(), scope(), key, value);
    if (gameRunning_ && !key.startsWith(QLatin1String("framebeam."))) restartTouched_ = true;
  }
  rebuild();
  emit systemsChanged();
  emit gamesChanged();
  if (key.startsWith(QLatin1String("framebeam."))) emit frameBeamOptionsChanged();
  if (key == QLatin1String(kCoreKey)) emit coreChoiceChanged();
}

void EmulationController::resetOption(const QString& key) {
  if (selected_.isEmpty() || (level_ == QLatin1String("game") && selectedGame_.isEmpty())) return;
  settings_.removeValue(levelEnum(), scope(), key);
  rebuild();
  emit systemsChanged();
  emit gamesChanged();
  if (key.startsWith(QLatin1String("framebeam."))) emit frameBeamOptionsChanged();
  if (key == QLatin1String(kCoreKey)) emit coreChoiceChanged();
}

void EmulationController::resetAllChanged() {
  if (selected_.isEmpty() || (level_ == QLatin1String("game") && selectedGame_.isEmpty())) return;
  bool fb = false;
  bool coreChanged = false;
  const QMap<QString, QString> vals = settings_.values(levelEnum(), scope());
  for (auto it = vals.cbegin(); it != vals.cend(); ++it) {
    settings_.removeValue(levelEnum(), scope(), it.key());
    fb = fb || it.key().startsWith(QLatin1String("framebeam."));
    coreChanged = coreChanged || it.key() == QLatin1String(kCoreKey);
  }
  restartTouched_ = false;
  rebuild();
  emit systemsChanged();
  emit gamesChanged();
  if (fb) emit frameBeamOptionsChanged();
  if (coreChanged) emit coreChoiceChanged();
}

void EmulationController::setCategoryFilter(const QString& category) {
  if (categoryFilter_ == category) return;
  categoryFilter_ = category;
  rebuild();
  emit filterChanged();
}

void EmulationController::setSearchText(const QString& text) {
  if (searchText_ == text) return;
  searchText_ = text;
  rebuild();
  emit filterChanged();
}

QVariantMap EmulationController::row(const QString& key, const QString& label, const QString& description, const QString& category,
                                     const QList<emu::CoreOptionValue>& values, const QString& manifestDefault,
                                     const QString& coreDefault, bool frameBeam) const {
  const bool global = level_ == QLatin1String("global");
  const Level lvl = levelEnum();
  const bool isSet = settings_.hasValue(lvl, scope(), key);
  // Value shown: what the level being edited yields (global: global > default; system: system > global > ... ).
  const bool gameLvl = level_ == QLatin1String("game");
  const EmulationSettings::Resolved r =
      settings_.resolve(key, global ? QString() : selected_, gameLvl ? selectedGame_ : QString(), manifestDefault, coreDefault);
  QString origin;
  if (isSet) {
    origin = tr("set here");
  } else {
    switch (r.source) {
      case Source::System: origin = tr("inherited · System"); break;
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
  QStringList categories;
  lockedCount_ = 0;
  coreNote_.clear();
  changedCount_ = 0;
  const auto keep = [this](const QVariantMap& row) {
    if (!categoryFilter_.isEmpty() && row.value(QStringLiteral("category")).toString() != categoryFilter_) return false;
    const QString needle = searchText_.trimmed();
    return needle.isEmpty() || row.value(QStringLiteral("label")).toString().contains(needle, Qt::CaseInsensitive) ||
           row.value(QStringLiteral("description")).toString().contains(needle, Qt::CaseInsensitive);
  };
  // Collects chips and the changed count over all rows of the scope, returns the rows that pass the filters.
  const auto collect = [&](const QVariantList& rows) {
    QVariantList out;
    for (const QVariant& v : rows) {
      const QVariantMap r = v.toMap();
      const QString cat = r.value(QStringLiteral("category")).toString();
      if (!cat.isEmpty() && !categories.contains(cat)) categories.append(cat);
      if (r.value(QStringLiteral("isSet")).toBool()) ++changedCount_;
    }
    if (!categoryFilter_.isEmpty() && !categories.contains(categoryFilter_)) categoryFilter_.clear();
    for (const QVariant& v : rows) {
      if (keep(v.toMap())) out.append(v);
    }
    return out;
  };
  if (!selected_.isEmpty() && !(level_ == QLatin1String("game") && selectedGame_.isEmpty())) {
    const bool global = level_ == QLatin1String("global");
    const bool gameLvl = level_ == QLatin1String("game");
    if (global) {
      // Defaults: options that do not depend on a system.
      QVariantList fb;
      for (const FbOption& o : frameBeamOptions()) {
        fb.append(row(o.key, o.label, o.description, o.perSystem ? tr("Speed-up") : tr("Display"), o.values, QString(), o.defaultValue, true));
      }
      groups.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("framebeam")},
                                {QStringLiteral("title"), tr("FrameBeam")},
                                {QStringLiteral("subtitle"), tr("Presentation in the Player")},
                                {QStringLiteral("note"), QString()},
                                {QStringLiteral("options"), collect(fb)}});
    }
    // Core group (system scope only: the keys belong to the core)
    const std::optional<emu::SystemManifest> manOpt = selectedManifest();
    const emu::SystemManifest* man = manOpt ? &*manOpt : nullptr;
    if (man != nullptr && !global) {
      const emu::CoreProbe* probe = coreProbe(man->coreId);
      QString coreName = probe != nullptr && !probe->info.name.isEmpty() ? probe->info.name : QString();
      if (gameLvl && coreName.isEmpty()) coreName = man->coreDisplayName;
      for (const QVariant& v : systems_) {
        const QVariantMap m = v.toMap();
        if (!gameLvl && m.value(QStringLiteral("id")).toString() == selected_ && coreName.isEmpty()) coreName = m.value(QStringLiteral("coreName")).toString();
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
          if (catName.isEmpty()) catName = tr("Other");
          for (const emu::CoreOption& o : probe->options) {
            if (o.categoryKey != catKey && !(catKey.isEmpty() && !order.contains(o.categoryKey))) continue;
            if ((!o.visible && !man->alwaysShownCoreOptions.contains(o.key)) || o.values.isEmpty()) continue;
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
      QString groupNote = coreNote_;
      if (man->experimental) {
        const QString exp = tr("Experimental core: FrameBeam has no profile for it. It runs without option defaults or locked options, "
                               "its picture is shown as the core renders it, and touch input is off.");
        groupNote = groupNote.isEmpty() ? exp : groupNote + QLatin1Char(' ') + exp;
      }
      groups.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("core")},
                                {QStringLiteral("title"), coreName.isEmpty() ? man->coreId : coreName},
                                {QStringLiteral("subtitle"), tr("from Libretro core options · only options reported by the core")},
                                {QStringLiteral("note"), groupNote},
                                {QStringLiteral("options"), collect(core)}});
    }
    // Speed-up options of the system: collected after the core group so its categories exist for the filter.
    if (!global) {
      QVariantList fb;
      const QVariantMap card = system();
      QList<emu::CoreOptionValue> coreValues;
      if (knownOption(QString::fromLatin1(kCoreKey), &coreValues)) {
        QVariantMap r = row(QString::fromLatin1(kCoreKey), tr("Core"),
                            tr("The core that runs games of this system. The Hub offers the cores listed here; the change applies the next time a game starts."),
                            QString(), coreValues, QString(), card.value(QStringLiteral("defaultCoreId")).toString(), true);
        const QString effective = card.value(QStringLiteral("coreId")).toString();
        if (!card.value(QStringLiteral("coreNotice")).toString().isEmpty() && !effective.isEmpty()) {
          // The stored choice is not served any more: show what is really used.
          for (const emu::CoreOptionValue& v : std::as_const(coreValues)) {
            if (v.value == effective) {
              r.insert(QStringLiteral("value"), v.value);
              r.insert(QStringLiteral("valueLabel"), v.label);
            }
          }
          r.insert(QStringLiteral("values"), [&]() {
            QVariantList vals;
            for (const emu::CoreOptionValue& v : std::as_const(coreValues)) vals.append(QVariantMap{{QStringLiteral("value"), v.value}, {QStringLiteral("label"), v.label}});
            return vals;
          }());
        } else if (!r.value(QStringLiteral("isSet")).toBool()) {
          r.insert(QStringLiteral("origin"), tr("Hub default"));
        }
        if (gameLvl) {
          // Per game: "Use system default" when the game has no choice of its own; the stored id is shown even when the Hub
          // does not serve that core any more (listed as it is, never replaced silently).
          const QString own = settings_.value(Level::Game, selectedGame_, QString::fromLatin1(kCoreKey));
          if (own.isEmpty()) {
            r.insert(QStringLiteral("value"), QString());
            r.insert(QStringLiteral("valueLabel"), tr("Use system default"));
            r.insert(QStringLiteral("origin"), tr("inherited · %1").arg(card.value(QStringLiteral("coreName")).toString()));
          }
          r.insert(QStringLiteral("description"),
                   tr("The core that runs this game. \"Use system default\" follows the system's choice; the change applies the next time the game starts."));
        }
        r.insert(QStringLiteral("restart"), false);
        fb.append(r);
      }
      for (const FbOption& o : frameBeamOptions()) {
        if (o.perSystem) fb.append(row(o.key, o.label, o.description, QString(), o.values, QString(), o.defaultValue, true));
      }
      groups.prepend(QVariantMap{{QStringLiteral("id"), QStringLiteral("framebeam")},
                                {QStringLiteral("title"), tr("FrameBeam")},
                                {QStringLiteral("subtitle"), gameLvl ? tr("Core and speed-up for this game") : tr("Core and speed-up in the Player")},
                                {QStringLiteral("note"), gameLvl ? QString() : card.value(QStringLiteral("coreNotice")).toString()},
                                {QStringLiteral("options"), collect(fb)}});
    }
  }
  categories_ = categories;
  restartHint_ = restartTouched_ && gameRunning_;
  groups_ = groups;
  emit groupsChanged();
}

}  // namespace framebeam::ui
