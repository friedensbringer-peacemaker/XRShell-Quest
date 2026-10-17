// XRShell — Menü-Optik aus den Spieldateien (xrshell.theme im Spielordner).
//
// BYOA: Die Bilder stammen aus der eigenen Kopie des Spielers (z. B. von dessen
// CD nach C:\XRTHEME\ kopiert), nie aus dem APK. Ohne Datei bleibt die
// Standardoptik (Glas-Tafel). Aufbau, eine Angabe pro Zeile, `#` = Kommentar:
//
//   panel       = XRTHEME/PANEL.BMP     # Tafel-Hintergrund (auf die Tafel gestreckt)
//   title       = XRTHEME/TITLE.BMP     # Kopfzeile (gestreckt)
//   strip       = XRTHEME/STRIP.BMP     # Zierleiste unter der Kopfzeile
//   button      = XRTHEME/BUTTON.BMP    # Zeilen, Reiter, Knöpfe (dreiteilig gestreckt)
//   arrow_left  = XRTHEME/LEFT.BMP      # dazu arrow_left_pressed, arrow_right(_pressed)
//   text        = #000000               # Beschriftung auf den Knöpfen
//   text_dim    = #606060
//   title_text  = #FFFFFF
//   accent      = #E8B800C0             # aktiver Reiter (Tönung über dem Knopf)
//   hover       = #FFFFFF50             # Zeile unter dem Strahl
//   transparent = #FF00FF               # Schlüsselfarbe der Spielgrafik → durchsichtig
//   font        = XRTHEME/FONT.BMP      # optional: Bitmap-Schrift der Spielgrafik
//   font_cell   = 18x17                 # Zellengröße, Zellen zeilenweise von links oben
//   font_chars  = 0123456789ABC…        # Zeichen in Zellenreihenfolge (UTF-8, ohne Leerzeichen)
//   font_upper  = 1                     # Text vor dem Nachschlagen in Großbuchstaben
//
// Die Bitmap-Schrift wird ganzzahlig vergrößert (scharfe Pixel) und behält ihre
// Farben; gedimmter Text wird abgedunkelt. Breite je Zeichen = belegte Spalten der
// Zelle. Zeichen ohne Zelle zeichnet die Systemschrift in der Textfarbe.
//
// Pfade relativ zum Spielordner. Farben #RRGGBB oder #RRGGBBAA. Kommentar hinter
// einem Wert mit „ # “ (Leerzeichen davor und danach). Bilder: BMP.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ThemeImage {
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // RGBA8 wie der Menüpuffer (R | G<<8 | B<<16 | A<<24)
    bool ok() const { return w > 0 && h > 0; }
};

struct MenuTheme {
    bool active = false;
    ThemeImage panel, title, strip, button;
    ThemeImage arrowLeft, arrowLeftPressed, arrowRight, arrowRightPressed;
    uint32_t text = 0xFF000000u, textDim = 0xFF606060u, titleText = 0xFFFFFFFFu;
    uint32_t accent = 0xC000B8E8u, hover = 0x50FFFFFFu;

    // Bitmap-Schrift (optional, siehe oben).
    struct Glyph { uint32_t cp; int x, y, w; };   // Zelle links oben, belegte Breite
    ThemeImage font;
    int fontCellW = 0, fontCellH = 0;
    bool fontUpper = false;
    std::vector<Glyph> glyphs;
    bool hasFont() const { return !glyphs.empty(); }
    const Glyph* glyph(uint32_t cp) const;

    // Sucht xrshell.theme im Spielordner (bzw. neben der Spieldatei). false = keine
    // Datei oder kein Bild ladbar → Standardoptik.
    bool load(const std::string& gamePath);
};

// Nächster UTF-8-Codepunkt (s rückt vor); ungültige Folgen als einzelnes Byte.
uint32_t utf8Next(const char*& s);
// Großbuchstabe für ASCII und Latin-1 (ä → Ä, ß bleibt).
uint32_t latin1Upper(uint32_t cp);
