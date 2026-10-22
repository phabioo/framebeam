#include "game_identity.h"

#include <QFile>

#include <cstring>

namespace uzume::emu {

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
  if (!readAt(f, 0x120, off, 4)) return {};  // partition table entry 0: offset in media units
  const quint32 units = le32(off);
  if (units == 0) return {};
  // Media unit size = 0x200 << shift (NCSD header byte 0x18E), like formats/romfs.cpp (PE-13). Real images use shift 0.
  char shiftByte = 0;
  if (!readAt(f, 0x18E, &shiftByte, 1)) return {};
  const unsigned shift = static_cast<unsigned char>(shiftByte);
  if (shift > 8) return {};
  return ncchIdentity(f, static_cast<qint64>(units) * (qint64(0x200) << shift));
}

bool printableAscii(const char* p, int n) {
  for (int i = 0; i < n; ++i)
    if (p[i] < 0x20 || p[i] > 0x7E) return false;
  return true;
}
// Header text field: up to the first NUL, control and non-ASCII bytes shown as '?', trimmed.
QString headerText(const char* p, int n) {
  QString s;
  for (int i = 0; i < n && p[i] != '\0'; ++i) s.append(p[i] >= 0x20 && p[i] <= 0x7E ? QLatin1Char(p[i]) : QLatin1Char('?'));
  return s.trimmed();
}

// Offsets: GBA cartridge header as documented in GBATEK.
GameIdentity gbaIdentity(QFile& f) {
  char h[0xC0];  // through the version byte at 0xBC
  if (!readAt(f, 0, h, sizeof h)) return {};
  if (!printableAscii(h + 0xAC, 4)) return {};
  GameIdentity id{QString::fromLatin1(h + 0xAC, 4), QStringLiteral("gba-gamecode")};
  id.title = headerText(h + 0xA0, 12);
  id.maker = printableAscii(h + 0xB0, 2) ? QString::fromLatin1(h + 0xB0, 2) : QString();
  id.version = static_cast<unsigned char>(h[0xBC]);
  return id;
}

// Offsets and header checksum: Pan Docs, "The Cartridge Header".
GameIdentity gbIdentity(QFile& f) {
  char h[0x150];
  if (!readAt(f, 0, h, sizeof h)) return {};
  quint8 sum = 0;  // header checksum over 0x134..0x14C
  for (int i = 0x134; i <= 0x14C; ++i) sum = static_cast<quint8>(sum - static_cast<unsigned char>(h[i]) - 1);
  if (sum != static_cast<unsigned char>(h[0x14D])) return {};
  const unsigned char cgb = static_cast<unsigned char>(h[0x143]);
  GameIdentity id;
  id.title = headerText(h + 0x134, (cgb & 0x80) ? 11 : 16);
  if (id.title.isEmpty()) return {};
  id.code = id.title;
  id.kind = QStringLiteral("gb-title");
  const unsigned char old = static_cast<unsigned char>(h[0x14B]);
  id.maker = old == 0x33 ? headerText(h + 0x144, 2) : QStringLiteral("%1").arg(old, 2, 16, QLatin1Char('0')).toUpper();
  id.version = static_cast<unsigned char>(h[0x14C]);
  return id;
}
}  // namespace

GameIdentity readGameIdentity(const QString& path, const QString& reader) {
  if (path.isEmpty()) return {};
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  if (reader == QLatin1String(kIdentityNdsHeader)) return ndsIdentity(f);
  if (reader == QLatin1String(kIdentity3dsNcsd)) return threeDsIdentity(f);
  if (reader == QLatin1String(kIdentityGbHeader)) return gbIdentity(f);
  if (reader == QLatin1String(kIdentityGbaHeader)) return gbaIdentity(f);
  return {};
}

}  // namespace uzume::emu
