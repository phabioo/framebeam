// Picks which emulated frames go to the UI / Session while Speed-up runs the core faster than real time (ADR 0018 D2).
// Pure schedule logic on nanosecond ticks (no clock access) so it can be tested with simulated time.
#pragma once

#include <algorithm>
#include <cstdint>

namespace framebeam::emu {

class FramePacer {
 public:
  // Restart the schedule (start, pause end, speed-up toggled): the first frame is wanted at once.
  void reset(int64_t nowNs) { nextSlotNs_ = nowNs; }

  // Before an emulated frame starts: does the stream need its video (readback, conversion, GPU blit)?
  bool wantsFrame(int64_t frameStartNs) const { return frameStartNs >= nextSlotNs_; }

  // After a wanted frame was delivered at `deliverNs`: advances the schedule and returns how late the frame is against
  // its slot (ns, 0..periodNs). The Session stamps the frame at "arrival - lateness", which gives evenly spaced
  // timestamps at the base rate even when the emulated frame grid (e.g. 1.5x) does not line up with it.
  int64_t delivered(int64_t frameStartNs, int64_t deliverNs, int64_t periodNs) {
    const int64_t slot = nextSlotNs_;
    nextSlotNs_ += periodNs;  // a schedule, not 'last + period': jitter must not skip a whole slot
    if (nextSlotNs_ < frameStartNs) nextSlotNs_ = frameStartNs + periodNs;  // far behind: resynchronize
    return std::clamp<int64_t>(deliverNs - slot, 0, periodNs);
  }

 private:
  int64_t nextSlotNs_ = 0;
};

}  // namespace framebeam::emu
