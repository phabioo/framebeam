#include "hubprotocol.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSysInfo>

#include "romcache.h"
#include "version.h"

namespace framebeam {

std::optional<HubInfo> parseHubInfo(const QJsonObject& obj) {
  HubInfo i;
  i.hubId = obj.value(QStringLiteral("hub_id")).toString();
  i.name = obj.value(QStringLiteral("name")).toString();
  i.hubVersion = obj.value(QStringLiteral("hub_version")).toString();
  i.protocolVersion = obj.value(QStringLiteral("protocol_version")).toInt(0);
  i.minProtocolVersion = obj.value(QStringLiteral("min_protocol_version")).toInt(0);
  i.apiBase = obj.value(QStringLiteral("api_base")).toString();
  if (i.hubId.isEmpty() || i.protocolVersion < 1 || i.minProtocolVersion < 1) {
    return std::nullopt;
  }
  return i;
}

HandshakeInfo HandshakeInfo::detect() {
  HandshakeInfo h;
#if defined(Q_OS_WIN)
  h.platform = QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
  h.platform = QStringLiteral("macos");
#elif defined(Q_OS_LINUX)
  h.platform = QStringLiteral("linux");
#else
  h.platform = QSysInfo::productType();
#endif
  h.arch = QSysInfo::currentCpuArchitecture();
  const std::string_view ver = framebeam::playerVersion();
  h.playerVersion = QString::fromUtf8(ver.data(), static_cast<qsizetype>(ver.size()));
  return h;
}

QJsonObject HandshakeInfo::toJson() const {
  QJsonArray coreArr;
  for (const CoreInfo& c : cores) {
    coreArr.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("version"), c.version}});
  }
  return QJsonObject{
      {QStringLiteral("platform"), platform},
      {QStringLiteral("arch"), arch},
      {QStringLiteral("player_version"), playerVersion},
      {QStringLiteral("protocol_version"), protocolVersion},
      {QStringLiteral("min_protocol_version"), minProtocolVersion},
      {QStringLiteral("cores"), coreArr},
      {QStringLiteral("video"),
       QJsonObject{{QStringLiteral("h264_encode"), h264Encode},
                   {QStringLiteral("h264_decode"), h264Decode},
                   {QStringLiteral("encoders"), QJsonArray::fromStringList(encoders)}}},
      {QStringLiteral("audio"), QJsonObject{{QStringLiteral("opus"), opus}}},
      {QStringLiteral("input"), QJsonObject{{QStringLiteral("gamepad"), gamepad}, {QStringLiteral("keyboard"), keyboard}}},
  };
}

std::optional<HandshakeResult> parseHandshakeResult(const QJsonObject& obj) {
  if (!obj.contains(QStringLiteral("compatible"))) {
    return std::nullopt;
  }
  HandshakeResult r;
  r.hubVersion = obj.value(QStringLiteral("hub_version")).toString();
  r.protocolVersion = obj.value(QStringLiteral("protocol_version")).toInt(0);
  r.minProtocolVersion = obj.value(QStringLiteral("min_protocol_version")).toInt(0);
  r.compatible = obj.value(QStringLiteral("compatible")).toBool(false);
  const QJsonArray arr = obj.value(QStringLiteral("problems")).toArray();
  for (const QJsonValue& v : arr) {
    const QJsonObject p = v.toObject();
    r.problems.append({p.value(QStringLiteral("code")).toString(), p.value(QStringLiteral("detail")).toString(),
                       p.value(QStringLiteral("core_id")).toString()});
  }
  const QJsonArray feats = obj.value(QStringLiteral("features")).toArray();
  for (const QJsonValue& v : feats) {
    if (v.isString()) {
      r.features.append(v.toString());
    }
  }
  const QJsonObject user = obj.value(QStringLiteral("user")).toObject();
  r.userId = user.value(QStringLiteral("id")).toString();
  r.userDisplayName = user.value(QStringLiteral("display_name")).toString().trimmed();
  r.userRole = user.value(QStringLiteral("role")).toString();
  return r;
}

const FirmwareFileInfo* SystemInfo::file(const QString& fileId) const {
  for (const FirmwareFileInfo& f : firmware) {
    if (f.id == fileId) {
      return &f;
    }
  }
  return nullptr;
}

const SystemCore* SystemInfo::core(const QString& coreId) const {
  for (const SystemCore& c : cores) {
    if (c.coreId == coreId) {
      return &c;
    }
  }
  return nullptr;
}

SystemCore SystemInfo::defaultCore() const {
  if (const SystemCore* c = core(defaultCoreId)) {
    return *c;
  }
  if (!cores.isEmpty() && defaultCoreId.isEmpty() && preferredCoreId.isEmpty()) {
    return cores.first();
  }
  SystemCore legacy;  // Hub without cores_v2 (or a default that is not listed): the preferred core
  if (!preferredCoreId.isEmpty() && !corePackageVersion.isEmpty()) {
    legacy.coreId = preferredCoreId;
    legacy.version = corePackageVersion;
    legacy.displayName = preferredCoreId;
    if (const SystemCore* c = core(preferredCoreId)) {
      legacy = *c;
    }
  }
  return legacy;
}

std::optional<SystemInfo> parseSystemInfo(const QJsonObject& obj) {
  SystemInfo s;
  s.id = obj.value(QStringLiteral("id")).toString();
  s.displayName = obj.value(QStringLiteral("display_name")).toString();
  s.preferredCoreId = obj.value(QStringLiteral("preferred_core_id")).toString();
  s.expectedCoreVersion = obj.value(QStringLiteral("expected_core_version")).toString();
  // Unknown mode: the safe reading is "builtin" (nothing is required or downloaded).
  s.firmwareMode = obj.value(QStringLiteral("firmware_mode")).toString() == QLatin1String("native") ? QStringLiteral("native")
                                                                                                  : QStringLiteral("builtin");
  s.corePackageVersion = obj.value(QStringLiteral("core_package_version")).toString();
  if (!s.corePackageVersion.isEmpty() && !isValidCoreVersion(s.corePackageVersion)) {
    s.corePackageVersion.clear();
  }
  if (s.id.isEmpty()) {
    return std::nullopt;
  }
  s.defaultCoreId = obj.value(QStringLiteral("default_core_id")).toString();
  if (!isValidCoreId(s.defaultCoreId)) {
    s.defaultCoreId.clear();
  }
  for (const QJsonValue& v : obj.value(QStringLiteral("cores")).toArray()) {
    const QJsonObject o = v.toObject();
    SystemCore c;
    c.coreId = o.value(QStringLiteral("core_id")).toString();
    c.version = o.value(QStringLiteral("version")).toString();
    if (!isValidCoreId(c.coreId) || !isValidCoreVersion(c.version) || s.core(c.coreId) != nullptr) {
      continue;  // unusable entry: the Player could not provision it
    }
    c.displayName = o.value(QStringLiteral("display_name")).toString(c.coreId);
    c.license = o.value(QStringLiteral("license")).toString();
    c.experimental = o.value(QStringLiteral("experimental")).toBool(false);
    c.requiredHwApi = o.value(QStringLiteral("required_hw_api")).toString();
    c.origin = o.value(QStringLiteral("origin")).toString();
    c.buildDate = o.value(QStringLiteral("build_date")).toString();
    s.cores.append(c);
  }
  for (const QJsonValue& v : obj.value(QStringLiteral("firmware")).toArray()) {
    const QJsonObject o = v.toObject();
    FirmwareFileInfo f;
    f.id = o.value(QStringLiteral("id")).toString();
    if (f.id.isEmpty()) {
      continue;
    }
    f.displayName = o.value(QStringLiteral("display_name")).toString(f.id);
    f.required = o.value(QStringLiteral("required")).toBool(false);
    f.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
    f.sha256 = o.value(QStringLiteral("sha256")).toString();
    // "present" only counts with a usable size and hash (otherwise the Player could not validate it).
    f.present = o.value(QStringLiteral("present")).toBool(false) && f.size > 0 && RomCache::isValidSha256(f.sha256);
    s.firmware.append(f);
  }
  return s;
}

bool isValidCoreId(const QString& id) {
  static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,63}$"));
  return re.match(id).hasMatch();
}

bool isValidCoreVersion(const QString& v) {
  static const QRegularExpression re(QStringLiteral("^[0-9A-Za-z][0-9A-Za-z.+_-]{0,63}$"));
  return re.match(v).hasMatch();
}

bool isValidCorePlatform(const QString& p) {
  static const QStringList all{QStringLiteral("windows-x64"), QStringLiteral("linux-x64"), QStringLiteral("linux-arm64"),
                               QStringLiteral("macos-x64"), QStringLiteral("macos-arm64")};
  return all.contains(p);
}

bool isValidCoreFileName(const QString& n) {
  static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$"));
  return re.match(n).hasMatch() && !n.contains(QLatin1String(".."));
}

const CorePackageFile* CorePackageInfo::library() const {
  for (const CorePackageFile& f : files) {
    if (f.role == QLatin1String("library")) {
      return &f;
    }
  }
  return nullptr;
}

QJsonObject CorePackageInfo::toJson() const {
  QJsonArray arr;
  for (const CorePackageFile& f : files) {
    arr.append(QJsonObject{{QStringLiteral("name"), f.name},
                           {QStringLiteral("role"), f.role},
                           {QStringLiteral("size"), f.size},
                           {QStringLiteral("sha256"), f.sha256},
                           {QStringLiteral("available"), f.available}});
  }
  return QJsonObject{{QStringLiteral("core_id"), coreId},         {QStringLiteral("version"), version},
                     {QStringLiteral("platform"), platform},      {QStringLiteral("license"), license},
                     {QStringLiteral("source_url"), sourceUrl},   {QStringLiteral("source_ref"), sourceRef},
                     {QStringLiteral("files"), arr}};
}

std::optional<CorePackageInfo> parseCorePackage(const QJsonObject& obj) {
  CorePackageInfo p;
  p.coreId = obj.value(QStringLiteral("core_id")).toString();
  p.version = obj.value(QStringLiteral("version")).toString();
  p.platform = obj.value(QStringLiteral("platform")).toString();
  p.license = obj.value(QStringLiteral("license")).toString();
  p.sourceUrl = obj.value(QStringLiteral("source_url")).toString();
  p.sourceRef = obj.value(QStringLiteral("source_ref")).toString();
  if (!isValidCoreId(p.coreId) || !isValidCoreVersion(p.version) || !isValidCorePlatform(p.platform)) {
    return std::nullopt;
  }
  int libraries = 0;
  QStringList names;
  for (const QJsonValue& v : obj.value(QStringLiteral("files")).toArray()) {
    const QJsonObject o = v.toObject();
    CorePackageFile f;
    f.name = o.value(QStringLiteral("name")).toString();
    f.role = o.value(QStringLiteral("role")).toString();
    f.size = o.value(QStringLiteral("size")).toVariant().toLongLong();
    f.sha256 = o.value(QStringLiteral("sha256")).toString();
    f.available = o.value(QStringLiteral("available")).toBool(false);
    constexpr qint64 kMaxCoreFile = 512LL * 1024 * 1024;
    if (!isValidCoreFileName(f.name) || names.contains(f.name) || (f.role != QLatin1String("library") && f.role != QLatin1String("license")) ||
        f.size <= 0 || f.size > kMaxCoreFile || !RomCache::isValidSha256(f.sha256)) {
      return std::nullopt;
    }
    names.append(f.name);
    if (f.role == QLatin1String("library")) {
      ++libraries;
    }
    p.files.append(f);
  }
  if (libraries != 1) {
    return std::nullopt;
  }
  return p;
}

std::optional<GameEntry> parseGame(const QJsonObject& obj) {
  GameEntry g;
  g.id = obj.value(QStringLiteral("id")).toString();
  g.title = obj.value(QStringLiteral("title")).toString();
  g.system = obj.value(QStringLiteral("system")).toString();
  const QJsonObject rom = obj.value(QStringLiteral("rom")).toObject();
  g.romSha256 = rom.value(QStringLiteral("sha256")).toString();
  g.romSize = rom.value(QStringLiteral("size")).toVariant().toLongLong();
  g.romFilename = rom.value(QStringLiteral("filename")).toString();
  g.uploadedBy = obj.value(QStringLiteral("uploaded_by")).toString();
  g.addedAt = obj.value(QStringLiteral("added_at")).toString();
  if (g.id.isEmpty() || !RomCache::isValidSha256(g.romSha256) || g.romSize < 0) {
    return std::nullopt;
  }
  return g;
}

}  // namespace framebeam
