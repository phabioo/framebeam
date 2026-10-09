#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace framebeam::dsv {

// DeSmuME backup format (".dsv", libretro DeSmuME writes <content basename>.dsv into the save directory):
//   <padSize bytes of raw cartridge save> + footer
//   footer = ASCII text "|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:"
//            + 6 x u32 LE: written size, padSize, type, addr_size, mem_size, version (0)
//            + ASCII cookie "|-DESMUME SAVE-|"
// The Hub stores the raw cartridge save only (ADR 0020 D7); the Player converts around the core.
// Layout and tables taken from libretro/desmume (master, retrieved 2026-10-09):
//   desmume/src/mc.cpp sha256 82a579c5bf3bc7d824f90526600ee97b95162097df8f1e0a340ef1a79d191b06
//   desmume/src/mc.h   sha256 3d663d47b00356d6a87e5eb7023804da905ee34e30f2d6a4086f044e6396af68
// (BackupDevice::ensure writes the footer, readFooter / GetDSVFooterSize read it; save_types[] gives type and addr_size.)

size_t footerSize();

// Raw cartridge save -> .dsv bytes. Fails (nullopt, *error) when the size is no DeSmuME save size.
std::optional<QByteArray> rawToDsv(const QByteArray& raw, QString* error = nullptr);
// .dsv bytes -> raw cartridge save. Validates the footer completely (cookie, version, sizes, type, address size);
// never guesses: nullopt + *error for anything that is not a well-formed DeSmuME save.
std::optional<QByteArray> dsvToRaw(const QByteArray& dsv, QString* error = nullptr);

}  // namespace framebeam::dsv
