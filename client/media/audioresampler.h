#pragma once
// Linear resampler for interleaved stereo int16 (fallback when the audio device does not support the core
// sample rate). Stateful: chunks are joined seamlessly.

#include <QByteArray>

namespace framebeam::ui {

class LinearResampler {
 public:
  LinearResampler() = default;
  LinearResampler(int inRate, int outRate) { setRates(inRate, outRate); }

  void setRates(int inRate, int outRate);
  void reset();
  bool isPassthrough() const { return in_ == out_; }

  // Input/output: interleaved stereo int16 (4 bytes per frame); an incomplete remainder is ignored.
  QByteArray process(const QByteArray& pcm);

 private:
  int in_ = 48000;
  int out_ = 48000;
  double pos_ = 0.0;  // read position relative to the (prev_ + next chunk) buffer
  bool hasPrev_ = false;
  qint16 prevL_ = 0;
  qint16 prevR_ = 0;
};

}  // namespace framebeam::ui
