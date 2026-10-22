#include "format_helpers.h"

#include "bytes.h"

namespace uzume::formats {
namespace {

constexpr size_t kStored = 232, kParty = 260, kBlock = 56, kBlocksStart = 8;

// Block order tables for shuffle values 0..23: result block i (A,B,C,D order) is stored at position kPos[sv][i].
const uint8_t kPos[24][4] = {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
                             {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
                             {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
                             {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0}};

void crypt(std::string& d, uint32_t ec) {
    auto run = [&](size_t from, size_t to) {
        uint32_t seed = ec;
        for (size_t i = from; i < to; i += 2) {
            seed = seed * 0x41C64E6Du + 0x6073u;
            const uint32_t x = seed >> 16;
            d[i] = static_cast<char>(static_cast<uint8_t>(d[i]) ^ (x & 0xFF));
            d[i + 1] = static_cast<char>(static_cast<uint8_t>(d[i + 1]) ^ ((x >> 8) & 0xFF));
        }
    };
    run(kBlocksStart, kStored);
    if (d.size() == kParty) run(kStored, kParty);
}

uint16_t checksum(std::string_view d) {
    uint32_t sum = 0;
    for (size_t i = kBlocksStart; i < kStored; i += 2) sum += detail::u8at(d, i) | (detail::u8at(d, i + 1) << 8);
    return static_cast<uint16_t>(sum & 0xFFFF);
}

bool check(std::string_view in, uint32_t& ec, std::string& err) {
    if (in.size() != kStored && in.size() != kParty) {
        err = "PK6: size must be 232 or 260 bytes";
        return false;
    }
    detail::rd32(in, 0, ec);
    return true;
}

}  // namespace

Pk6 pk6Decrypt(std::string_view in) {
    Pk6 r;
    uint32_t ec = 0;
    if (!check(in, ec, r.error)) return r;
    std::string d(in);
    crypt(d, ec);
    const unsigned sv = ((ec >> 13) & 31) % 24;
    std::string out = d;
    for (size_t b = 0; b < 4; ++b)
        out.replace(kBlocksStart + b * kBlock, kBlock, d, kBlocksStart + kPos[sv][b] * kBlock, kBlock);
    uint32_t stored = 0;
    detail::rd16(out, 6, stored);
    r.checksumValid = stored == checksum(out);
    r.data = std::move(out);
    r.ok = true;
    return r;
}

Pk6 pk6Encrypt(std::string_view in) {
    Pk6 r;
    uint32_t ec = 0;
    if (!check(in, ec, r.error)) return r;
    uint32_t stored = 0;
    detail::rd16(in, 6, stored);
    r.checksumValid = stored == checksum(in);
    const unsigned sv = ((ec >> 13) & 31) % 24;
    std::string d(in);
    for (size_t b = 0; b < 4; ++b)
        d.replace(kBlocksStart + kPos[sv][b] * kBlock, kBlock, in.data() + kBlocksStart + b * kBlock, kBlock);
    crypt(d, ec);
    r.data = std::move(d);
    r.ok = true;
    return r;
}

}  // namespace uzume::formats
