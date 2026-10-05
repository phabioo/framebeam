#include "hubprotocol.h"

#include <QJsonArray>
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
  return r;
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
