#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Seam between the plugin runtime and the emulation layer (wired in a later package). The emulation thread copies the
// registered ranges into a snapshot after a frame; plugins only ever read that copy (docs/architecture/12-plugins.md 16.3).
namespace uzume::plugins {

struct WatchRange {
  uint64_t addr = 0;
  uint32_t len = 0;
};

class MemoryProvider {
 public:
  virtual ~MemoryProvider() = default;
  // Replaces the ranges registered by `owner` (a plugin id). Thread-safe. Included in the snapshots from the next one on.
  virtual void setWatches(const std::string& owner, std::vector<WatchRange> ranges) = 0;
  // Copies [addr, addr+len) of the latest snapshot into `out`; false when the range is not (completely) in it.
  virtual bool read(uint64_t addr, void* out, size_t len) const = 0;
  // Frame number of the latest snapshot; nullopt = none yet.
  virtual std::optional<uint64_t> frame() const = 0;
  // Changes with every new snapshot.
  virtual uint64_t generation() const = 0;
};

}  // namespace uzume::plugins
