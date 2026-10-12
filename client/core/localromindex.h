#pragma once
// Local ROM index (0.12): ROM files found in the user's library folders, with SHA-256 and game identity.
// The scan walks the folders, hashes new or changed files and reads the game code; a hash cache keyed by
// path + size + mtime makes sure a 4 GB file is hashed once. Scanning runs off the UI thread (LocalRomIndex::rescan).
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <optional>

namespace framebeam {

struct LocalRomFile {
  QString path;
  qint64 size = 0;
  qint64 mtimeMs = 0;
  QString sha256;
  QString system;     // "nds", "3ds", ...
  QString gameCode;   // empty when the header could not be read
  int gameVersion = -1;
  QString filename() const;
};

// What the header reader returns for one file (core/ does not link the emulation library; the caller passes the
// reader, see emu::readGameIdentity).
struct LocalRomIdentity {
  QString code;
  int version = -1;
};
using LocalIdentityFn = std::function<LocalRomIdentity(const QString& path, const QString& system)>;

// Persistent hash cache: <file> JSON, entries keyed by path + size + mtime.
class RomHashCache {
 public:
  explicit RomHashCache(const QString& filePath = QString());
  std::optional<LocalRomFile> lookup(const QString& path, qint64 size, qint64 mtimeMs) const;
  void put(const LocalRomFile& f);
  void retainOnly(const QSet<QString>& paths);  // drops entries of files that are gone
  bool save() const;
  int size() const { return static_cast<int>(entries_.size()); }
  QList<LocalRomFile> all() const { return entries_.values(); }

 private:
  QString file_;
  QHash<QString, LocalRomFile> entries_;
};

struct LocalScanOptions {
  QStringList folders;
  QMap<QString, QString> extensionToSystem;  // ".nds" -> "nds" (lowercase, with dot)
  LocalIdentityFn identity;                  // optional
  // Called after each file with (files done, files total); may be empty.
  std::function<void(int, int)> progress;
  const std::atomic_bool* cancel = nullptr;
};

struct LocalScanResult {
  QList<LocalRomFile> files;
  int hashed = 0;     // files that had to be hashed (cache misses)
  int unreadable = 0;
  bool cancelled = false;
};

// Blocking scan (runs in a worker thread in the app). Updates `cache`.
LocalScanResult scanLibraryFolders(const LocalScanOptions& opt, RomHashCache& cache);

class LocalRomIndex : public QObject {
  Q_OBJECT
 public:
  explicit LocalRomIndex(const QString& cacheFilePath, QObject* parent = nullptr);
  ~LocalRomIndex() override;

  bool scanning() const { return scanning_; }
  const QList<LocalRomFile>& files() const { return files_; }
  // Starts an asynchronous scan; false if one runs already. The result arrives via scanFinished().
  bool rescan(const LocalScanOptions& options);
  void cancelScan();

  // Path of a local file with this SHA-256 that still exists unchanged (size + mtime; no hashing, cheap stat), else
  // empty. A changed or missing file yields an empty path until the next scan.
  QString pathForSha(const QString& sha256);
  QList<LocalRomFile> filesFor(const QString& sha256) const;
  // Removes all files and the cache (no folders configured any more).
  void clear();

 signals:
  void scanStarted();
  void scanProgress(int done, int total);
  void scanFinished(int fileCount, int hashed, bool cancelled);

 private:
  QString cacheFile_;
  QList<LocalRomFile> files_;
  bool scanning_ = false;
  std::atomic_bool cancel_{false};
  quint64 generation_ = 0;
};

}  // namespace framebeam
