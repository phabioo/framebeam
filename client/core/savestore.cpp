#include "savestore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

#include "profilestore.h"

namespace framebeam {

namespace {
constexpr const char* kStateFile = "sync.json";

bool isCandidateName(const QString& name) {
  return !(name == QLatin1String(kStateFile) || name.endsWith(QLatin1String(".bak")) || name.endsWith(QLatin1String(".tmp")) ||
           name.endsWith(QLatin1String(".part")) || name.startsWith(QLatin1Char('.')));
}
}  // namespace

bool SaveStore::isSafeId(const QString& id) {
  static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$"));
  return re.match(id).hasMatch() && !id.contains(QStringLiteral(".."));
}

QString SaveStore::userSavesDir(const ProfileStore& profiles, const QString& hubId, const QString& userId) {
  if (!isSafeId(userId)) {
    return {};
  }
  const QString hub = profiles.hubDir(hubId);
  return hub.isEmpty() ? QString() : QDir(hub).filePath(QStringLiteral("users/%1/saves").arg(userId));
}

QString SaveStore::gameDir(const ProfileStore& profiles, const QString& hubId, const QString& userId, const QString& gameId) {
  const QString base = userSavesDir(profiles, hubId, userId);
  return (base.isEmpty() || !isSafeId(gameId)) ? QString() : QDir(base).filePath(gameId);
}

bool SaveStore::isValidSlotName(const QString& slot) {
  static const QRegularExpression re(QStringLiteral("\\A[a-z0-9_-]{1,32}\\z"));  // \z: no trailing newline (Go's $ semantics)
  return re.match(slot).hasMatch();
}

QString SaveStore::slotDirIn(const QString& gameDir, const QString& slot) {
  if (gameDir.isEmpty() || !isValidSlotName(slot)) {
    return {};
  }
  return slot == QLatin1String("default") ? gameDir : QDir(gameDir).filePath(QStringLiteral("slots/%1").arg(slot));
}

QString SaveStore::slotDir(const ProfileStore& profiles, const QString& hubId, const QString& userId, const QString& gameId,
                           const QString& slot) {
  return slotDirIn(gameDir(profiles, hubId, userId, gameId), slot);
}

QStringList SaveStore::localSlots(const QString& gameDir) {
  QStringList out;
  if (gameDir.isEmpty()) {
    return out;
  }
  if (QFileInfo::exists(stateFilePath(gameDir)) || !findSaveFile(gameDir, QString()).isEmpty()) {
    out.append(QStringLiteral("default"));
  }
  QStringList extra;
  const QDir slotsDir(QDir(gameDir).filePath(QStringLiteral("slots")));
  for (const QString& n : slotsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    if (isValidSlotName(n) && n != QLatin1String("default")) {
      extra.append(n);
    }
  }
  return out + extra;
}

QString SaveStore::legacyDir(const ProfileStore& profiles, const QString& hubId) {
  const QString hub = profiles.hubDir(hubId);
  return hub.isEmpty() ? QString() : QDir(hub).filePath(QStringLiteral("saves"));
}

QString SaveStore::expectedSaveName(const QString& romPath) {
  return QFileInfo(romPath).completeBaseName() + QStringLiteral(".sav");
}

QString SaveStore::findSaveFile(const QString& gameDir, const QString& expectedName, QStringList* warnings) {
  const QDir dir(gameDir);
  if (!expectedName.isEmpty() && QFileInfo(dir.filePath(expectedName)).isFile()) {
    return dir.filePath(expectedName);
  }
  const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
  QFileInfoList cands;
  for (const QFileInfo& fi : files) {
    if (isCandidateName(fi.fileName())) {
      cands.append(fi);
    }
  }
  if (cands.isEmpty()) {
    return {};
  }
  if (cands.size() > 1 && warnings != nullptr) {
    warnings->append(QStringLiteral("%1 candidate save files, syncing the newest").arg(cands.size()));
  }
  return cands.first().absoluteFilePath();  // sorted by time, newest first
}

QString SaveStore::stateFilePath(const QString& gameDir) { return QDir(gameDir).filePath(QLatin1String(kStateFile)); }

SyncState SaveStore::loadState(const QString& gameDir) {
  SyncState s;
  QFile f(stateFilePath(gameDir));
  if (!f.open(QIODevice::ReadOnly)) {
    return s;
  }
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  s.slot = o.value(QStringLiteral("slot")).toString(QStringLiteral("default"));
  s.baseRevision = o.value(QStringLiteral("base_revision")).toInt(0);
  s.lastSyncedSha256 = o.value(QStringLiteral("last_synced_sha256")).toString();
  s.pending = o.value(QStringLiteral("pending")).toBool(false);
  s.conflictId = o.value(QStringLiteral("conflict_id")).toString();
  s.lastError = o.value(QStringLiteral("last_error")).toString();
  s.updatedAt = QDateTime::fromString(o.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
  s.writerCoreId = o.value(QStringLiteral("writer_core_id")).toString();
  s.writerCoreVersion = o.value(QStringLiteral("writer_core_version")).toString();
  return s;
}

bool SaveStore::saveState(const QString& gameDir, SyncState s) {
  s.updatedAt = QDateTime::currentDateTimeUtc();
  const QJsonObject o{{QStringLiteral("slot"), s.slot},
                      {QStringLiteral("base_revision"), s.baseRevision},
                      {QStringLiteral("last_synced_sha256"), s.lastSyncedSha256},
                      {QStringLiteral("pending"), s.pending},
                      {QStringLiteral("conflict_id"), s.conflictId},
                      {QStringLiteral("last_error"), s.lastError},
                      {QStringLiteral("updated_at"), s.updatedAt.toString(Qt::ISODate)},
                      {QStringLiteral("writer_core_id"), s.writerCoreId},
                      {QStringLiteral("writer_core_version"), s.writerCoreVersion}};
  return atomicWrite(stateFilePath(gameDir), QJsonDocument(o).toJson(QJsonDocument::Indented));
}

QString SaveStore::sha256Of(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString SaveStore::sha256OfFile(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash h(QCryptographicHash::Sha256);
  if (!h.addData(&f)) {
    return {};
  }
  return QString::fromLatin1(h.result().toHex());
}

bool SaveStore::atomicWrite(const QString& path, const QByteArray& data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    return false;
  }
  f.write(data);
  return f.commit();
}

QString SaveStore::backupFile(const QString& path, const QString& tag) {
  if (!QFileInfo::exists(path)) {
    return {};
  }
  QString target;
  if (tag.isEmpty()) {
    target = path + QStringLiteral(".bak");
    QFile::remove(target);
  } else {
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    target = QStringLiteral("%1.%2-%3.bak").arg(path, tag, stamp);
  }
  return QFile::copy(path, target) ? target : QString();
}

bool SaveStore::migrateLegacy(const QString& legacy, const QStringList& basenames, const QString& gameDir,
                              const QString& expectedName) {
  if (legacy.isEmpty() || !QDir(legacy).exists() || expectedName.isEmpty()) {
    return false;
  }
  if (!findSaveFile(gameDir, expectedName).isEmpty()) {
    return false;  // never overwrite an existing save
  }
  for (const QString& base : basenames) {
    if (base.isEmpty() || base.contains(QLatin1Char('/')) || base.contains(QLatin1Char('\\'))) {
      continue;
    }
    const QString src = QDir(legacy).filePath(base + QStringLiteral(".sav"));
    if (QFileInfo(src).isFile()) {
      QDir().mkpath(gameDir);
      return QFile::copy(src, QDir(gameDir).filePath(expectedName));
    }
  }
  return false;
}

}  // namespace framebeam
