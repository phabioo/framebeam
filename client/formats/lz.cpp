#include "format_helpers.h"

#include "bytes.h"

#include <algorithm>

namespace framebeam::formats {

Result lzDecompress(std::string_view in, size_t maxOut) {
    using namespace detail;
    Result r;
    if (in.size() < 4) {
        r.error = "LZ: input too short";
        return r;
    }
    const uint8_t type = u8at(in, 0);
    if (type != 0x10 && type != 0x11) {
        r.error = "LZ: unknown header type (expected 0x10 or 0x11)";
        return r;
    }
    uint64_t size = u8at(in, 1) | (u8at(in, 2) << 8) | (u8at(in, 3) << 16);
    size_t pos = 4;
    if (size == 0 && type == 0x11) {
        uint32_t ext = 0;
        if (!rd32(in, 4, ext)) {
            r.error = "LZ: truncated extended size";
            return r;
        }
        size = ext;
        pos = 8;
    }
    if (size > maxOut) {
        r.error = "LZ: output exceeds limit";
        return r;
    }
    std::string out;
    out.reserve(static_cast<size_t>(std::min<uint64_t>(size, 1u << 24)));
    while (out.size() < size) {
        if (pos >= in.size()) {
            r.error = "LZ: truncated input";
            return r;
        }
        const uint8_t flags = u8at(in, pos++);
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (!(flags & (1 << bit))) {
                if (pos >= in.size()) {
                    r.error = "LZ: truncated input";
                    return r;
                }
                out.push_back(in[pos++]);
                continue;
            }
            if (pos >= in.size()) {
                r.error = "LZ: truncated input";
                return r;
            }
            const uint32_t b0 = u8at(in, pos++);
            uint32_t len = 0, disp = 0;
            if (type == 0x10) {
                if (pos >= in.size()) {
                    r.error = "LZ: truncated input";
                    return r;
                }
                const uint32_t b1 = u8at(in, pos++);
                len = (b0 >> 4) + 3;
                disp = (((b0 & 0xF) << 8) | b1) + 1;
            } else {
                const uint32_t ind = b0 >> 4;
                const size_t extra = ind == 0 ? 2 : (ind == 1 ? 3 : 1);
                if (pos + extra > in.size()) {
                    r.error = "LZ: truncated input";
                    return r;
                }
                const uint32_t b1 = u8at(in, pos);
                const uint32_t b2 = extra >= 2 ? u8at(in, pos + 1) : 0;
                const uint32_t b3 = extra >= 3 ? u8at(in, pos + 2) : 0;
                if (ind == 0) {
                    len = (((b0 & 0xF) << 4) | (b1 >> 4)) + 0x11;
                    disp = (((b1 & 0xF) << 8) | b2) + 1;
                } else if (ind == 1) {
                    len = (((b0 & 0xF) << 12) | (b1 << 4) | (b2 >> 4)) + 0x111;
                    disp = (((b2 & 0xF) << 8) | b3) + 1;
                } else {
                    len = ind + 1;
                    disp = (((b0 & 0xF) << 8) | b1) + 1;
                }
                pos += extra;
            }
            if (disp > out.size()) {
                r.error = "LZ: back-reference before start of output";
                return r;
            }
            len = static_cast<uint32_t>(std::min<uint64_t>(len, size - out.size()));
            for (uint32_t k = 0; k < len; ++k) out.push_back(out[out.size() - disp]);
        }
    }
    r.ok = true;
    r.data = std::move(out);
    return r;
}

}  // namespace framebeam::formats
