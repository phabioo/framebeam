#pragma once
// Audio output via QAudioSink (Qt 6.4 API, pull mode). The output format (rate, sample format) comes from
// device.preferredFormat() (WASAPI shared mode usually accepts only the mix format, e.g. float 48 kHz);
// Core audio (int16 stereo) is resampled and converted. Buffer ~150 ms; the sink pulls from AudioQueueDevice,
// which always reports data and returns silence only when completely dry, so the WASAPI client is never stopped and restarted (Qt's push
// mode does that after every write and produced endless Idle/Active flips without audible sound on Windows).
// Without an output device the class stays a no-op.

#include <QAudio>
#include <QAudioFormat>
#include <QByteArray>
#include <QIODevice>
#include <QLoggingCategory>
#include <QMutex>
#include <QObject>
#include <memory>

#include "audioresampler.h"

class QAudioSink;

namespace framebeam::ui {

Q_DECLARE_LOGGING_CATEGORY(lcAudio)

// Always-readable source for the pull-mode sink: queued audio in the output format (short reads return just the
// queued frames, no padding), silence only while the queue is completely empty.
// Thread-safe (Qt may read from its own audio thread).
class AudioQueueDevice : public QIODevice {
 public:
  explicit AudioQueueDevice(QObject* parent = nullptr);
  // bytesPerFrame/silenceByte of the output format; nominalBytes: what bytesAvailable() reports while the queue is empty;
  // capBytes: queue limit (oldest whole frames are dropped beyond it).
  void configure(int bytesPerFrame, char silenceByte, qint64 nominalBytes, qsizetype capBytes);
  // Appends audio and returns the number of bytes dropped to stay within the cap.
  qint64 append(const QByteArray& bytes);
  qint64 queuedBytes() const;
  int underruns() const;
  void setUnderrunCounting(bool on);
  void resetState();  // empties the queue and the underrun state; the counter stays
  void notifyNewData() { emit readyRead(); }

  bool isSequential() const override { return true; }
  qint64 bytesAvailable() const override;

 protected:
  qint64 readData(char* data, qint64 maxlen) override;
  qint64 writeData(const char*, qint64) override { return -1; }

 private:
  mutable QMutex mutex_;
  QByteArray queue_;
  int bytesPerFrame_ = 1;
  char silence_ = 0;
  qint64 nominal_ = 0;
  qsizetype cap_ = 0;
  int underruns_ = 0;
  bool counting_ = true;
  bool hadData_ = false;  // real audio was delivered since the last dry spell / reset
};

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

  // Diagnostics (0.6 D6), UI thread. Audio queued for playback in ms: what the sink still holds plus what waits in
  // the queue. Underrun = a read found the queue dry (padded with silence) after audio had been delivered, once per
  // dry spell; not counted while counting is off (paused game, muted surface: nothing is pushed on purpose).
  double bufferedMs() const;
  int underruns() const { return device_ ? device_->underruns() : underruns_; }
  void setUnderrunCounting(bool on);
  static double bufferedMsFor(qint64 sinkBufferBytes, qint64 sinkFreeBytes, qint64 pendingBytes, int bytesPerFrame, int sampleRate);

 private:
  std::unique_ptr<AudioQueueDevice> device_;  // declared before sink_: the sink is destroyed first
  std::unique_ptr<QAudioSink> sink_;
  LinearResampler resampler_;
  QAudioFormat fmt_;
  int outRate_ = 0;
  int coreRate_ = 0;
  qint64 dropped_ = 0;
  int underruns_ = 0;  // value kept when the device is gone
  bool counting_ = true;
  // One-time signal level log: peak of the core audio over the first second.
  qint64 levelFrames_ = 0;
  int levelPeak_ = 0;
  bool levelLogged_ = false;
};

}  // namespace framebeam::ui
