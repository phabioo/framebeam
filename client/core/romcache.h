#pragma once

#include <QString>

namespace framebeam {

// Content-addressed, cross-hub ROM cache: <dir>/<sha256>.<ext>.
// Downloads land in <sha256>.<ext>.part; only after the SHA-256 check is it renamed atomically.
// A hit counts only after validation. To avoid hashing large files every time, the
// sidecar file <sha256>.<ext>.ok (size + mtime) records the last successful check.
class RomCache {
 public:
  enum class CommitResult { Ok, HashMismatch, IoError };

  explicit RomCache(const QString& dir);

  static bool isValidSha256(const QString& sha256);  // 64 hex, lowercase
  static QString extensionFromFilename(const QString& filename);  // [a-z0-9]{1,8}, otherwise "bin"
  static bool sha256OfFile(const QString& path, QString* hexOut);

  const QString& dir() const { return dir_; }
  QString finalPath(const QString& sha256, const QString& ext) const;
  QString partPath(const QString& sha256, const QString& ext) const;

  // Quick check without hashing (UI-thread safe). A file with the wrong size is removed.
  enum class Probe { Missing, Valid, Unverified };  // Valid: sidecar matches; Unverified: hash needed (off-thread)
  Probe probe(const QString& sha256, const QString& ext, qint64 expectedSize) const;
  void markVerified(const QString& sha256, const QString& ext) const { writeSidecar(sha256, ext); }
  void dropFinal(const QString& sha256, const QString& ext) const;
  // .part was already verified (incrementally): rename atomically + sidecar.
  bool commitVerified(const QString& sha256, const QString& ext) const;

  // Blocking, validated hit (may hash the whole file; do not use on the UI thread).
  // Validated hit. A corrupted cache file (size/hash mismatch) is removed.
  bool lookup(const QString& sha256, const QString& ext, qint64 expectedSize, QString* pathOut = nullptr) const;

  qint64 partSize(const QString& sha256, const QString& ext) const;
  void discardPart(const QString& sha256, const QString& ext) const;
  // Checks the .part file; Ok: renamed atomically + sidecar. HashMismatch: .part deleted.
  CommitResult verifyAndCommit(const QString& sha256, const QString& ext) const;

 private:
  void writeSidecar(const QString& sha256, const QString& ext) const;
  bool sidecarMatches(const QString& sha256, const QString& ext) const;

  QString dir_;
};

}  // namespace framebeam
