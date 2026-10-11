#include "format_helpers.h"

#include "bytes.h"

namespace framebeam::formats {

Garc garcParse(std::string_view in) {
    using namespace detail;
    Garc g;
    auto fail = [&g](const char* msg) {
        g.ok = false;
        g.files.clear();
        g.error = msg;
        return g;
    };
    uint32_t headerSize = 0, version = 0, dataOffset = 0;
    if (in.size() < 0x1C || in.substr(0, 4) != "GARC") return fail("GARC: bad magic");
    if (!rd32(in, 4, headerSize) || !rd16(in, 0x0A, version) || !rd32(in, 0x10, dataOffset)) return fail("GARC: truncated header");
    if (version != 0x0400 && version != 0x0600) return fail("GARC: unsupported version");
    if (headerSize < 0x1C || !inRange(in.size(), headerSize, 0xC)) return fail("GARC: bad header size");
    if (dataOffset > in.size()) return fail("GARC: data offset out of range");

    // FATO
    const size_t fatoPos = headerSize;
    uint32_t fatoSize = 0, fatoCount = 0;
    if (in.substr(fatoPos, 4) != "OTAF" || !rd32(in, fatoPos + 4, fatoSize) || !rd16(in, fatoPos + 8, fatoCount))
        return fail("GARC: bad FATO chunk");
    if (fatoSize < 0xC + 4ull * fatoCount || !inRange(in.size(), fatoPos, fatoSize)) return fail("GARC: FATO out of range");

    // FATB
    const uint64_t fatbPos = static_cast<uint64_t>(fatoPos) + fatoSize;
    uint32_t fatbSize = 0, fatbCount = 0;
    if (!inRange(in.size(), fatbPos, 0xC) || in.substr(static_cast<size_t>(fatbPos), 4) != "BTAF" ||
        !rd32(in, fatbPos + 4, fatbSize) || !rd32(in, fatbPos + 8, fatbCount))
        return fail("GARC: bad FATB chunk");
    if (fatbCount != fatoCount) return fail("GARC: FATO/FATB entry count mismatch");
    if (fatbSize < 0xC || !inRange(in.size(), fatbPos, fatbSize)) return fail("GARC: FATB out of range");
    const uint64_t fatbBody = fatbPos + 0xC;
    const uint64_t fatbEnd = fatbPos + fatbSize;

    // FIMB
    if (!inRange(in.size(), fatbEnd, 0xC) || in.substr(static_cast<size_t>(fatbEnd), 4) != "BMIF") return fail("GARC: FIMB chunk missing");

    const uint64_t dataSize = in.size() - dataOffset;
    g.files.reserve(fatoCount);
    for (uint32_t i = 0; i < fatoCount; ++i) {
        uint32_t rel = 0;
        rd32(in, fatoPos + 0xC + 4ull * i, rel);
        uint64_t p = fatbBody + rel;
        uint32_t bits = 0;
        if (p + 4 > fatbEnd || !rd32(in, p, bits)) return fail("GARC: FATB entry out of range");
        p += 4;
        std::vector<GarcEntry> subs;
        for (int b = 0; b < 32; ++b) {
            if (!(bits & (1u << b))) continue;
            uint32_t start = 0, end = 0, len = 0;
            if (p + 12 > fatbEnd || !rd32(in, p, start) || !rd32(in, p + 4, end) || !rd32(in, p + 8, len))
                return fail("GARC: FATB sub-entry out of range");
            p += 12;
            if (static_cast<uint64_t>(start) + len > dataSize) return fail("GARC: file data out of range");
            subs.push_back({start, len});
        }
        g.files.push_back(std::move(subs));
    }
    g.dataOffset = dataOffset;
    g.ok = true;
    return g;
}

Result garcFile(std::string_view in, size_t index, size_t sub) {
    Result r;
    const Garc g = garcParse(in);
    if (!g.ok) {
        r.error = g.error;
        return r;
    }
    if (index >= g.files.size() || sub >= g.files[index].size()) {
        r.error = "GARC: file index out of range";
        return r;
    }
    const GarcEntry& e = g.files[index][sub];
    const uint64_t off = static_cast<uint64_t>(g.dataOffset) + e.offset;
    if (!detail::inRange(in.size(), off, e.length)) {
        r.error = "GARC: file data out of range";
        return r;
    }
    r.data.assign(in.substr(static_cast<size_t>(off), e.length));
    r.ok = true;
    return r;
}

}  // namespace framebeam::formats
