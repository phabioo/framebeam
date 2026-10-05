#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

namespace framebeam {

// Hub profile per docs/architecture/10-identity-pairing-tls.md. Never contains a secret:
// credentialRef is only the key in the CredentialStore.
struct HubProfile {
  QString hubId;
  QString name;
  QString address;  // normalized: scheme://host[:port]
  QString hubUserId;
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
  static bool isValidHubId(const QString& hubId);

  const QString& baseDir() const { return baseDir_; }
  QString profilesFilePath() const;
  // Hub-specific directory (created on demand); empty for an invalid hub ID.
  QString hubDir(const QString& hubId) const;
  // Cross-hub, content-addressed ROM cache (created on demand).
  QString romCacheDir() const;

  QString deviceId() const { return deviceId_; }
  QString deviceName() const { return deviceName_; }
  bool setDeviceName(const QString& name);

  QList<HubProfile> profiles() const { return profiles_; }
  std::optional<HubProfile> profile(const QString& hubId) const;
  std::optional<HubProfile> profileByAddress(const QString& address) const;
  bool upsertProfile(const HubProfile& profile);
  bool removeProfile(const QString& hubId);

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
