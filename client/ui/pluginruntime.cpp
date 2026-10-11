#include "pluginruntime.h"

namespace framebeam::ui {

namespace {
std::vector<emu::WatchRange> toEmu(const std::vector<plugins::WatchRange>& in) {
  std::vector<emu::WatchRange> out;
  out.reserve(in.size());
  for (const auto& r : in) out.push_back({r.addr, r.len});
  return out;
}
}  // namespace

MemoryWatchAdapter::MemoryWatchAdapter(std::shared_ptr<emu::MemoryWatchSet> set) : m_set(std::move(set)) {}

MemoryWatchAdapter::~MemoryWatchAdapter() { clear(); }

void MemoryWatchAdapter::bind(std::shared_ptr<emu::MemoryWatchSet> set) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_set && m_set != set)
    for (const auto& kv : m_owners) m_set->clear(kv.first);
  m_set = std::move(set);
  if (m_set)
    for (const auto& kv : m_owners) m_set->setRanges(kv.first, toEmu(kv.second));
}

void MemoryWatchAdapter::clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_set)
    for (const auto& kv : m_owners) m_set->clear(kv.first);
  m_owners.clear();
}

void MemoryWatchAdapter::setWatches(const std::string& owner, std::vector<plugins::WatchRange> ranges) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (ranges.empty()) {
    m_owners.erase(owner);
    if (m_set) m_set->clear(owner);
    return;
  }
  if (m_set) m_set->setRanges(owner, toEmu(ranges));
  m_owners[owner] = std::move(ranges);
}

std::shared_ptr<const emu::MemorySnapshot> MemoryWatchAdapter::latest() const {
  std::shared_ptr<emu::MemoryWatchSet> set;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    set = m_set;
  }
  return set ? set->latest() : nullptr;
}

bool MemoryWatchAdapter::read(uint64_t addr, void* out, size_t len) const {
  const auto snap = latest();
  return snap && snap->read(addr, out, len);
}

std::optional<uint64_t> MemoryWatchAdapter::frame() const {
  const auto snap = latest();
  if (!snap) return std::nullopt;
  return snap->frame();
}

uint64_t MemoryWatchAdapter::generation() const {
  std::shared_ptr<emu::MemoryWatchSet> set;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    set = m_set;
  }
  return set ? set->generation() : 0;
}

}  // namespace framebeam::ui
