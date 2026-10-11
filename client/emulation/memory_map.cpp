#include "memory_map.h"

#include <algorithm>

#include "third_party/libretro/libretro.h"

namespace framebeam::emu {

MemoryMap::MemoryMap(const retro_memory_map& map) {
  std::vector<Descriptor> v;
  if (map.descriptors) {
    v.reserve(map.num_descriptors);
    for (unsigned i = 0; i < map.num_descriptors; ++i) {
      const retro_memory_descriptor& d = map.descriptors[i];
      Descriptor o;
      o.flags = d.flags;
      o.ptr = d.ptr;
      o.offset = d.offset;
      o.start = d.start;
      o.select = d.select;
      o.disconnect = d.disconnect;
      o.len = d.len;
      if (d.addrspace) o.addrspace = d.addrspace;
      v.push_back(std::move(o));
    }
  }
  *this = fromDescriptors(std::move(v));
}

MemoryMap MemoryMap::fromDescriptors(std::vector<Descriptor> descriptors) {
  MemoryMap m;
  for (Descriptor& d : descriptors) {
    if (d.len == 0) continue;
    // select == 0: matched by range (start <= addr & ~disconnect < start + len), any len.
    m.m_descs.push_back(std::move(d));
  }
  return m;
}

MemoryMap MemoryMap::fromBuffer(void* ptr, size_t len, uint64_t start, std::string addrspace) {
  Descriptor d;
  d.ptr = ptr;
  d.start = start;
  d.len = len;
  d.addrspace = std::move(addrspace);
  MemoryMap m;
  if (len != 0) m.m_descs.push_back(std::move(d));  // select 0 stays: the len bound alone decides (already resolved)
  return m;
}

MemoryMap::Span MemoryMap::resolve(uint64_t addr) const {
  for (const Descriptor& d : m_descs) {
    if (!d.ptr) continue;
    if (((d.start ^ addr) & d.select) != 0) continue;
    const uint64_t masked = addr & ~d.disconnect;
    if (masked < d.start) continue;
    const uint64_t idx = masked - d.start;
    if (idx >= d.len) continue;
    uint64_t avail = d.len - idx;
    if (d.disconnect != 0) {  // bytes stay contiguous only until the next disconnected address bit
      const uint64_t low = d.disconnect & (~d.disconnect + 1);
      avail = std::min(avail, low - (addr & (low - 1)));
    }
    return {static_cast<uint8_t*>(d.ptr) + d.offset + idx, avail};
  }
  return {};
}

std::vector<MemoryMap::RegionInfo> MemoryMap::regions() const {
  std::vector<RegionInfo> out;
  out.reserve(m_descs.size());
  for (const Descriptor& d : m_descs) out.push_back({d.start, d.len, d.flags, d.addrspace});
  return out;
}

}  // namespace framebeam::emu
