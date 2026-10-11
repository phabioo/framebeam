#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace framebeam::sdtree {

// Save data of a 3DS game inside the core's virtual SD tree (Azahar, ADR 0020 D7 "core_tree" / "azahar_sdmc"),
// packed into ONE deterministic ustar archive. The Hub stores that archive as the slot's raw save.
//
// Managed paths, relative to the tree root, '/' separators (everything else - nand, sysdata, shaders, cheats,
// title/.../content - is never read, written or deleted):
//   sdmc/Nintendo 3DS/<32 hex>/<32 hex>/title/<8 hex>/<8 hex>/data/<rest>
//   sdmc/Nintendo 3DS/<32 hex>/<32 hex>/extdata/<rest>
// Every component is non-empty, not "." / "..", free of '\\', ':' and control characters and no Windows alias name.

// Upper bound of one archive; keep equal to kMaxSaveBytes (network/saveapi.h, static_assert in savesync.cpp).
inline constexpr qint64 kMaxTreeBytes = 64LL * 1024 * 1024;
inline constexpr int kMaxTreeFiles = 4096;

// True if `relPath` (as in the archive) is a managed path.
bool isManagedPath(const QString& relPath);

// Regular files only (hidden ones included, symlinks skipped), sorted by relative path (byte order); ustar, mode
// 0644, uid/gid 0, mtime 0: identical content gives identical bytes. No managed files: an empty QByteArray (no
// save yet). nullopt + *error when too large (kMaxTreeBytes / kMaxTreeFiles) or a file cannot be read.
std::optional<QByteArray> pack(const QString& root, QString* error = nullptr);

// Validates the whole archive first (header checksum, ustar / GNU magic, regular files, '5' directories ignored,
// anything else rejected, every file path managed, no duplicates, size limits, only zero blocks after the end,
// at least one file) and touches nothing on failure. Then deletes managed files below `root` that are not in the
// archive and writes every file atomically.
bool unpack(const QByteArray& tar, const QString& root, QString* error = nullptr);

// The parse and validation part of unpack() alone: true if unpack() would accept this archive's content (nothing is
// read from or written to disk).
bool validate(const QByteArray& tar, QString* error = nullptr);

// Cheap hash of (path, size, mtime) of the managed files, for polling. Empty when there are none.
QString fingerprint(const QString& root);

}  // namespace framebeam::sdtree
