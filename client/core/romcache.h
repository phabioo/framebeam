#pragma once

#include <QDateTime>
#include <atomic>
#include <QList>
#include <QSet>
#include <QString>

namespace uzume {

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
  // `cancel` (optional) is checked per 1 MiB chunk; a cancelled hash returns false.
  static bool sha256OfFile(const QString& path, QString* hexOut, const std::atomic_bool* cancel = nullptr);

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

  // ---- Size limit and cleanup (LRU by last use) ----
  struct Entry {
    QString sha256;
    QString ext;
    QString path;
    qint64 size = 0;
    qint64 lastUsedMs = 0;  // Unix ms; files without a record count as used at their modification time
  };
  struct TrimResult {
    qint64 freedBytes = 0;
    int removedFiles = 0;
    qint64 remainingBytes = 0;  // finished ROM files left in the cache
  };
  static constexpr qint64 kDefaultLimitBytes = 20ll * 1024 * 1024 * 1024;  // 20 GB; 0 = unlimited

  // Records "used now" (a hit or a start) so that the file is evicted last. A missing file is ignored.
  void touch(const QString& sha256, const QString& ext, qint64 nowMs = 0) const;
  // Unfinished files (<sha256>.<ext>.part of a download, <sha256>.<ext>.adopt of an adoption). They count towards totalSize()
  // and the limit; trimToLimit() removes the ones older than kStaleTempAgeDays (not protected, not being downloaded), clear()
  // removes every unprotected one.
  struct TempFile {
    QString sha256;
    QString path;
    qint64 size = 0;
    qint64 modifiedMs = 0;
  };
  static constexpr int kStaleTempAgeDays = 7;
  QList<TempFile> tempFiles() const;
  // Finished ROM files (<sha256>.<ext>), least recently used first. .part files and sidecars are not listed.
  QList<Entry> entries() const;
  qint64 totalSize() const;
  // Evicts least recently used files until the finished files fit `limitBytes` (<= 0: nothing is evicted).
  // `protectedHashes` (the running game, files being downloaded) are never removed; a file with a .part next to it counts as
  // being downloaded. Returns what was freed.
  TrimResult trimToLimit(qint64 limitBytes, const QSet<QString>& protectedHashes = {}) const;
  // Bytes a clear() would free right now (everything except protected files).
  qint64 clearableSize(const QSet<QString>& protectedHashes = {}) const;
  // Removes all finished ROM files except the protected ones (and those being downloaded).
  TrimResult clear(const QSet<QString>& protectedHashes = {}) const;

  // Clean-up suggestion "games not played in N days" (Settings > This PC > Storage): finished ROM files whose last use is older than
  // `cutoffMs` (Unix ms), never the protected ones (running game, downloads in progress) and never a file with a .part next to it.
  // Oldest first. Saves are not part of this cache.
  static constexpr int kUnusedGameDays = 60;
  QList<Entry> unusedSince(qint64 cutoffMs, const QSet<QString>& protectedHashes = {}) const;
  TrimResult removeUnusedSince(qint64 cutoffMs, const QSet<QString>& protectedHashes = {}) const;

  qint64 partSize(const QString& sha256, const QString& ext) const;
  void discardPart(const QString& sha256, const QString& ext) const;
  // Checks the .part file; Ok: renamed atomically + sidecar. HashMismatch: .part deleted.
  CommitResult verifyAndCommit(const QString& sha256, const QString& ext) const;
  // Adopts a local file (e.g. a just uploaded ROM) into the cache: copies it to <sha256>.<ext>.adopt (never the download's .part), verifies SHA-256, then renames atomically.
  // Ok without copying when a valid cache file already exists. IoError when a download of the same ROM is running (.part exists).
  // A stale .adopt after a crash is ignored by entries() and overwritten by the next adopt.
  // Blocking (copies and hashes the whole file; do not use on the UI thread).
  CommitResult adoptFile(const QString& srcPath, const QString& sha256, const QString& ext) const;

 private:
  void writeSidecar(const QString& sha256, const QString& ext) const;
  bool sidecarMatches(const QString& sha256, const QString& ext) const;

  QString dir_;
};

}  // namespace uzume
