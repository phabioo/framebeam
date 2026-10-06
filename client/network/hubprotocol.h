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
struct SystemInfo {
  QString id;
  QString displayName;
  QString preferredCoreId;
  QString expectedCoreVersion;  // empty = any
  QString firmwareMode = QStringLiteral("builtin");  // "builtin" | "native"
  QList<FirmwareFileInfo> firmware;

  bool nativeFirmware() const { return firmwareMode == QLatin1String("native"); }
  const FirmwareFileInfo* file(const QString& fileId) const;
};
std::optional<SystemInfo> parseSystemInfo(const QJsonObject& obj);

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
