#pragma once
// Game identity from the game file's header, for plugins (docs/architecture/12-plugins.md §16.2, Game start).
// Reads only the few header bytes needed (seeks; game files can be several GB).
#include <QString>

namespace framebeam::emu {

struct GameIdentity {
  QString code;  // NDS: 4-character game code; 3DS: program ID as 16 uppercase hex digits; empty when unknown
  QString kind;  // "nds-gamecode" or "3ds-titleid"; empty when unknown
};

// system: "nds" or "3ds". Unknown system, unreadable or invalid file -> empty identity.
// 3DS: .3ds/.cci (NCSD, magic at 0x100; first partition's NCCH) and bare .cxi (NCCH, magic at 0x100); encrypted or
// compressed containers that do not show the magic yield an empty identity.
GameIdentity readGameIdentity(const QString& path, const QString& system);

}  // namespace framebeam::emu
