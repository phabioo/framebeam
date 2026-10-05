#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace framebeam {

// Protokoll laut ADR 0002 / protocol/openapi/framebeam.yaml.
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

// Handshake-Daten des Players (cores werden von aussen befuellt).
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

  static HandshakeInfo detect();  // platform/arch/player_version vorbelegt
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
};
std::optional<HandshakeResult> parseHandshakeResult(const QJsonObject& obj);

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
