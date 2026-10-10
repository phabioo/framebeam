#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace framebeam {

class ProfileStore;

// Local sync state of one game save slot (<game dir>/sync.json). Never contains secrets.
struct SyncState {
  QString slot = QStringLiteral("default");
  int baseRevision = 0;      // Hub checkpoint revision the local file is based on (0 = none)
  QString lastSyncedSha256;  // hash of the file as last known identical to the Hub
  bool pending = false;      // local changes not (yet) on the Hub
  QString conflictId;        // open conflict, uploads paused while set
  QString lastError;
  QDateTime updatedAt;
  // Core (id + version) that last wrote this save on this Player (ADR 0020 D7); empty = not recorded yet.
  QString writerCoreId;
  QString writerCoreVersion;
  // Core-managed save file (DeSmuME .dsv): hash of that file as last exported from / imported into the raw save.
  QString coreFileSha256;
};

// File logic of the save sync (no network, no GUI).
// Layout: <base>/hubs/<hub_id>/users/<user_id>/saves/<game_id>/ (core save dir + sync.json).
class SaveStore {
 public:
  static bool isSafeId(const QString& id);
  // Names Windows aliases or cannot create: trailing '.' or ' ', reserved device names (CON, PRN, AUX, NUL, COM1-9,
  // LPT1-9, any case, also with an extension). Rejected for ids, slot names and Hub ids on every platform.
  static bool isWindowsAliasName(const QString& name);
  // Empty if one of the IDs is invalid.
  static QString gameDir(const ProfileStore& profiles, const QString& hubId, const QString& userId, const QString& gameId);
  static QString userSavesDir(const ProfileStore& profiles, const QString& hubId, const QString& userId);
  static QString legacyDir(const ProfileStore& profiles, const QString& hubId);  // hubs/<hub_id>/saves

  // Slots (ADR 0012 D7). Slot names follow the Hub rule ^[a-z0-9_-]{1,32}$ (server ValidSlotName).
  // Layout: slot "default" keeps the pre-0.4 game directory (existing saves stay "default" without moving files),
  // every other slot lives in <game dir>/slots/<slot>/ (core save dir + sync.json). Empty if an ID or the slot is invalid.
  static bool isValidSlotName(const QString& slot);
  static QString slotDir(const ProfileStore& profiles, const QString& hubId, const QString& userId, const QString& gameId,
                         const QString& slot);
  // Short directory handed to cores that cannot cope with long paths (core profile save.short_dir, Windows MAX_PATH):
  // <baseDir>/c/<first 16 hex of sha256("<hubId>/<userId>/<gameId>/<slot>")>. Empty for unsafe ids or an invalid slot.
  static QString shortCoreDir(const ProfileStore& profiles, const QString& hubId, const QString& userId, const QString& gameId,
                              const QString& slot);
  // One-time best effort: moves <oldDir>/<subfolder> to <newDir>/<subfolder> if the old one exists and the new one does
  // not (rename, else copy; the source is removed only after a complete copy). True if moved.
  static bool migrateCoreSubfolder(const QString& oldDir, const QString& newDir, const QString& subfolder);
  // One-time best effort for a single file written by a core (DeSmuME .dsv): <oldDir>/<fileName> -> <newDir>/<fileName>.
  // Never overwrites: if the target exists the source stays (KeptBoth).
  enum class FileMove { None, Moved, KeptBoth, Failed };
  static FileMove migrateCoreFile(const QString& oldDir, const QString& newDir, const QString& fileName);
  // True if <dir>/<name with longestNameChars characters> would exceed the narrow Windows limit (default 259), whatever the OS.
  static bool exceedsLegacyPathLimit(const QString& dir, int longestNameChars, int limit = 259);
  static QString slotDirIn(const QString& gameDir, const QString& slot);
  // Slots with a local directory for this game ("default" first when its game dir has a sync.json or save file).
  static QStringList localSlots(const QString& gameDir);

  // Save file name the core writes for a ROM path (melonDS DS: <rom basename>.sav in the save directory).
  static QString expectedSaveName(const QString& romPath);
  // Path of the save file in a game dir: the expected name, otherwise the newest other candidate
  // (warning appended to *warnings if there are several). Empty if there is none.
  // exactOnly: only `expectedName` (profiled cores: never another file); core-managed mirrors (.dsv) are never candidates.
  static QString findSaveFile(const QString& gameDir, const QString& expectedName, QStringList* warnings = nullptr, bool exactOnly = false);
  // Save file name for a ROM path and extension (".sav" raw / ".dsv" core file).
  static QString expectedSaveName(const QString& romPath, const QString& extension);

  static SyncState loadState(const QString& gameDir);
  static bool saveState(const QString& gameDir, SyncState state);
  static QString stateFilePath(const QString& gameDir);

  static QString sha256OfFile(const QString& path);  // lowercase hex, empty if unreadable
  static QString sha256Of(const QByteArray& data);
  static bool atomicWrite(const QString& path, const QByteArray& data);
  // Copies <path> to <path>.<tag>-<timestamp>.bak (tag set) or <path>.bak (empty tag). Returns the backup path.
  static QString backupFile(const QString& path, const QString& tag = QString());
  // Copies (never deletes) a legacy save <legacy>/<basename>.sav to <gameDir>/<expectedName> if the game dir has
  // no save yet. Candidates are the ROM basenames in `basenames`. True if copied.
  static bool migrateLegacy(const QString& legacyDir, const QStringList& basenames, const QString& gameDir,
                            const QString& expectedName);
};

}  // namespace framebeam
