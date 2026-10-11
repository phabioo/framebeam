#include "memory_snapshot.h"

#include <algorithm>
#include <cstring>

namespace framebeam::emu {

namespace {
constexpr uint64_t kUnmappedStep = 256;  // granularity when skipping unmapped addresses
}

bool MemorySnapshot::read(uint64_t addr, void* out, size_t len) const {
  if (len == 0) return true;
  if (addr + len < addr) return false;
  auto it = std::upper_bound(m_segments.begin(), m_segments.end(), addr,
                             [](uint64_t a, const Segment& s) { return a < s.addr; });
  if (it == m_segments.begin()) return false;
  --it;
  if (addr < it->addr || addr - it->addr + len > it->len) return false;
  std::memcpy(out, m_data.data() + it->offset + (addr - it->addr), len);
  return true;
}

void MemoryWatchSet::setRanges(const std::string& owner, std::vector<WatchRange> ranges) {
  std::lock_guard<std::mutex> l(m_mutex);
  if (ranges.empty()) m_owners.erase(owner);
  else m_owners[owner] = std::move(ranges);
  rebuildLocked();
}

void MemoryWatchSet::clear(const std::string& owner) {
  std::lock_guard<std::mutex> l(m_mutex);
  m_owners.erase(owner);
  rebuildLocked();
}

bool MemoryWatchSet::empty() const { return m_empty.load(std::memory_order_acquire); }

void MemoryWatchSet::rebuildLocked() {
  std::vector<WatchRange> all;
  for (const auto& [owner, ranges] : m_owners) {
    for (const WatchRange& r : ranges) {
      if (r.len == 0 || r.len > kMaxRangeBytes || r.addr + r.len < r.addr) continue;  // over the limit: reads invalid
      all.push_back(r);
    }
  }
  std::sort(all.begin(), all.end(), [](const WatchRange& a, const WatchRange& b) { return a.addr < b.addr; });
  auto merged = std::make_shared<std::vector<Merged>>();
  for (const WatchRange& r : all) {
    const uint64_t end = r.addr + r.len;
    if (!merged->empty() && r.addr <= merged->back().addr + merged->back().len) {  // overlapping or adjacent
      Merged& m = merged->back();
      m.len = std::max(m.addr + m.len, end) - m.addr;
    } else {
      merged->push_back({r.addr, r.len});
    }
  }
  uint64_t total = 0;  // budget in ascending address order
  size_t keep = 0;
  for (; keep < merged->size(); ++keep) {
    if (total + (*merged)[keep].len > kMaxTotalBytes) break;
    total += (*merged)[keep].len;
  }
  merged->resize(keep);
  m_empty.store(merged->empty(), std::memory_order_release);
  m_merged = std::move(merged);
}

void MemoryWatchSet::capture(const MemoryMap& map, uint64_t frame) {
  if (m_empty.load(std::memory_order_acquire)) return;
  std::shared_ptr<const std::vector<Merged>> ranges;
  {
    std::lock_guard<std::mutex> l(m_mutex);
    ranges = m_merged;
  }
  if (!ranges || ranges->empty()) return;

  auto snap = std::make_shared<MemorySnapshot>();
  snap->m_frame = frame;
  uint64_t total = 0;
  for (const Merged& m : *ranges) total += m.len;
  snap->m_data.reserve(static_cast<size_t>(total));
  for (const Merged& m : *ranges) {
    uint64_t addr = m.addr;
    const uint64_t end = m.addr + m.len;
    while (addr < end) {
      const MemoryMap::Span s = map.resolve(addr);
      if (!s.ptr) {  // unmapped: skip ahead, those bytes stay invalid
        addr = std::min(end, (addr / kUnmappedStep + 1) * kUnmappedStep);
        continue;
      }
      const uint64_t n = std::min(s.len, end - addr);
      const size_t off = snap->m_data.size();
      snap->m_data.insert(snap->m_data.end(), s.ptr, s.ptr + n);
      if (!snap->m_segments.empty() && snap->m_segments.back().addr + snap->m_segments.back().len == addr) {
        snap->m_segments.back().len += n;  // contiguous with the previous piece (data is contiguous too)
      } else {
        snap->m_segments.push_back({addr, n, off});
      }
      addr += n;
    }
  }
  {
    std::lock_guard<std::mutex> l(m_mutex);
    m_latest = std::move(snap);
  }
  m_generation.fetch_add(1, std::memory_order_release);
}

std::shared_ptr<const MemorySnapshot> MemoryWatchSet::latest() const {
  std::lock_guard<std::mutex> l(m_mutex);
  return m_latest;
}

uint64_t MemoryWatchSet::generation() const { return m_generation.load(std::memory_order_acquire); }

}  // namespace framebeam::emu
