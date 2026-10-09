#include "dsvsave.h"

#include <cstring>

namespace framebeam::dsv {

namespace {
constexpr const char kFooterText[] = "|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:";
constexpr const char kCookie[] = "|-DESMUME SAVE-|";
constexpr size_t kTextLen = sizeof(kFooterText) - 1;
constexpr size_t kCookieLen = sizeof(kCookie) - 1;
constexpr size_t kFields = 6 * 4;

// save_types[1..12] of mc.cpp: size and address size (index = footer "type").
struct SaveType {
  quint32 size;
  quint32 addrSize;
};
constexpr SaveType kTypes[] = {{0, 0},          {0x200, 1},      {0x2000, 2},     {0x10000, 2},    {0x8000, 2},
                               {0x40000, 3},    {0x80000, 3},    {0x100000, 3},   {0x200000, 3},   {0x400000, 3},
                               {0x800000, 3},   {0x1000000, 3},  {0x2000000, 3}};
constexpr int kTypeCount = static_cast<int>(sizeof(kTypes) / sizeof(kTypes[0]));

void put32(QByteArray& out, quint32 v) {
  for (int i = 0; i < 4; ++i) out.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}
quint32 get32(const char* p) {
  quint32 v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<quint32>(static_cast<unsigned char>(p[i])) << (8 * i);
  return v;
}
std::nullopt_t fail(QString* error, const QString& why) {
  if (error != nullptr) *error = why;
  return std::nullopt;
}
}  // namespace

size_t footerSize() { return kTextLen + kFields + kCookieLen; }

std::optional<QByteArray> rawToDsv(const QByteArray& raw, QString* error) {
  for (int t = 1; t < kTypeCount; ++t) {
    if (static_cast<quint32>(raw.size()) != kTypes[t].size) continue;
    QByteArray out = raw;
    out.append(kFooterText, static_cast<qsizetype>(kTextLen));
    put32(out, kTypes[t].size);  // size of data actually written (a full save here)
    put32(out, kTypes[t].size);  // padSize
    put32(out, static_cast<quint32>(t));
    put32(out, kTypes[t].addrSize);
    put32(out, kTypes[t].size);  // mem_size
    put32(out, 0);               // version
    out.append(kCookie, static_cast<qsizetype>(kCookieLen));
    return out;
  }
  return fail(error, QStringLiteral("%1 bytes is not a DeSmuME save size").arg(raw.size()));
}

std::optional<QByteArray> dsvToRaw(const QByteArray& dsv, QString* error) {
  const qsizetype fs = static_cast<qsizetype>(footerSize());
  if (dsv.size() < fs) return fail(error, QStringLiteral("file is shorter than the DeSmuME footer"));
  if (std::memcmp(dsv.constData() + dsv.size() - static_cast<qsizetype>(kCookieLen), kCookie, kCookieLen) != 0) {
    return fail(error, QStringLiteral("DeSmuME footer marker missing"));
  }
  const char* f = dsv.constData() + dsv.size() - static_cast<qsizetype>(kCookieLen + kFields);
  const quint32 written = get32(f), pad = get32(f + 4), type = get32(f + 8), addr = get32(f + 12), mem = get32(f + 16), version = get32(f + 20);
  if (version != 0) return fail(error, QStringLiteral("unknown DeSmuME footer version %1").arg(version));
  if (type < 1 || type >= static_cast<quint32>(kTypeCount)) return fail(error, QStringLiteral("unknown save type %1").arg(type));
  if (pad != kTypes[type].size || mem != pad || addr != kTypes[type].addrSize || written > pad) {
    return fail(error, QStringLiteral("inconsistent DeSmuME footer"));
  }
  if (static_cast<qsizetype>(pad) != dsv.size() - fs) return fail(error, QStringLiteral("save data size does not match the footer"));
  if (std::memcmp(dsv.constData() + pad, kFooterText, kTextLen) != 0) return fail(error, QStringLiteral("DeSmuME footer text missing"));
  return dsv.left(static_cast<qsizetype>(pad));
}

}  // namespace framebeam::dsv
