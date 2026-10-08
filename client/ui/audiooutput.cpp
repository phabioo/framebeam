#include "audiooutput.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace framebeam::ui {

Q_LOGGING_CATEGORY(lcAudio, "framebeam.audio")

namespace {
constexpr int kFallbackRate = 48000;
constexpr int kBufferMs = 150;

bool isConvertible(QAudioFormat::SampleFormat f) {
  return f == QAudioFormat::UInt8 || f == QAudioFormat::Int16 || f == QAudioFormat::Int32 ||
         f == QAudioFormat::Float;
}
}  // namespace

AudioOutput::AudioOutput(QObject* parent) : QObject(parent) {}

AudioOutput::~AudioOutput() { stop(); }

QByteArray AudioOutput::convertSamples(const QByteArray& pcm16, QAudioFormat::SampleFormat target) {
  if (!isConvertible(target)) {
    return {};
  }
  if (target == QAudioFormat::Int16) {
    return pcm16;
  }
  const qsizetype n = pcm16.size() / 2;  // samples (both channels)
  const auto* in = reinterpret_cast<const unsigned char*>(pcm16.constData());
  auto s16 = [in](qsizetype i) {
    return static_cast<qint16>(static_cast<quint16>(in[2 * i] | (in[2 * i + 1] << 8)));
  };
  QByteArray out;
  switch (target) {
    case QAudioFormat::UInt8: {
      out.resize(n);
      auto* o = reinterpret_cast<unsigned char*>(out.data());
      for (qsizetype i = 0; i < n; ++i) {
        o[i] = static_cast<unsigned char>((s16(i) >> 8) + 128);
      }
      break;
    }
    case QAudioFormat::Int32: {
      out.resize(n * 4);
      for (qsizetype i = 0; i < n; ++i) {
        const qint32 v = static_cast<qint32>(s16(i)) * 65536;
        std::memcpy(out.data() + 4 * i, &v, 4);
      }
      break;
    }
    default: {  // Float
      out.resize(n * 4);
      for (qsizetype i = 0; i < n; ++i) {
        const float v = static_cast<float>(s16(i)) / 32768.0f;
        std::memcpy(out.data() + 4 * i, &v, 4);
      }
      break;
    }
  }
  return out;
}

bool AudioOutput::start(int coreSampleRate) {
  stop();
  if (coreSampleRate <= 0) {
    coreSampleRate = kFallbackRate;
  }
  const QAudioDevice device = QMediaDevices::defaultAudioOutput();
  if (device.isNull()) {
    qCWarning(lcAudio) << "no default audio output device; session runs without sound";
    return false;
  }
  const QAudioFormat pref = device.preferredFormat();
  qCInfo(lcAudio) << "Device:" << device.description() << "preferred:" << pref.sampleRate() << "Hz,"
                  << pref.channelCount() << "ch, Format" << static_cast<int>(pref.sampleFormat())
                  << "core rate:" << coreSampleRate;

  QAudioFormat fmt;
  fmt.setChannelCount(2);
  bool chosen = false;
  if (pref.sampleRate() > 0 && isConvertible(pref.sampleFormat())) {
    fmt.setSampleRate(pref.sampleRate());
    fmt.setSampleFormat(pref.sampleFormat());
    chosen = device.isFormatSupported(fmt);
    if (!chosen) {
      qCWarning(lcAudio) << "preferred format not supported as stereo";
    }
  }
  if (!chosen) {  // Fallback: previous logic (Int16, core rate, then 48 kHz)
    fmt.setSampleFormat(QAudioFormat::Int16);
    fmt.setSampleRate(coreSampleRate);
    if (!device.isFormatSupported(fmt)) {
      fmt.setSampleRate(kFallbackRate);
      if (!device.isFormatSupported(fmt)) {
        qCWarning(lcAudio) << "no supported output format; session runs without sound";
        return false;
      }
    }
  }
  const int rate = fmt.sampleRate();
  fmt_ = fmt;
  qCInfo(lcAudio) << "Output format:" << rate << "Hz, 2 ch, Format" << static_cast<int>(fmt.sampleFormat())
                  << "(" << fmt.bytesPerFrame() << "bytes/frame )";

  resampler_.reset();
  resampler_.setRates(coreSampleRate, rate);
  outRate_ = rate;
  coreRate_ = coreSampleRate;
  dropped_ = 0;
  underruns_ = 0;
  levelFrames_ = 0;
  levelPeak_ = 0;
  levelLogged_ = false;
  const int bufBytes = rate * fmt.bytesPerFrame() * kBufferMs / 1000;
  auto dev = std::make_unique<AudioQueueDevice>();
  dev->configure(fmt.bytesPerFrame(), fmt.sampleFormat() == QAudioFormat::UInt8 ? static_cast<char>(0x80) : char(0),
                 bufBytes, bufBytes);
  dev->setUnderrunCounting(counting_);
  dev->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
  auto sink = std::make_unique<QAudioSink>(device, fmt);
  sink->setBufferSize(bufBytes);
  connect(sink.get(), &QAudioSink::stateChanged, this, [s = sink.get()](QAudio::State st) {
    qCDebug(lcAudio) << "Sink state:" << static_cast<int>(st) << "error:" << static_cast<int>(s->error());
  });
  sink->start(dev.get());  // pull mode
  qCInfo(lcAudio) << "start():" << (sink->error() == QAudio::NoError ? "ok" : "failed")
                  << "error:" << static_cast<int>(sink->error()) << "Status:" << static_cast<int>(sink->state())
                  << "buffer:" << sink->bufferSize() << "bytes, free:" << sink->bytesFree();
  if (sink->error() != QAudio::NoError) {
    qCWarning(lcAudio) << "QAudioSink could not start, error" << static_cast<int>(sink->error());
    sink->stop();
    return false;
  }
  device_ = std::move(dev);
  sink_ = std::move(sink);
  return true;
}

double AudioOutput::bufferedMsFor(qint64 sinkBufferBytes, qint64 sinkFreeBytes, qint64 pendingBytes, int bytesPerFrame, int sampleRate) {
  if (bytesPerFrame <= 0 || sampleRate <= 0) {
    return 0.0;
  }
  const qint64 queued = std::max<qint64>(0, sinkBufferBytes - sinkFreeBytes) + std::max<qint64>(0, pendingBytes);
  return static_cast<double>(queued) / bytesPerFrame * 1000.0 / sampleRate;
}

double AudioOutput::bufferedMs() const {
  if (!sink_ || !device_) {
    return 0.0;
  }
  return bufferedMsFor(sink_->bufferSize(), sink_->bytesFree(), device_->queuedBytes(), fmt_.bytesPerFrame(), outRate_);
}

void AudioOutput::setUnderrunCounting(bool on) {
  if (on == counting_) {
    return;
  }
  counting_ = on;
  if (device_) {
    device_->setUnderrunCounting(on);
  }
}

void AudioOutput::stop() {
  if (sink_) {
    qCInfo(lcAudio) << "stop(): dropped bytes" << dropped_ << "error:" << static_cast<int>(sink_->error());
    sink_->stop();
    sink_.reset();
  }
  if (device_) {
    underruns_ = device_->underruns();
    device_->resetState();
    device_->close();
    device_.reset();
  }
}

void AudioOutput::push(const QByteArray& pcm) {
  if (!sink_ || !device_ || pcm.isEmpty()) {
    return;
  }
  if (!levelLogged_) {
    const auto* s = reinterpret_cast<const unsigned char*>(pcm.constData());
    const qsizetype n = pcm.size() / 2;
    for (qsizetype i = 0; i < n; ++i) {
      const int v = static_cast<qint16>(static_cast<quint16>(s[2 * i] | (s[2 * i + 1] << 8)));
      levelPeak_ = std::max(levelPeak_, v < 0 ? -v : v);
    }
    levelFrames_ += pcm.size() / 4;
    if (levelFrames_ >= coreRate_) {
      levelLogged_ = true;
      qCInfo(lcAudio) << "Audio level after 1 s: peak" << levelPeak_ << "of 32767";
    }
  }
  const QByteArray resampled = resampler_.isPassthrough() ? pcm : resampler_.process(pcm);
  const qint64 drop = device_->append(convertSamples(resampled, fmt_.sampleFormat()));
  if (drop > 0) {
    if (dropped_ == 0) {
      qCWarning(lcAudio) << "Audio overflow: first dropped bytes (buffer full)";
    }
    dropped_ += drop;
  }
  device_->notifyNewData();
}

AudioQueueDevice::AudioQueueDevice(QObject* parent) : QIODevice(parent) {}

void AudioQueueDevice::configure(int bytesPerFrame, char silenceByte, qint64 nominalBytes, qsizetype capBytes) {
  QMutexLocker lock(&mutex_);
  bytesPerFrame_ = std::max(1, bytesPerFrame);
  silence_ = silenceByte;
  nominal_ = nominalBytes;
  cap_ = capBytes;
}

qint64 AudioQueueDevice::append(const QByteArray& bytes) {
  QMutexLocker lock(&mutex_);
  if (bytes.isEmpty()) {
    return 0;
  }
  queue_.append(bytes);
  hadData_ = true;
  qsizetype drop = queue_.size() - cap_;
  if (drop <= 0) {
    return 0;
  }
  drop += (bytesPerFrame_ - drop % bytesPerFrame_) % bytesPerFrame_;  // whole frames, queue ends up <= cap
  drop = std::min<qsizetype>(drop, queue_.size());
  queue_.remove(0, drop);
  return drop;
}

qint64 AudioQueueDevice::queuedBytes() const {
  QMutexLocker lock(&mutex_);
  return queue_.size();
}

int AudioQueueDevice::underruns() const {
  QMutexLocker lock(&mutex_);
  return underruns_;
}

void AudioQueueDevice::setUnderrunCounting(bool on) {
  QMutexLocker lock(&mutex_);
  if (on && !counting_) {
    hadData_ = false;  // the dry period while counting was off is not an underrun
  }
  counting_ = on;
}

void AudioQueueDevice::resetState() {
  QMutexLocker lock(&mutex_);
  queue_.clear();
  hadData_ = false;
}

qint64 AudioQueueDevice::bytesAvailable() const {
  QMutexLocker lock(&mutex_);
  return (queue_.size() > 0 ? static_cast<qint64>(queue_.size()) : nominal_) + QIODevice::bytesAvailable();
}

qint64 AudioQueueDevice::readData(char* data, qint64 maxlen) {
  QMutexLocker lock(&mutex_);
  const qint64 len = maxlen - maxlen % bytesPerFrame_;
  if (len <= 0) {
    return 0;
  }
  qint64 avail = queue_.size();
  avail -= avail % bytesPerFrame_;
  if (avail > 0) {  // partial read: only what is queued, no padding
    const qint64 n = std::min(len, avail);
    std::memcpy(data, queue_.constData(), static_cast<size_t>(n));
    queue_.remove(0, static_cast<qsizetype>(n));
    return n;
  }
  // Completely dry: silence for the whole request, one underrun per dry spell.
  std::memset(data, static_cast<unsigned char>(silence_), static_cast<size_t>(len));
  if (hadData_) {
    if (counting_) {
      ++underruns_;
    }
    hadData_ = false;
  }
  return len;
}

}  // namespace framebeam::ui
