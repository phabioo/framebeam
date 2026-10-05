#include "audiooutput.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QMediaDevices>
#include <algorithm>

namespace framebeam::ui {

namespace {
constexpr int kFallbackRate = 48000;
constexpr int kBufferMs = 70;
}  // namespace

AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {}

AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(int coreSampleRate) {
  stop();
  if (coreSampleRate <= 0) {
    coreSampleRate = kFallbackRate;
  }
  const QAudioDevice device = QMediaDevices::defaultAudioOutput();
  if (device.isNull()) {
    return false;
  }
  QAudioFormat fmt;
  fmt.setSampleRate(coreSampleRate);
  fmt.setChannelCount(2);
  fmt.setSampleFormat(QAudioFormat::Int16);
  int rate = coreSampleRate;
  if (!device.isFormatSupported(fmt)) {
    rate = kFallbackRate;
    fmt.setSampleRate(rate);
    if (!device.isFormatSupported(fmt)) {
      return false;
    }
  }
  resampler_.setRates(coreSampleRate, rate);
  outRate_ = rate;
  dropped_ = 0;
  auto sink = std::make_unique<QAudioSink>(device, fmt);
  sink->setBufferSize(rate * 4 * kBufferMs / 1000);
  io_ = sink->start();
  if (io_ == nullptr) {
    return false;
  }
  sink_ = std::move(sink);
  return true;
}

void AudioOutput::stop() {
  if (sink_) {
    sink_->stop();
    sink_.reset();
  }
  io_ = nullptr;
}

void AudioOutput::push(const QByteArray& pcm) {
  if (!sink_ || io_ == nullptr || pcm.isEmpty()) {
    return;
  }
  const QByteArray data = resampler_.isPassthrough() ? pcm : resampler_.process(pcm);
  // Nur schreiben, was in den (kleinen) Puffer passt; Rest verwerfen, damit die Latenz nicht waechst.
  const qint64 free = sink_->bytesFree();
  const qint64 n = std::min<qint64>(free - (free % 4), data.size());
  if (n > 0) {
    io_->write(data.constData(), n);
  }
  if (n < data.size()) {
    dropped_ += data.size() - std::max<qint64>(n, 0);
  }
}

}  // namespace framebeam::ui
