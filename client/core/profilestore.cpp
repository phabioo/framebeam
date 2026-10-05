#include "profilestore.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryFile>
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

namespace {

bool dirIsWritable(const QString& path) {
  if (path.isEmpty() || !QDir().mkpath(path)) {
    return false;
  }
  QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".framebeam-write-test-XXXXXX")));
  return probe.open();  // Datei wird beim Zerstoeren geloescht
}

}  // namespace

ProfileStore::BaseDirChoice ProfileStore::chooseBaseDir(const QString& appDir, const QString& appDataDir) {
  if (!appDir.isEmpty()) {
    const QString portable = QDir(appDir).filePath(QStringLiteral("data"));
    if (dirIsWritable(portable)) {
      return {portable, true};
    }
  }
  return {appDataDir, false};
}

// One-time AppData -> portable migration.
// Completion is tracked by a marker file in the new folder, written only after every required
// copy succeeded. The presence of profiles.json etc. says nothing (an interrupted run may have
// copied only some files); a failed or interrupted run is simply retried on the next start.
// Never deletes the source, never overwrites existing files, skips cache/.
// Exception (device.json): if the new folder already has a device.json with a different ID and
// no marker exists, the legacy ID wins only if no profile in the new folder references
// credentials (so no pairing is lost); otherwise the existing one is kept and a warning logged.
// ProfileStore::ProfileStore calls defaultBaseDir() (and thus this function) before load(), so
// no fresh device.json can be created in the new folder before the migration ran.
int ProfileStore::migrateLegacyData(const QString& legacyDir, const QString& newDir) {
  if (legacyDir.isEmpty() || newDir.isEmpty() || QFileInfo(legacyDir).absoluteFilePath() == QFileInfo(newDir).absoluteFilePath() ||
      !QFileInfo(legacyDir).isDir()) {
    return 0;
  }
  const QString marker = QDir(newDir).filePath(QStringLiteral(".migrated-from-appdata"));
  if (QFileInfo::exists(marker)) {
    return 0;
  }
  const QDir src(legacyDir);
  if (!src.exists(QStringLiteral("profiles.json")) && !src.exists(QStringLiteral("device.json")) &&
      !src.exists(QStringLiteral("hubs"))) {
    return 0;
  }
  if (!QDir().mkpath(newDir)) {
    qCWarning(lcProfiles) << "Migration: target folder not creatable";
    return 0;
  }
  int copied = 0;
  bool ok = true;
  QDirIterator it(legacyDir, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString from = it.next();
    const QString rel = src.relativeFilePath(from);
    if (rel.startsWith(QStringLiteral("cache/"))) {
      continue;  // ROM cache is not migrated (large, re-downloaded)
    }
    const QString to = QDir(newDir).filePath(rel);
    if (QFileInfo::exists(to)) {
      if (rel != QStringLiteral("device.json")) {
        continue;  // never overwrite anything existing
      }
      const QString legacyId = readJsonFile(from).value(QStringLiteral("device_id")).toString();
      const QString curId = readJsonFile(to).value(QStringLiteral("device_id")).toString();
      if (legacyId.isEmpty() || legacyId == curId) {
        continue;
      }
      bool hasCredentials = false;
      const QJsonArray arr = readJsonFile(QDir(newDir).filePath(QStringLiteral("profiles.json")))
                                 .value(QStringLiteral("profiles")).toArray();
      for (const QJsonValue& v : arr) {
        if (!v.toObject().value(QStringLiteral("credential_ref")).toString().isEmpty()) {
          hasCredentials = true;
        }
      }
      if (hasCredentials) {
        qCWarning(lcProfiles) << "Migration: existing device.json differs from legacy one and is in use, kept";
        continue;
      }
      if (!QFile::remove(to)) {  // only the new-folder copy; the source is never touched
        qCWarning(lcProfiles) << "Migration: device.json not replaced";
        ok = false;
        continue;
      }
    }
    if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(from, to)) {
      qCWarning(lcProfiles) << "Migration: file not copied:" << rel;
      ok = false;
      continue;
    }
    ++copied;
  }
  if (ok) {
    QFile m(marker);
    if (!m.open(QIODevice::WriteOnly)) {
      qCWarning(lcProfiles) << "Migration: completion marker not written, will retry";
    }
  } else {
    qCWarning(lcProfiles) << "Migration incomplete, will retry on next start";
  }
  return copied;
}

QString ProfileStore::defaultBaseDir() {
  const QByteArray env = qgetenv("FRAMEBEAM_DATA_DIR");
  if (!env.isEmpty()) {
    const QString dir = QString::fromLocal8Bit(env);
    qCInfo(lcProfiles) << "Datenverzeichnis (FRAMEBEAM_DATA_DIR):" << dir;
    return dir;
  }
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString appDir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QString();
  const BaseDirChoice c = chooseBaseDir(appDir, appData);
  if (!c.portable) {
    qCInfo(lcProfiles) << "Datenverzeichnis (Rueckfall AppData, Programmverzeichnis nicht beschreibbar oder unbekannt):"
                       << c.path;
    return c.path;
  }
  qCInfo(lcProfiles) << "Datenverzeichnis (portabel):" << c.path;
  const int n = migrateLegacyData(appData, c.path);
  if (n > 0) {
    qCInfo(lcProfiles) << "Daten aus" << appData << "nach" << c.path << "kopiert (" << n
                       << "Dateien, Quelle unveraendert, ROM-Cache nicht migriert)";
  }
  return c.path;
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
