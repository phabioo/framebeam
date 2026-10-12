#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>

#include "semver.h"

// Plugin manifest (manifest.json, docs/architecture/12-plugins.md 16.6). Pure parsing/validation, no Lua.
namespace framebeam::plugins {

inline constexpr int kPluginApiVersion = 1;

// Version range of a `requires` entry: "^1.2", "~1.2", ">=1.0", "1.2.3", "*". Partial versions are padded with zeros.
// ^ : >= v and below the next change of the left-most non-zero part (^1.2 -> <2.0.0, ^0.2 -> <0.3.0).
// ~ : >= v and below the next minor (~1.2 -> <1.3.0; ~1 -> <2.0.0).
struct VersionRange {
  enum class Kind { Any, Exact, Bounded };
  Kind kind = Kind::Any;
  update::SemVer lo;                  // inclusive (Exact: the version, Bounded: lower bound)
  std::optional<update::SemVer> hi;   // exclusive (Bounded only)
  QString text = QStringLiteral("*");

  static std::optional<VersionRange> parse(const QString& text);
  bool matches(const update::SemVer& v) const;
};

struct GamePattern {
  QString system;      // case-insensitive, e.g. "nds"
  QStringList codes;   // globs with * and ?; empty = every game of the system
  bool matches(const QString& sys, const QString& code) const;
};

// Case-insensitive glob (* and ?) over a whole string.
bool globMatch(const QString& pattern, const QString& text);

struct Dependency {
  QString id;
  VersionRange range;
};

struct Manifest {
  QString id;
  QString name;
  update::SemVer version;
  int api = kPluginApiVersion;
  QString type = QStringLiteral("script");
  QList<Dependency> dependencies;  // manifest field "requires"
  QStringList provides;
  QList<GamePattern> games;        // empty = all games
  QStringList permissions;
  QString entry;                   // relative path of the .lua entry file
  QStringList assets;
  QJsonObject settings;            // raw, schema not interpreted yet

  bool matchesGame(const QString& system, const QString& code) const;
  bool hasPermission(const QString& p) const { return permissions.contains(p); }
  const Dependency* dependency(const QString& pluginId) const;
};

struct ManifestResult {
  bool ok = false;
  Manifest manifest;
  QString error;  // clear English message when !ok
};

// Parses and validates `json`. When `pluginDir` is not empty, `entry` must exist inside it.
ManifestResult parseManifest(const QByteArray& json, const QString& pluginDir = QString());
// Reads <dir>/manifest.json and validates it against <dir>.
ManifestResult loadManifest(const QString& dir);

// A plugin as known to the manager and handed to the host.
struct PluginInfo {
  Manifest manifest;
  QString origin = QStringLiteral("local");  // "official" (valid FrameBeam signature) | "unverified" (unsigned Hub copy) | "local"
  bool hubCopy = false;                      // installed under hub/<id>/<version> (delivered by the Hub)
  QString sha256;                            // hub copy: SHA-256 of the .fbplugin file it was installed from
  QString path;                              // directory the plugin runs from
  QString cacheDir;                          // per-plugin cache directory (created on demand)
  bool enabled = false;
  bool devLinked = false;                    // runs straight from a linked developer folder
  QStringList problems;                      // why it cannot start (manifest or dependency problems)
};

}  // namespace framebeam::plugins
