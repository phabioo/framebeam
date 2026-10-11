#pragma once
// Adapter between the plugin runtime and the emulation layer: plugins::MemoryProvider on top of emu::MemoryWatchSet
// (docs/architecture/12-plugins.md 16.3). Plugins register ranges, the emulation thread captures them after a frame,
// plugins read the latest immutable snapshot. Thread-safe; the watch set can be replaced (live-save restart).
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "memory_snapshot.h"
#include "plugin_memory.h"

namespace framebeam::ui {

class MemoryWatchAdapter : public plugins::MemoryProvider {
 public:
  explicit MemoryWatchAdapter(std::shared_ptr<emu::MemoryWatchSet> set = nullptr);
  ~MemoryWatchAdapter() override;

  // Attaches to another watch set (a new runner) and registers the ranges of all owners there. nullptr detaches.
  void bind(std::shared_ptr<emu::MemoryWatchSet> set);
  // Unregisters all ranges from the current set (game stopped).
  void clear();

  void setWatches(const std::string& owner, std::vector<plugins::WatchRange> ranges) override;
  bool read(uint64_t addr, void* out, size_t len) const override;
  std::optional<uint64_t> frame() const override;
  uint64_t generation() const override;

 private:
  std::shared_ptr<const emu::MemorySnapshot> latest() const;

  mutable std::mutex m_mutex;
  std::shared_ptr<emu::MemoryWatchSet> m_set;
  std::map<std::string, std::vector<plugins::WatchRange>> m_owners;
};

}  // namespace framebeam::ui
