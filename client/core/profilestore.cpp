#include "profilestore.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>

namespace framebeam {

Q_LOGGING_CATEGORY(lcProfiles, "framebeam.profiles")

namespace {

QJsonObject readJsonFile(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  return err.error == QJsonParseError::NoError ? doc.object() : QJsonObject();
}

bool writeJsonFile(const QString& path, const QJsonObject& obj) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
  return f.commit();
}

QJsonObject toJson(const HubProfile& p) {
  QJsonObject o;
  o.insert(QStringLiteral("hub_id"), p.hubId);
  o.insert(QStringLiteral("name"), p.name);
  o.insert(QStringLiteral("address"), p.address);
  o.insert(QStringLiteral("hub_user_id"), p.hubUserId);
  o.insert(QStringLiteral("device_id"), p.deviceId);
  o.insert(QStringLiteral("credential_ref"), p.credentialRef);
  o.insert(QStringLiteral("pinned_fingerprint"), p.pinnedFingerprint);
  o.insert(QStringLiteral("last_connected"),
           p.lastConnected.isValid() ? p.lastConnected.toUTC().toString(Qt::ISODate) : QString());
  o.insert(QStringLiteral("allow_http"), p.allowHttp);
  return o;
}

HubProfile fromJson(const QJsonObject& o) {
  HubProfile p;
  p.hubId = o.value(QStringLiteral("hub_id")).toString();
  p.name = o.value(QStringLiteral("name")).toString();
  p.address = o.value(QStringLiteral("address")).toString();
  p.hubUserId = o.value(QStringLiteral("hub_user_id")).toString();
  p.deviceId = o.value(QStringLiteral("device_id")).toString();
  p.credentialRef = o.value(QStringLiteral("credential_ref")).toString();
  p.pinnedFingerprint = o.value(QStringLiteral("pinned_fingerprint")).toString();
  p.lastConnected = QDateTime::fromString(o.value(QStringLiteral("last_connected")).toString(), Qt::ISODate);
  p.allowHttp = o.value(QStringLiteral("allow_http")).toBool(false);
  return p;
}

}  // namespace

QString ProfileStore::defaultBaseDir() {
  const QByteArray env = qgetenv("FRAMEBEAM_DATA_DIR");
  if (!env.isEmpty()) {
    return QString::fromLocal8Bit(env);
  }
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool ProfileStore::isValidHubId(const QString& hubId) {
  static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
  return re.match(hubId).hasMatch();
}

ProfileStore::ProfileStore(const QString& baseDir) : baseDir_(baseDir.isEmpty() ? defaultBaseDir() : baseDir) {
  QDir().mkpath(baseDir_);
  load();
}

QString ProfileStore::profilesFilePath() const { return QDir(baseDir_).filePath(QStringLiteral("profiles.json")); }

QString ProfileStore::hubDir(const QString& hubId) const {
  if (!isValidHubId(hubId)) {
    return {};
  }
  const QString dir = QDir(baseDir_).filePath(QStringLiteral("hubs/") + hubId);
  QDir().mkpath(dir);
  return dir;
}

QString ProfileStore::romCacheDir() const {
  const QString dir = QDir(baseDir_).filePath(QStringLiteral("cache/roms"));
  QDir().mkpath(dir);
  return dir;
}

void ProfileStore::load() {
  const QJsonObject dev = readJsonFile(QDir(baseDir_).filePath(QStringLiteral("device.json")));
  deviceId_ = dev.value(QStringLiteral("device_id")).toString();
  deviceName_ = dev.value(QStringLiteral("device_name")).toString();
  bool devDirty = false;
  if (QUuid::fromString(deviceId_).isNull()) {
    deviceId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    devDirty = true;
  }
  if (deviceName_.isEmpty()) {
    deviceName_ = QSysInfo::machineHostName();
    if (deviceName_.isEmpty()) {
      deviceName_ = QStringLiteral("FrameBeam Player");
    }
    devDirty = true;
  }
  if (devDirty && !saveDevice()) {
    qCWarning(lcProfiles) << "device.json konnte nicht geschrieben werden";
  }

  const QJsonObject root = readJsonFile(profilesFilePath());
  const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
  autoConnect_ = settings.value(QStringLiteral("auto_connect")).toBool(false);
  lastHubId_ = settings.value(QStringLiteral("last_hub_id")).toString();
  profiles_.clear();
  const QJsonArray arr = root.value(QStringLiteral("profiles")).toArray();
  for (const QJsonValue& v : arr) {
    const HubProfile p = fromJson(v.toObject());
    if (isValidHubId(p.hubId)) {
      profiles_.append(p);
    }
  }
}

bool ProfileStore::save() const {
  QJsonArray arr;
  for (const HubProfile& p : profiles_) {
    arr.append(toJson(p));
  }
  QJsonObject settings;
  settings.insert(QStringLiteral("auto_connect"), autoConnect_);
  settings.insert(QStringLiteral("last_hub_id"), lastHubId_);
  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("settings"), settings);
  root.insert(QStringLiteral("profiles"), arr);
  return writeJsonFile(profilesFilePath(), root);
}

bool ProfileStore::saveDevice() const {
  QJsonObject o;
  o.insert(QStringLiteral("device_id"), deviceId_);
  o.insert(QStringLiteral("device_name"), deviceName_);
  return writeJsonFile(QDir(baseDir_).filePath(QStringLiteral("device.json")), o);
}

bool ProfileStore::setDeviceName(const QString& name) {
  if (name.trimmed().isEmpty()) {
    return false;
  }
  deviceName_ = name.trimmed();
  return saveDevice();
}

std::optional<HubProfile> ProfileStore::profile(const QString& hubId) const {
  for (const HubProfile& p : profiles_) {
    if (p.hubId == hubId) {
      return p;
    }
  }
  return std::nullopt;
}

std::optional<HubProfile> ProfileStore::profileByAddress(const QString& address) const {
  for (const HubProfile& p : profiles_) {
    if (p.address.compare(address, Qt::CaseInsensitive) == 0) {
      return p;
    }
  }
  return std::nullopt;
}

bool ProfileStore::upsertProfile(const HubProfile& profile) {
  if (!isValidHubId(profile.hubId)) {
    return false;
  }
  bool found = false;
  for (HubProfile& p : profiles_) {
    if (p.hubId == profile.hubId) {
      p = profile;
      found = true;
      break;
    }
  }
  if (!found) {
    profiles_.append(profile);
  }
  return save();
}

bool ProfileStore::removeProfile(const QString& hubId) {
  for (qsizetype i = 0; i < profiles_.size(); ++i) {
    if (profiles_.at(i).hubId == hubId) {
      profiles_.removeAt(i);
      if (lastHubId_ == hubId) {
        lastHubId_.clear();
      }
      return save();
    }
  }
  return true;
}

bool ProfileStore::setAutoConnect(bool on) {
  autoConnect_ = on;
  return save();
}

bool ProfileStore::setLastHubId(const QString& hubId) {
  lastHubId_ = hubId;
  return save();
}

}  // namespace framebeam
