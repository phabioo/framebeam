#pragma once
// MemoryMap: device virtual address -> host pointer, built from a libretro memory map (SET_MEMORY_MAPS). Plain C++,
// no Qt. Address semantics follow RetroArch/rcheevos: a descriptor claims `addr` when ((start ^ addr) & select) == 0;
// the offset inside the block is (addr & ~disconnect) - start; the host pointer is ptr + offset + that. The first
// matching descriptor in the core's order wins; descriptors without a host pointer are skipped. A descriptor with
// select == 0 is matched by range instead: start <= (addr & ~disconnect) < start + len, any len.
//
// Threading: resolve() reads only immutable data, but the host pointers belong to the core and are written by it while
// retro_run() executes: use a map only on the emulation thread between frames (or via a MemorySnapshot).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct retro_memory_map;

namespace framebeam::emu {

class MemoryMap {
 public:
  struct Span {
    uint8_t* ptr = nullptr;  // host pointer for the address, null = unmapped
    uint64_t len = 0;        // contiguous bytes available from there inside the matching descriptor
  };
  struct RegionInfo {
    uint64_t start = 0;
    uint64_t len = 0;
    uint64_t flags = 0;
    std::string addrspace;
  };
  // Plain copy of retro_memory_descriptor (for tests and the buffer fallback).
  struct Descriptor {
    uint64_t flags = 0;
    void* ptr = nullptr;
    uint64_t offset = 0;
    uint64_t start = 0;
    uint64_t select = 0;
    uint64_t disconnect = 0;
    uint64_t len = 0;
    std::string addrspace;
  };

  MemoryMap() = default;
  // Copies the descriptors; the core's pointers must stay valid while the game is loaded. Descriptors with len 0 are dropped.
  explicit MemoryMap(const retro_memory_map& map);
  static MemoryMap fromDescriptors(std::vector<Descriptor> descriptors);
  // One plain buffer mapped at `start` (e.g. the core's SYSTEM_RAM at virtual address 0).
  static MemoryMap fromBuffer(void* ptr, size_t len, uint64_t start = 0, std::string addrspace = {});

  bool empty() const { return m_descs.empty(); }
  Span resolve(uint64_t addr) const;
  std::vector<RegionInfo> regions() const;

 private:
  std::vector<Descriptor> m_descs;  // select resolved
};

}  // namespace framebeam::emu
