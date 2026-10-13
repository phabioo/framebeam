#pragma once
// Slot state machine of the zero-copy display ring (ADR 0027, design D4). Pure C++ (no Qt, no COM, no GL): the D3D11
// display ring wraps it, tests run it on every platform.
//
// One DisplaySlots object describes ONE generation: kCount slots of one fixed texture size. Growing the textures
// means a new generation (a new DisplaySlots); the ring switches to it and drops the old one.
//
// Per slot:
//   Free     nobody uses it (the producer's side holds the keyed-mutex key 0 conceptually, see the ring)
//   Writing  the producer (emulation thread) renders into it           producer: beginWrite -> endWrite | abortWrite
//   Ready    published, not yet taken by the consumer                  producer: endWrite
//   Shown    taken by the consumer (render thread), on screen          consumer: takeShown
//   Ready -> Free is the consumer recycling a superseded frame ("older pending slots", newest wins): the producer
//   never touches a Ready slot because only the consumer can acquire it again (keyed mutex key 1).
//
// Transitions (anything else is refused and returns false/-1):
//   Free    -> Writing  beginWrite()
//   Writing -> Ready    endWrite()      (gets the next sequence number)
//   Writing -> Free     abortWrite()
//   Ready   -> Shown    takeShown()     (consumer; the slot being replaced stays Shown until releaseShown())
//   Ready   -> Free     recycle()       (consumer; a superseded pending frame)
//   Shown   -> Free     releaseShown()  (consumer; after Qt has executed the frame that used it)
//
// Thread-safe: one mutex, tiny critical sections, never held across a COM or GL call.
#include <QtGlobal>
#include <mutex>
#include <utility>
#include <vector>

namespace framebeam {

class DisplaySlots {
 public:
  static constexpr int kCount = 3;
  enum class State { Free, Writing, Ready, Shown };
  struct Frame {
    int slot = -1;  // -1: none
    int width = 0;
    int height = 0;
    quint64 sequence = 0;  // 1, 2, ...; higher = newer
  };

  // Producer. A Free slot (lowest index) moves to Writing; -1 when every slot is Writing/Ready/Shown (the frame is
  // skipped and counted).
  int beginWrite() {
    std::lock_guard lock(m_);
    for (int i = 0; i < kCount; ++i) {
      if (slots_[i].state == State::Free) {
        slots_[i].state = State::Writing;
        return i;
      }
    }
    ++skipped_;
    return -1;
  }
  // Producer: the w x h frame in `slot` is complete. Writing -> Ready; returns its sequence (0: refused).
  quint64 endWrite(int slot, int width, int height) {
    std::lock_guard lock(m_);
    if (!valid(slot) || slots_[slot].state != State::Writing) {
      return 0;
    }
    slots_[slot].state = State::Ready;
    slots_[slot].width = width;
    slots_[slot].height = height;
    slots_[slot].sequence = ++sequence_;
    return slots_[slot].sequence;
  }
  bool abortWrite(int slot) { return move(slot, State::Writing, State::Free); }

  // Consumer. The newest Ready frame (slot -1: none). Read-only: the consumer first acquires the keyed mutex, then
  // calls takeShown(); a timeout simply leaves the slot Ready for the next frame.
  Frame newestReady() const {
    std::lock_guard lock(m_);
    Frame best;
    for (int i = 0; i < kCount; ++i) {
      if (slots_[i].state == State::Ready && slots_[i].sequence > best.sequence) {
        best = {i, slots_[i].width, slots_[i].height, slots_[i].sequence};
      }
    }
    return best;
  }
  // Consumer: every Ready slot older than `sequence` (the superseded pending frames), oldest first.
  std::vector<int> readyOlderThan(quint64 sequence) const {
    std::lock_guard lock(m_);
    std::vector<int> out;
    for (int i = 0; i < kCount; ++i) {
      if (slots_[i].state == State::Ready && slots_[i].sequence < sequence) {
        out.push_back(i);
      }
    }
    for (size_t a = 1; a < out.size(); ++a) {  // at most 2 entries
      if (slots_[out[a]].sequence < slots_[out[a - 1]].sequence) {
        std::swap(out[a], out[a - 1]);
      }
    }
    return out;
  }
  bool takeShown(int slot) { return move(slot, State::Ready, State::Shown); }
  bool recycle(int slot) { return move(slot, State::Ready, State::Free); }
  bool releaseShown(int slot) { return move(slot, State::Shown, State::Free); }

  State state(int slot) const {
    std::lock_guard lock(m_);
    return valid(slot) ? slots_[slot].state : State::Free;
  }
  quint64 skipped() const {  // beginWrite() calls that found no free slot
    std::lock_guard lock(m_);
    return skipped_;
  }

 private:
  static bool valid(int slot) { return slot >= 0 && slot < kCount; }
  bool move(int slot, State from, State to) {
    std::lock_guard lock(m_);
    if (!valid(slot) || slots_[slot].state != from) {
      return false;
    }
    slots_[slot].state = to;
    return true;
  }

  struct Slot {
    State state = State::Free;
    int width = 0;
    int height = 0;
    quint64 sequence = 0;
  };
  mutable std::mutex m_;
  Slot slots_[kCount];
  quint64 sequence_ = 0;
  quint64 skipped_ = 0;
};

// Grow-only texture size: each dimension is the larger of the current size and the request rounded up to 64.
inline int roundUpTo64(int v) { return v <= 0 ? 0 : ((v + 63) / 64) * 64; }
inline void growSize(int curW, int curH, int reqW, int reqH, int* outW, int* outH) {
  const int rw = roundUpTo64(reqW);
  const int rh = roundUpTo64(reqH);
  *outW = rw > curW ? rw : curW;
  *outH = rh > curH ? rh : curH;
}

}  // namespace framebeam
