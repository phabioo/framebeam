#include "format_helpers.h"
#include "synth_formats.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace framebeam::formats;
namespace syn = framebeam::synth;

class RomfsTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;

    static std::map<std::string, std::string> files() {
        std::map<std::string, std::string> m;
        m["/root.bin"] = "root file";
        m["/a/b.bin"] = std::string("\x01\x02\x03\x04", 4);
        m["/a/c/d.txt"] = "deep";
        m["/a/c/e.txt"] = std::string(5000, 'e');
        m["/z/one"] = "1";
        m["/empty"] = "";
        return m;
    }
    QString write(const std::string& bytes, const char* name) {
        const QString p = dir_.filePath(QString::fromLatin1(name));
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly)) return {};
        f.write(bytes.data(), static_cast<qint64>(bytes.size()));
        return p;
    }
    static std::vector<std::string> names(const std::vector<RomfsEntry>& v) {
        std::vector<std::string> n;
        for (const auto& e : v) n.push_back((e.dir ? "d:" : "f:") + e.name);
        std::sort(n.begin(), n.end());
        return n;
    }

private slots:
    void initTestCase() { QVERIFY(dir_.isValid()); }

    void readFromCxiAndCci() {
        const std::string cxi = syn::makeNcchWithRomfs(files());
        const QString paths[2] = {write(cxi, "g.cxi"), write(syn::wrapNcsd(cxi), "g.3ds")};
        for (const QString& p : paths) {
            QVERIFY(!p.isEmpty());
            for (const auto& [path, data] : files()) {
                const Result r = romfsRead(p, path);
                QVERIFY2(r.ok, (path + ": " + r.error).c_str());
                QVERIFY2(r.data == data, path.c_str());
            }
            // path spelling: no leading slash, doubled slashes
            QCOMPARE(romfsRead(p, "a//c/d.txt").data, std::string("deep"));
        }
    }
    void list() {
        const QString p = write(syn::wrapNcsd(syn::makeNcchWithRomfs(files())), "l.3ds");
        std::string err;
        auto root = romfsList(p, "/", &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(names(root), (std::vector<std::string>{"d:a", "d:z", "f:empty", "f:root.bin"}));
        for (const auto& e : root)
            if (e.name == "root.bin") QCOMPARE(e.size, uint64_t(9));
        auto c = romfsList(p, "/a/c", &err);
        QCOMPARE(names(c), (std::vector<std::string>{"f:d.txt", "f:e.txt"}));
        for (const auto& e : c)
            if (e.name == "e.txt") QCOMPARE(e.size, uint64_t(5000));
        QCOMPARE(names(romfsList(p, "/a")), (std::vector<std::string>{"d:c", "f:b.bin"}));
        QVERIFY(romfsList(p, "/nope", &err).empty());
        QVERIFY(!err.empty());
    }
    void errors() {
        const QString p = write(syn::makeNcchWithRomfs(files()), "e.cxi");
        QVERIFY(!romfsRead(p, "/missing").ok);
        QVERIFY(!romfsRead(p, "/a/missing.bin").ok);
        QVERIFY(!romfsRead(p, "/nodir/x").ok);
        QVERIFY(!romfsRead(p, "/a").ok);  // directory
        QVERIFY(!romfsRead(p, "/").ok);
        QVERIFY(!romfsRead(p, "/a/c/d.txt/x").ok);  // file used as directory
        const Result big = romfsRead(p, "/a/c/e.txt", 4999);
        QVERIFY(!big.ok);
        QVERIFY(romfsRead(p, "/a/c/e.txt", 5000).ok);
        QVERIFY(!romfsRead(dir_.filePath("does-not-exist"), "/x").ok);
        QVERIFY(!romfsRead(write("tiny", "tiny.bin"), "/x").ok);
        QVERIFY(!romfsRead(write(std::string(0x400, 'x'), "junk.bin"), "/x").ok);
    }
    void encrypted() {
        const QString p = write(syn::makeNcchWithRomfs(files(), 1, false), "enc.cxi");
        const Result r = romfsRead(p, "/root.bin");
        QVERIFY(!r.ok);
        QCOMPARE(r.error, std::string("encrypted game: FrameBeam needs a decrypted game file"));
        std::string err;
        QVERIFY(romfsList(p, "/", &err).empty());
        QCOMPARE(err, std::string("encrypted game: FrameBeam needs a decrypted game file"));
        const QString q = write(syn::wrapNcsd(syn::makeNcchWithRomfs(files(), 1, false)), "enc.3ds");
        QCOMPARE(romfsRead(q, "/root.bin").error, std::string("encrypted game: FrameBeam needs a decrypted game file"));
    }
    void truncatedFiles() {
        const std::string full = syn::wrapNcsd(syn::makeNcchWithRomfs(files()));
        // cut at many points: header, IVFC, metadata, file data
        for (size_t n : {size_t(0), size_t(0x100), size_t(0x1FF), size_t(0x200), size_t(0x4000), size_t(0x4200), size_t(0x5000),
                         size_t(0x5100), size_t(0x6000), size_t(0x6040), size_t(0x6100), full.size() - 1}) {
            const QString p = write(full.substr(0, n), "cut.3ds");
            const Result r = romfsRead(p, "/a/c/e.txt");
            QVERIFY2(!r.ok, std::to_string(n).c_str());
            std::string err;
            QVERIFY(romfsList(p, "/", &err).empty());
        }
    }
    void corruptionNeverCrashes() {
        const std::string good = syn::makeNcchWithRomfs(files());
        const size_t l3 = syn::kNcchRomfsOffset + syn::kRomfsLevel3Offset;
        // sibling/child loop in the root directory entry
        std::string bad = good;
        syn::set32(bad, l3 + syn::kRomfsDirMetaOffset + 4, 0);   // root sibling -> itself
        syn::set32(bad, l3 + syn::kRomfsDirMetaOffset + 8, 0);   // root child dir -> itself
        QVERIFY(!romfsRead(write(bad, "loop1.cxi"), "/nope/x").ok);
        QVERIFY(!romfsRead(write(bad, "loop1.cxi"), "/a/b.bin").ok);
        // out-of-range name length / offsets
        bad = good;
        syn::set32(bad, l3 + syn::kRomfsDirMetaOffset + 0x14, 0x7ffffffe);
        QVERIFY(!romfsRead(write(bad, "name.cxi"), "/a/b.bin").ok);
        // metadata table ranges and data offset beyond level 3
        bad = good;
        syn::set32(bad, l3 + 0x10, 0xffffff00u);
        QVERIFY(!romfsRead(write(bad, "range.cxi"), "/a/b.bin").ok);
        bad = good;
        syn::set32(bad, l3 + 0x24, 0xfffffff0u);
        QVERIFY(!romfsRead(write(bad, "dataoff.cxi"), "/a/b.bin").ok);
        // IVFC parameters
        bad = good;
        syn::set32(bad, syn::kNcchRomfsOffset + 0x4C, 31);
        QVERIFY(!romfsRead(write(bad, "ivfc.cxi"), "/a/b.bin").ok);
        bad = good;
        syn::set32(bad, syn::kNcchRomfsOffset + 8, 0xffffffffu);
        QVERIFY(!romfsRead(write(bad, "master.cxi"), "/a/b.bin").ok);
        // deterministic byte-flip fuzz over header, IVFC and metadata
        unsigned x = 99;
        const QString fz = dir_.filePath("fuzz.cxi");
        int rejected = 0;
        for (int i = 0; i < 400; ++i) {
            bad = good;
            for (int k = 0; k < 3; ++k) {
                x = x * 1664525u + 1013904223u;
                const size_t pos = (x >> 8) % (l3 + 0x200);
                x = x * 1664525u + 1013904223u;
                bad[pos] = static_cast<char>(x >> 24);
            }
            write(bad, "fuzz.cxi");
            for (const char* path : {"/a/c/e.txt", "/root.bin", "/nope"}) rejected += romfsRead(fz, path).ok ? 0 : 1;
            romfsList(fz, "/a");
        }
        QVERIFY(rejected > 0);
    }
};

QTEST_APPLESS_MAIN(RomfsTest)
#include "romfs_test.moc"
