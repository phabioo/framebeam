#pragma once
// Memory snapshots for plugins (docs/architecture/12-plugins.md §16.3): after a frame the emulation thread copies the
// registered address ranges into an immutable MemorySnapshot; other threads read the latest one without ever touching
// the core's memory. Plain C++, no Qt.
//
// Limits per tick: one registered range may be at most kMaxRangeBytes (4 MiB), the merged ranges together at most
// kMaxTotalBytes (16 MiB). Ranges over the limit are not copied and read as invalid (read() returns false); when the
// total is exceeded, merged ranges are taken in ascending address order until the budget is used up.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "memory_map.h"

namespace uzume::emu {

struct WatchRange {
  uint64_t addr = 0;
  uint32_t len = 0;
};

inline constexpr uint32_t kMaxRangeBytes = 4u * 1024 * 1024;
inline constexpr uint64_t kMaxTotalBytes = 16ull * 1024 * 1024;

class MemoryWatchSet;

// Immutable after capture, safe to read from any thread.
class MemorySnapshot {
 public:
  uint64_t frame() const { return m_frame; }
  // True only when [addr, addr+len) lies inside one captured valid range (unmapped or over-limit parts are invalid).
  bool read(uint64_t addr, void* out, size_t len) const;

 private:
  friend class MemoryWatchSet;
  struct Segment {
    uint64_t addr;
    uint64_t len;
    size_t offset;  // into m_data
  };
  uint64_t m_frame = 0;
  std::vector<Segment> m_segments;  // ascending, non-overlapping, contiguous neighbours coalesced
  std::vector<uint8_t> m_data;
};

// Thread-safe. Owners (plugins) register ranges; the emulation thread captures them.
class MemoryWatchSet {
 public:
  void setRanges(const std::string& owner, std::vector<WatchRange> ranges);
  void clear(const std::string& owner);
  bool empty() const;

  // Emulation thread, between frames. Copies the merged ranges and publishes a new snapshot; returns immediately
  // (no allocation, no lock) when nothing is watched. Unmapped parts of a range are invalid.
  void capture(const MemoryMap& map, uint64_t frame);

  std::shared_ptr<const MemorySnapshot> latest() const;
  uint64_t generation() const;  // increments per published snapshot

 private:
  struct Merged {
    uint64_t addr;
    uint64_t len;
  };
  void rebuildLocked();

  mutable std::mutex m_mutex;
  std::map<std::string, std::vector<WatchRange>> m_owners;
  std::shared_ptr<const std::vector<Merged>> m_merged;
  std::shared_ptr<const MemorySnapshot> m_latest;
  std::atomic_bool m_empty{true};
  std::atomic<uint64_t> m_generation{0};
};

}  // namespace uzume::emu
