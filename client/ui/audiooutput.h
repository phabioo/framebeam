#pragma once
// Audioausgabe ueber QAudioSink (Qt-6.4-API, Push-Modus). Das Ausgabeformat (Rate, Sample-Format) kommt aus
// device.preferredFormat() (WASAPI-Shared-Mode akzeptiert meist nur das Mix-Format, z. B. Float 48 kHz);
// Core-Audio (int16 Stereo) wird resampled und konvertiert. Puffer ~150 ms; was nicht sofort passt, wird
// begrenzt zwischengespeichert, der Rest verworfen. Ohne Ausgabegeraet bleibt die Klasse ein No-op.

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

  // Startet die Ausgabe fuer die Core-Samplerate; unterstuetzt das Geraet sie nicht, wird linear auf
  // 48000 Hz resampled. false: kein Geraet (stumm).
  bool start(int coreSampleRate);
  // Interleaved Stereo int16 -> Stereo im Zielformat (UInt8, Int16, Int32, Float); anderes Format: leer.
  static QByteArray convertSamples(const QByteArray& pcm16, QAudioFormat::SampleFormat target);
  void stop();
  // Interleaved Stereo int16 mit der bei start() genannten Rate.
  void push(const QByteArray& pcm);

  bool isActive() const { return sink_ != nullptr; }
  int outputRate() const { return outRate_; }
  qint64 droppedBytes() const { return dropped_; }
  QAudioFormat outputFormat() const { return fmt_; }

 private:
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* io_ = nullptr;  // gehoert dem Sink
  LinearResampler resampler_;
  QAudioFormat fmt_;
  QByteArray pending_;  // noch nicht geschriebene Bytes im Zielformat
  qsizetype pendingCap_ = 0;
  int outRate_ = 0;
  qint64 dropped_ = 0;
};

}  // namespace framebeam::ui
