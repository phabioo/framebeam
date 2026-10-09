#include "frame_stats.h"

#include <algorithm>

namespace framebeam::emu {

void FrameTimingStats::reset() {
  QMutexLocker l(&mutex_);
  samples_.clear();
}

void FrameTimingStats::recordFrame(qint64 nowMs, double totalMs, double readbackMs, double gpuCopyMs, bool gpuCaptured) {
  QMutexLocker l(&mutex_);
  Sample s;
  s.atMs = nowMs;
  s.totalMs = static_cast<float>(std::max(0.0, totalMs));
  s.readbackMs = static_cast<float>(std::clamp(readbackMs, 0.0, std::max(0.0, totalMs)));
  s.gpuCopyMs = static_cast<float>(std::clamp(gpuCopyMs, 0.0, std::max(0.0, totalMs - s.readbackMs)));
  s.gpuCaptured = gpuCaptured;
  s.emuMs = std::max(0.0f, s.totalMs - s.readbackMs - s.gpuCopyMs);
  samples_.append(s);
  while (!samples_.isEmpty() && samples_.first().atMs < nowMs - kHistoryMs) {
    samples_.removeFirst();
  }
}

FrameTimingStats::Snapshot FrameTimingStats::snapshot(qint64 nowMs) const {
  QMutexLocker l(&mutex_);
  Snapshot out;
  qsizetype first = -1;
  double total = 0, emu = 0, rb = 0, gpuCopy = 0;
  qsizetype n = 0, nRb = 0, nGpu = 0;
  for (qsizetype i = 0; i < samples_.size(); ++i) {
    const Sample& s = samples_.at(i);
    if (s.atMs > nowMs - kFpsWindowMs && s.atMs <= nowMs) {
      if (first < 0) first = i;
      total += s.totalMs;
      emu += s.emuMs;
      rb += s.readbackMs;
      nRb += s.readbackMs > 0.0f ? 1 : 0;
      gpuCopy += s.gpuCopyMs;
      nGpu += s.gpuCaptured ? 1 : 0;
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
    out.gpuCopyMs = gpuCopy / static_cast<double>(n);
    // Frames per second from the spacing inside the window: (n - 1) intervals between the first and last frame.
    // A single frame gives no interval, then the count itself is the best estimate.
    const qint64 span = samples_.at(first + n - 1).atMs - samples_.at(first).atMs;
    out.readbackMsPerRead = nRb > 0 ? rb / static_cast<double>(nRb) : 0.0;
    out.fps = (n > 1 && span > 0) ? static_cast<double>(n - 1) * 1000.0 / static_cast<double>(span) : static_cast<double>(n);
  }
  if (out.valid && out.fps > 0.0) {
    out.readbacksPerSec = out.fps * static_cast<double>(nRb) / static_cast<double>(n);
    out.gpuCopiesPerSec = out.fps * static_cast<double>(nGpu) / static_cast<double>(n);
  }
  return out;
}

}  // namespace framebeam::emu
