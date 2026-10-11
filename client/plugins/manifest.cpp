#include "manifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <set>

namespace framebeam::plugins {

using update::SemVer;

namespace {

constexpr qint64 kMaxManifestBytes = 256 * 1024;

bool validId(const QString& id) {
  static const QRegularExpression re(QStringLiteral("^[a-z0-9-]+(\\.[a-z0-9-]+)+$"));
  return id.size() <= 128 && re.match(id).hasMatch();
}

// Pads "1", "1.2", "1.2.3" to three parts. Returns false for anything else (pre-release only on full versions).
bool parseLooseVersion(const QString& s, SemVer& out, int& parts) {
  const QString core = s.section(QLatin1Char('-'), 0, 0);
  const QStringList p = core.split(QLatin1Char('.'));
  if (p.size() < 1 || p.size() > 3) return false;
  parts = static_cast<int>(p.size());
  if (parts < 3 && s.contains(QLatin1Char('-'))) return false;
  qint64 n[3] = {0, 0, 0};
  for (int i = 0; i < p.size(); ++i) {
    bool ok = false;
    n[i] = p[i].toLongLong(&ok);
    if (!ok || n[i] < 0 || p[i].size() > 9) return false;
  }
  QString full = QStringLiteral("%1.%2.%3").arg(n[0]).arg(n[1]).arg(n[2]);
  if (parts == 3 && s.contains(QLatin1Char('-'))) full += s.mid(core.size());
  auto v = SemVer::parse(full);
  if (!v) return false;
  out = *v;
  return true;
}

bool relativeSafePath(const QString& p) {
  if (p.isEmpty() || p.startsWith(QLatin1Char('/')) || p.startsWith(QLatin1Char('\\')) || p.contains(QLatin1Char('\\')) ||
      p.contains(QLatin1Char(':')) || p.contains(QLatin1Char('\0')))
    return false;
  for (const QString& seg : p.split(QLatin1Char('/'))) {
    if (seg == QLatin1String("..")) return false;
  }
  return true;
}

QString permissionError(const QString& p) {
  static const std::set<QString> supported = {QStringLiteral("memory.read"), QStringLiteral("rom.read"), QStringLiteral("ui"),
                                              QStringLiteral("run.state")};
  if (supported.count(p)) return {};
  if (p == QLatin1String("memory.write") || p == QLatin1String("tool.exec"))
    return QStringLiteral("permission %1 is not available yet").arg(p);
  return QStringLiteral("unknown permission '%1'").arg(p);
}

ManifestResult fail(const QString& msg) {
  ManifestResult r;
  r.error = msg;
  return r;
}

}  // namespace

std::optional<VersionRange> VersionRange::parse(const QString& textIn) {
  const QString t = textIn.trimmed();
  VersionRange r;
  r.text = t;
  if (t == QLatin1String("*") || t.isEmpty()) {
    r.kind = Kind::Any;
    r.text = QStringLiteral("*");
    return r;
  }
  int parts = 0;
  if (t.startsWith(QLatin1Char('^')) || t.startsWith(QLatin1Char('~'))) {
    const bool caret = t.startsWith(QLatin1Char('^'));
    SemVer v;
    if (!parseLooseVersion(t.mid(1).trimmed(), v, parts)) return std::nullopt;
    SemVer hi;
    if (caret) {
      if (v.major > 0 || parts == 1) {
        hi.major = v.major + 1;
      } else if (v.minor > 0 || parts == 2) {
        hi.minor = v.minor + 1;
      } else {
        hi.patch = v.patch + 1;
      }
    } else {
      if (parts == 1) {
        hi.major = v.major + 1;
      } else {
        hi.major = v.major;
        hi.minor = v.minor + 1;
      }
    }
    // The lower-bound pre-release (if any) is kept; the upper bound is "-0" to also exclude its pre-releases.
    hi.pre = QStringList{QStringLiteral("0")};
    r.kind = Kind::Bounded;
    r.lo = v;
    r.hi = hi;
    return r;
  }
  if (t.startsWith(QLatin1String(">="))) {
    SemVer v;
    if (!parseLooseVersion(t.mid(2).trimmed(), v, parts)) return std::nullopt;
    r.kind = Kind::Bounded;
    r.lo = v;
    return r;
  }
  SemVer v;
  if (!parseLooseVersion(t, v, parts)) return std::nullopt;
  r.kind = Kind::Exact;
  r.lo = v;
  return r;
}

bool VersionRange::matches(const SemVer& v) const {
  switch (kind) {
    case Kind::Any: return true;
    case Kind::Exact: return SemVer::compare(v, lo) == 0;
    case Kind::Bounded:
      if (SemVer::compare(v, lo) < 0) return false;
      return !hi || SemVer::compare(v, *hi) < 0;
  }
  return false;
}

bool globMatch(const QString& pattern, const QString& text) {
  const QString p = pattern.toLower();
  const QString s = text.toLower();
  qsizetype pi = 0, si = 0, star = -1, mark = 0;
  while (si < s.size()) {
    if (pi < p.size() && (p[pi] == QLatin1Char('?') || p[pi] == s[si])) {
      ++pi;
      ++si;
    } else if (pi < p.size() && p[pi] == QLatin1Char('*')) {
      star = pi++;
      mark = si;
    } else if (star >= 0) {
      pi = star + 1;
      si = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == QLatin1Char('*')) ++pi;
  return pi == p.size();
}

bool GamePattern::matches(const QString& sys, const QString& code) const {
  if (system.compare(sys, Qt::CaseInsensitive) != 0) return false;
  if (codes.isEmpty()) return true;
  for (const QString& c : codes)
    if (globMatch(c, code)) return true;
  return false;
}

bool Manifest::matchesGame(const QString& system, const QString& code) const {
  if (games.isEmpty()) return true;
  for (const GamePattern& g : games)
    if (g.matches(system, code)) return true;
  return false;
}

const Dependency* Manifest::dependency(const QString& pluginId) const {
  for (const Dependency& d : dependencies)
    if (d.id == pluginId) return &d;
  return nullptr;
}

ManifestResult parseManifest(const QByteArray& json, const QString& pluginDir) {
  if (json.size() > kMaxManifestBytes) return fail(QStringLiteral("manifest.json is larger than 256 KiB"));
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (doc.isNull() || !doc.isObject())
    return fail(QStringLiteral("manifest.json is not valid JSON: %1").arg(pe.error == QJsonParseError::NoError ? QStringLiteral("not an object") : pe.errorString()));
  const QJsonObject o = doc.object();

  auto str = [&](const char* key, QString& out, bool required) -> QString {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull()) return required ? QStringLiteral("manifest field '%1' is required").arg(QLatin1String(key)) : QString();
    if (!v.isString()) return QStringLiteral("manifest field '%1' must be a string").arg(QLatin1String(key));
    out = v.toString();
    if (required && out.trimmed().isEmpty()) return QStringLiteral("manifest field '%1' must not be empty").arg(QLatin1String(key));
    return {};
  };

  Manifest m;
  // The API version is checked first: a plugin written for another API may have a different shape.
  if (!o.contains(QLatin1String("api"))) return fail(QStringLiteral("manifest field 'api' is required"));
  const QJsonValue apiV = o.value(QLatin1String("api"));
  QString nameForMsg = o.value(QLatin1String("name")).isString() ? o.value(QLatin1String("name")).toString() : QString();
  if (nameForMsg.isEmpty()) nameForMsg = o.value(QLatin1String("id")).isString() ? o.value(QLatin1String("id")).toString() : QStringLiteral("This plugin");
  if (!apiV.isDouble() || apiV.toDouble() != static_cast<double>(static_cast<int>(apiV.toDouble())))
    return fail(QStringLiteral("manifest field 'api' must be an integer"));
  m.api = apiV.toInt();
  if (m.api != kPluginApiVersion)
    return fail(QStringLiteral("%1 needs plugin API %2; this Player supports API %3").arg(nameForMsg).arg(m.api).arg(kPluginApiVersion));

  if (QString e = str("id", m.id, true); !e.isEmpty()) return fail(e);
  if (!validId(m.id))
    return fail(QStringLiteral("invalid plugin id '%1': use reverse-DNS notation with lowercase letters, digits, '-' and '.', e.g. net.example.tracker").arg(m.id));
  if (QString e = str("name", m.name, true); !e.isEmpty()) return fail(e);
  if (m.name.size() > 100) return fail(QStringLiteral("manifest field 'name' is longer than 100 characters"));

  QString versionText;
  if (QString e = str("version", versionText, true); !e.isEmpty()) return fail(e);
  const auto ver = SemVer::parse(versionText);
  if (!ver) return fail(QStringLiteral("invalid version '%1': use SemVer, e.g. 1.0.0").arg(versionText));
  m.version = *ver;

  if (QString e = str("type", m.type, false); !e.isEmpty()) return fail(e);
  if (m.type.isEmpty()) m.type = QStringLiteral("script");
  if (m.type == QLatin1String("tool")) return fail(QStringLiteral("tool plugins are not supported yet"));
  if (m.type != QLatin1String("script")) return fail(QStringLiteral("unknown plugin type '%1'").arg(m.type));

  const QJsonValue req = o.value(QLatin1String("requires"));
  if (!req.isUndefined() && !req.isNull()) {
    if (!req.isObject()) return fail(QStringLiteral("manifest field 'requires' must be an object of plugin id to version range"));
    const QJsonObject ro = req.toObject();
    for (auto it = ro.begin(); it != ro.end(); ++it) {
      if (!validId(it.key())) return fail(QStringLiteral("requires: invalid plugin id '%1'").arg(it.key()));
      if (it.key() == m.id) return fail(QStringLiteral("a plugin cannot require itself"));
      if (!it.value().isString()) return fail(QStringLiteral("requires: the range for %1 must be a string").arg(it.key()));
      const auto range = VersionRange::parse(it.value().toString());
      if (!range) return fail(QStringLiteral("requires: invalid version range '%1' for %2 (use ^1.2, ~1.2, >=1.0, 1.2.3 or *)").arg(it.value().toString(), it.key()));
      m.dependencies.append({it.key(), *range});
    }
  }

  auto stringList = [&](const char* key, QStringList& out, QString& err) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isUndefined() || v.isNull()) return;
    if (!v.isArray()) {
      err = QStringLiteral("manifest field '%1' must be an array of strings").arg(QLatin1String(key));
      return;
    }
    for (const QJsonValue& e : v.toArray()) {
      if (!e.isString() || e.toString().isEmpty()) {
        err = QStringLiteral("manifest field '%1' must be an array of non-empty strings").arg(QLatin1String(key));
        return;
      }
      out.append(e.toString());
    }
  };

  QString err;
  stringList("provides", m.provides, err);
  if (!err.isEmpty()) return fail(err);
  for (const QString& p : std::as_const(m.provides))
    if (p.size() > 128 || p.contains(QLatin1Char(' '))) return fail(QStringLiteral("invalid provided interface name '%1'").arg(p));

  QStringList perms;
  stringList("permissions", perms, err);
  if (!err.isEmpty()) return fail(err);
  for (const QString& p : std::as_const(perms)) {
    if (QString e = permissionError(p); !e.isEmpty()) return fail(e);
    if (!m.permissions.contains(p)) m.permissions.append(p);
  }

  const QJsonValue gamesV = o.value(QLatin1String("games"));
  if (!gamesV.isUndefined() && !gamesV.isNull()) {
    if (!gamesV.isArray()) return fail(QStringLiteral("manifest field 'games' must be an array"));
    for (const QJsonValue& gv : gamesV.toArray()) {
      if (!gv.isObject()) return fail(QStringLiteral("games: every entry must be an object with 'system' and 'codes'"));
      const QJsonObject go = gv.toObject();
      GamePattern g;
      if (!go.value(QLatin1String("system")).isString() || go.value(QLatin1String("system")).toString().isEmpty())
        return fail(QStringLiteral("games: every entry needs a 'system' string"));
      g.system = go.value(QLatin1String("system")).toString();
      const QJsonValue cv = go.value(QLatin1String("codes"));
      if (!cv.isUndefined() && !cv.isNull()) {
        if (!cv.isArray()) return fail(QStringLiteral("games: 'codes' must be an array of glob strings"));
        for (const QJsonValue& c : cv.toArray()) {
          if (!c.isString() || c.toString().isEmpty()) return fail(QStringLiteral("games: 'codes' must contain non-empty strings"));
          g.codes.append(c.toString());
        }
      }
      m.games.append(g);
    }
  }

  if (QString e = str("entry", m.entry, true); !e.isEmpty()) return fail(e);
  if (!relativeSafePath(m.entry) || !m.entry.endsWith(QLatin1String(".lua")))
    return fail(QStringLiteral("invalid entry '%1': use a relative .lua path inside the plugin folder, without '..'").arg(m.entry));
  if (!pluginDir.isEmpty()) {
    const QDir dir(pluginDir);
    const QFileInfo fi(dir.filePath(m.entry));
    if (!fi.isFile()) return fail(QStringLiteral("entry file '%1' does not exist").arg(m.entry));
    const QString base = QDir(dir.canonicalPath()).absolutePath();
    if (!fi.canonicalFilePath().startsWith(base + QLatin1Char('/')))
      return fail(QStringLiteral("entry file '%1' is outside the plugin folder").arg(m.entry));
  }

  stringList("assets", m.assets, err);
  if (!err.isEmpty()) return fail(err);
  for (const QString& a : std::as_const(m.assets))
    if (!relativeSafePath(a)) return fail(QStringLiteral("invalid asset path '%1'").arg(a));

  const QJsonValue sv = o.value(QLatin1String("settings"));
  if (!sv.isUndefined() && !sv.isNull()) {
    if (!sv.isObject()) return fail(QStringLiteral("manifest field 'settings' must be an object"));
    m.settings = sv.toObject();
  }

  ManifestResult r;
  r.ok = true;
  r.manifest = std::move(m);
  return r;
}

ManifestResult loadManifest(const QString& dir) {
  QFile f(QDir(dir).filePath(QStringLiteral("manifest.json")));
  if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("manifest.json not found in %1").arg(dir));
  return parseManifest(f.read(kMaxManifestBytes + 1), dir);
}

}  // namespace framebeam::plugins
