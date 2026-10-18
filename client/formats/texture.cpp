#include "format_helpers.h"

#include "bytes.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>

#include <algorithm>
#include <cstring>

namespace framebeam::formats {
namespace {

using detail::u8at;

constexpr int kMaxDim = 4096;

struct Rgba {
    uint8_t r, g, b, a;
};

uint8_t expand5(uint32_t v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); }
uint8_t expand6(uint32_t v) { return static_cast<uint8_t>((v << 2) | (v >> 4)); }
uint8_t expand4(uint32_t v) { return static_cast<uint8_t>(v * 17); }

int bitsPerPixel(int format) {
    static const int kBpp[14] = {32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8};
    return kBpp[format];
}

Image fail(const char* msg) {
    Image i;
    i.error = msg;
    return i;
}

void put(std::string& rgba, int w, int x, int y, Rgba c) {
    char* p = &rgba[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
    p[0] = static_cast<char>(c.r);
    p[1] = static_cast<char>(c.g);
    p[2] = static_cast<char>(c.b);
    p[3] = static_cast<char>(c.a);
}

// ---- ETC1 ----
const int kEtcMod[8][4] = {{2, 8, -2, -8},   {5, 17, -5, -17},  {9, 29, -9, -29},   {13, 42, -13, -42},
                           {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183}};

uint8_t clamp8(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }

// Decodes one 3DS ETC1 colour block (8 bytes, byte-reversed vs. the standard layout) into 16 pixels, [y*4+x].
void etcBlock(const uint8_t* b, Rgba out[16]) {
    uint8_t s[8];
    for (int i = 0; i < 8; ++i) s[i] = b[7 - i];
    const bool diff = (s[3] & 2) != 0;
    const bool flip = (s[3] & 1) != 0;
    const int cw[2] = {(s[3] >> 5) & 7, (s[3] >> 2) & 7};
    int base[2][3];
    if (diff) {
        for (int c = 0; c < 3; ++c) {
            const int v1 = s[c] >> 3;
            int d = s[c] & 7;
            if (d >= 4) d -= 8;
            const int v2 = std::clamp(v1 + d, 0, 31);
            base[0][c] = expand5(static_cast<uint32_t>(v1));
            base[1][c] = expand5(static_cast<uint32_t>(v2));
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            base[0][c] = expand4(static_cast<uint32_t>(s[c] >> 4));
            base[1][c] = expand4(static_cast<uint32_t>(s[c] & 0xF));
        }
    }
    const uint32_t msb = (static_cast<uint32_t>(s[4]) << 8) | s[5];
    const uint32_t lsb = (static_cast<uint32_t>(s[6]) << 8) | s[7];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int sub = flip ? (y >= 2 ? 1 : 0) : (x >= 2 ? 1 : 0);
            const int p = x * 4 + y;
            const int idx = static_cast<int>((((msb >> p) & 1) << 1) | ((lsb >> p) & 1));
            const int mod = kEtcMod[cw[sub]][idx];
            out[y * 4 + x] = {clamp8(base[sub][0] + mod), clamp8(base[sub][1] + mod), clamp8(base[sub][2] + mod), 255};
        }
    }
}

void etcAlpha(const uint8_t* b, Rgba px[16]) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | b[i];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) px[y * 4 + x].a = expand4(static_cast<uint32_t>((v >> ((x * 4 + y) * 4)) & 0xF));
}

// ---- Linear texel formats ----
Rgba texel(const uint8_t* d, size_t index, int format) {
    switch (format) {
    case 0: {  // RGBA8: A,B,G,R
        const uint8_t* p = d + index * 4;
        return {p[3], p[2], p[1], p[0]};
    }
    case 1: {  // RGB8: B,G,R
        const uint8_t* p = d + index * 3;
        return {p[2], p[1], p[0], 255};
    }
    case 2: {
        const uint8_t* p = d + index * 2;
        const uint32_t v = p[0] | (p[1] << 8);
        return {expand5((v >> 11) & 31), expand5((v >> 6) & 31), expand5((v >> 1) & 31), static_cast<uint8_t>((v & 1) ? 255 : 0)};
    }
    case 3: {
        const uint8_t* p = d + index * 2;
        const uint32_t v = p[0] | (p[1] << 8);
        return {expand5((v >> 11) & 31), expand6((v >> 5) & 63), expand5(v & 31), 255};
    }
    case 4: {
        const uint8_t* p = d + index * 2;
        const uint32_t v = p[0] | (p[1] << 8);
        return {expand4((v >> 12) & 15), expand4((v >> 8) & 15), expand4((v >> 4) & 15), expand4(v & 15)};
    }
    case 5: {  // LA8: A, L
        const uint8_t* p = d + index * 2;
        return {p[1], p[1], p[1], p[0]};
    }
    case 6: {  // HILO8: byte0 -> G, byte1 -> R
        const uint8_t* p = d + index * 2;
        return {p[1], p[0], 0, 255};
    }
    case 7: {
        const uint8_t v = d[index];
        return {v, v, v, 255};
    }
    case 8:
        return {255, 255, 255, d[index]};
    case 9: {  // LA4: L high nibble, A low nibble
        const uint8_t v = d[index];
        const uint8_t l = expand4(v >> 4);
        return {l, l, l, expand4(v & 15)};
    }
    case 10: {  // L4
        const uint8_t n = (index & 1) ? (d[index / 2] >> 4) : (d[index / 2] & 15);
        const uint8_t l = expand4(n);
        return {l, l, l, 255};
    }
    default: {  // 11: A4
        const uint8_t n = (index & 1) ? (d[index / 2] >> 4) : (d[index / 2] & 15);
        return {255, 255, 255, expand4(n)};
    }
    }
}

// Decodes tiled data of stored size (sw x sh, multiples of 8) and crops to w x h.
Image decodeTiled(std::string_view in, int sw, int sh, int w, int h, int format) {
    const size_t need = static_cast<size_t>(sw) * static_cast<size_t>(sh) * static_cast<size_t>(bitsPerPixel(format)) / 8;
    if (in.size() < need) return fail("texture data truncated");
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, '\0');
    const auto* d = reinterpret_cast<const uint8_t*>(in.data());
    const int tilesX = sw / 8, tilesY = sh / 8;
    const bool etc = format >= 12;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            const size_t tile = static_cast<size_t>(ty) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx);
            if (etc) {
                const size_t blockBytes = format == 13 ? 16 : 8;
                for (int b = 0; b < 4; ++b) {
                    const uint8_t* src = d + (tile * 4 + static_cast<size_t>(b)) * blockBytes;
                    Rgba px[16];
                    etcBlock(src + (format == 13 ? 8 : 0), px);
                    if (format == 13) etcAlpha(src, px);
                    for (int i = 0; i < 16; ++i) {
                        const int x = tx * 8 + (b & 1) * 4 + (i & 3);
                        const int y = ty * 8 + (b >> 1) * 4 + (i >> 2);
                        if (x < w && y < h) put(img.rgba, w, x, y, px[i]);
                    }
                }
                continue;
            }
            for (int i = 0; i < 64; ++i) {
                const int x = tx * 8 + ((i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4));
                const int y = ty * 8 + (((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4));
                if (x >= w || y >= h) continue;
                put(img.rgba, w, x, y, texel(d, tile * 64 + static_cast<size_t>(i), format));
            }
        }
    }
    img.ok = true;
    return img;
}

int pow2AtLeast8(int v) {
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

}  // namespace

Image ctrTextureDecode(std::string_view in, int width, int height, int format) {
    if (format < 0 || format > 13) return fail("texture: unknown format");
    if (width < 1 || height < 1 || width > kMaxDim || height > kMaxDim) return fail("texture: size out of range (1..4096)");
    return decodeTiled(in, (width + 7) & ~7, (height + 7) & ~7, width, height, format);
}

Image bclimDecode(std::string_view in) {
    using namespace detail;
    constexpr size_t kFooter = 0x28;
    if (in.size() < kFooter) return fail("BCLIM: file too small");
    const std::string_view f = in.substr(in.size() - kFooter);
    if (f.substr(0, 4) != "CLIM" || f.substr(0x14, 4) != "imag") return fail("BCLIM: footer not found");
    uint32_t w = 0, h = 0;
    rd16(f, 0x1C, w);
    rd16(f, 0x1E, h);
    const uint8_t fmt = u8at(f, 0x20);
    const uint8_t swizzle = u8at(f, 0x21);
    static const int kMap[14] = {7, 8, 9, 5, 6, 3, 1, 2, 4, 0, 12, 13, 10, 11};
    if (fmt >= 14) return fail("BCLIM: unknown format");
    if (swizzle != 0) return fail("BCLIM: swizzled images are not supported");
    if (w < 1 || h < 1 || w > kMaxDim || h > kMaxDim) return fail("BCLIM: size out of range (1..4096)");
    const int sw = pow2AtLeast8(static_cast<int>(w)), sh = pow2AtLeast8(static_cast<int>(h));
    const std::string_view data = in.substr(0, in.size() - kFooter);
    if (fmt == 7) {
        // Gen 6 palette variant (pk3DS BCLIM.GetPixels / GetPixelsViaPalette): RGB5A1 data that starts
        // with u16 2 is u16 2, u16 colour count, count x u16 RGBA5551 colours, then indices in 8x8 Morton
        // tiles: one byte per pixel, or two 4-bit indices per byte (low nibble first) when count < 16.
        // Colours: pk3DS's reader decodes them as RGB565, but its writer (BCLIM.cs:229) and the footer format
        // (RGB5A1) say RGBA5551, so the alpha bit decides transparency and index 0 is not forced.
        // Extra safety over pk3DS: the palette and all indices must fit, else it is a normal image.
        const auto* d = reinterpret_cast<const uint8_t*>(data.data());
        if (data.size() >= 4 && (d[0] | (d[1] << 8)) == 2) {
            const size_t count = static_cast<size_t>(d[2] | (d[3] << 8));
            const bool half = count < 16;
            const size_t pixels = static_cast<size_t>(sw) * static_cast<size_t>(sh);
            const size_t idxBytes = half ? pixels / 2 : pixels;
            if (count >= 1 && 4 + 2 * count + idxBytes <= data.size()) {
                const uint8_t* pal = d + 4;
                const uint8_t* idx = pal + 2 * count;
                Image img;
                img.width = static_cast<int>(w);
                img.height = static_cast<int>(h);
                img.rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, '\0');
                for (size_t i = 0; i < pixels; ++i) {
                    const int tile = static_cast<int>(i / 64), j = static_cast<int>(i % 64);
                    const int x = (tile % (sw / 8)) * 8 + ((j & 1) | ((j >> 1) & 2) | ((j >> 2) & 4));
                    const int y = (tile / (sw / 8)) * 8 + (((j >> 1) & 1) | ((j >> 2) & 2) | ((j >> 3) & 4));
                    if (x >= static_cast<int>(w) || y >= static_cast<int>(h)) continue;
                    const uint8_t b = idx[half ? i / 2 : i];
                    const size_t c = half ? ((i & 1) ? (b >> 4) : (b & 15)) : b;
                    if (c >= count) return fail("BCLIM: palette index out of range");
                    put(img.rgba, static_cast<int>(w), x, y, texel(pal, c, 2));
                }
                img.ok = true;
                return img;
            }
        }
    }
    return decodeTiled(data, sw, sh, static_cast<int>(w), static_cast<int>(h), kMap[fmt]);
}

Image etc1Decode(std::string_view in, int width, int height, bool withAlpha) {
    if (width < 4 || height < 4 || width > kMaxDim || height > kMaxDim || (width & 3) || (height & 3))
        return fail("ETC1: size must be a multiple of 4 within 4..4096");
    const size_t blockBytes = withAlpha ? 16 : 8;
    const size_t bw = static_cast<size_t>(width / 4), bh = static_cast<size_t>(height / 4);
    if (in.size() < bw * bh * blockBytes) return fail("ETC1: data truncated");
    Image img;
    img.width = width;
    img.height = height;
    img.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, '\0');
    const auto* d = reinterpret_cast<const uint8_t*>(in.data());
    for (size_t by = 0; by < bh; ++by) {
        for (size_t bx = 0; bx < bw; ++bx) {
            const uint8_t* src = d + (by * bw + bx) * blockBytes;
            Rgba px[16];
            etcBlock(src + (withAlpha ? 8 : 0), px);
            if (withAlpha) etcAlpha(src, px);
            for (int i = 0; i < 16; ++i)
                put(img.rgba, width, static_cast<int>(bx) * 4 + (i & 3), static_cast<int>(by) * 4 + (i >> 2), px[i]);
        }
    }
    img.ok = true;
    return img;
}

Result pngEncode(const Image& img) {
    Result r;
    if (!img.ok || img.width < 1 || img.height < 1 ||
        img.rgba.size() != static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4) {
        r.error = "PNG: invalid image";
        return r;
    }
    QImage q(img.width, img.height, QImage::Format_RGBA8888);
    for (int y = 0; y < img.height; ++y)
        std::memcpy(q.scanLine(y), img.rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(img.width) * 4,
                    static_cast<size_t>(img.width) * 4);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    if (!q.save(&buf, "PNG")) {
        r.error = "PNG: encoding failed";
        return r;
    }
    r.data.assign(bytes.constData(), static_cast<size_t>(bytes.size()));
    r.ok = true;
    return r;
}

}  // namespace framebeam::formats
