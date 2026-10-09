#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include "hubprotocol.h"

namespace framebeam {

// Core package cache: <root>/<core_id>/<version>/<platform>/<file> plus package.json (the metadata as served by the
// Hub). root = <data>/cache/cores. A library counts only after package.json exists and size + SHA-256 of the
// library match it (hash result memoized per process by path, size and mtime). Contents are never logged.
class CoreCache {
 public:
  enum class StoreResult { Ok, SizeMismatch, HashMismatch, IoError, InvalidArgument };

  explicit CoreCache(const QString& root);

  // Platform of this build: windows-x64 | linux-x64 | linux-arm64 | macos-x64 | macos-arm64 (compile-time macros);
  // empty on an unsupported platform.
  static QString currentPlatform();
  // Numeric dot-segment compare, then string compare (like the Hub): <0, 0, >0.
  static int compareVersions(const QString& a, const QString& b);

  const QString& root() const { return root_; }
  QString packageDir(const QString& coreId, const QString& version, const QString& platform) const;  // empty if invalid
  QString filePath(const QString& coreId, const QString& version, const QString& platform, const QString& name) const;

  // Size + SHA-256 check of one file of the package (memoized).
  bool fileValid(const CorePackageInfo& pkg, const CorePackageFile& file) const;
  // Validates data against size and sha256 and stores it atomically (temp file in the same directory, rename).
  StoreResult store(const CorePackageInfo& pkg, const CorePackageFile& file, const QByteArray& data) const;
  // Writes package.json (only call when every file is stored).
  bool writePackage(const CorePackageInfo& pkg) const;
  std::optional<CorePackageInfo> readPackage(const QString& coreId, const QString& version, const QString& platform) const;
  // Library path of a complete, valid package; empty otherwise.
  QString libraryPath(const QString& coreId, const QString& version, const QString& platform) const;
  // Cached versions with a package.json for this platform, newest first (validity of the files is not checked).
  QStringList versions(const QString& coreId, const QString& platform) const;

 // Tests only: how long a file must have been unchanged before its verified hash is memoized (default 2 s).
  void setMemoSettleMsForTest(qint64 ms) { memoSettleMs_ = ms; }

 private:
  struct Memo {
    qint64 size = 0;
    qint64 mtimeMs = 0;
    qint64 ctimeMs = 0;  // metadata change time
    QString sha256;  // hash that was verified
  };
  QString root_;
  qint64 memoSettleMs_ = 2000;
  mutable QHash<QString, Memo> memo_;
};

}  // namespace framebeam
