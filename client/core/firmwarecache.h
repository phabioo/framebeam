#pragma once

#include <QByteArray>
#include <QString>

namespace framebeam {

// Content-addressed firmware cache: <dir>/<system>/<sha256> (dir = <data>/system/firmware), separate from ROMs.
// A hit counts only after size and SHA-256 were checked. Contents are never logged (only ids and hashes).
class FirmwareCache {
 public:
  enum class StoreResult { Ok, SizeMismatch, HashMismatch, IoError, InvalidArgument };

  explicit FirmwareCache(const QString& dir);

  static bool isValidSystemId(const QString& id);  // [a-z0-9_-]{1,32}
  static bool isValidSha256(const QString& sha256);  // 64 hex, lowercase

  const QString& dir() const { return dir_; }
  QString path(const QString& system, const QString& sha256) const;  // empty for invalid arguments

  // Cheap check without hashing (UI thread): file exists with the expected size.
  bool probe(const QString& system, const QString& sha256, qint64 expectedSize) const;
  // Validated hit (size + SHA-256). A corrupted file is removed.
  bool lookup(const QString& system, const QString& sha256, qint64 expectedSize, QString* pathOut = nullptr) const;
  // Validates data against size and sha256 and stores it atomically. Nothing is written on mismatch.
  StoreResult store(const QString& system, const QString& sha256, qint64 expectedSize, const QByteArray& data) const;

 private:
  QString dir_;
};

}  // namespace framebeam
