// XRShell — Menü-Optik aus den Spieldateien (siehe menu_theme.h).

#include "menu_theme.h"

#include "stb_image.h"

#include <android/log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n\"";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

// #RRGGBB oder #RRGGBBAA → R | G<<8 | B<<16 | A<<24 (ohne Alpha: deckend).
bool parseColor(const std::string& v, uint32_t& out) {
    if (v.size() != 7 && v.size() != 9) return false;
    if (v[0] != '#') return false;
    unsigned long n = 0;
    char* end = nullptr;
    n = std::strtoul(v.c_str() + 1, &end, 16);
    if (!end || *end) return false;
    const uint32_t a = v.size() == 9 ? static_cast<uint32_t>(n & 0xFF) : 0xFF;
    const uint32_t rgb = static_cast<uint32_t>(v.size() == 9 ? n >> 8 : n);
    out = ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | (a << 24);
    return true;
}

bool loadImage(const std::string& path, uint32_t keyColor, ThemeImage& img) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        LOGE("Menü-Optik: Bild nicht ladbar: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }
    img.w = w;
    img.h = h;
    img.px.resize(static_cast<size_t>(w) * h);
    std::memcpy(img.px.data(), data, img.px.size() * 4);
    stbi_image_free(data);
    for (uint32_t& p : img.px)
        if ((p & 0x00FFFFFFu) == (keyColor & 0x00FFFFFFu)) p = 0;   // Schlüsselfarbe → durchsichtig
    return true;
}

} // namespace

uint32_t utf8Next(const char*& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    uint32_t cp = p[0];
    int len = 1;
    if (cp >= 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        cp = ((cp & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        len = 4;
    } else if (cp >= 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((cp & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        len = 3;
    } else if (cp >= 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = ((cp & 0x1F) << 6) | (p[1] & 0x3F);
        len = 2;
    }
    s += len;
    return cp;
}

uint32_t latin1Upper(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
    return cp;
}

const MenuTheme::Glyph* MenuTheme::glyph(uint32_t cp) const {
    for (const Glyph& g : glyphs)
        if (g.cp == cp) return &g;
    return nullptr;
}

bool MenuTheme::load(const std::string& gamePath) {
    *this = MenuTheme();
    struct stat st {};
    std::string dir = gamePath;
    if (::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) dir = dir.substr(0, dir.rfind('/'));
    const std::string path = dir + "/xrshell.theme";
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;

    std::vector<std::pair<std::string, std::string>> entries;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (trim(s).rfind('#', 0) == 0) continue;         // Kommentarzeile
        const size_t hash = s.find(" # ");                // Kommentar hinter dem Wert
        if (hash != std::string::npos) s.erase(hash);     // (Farben #RRGGBB bleiben)
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(s.substr(0, eq)), value = trim(s.substr(eq + 1));
        if (!key.empty() && !value.empty()) entries.emplace_back(key, value);
    }
    std::fclose(f);

    uint32_t keyColor = 0xFFFF00FFu;   // Magenta — üblich in Spielgrafik
    for (const auto& [key, value] : entries)
        if (key == "transparent") parseColor(value, keyColor);

    int images = 0;
    for (const auto& [key, value] : entries) {
        ThemeImage* img = key == "panel" ? &panel : key == "title" ? &title
                        : key == "strip" ? &strip : key == "button" ? &button
                        : key == "arrow_left" ? &arrowLeft : key == "arrow_left_pressed" ? &arrowLeftPressed
                        : key == "arrow_right" ? &arrowRight : key == "arrow_right_pressed" ? &arrowRightPressed
                        : nullptr;
        if (img) {
            if (loadImage(dir + "/" + value, keyColor, *img)) ++images;
            continue;
        }
        uint32_t* color = key == "text" ? &text : key == "text_dim" ? &textDim
                        : key == "title_text" ? &titleText : key == "accent" ? &accent
                        : key == "hover" ? &hover : nullptr;
        if (color && !parseColor(value, *color)) LOGE("Menü-Optik: Farbe ungültig: %s = %s", key.c_str(), value.c_str());
        if (key == "font_cell") std::sscanf(value.c_str(), "%dx%d", &fontCellW, &fontCellH);
        else if (key == "font_upper") fontUpper = value != "0";
    }

    // Bitmap-Schrift: Zellen zeilenweise, belegte Breite = rechteste deckende Spalte.
    std::string fontFile, fontChars;
    for (const auto& [key, value] : entries) {
        if (key == "font") fontFile = value;
        else if (key == "font_chars") fontChars = value;
    }
    if (!fontFile.empty() && fontCellW > 0 && fontCellH > 0 &&
        loadImage(dir + "/" + fontFile, keyColor, font) && font.w >= fontCellW) {
        const int cols = font.w / fontCellW;
        int cell = 0;
        for (const char* s = fontChars.c_str(); *s; ++cell) {
            const uint32_t cp = utf8Next(s);
            const int cx = (cell % cols) * fontCellW, cy = (cell / cols) * fontCellH;
            if (cy + fontCellH > font.h) break;
            int w = 0;
            for (int y = 0; y < fontCellH; ++y)
                for (int x = 0; x < fontCellW; ++x)
                    if (font.px[static_cast<size_t>(cy + y) * font.w + cx + x] >> 24) w = std::max(w, x + 1);
            if (w > 0) glyphs.push_back({ cp, cx, cy, w });
        }
        LOGI("Menü-Schrift: %zu Zeichen, Zelle %dx%d", glyphs.size(), fontCellW, fontCellH);
    }
    active = images > 0 && panel.ok();
    LOGI("Menü-Optik aus %s: %d Bilder%s", path.c_str(), images, active ? "" : " — ohne Tafelbild, Standardoptik");
    return active;
}
