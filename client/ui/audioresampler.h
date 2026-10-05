#pragma once
// Linearer Resampler fuer interleaved Stereo int16 (Fallback, wenn das Audiogeraet die Core-Samplerate
// nicht unterstuetzt). Zustandsbehaftet: Chunks werden nahtlos aneinandergesetzt.

#include <QByteArray>

namespace framebeam::ui {

class LinearResampler {
 public:
  LinearResampler() = default;
  LinearResampler(int inRate, int outRate) { setRates(inRate, outRate); }

  void setRates(int inRate, int outRate);
  void reset();
  bool isPassthrough() const { return in_ == out_; }

  // Eingabe/Ausgabe: interleaved Stereo int16 (4 Bytes je Frame); ein unvollstaendiger Rest wird ignoriert.
  QByteArray process(const QByteArray& pcm);

 private:
  int in_ = 48000;
  int out_ = 48000;
  double pos_ = 0.0;  // Leseposition relativ zum (prev_ + naechster Chunk)-Puffer
  bool hasPrev_ = false;
  qint16 prevL_ = 0;
  qint16 prevR_ = 0;
};

}  // namespace framebeam::ui
