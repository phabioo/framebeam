#include "playersettings.h"
#include "fsutil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>

#include "savestore.h"

namespace framebeam {

namespace {
constexpr const char* kAppearanceKey = "appearance";
constexpr const char* kUpdateChannelKey = "update_channel";
constexpr const char* kUpdateChannelDefaultKey = "update_channel_default";
constexpr const char* kUpdateAutoKey = "update_auto_install";
constexpr const char* kDiagOpenKey = "diagnostics_open";
constexpr const char* kDeveloperModeKey = "developer_mode";
constexpr const char* kDiagEmulationKey = "diagnostics_emulation_open";
constexpr const char* kDiagStreamingKey = "diagnostics_streaming_open";
constexpr const char* kPluginHudCornersKey = "plugin_hud_corners";  // { plugin_id: corner }
constexpr const char* kPluginHudHiddenKey = "plugin_hud_hidden";    // [plugin_id]
constexpr const char* kPluginHudAllHiddenKey = "plugin_hud_all_hidden";
constexpr const char* kPluginHotkeysKey = "plugin_hotkeys";          // { "<plugin_id>/<hotkey_id>": key | "none" }
constexpr const char* kPluginToastMuteKey = "plugin_toast_mute";    // { plugin_id: mode }
constexpr const char* kSessionVisibilityKey = "session_visibility";
constexpr const char* kLibrarySortKey = "library_sort";
constexpr const char* kLibraryReadyFirstKey = "library_ready_first";
constexpr const char* kRomCacheLimitKey = "rom_cache_limit_bytes";
constexpr const char* kLibraryFoldersKey = "library_folders";
constexpr const char* kLastPlayedKey = "last_played";  // { hub_id: { game_id: epoch ms } }
constexpr const char* kSaveSlotsKey = "save_slots";  // { hub_id: { game_id: slot } }
}

PlayerSettings::PlayerSettings(const QString& baseDir)
    : path_(QDir(baseDir).filePath(QStringLiteral("settings/player.json"))) {
  raw_ = fsutil::readJsonObject(path_);  // a corrupt file is moved aside before anything can overwrite it
  appearance_ = parseAppearance(raw_.value(QLatin1String(kAppearanceKey)).toString());
  const QString ch = raw_.value(QLatin1String(kUpdateChannelKey)).toString();
  if (ch == QLatin1String("stable") || ch == QLatin1String("beta")) {
    updateChannel_ = ch;
  } else if (ch == QLatin1String("test")) {
    updateChannel_ = QStringLiteral("beta");  // old name; rewritten as "beta" on the next save
    raw_.insert(QLatin1String(kUpdateChannelKey), updateChannel_);
  }
  const QString chDef = raw_.value(QLatin1String(kUpdateChannelDefaultKey)).toString();
  if (chDef == QLatin1String("stable") || chDef == QLatin1String("beta")) updateChannelDefault_ = chDef;
  if (raw_.value(QLatin1String(kUpdateAutoKey)).isBool()) {
    updateAutoInstall_ = raw_.value(QLatin1String(kUpdateAutoKey)).toBool();
  }
  diagOpen_ = raw_.value(QLatin1String(kDiagOpenKey)).toBool(false);
  developerMode_ = raw_.value(QLatin1String(kDeveloperModeKey)).toBool(false);
  pluginHudAllHidden_ = raw_.value(QLatin1String(kPluginHudAllHiddenKey)).toBool(false);
  diagEmulationOpen_ = raw_.value(QLatin1String(kDiagEmulationKey)).toBool(true);
  diagStreamingOpen_ = raw_.value(QLatin1String(kDiagStreamingKey)).toBool(true);
  const QString sort = raw_.value(QLatin1String(kLibrarySortKey)).toString();
  if (isValidLibrarySort(sort)) librarySort_ = sort;
  libraryReadyFirst_ = raw_.value(QLatin1String(kLibraryReadyFirstKey)).toBool(false);
  if (const QJsonValue v = raw_.value(QLatin1String(kRomCacheLimitKey)); v.isDouble() && v.toDouble() >= 0) {
    romCacheLimit_ = static_cast<qint64>(v.toDouble());
  }
  for (const QJsonValue& v : raw_.value(QLatin1String(kLibraryFoldersKey)).toArray()) {
    const QString f = QDir::cleanPath(v.toString());
    if (QDir::isAbsolutePath(f) && !libraryFolders_.contains(f)) libraryFolders_.append(f);
  }
  const QString vis = raw_.value(QLatin1String(kSessionVisibilityKey)).toString();
  if (vis == QLatin1String("private") || vis == QLatin1String("hub_users") || vis == QLatin1String("invite_only")) {
    sessionVisibility_ = vis;
  }
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

bool PlayerSettings::setUpdateChannel(const QString& channelIn) {
  const QString channel = channelIn == QLatin1String("test") ? QStringLiteral("beta") : channelIn;
  if (!channel.isEmpty() && channel != QLatin1String("stable") && channel != QLatin1String("beta")) {
    return false;
  }
  updateChannel_ = channel;
  if (channel.isEmpty()) {
    raw_.remove(QLatin1String(kUpdateChannelKey));
  } else {
    raw_.insert(QLatin1String(kUpdateChannelKey), channel);
  }
  return save();
}

bool PlayerSettings::setUpdateChannelDefault(const QString& channel) {
  if (channel != QLatin1String("stable") && channel != QLatin1String("beta")) return false;
  updateChannelDefault_ = channel;
  raw_.insert(QLatin1String(kUpdateChannelDefaultKey), channel);
  return save();
}

bool PlayerSettings::setUpdateAutoInstall(bool on) {
  updateAutoInstall_ = on;
  raw_.insert(QLatin1String(kUpdateAutoKey), on);
  return save();
}

bool PlayerSettings::setDiagnosticsOpen(bool open) {
  diagOpen_ = open;
  raw_.insert(QLatin1String(kDiagOpenKey), open);
  return save();
}

bool PlayerSettings::setDeveloperMode(bool on) {
  developerMode_ = on;
  raw_.insert(QLatin1String(kDeveloperModeKey), on);
  return save();
}

bool PlayerSettings::setDiagnosticsEmulationOpen(bool open) {
  diagEmulationOpen_ = open;
  raw_.insert(QLatin1String(kDiagEmulationKey), open);
  return save();
}

bool PlayerSettings::setDiagnosticsStreamingOpen(bool open) {
  diagStreamingOpen_ = open;
  raw_.insert(QLatin1String(kDiagStreamingKey), open);
  return save();
}

bool PlayerSettings::isValidLibrarySort(const QString& key) {
  static const QStringList keys = {QStringLiteral("name_asc"),  QStringLiteral("name_desc"), QStringLiteral("added_desc"),
                                   QStringLiteral("added_asc"), QStringLiteral("size_desc"), QStringLiteral("size_asc"),
                                   QStringLiteral("system"),    QStringLiteral("played")};
  return keys.contains(key);
}

bool PlayerSettings::setLibrarySort(const QString& key) {
  if (!isValidLibrarySort(key)) return false;
  librarySort_ = key;
  raw_.insert(QLatin1String(kLibrarySortKey), key);
  return save();
}

bool PlayerSettings::setLibraryReadyFirst(bool on) {
  libraryReadyFirst_ = on;
  raw_.insert(QLatin1String(kLibraryReadyFirstKey), on);
  return save();
}

bool PlayerSettings::setLibraryFolders(const QStringList& folders) {
  QStringList clean;
  for (const QString& raw : folders) {
    if (raw.trimmed().isEmpty() || !QDir::isAbsolutePath(raw)) return false;
    const QString f = QDir::cleanPath(raw);
    if (!clean.contains(f)) clean.append(f);
  }
  libraryFolders_ = clean;
  raw_.insert(QLatin1String(kLibraryFoldersKey), QJsonArray::fromStringList(clean));
  return save();
}

bool PlayerSettings::setRomCacheLimitBytes(qint64 bytes) {
  if (bytes < 0) return false;
  romCacheLimit_ = bytes;
  raw_.insert(QLatin1String(kRomCacheLimitKey), static_cast<double>(bytes));
  return save();
}

qint64 PlayerSettings::lastPlayed(const QString& hubId, const QString& gameId) const {
  return static_cast<qint64>(raw_.value(QLatin1String(kLastPlayedKey)).toObject().value(hubId).toObject().value(gameId).toDouble(0));
}

QHash<QString, qint64> PlayerSettings::lastPlayedAll(const QString& hubId) const {
  QHash<QString, qint64> out;
  const QJsonObject hub = raw_.value(QLatin1String(kLastPlayedKey)).toObject().value(hubId).toObject();
  for (auto it = hub.begin(); it != hub.end(); ++it) {
    const qint64 ms = static_cast<qint64>(it.value().toDouble(0));
    if (ms > 0) out.insert(it.key(), ms);
  }
  return out;
}

bool PlayerSettings::setLastPlayed(const QString& hubId, const QString& gameId, qint64 msecs) {
  if (hubId.isEmpty() || gameId.isEmpty() || msecs <= 0) return false;
  QJsonObject all = raw_.value(QLatin1String(kLastPlayedKey)).toObject();
  QJsonObject hub = all.value(hubId).toObject();
  hub.insert(gameId, static_cast<double>(msecs));
  all.insert(hubId, hub);
  raw_.insert(QLatin1String(kLastPlayedKey), all);
  return save();
}

QHash<QString, QString> PlayerSettings::pluginHudCorners() const {
  QHash<QString, QString> out;
  const QJsonObject o = raw_.value(QLatin1String(kPluginHudCornersKey)).toObject();
  for (auto it = o.begin(); it != o.end(); ++it) {
    const QString c = it.value().toString();
    if (c == QLatin1String("top-left") || c == QLatin1String("bottom-left") || c == QLatin1String("bottom-right")) out.insert(it.key(), c);
  }
  return out;
}

bool PlayerSettings::setPluginHudCorner(const QString& pluginId, const QString& corner) {
  if (pluginId.isEmpty() || (corner != QLatin1String("top-left") && corner != QLatin1String("bottom-left") && corner != QLatin1String("bottom-right")))
    return false;
  QJsonObject o = raw_.value(QLatin1String(kPluginHudCornersKey)).toObject();
  o.insert(pluginId, corner);
  raw_.insert(QLatin1String(kPluginHudCornersKey), o);
  return save();
}

QStringList PlayerSettings::pluginHudHidden() const {
  QStringList out;
  const QJsonArray a = raw_.value(QLatin1String(kPluginHudHiddenKey)).toArray();
  for (const QJsonValue& v : a)
    if (v.isString() && !v.toString().isEmpty()) out << v.toString();
  return out;
}

bool PlayerSettings::setPluginHudHidden(const QString& pluginId, bool hidden) {
  if (pluginId.isEmpty()) return false;
  QStringList ids = pluginHudHidden();
  ids.removeAll(pluginId);
  if (hidden) ids << pluginId;
  raw_.insert(QLatin1String(kPluginHudHiddenKey), QJsonArray::fromStringList(ids));
  return save();
}

bool PlayerSettings::setPluginHudAllHidden(bool hidden) {
  pluginHudAllHidden_ = hidden;
  raw_.insert(QLatin1String(kPluginHudAllHiddenKey), hidden);
  return save();
}

QHash<QString, QString> PlayerSettings::pluginToastMutes() const {
  QHash<QString, QString> out;
  const QJsonObject o = raw_.value(QLatin1String(kPluginToastMuteKey)).toObject();
  for (auto it = o.begin(); it != o.end(); ++it) {
    const QString m = it.value().toString();
    if (m == QLatin1String("always") || m == QLatin1String("important")) out.insert(it.key(), m);
  }
  return out;
}

bool PlayerSettings::setPluginToastMute(const QString& pluginId, const QString& mode) {
  if (pluginId.isEmpty() || (!mode.isEmpty() && mode != QLatin1String("always") && mode != QLatin1String("important"))) return false;
  QJsonObject o = raw_.value(QLatin1String(kPluginToastMuteKey)).toObject();
  if (mode.isEmpty()) o.remove(pluginId);
  else o.insert(pluginId, mode);
  raw_.insert(QLatin1String(kPluginToastMuteKey), o);
  return save();
}

QHash<QString, QString> PlayerSettings::pluginHotkeyOverrides() const {
  QHash<QString, QString> out;
  const QJsonObject o = raw_.value(QLatin1String(kPluginHotkeysKey)).toObject();
  for (auto it = o.begin(); it != o.end(); ++it)
    if (it.value().isString() && !it.value().toString().isEmpty()) out.insert(it.key(), it.value().toString());
  return out;
}

bool PlayerSettings::setPluginHotkeyOverride(const QString& pluginId, const QString& hotkeyId, const QString& key) {
  if (pluginId.isEmpty() || hotkeyId.isEmpty()) return false;
  QJsonObject o = raw_.value(QLatin1String(kPluginHotkeysKey)).toObject();
  const QString k = pluginId + QLatin1Char('/') + hotkeyId;
  if (key.isEmpty()) o.remove(k);
  else o.insert(k, key);
  if (o.isEmpty()) raw_.remove(QLatin1String(kPluginHotkeysKey));
  else raw_.insert(QLatin1String(kPluginHotkeysKey), o);
  return save();
}

bool PlayerSettings::setSessionVisibility(const QString& visibility) {
  if (visibility != QLatin1String("private") && visibility != QLatin1String("hub_users") && visibility != QLatin1String("invite_only")) {
    return false;
  }
  sessionVisibility_ = visibility;
  raw_.insert(QLatin1String(kSessionVisibilityKey), visibility);
  return save();
}

QString PlayerSettings::saveSlot(const QString& hubId, const QString& gameId) const {
  const QString slot = raw_.value(QLatin1String(kSaveSlotsKey)).toObject().value(hubId).toObject().value(gameId).toString();
  return SaveStore::isValidSlotName(slot) ? slot : QStringLiteral("default");
}

bool PlayerSettings::setSaveSlot(const QString& hubId, const QString& gameId, const QString& slot) {
  if (!SaveStore::isValidSlotName(slot) || hubId.isEmpty() || gameId.isEmpty()) {
    return false;
  }
  QJsonObject all = raw_.value(QLatin1String(kSaveSlotsKey)).toObject();
  QJsonObject hub = all.value(hubId).toObject();
  if (slot == QLatin1String("default")) {
    hub.remove(gameId);
  } else {
    hub.insert(gameId, slot);
  }
  if (hub.isEmpty()) {
    all.remove(hubId);
  } else {
    all.insert(hubId, hub);
  }
  if (all.isEmpty()) {
    raw_.remove(QLatin1String(kSaveSlotsKey));
  } else {
    raw_.insert(QLatin1String(kSaveSlotsKey), all);
  }
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
