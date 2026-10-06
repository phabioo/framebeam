#include "firmwarecache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace framebeam {

FirmwareCache::FirmwareCache(const QString& dir) : dir_(dir) {}

bool FirmwareCache::isValidSystemId(const QString& id) {
  static const QRegularExpression re(QStringLiteral("^[a-z0-9_-]{1,32}$"));
  return re.match(id).hasMatch();
}

bool FirmwareCache::isValidSha256(const QString& sha256) {
  static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
  return re.match(sha256).hasMatch();
}

QString FirmwareCache::path(const QString& system, const QString& sha256) const {
  if (!isValidSystemId(system) || !isValidSha256(sha256)) {
    return {};
  }
  return QDir(dir_).filePath(system + QLatin1Char('/') + sha256);
}

bool FirmwareCache::probe(const QString& system, const QString& sha256, qint64 expectedSize) const {
  const QString p = path(system, sha256);
  if (p.isEmpty() || expectedSize <= 0) {
    return false;
  }
  const QFileInfo fi(p);
  return fi.isFile() && fi.size() == expectedSize;
}

bool FirmwareCache::lookup(const QString& system, const QString& sha256, qint64 expectedSize, QString* pathOut) const {
  const QString p = path(system, sha256);
  if (p.isEmpty()) {
    return false;
  }
  QFile f(p);
  if (!f.exists()) {
    return false;
  }
  bool ok = false;
  if (f.size() == expectedSize && f.open(QIODevice::ReadOnly)) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    ok = h.addData(&f) && QString::fromLatin1(h.result().toHex()) == sha256;
    f.close();
  }
  if (!ok) {
    QFile::remove(p);  // corrupted or foreign content under a hash name
    return false;
  }
  if (pathOut != nullptr) {
    *pathOut = p;
  }
  return true;
}

FirmwareCache::StoreResult FirmwareCache::store(const QString& system, const QString& sha256, qint64 expectedSize,
                                                const QByteArray& data) const {
  const QString p = path(system, sha256);
  if (p.isEmpty() || expectedSize <= 0) {
    return StoreResult::InvalidArgument;
  }
  if (data.size() != expectedSize) {
    return StoreResult::SizeMismatch;
  }
  if (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) != sha256) {
    return StoreResult::HashMismatch;
  }
  if (!QDir().mkpath(QFileInfo(p).absolutePath())) {
    return StoreResult::IoError;
  }
  QSaveFile f(p);
  if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
    return StoreResult::IoError;
  }
  return StoreResult::Ok;
}

}  // namespace framebeam
