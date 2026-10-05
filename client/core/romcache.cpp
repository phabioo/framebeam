#include "romcache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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

}  // namespace framebeam
