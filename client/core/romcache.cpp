#include "romcache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace framebeam {

namespace {
std::filesystem::path toFsPath(const QString& p) {
  const QByteArray u8 = p.toUtf8();
  return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(u8.constData()), static_cast<size_t>(u8.size())));
}
}  // namespace

RomCache::RomCache(const QString& dir) : dir_(dir) { QDir().mkpath(dir_); }

bool RomCache::isValidSha256(const QString& sha256) {
  static const QRegularExpression re(QStringLiteral("^[0-9a-f]{64}$"));
  return re.match(sha256).hasMatch();
}

QString RomCache::extensionFromFilename(const QString& filename) {
  static const QRegularExpression re(QStringLiteral("^[a-z0-9]{1,8}$"));
  const QString ext = QFileInfo(filename).suffix().toLower();
  return re.match(ext).hasMatch() ? ext : QStringLiteral("bin");
}

bool RomCache::sha256OfFile(const QString& path, QString* hexOut) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return false;
  }
  QCryptographicHash h(QCryptographicHash::Sha256);
  if (!h.addData(&f)) {
    return false;
  }
  *hexOut = QString::fromLatin1(h.result().toHex());
  return true;
}

QString RomCache::finalPath(const QString& sha256, const QString& ext) const {
  return QDir(dir_).filePath(sha256 + QLatin1Char('.') + ext);
}

QString RomCache::partPath(const QString& sha256, const QString& ext) const {
  return finalPath(sha256, ext) + QStringLiteral(".part");
}

void RomCache::writeSidecar(const QString& sha256, const QString& ext) const {
  const QFileInfo fi(finalPath(sha256, ext));
  QJsonObject o;
  o.insert(QStringLiteral("size"), static_cast<double>(fi.size()));
  o.insert(QStringLiteral("mtime_ms"), static_cast<double>(fi.lastModified().toMSecsSinceEpoch()));
  o.insert(QStringLiteral("last_used_ms"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
  QFile f(finalPath(sha256, ext) + QStringLiteral(".ok"));
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
  }
}

bool RomCache::sidecarMatches(const QString& sha256, const QString& ext) const {
  QFile f(finalPath(sha256, ext) + QStringLiteral(".ok"));
  if (!f.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  const QFileInfo fi(finalPath(sha256, ext));
  return o.contains(QStringLiteral("size")) && o.contains(QStringLiteral("mtime_ms")) &&
         static_cast<qint64>(o.value(QStringLiteral("size")).toDouble()) == fi.size() &&
         static_cast<qint64>(o.value(QStringLiteral("mtime_ms")).toDouble()) == fi.lastModified().toMSecsSinceEpoch();
}

namespace {
bool isRomFileName(const QString& name, QString* sha, QString* ext) {
  static const QRegularExpression re(QStringLiteral("^([0-9a-f]{64})\\.([a-z0-9]{1,8})$"));
  const QRegularExpressionMatch m = re.match(name);
  if (!m.hasMatch()) {
    return false;
  }
  *sha = m.captured(1);
  *ext = m.captured(2);
  return true;
}
}  // namespace

void RomCache::touch(const QString& sha256, const QString& ext, qint64 nowMs) const {
  if (!isValidSha256(sha256)) {
    return;
  }
  const QString path = finalPath(sha256, ext);
  const QFileInfo fi(path);
  if (!fi.isFile()) {
    return;
  }
  QFile sc(path + QStringLiteral(".ok"));
  QJsonObject o;
  if (sc.open(QIODevice::ReadOnly)) {
    o = QJsonDocument::fromJson(sc.readAll()).object();
    sc.close();
  }
  if (!o.contains(QStringLiteral("size"))) {
    return;  // not verified yet: the check writes the record (with the use time) itself
  }
  o.insert(QStringLiteral("last_used_ms"), static_cast<double>(nowMs > 0 ? nowMs : QDateTime::currentMSecsSinceEpoch()));
  if (sc.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    sc.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
  }
}

QList<RomCache::Entry> RomCache::entries() const {
  QList<Entry> out;
  const QFileInfoList files = QDir(dir_).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
  for (const QFileInfo& fi : files) {
    Entry e;
    if (!isRomFileName(fi.fileName(), &e.sha256, &e.ext)) {
      continue;
    }
    e.path = fi.absoluteFilePath();
    e.size = fi.size();
    e.lastUsedMs = fi.lastModified().toMSecsSinceEpoch();
    QFile sc(e.path + QStringLiteral(".ok"));
    if (sc.open(QIODevice::ReadOnly)) {
      const QJsonObject o = QJsonDocument::fromJson(sc.readAll()).object();
      if (o.contains(QStringLiteral("last_used_ms"))) {
        e.lastUsedMs = static_cast<qint64>(o.value(QStringLiteral("last_used_ms")).toDouble());
      }
    }
    out.append(e);
  }
  std::stable_sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    return a.lastUsedMs != b.lastUsedMs ? a.lastUsedMs < b.lastUsedMs : a.sha256 < b.sha256;
  });
  return out;
}

qint64 RomCache::totalSize() const {
  qint64 n = 0;
  for (const Entry& e : entries()) n += e.size;
  return n;
}

RomCache::TrimResult RomCache::trimToLimit(qint64 limitBytes, const QSet<QString>& protectedHashes) const {
  TrimResult r;
  const QList<Entry> list = entries();
  qint64 total = 0;
  for (const Entry& e : list) total += e.size;
  for (const Entry& e : list) {
    if (limitBytes <= 0 || total <= limitBytes) {
      break;
    }
    if (protectedHashes.contains(e.sha256) || QFileInfo::exists(partPath(e.sha256, e.ext))) {
      continue;
    }
    if (QFile::remove(e.path)) {
      QFile::remove(e.path + QStringLiteral(".ok"));
      total -= e.size;
      r.freedBytes += e.size;
      ++r.removedFiles;
    }
  }
  r.remainingBytes = total;
  return r;
}

qint64 RomCache::clearableSize(const QSet<QString>& protectedHashes) const {
  qint64 n = 0;
  for (const Entry& e : entries()) {
    if (!protectedHashes.contains(e.sha256) && !QFileInfo::exists(partPath(e.sha256, e.ext))) n += e.size;
  }
  return n;
}

RomCache::TrimResult RomCache::clear(const QSet<QString>& protectedHashes) const {
  TrimResult r;
  for (const Entry& e : entries()) {
    if (protectedHashes.contains(e.sha256) || QFileInfo::exists(partPath(e.sha256, e.ext))) {
      r.remainingBytes += e.size;
      continue;
    }
    if (QFile::remove(e.path)) {
      QFile::remove(e.path + QStringLiteral(".ok"));
      r.freedBytes += e.size;
      ++r.removedFiles;
    } else {
      r.remainingBytes += e.size;
    }
  }
  return r;
}

bool RomCache::lookup(const QString& sha256, const QString& ext, qint64 expectedSize, QString* pathOut) const {
  if (!isValidSha256(sha256)) {
    return false;
  }
  const QString path = finalPath(sha256, ext);
  const QFileInfo fi(path);
  if (!fi.isFile()) {
    return false;
  }
  const auto drop = [&]() {
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".ok"));
    return false;
  };
  if (expectedSize >= 0 && fi.size() != expectedSize) {
    return drop();
  }
  if (!sidecarMatches(sha256, ext)) {
    QString actual;
    if (!sha256OfFile(path, &actual)) {
      return false;
    }
    if (actual != sha256) {
      return drop();
    }
    writeSidecar(sha256, ext);
  }
  if (pathOut != nullptr) {
    *pathOut = path;
  }
  return true;
}

RomCache::Probe RomCache::probe(const QString& sha256, const QString& ext, qint64 expectedSize) const {
  if (!isValidSha256(sha256)) {
    return Probe::Missing;
  }
  const QFileInfo fi(finalPath(sha256, ext));
  if (!fi.isFile()) {
    return Probe::Missing;
  }
  if (expectedSize >= 0 && fi.size() != expectedSize) {
    dropFinal(sha256, ext);
    return Probe::Missing;
  }
  return sidecarMatches(sha256, ext) ? Probe::Valid : Probe::Unverified;
}

void RomCache::dropFinal(const QString& sha256, const QString& ext) const {
  QFile::remove(finalPath(sha256, ext));
  QFile::remove(finalPath(sha256, ext) + QStringLiteral(".ok"));
}

bool RomCache::commitVerified(const QString& sha256, const QString& ext) const {
  std::error_code ec;
  std::filesystem::rename(toFsPath(partPath(sha256, ext)), toFsPath(finalPath(sha256, ext)), ec);
  if (ec) {
    return false;
  }
  writeSidecar(sha256, ext);
  return true;
}

qint64 RomCache::partSize(const QString& sha256, const QString& ext) const {
  const QFileInfo fi(partPath(sha256, ext));
  return fi.isFile() ? fi.size() : 0;
}

void RomCache::discardPart(const QString& sha256, const QString& ext) const { QFile::remove(partPath(sha256, ext)); }

RomCache::CommitResult RomCache::verifyAndCommit(const QString& sha256, const QString& ext) const {
  QString actual;
  if (!sha256OfFile(partPath(sha256, ext), &actual)) {
    return CommitResult::IoError;
  }
  if (actual != sha256) {
    discardPart(sha256, ext);
    return CommitResult::HashMismatch;
  }
  std::error_code ec;
  std::filesystem::rename(toFsPath(partPath(sha256, ext)), toFsPath(finalPath(sha256, ext)), ec);
  if (ec) {
    return CommitResult::IoError;
  }
  writeSidecar(sha256, ext);
  return CommitResult::Ok;
}

RomCache::CommitResult RomCache::adoptFile(const QString& srcPath, const QString& sha256, const QString& ext) const {
  const QFileInfo src(srcPath);
  if (!isValidSha256(sha256) || !src.isFile()) {
    return CommitResult::IoError;
  }
  if (lookup(sha256, ext, src.size())) {
    return CommitResult::Ok;
  }
  if (QFileInfo::exists(partPath(sha256, ext))) {
    return CommitResult::IoError;  // a download of this ROM is in progress; never write into its .part
  }
  // Own temp name: not a ROM file name (entries()/trim ignore it) and not the download's resume path.
  const QString tmp = finalPath(sha256, ext) + QStringLiteral(".adopt");
  const auto fail = [&](CommitResult r) {
    QFile::remove(tmp);
    return r;
  };
  std::error_code ec;
  std::filesystem::copy_file(toFsPath(srcPath), toFsPath(tmp), std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return fail(CommitResult::IoError);
  }
  QString actual;
  if (!sha256OfFile(tmp, &actual)) {
    return fail(CommitResult::IoError);
  }
  if (actual != sha256) {
    return fail(CommitResult::HashMismatch);
  }
  if (QFileInfo::exists(finalPath(sha256, ext))) {
    return fail(CommitResult::Ok);  // a download finished meanwhile
  }
  std::filesystem::rename(toFsPath(tmp), toFsPath(finalPath(sha256, ext)), ec);
  if (ec) {
    return fail(CommitResult::IoError);
  }
  writeSidecar(sha256, ext);
  return CommitResult::Ok;
}

}  // namespace framebeam
