#include "format_helpers.h"
#include "synth_formats.h"

#include <QImage>
#include <QtTest>

#include <array>

using namespace framebeam::formats;
namespace syn = framebeam::synth;

namespace {

struct Px { uint8_t r, g, b, a; };

uint8_t e5(unsigned v) { return uint8_t((v << 3) | (v >> 2)); }
uint8_t e6(unsigned v) { return uint8_t((v << 2) | (v >> 4)); }
uint8_t e4(unsigned v) { return uint8_t(v * 17); }

// Morton index of (x, y) inside an 8x8 tile, computed by bit interleaving.
int morton(int x, int y) {
    int m = 0;
    for (int b = 0; b < 3; ++b) m |= (((x >> b) & 1) << (2 * b)) | (((y >> b) & 1) << (2 * b + 1));
    return m;
}

// Raw texel (as bytes, or a nibble for 4 bit formats) and the expected RGBA for a value v.
struct Texel { std::string bytes; unsigned nibble = 0; Px px{}; };
Texel texelFor(int fmt, unsigned v) {
    Texel t;
    const uint8_t b = uint8_t(v);
    auto u16 = [&](unsigned w) { t.bytes = {char(w & 0xFF), char(w >> 8)}; };
    switch (fmt) {
    case 0: t.bytes = {char(b + 3), char(b + 2), char(b + 1), char(b)}; t.px = {b, uint8_t(b + 1), uint8_t(b + 2), uint8_t(b + 3)}; break;
    case 1: t.bytes = {char(b + 2), char(b + 1), char(b)}; t.px = {b, uint8_t(b + 1), uint8_t(b + 2), 255}; break;
    case 2: { unsigned r = v & 31, g = (v >> 1) & 31, bl = (v >> 2) & 31, a = v & 1;
              u16((r << 11) | (g << 6) | (bl << 1) | a); t.px = {e5(r), e5(g), e5(bl), uint8_t(a ? 255 : 0)}; break; }
    case 3: { unsigned r = v & 31, g = v & 63, bl = (v >> 3) & 31;
              u16((r << 11) | (g << 5) | bl); t.px = {e5(r), e6(g), e5(bl), 255}; break; }
    case 4: { unsigned r = v & 15, g = (v >> 4) & 15, bl = (v >> 1) & 15, a = (v >> 3) & 15;
              u16((r << 12) | (g << 8) | (bl << 4) | a); t.px = {e4(r), e4(g), e4(bl), e4(a)}; break; }
    case 5: t.bytes = {char(b), char(b ^ 0x55)}; t.px = {uint8_t(b ^ 0x55), uint8_t(b ^ 0x55), uint8_t(b ^ 0x55), b}; break;
    case 6: t.bytes = {char(b), char(b ^ 0x33)}; t.px = {uint8_t(b ^ 0x33), b, 0, 255}; break;
    case 7: t.bytes = {char(b)}; t.px = {b, b, b, 255}; break;
    case 8: t.bytes = {char(b)}; t.px = {255, 255, 255, b}; break;
    case 9: t.bytes = {char(b)}; t.px = {e4(b >> 4), e4(b >> 4), e4(b >> 4), e4(b & 15)}; break;
    case 10: t.nibble = v & 15; t.px = {e4(v & 15), e4(v & 15), e4(v & 15), 255}; break;
    default: t.nibble = v & 15; t.px = {255, 255, 255, e4(v & 15)}; break;
    }
    return t;
}

// Builds tiled data and expected pixels for a w x h image (stored size padded to 8).
void build(int fmt, int w, int h, std::string& data, std::vector<Px>& want) {
    const int sw = (w + 7) & ~7, sh = (h + 7) & ~7;
    const bool nib = fmt == 10 || fmt == 11;
    std::vector<Texel> tex(static_cast<size_t>(sw) * static_cast<size_t>(sh));
    want.assign(static_cast<size_t>(w) * static_cast<size_t>(h), Px{});
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            const Texel t = texelFor(fmt, unsigned((x * 7 + y * 13 + 1) & 0xFF));
            const size_t idx = (size_t(y / 8) * size_t(sw / 8) + size_t(x / 8)) * 64 + size_t(morton(x % 8, y % 8));
            tex[idx] = t;
            if (x < w && y < h) want[size_t(y) * size_t(w) + size_t(x)] = t.px;
        }
    }
    data.clear();
    if (nib) {
        for (size_t i = 0; i < tex.size(); i += 2) data.push_back(char(tex[i].nibble | (tex[i + 1].nibble << 4)));
    } else {
        for (const auto& t : tex) data += t.bytes;
    }
}

// ETC1 block in standard layout s[0..7] -> 3DS storage (byte-reversed).
std::string ctr(std::array<uint8_t, 8> s) {
    std::string o;
    for (int i = 7; i >= 0; --i) o.push_back(char(s[size_t(i)]));
    return o;
}
Px at(const Image& im, int x, int y) {
    const auto* p = reinterpret_cast<const uint8_t*>(im.rgba.data()) + (size_t(y) * size_t(im.width) + size_t(x)) * 4;
    return {p[0], p[1], p[2], p[3]};
}
bool eq(Px a, Px b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

}  // namespace

class TextureTest : public QObject {
    Q_OBJECT
private slots:
    void linearFormats_data() {
        QTest::addColumn<int>("fmt");
        const char* names[] = {"RGBA8", "RGB8", "RGBA5551", "RGB565", "RGBA4", "LA8", "HILO8", "L8", "A8", "LA4", "L4", "A4"};
        for (int f = 0; f < 12; ++f) QTest::newRow(names[f]) << f;
    }
    void linearFormats() {
        QFETCH(int, fmt);
        for (auto dims : {std::pair{16, 16}, std::pair{13, 11}, std::pair{8, 8}, std::pair{40, 8}}) {
            std::string data;
            std::vector<Px> want;
            build(fmt, dims.first, dims.second, data, want);
            const Image im = ctrTextureDecode(data, dims.first, dims.second, fmt);
            QVERIFY2(im.ok, im.error.c_str());
            QCOMPARE(im.width, dims.first);
            QCOMPARE(im.rgba.size(), size_t(dims.first) * size_t(dims.second) * 4);
            for (int y = 0; y < dims.second; ++y)
                for (int x = 0; x < dims.first; ++x)
                    QVERIFY2(eq(at(im, x, y), want[size_t(y) * size_t(dims.first) + size_t(x)]),
                             (std::to_string(x) + "," + std::to_string(y)).c_str());
            // truncated data
            QVERIFY(!ctrTextureDecode(std::string_view(data).substr(0, data.size() - 1), dims.first, dims.second, fmt).ok);
        }
    }
    void etc1Blocks() {
        auto px = [](const std::string& blk, bool alpha = false) { return etc1Decode(blk, 4, 4, alpha); };
        // individual mode, base (8,8,8) -> 0x88, table 0, all indices 0 (+2) -> 138
        Image im = px(ctr({0x88, 0x88, 0x88, 0, 0, 0, 0, 0}));
        QVERIFY2(im.ok, im.error.c_str());
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) QVERIFY(eq(at(im, x, y), {138, 138, 138, 255}));
        // all indices 3 (-8) -> 128
        im = px(ctr({0x88, 0x88, 0x88, 0, 0xFF, 0xFF, 0xFF, 0xFF}));
        QVERIFY(eq(at(im, 3, 3), {128, 128, 128, 255}));
        // msb=0, lsb=1 -> index 1 (+8) -> 144
        im = px(ctr({0x88, 0x88, 0x88, 0, 0, 0, 0xFF, 0xFF}));
        QVERIFY(eq(at(im, 0, 0), {144, 144, 144, 255}));
        // pixel (x=1,y=2) is bit 6 (column-major): msb set only there -> index 2 (-2) -> 134
        im = px(ctr({0x88, 0x88, 0x88, 0, 0, 0x40, 0, 0}));
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                QVERIFY(eq(at(im, x, y), (x == 1 && y == 2) ? Px{134, 134, 134, 255} : Px{138, 138, 138, 255}));
        // differential mode: R1=16,dR=+1 / G1=16,dG=0 / B1=16,dB=-1; cw1=1 (+5), cw2=2 (+9)
        // sub 1 base (132,132,132)+5 = 137; sub 2 base (140,132,123)+9 = (149,141,132)
        im = px(ctr({0x81, 0x80, 0x87, 0x2A, 0, 0, 0, 0}));  // flip = 0: left/right halves
        for (int y = 0; y < 4; ++y) {
            QVERIFY(eq(at(im, 1, y), {137, 137, 137, 255}));
            QVERIFY(eq(at(im, 2, y), {149, 141, 132, 255}));
        }
        im = px(ctr({0x81, 0x80, 0x87, 0x2B, 0, 0, 0, 0}));  // flip = 1: top/bottom halves
        for (int x = 0; x < 4; ++x) {
            QVERIFY(eq(at(im, x, 1), {137, 137, 137, 255}));
            QVERIFY(eq(at(im, x, 2), {149, 141, 132, 255}));
        }
        // ETC1A4: alpha block first (little-endian, nibble p = x*4+y); pixel (2,1) -> nibble 9 = 0xA -> 170, rest 0
        im = px(std::string("\0\0\0\0\xA0\0\0\0", 8) + ctr({0x88, 0x88, 0x88, 0, 0, 0, 0, 0}), true);
        QVERIFY2(im.ok, im.error.c_str());
        QVERIFY(eq(at(im, 2, 1), {138, 138, 138, 170}));
        QVERIFY(eq(at(im, 0, 0), {138, 138, 138, 0}));
        QVERIFY(eq(at(im, 1, 2), {138, 138, 138, 0}));
        // limits
        QVERIFY(!etc1Decode("", 4, 4, false).ok);
        QVERIFY(!etc1Decode(std::string(8, 'x'), 5, 4, false).ok);
        QVERIFY(!etc1Decode(std::string(8, 'x'), 0, 4, false).ok);
        QVERIFY(!etc1Decode(std::string(8, 'x'), 4, 4, true).ok);
    }
    void etc1Tiled() {
        // 8x8 ETC1 texture: four blocks in Morton order (0,0) (4,0) (0,4) (4,4), flat gray k*17+2
        std::string data;
        for (int k = 1; k <= 4; ++k) {
            const uint8_t c = uint8_t(k * 0x11);
            data += ctr({c, c, c, 0, 0, 0, 0, 0});
        }
        const Image im = ctrTextureDecode(data, 8, 8, 12);
        QVERIFY2(im.ok, im.error.c_str());
        QVERIFY(eq(at(im, 0, 0), {19, 19, 19, 255}));
        QVERIFY(eq(at(im, 7, 3), {36, 36, 36, 255}));
        QVERIFY(eq(at(im, 3, 7), {53, 53, 53, 255}));
        QVERIFY(eq(at(im, 4, 4), {70, 70, 70, 255}));
        const Image a = ctrTextureDecode(std::string(2 * 4 * 16, '\0'), 8, 8, 13);  // ETC1A4: 16 bytes per block
        QVERIFY2(a.ok, a.error.c_str());
        QVERIFY(!ctrTextureDecode(std::string(31, '\0'), 8, 8, 12).ok);
    }
    void bclimRoundTrip() {
        const int w = 20, h = 12;
        std::string rgba;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                rgba += char(x * 12);
                rgba += char(y * 20);
                rgba += char(x * 5 + y);
                rgba += char(255 - x * 9);
            }
        // RGBA8 (BCLIM format 9): exact
        Image im = bclimDecode(syn::makeBclim(w, h, 9, rgba));
        QVERIFY2(im.ok, im.error.c_str());
        QCOMPARE(im.width, w);
        QCOMPARE(im.height, h);
        QVERIFY(im.rgba == rgba);
        // RGB565 (format 5): quantised, alpha 255
        im = bclimDecode(syn::makeBclim(w, h, 5, rgba));
        QVERIFY2(im.ok, im.error.c_str());
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto* s = reinterpret_cast<const uint8_t*>(rgba.data()) + (size_t(y) * w + size_t(x)) * 4;
                QVERIFY(eq(at(im, x, y), {e5(s[0] >> 3), e6(s[1] >> 2), e5(s[2] >> 3), 255}));
            }
        // power-of-two exact size too
        QVERIFY(bclimDecode(syn::makeBclim(8, 8, 9, std::string(8 * 8 * 4, 'q'))).ok);
    }
    void bclimMalformed() {
        const std::string good = syn::makeBclim(16, 16, 9, std::string(16 * 16 * 4, 'a'));
        QVERIFY(bclimDecode(good).ok);
        QVERIFY(!bclimDecode("").ok);
        QVERIFY(!bclimDecode(good.substr(0, 20)).ok);
        QVERIFY(!bclimDecode(good.substr(0, good.size() - 1)).ok);   // footer cut
        QVERIFY(!bclimDecode(good.substr(good.size() - 0x28)).ok);   // footer without image data
        std::string bad = good;
        bad[good.size() - 0x28 + 0x20] = 14;  // unknown format
        QVERIFY(!bclimDecode(bad).ok);
        bad = good;
        bad[good.size() - 0x28 + 0x21] = 4;  // swizzle
        QVERIFY(!bclimDecode(bad).ok);
        bad = good;
        syn::set16(bad, good.size() - 0x28 + 0x1C, 5000);  // width too large
        QVERIFY(!bclimDecode(bad).ok);
        syn::set16(bad, good.size() - 0x28 + 0x1C, 0);
        QVERIFY(!bclimDecode(bad).ok);
        for (size_t n = 0; n < good.size(); n += 61) bclimDecode(std::string_view(good).substr(0, n));
    }
    void limitsAndErrors() {
        const std::string d(64, '\0');
        QVERIFY(!ctrTextureDecode(d, 0, 8, 7).ok);
        QVERIFY(!ctrTextureDecode(d, 8, -1, 7).ok);
        QVERIFY(!ctrTextureDecode(d, 4097, 8, 7).ok);
        QVERIFY(!ctrTextureDecode(d, 8, 4097, 7).ok);
        QVERIFY(!ctrTextureDecode(d, 8, 8, 14).ok);
        QVERIFY(!ctrTextureDecode(d, 8, 8, -1).ok);
        QVERIFY(ctrTextureDecode(d, 8, 8, 7).ok);
        // 4096 x 4096 is accepted (needs the data), larger is not
        QVERIFY(ctrTextureDecode(std::string(size_t(4096) * 4096 / 2, '\0'), 4096, 4096, 10).ok);
    }
    void png() {
        const int w = 5, h = 3;
        std::string rgba;
        for (int i = 0; i < w * h; ++i) { rgba += char(i * 10); rgba += char(255 - i * 3); rgba += char(i); rgba += char(i * 16); }
        Image im;
        im.ok = true;
        im.width = w;
        im.height = h;
        im.rgba = rgba;
        const Result p = pngEncode(im);
        QVERIFY2(p.ok, p.error.c_str());
        QCOMPARE(p.data.substr(1, 3), std::string("PNG"));
        QImage q = QImage::fromData(reinterpret_cast<const uchar*>(p.data.data()), int(p.data.size()), "PNG");
        QVERIFY(!q.isNull());
        q = q.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(q.width(), w);
        QCOMPARE(q.height(), h);
        for (int y = 0; y < h; ++y)
            QVERIFY(std::memcmp(q.constScanLine(y), rgba.data() + size_t(y) * w * 4, size_t(w) * 4) == 0);
        Image bad = im;
        bad.rgba.pop_back();
        QVERIFY(!pngEncode(bad).ok);
        QVERIFY(!pngEncode(Image{}).ok);
    }
};

QTEST_APPLESS_MAIN(TextureTest)
#include "texture_test.moc"
