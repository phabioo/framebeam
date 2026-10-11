#include "format_helpers.h"
#include "synth_formats.h"

#include <QtTest>

using namespace framebeam::formats;

class LzTest : public QObject {
    Q_OBJECT

    static std::string sample() {
        std::string d;
        for (int i = 0; i < 40; ++i) d += "ABCDEFGH";           // short/medium back-references
        d.append(300, '\0');                                     // run longer than 0x111 (4-byte LZ11 form)
        d.append(40, 'x');                                       // 3-byte LZ11 form (0x11..0x110)
        unsigned x = 12345;
        for (int i = 0; i < 500; ++i) { x = x * 1103515245u + 12345u; d.push_back(static_cast<char>(x >> 16)); }  // literals
        d += d.substr(100, 200);
        return d;
    }

private slots:
    void handWrittenLz11() {
        // "AB" literals, then a 2-byte reference: len 4, distance 2 -> "ABABAB"
        const std::string in("\x11\x06\x00\x00\x20" "AB\x30\x01", 9);
        const Result r = lzDecompress(in);
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(r.data, std::string("ABABAB"));
    }
    void handWrittenLz10() {
        // "ab" literals, reference len 5 (nibble 2), distance 2 -> "ababababa"... total 7 bytes
        const std::string in("\x10\x07\x00\x00\x20" "ab\x20\x01", 9);
        const Result r = lzDecompress(in);
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(r.data, std::string("abababa"));
    }
    void roundTrip() {
        const std::string d = sample();
        for (int type : {0x10, 0x11}) {
            for (bool matches : {true, false}) {
                const std::string enc = framebeam::synth::makeLz(d, type, matches);
                const Result r = lzDecompress(enc);
                QVERIFY2(r.ok, r.error.c_str());
                QVERIFY(r.data == d);
            }
        }
        // the compressed LZ11 stream really uses the long forms (smaller than literals)
        QVERIFY(framebeam::synth::makeLz11(d).size() < d.size());
    }
    void emptyAndExtendedSize() {
        QVERIFY(lzDecompress(framebeam::synth::makeLz11("")).ok);
        QCOMPARE(lzDecompress(framebeam::synth::makeLz11("")).data, std::string());
        QVERIFY(lzDecompress(framebeam::synth::makeLz10("")).ok);
    }
    void limits() {
        const std::string enc = framebeam::synth::makeLz11(std::string(1000, 'q'));
        QVERIFY(lzDecompress(enc, 1000).ok);
        const Result r = lzDecompress(enc, 999);
        QVERIFY(!r.ok);
        QVERIFY(r.error.find("limit") != std::string::npos);
        // header claims 16 MiB from a tiny input: no huge allocation, truncated error
        const std::string bomb("\x11\x00\x00\x00\x00\x00\x00\x01", 8);
        QVERIFY(!lzDecompress(bomb).ok);
    }
    void malformed() {
        QVERIFY(!lzDecompress("").ok);
        QVERIFY(!lzDecompress("\x11\x01").ok);
        QVERIFY(!lzDecompress(std::string("\x12\x01\x00\x00" "x", 5)).ok);
        // reference before the start of the output
        QVERIFY(!lzDecompress(std::string("\x10\x04\x00\x00\x80\x10\x05", 7)).ok);
        // every strict prefix of a valid stream fails cleanly
        const std::string enc = framebeam::synth::makeLz11(sample());
        for (size_t n = 0; n < enc.size(); n += 7) QVERIFY(!lzDecompress(std::string_view(enc).substr(0, n)).ok);
    }
};

QTEST_APPLESS_MAIN(LzTest)
#include "lz_test.moc"
