#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace framebeam {

// Protocol per ADR 0002 / protocol/openapi/framebeam.yaml.
inline constexpr int kProtocolVersion = 1;
inline constexpr int kMinProtocolVersion = 1;

struct HubInfo {
  QString hubId;
  QString name;
  QString hubVersion;
  int protocolVersion = 0;
  int minProtocolVersion = 0;
  QString apiBase;
};
std::optional<HubInfo> parseHubInfo(const QJsonObject& obj);

struct CoreInfo {
  QString id;
  QString version;
};

// Handshake data of the player (cores are filled in from outside).
struct HandshakeInfo {
  QString platform;
  QString arch;
  QString playerVersion;
  int protocolVersion = kProtocolVersion;
  int minProtocolVersion = kMinProtocolVersion;
  QList<CoreInfo> cores;
  bool h264Encode = false;
  bool h264Decode = false;
  QStringList encoders;
  bool opus = false;
  bool gamepad = true;
  bool keyboard = true;

  static HandshakeInfo detect();  // platform/arch/player_version prefilled
  QJsonObject toJson() const;
};

struct HandshakeProblem {
  QString code;
  QString detail;
  QString coreId;
};

struct HandshakeResult {
  QString hubVersion;
  int protocolVersion = 0;
  int minProtocolVersion = 0;
  bool compatible = false;
  QList<HandshakeProblem> problems;
  QStringList features;  // optional Hub feature flags, e.g. "saves_v1"
  QString userId;           // optional `user` (older Hubs omit it)
  QString userDisplayName;
  QString userRole;         // "admin" | "user"
};
std::optional<HandshakeResult> parseHandshakeResult(const QJsonObject& obj);

// Systems registry (GET /systems, feature firmware_v1).
struct FirmwareFileInfo {
  QString id;
  QString displayName;
  bool required = false;  // true only in firmware mode "native"
  bool present = false;   // on the Hub with valid size/sha256
  qint64 size = 0;
  QString sha256;
};
// One core the Hub serves for a system (feature cores_v2, ADR 0020 D5).
struct SystemCore {
  QString coreId;
  QString displayName;
  QString version;  // package version the Hub serves; the Player provisions exactly this one
  QString license;
  bool experimental = false;  // Hub's view: no Player core profile
  QString requiredHwApi;      // empty = none
  QString origin;
  QString buildDate;  // YYYY-MM-DD, may be empty
  bool valid() const { return !coreId.isEmpty(); }
};
inline constexpr const char* kCoresV2Feature = "cores_v2";

struct SystemInfo {
  QString id;
  QString displayName;
  QString preferredCoreId;
  QString expectedCoreVersion;  // empty = any
  QString corePackageVersion;   // core version the Hub serves for the preferred core (cores_v1); empty = none
  QString defaultCoreId;        // cores_v2: the Hub's default core; empty = none / Hub without cores_v2
  QList<SystemCore> cores;      // cores_v2: every core the Hub serves for the system
  QString firmwareMode = QStringLiteral("builtin");  // "builtin" | "native"
  QList<FirmwareFileInfo> firmware;

  const SystemCore* core(const QString& coreId) const;
  // The Hub's default core: default_core_id when listed, else the legacy preferred core with corePackageVersion.
  // Empty coreId = the Hub serves no core.
  SystemCore defaultCore() const;

  bool nativeFirmware() const { return firmwareMode == QLatin1String("native"); }
  const FirmwareFileInfo* file(const QString& fileId) const;
};
std::optional<SystemInfo> parseSystemInfo(const QJsonObject& obj);

// Core packages (feature cores_v1): GET /cores/{core_id}/packages/{version}/{platform}.
struct CorePackageFile {
  QString name;
  QString role;  // "library" | "license"
  qint64 size = 0;
  QString sha256;
  bool available = false;  // the file is in the Hub cache (not part of package.json)
};
struct CorePackageInfo {
  QString coreId;
  QString version;
  QString platform;
  QString license;
  QString sourceUrl;
  QString sourceRef;
  QList<CorePackageFile> files;

  const CorePackageFile* library() const;
  QJsonObject toJson() const;  // as served by the Hub (the cache stores this as package.json)
};
// Rejects invalid identifiers/file names/hashes and packages without exactly one library file.
std::optional<CorePackageInfo> parseCorePackage(const QJsonObject& obj);
bool isValidCoreId(const QString& id);      // ^[a-z0-9][a-z0-9_-]{0,63}$
bool isValidCoreVersion(const QString& v);  // ^[0-9A-Za-z][0-9A-Za-z.+_-]{0,63}$
bool isValidCorePlatform(const QString& p);
bool isValidCoreFileName(const QString& n);  // ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$, no ".."

struct GameEntry {
  QString id;
  QString title;
  QString system;
  QString romSha256;
  qint64 romSize = 0;
  QString romFilename;
  QString uploadedBy;
  QString addedAt;
};
std::optional<GameEntry> parseGame(const QJsonObject& obj);

}  // namespace framebeam
