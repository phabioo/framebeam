#pragma once
// Internal bounds-checked little-endian readers.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace framebeam::formats::detail {

inline bool inRange(size_t size, uint64_t off, uint64_t len) {
    return off <= size && len <= size - off;
}
inline uint8_t u8at(std::string_view s, size_t o) { return static_cast<uint8_t>(s[o]); }
inline bool rd16(std::string_view s, uint64_t o, uint32_t& v) {
    if (!inRange(s.size(), o, 2)) return false;
    v = u8at(s, o) | (u8at(s, o + 1) << 8);
    return true;
}
inline bool rd32(std::string_view s, uint64_t o, uint32_t& v) {
    if (!inRange(s.size(), o, 4)) return false;
    v = static_cast<uint32_t>(u8at(s, o)) | (static_cast<uint32_t>(u8at(s, o + 1)) << 8) |
        (static_cast<uint32_t>(u8at(s, o + 2)) << 16) | (static_cast<uint32_t>(u8at(s, o + 3)) << 24);
    return true;
}
inline bool rd64(std::string_view s, uint64_t o, uint64_t& v) {
    uint32_t lo = 0, hi = 0;
    if (!rd32(s, o, lo) || !rd32(s, o + 4, hi)) return false;
    v = lo | (static_cast<uint64_t>(hi) << 32);
    return true;
}

}  // namespace framebeam::formats::detail
