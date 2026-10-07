#include "system_manifest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

// Explicitly initialize the Qt resource from a static library.
static void initManifestResources() { Q_INIT_RESOURCE(framebeam_manifests); }

namespace framebeam::emu {

namespace {
const char kBuiltinDir[] = ":/framebeam/emulation/manifests";

QString normExt(QString e) {
  e = e.trimmed().toLower();
  if (!e.isEmpty() && !e.startsWith(QLatin1Char('.'))) e.prepend(QLatin1Char('.'));
  return e;
}
}  // namespace

QSize DisplayProfile::frameSize() const {
  int w = 0, h = 0;
  const bool vertical = layout == QLatin1String("vertical");
  for (int i = 0; i < screens.size(); ++i) {
    const ScreenSpec& s = screens.at(i);
    const int g = i > 0 ? gap : 0;
    if (vertical) {
      w = std::max(w, s.width);
      h += s.height + g;
    } else {
      w += s.width + g;
      h = std::max(h, s.height);
    }
  }
  return {w, h};
}

QRect DisplayProfile::screenRect(int index) const {
  if (index < 0 || index >= screens.size()) return {};
  const bool vertical = layout == QLatin1String("vertical");
  int x = 0, y = 0;
  for (int i = 0; i < index; ++i) {
    if (vertical) y += screens.at(i).height + gap;
    else x += screens.at(i).width + gap;
  }
  return {x, y, screens.at(index).width, screens.at(index).height};
}

int DisplayProfile::touchScreenIndex() const {
  for (int i = 0; i < screens.size(); ++i)
    if (screens.at(i).touch) return i;
  return -1;
}

QPointF DisplayProfile::toFrameNormalized(int screenIndex, QPointF n) const {
  const QSize fs = frameSize();
  const QRect r = screenRect(screenIndex);
  if (fs.isEmpty() || r.isEmpty()) return {};
  return {(r.x() + n.x() * r.width()) / fs.width(), (r.y() + n.y() * r.height()) / fs.height()};
}

const FirmwareFile* FirmwareSpec::fileById(const QString& id) const {
  for (const FirmwareFile& f : files)
    if (f.id == id) return &f;
  return nullptr;
}

bool SystemManifest::supportsExtension(const QString& ext) const { return extensions.contains(normExt(ext)); }

std::optional<SystemManifest> ManifestRegistry::parse(const QByteArray& json, QString* error) {
  auto fail = [&](const QString& m) -> std::optional<SystemManifest> {
    if (error) *error = m;
    return std::nullopt;
  };
  QJsonParseError pe;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) return fail(QStringLiteral("Invalid JSON: ") + pe.errorString());
  const QJsonObject o = doc.object();

  SystemManifest m;
  struct Req { const char* key; QString* dst; };
  for (const Req& r : {Req{"system_id", &m.systemId}, Req{"display_name", &m.displayName},
                       Req{"core_id", &m.coreId}, Req{"core_library_basename", &m.coreLibraryBasename}}) {
    *r.dst = o.value(QLatin1String(r.key)).toString().trimmed();
    if (r.dst->isEmpty()) return fail(QStringLiteral("Missing required field: ") + QLatin1String(r.key));
  }
  for (const QJsonValue& v : o.value(QLatin1String("extensions")).toArray()) {
    const QString e = normExt(v.toString());
    if (!e.isEmpty() && e != QLatin1String(".")) m.extensions.append(e);
  }
  if (m.extensions.isEmpty()) return fail(QStringLiteral("Missing required field: extensions"));

  m.coreDisplayName = o.value(QLatin1String("core_display_name")).toString(m.coreId);

  const QJsonObject fw = o.value(QLatin1String("firmware")).toObject();
  m.firmware.required = fw.value(QLatin1String("required")).toBool(false);
  m.firmware.sysfileOption = fw.value(QLatin1String("sysfile_option")).toString();
  m.firmware.sysfileNative = fw.value(QLatin1String("sysfile_native")).toString(m.firmware.sysfileNative);
  m.firmware.sysfileBuiltin = fw.value(QLatin1String("sysfile_builtin")).toString(m.firmware.sysfileBuiltin);
  for (const QJsonValue& v : fw.value(QLatin1String("files")).toArray()) {
    const QJsonObject f = v.toObject();
    FirmwareFile ff;
    ff.name = f.value(QLatin1String("name")).toString();
    // The name ends up in the system directory: plain file name only.
    if (ff.name.isEmpty() || ff.name.contains(QLatin1Char('/')) || ff.name.contains(QLatin1Char('\\')) ||
        ff.name.startsWith(QLatin1Char('.'))) {
      return fail(QStringLiteral("firmware.files: invalid name"));
    }
    ff.id = f.value(QLatin1String("id")).toString(QFileInfo(ff.name).completeBaseName());
    ff.required = f.value(QLatin1String("required")).toBool(false);
    ff.coreOption = f.value(QLatin1String("core_option")).toString();
    m.firmware.files.append(ff);
  }

  m.inputProfile = o.value(QLatin1String("input_profile")).toString();
  m.displayProfile = o.value(QLatin1String("display_profile")).toString();
  const QJsonObject labels = o.value(QLatin1String("labels")).toObject();
  m.inputLabel = labels.value(QLatin1String("input")).toString(m.displayName);
  m.touchLabel = labels.value(QLatin1String("touch")).toString(QStringLiteral("Touch"));

  const QJsonObject d = o.value(QLatin1String("display")).toObject();
  m.display.layout = d.value(QLatin1String("layout")).toString(QStringLiteral("single"));
  m.display.gap = d.value(QLatin1String("gap")).toInt(0);
  for (const QJsonValue& v : d.value(QLatin1String("screens")).toArray()) {
    const QJsonObject s = v.toObject();
    ScreenSpec sp{s.value(QLatin1String("id")).toString(), s.value(QLatin1String("width")).toInt(),
                  s.value(QLatin1String("height")).toInt(), s.value(QLatin1String("touch")).toBool(false)};
    if (sp.width <= 0 || sp.height <= 0) return fail(QStringLiteral("display.screens: invalid size"));
    m.display.screens.append(sp);
  }
  if (m.display.screens.isEmpty()) return fail(QStringLiteral("Missing required field: display.screens"));
  if (m.display.layout != QLatin1String("single") && m.display.layout != QLatin1String("vertical") &&
      m.display.layout != QLatin1String("horizontal"))
    return fail(QStringLiteral("display.layout unknown: ") + m.display.layout);

  const QJsonObject co = o.value(QLatin1String("core_options")).toObject();
  for (auto it = co.begin(); it != co.end(); ++it) m.coreOptions.insert(it.key(), it.value().toString());
  for (const QJsonValue& v : o.value(QLatin1String("locked_core_options")).toArray()) {
    if (v.isString()) m.lockedCoreOptions.append(v.toString());
  }
  for (const QJsonValue& v : o.value(QLatin1String("always_shown_core_options")).toArray()) {
    if (v.isString()) m.alwaysShownCoreOptions.append(v.toString());
  }
  return m;
}

bool ManifestRegistry::add(const SystemManifest& manifest, QString* error) {
  if (find(manifest.systemId)) {
    if (error) *error = QStringLiteral("Duplicate system_id: ") + manifest.systemId;
    return false;
  }
  m_manifests.append(manifest);
  return true;
}

bool ManifestRegistry::loadDirectory(const QString& dir, QString* error) {
  const QDir d(dir);
  const QStringList files = d.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
  for (const QString& name : files) {
    QFile f(d.filePath(name));
    if (!f.open(QIODevice::ReadOnly)) {
      if (error) *error = name + QStringLiteral(": ") + f.errorString();
      return false;
    }
    QString err;
    const auto m = parse(f.readAll(), &err);
    if (!m || !add(*m, &err)) {
      if (error) *error = name + QStringLiteral(": ") + err;
      return false;
    }
  }
  return true;
}

bool ManifestRegistry::loadBuiltin(QString* error) {
  initManifestResources();
  return loadDirectory(QString::fromLatin1(kBuiltinDir), error);
}

const SystemManifest* ManifestRegistry::find(const QString& systemId) const {
  for (const SystemManifest& m : m_manifests)
    if (m.systemId == systemId) return &m;
  return nullptr;
}

const SystemManifest* ManifestRegistry::forExtension(const QString& ext) const {
  for (const SystemManifest& m : m_manifests)
    if (m.supportsExtension(ext)) return &m;
  return nullptr;
}

}  // namespace framebeam::emu
