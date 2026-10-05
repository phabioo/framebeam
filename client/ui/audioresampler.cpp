#include "audioresampler.h"

#include <QList>
#include <cmath>

namespace framebeam::ui {

void LinearResampler::setRates(int inRate, int outRate) {
  in_ = inRate > 0 ? inRate : 48000;
  out_ = outRate > 0 ? outRate : 48000;
  reset();
}

void LinearResampler::reset() {
  pos_ = 0.0;
  hasPrev_ = false;
  prevL_ = prevR_ = 0;
}

QByteArray LinearResampler::process(const QByteArray& pcm) {
  if (in_ == out_) {
    return pcm;
  }
  const qsizetype chunkFrames = pcm.size() / 4;
  if (chunkFrames == 0) {
    return {};
  }
  const auto* src = reinterpret_cast<const qint16*>(pcm.constData());

  // Working buffer: optionally the last frame of the previous chunk, then the new chunk.
  const qsizetype offset = hasPrev_ ? 1 : 0;
  const qsizetype n = chunkFrames + offset;
  auto at = [&](qsizetype frame, int ch) -> double {
    if (frame < offset) {
      return ch == 0 ? prevL_ : prevR_;
    }
    return src[(frame - offset) * 2 + ch];
  };

  const double step = static_cast<double>(in_) / static_cast<double>(out_);
  QList<qint16> out;
  out.reserve(static_cast<qsizetype>(static_cast<double>(chunkFrames) / step * 2.0) + 8);
  while (true) {
    const auto i = static_cast<qsizetype>(std::floor(pos_));
    if (i + 1 >= n) {
      break;
    }
    const double f = pos_ - static_cast<double>(i);
    for (int ch = 0; ch < 2; ++ch) {
      const double v = at(i, ch) * (1.0 - f) + at(i + 1, ch) * f;
      out.append(static_cast<qint16>(std::lround(v)));
    }
    pos_ += step;
  }
  prevL_ = src[(chunkFrames - 1) * 2];
  prevR_ = src[(chunkFrames - 1) * 2 + 1];
  hasPrev_ = true;
  pos_ -= static_cast<double>(n - 1);
  if (pos_ < 0.0) {
    pos_ = 0.0;
  }
  return QByteArray(reinterpret_cast<const char*>(out.constData()), out.size() * static_cast<qsizetype>(sizeof(qint16)));
}

}  // namespace framebeam::ui
