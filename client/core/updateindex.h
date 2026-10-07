#pragma once
// Update index (release feed, spec 0.3 S2): parsing, validation, selection, protocol compatibility.
// Pure functions without I/O; the signature is checked separately (updatesig.h).

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

#include "semver.h"

namespace framebeam::update {

inline constexpr const char* kDefaultIndexUrl =
    "https://github.com/phabioo/framebeam/releases/download/updates-index/updates-index.json";
inline constexpr const char* kPlayerPlatform = "windows-x64";  // the only Player update platform
inline constexpr const char* kKindInstaller = "installer";
inline constexpr const char* kKindZip = "zip";

struct Artifact {
  QString platform;
  QString kind;
  QString name;
  qint64 size = 0;
  QString sha256;  // 64 lowercase hex
  QString url;
};

struct Release {
  QString product;  // hub | player
  QString channel;  // test | stable
  QString version;
  SemVer semver;
  QString commit;
  QString publishedAt;
  QString notesUrl;
  int protocolVersion = 0;
  int minProtocolVersion = 0;
  QList<Artifact> artifacts;

  std::optional<Artifact> artifact(const QString& platform, const QString& kind) const;
  QJsonObject toJson() const;
};

struct Index {
  QString generatedAt;
  QList<Release> releases;
  QStringList skipped;  // reasons of skipped (invalid) release entries
};

struct ParseResult {
  bool ok = false;
  QString error;  // set when !ok (unknown schema, duplicate, malformed JSON)
  Index index;
};

// allowFileUrls: artifact URLs may be file:// (only when the index itself came from file://, tests).
ParseResult parseIndex(const QByteArray& json, bool allowFileUrls = false);

// Update channel as the user sees it.
enum class Channel { Off, Stable, Test };
QString channelName(Channel c);  // "off" | "stable" | "test"
std::optional<Channel> parseChannel(const QString& name);  // accepts "off", "stable", "test"
// Compiled default: "dev" (or anything unknown) = Off.
Channel channelFromCompiled(const QString& compiled);

// Protocol compatibility (S4, Player view): the Hub values come from the last handshake.
struct HubProtocol {
  int protocolVersion = 0;
  int minProtocolVersion = 0;
};
bool protocolCompatible(const Release& candidate, const std::optional<HubProtocol>& hub);

struct SelectionInput {
  QString product = QStringLiteral("player");
  Channel channel = Channel::Off;
  QString currentVersion;
  QString platform = QLatin1String(kPlayerPlatform);
  QString kind = QLatin1String(kKindInstaller);
  std::optional<HubProtocol> hub;
};

struct Selection {
  enum class Status {
    Disabled,      // channel off or current version is not SemVer
    UpToDate,      // nothing newer
    Available,     // `release` is the highest compatible candidate
    Incompatible,  // newer releases exist, but none is compatible with the current Hub
  };
  Status status = Status::Disabled;
  QString reason;  // Disabled: why
  Release release;
  Artifact artifact;

  static QString statusName(Status s);  // disabled | up_to_date | available | incompatible
};

// Own product, channel per S1 (test sees test + stable; stable only stable), an artifact for platform/kind,
// SemVer strictly greater than the running version, protocol-compatible; highest wins. Never a downgrade.
Selection selectRelease(const Index& index, const SelectionInput& in);

// {"status","current_version","channel","release":{...},"artifact":{...},"skipped":[...]} for the CLI.
QJsonObject selectionToJson(const Selection& sel, const SelectionInput& in, const QStringList& skipped);

}  // namespace framebeam::update
