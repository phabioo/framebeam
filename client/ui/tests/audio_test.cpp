// Tests without an audio device: format conversion and resampling path of AudioOutput.
#include <QtTest>
#include <cstring>

#include "audiooutput.h"

using namespace framebeam::ui;

namespace {
QByteArray pcm(std::initializer_list<qint16> v) {
  QByteArray b(static_cast<qsizetype>(v.size() * 2), 0);
  std::memcpy(b.data(), v.begin(), b.size());
  return b;
}
}  // namespace

class AudioTest : public QObject {
  Q_OBJECT
 private slots:
  void toFloat() {
    const QByteArray out = AudioOutput::convertSamples(pcm({0, 16384, -32768, 32767}), QAudioFormat::Float);
    QCOMPARE(out.size(), 16);
    float f[4];
    std::memcpy(f, out.constData(), 16);
    QCOMPARE(f[0], 0.0f);
    QCOMPARE(f[1], 0.5f);
    QCOMPARE(f[2], -1.0f);
    QVERIFY(f[3] < 1.0f && f[3] > 0.999f);
  }
  void toInt32() {
    const QByteArray out = AudioOutput::convertSamples(pcm({1, -2}), QAudioFormat::Int32);
    QCOMPARE(out.size(), 8);
    qint32 v[2];
    std::memcpy(v, out.constData(), 8);
    QCOMPARE(v[0], 65536);
    QCOMPARE(v[1], -131072);
  }
  void toUInt8() {
    const QByteArray out = AudioOutput::convertSamples(pcm({0, -32768, 32767}), QAudioFormat::UInt8);
    QCOMPARE(out.size(), 3);
    QCOMPARE(static_cast<quint8>(out[0]), quint8(128));
    QCOMPARE(static_cast<quint8>(out[1]), quint8(0));
    QCOMPARE(static_cast<quint8>(out[2]), quint8(255));
  }
  void int16Passthrough() {
    const QByteArray in = pcm({1, 2, 3, 4});
    QCOMPARE(AudioOutput::convertSamples(in, QAudioFormat::Int16), in);
    QVERIFY(AudioOutput::convertSamples(in, QAudioFormat::Unknown).isEmpty());
  }
  void resampleThenFloat() {
    // 32768 -> 48000 Hz, 1 s constant signal: ~48000 frames of float stereo (8 bytes per frame).
    LinearResampler r(32768, 48000);
    QByteArray in(32768 * 4, 0);
    auto* s = reinterpret_cast<qint16*>(in.data());
    for (int i = 0; i < 32768 * 2; ++i) s[i] = 16384;
    const QByteArray out = AudioOutput::convertSamples(r.process(in), QAudioFormat::Float);
    const qsizetype frames = out.size() / 8;
    QVERIFY(std::abs(frames - 48000) <= 2);
    float f;
    std::memcpy(&f, out.constData() + out.size() - 4, 4);
    QCOMPARE(f, 0.5f);
  }
};

QTEST_GUILESS_MAIN(AudioTest)
#include "audio_test.moc"
