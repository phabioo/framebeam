// Generates a minimal, self-written NDS homebrew test ROM (no third-party content).
//   framebeam_make_test_rom <output.nds>
//
// ARM9 (direct boot, loaded to 0x02000000): enables both LCDs and engine A/B, maps VRAM A
// as LCDC, engine A in display mode "VRAM direct" and copies a 256x192 pattern (4 quadrants:
// red, green, blue, white; a gradient strip at the bottom) into VRAM. Engine B shows only the backdrop
// color (palette entry 0), magenta. Then an endless loop. ARM7: endless loop.
// Standard C++ only (no POSIX dependency), output always little-endian.
//
// The header contains no Nintendo logo data (range 0xC0..0x15B is zero); logo and
// header CRC16 are computed over the actual header bytes (melonDS does not check them,
// a real device would not boot this way).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::size_t off, std::uint32_t v) {
  b[off] = static_cast<std::uint8_t>(v);
  b[off + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(Bytes& b, std::size_t off, std::uint32_t v) {
  put16(b, off, v & 0xFFFF);
  put16(b, off + 2, v >> 16);
}
void push32(Bytes& b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::uint16_t crc16(const std::uint8_t* d, std::size_t n) {  // CRC-16/MODBUS as in the NDS header
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int k = 0; k < 8; ++k) crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
  }
  return crc;
}

constexpr int kW = 256, kH = 192;
constexpr std::uint32_t kArm9Ram = 0x02000000;
constexpr std::uint32_t kArm7Ram = 0x02380000;

// Minimal ARM emitter (only the required instructions, condition always AL except bne).
struct Asm {
  Bytes code;
  std::vector<std::pair<std::size_t, std::uint32_t>> lits;  // (position of the LDR, literal)

  void emit(std::uint32_t w) { push32(code, w); }
  std::size_t here() const { return code.size(); }
  void ldrLit(unsigned rd, std::uint32_t value) {  // LDR rd, =value (literal pool at the end)
    lits.push_back({here(), value});
    emit(0xE59F0000u | (rd << 12));
  }
  void strh(unsigned rd, unsigned rn) { emit(0xE1C000B0u | (rn << 16) | (rd << 12)); }
  void strb(unsigned rd, unsigned rn) { emit(0xE5C00000u | (rn << 16) | (rd << 12)); }
  void str(unsigned rd, unsigned rn) { emit(0xE5800000u | (rn << 16) | (rd << 12)); }
  void ldrPost4(unsigned rd, unsigned rn) { emit(0xE4900004u | (rn << 16) | (rd << 12)); }  // LDR rd,[rn],#4
  void strPost4(unsigned rd, unsigned rn) { emit(0xE4800004u | (rn << 16) | (rd << 12)); }  // STR rd,[rn],#4
  void subsImm(unsigned rd, unsigned rn, unsigned imm) { emit(0xE2500000u | (rn << 16) | (rd << 12) | imm); }
  void branch(std::uint32_t cond, std::ptrdiff_t targetOffset) {
    const std::ptrdiff_t rel = (targetOffset - static_cast<std::ptrdiff_t>(here()) - 8) / 4;
    emit((cond << 28) | 0x0A000000u | (static_cast<std::uint32_t>(rel) & 0x00FFFFFFu));
  }
  void finish() {  // append literal pool and patch LDR offsets
    for (auto& [pos, value] : lits) {
      const std::size_t litPos = here();
      push32(code, value);
      const std::uint32_t off = static_cast<std::uint32_t>(litPos - pos - 8);
      code[pos] = static_cast<std::uint8_t>(off);
      code[pos + 1] = static_cast<std::uint8_t>((code[pos + 1] & 0xF0) | ((off >> 8) & 0x0F));
    }
  }
};

std::uint16_t patternPixel(int x, int y) {  // BGR555, bit 15 set
  std::uint16_t c;
  if (y >= 176) c = static_cast<std::uint16_t>((x >> 3) | ((x >> 3) << 5));  // gradient strip (yellow)
  else if (y < kH / 2) c = x < kW / 2 ? 0x001F : 0x03E0;   // red | green
  else c = x < kW / 2 ? 0x7C00 : 0x7FFF;                   // blue | white
  return static_cast<std::uint16_t>(c | 0x8000);
}

Bytes buildArm9() {
  // Code first, then the pattern (24576 words). The data address is fixed by the code length.
  auto make = [](std::uint32_t dataAddr) {
    Asm a;
    a.ldrLit(0, 0x04000304); a.ldrLit(1, 0x8203); a.strh(1, 0);          // POWCNT1: LCDs + engine A/B, A on top
    a.ldrLit(0, 0x04000240); a.ldrLit(1, 0x80);   a.strb(1, 0);          // VRAMCNT_A: enable, LCDC
    a.ldrLit(0, 0x04000000); a.ldrLit(1, 0x00020000); a.str(1, 0);       // DISPCNT A: VRAM direct mode, block A
    a.ldrLit(2, dataAddr);  a.ldrLit(3, 0x06800000); a.ldrLit(5, (kW * kH * 2) / 4);
    const std::size_t loop = a.here();                                   // copy loop
    a.ldrPost4(4, 2); a.strPost4(4, 3); a.subsImm(5, 5, 1);
    a.branch(0x1 /*NE*/, static_cast<std::ptrdiff_t>(loop));
    a.ldrLit(0, 0x05000400); a.ldrLit(1, 0x7C1F); a.strh(1, 0);          // engine B palette[0] = magenta (backdrop)
    a.ldrLit(0, 0x04001000); a.ldrLit(1, 0x00010000); a.str(1, 0);       // DISPCNT B: graphics mode, no BGs
    const std::size_t end = a.here();
    a.branch(0xE, static_cast<std::ptrdiff_t>(end));                     // b .
    a.finish();
    return a.code;
  };
  Bytes code = make(0);
  const std::uint32_t dataAddr = kArm9Ram + static_cast<std::uint32_t>(code.size());
  code = make(dataAddr);
  Bytes out = code;
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; x += 2) push32(out, patternPixel(x, y) | (static_cast<std::uint32_t>(patternPixel(x + 1, y)) << 16));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "Aufruf: %s <output.nds>\n", argv[0]);
    return 2;
  }
  const Bytes arm9 = buildArm9();
  Bytes arm7;
  push32(arm7, 0xEAFFFFFEu);  // b .

  constexpr std::uint32_t kArm9Off = 0x200;
  const std::uint32_t arm7Off = (kArm9Off + static_cast<std::uint32_t>(arm9.size()) + 0x1FF) & ~0x1FFu;
  Bytes rom(arm7Off + 0x200, 0);
  std::copy(arm9.begin(), arm9.end(), rom.begin() + kArm9Off);
  std::copy(arm7.begin(), arm7.end(), rom.begin() + arm7Off);

  const char title[] = "FBTESTROM";
  for (std::size_t i = 0; i < sizeof(title) - 1; ++i) rom[i] = static_cast<std::uint8_t>(title[i]);
  const char code[] = "FBTR";
  for (int i = 0; i < 4; ++i) rom[0x0C + i] = static_cast<std::uint8_t>(code[i]);
  rom[0x10] = '0'; rom[0x11] = '0';  // maker code
  put32(rom, 0x20, kArm9Off);  put32(rom, 0x24, kArm9Ram); put32(rom, 0x28, kArm9Ram); put32(rom, 0x2C, static_cast<std::uint32_t>(arm9.size()));
  put32(rom, 0x30, arm7Off);   put32(rom, 0x34, kArm7Ram); put32(rom, 0x38, kArm7Ram); put32(rom, 0x3C, static_cast<std::uint32_t>(arm7.size()));
  put32(rom, 0x40, arm7Off + 0x200); put32(rom, 0x48, arm7Off + 0x200);  // FNT/FAT empty (offset behind data)
  put32(rom, 0x60, 0x00586000); put32(rom, 0x64, 0x001808F8);            // gamecart timing as usual
  put32(rom, 0x80, static_cast<std::uint32_t>(rom.size()));              // used ROM size
  put32(rom, 0x84, 0x200);                                               // header size (usual for homebrew)
  put16(rom, 0x15C, crc16(&rom[0xC0], 0x9C));                            // logo CRC16 (over the zero range)
  put16(rom, 0x15E, crc16(&rom[0], 0x15E));                              // header CRC16

  std::FILE* f = std::fopen(argv[1], "wb");
  if (!f) {
    std::fprintf(stderr, "Cannot write %s\n", argv[1]);
    return 1;
  }
  const bool ok = std::fwrite(rom.data(), 1, rom.size(), f) == rom.size();
  std::fclose(f);
  return ok ? 0 : 1;
}
