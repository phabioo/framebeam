#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace framebeam {

// Hub profile per docs/architecture/10-identity-pairing-tls.md. Never contains a secret:
// credentialRef is only the key in the CredentialStore.
struct HubProfile {
  QString hubId;
  QString name;
  QString address;  // normalized: scheme://host[:port]
  QString hubUserId;
  QString userDisplayName;  // Hub user name from the last handshake; empty for older Hubs
  QString deviceId;
  QString credentialRef;
  QString pinnedFingerprint;  // SHA-256 over leaf DER, uppercase hex with colons; empty for HTTP dev
  QDateTime lastConnected;
  bool allowHttp = false;  // dev flag: HTTP also outside localhost
};

// Local data storage: <base>/profiles.json, <base>/device.json, <base>/hubs/<hub_id>/, <base>/cache/roms/.
class ProfileStore {
 public:
  // baseDir empty: defaultBaseDir() (see below).
  explicit ProfileStore(const QString& baseDir = QString());

  // Order: env FRAMEBEAM_DATA_DIR, portable <program directory>/data (only if writable;
  // one-time copy from AppData), otherwise AppDataLocation.
  static QString defaultBaseDir();
  struct BaseDirChoice {
    QString path;
    bool portable = false;
  };
  // Testable: empty appDir = unknown -> fall back to appDataDir.
  static BaseDirChoice chooseBaseDir(const QString& appDir, const QString& appDataDir);
  // Copies (without ROM cache, without overwriting, without touching the source) if newDir has no
  // profiles.json yet. Returns the number of copied files.
  static int migrateLegacyData(const QString& legacyDir, const QString& newDir);
  // 0.9 "One installer": a per-machine Player cannot write next to its program, so its data lives in newDir (AppData).
  // Candidate folders of the old portable data: <localAppData>\\Programs\\FrameBeam Player\\data (per-user Inno/MSI
  // install) and <programFiles>\\FrameBeam Player\\data (old all-users Inno install). Empty arguments are skipped.
  static QStringList portableMigrationSources(const QString& localAppData, const QString& programFiles);
  // Copies the first source that holds Player data (profiles.json, device.json or hubs/) into newDir once: never
  // overwrites an existing file, skips cache/, never touches the source. Completion marker `.migrated-from-portable`
  // in newDir (written only after every copy succeeded; an incomplete run is retried). Returns the copied files.
  static int migratePortableData(const QStringList& sourceDirs, const QString& newDir);
  static bool isValidHubId(const QString& hubId);

  const QString& baseDir() const { return baseDir_; }
  QString profilesFilePath() const;
  // Hub-specific directory (created on demand); empty for an invalid hub ID.
  QString hubDir(const QString& hubId) const;
  // Cross-hub, content-addressed ROM cache (created on demand).
  QString romCacheDir() const;
  QString coreCacheDir() const;  // <data>/cache/cores (downloaded core packages, never migrated)

  QString deviceId() const { return deviceId_; }
  QString deviceName() const { return deviceName_; }
  bool setDeviceName(const QString& name);

  QList<HubProfile> profiles() const { return profiles_; }
  std::optional<HubProfile> profile(const QString& hubId) const;
  std::optional<HubProfile> profileByAddress(const QString& address) const;
  bool upsertProfile(const HubProfile& profile);
  bool removeProfile(const QString& hubId);

  // Hub edit (Settings > Hubs, docs/design/player.md 3p): only host and port of a saved Hub change. hubId, pinned
  // fingerprint, credentialRef, hubUserId and the scheme stay. Fails for an unknown Hub, an invalid input or a
  // target address that another saved Hub already uses.
  bool updateHubAddress(const QString& hubId, const QString& host, int port);
  // Validation of the edit fields (port as typed). One message per field at most; empty list = valid.
  static QStringList validateHubAddress(const QString& host, const QString& port);
  // "host" and "port" of a stored address (scheme://host[:port]); port 0 = none. Used to fill the edit row.
  static void splitAddress(const QString& address, QString* host, int* port);

  bool autoConnect() const { return autoConnect_; }
  bool setAutoConnect(bool on);
  QString lastHubId() const { return lastHubId_; }
  bool setLastHubId(const QString& hubId);

 private:
  void load();
  bool save() const;
  bool saveDevice() const;

  QString baseDir_;
  QString deviceId_;
  QString deviceName_;
  QList<HubProfile> profiles_;
  bool autoConnect_ = false;
  QString lastHubId_;
};

}  // namespace framebeam
