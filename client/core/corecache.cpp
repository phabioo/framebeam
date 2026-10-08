#include "corecache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

#include "romcache.h"

namespace framebeam {

CoreCache::CoreCache(const QString& root) : root_(root) {}

QString CoreCache::currentPlatform() {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
  return QStringLiteral("windows-x64");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
  return QStringLiteral("macos-arm64");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_X86_64)
  return QStringLiteral("macos-x64");
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
  return QStringLiteral("linux-x64");
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM_64)
  return QStringLiteral("linux-arm64");
#else
  return {};
#endif
}

int CoreCache::compareVersions(const QString& a, const QString& b) {
  const auto segs = [](const QString& v) { return v.split(QLatin1Char('.')); };
  const QStringList sa = segs(a);
  const QStringList sb = segs(b);
  const qsizetype n = std::min(sa.size(), sb.size());
  for (qsizetype i = 0; i < n; ++i) {
    bool oa = false;
    bool ob = false;
    const qlonglong na = sa.at(i).toLongLong(&oa);
    const qlonglong nb = sb.at(i).toLongLong(&ob);
    if (oa && ob) {
      if (na != nb) return na < nb ? -1 : 1;
    } else if (sa.at(i) != sb.at(i)) {
      return sa.at(i) < sb.at(i) ? -1 : 1;
    }
  }
  if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
  return a == b ? 0 : (a < b ? -1 : 1);
}

QString CoreCache::packageDir(const QString& coreId, const QString& version, const QString& platform) const {
  if (!isValidCoreId(coreId) || !isValidCoreVersion(version) || !isValidCorePlatform(platform)) {
    return {};
  }
  return QDir(root_).filePath(coreId + QLatin1Char('/') + version + QLatin1Char('/') + platform);
}

QString CoreCache::filePath(const QString& coreId, const QString& version, const QString& platform, const QString& name) const {
  const QString dir = packageDir(coreId, version, platform);
  if (dir.isEmpty() || !isValidCoreFileName(name)) {
    return {};
  }
  return QDir(dir).filePath(name);
}

bool CoreCache::fileValid(const CorePackageInfo& pkg, const CorePackageFile& file) const {
  const QString p = filePath(pkg.coreId, pkg.version, pkg.platform, file.name);
  if (p.isEmpty()) {
    return false;
  }
  const QFileInfo fi(p);
  if (!fi.isFile() || fi.size() != file.size) {
    return false;
  }
  // The memo skips re-hashing a big core on every launch, but must never vouch for a file rewritten since it was
  // verified. mtime alone is not enough: a same-size rewrite within one timestamp tick (coarse file systems) or with
  // a restored mtime keeps size + mtime identical. So the key also holds the metadata change time (ctime, which
  // every write bumps and user code cannot set back), and an entry is only recorded when the file had settled, i.e.
  // both timestamps lie at least memoSettleMs_ before this verification; a file touched more recently is re-hashed
  // next time instead.
  const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
  const qint64 ctime = fi.metadataChangeTime().toMSecsSinceEpoch();
  const auto it = memo_.constFind(p);
  if (it != memo_.constEnd() && it->size == fi.size() && it->mtimeMs == mtime && it->ctimeMs == ctime && it->sha256 == file.sha256) {
    return true;
  }
  QFile f(p);
  if (!f.open(QIODevice::ReadOnly)) {
    return false;
  }
  const qint64 startedMs = QDateTime::currentMSecsSinceEpoch();
  QCryptographicHash h(QCryptographicHash::Sha256);
  const bool ok = h.addData(&f) && QString::fromLatin1(h.result().toHex()) == file.sha256;
  if (ok && std::max(mtime, ctime) + memoSettleMs_ <= startedMs) {
    memo_.insert(p, {fi.size(), mtime, ctime, file.sha256});
  } else {
    memo_.remove(p);
  }
  return ok;
}

CoreCache::StoreResult CoreCache::store(const CorePackageInfo& pkg, const CorePackageFile& file, const QByteArray& data) const {
  const QString p = filePath(pkg.coreId, pkg.version, pkg.platform, file.name);
  if (p.isEmpty() || file.size <= 0 || !RomCache::isValidSha256(file.sha256)) {
    return StoreResult::InvalidArgument;
  }
  if (data.size() != file.size) {
    return StoreResult::SizeMismatch;
  }
  if (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) != file.sha256) {
    return StoreResult::HashMismatch;
  }
  if (!QDir().mkpath(QFileInfo(p).absolutePath())) {
    return StoreResult::IoError;
  }
  memo_.remove(p);
  QSaveFile f(p);  // temp file next to the target, renamed on commit
  if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
    return StoreResult::IoError;
  }
  return StoreResult::Ok;
}

bool CoreCache::writePackage(const CorePackageInfo& pkg) const {
  const QString dir = packageDir(pkg.coreId, pkg.version, pkg.platform);
  if (dir.isEmpty() || !QDir().mkpath(dir)) {
    return false;
  }
  QSaveFile f(QDir(dir).filePath(QStringLiteral("package.json")));
  const QByteArray json = QJsonDocument(pkg.toJson()).toJson(QJsonDocument::Indented);
  return f.open(QIODevice::WriteOnly) && f.write(json) == json.size() && f.commit();
}

std::optional<CorePackageInfo> CoreCache::readPackage(const QString& coreId, const QString& version, const QString& platform) const {
  const QString dir = packageDir(coreId, version, platform);
  if (dir.isEmpty()) {
    return std::nullopt;
  }
  QFile f(QDir(dir).filePath(QStringLiteral("package.json")));
  if (!f.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject()) {
    return std::nullopt;
  }
  auto pkg = parseCorePackage(doc.object());
  if (!pkg || pkg->coreId != coreId || pkg->version != version || pkg->platform != platform) {
    return std::nullopt;
  }
  return pkg;
}

QString CoreCache::libraryPath(const QString& coreId, const QString& version, const QString& platform) const {
  const auto pkg = readPackage(coreId, version, platform);
  if (!pkg) {
    return {};
  }
  const CorePackageFile* lib = pkg->library();
  if (lib == nullptr || !fileValid(*pkg, *lib)) {
    return {};
  }
  return filePath(coreId, version, platform, lib->name);
}

QStringList CoreCache::versions(const QString& coreId, const QString& platform) const {
  QStringList out;
  if (!isValidCoreId(coreId) || !isValidCorePlatform(platform)) {
    return out;
  }
  const QDir coreDir(QDir(root_).filePath(coreId));
  for (const QString& v : coreDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    if (isValidCoreVersion(v) && QFileInfo::exists(packageDir(coreId, v, platform) + QStringLiteral("/package.json"))) {
      out.append(v);
    }
  }
  std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return compareVersions(a, b) > 0; });
  return out;
}

}  // namespace framebeam
