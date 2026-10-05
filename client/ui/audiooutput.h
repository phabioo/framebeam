#pragma once
// Audio output via QAudioSink (Qt 6.4 API, push mode). The output format (rate, sample format) comes from
// device.preferredFormat() (WASAPI shared mode usually accepts only the mix format, e.g. float 48 kHz);
// Core audio (int16 stereo) is resampled and converted. Buffer ~150 ms; whatever does not fit right away is
// buffered up to a limit, the rest is dropped. Without an output device the class stays a no-op.

#include <QAudioFormat>
#include <QByteArray>
#include <QIODevice>
#include <QLoggingCategory>
#include <QObject>
#include <memory>

#include "audioresampler.h"

class QAudioSink;

namespace framebeam::ui {

Q_DECLARE_LOGGING_CATEGORY(lcAudio)

class AudioOutput : public QObject {
  Q_OBJECT
 public:
  explicit AudioOutput(QObject* parent = nullptr);
  ~AudioOutput() override;

  // Starts output for the core sample rate; if the device does not support it, audio is linearly resampled to
  // 48000 Hz. false: no device (silent).
  bool start(int coreSampleRate);
  // Interleaved Stereo int16 -> stereo in the target format (UInt8, Int16, Int32, Float); other format: empty.
  static QByteArray convertSamples(const QByteArray& pcm16, QAudioFormat::SampleFormat target);
  void stop();
  // Interleaved stereo int16 at the rate given to start().
  void push(const QByteArray& pcm);

  bool isActive() const { return sink_ != nullptr; }
  int outputRate() const { return outRate_; }
  qint64 droppedBytes() const { return dropped_; }
  QAudioFormat outputFormat() const { return fmt_; }

 private:
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* io_ = nullptr;  // owned by the sink
  LinearResampler resampler_;
  QAudioFormat fmt_;
  QByteArray pending_;  // bytes not yet written, in the target format
  qsizetype pendingCap_ = 0;
  int outRate_ = 0;
  qint64 dropped_ = 0;
};

}  // namespace framebeam::ui
