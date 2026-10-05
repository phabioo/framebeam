#pragma once
// Audioausgabe ueber QAudioSink (Qt-6.4-API, Push-Modus). Kleiner Puffer (~70 ms); Ueberlauf verwirft
// Samples, Unterlauf erzeugt Stille. Ohne Ausgabegeraet bleibt die Klasse ein No-op.

#include <QAudioFormat>
#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <memory>

#include "audioresampler.h"

class QAudioSink;

namespace framebeam::ui {

class AudioOutput : public QObject {
  Q_OBJECT
 public:
  explicit AudioOutput(QObject* parent = nullptr);
  ~AudioOutput() override;

  // Startet die Ausgabe fuer die Core-Samplerate; unterstuetzt das Geraet sie nicht, wird linear auf
  // 48000 Hz resampled. false: kein Geraet (stumm).
  bool start(int coreSampleRate);
  void stop();
  // Interleaved Stereo int16 mit der bei start() genannten Rate.
  void push(const QByteArray& pcm);

  bool isActive() const { return sink_ != nullptr; }
  int outputRate() const { return outRate_; }
  qint64 droppedBytes() const { return dropped_; }

 private:
  std::unique_ptr<QAudioSink> sink_;
  QIODevice* io_ = nullptr;  // gehoert dem Sink
  LinearResampler resampler_;
  int outRate_ = 0;
  qint64 dropped_ = 0;
};

}  // namespace framebeam::ui
