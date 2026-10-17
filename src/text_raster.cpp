// XRShell — geglättete Menüschrift, Implementierung. Siehe text_raster.h.

#include "text_raster.h"

#include "stb_truetype.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

struct TextRaster::Font {
    stbtt_fontinfo info;
};

namespace {

// Nächster Unicode-Codepunkt aus UTF-8; ungültige Bytes werden übersprungen.
uint32_t nextCodepoint(const char*& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    uint32_t cp = *p++;
    int extra = 0;
    if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
    else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
    else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
    for (; extra > 0 && (*p & 0xC0) == 0x80; --extra) cp = (cp << 6) | (*p++ & 0x3F);
    s = reinterpret_cast<const char*>(p);
    return cp;
}

} // namespace

TextRaster::TextRaster() = default;
TextRaster::~TextRaster() = default;

bool TextRaster::load(bool bold) {
    static const char* const kRegular[] = {
#ifdef XRSHELL_HOST_PREVIEW
        // Nur Desktop-Vorschau (tests/host/menu_preview.cpp): Systemschrift des Rechners.
        "C:/Windows/Fonts/segoeui.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
        "/system/fonts/RobotoStatic-Regular.ttf",
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
    };
    static const char* const kBold[] = {
#ifdef XRSHELL_HOST_PREVIEW
        "C:/Windows/Fonts/segoeuib.ttf", "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
#endif
        "/system/fonts/RobotoStatic-Bold.ttf",
        "/system/fonts/DroidSans-Bold.ttf",
        "/system/fonts/RobotoStatic-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
    };
    std::vector<const char*> fonts =
        bold ? std::vector<const char*>(std::begin(kBold), std::end(kBold))
             : std::vector<const char*>(std::begin(kRegular), std::end(kRegular));
    // Host-Vorschau (tests/run-menu-preview.sh): Schrift des Entwicklungsrechners.
    if (const char* own = std::getenv(bold ? "XRSHELL_FONT_BOLD" : "XRSHELL_FONT")) fonts.insert(fonts.begin(), own);
    for (const char* path : fonts) {
        FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        mData.resize(size > 0 ? static_cast<size_t>(size) : 0);
        const bool read = size > 0 && std::fread(mData.data(), 1, mData.size(), f) == mData.size();
        std::fclose(f);
        if (!read) continue;
        mFont = std::make_unique<Font>();
        if (stbtt_InitFont(&mFont->info, mData.data(),
                           stbtt_GetFontOffsetForIndex(mData.data(), 0))) {
            mOk = true;
            LOGI("Menüschrift: %s", path);
            return true;
        }
    }
    LOGI("Keine System-TTF gefunden — Menü nutzt die Pixelschrift");
    mFont.reset();
    mData.clear();
    return false;
}

const TextRaster::Codepoint& TextRaster::codepoint(uint32_t cp) const {
    auto it = mCodepoints.find(cp);
    if (it != mCodepoints.end()) return it->second;
    Codepoint c;
    c.glyph = stbtt_FindGlyphIndex(&mFont->info, static_cast<int>(cp));
    int lsb = 0;
    stbtt_GetGlyphHMetrics(&mFont->info, c.glyph, &c.advance, &lsb);
    return mCodepoints.emplace(cp, c).first->second;
}

int TextRaster::kern(int a, int b) const {
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
    auto it = mKerning.find(key);
    if (it != mKerning.end()) return it->second;
    return mKerning.emplace(key, stbtt_GetGlyphKernAdvance(&mFont->info, a, b)).first->second;
}

int TextRaster::width(const char* utf8, float px) const {
    if (!mOk) return 0;
    const float scale = stbtt_ScaleForPixelHeight(&mFont->info, px);
    float x = 0.0f;
    int prev = 0;
    for (const char* s = utf8; *s;) {
        const Codepoint& c = codepoint(nextCodepoint(s));
        if (prev) x += scale * kern(prev, c.glyph);
        x += scale * c.advance;
        prev = c.glyph;
    }
    return static_cast<int>(std::ceil(x));
}

void TextRaster::draw(uint32_t* pixels, int texW, int texH, const char* utf8,
                      int x, int y, float px, uint32_t color) const {
    if (!mOk) return;
    const float scale = stbtt_ScaleForPixelHeight(&mFont->info, px);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&mFont->info, &ascent, &descent, &gap);
    const int baseline = y + static_cast<int>(std::lround(ascent * scale));
    const uint32_t cr = color & 0xFF, cg = (color >> 8) & 0xFF, cb = (color >> 16) & 0xFF;
    const uint32_t ca = (color >> 24) & 0xFF;

    float pen = static_cast<float>(x);
    int prev = 0;
    for (const char* s = utf8; *s;) {
        const Codepoint& cpInfo = codepoint(nextCodepoint(s));
        const int glyph = cpInfo.glyph, advance = cpInfo.advance;
        if (prev) pen += scale * kern(prev, glyph);

        // Viertelpixel-Versatz reicht optisch; so lässt sich die Glyphe zwischenspeichern.
        const int quarter = static_cast<int>((pen - std::floor(pen)) * 4.0f) & 3;
        const Glyph& cached = glyphBitmap(glyph, scale, quarter);
        const int w = cached.w, h = cached.h, xoff = cached.xoff, yoff = cached.yoff;
        const unsigned char* bitmap = cached.pixels.data();
        const int gx = static_cast<int>(std::floor(pen)) + xoff, gy = baseline + yoff;
        for (int row = 0; row < h; ++row) {
            const int ty = gy + row;
            if (ty < 0 || ty >= texH) continue;
            for (int col = 0; col < w; ++col) {
                const int tx = gx + col;
                if (tx < 0 || tx >= texW) continue;
                const uint32_t cov = bitmap[row * w + col] * ca / 255;
                if (!cov) continue;
                uint32_t& dst = pixels[ty * texW + tx];
                const uint32_t inv = 255 - cov;
                const uint32_t r = (cr * cov + (dst & 0xFF) * inv) / 255;
                const uint32_t g = (cg * cov + ((dst >> 8) & 0xFF) * inv) / 255;
                const uint32_t b = (cb * cov + ((dst >> 16) & 0xFF) * inv) / 255;
                const uint32_t a = std::max(cov, (dst >> 24) & 0xFF);
                dst = r | (g << 8) | (b << 16) | (a << 24);
            }
        }
        pen += scale * advance;
        prev = glyph;
    }
}

// Gerasterte Glyphe aus dem Cache (Schlüssel: Glyphe, Größe, Viertelpixel-Versatz).
const TextRaster::Glyph& TextRaster::glyphBitmap(int glyph, float scale, int quarter) const {
    const uint64_t key = (static_cast<uint64_t>(glyph) << 32) |
                         (static_cast<uint64_t>(std::lround(scale * 100000.0f)) << 2) |
                         static_cast<uint64_t>(quarter);
    auto it = mGlyphs.find(key);
    if (it != mGlyphs.end()) return it->second;
    Glyph g;
    unsigned char* bitmap = stbtt_GetGlyphBitmapSubpixel(&mFont->info, scale, scale,
                                                         static_cast<float>(quarter) * 0.25f, 0.0f,
                                                         glyph, &g.w, &g.h, &g.xoff, &g.yoff);
    if (bitmap) {
        g.pixels.assign(bitmap, bitmap + static_cast<size_t>(g.w) * g.h);
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    return mGlyphs.emplace(key, std::move(g)).first->second;
}
