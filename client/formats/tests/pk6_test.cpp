#include "format_helpers.h"
#include "synth_formats.h"

#include <QtTest>

using namespace uzume::formats;

class Pk6Test : public QObject {
    Q_OBJECT
private slots:
    void knownStream() {
        // EC = 0: LCRNG outputs (seed >> 16) are 0x0000, 0xE97E, 0x5271 (computed independently).
        std::string z(232, '\0');
        const Pk6 e = pk6Encrypt(z);
        QVERIFY(e.ok);
        QCOMPARE(int(uint8_t(e.data[8])), 0x00);
        QCOMPARE(int(uint8_t(e.data[9])), 0x00);
        QCOMPARE(int(uint8_t(e.data[10])), 0x7E);
        QCOMPARE(int(uint8_t(e.data[11])), 0xE9);
        QCOMPARE(int(uint8_t(e.data[12])), 0x71);
        QCOMPARE(int(uint8_t(e.data[13])), 0x52);
        QCOMPARE(int(uint8_t(e.data[0])), 0);  // header stays plain
    }
    void roundTripAllShuffles() {
        for (bool party : {false, true}) {
            for (unsigned sv = 0; sv < 32; ++sv) {
                const uint32_t ec = (sv << 13) | 0xA5000000u | (sv * 977);
                const auto s = uzume::synth::makePk6(ec, uint16_t(100 + sv), party, sv + 1);
                const Pk6 d = pk6Decrypt(s.encrypted);
                QVERIFY2(d.ok, d.error.c_str());
                QVERIFY2(d.data == s.plain, "decrypt differs from synth plain");
                QVERIFY(d.checksumValid);
                QCOMPARE(int(uint8_t(d.data[8])) | (int(uint8_t(d.data[9])) << 8), int(100 + sv));
                const Pk6 e = pk6Encrypt(s.plain);
                QVERIFY(e.ok);
                QVERIFY(e.checksumValid);
                QVERIFY2(e.data == s.encrypted, "encrypt differs from synth encrypted");
            }
        }
    }
    void corruptionDetected() {
        auto s = uzume::synth::makePk6(0x12345678, 25);
        s.encrypted[100] = char(s.encrypted[100] ^ 1);
        const Pk6 d = pk6Decrypt(s.encrypted);
        QVERIFY(d.ok);
        QVERIFY(!d.checksumValid);
        std::string p = s.plain;
        p[9] = char(p[9] ^ 0x40);  // edit without fixing the checksum
        QVERIFY(!pk6Encrypt(p).checksumValid);
    }
    void sizes() {
        QVERIFY(!pk6Decrypt("").ok);
        QVERIFY(!pk6Decrypt(std::string(231, 'a')).ok);
        QVERIFY(!pk6Encrypt(std::string(233, 'a')).ok);
        QVERIFY(!pk6Decrypt(std::string(344, 'a')).ok);
        QVERIFY(pk6Decrypt(std::string(232, 'a')).ok);
        QVERIFY(pk6Decrypt(std::string(260, 'a')).ok);
    }
};

QTEST_APPLESS_MAIN(Pk6Test)
#include "pk6_test.moc"
