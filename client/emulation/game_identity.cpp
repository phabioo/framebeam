#include "game_identity.h"

#include <QFile>

#include <cstring>

namespace framebeam::emu {

namespace {
bool readAt(QFile& f, qint64 pos, char* buf, qint64 len) {
  return f.seek(pos) && f.read(buf, len) == len;
}
quint32 le32(const char* p) {
  const auto* u = reinterpret_cast<const unsigned char*>(p);
  return u[0] | (u[1] << 8) | (u[2] << 16) | (static_cast<quint32>(u[3]) << 24);
}
quint64 le64(const char* p) { return le32(p) | (static_cast<quint64>(le32(p + 4)) << 32); }

GameIdentity ndsIdentity(QFile& f) {
  char code[4];
  char ver = 0;
  if (!readAt(f, 0x0C, code, 4) || !readAt(f, 0x1E, &ver, 1)) return {};
  for (char c : code)
    if (c < 0x20 || c > 0x7E) return {};  // printable ASCII only
  return {QString::fromLatin1(code, 4), QStringLiteral("nds-gamecode"), static_cast<unsigned char>(ver)};
}

GameIdentity ncchIdentity(QFile& f, qint64 base) {
  char hdr[0x120];  // through the program ID at +0x118
  if (!readAt(f, base, hdr, sizeof hdr)) return {};
  if (memcmp(hdr + 0x100, "NCCH", 4) != 0) return {};
  const quint64 id = le64(hdr + 0x118);
  const auto* u = reinterpret_cast<const unsigned char*>(hdr);
  const int version = u[0x112] | (u[0x113] << 8);
  return {QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0')).toUpper(), QStringLiteral("3ds-titleid"), version};
}

GameIdentity threeDsIdentity(QFile& f) {
  char magic[4];
  if (!readAt(f, 0x100, magic, 4)) return {};
  if (memcmp(magic, "NCCH", 4) == 0) return ncchIdentity(f, 0);
  if (memcmp(magic, "NCSD", 4) != 0) return {};
  char off[4];
  if (!readAt(f, 0x120, off, 4)) return {};  // partition table entry 0: offset in media units (0x200)
  const quint32 units = le32(off);
  if (units == 0) return {};
  return ncchIdentity(f, static_cast<qint64>(units) * 0x200);
}
}  // namespace

GameIdentity readGameIdentity(const QString& path, const QString& system) {
  if (path.isEmpty()) return {};
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  if (system == QLatin1String("nds")) return ndsIdentity(f);
  if (system == QLatin1String("3ds")) return threeDsIdentity(f);
  return {};
}

}  // namespace framebeam::emu
