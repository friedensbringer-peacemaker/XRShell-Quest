// XRShell — geglättete Menüschrift aus der System-TTF des Geräts (stb_truetype).
//
// Keine Schriftdatei im Repo: Android/Quest bringt Roboto bzw. DroidSans unter
// /system/fonts mit. Fehlt beides, meldet load() false und das Menü nutzt die
// eingebettete 3×5-Pixelschrift. Text ist UTF-8 (Umlaute, ß).

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class TextRaster {
public:
    TextRaster();
    ~TextRaster();

    bool load(bool bold = false); // System-TTF suchen und öffnen (bold: fetter Schnitt)
    bool ok() const { return mOk; }

    // Breite in Pixeln bei Schriftgröße px (Zeilenhöhe).
    int width(const char* utf8, float px) const;
    // Text in einen RGBA-Puffer (R,G,B,A je Byte) mischen; y = Oberkante der Zeile.
    void draw(uint32_t* pixels, int texW, int texH, const char* utf8,
              int x, int y, float px, uint32_t color) const;

private:
    struct Font;
    std::unique_ptr<Font> mFont;
    std::vector<unsigned char> mData;
    bool mOk = false;
    struct Glyph { int w = 0, h = 0, xoff = 0, yoff = 0; std::vector<unsigned char> pixels; };
    const Glyph& glyphBitmap(int glyph, float scale, int quarter) const;
    mutable std::unordered_map<uint64_t, Glyph> mGlyphs;   // Raster-Cache (Menü zeichnet oft neu)
    // Zeichen → Glyph + Vorschub (unskaliert) und Unterschneidung je Glyphenpaar: stb_truetype
    // sucht beides sonst bei jedem Zeichen in den Tabellen der Schrift (Breiten messen das oft).
    struct Codepoint { int glyph = 0, advance = 0; };
    const Codepoint& codepoint(uint32_t cp) const;
    int kern(int a, int b) const;
    mutable std::unordered_map<uint32_t, Codepoint> mCodepoints;
    mutable std::unordered_map<uint64_t, int> mKerning;
};
