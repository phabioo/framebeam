#pragma once

#include <QByteArray>
#include <cstdint>
#include <vector>

#include "audioresampler.h"

struct OpusEncoder;
struct OpusDecoder;

namespace framebeam {

inline constexpr int kOpusRate = 48000;
inline constexpr int kOpusChannels = 2;
inline constexpr int kOpusFrameSamples = 960;  // 20 ms at 48 kHz
inline constexpr int kOpusFrameBytes = kOpusFrameSamples * kOpusChannels * int(sizeof(int16_t));

// Core audio (int16 interleaved stereo, any rate) -> 48 kHz stereo -> libopus 20 ms frames.
class OpusFramer {
 public:
  OpusFramer();
  ~OpusFramer();
  OpusFramer(const OpusFramer&) = delete;
  OpusFramer& operator=(const OpusFramer&) = delete;

  bool open(int bitrate = 96000);
  void close();
  bool isOpen() const { return enc_ != nullptr; }
  // Discards buffered samples (encoder was idle).
  void reset();
  // Adds PCM and appends one Opus packet per completed 20 ms frame.
  void push(const QByteArray& pcm, int sampleRate, std::vector<std::vector<uint8_t>>& packets);

 private:
  OpusEncoder* enc_ = nullptr;
  ui::LinearResampler resampler_;
  int rate_ = 0;
  QByteArray pending_;
};

class OpusDepacker {
 public:
  OpusDepacker();
  ~OpusDepacker();
  OpusDepacker(const OpusDepacker&) = delete;
  OpusDepacker& operator=(const OpusDepacker&) = delete;

  bool open();
  // Decodes one packet to 48 kHz stereo int16; empty on error.
  QByteArray decode(const uint8_t* data, size_t size);

 private:
  OpusDecoder* dec_ = nullptr;
};

}  // namespace framebeam
