#pragma once
// Game identity from the game file's header, for plugins (docs/architecture/12-plugins.md §16.2, Game start).
// Reads only the few header bytes needed (seeks; game files can be several GB).
#include <QString>

namespace framebeam::emu {

struct GameIdentity {
  QString code;  // NDS/GBA: 4-character game code; 3DS: program ID as 16 uppercase hex digits; GB/GBC: the header title; empty when unknown
  QString kind;  // "nds-gamecode", "3ds-titleid", "gba-gamecode" or "gb-title"; empty when unknown
  // GBA and GB/GBC only (empty / -1 otherwise): header title, maker code (GBA: 2 characters; GB: new licensee code, or the
  // old licensee byte as 2 hex digits) and the mask ROM version byte.
  QString title;
  QString maker;
  int version = -1;
};

// Names a manifest can give in "identity".
inline constexpr const char* kIdentityNdsHeader = "nds_header";
inline constexpr const char* kIdentity3dsNcsd = "3ds_ncsd";
inline constexpr const char* kIdentityGbHeader = "gb_header";
inline constexpr const char* kIdentityGbaHeader = "gba_header";

// reader: the manifest's "identity" (SystemManifest::identity). Unknown or empty reader, unreadable or invalid file -> empty identity.
// GB/GBC (gb_header): title at 0x134 (11 bytes when the CGB flag at 0x143 is set, else 16), only valid when the header
// checksum at 0x14D matches. GBA (gba_header): game code at 0xAC, maker 0xB0, version 0xBC, title 0xA0 (12 bytes).
// 3DS: .3ds/.cci (NCSD, magic at 0x100; first partition's NCCH) and bare .cxi (NCCH, magic at 0x100); encrypted or
// compressed containers that do not show the magic yield an empty identity.
GameIdentity readGameIdentity(const QString& path, const QString& reader);

}  // namespace framebeam::emu
