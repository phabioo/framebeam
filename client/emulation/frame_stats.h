#pragma once
// FrameTimingStats: measurements of the emulation loop for the diagnostics overlay (ADR 0006 D6, 0.6 UI pass).
// Written by the emulation thread once per frame (recordFrame), read by the UI thread about twice a second
// (snapshot). Thread-safe through one short mutex; no clock of its own (timestamps are passed in, so the logic
// is deterministic and testable).
//  - actual fps: frames inside a sliding 1 s window,
//  - frame time = retro_run total, split into emulation (core) and readback (hardware readback, 0 for software),
//  - history: the last 5 s of per-frame times for the sparkline.

#include <QList>
#include <QMutex>
#include <QtGlobal>

namespace framebeam::emu {

class FrameTimingStats {
 public:
  static constexpr qint64 kFpsWindowMs = 1000;
  static constexpr qint64 kHistoryMs = 5000;

  struct Sample {
    qint64 atMs = 0;
    float totalMs = 0;
    float emuMs = 0;
    float readbackMs = 0;
  };
  struct Snapshot {
    bool valid = false;        // at least one frame inside the window
    double fps = 0.0;          // actual frames per second over the last second
    double frameMs = 0.0;      // mean total frame time inside the window
    double emuMs = 0.0;        // mean emulation part
    double readbackMs = 0.0;   // mean readback part (0 without hardware rendering)
    QList<Sample> history;     // last 5 s, oldest first
  };

  void reset();
  // totalMs: duration of the whole frame step; readbackMs: part of it spent reading the GPU frame back (0 = none).
  void recordFrame(qint64 nowMs, double totalMs, double readbackMs);
  Snapshot snapshot(qint64 nowMs) const;

 private:
  mutable QMutex mutex_;
  QList<Sample> samples_;  // newest last, trimmed to kHistoryMs on every record
};

}  // namespace framebeam::emu
