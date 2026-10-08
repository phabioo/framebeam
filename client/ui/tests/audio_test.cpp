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
  // 0.6 D6: underrun counting and buffer fill.
  void readPadsWithSilence() {
    AudioQueueDevice d;
    d.configure(8, 0, 1000, 1000);
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    d.append(QByteArray(16, 'x'));
    QByteArray out(30, 'z');  // rounds down to 3 frames; only 2 are queued: no padding
    QCOMPARE(d.read(out.data(), out.size()), qint64(16));
    QCOMPARE(out.left(16), QByteArray(16, 'x'));
    QCOMPARE(out.mid(16), QByteArray(14, 'z'));  // untouched
    out.fill('z');
    QCOMPARE(d.read(out.data(), 30), qint64(24));  // dry: whole request is silence
    QCOMPARE(out.left(24), QByteArray(24, '\0'));
    QCOMPARE(out.mid(24), QByteArray(6, 'z'));
  }
  void silenceIsMidpointForUInt8() {
    AudioQueueDevice d;
    d.configure(2, static_cast<char>(0x80), 100, 100);
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QByteArray out(4, 0);
    QCOMPARE(d.read(out.data(), 4), qint64(4));
    QCOMPARE(out, QByteArray(4, static_cast<char>(0x80)));
  }
  void queuedDataKeepsOrder() {
    AudioQueueDevice d;
    d.configure(2, 0, 100, 100);
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    d.append(QByteArray("ab"));
    d.append(QByteArray("cd"));
    QCOMPARE(d.queuedBytes(), qint64(4));
    char o[2];
    QCOMPARE(d.read(o, 2), qint64(2));
    QCOMPARE(QByteArray(o, 2), QByteArray("ab"));
    QCOMPARE(d.read(o, 2), qint64(2));
    QCOMPARE(QByteArray(o, 2), QByteArray("cd"));
    QCOMPARE(d.queuedBytes(), qint64(0));
  }
  void underrunOncePerDrySpell() {
    AudioQueueDevice d;
    d.configure(2, 0, 100, 100);
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    char o[4];
    d.read(o, 4);  // dry before any data: no underrun
    QCOMPARE(d.underruns(), 0);
    d.append(QByteArray(2, 'a'));
    d.read(o, 4);  // partial read, not dry yet
    QCOMPARE(d.underruns(), 0);
    d.read(o, 4);  // now dry after data
    QCOMPARE(d.underruns(), 1);
    d.read(o, 4);  // still dry: not counted again
    d.read(o, 4);
    QCOMPARE(d.underruns(), 1);
    d.append(QByteArray(2, 'b'));
    d.read(o, 4);
    d.read(o, 4);
    QCOMPARE(d.underruns(), 2);
  }
  void underrunNotCountedWhileOff() {
    AudioQueueDevice d;
    d.configure(2, 0, 100, 100);
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    char o[4];
    d.setUnderrunCounting(false);
    d.append(QByteArray(2, 'a'));
    d.read(o, 4);
    d.read(o, 4);
    QCOMPARE(d.underruns(), 0);
    d.append(QByteArray(2, 'a'));
    d.setUnderrunCounting(true);  // data predates re-enabling
    d.read(o, 4);
    d.read(o, 4);
    QCOMPARE(d.underruns(), 0);
    d.append(QByteArray(2, 'a'));
    d.read(o, 4);
    d.read(o, 4);
    QCOMPARE(d.underruns(), 1);
  }
  void capDropsOldestWholeFrames() {
    AudioQueueDevice d;
    d.configure(4, 0, 100, 8);
    QCOMPARE(d.append(QByteArray("aaaabbbb")), qint64(0));
    QCOMPARE(d.append(QByteArray("cccc")), qint64(4));
    QCOMPARE(d.queuedBytes(), qint64(8));
    QCOMPARE(d.append(QByteArray(20, 'd')), qint64(20));  // more than the cap in one go
    QCOMPARE(d.queuedBytes(), qint64(8));
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    char o[8];
    d.read(o, 8);
    QCOMPARE(QByteArray(o, 8), QByteArray(8, 'd'));
  }
  void bytesAvailableNeverZero() {
    AudioQueueDevice d;
    d.configure(8, 0, 1200, 8000);
    d.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QCOMPARE(d.bytesAvailable(), qint64(1200));  // empty: nominal
    d.append(QByteArray(4000, 'x'));
    QCOMPARE(d.bytesAvailable(), qint64(4000));  // queued
    QVERIFY(d.isSequential());
  }
  void underrunsStartAtZeroWithoutDevice() {
    AudioOutput out;
    QCOMPARE(out.underruns(), 0);
    QCOMPARE(out.bufferedMs(), 0.0);
    out.setUnderrunCounting(false);
    out.setUnderrunCounting(true);
    QCOMPARE(out.underruns(), 0);
  }
  void bufferFillInMilliseconds() {
    // 48 kHz, 8-byte float frames: 14400-byte buffer (37.5 ms), 7200 free -> 7200 bytes queued = 900 frames = 18.75 ms
    QVERIFY(qAbs(AudioOutput::bufferedMsFor(14400, 7200, 0, 8, 48000) - 18.75) < 1e-9);
    // plus what waits in the device queue
    QVERIFY(qAbs(AudioOutput::bufferedMsFor(14400, 7200, 3600, 8, 48000) - 28.125) < 1e-9);
    QCOMPARE(AudioOutput::bufferedMsFor(14400, 14400, 0, 8, 48000), 0.0);  // empty sink
    QCOMPARE(AudioOutput::bufferedMsFor(100, 400, -5, 8, 48000), 0.0);     // bogus values never go negative
    QCOMPARE(AudioOutput::bufferedMsFor(100, 0, 0, 0, 48000), 0.0);        // no format yet
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
