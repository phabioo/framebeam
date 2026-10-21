#include "format_helpers.h"
#include "synth_formats.h"

#include <QtTest>

using namespace framebeam::formats;

class GarcTest : public QObject {
    Q_OBJECT

    static std::vector<std::vector<std::string>> files() {
        return {{"first"}, {"second file", "sub1", std::string(5, '\x7f')}, {""}, {std::string(1000, 'z')}};
    }

private slots:
    void parseBothVersions() {
        for (uint32_t version : {0x0400u, 0x0600u}) {
            const std::string g = framebeam::synth::makeGarc(files(), version);
            const Garc p = garcParse(g);
            QVERIFY2(p.ok, p.error.c_str());
            QCOMPARE(p.files.size(), size_t(4));
            QCOMPARE(p.files[1].size(), size_t(3));
            QCOMPARE(p.files[1][0].length, 11u);
            const auto want = files();
            for (size_t i = 0; i < want.size(); ++i) {
                for (size_t s = 0; s < want[i].size(); ++s) {
                    const Result r = garcFile(g, i, s);
                    QVERIFY2(r.ok, r.error.c_str());
                    QVERIFY(r.data == want[i][s]);
                }
            }
        }
    }
    void defaultSub() {
        const std::string g = framebeam::synth::makeGarc(std::vector<std::string>{"aa", "bbb"});
        QCOMPARE(garcFile(g, 1).data, std::string("bbb"));
    }
    // PE-12: the parsed archive is reused between calls, but an archive of the same size with other tables is never mistaken for it.
    void cachedParseFollowsChangedTables() {
        const std::string a = framebeam::synth::makeGarc(std::vector<std::string>{"aaaa", "bbbb"});
        const std::string b = framebeam::synth::makeGarc(std::vector<std::string>{"bbbb", "aaaa"});
        QCOMPARE(a.size(), b.size());
        for (int round = 0; round < 3; ++round) {  // repeated calls on the same bytes use the cache
            QCOMPARE(garcFile(a, 0).data, std::string("aaaa"));
            QCOMPARE(garcFile(a, 1).data, std::string("bbbb"));
        }
        QCOMPARE(garcFile(b, 0).data, std::string("bbbb"));
        std::string c = a;  // same size, one table byte changed: sub-entry 1 now ends one byte earlier
        QVERIFY(garcFile(a, 1).ok);
        c[c.size() - 1] = 'Z';  // data only: tables identical, so the cached parse applies and the new byte is served
        QCOMPARE(garcFile(c, 1).data, std::string("bbbZ"));
        QVERIFY(!garcFile(std::string_view(a).substr(0, 8), 0).ok);  // truncated: reparsed, fails
    }
    void outOfRange() {
        const std::string g = framebeam::synth::makeGarc(files());
        QVERIFY(!garcFile(g, 4).ok);
        QVERIFY(!garcFile(g, 0, 1).ok);
        QVERIFY(!garcFile(g, size_t(-1)).ok);
    }
    void magic() {
        // Real GARC files start with "CRAG" (u32 0x47415243 little-endian), not "GARC".
        const std::string g = framebeam::synth::makeGarc(files());
        QCOMPARE(g.substr(0, 4), std::string("CRAG"));
        QVERIFY(garcParse(g).ok);
        std::string wrong = g;
        wrong.replace(0, 4, "GARC");
        const Garc p = garcParse(wrong);
        QVERIFY(!p.ok);
        QCOMPARE(p.error, std::string("GARC: bad magic"));
        QVERIFY(!garcFile(wrong, 0).ok);
    }
    void malformed() {
        const std::string g = framebeam::synth::makeGarc(files());
        QVERIFY(!garcParse("").ok);
        QVERIFY(!garcParse("CRAG").ok);
        std::string bad = g;
        bad[0] = 'X';
        QVERIFY(!garcParse(bad).ok);
        bad = g;
        bad[0x0A] = 0x05;  // version
        QVERIFY(!garcParse(bad).ok);
        // every truncation fails or yields only in-range entries; never crashes
        for (size_t n = 0; n < g.size(); ++n) {
            const Garc p = garcParse(std::string_view(g).substr(0, n));
            if (p.ok)
                for (size_t i = 0; i < p.files.size(); ++i)
                    for (size_t s = 0; s < p.files[i].size(); ++s) QVERIFY(garcFile(std::string_view(g).substr(0, n), i, s).ok);
        }
        QVERIFY(!garcParse(std::string_view(g).substr(0, 0x30)).ok);
        // entry count mismatch between FATO and FATB
        bad = g;
        bad[0x1C + 8] = 3;
        QVERIFY(!garcParse(bad).ok);
        // data offset beyond the file, and a file range beyond the data
        bad = g;
        framebeam::synth::set32(bad, 0x10, 0x7fffffff);
        QVERIFY(!garcParse(bad).ok);
        bad = g;
        const Garc p = garcParse(g);
        // first FATB sub-entry length field -> huge
        const size_t fatb = 0x1C + (0xC + 4 * 4);
        framebeam::synth::set32(bad, fatb + 0xC + 4 + 8, 0x7ffffff0);
        QVERIFY(!garcParse(bad).ok);
        QVERIFY(p.ok);
    }
};

QTEST_APPLESS_MAIN(GarcTest)
#include "garc_test.moc"
