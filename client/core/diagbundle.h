#pragma once
// Diagnostics bundle of the Player ("Collect logs"): a zip with the Player's logs and a player-info.json, secrets
// removed. Mirrors the Hub bundle (server/internal/diag):
//
//   README.txt          what the bundle contains and what was removed
//   player-info.json    versions, system, Hubs (no tokens), cores, media, controllers, settings summary
//   logs/<name>         player.log, player.1.log ... newest first, redacted, total cap 50 MiB
//   logs/plugins/<name> plugin console logs, when they exist on disk (<data>/logs/plugins)
//
// IP addresses and host names are kept on purpose (needed to debug connections).

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace framebeam::diag {

inline constexpr const char* kEntryReadme = "README.txt";
inline constexpr const char* kEntryInfo = "player-info.json";
inline constexpr const char* kLogsPrefix = "logs/";
inline constexpr const char* kPluginLogsPrefix = "logs/plugins/";
inline constexpr int kInfoVersion = 1;
inline constexpr qint64 kDefaultMaxLogBytes = 50ll << 20;
inline constexpr const char* kCutMarker = "[older content cut: size cap of the bundle]\n";

// Removes TURN credentials and secrets, tokens, passwords, invite codes, Authorization/Bearer and cookies from text.
// Host names and IP addresses stay. Same patterns as the Hub (server/internal/diag/redact.go).
QByteArray redact(const QByteArray& text);

// framebeam-player-diagnostics-<UTC timestamp>.zip
QString fileName(const QDateTime& when);

struct Options {
  QString logDir;                      // default <dataDir>/logs
  qint64 maxLogBytes = kDefaultMaxLogBytes;  // cap of all log content, before compression
  QDateTime now;                       // tests; default current time
};

// player-info.json content that needs no running Player: info_version, generated_at, player, system, hubs (from
// profiles.json, connected=false), emulation (cores found in the core cache, hw render switch), media (encoder
// switches), controllers (empty) and settings (non-secret summary). The GUI replaces hubs/emulation/media/controllers
// with live values.
QJsonObject offlineInfo(const QString& dataDir);

// Writes the bundle atomically to outPath (QSaveFile, an existing file is replaced). Log content is redacted line by
// line; the newest file comes first, the oldest file that does not fit is cut to its newest lines at a line boundary
// (kCutMarker first), further files are left out. false + *error on failure.
bool writeBundle(const QString& outPath, const QJsonObject& info, const QString& dataDir, const Options& options = {},
                 QString* error = nullptr);

// Minimal zip reader (tests, store and deflate): entry name -> content; false when the archive is damaged or a CRC differs.
bool readZip(const QString& path, QMap<QString, QByteArray>* entries, QString* error = nullptr);

}  // namespace framebeam::diag
