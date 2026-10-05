#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

namespace framebeam {

// Hub-Profil laut docs/architektur/10-identitaet-pairing-tls.md. Enthaelt nie ein Secret:
// credentialRef ist nur der Schluessel im CredentialStore.
struct HubProfile {
  QString hubId;
  QString name;
  QString address;  // normalisiert: scheme://host[:port]
  QString hubUserId;
  QString deviceId;
  QString credentialRef;
  QString pinnedFingerprint;  // SHA-256 ueber Leaf-DER, Hex gross mit Doppelpunkten; leer bei HTTP-Dev
  QDateTime lastConnected;
  bool allowHttp = false;  // Dev-Flag: HTTP auch ausserhalb von localhost
};

// Lokale Datenablage: <base>/profiles.json, <base>/device.json, <base>/hubs/<hub_id>/, <base>/cache/roms/.
class ProfileStore {
 public:
  // baseDir leer: defaultBaseDir() (Env FRAMEBEAM_DATA_DIR, sonst QStandardPaths::AppDataLocation).
  explicit ProfileStore(const QString& baseDir = QString());

  static QString defaultBaseDir();
  static bool isValidHubId(const QString& hubId);

  const QString& baseDir() const { return baseDir_; }
  QString profilesFilePath() const;
  // Hub-spezifisches Verzeichnis (wird angelegt); leer bei ungueltiger Hub-ID.
  QString hubDir(const QString& hubId) const;
  // Hubuebergreifender, inhaltsadressierter ROM-Cache (wird angelegt).
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
