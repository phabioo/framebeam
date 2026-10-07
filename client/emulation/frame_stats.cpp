#include "frame_stats.h"

#include <algorithm>

namespace framebeam::emu {

void FrameTimingStats::reset() {
  QMutexLocker l(&mutex_);
  samples_.clear();
}

void FrameTimingStats::recordFrame(qint64 nowMs, double totalMs, double readbackMs) {
  QMutexLocker l(&mutex_);
  Sample s;
  s.atMs = nowMs;
  s.totalMs = static_cast<float>(std::max(0.0, totalMs));
  s.readbackMs = static_cast<float>(std::clamp(readbackMs, 0.0, std::max(0.0, totalMs)));
  s.emuMs = s.totalMs - s.readbackMs;
  samples_.append(s);
  while (!samples_.isEmpty() && samples_.first().atMs < nowMs - kHistoryMs) {
    samples_.removeFirst();
  }
}

FrameTimingStats::Snapshot FrameTimingStats::snapshot(qint64 nowMs) const {
  QMutexLocker l(&mutex_);
  Snapshot out;
  qsizetype first = -1;
  double total = 0, emu = 0, rb = 0;
  qsizetype n = 0;
  for (qsizetype i = 0; i < samples_.size(); ++i) {
    const Sample& s = samples_.at(i);
    if (s.atMs > nowMs - kFpsWindowMs && s.atMs <= nowMs) {
      if (first < 0) first = i;
      total += s.totalMs;
      emu += s.emuMs;
      rb += s.readbackMs;
      ++n;
    }
    if (s.atMs >= nowMs - kHistoryMs && s.atMs <= nowMs) {
      out.history.append(s);
    }
  }
  if (n > 0) {
    out.valid = true;
    out.frameMs = total / static_cast<double>(n);
    out.emuMs = emu / static_cast<double>(n);
    out.readbackMs = rb / static_cast<double>(n);
    // Frames per second from the spacing inside the window: (n - 1) intervals between the first and last frame.
    // A single frame gives no interval, then the count itself is the best estimate.
    const qint64 span = samples_.at(first + n - 1).atMs - samples_.at(first).atMs;
    out.fps = (n > 1 && span > 0) ? static_cast<double>(n - 1) * 1000.0 / static_cast<double>(span) : static_cast<double>(n);
  }
  return out;
}

}  // namespace framebeam::emu
