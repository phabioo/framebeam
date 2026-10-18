#pragma once
// Synthetic generators for game-file formats (header-only, std only). They build files from the documented
// formats (3dbrew, PKHeX crypto description) with implementations independent of client/formats, so tests
// can cross-check the library. No real game data is ever involved.

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace framebeam::synth {

inline void put8(std::string& s, uint8_t v) { s.push_back(static_cast<char>(v)); }
inline void put16(std::string& s, uint32_t v) { put8(s, v & 0xFF); put8(s, (v >> 8) & 0xFF); }
inline void put32(std::string& s, uint32_t v) { put16(s, v & 0xFFFF); put16(s, v >> 16); }
inline void put64(std::string& s, uint64_t v) { put32(s, static_cast<uint32_t>(v)); put32(s, static_cast<uint32_t>(v >> 32)); }
inline void set32(std::string& s, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) s[off + static_cast<size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline void set16(std::string& s, size_t off, uint32_t v) {
    s[off] = static_cast<char>(v & 0xFF);
    s[off + 1] = static_cast<char>((v >> 8) & 0xFF);
}
inline void padTo(std::string& s, size_t align) { while (s.size() % align) s.push_back('\0'); }

// ---- LZ10 / LZ11 (raw literal + back-reference encoder, greedy, window 4096) ----
// type 0x10 or 0x11; useMatches=false emits literals only.
inline std::string makeLz(std::string_view d, int type, bool useMatches = true) {
    std::string out;
    put8(out, static_cast<uint8_t>(type));
    if (d.size() > 0xFFFFFF || (type == 0x11 && d.empty())) {
        put8(out, 0); put8(out, 0); put8(out, 0);
        put32(out, static_cast<uint32_t>(d.size()));
    } else {
        put8(out, d.size() & 0xFF); put8(out, (d.size() >> 8) & 0xFF); put8(out, (d.size() >> 16) & 0xFF);
    }
    const size_t maxLen = type == 0x10 ? 18 : 0x10110;
    size_t pos = 0;
    while (pos < d.size()) {
        const size_t flagsAt = out.size();
        put8(out, 0);
        for (int bit = 7; bit >= 0 && pos < d.size(); --bit) {
            size_t bestLen = 0, bestDisp = 0;
            if (useMatches) {
                for (size_t disp = 1; disp <= 4096 && disp <= pos; ++disp) {
                    size_t l = 0;
                    while (l < maxLen && pos + l < d.size() && d[pos + l] == d[pos + l - disp]) ++l;
                    if (l > bestLen) { bestLen = l; bestDisp = disp; }
                }
            }
            if (bestLen < 3) {
                put8(out, static_cast<uint8_t>(d[pos++]));
                continue;
            }
            out[flagsAt] = static_cast<char>(static_cast<uint8_t>(out[flagsAt]) | (1 << bit));
            const uint32_t dm = static_cast<uint32_t>(bestDisp - 1);
            const uint32_t len = static_cast<uint32_t>(bestLen);
            if (type == 0x10) {
                put8(out, ((len - 3) << 4) | (dm >> 8));
                put8(out, dm & 0xFF);
            } else if (len <= 0x10) {
                put8(out, ((len - 1) << 4) | (dm >> 8));
                put8(out, dm & 0xFF);
            } else if (len <= 0x110) {
                const uint32_t v = len - 0x11;
                put8(out, v >> 4);
                put8(out, ((v & 0xF) << 4) | (dm >> 8));
                put8(out, dm & 0xFF);
            } else {
                const uint32_t v = len - 0x111;
                put8(out, 0x10 | (v >> 12));
                put8(out, (v >> 4) & 0xFF);
                put8(out, ((v & 0xF) << 4) | (dm >> 8));
                put8(out, dm & 0xFF);
            }
            pos += bestLen;
        }
    }
    return out;
}
inline std::string makeLz11(std::string_view d, bool useMatches = true) { return makeLz(d, 0x11, useMatches); }
inline std::string makeLz10(std::string_view d, bool useMatches = true) { return makeLz(d, 0x10, useMatches); }

// ---- GARC ----
// files[i] = sub-files of entry i. version 0x0400 (header 0x1C) or 0x0600 (header 0x20). Content padded to 4 bytes.
inline std::string makeGarc(const std::vector<std::vector<std::string>>& files, uint32_t version = 0x0400) {
    const uint32_t headerSize = version == 0x0600 ? 0x20 : 0x1C;
    std::string fato, fatb, data;
    uint32_t largest = 0;
    for (const auto& subs : files) {
        put32(fato, static_cast<uint32_t>(fatb.size()));
        uint32_t bits = subs.empty() ? 0 : (subs.size() >= 32 ? 0xFFFFFFFFu : ((1u << subs.size()) - 1));
        put32(fatb, bits);
        for (const auto& s : subs) {
            const uint32_t start = static_cast<uint32_t>(data.size());
            data += s;
            padTo(data, 4);
            put32(fatb, start);
            put32(fatb, static_cast<uint32_t>(data.size()));
            put32(fatb, static_cast<uint32_t>(s.size()));
            largest = (std::max)(largest, static_cast<uint32_t>(s.size()));
        }
    }
    std::string fatoChunk = "OTAF";
    put32(fatoChunk, 0xC + static_cast<uint32_t>(fato.size()));
    put16(fatoChunk, static_cast<uint32_t>(files.size()));
    put16(fatoChunk, 0xFFFF);
    fatoChunk += fato;
    std::string fatbChunk = "BTAF";
    put32(fatbChunk, 0xC + static_cast<uint32_t>(fatb.size()));
    put32(fatbChunk, static_cast<uint32_t>(files.size()));
    fatbChunk += fatb;
    std::string fimb = "BMIF";
    put32(fimb, 0xC);
    put32(fimb, static_cast<uint32_t>(data.size()));
    const uint32_t dataOffset = headerSize + static_cast<uint32_t>(fatoChunk.size() + fatbChunk.size()) + 0xC;
    std::string out = "CRAG";
    put32(out, headerSize);
    put16(out, 0xFEFF);
    put16(out, version);
    put32(out, 4);
    put32(out, dataOffset);
    put32(out, dataOffset + static_cast<uint32_t>(data.size()));
    put32(out, largest);
    if (version == 0x0600) put32(out, 4);
    out += fatoChunk;
    out += fatbChunk;
    out += fimb;
    out += data;
    return out;
}
inline std::string makeGarc(const std::vector<std::string>& files, uint32_t version = 0x0400) {
    std::vector<std::vector<std::string>> v;
    for (const auto& f : files) v.push_back({f});
    return makeGarc(v, version);
}

// ---- NCCH + RomFS ----
// Offsets inside the generated NCCH (for corruption tests): RomFS at 0x1000, IVFC level 3 at RomFS+0x1000.
constexpr size_t kNcchRomfsOffset = 0x1000;
constexpr size_t kRomfsLevel3Offset = 0x1000;
constexpr size_t kRomfsDirMetaOffset = 0x34;  // inside level 3

inline std::string makeRomfs(const std::map<std::string, std::string>& files) {
    struct Dir { std::string name; int parent; std::vector<int> dirs, fls; };
    struct File { std::string name; int parent; const std::string* data; };
    std::vector<Dir> dirs{{"", 0, {}, {}}};
    std::vector<File> fls;
    for (const auto& [path, data] : files) {
        int cur = 0;
        size_t i = 0;
        std::vector<std::string> comps;
        while (i < path.size()) {
            size_t j = path.find('/', i);
            if (j == std::string::npos) j = path.size();
            if (j > i) comps.push_back(path.substr(i, j - i));
            i = j + 1;
        }
        for (size_t c = 0; c + 1 < comps.size(); ++c) {
            int found = -1;
            for (int d : dirs[static_cast<size_t>(cur)].dirs)
                if (dirs[static_cast<size_t>(d)].name == comps[c]) found = d;
            if (found < 0) {
                dirs.push_back({comps[c], cur, {}, {}});
                found = static_cast<int>(dirs.size()) - 1;
                dirs[static_cast<size_t>(cur)].dirs.push_back(found);
            }
            cur = found;
        }
        fls.push_back({comps.back(), cur, &data});
        dirs[static_cast<size_t>(cur)].fls.push_back(static_cast<int>(fls.size()) - 1);
    }
    auto entrySize = [](size_t hdr, const std::string& n) { return hdr + ((n.size() * 2 + 3) & ~size_t(3)); };
    std::vector<uint32_t> dOff, fOff;
    uint32_t dTotal = 0, fTotal = 0;
    for (const auto& d : dirs) { dOff.push_back(dTotal); dTotal += static_cast<uint32_t>(entrySize(0x18, d.name)); }
    for (const auto& f : fls) { fOff.push_back(fTotal); fTotal += static_cast<uint32_t>(entrySize(0x20, f.name)); }
    auto utf16 = [](std::string& o, const std::string& n) {
        for (char c : n) { put8(o, static_cast<uint8_t>(c)); put8(o, 0); }
        padTo(o, 4);
    };
    std::string dirMeta, fileMeta, fileData;
    for (size_t i = 0; i < dirs.size(); ++i) {
        const Dir& d = dirs[i];
        uint32_t sib = 0xFFFFFFFF;
        if (i != 0) {
            const auto& sibs = dirs[static_cast<size_t>(d.parent)].dirs;
            auto it = std::find(sibs.begin(), sibs.end(), static_cast<int>(i));
            if (it + 1 != sibs.end()) sib = dOff[static_cast<size_t>(*(it + 1))];
        }
        put32(dirMeta, dOff[static_cast<size_t>(d.parent)]);
        put32(dirMeta, sib);
        put32(dirMeta, d.dirs.empty() ? 0xFFFFFFFF : dOff[static_cast<size_t>(d.dirs.front())]);
        put32(dirMeta, d.fls.empty() ? 0xFFFFFFFF : fOff[static_cast<size_t>(d.fls.front())]);
        put32(dirMeta, 0xFFFFFFFF);
        put32(dirMeta, static_cast<uint32_t>(d.name.size() * 2));
        utf16(dirMeta, d.name);
    }
    for (size_t i = 0; i < fls.size(); ++i) {
        const File& f = fls[i];
        const auto& sibs = dirs[static_cast<size_t>(f.parent)].fls;
        auto it = std::find(sibs.begin(), sibs.end(), static_cast<int>(i));
        const uint32_t sib = (it + 1 != sibs.end()) ? fOff[static_cast<size_t>(*(it + 1))] : 0xFFFFFFFF;
        padTo(fileData, 0x10);
        put32(fileMeta, dOff[static_cast<size_t>(f.parent)]);
        put32(fileMeta, sib);
        put64(fileMeta, fileData.size());
        put64(fileMeta, f.data->size());
        put32(fileMeta, 0xFFFFFFFF);
        put32(fileMeta, static_cast<uint32_t>(f.name.size() * 2));
        utf16(fileMeta, f.name);
        fileData += *f.data;
    }
    // Level 3: header 0x28, dir hash (3 buckets), dir meta, file hash (3 buckets), file meta, file data.
    std::string l3;
    const uint32_t dirHashOff = 0x28, dirMetaOff = dirHashOff + 12;
    const uint32_t fileHashOff = dirMetaOff + dTotal, fileMetaOff = fileHashOff + 12;
    uint32_t dataOff = fileMetaOff + fTotal;
    dataOff = (dataOff + 0xF) & ~0xFu;
    put32(l3, 0x28);
    put32(l3, dirHashOff); put32(l3, 12);
    put32(l3, dirMetaOff); put32(l3, dTotal);
    put32(l3, fileHashOff); put32(l3, 12);
    put32(l3, fileMetaOff); put32(l3, fTotal);
    put32(l3, dataOff);
    for (int i = 0; i < 3; ++i) put32(l3, 0xFFFFFFFF);
    l3 += dirMeta;
    for (int i = 0; i < 3; ++i) put32(l3, 0xFFFFFFFF);
    l3 += fileMeta;
    while (l3.size() < dataOff) l3.push_back('\0');
    l3 += fileData;
    // IVFC header: master hash 0x20 bytes, level 3 block size 2^12 -> level 3 at align(0x60 + 0x20, 0x1000).
    std::string r = "IVFC";
    put32(r, 0x10000);
    put32(r, 0x20);
    for (int lvl = 0; lvl < 3; ++lvl) {
        put64(r, 0);
        put64(r, lvl == 2 ? l3.size() : 0x1000);
        put32(r, 12);
        put32(r, 0);
    }
    put32(r, 0x5C);
    while (r.size() < 0x60) r.push_back('\0');
    r.append(0x20, '\x11');  // master hash (not verified)
    while (r.size() < kRomfsLevel3Offset) r.push_back('\0');
    r += l3;
    padTo(r, 0x200);
    return r;
}

// Decrypted-by-flag NCCH with only a RomFS. noCrypto=false sets no NoCrypto flag (= "encrypted").
inline std::string makeNcchWithRomfs(const std::map<std::string, std::string>& files, uint64_t programId = 0x0004000000123400ull,
                                     bool noCrypto = true) {
    const std::string romfs = makeRomfs(files);
    std::string h(0x200, '\0');
    h.replace(0x100, 4, "NCCH");
    const uint32_t total = static_cast<uint32_t>((kNcchRomfsOffset + romfs.size()) / 0x200);
    set32(h, 0x104, total);
    for (int i = 0; i < 8; ++i) {
        h[0x108 + static_cast<size_t>(i)] = static_cast<char>((programId >> (8 * i)) & 0xFF);
        h[0x118 + static_cast<size_t>(i)] = static_cast<char>((programId >> (8 * i)) & 0xFF);
    }
    h[0x18F] = noCrypto ? 4 : 0;
    set32(h, 0x1B0, static_cast<uint32_t>(kNcchRomfsOffset / 0x200));
    set32(h, 0x1B4, static_cast<uint32_t>(romfs.size() / 0x200));
    set32(h, 0x1B8, 1);
    while (h.size() < kNcchRomfsOffset) h.push_back('\0');
    return h + romfs;
}

// NCSD wrapper: partition 0 at 0x4000.
inline std::string wrapNcsd(const std::string& ncch) {
    std::string h(0x200, '\0');
    h.replace(0x100, 4, "NCSD");
    const uint32_t unitsTotal = static_cast<uint32_t>((0x4000 + ncch.size()) / 0x200);
    set32(h, 0x104, unitsTotal);
    set32(h, 0x120, 0x4000 / 0x200);
    set32(h, 0x124, static_cast<uint32_t>(ncch.size() / 0x200));
    while (h.size() < 0x4000) h.push_back('\0');
    return h + ncch;
}

// ---- BCLIM ----
// Supports BCLIM format RGBA8 (9) and RGB565 (5). rgba is straight RGBA8888, row-major top-down. The stored image
// is padded to a power of two (min 8) and tiled in 8x8 Morton tiles.
inline std::string makeBclim(int width, int height, int format, const std::string& rgba) {
    auto pow2 = [](int v) { int p = 8; while (p < v) p <<= 1; return p; };
    const int sw = pow2(width), sh = pow2(height);
    const int bytesPer = format == 9 ? 4 : 2;
    std::string data(static_cast<size_t>(sw) * static_cast<size_t>(sh) * static_cast<size_t>(bytesPer), '\0');
    for (int ty = 0; ty < sh / 8; ++ty) {
        for (int tx = 0; tx < sw / 8; ++tx) {
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    int morton = 0;
                    for (int bit = 0; bit < 3; ++bit) {
                        morton |= ((px >> bit) & 1) << (2 * bit);
                        morton |= ((py >> bit) & 1) << (2 * bit + 1);
                    }
                    const int x = tx * 8 + px, y = ty * 8 + py;
                    uint8_t c[4] = {0, 0, 0, 0};
                    if (x < width && y < height)
                        for (int k = 0; k < 4; ++k) c[k] = static_cast<uint8_t>(rgba[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4 + static_cast<size_t>(k)]);
                    const size_t idx = (static_cast<size_t>(ty) * static_cast<size_t>(sw / 8) + static_cast<size_t>(tx)) * 64 + static_cast<size_t>(morton);
                    char* o = &data[idx * static_cast<size_t>(bytesPer)];
                    if (format == 9) {
                        o[0] = static_cast<char>(c[3]); o[1] = static_cast<char>(c[2]); o[2] = static_cast<char>(c[1]); o[3] = static_cast<char>(c[0]);
                    } else {
                        const uint32_t v = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3);
                        o[0] = static_cast<char>(v & 0xFF); o[1] = static_cast<char>(v >> 8);
                    }
                }
            }
        }
    }
    std::string f = "CLIM";
    put16(f, 0xFEFF);
    put16(f, 0x14);
    put32(f, 0x02020000);
    put32(f, static_cast<uint32_t>(data.size() + 0x28));
    put16(f, 1);
    put16(f, 0);
    f += "imag";
    put32(f, 0x10);
    put16(f, static_cast<uint32_t>(width));
    put16(f, static_cast<uint32_t>(height));
    put8(f, static_cast<uint8_t>(format));
    put8(f, 0);
    put16(f, 0);
    put32(f, static_cast<uint32_t>(data.size()));
    return data + f;
}

// ---- PK6 ----
struct SynthPk6 {
    std::string plain;      // decrypted, A,B,C,D order, correct checksum
    std::string encrypted;  // as stored in the save / memory
};
// Builds a deterministic PK6 (232 or 260 bytes) from ec, species (u16 at 0x08) and a filler seed.
inline SynthPk6 makePk6(uint32_t ec, uint16_t species, bool party = false, uint32_t fillSeed = 1) {
    std::string p(party ? 260 : 232, '\0');
    uint32_t x = fillSeed * 2654435761u + 1;
    for (size_t i = 4; i < p.size(); ++i) {
        x = x * 1664525u + 1013904223u;
        p[i] = static_cast<char>(x >> 24);
    }
    set32(p, 0, ec);
    set16(p, 4, 0);  // sanity
    set16(p, 8, species);
    uint32_t sum = 0;
    for (size_t i = 8; i < 232; i += 2) sum += static_cast<uint8_t>(p[i]) | (static_cast<uint8_t>(p[i + 1]) << 8);
    set16(p, 6, sum & 0xFFFF);
    // Encrypt: shuffle (stored block position table), then XOR with the LCRNG stream.
    static const int order[24][4] = {
        {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},{1,0,2,3},{1,0,3,2},{2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},
        {1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},{2,3,0,1},{3,2,0,1},{1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0}};
    const int sv = static_cast<int>(((ec >> 13) & 0x1F) % 24);
    std::string e = p;
    for (int b = 0; b < 4; ++b) {
        // plain block b is stored at position order[sv][b]
        for (size_t k = 0; k < 56; ++k) e[8 + static_cast<size_t>(order[sv][b]) * 56 + k] = p[8 + static_cast<size_t>(b) * 56 + k];
    }
    auto stream = [&](size_t from, size_t to) {
        uint64_t seed = ec;
        for (size_t i = from; i < to; i += 2) {
            seed = (seed * 0x41C64E6Dull + 0x6073ull) & 0xFFFFFFFFull;
            const uint32_t w = static_cast<uint32_t>(seed >> 16);
            e[i] = static_cast<char>(static_cast<uint8_t>(e[i]) ^ (w & 0xFF));
            e[i + 1] = static_cast<char>(static_cast<uint8_t>(e[i + 1]) ^ (w >> 8));
        }
    };
    stream(8, 232);
    if (party) stream(232, 260);
    return {p, e};
}

}  // namespace framebeam::synth
