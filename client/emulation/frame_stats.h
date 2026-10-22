#pragma once
// FrameTimingStats: measurements of the emulation loop for the diagnostics overlay (ADR 0006 D6, 0.6 UI pass).
// Written by the emulation thread once per frame (recordFrame), read by the UI thread about twice a second
// (snapshot). Thread-safe through one short mutex; no clock of its own (timestamps are passed in, so the logic
// is deterministic and testable).
//  - actual fps: frames inside a sliding 1 s window,
//  - frame time = retro_run total, split into emulation (core), readback (hardware readback, 0 for software) and GPU
//    copy (the Session encode texture, ADR 0019: blit + capture; 0 without GPU-direct encoding),
//  - zero-copy display (ADR 0027): the blit into the display target is a fourth part (0 without one),
//  - history: the last 5 s of per-frame times for the sparkline.

#include <QList>
#include <QMutex>
#include <QtGlobal>

namespace uzume::emu {

class FrameTimingStats {
 public:
  static constexpr qint64 kFpsWindowMs = 1000;
  static constexpr qint64 kHistoryMs = 5000;

  struct Sample {
    qint64 atMs = 0;
    float totalMs = 0;
    float emuMs = 0;
    float readbackMs = 0;  // 0 = this frame did not read back
    float gpuCopyMs = 0;   // blit into / capture from the Session encode texture (0 = none)
    bool gpuCaptured = false;  // a frame was captured for the Session encoder
    float displayMs = 0;       // blit into the zero-copy display texture (0 = none)
    bool displayed = false;    // a frame was presented to the display target
  };
  struct Snapshot {
    bool valid = false;        // at least one frame inside the window
    double fps = 0.0;          // actual frames per second over the last second
    double frameMs = 0.0;      // mean total frame time inside the window
    double emuMs = 0.0;        // mean emulation part
    double readbackMs = 0.0;   // mean readback part over ALL frames of the window (0 without hardware rendering)
    double readbacksPerSec = 0.0;  // frames of the window that read back (hardware frames that were shown)
    double readbackMsPerRead = 0.0;  // mean duration of those frames' readback only
    double gpuCopyMs = 0.0;    // mean GPU copy part over ALL frames of the window (0 without GPU-direct encoding)
    double gpuCopiesPerSec = 0.0;  // frames of the window that captured a frame for the Session encoder
    double displayMs = 0.0;    // mean display blit part over ALL frames of the window (0 without zero-copy display)
    double displaysPerSec = 0.0;  // frames of the window that were presented to the display target
    QList<Sample> history;     // last 5 s, oldest first
  };

  void reset();
  // totalMs: duration of the whole frame step; readbackMs: part of it spent reading the GPU frame back (0 = none);
  // gpuCopyMs: part spent on the Session encode texture (0 = none), gpuCaptured: the frame captured one for the encoder.
  // displayMs: part spent on the zero-copy display blit (0 = none), displayed: the frame was presented to the target.
  // emu part = total - readback - gpuCopy - display, never below 0.
  void recordFrame(qint64 nowMs, double totalMs, double readbackMs, double gpuCopyMs = 0.0, bool gpuCaptured = false,
                   double displayMs = 0.0, bool displayed = false);
  Snapshot snapshot(qint64 nowMs) const;

 private:
  mutable QMutex mutex_;
  QList<Sample> samples_;  // newest last, trimmed to kHistoryMs on every record
};

}  // namespace uzume::emu
