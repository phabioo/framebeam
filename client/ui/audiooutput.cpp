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
  const qsizetype n = pcm16.size() / 2;  // Samples (beide Kanaele)
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
    qCWarning(lcAudio) << "kein Standard-Audioausgabegeraet; Session laeuft ohne Ton";
    return false;
  }
  const QAudioFormat pref = device.preferredFormat();
  qCInfo(lcAudio) << "Geraet:" << device.description() << "bevorzugt:" << pref.sampleRate() << "Hz,"
                  << pref.channelCount() << "ch, Format" << static_cast<int>(pref.sampleFormat())
                  << "Core-Rate:" << coreSampleRate;

  QAudioFormat fmt;
  fmt.setChannelCount(2);
  bool chosen = false;
  if (pref.sampleRate() > 0 && isConvertible(pref.sampleFormat())) {
    fmt.setSampleRate(pref.sampleRate());
    fmt.setSampleFormat(pref.sampleFormat());
    chosen = device.isFormatSupported(fmt);
    if (!chosen) {
      qCWarning(lcAudio) << "bevorzugtes Format als Stereo nicht unterstuetzt";
    }
  }
  if (!chosen) {  // Rueckfall: bisherige Logik (Int16, Core-Rate, dann 48 kHz)
    fmt.setSampleFormat(QAudioFormat::Int16);
    fmt.setSampleRate(coreSampleRate);
    if (!device.isFormatSupported(fmt)) {
      fmt.setSampleRate(kFallbackRate);
      if (!device.isFormatSupported(fmt)) {
        qCWarning(lcAudio) << "kein unterstuetztes Ausgabeformat; Session laeuft ohne Ton";
        return false;
      }
    }
  }
  const int rate = fmt.sampleRate();
  fmt_ = fmt;
  qCInfo(lcAudio) << "Ausgabeformat:" << rate << "Hz, 2 ch, Format" << static_cast<int>(fmt.sampleFormat())
                  << "(" << fmt.bytesPerFrame() << "Byte/Frame )";

  resampler_.reset();
  resampler_.setRates(coreSampleRate, rate);
  outRate_ = rate;
  dropped_ = 0;
  pending_.clear();
  const int bufBytes = rate * fmt.bytesPerFrame() * kBufferMs / 1000;
  pendingCap_ = bufBytes / 2;
  auto sink = std::make_unique<QAudioSink>(device, fmt);
  sink->setBufferSize(bufBytes);
  connect(sink.get(), &QAudioSink::stateChanged, this, [s = sink.get()](QAudio::State st) {
    qCInfo(lcAudio) << "Sink-Status:" << static_cast<int>(st) << "Fehler:" << static_cast<int>(s->error());
  });
  io_ = sink->start();
  qCInfo(lcAudio) << "start():" << (io_ ? "ok" : "fehlgeschlagen") << "Fehler:" << static_cast<int>(sink->error())
                  << "Status:" << static_cast<int>(sink->state()) << "Puffer:" << sink->bufferSize()
                  << "Byte, frei:" << sink->bytesFree();
  if (io_ == nullptr || sink->error() != QAudio::NoError) {
    qCWarning(lcAudio) << "QAudioSink konnte nicht starten, Fehler" << static_cast<int>(sink->error());
    if (io_ == nullptr) {
      io_ = nullptr;
      return false;
    }
  }
  sink_ = std::move(sink);
  return true;
}

void AudioOutput::stop() {
  if (sink_) {
    qCInfo(lcAudio) << "stop(): verworfene Bytes" << dropped_ << "Fehler:" << static_cast<int>(sink_->error());
    sink_->stop();
    sink_.reset();
  }
  io_ = nullptr;
  pending_.clear();
}

void AudioOutput::push(const QByteArray& pcm) {
  if (!sink_ || io_ == nullptr || pcm.isEmpty()) {
    return;
  }
  const QByteArray resampled = resampler_.isPassthrough() ? pcm : resampler_.process(pcm);
  pending_.append(convertSamples(resampled, fmt_.sampleFormat()));
  const int bpf = fmt_.bytesPerFrame();
  // Schreiben, was in den Puffer passt (bytesFree kann direkt nach start() noch 0 sein).
  const qint64 free = sink_->bytesFree();
  const qint64 n = std::min<qint64>(free - (free % bpf), pending_.size());
  if (n > 0) {
    const qint64 w = io_->write(pending_.constData(), n);
    pending_.remove(0, static_cast<qsizetype>(std::max<qint64>(w, 0)));
  }
  // Rest begrenzt halten, damit die Latenz nicht waechst; aelteste Daten verwerfen.
  if (pending_.size() > pendingCap_) {
    qsizetype drop = pending_.size() - pendingCap_;
    drop -= drop % bpf;
    if (dropped_ == 0 && drop > 0) {
      qCWarning(lcAudio) << "Audio-Ueberlauf: erste verworfene Bytes (Puffer voll)";
    }
    pending_.remove(0, drop);
    dropped_ += drop;
  }
}

}  // namespace framebeam::ui
