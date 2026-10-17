// Host-Test für src/keyboard_layout.h (Bildschirmtastatur je Gastbelegung). Abhängigkeitsfrei,
// gebaut von tests/run-host-tests.sh.
//
//  1. Jede Taste beider Belegungen liegt in der Tastentabelle von DOSBox-Pure (sonst tot,
//     wie früher „<“ = RETROK_LESS) und hat für jedes Zeichen eine Glyphe der Pixelschrift
//  2. Keine Taste doppelt, Zeilenlängen wie im Raster vorgesehen
//  3. QWERTZ = Position: Z → US-Y-Taste, Y → US-Z-Taste, „<“ = Zusatztaste (OEM_102),
//     „-“ = US-„/“-Taste; QWERTY = Beschriftung gleich US-Zeichen
//  4. Belegungswahl aus dosbox_pure_keyboard_layout (gr/de → QWERTZ, sonst QWERTY)

#include "keyboard_layout.h"
#include "pixel_font.h"

#include <cctype>
#include <cstdio>
#include <set>
#include <string>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

// Abschrift der Tabelle keymap[] aus dosbox_pure_libretro.cpp (libretro/dosbox-pure, main):
// nur diese RETROK_* erreichen den Gast.
const std::set<int>& dosboxPureKeys() {
    static const std::set<int> keys = [] {
        std::set<int> k;
        for (int c = RETROK_0; c <= RETROK_9; ++c) k.insert(c);
        for (int c = RETROK_a; c <= RETROK_z; ++c) k.insert(c);
        for (int c = RETROK_F1; c <= RETROK_F12; ++c) k.insert(c);
        for (int c = RETROK_KP0; c <= RETROK_KP9; ++c) k.insert(c);
        for (int c : {RETROK_ESCAPE, RETROK_TAB, RETROK_BACKSPACE, RETROK_RETURN, RETROK_SPACE,
                      RETROK_LALT, RETROK_RALT, RETROK_LCTRL, RETROK_RCTRL, RETROK_LSHIFT,
                      RETROK_RSHIFT, RETROK_CAPSLOCK, RETROK_SCROLLOCK, RETROK_NUMLOCK,
                      RETROK_MINUS, RETROK_EQUALS, RETROK_BACKSLASH, RETROK_LEFTBRACKET,
                      RETROK_RIGHTBRACKET, RETROK_SEMICOLON, RETROK_QUOTE, RETROK_PERIOD,
                      RETROK_COMMA, RETROK_SLASH, RETROK_PRINT, RETROK_SYSREQ, RETROK_PAUSE,
                      RETROK_INSERT, RETROK_HOME, RETROK_PAGEUP, RETROK_PAGEDOWN, RETROK_DELETE,
                      RETROK_END, RETROK_LEFT, RETROK_UP, RETROK_DOWN, RETROK_RIGHT,
                      RETROK_KP_DIVIDE, RETROK_KP_MULTIPLY, RETROK_KP_MINUS, RETROK_KP_PLUS,
                      RETROK_KP_ENTER, RETROK_KP_PERIOD, RETROK_BACKQUOTE, RETROK_OEM_102})
            k.insert(c);
        return k;
    }();
    return keys;
}

int codeOf(bool german, const char* label) {
    const kblayout::Row* rows = kblayout::rows(german);
    for (int r = 0; r < kblayout::kRowCount; ++r)
        for (int i = 0; i < rows[r].count; ++i)
            if (std::string(rows[r].keys[i].label) == label) return rows[r].keys[i].code;
    return -1;
}

void testKeysReachGuest() {
    for (bool german : {false, true}) {
        const char* name = german ? "QWERTZ" : "QWERTY";
        const kblayout::Row* rows = kblayout::rows(german);
        std::set<int> seen;
        for (int r = 0; r < kblayout::kRowCount; ++r) {
            check(rows[r].count > 0 && rows[r].count <= 16, std::string(name) + ": Zeilenlänge");
            for (int i = 0; i < rows[r].count; ++i) {
                const kblayout::KeyDef& k = rows[r].keys[i];
                const std::string label = k.label;
                check(dosboxPureKeys().count(k.code) == 1,
                      std::string(name) + ": Taste '" + label + "' erreicht den Gast nicht");
                check(seen.insert(k.code).second, std::string(name) + ": doppelt: " + label);
                check(k.span > 0.0f, std::string(name) + ": Breite " + label);
                check(!label.empty(), std::string(name) + ": leere Beschriftung");
                for (char c : label)
                    check(pixelfont::glyphFor(c) != nullptr,
                          std::string(name) + ": keine Glyphe für '" + c + "' in " + label);
            }
        }
        // Atlas 16 × 8 Zellen (keyboard_overlay.cpp)
        check(seen.size() <= 128, std::string(name) + ": mehr Tasten als Atlaszellen");
    }
}

void testPositions() {
    // QWERTZ: Beschriftung = was ein deutscher Gast an dieser Position liefert.
    check(codeOf(true, "Z") == RETROK_y, "QWERTZ: Z liegt auf der US-Y-Taste");
    check(codeOf(true, "Y") == RETROK_z, "QWERTZ: Y liegt auf der US-Z-Taste");
    check(codeOf(true, "<") == RETROK_OEM_102, "QWERTZ: < = Zusatztaste links neben Y");
    check(codeOf(true, "-") == RETROK_SLASH, "QWERTZ: - liegt auf der US-/-Taste");
    check(codeOf(true, "SS") == RETROK_MINUS, "QWERTZ: ß liegt auf der US---Taste");
    check(codeOf(true, "UE") == RETROK_LEFTBRACKET, "QWERTZ: Ü");
    check(codeOf(true, "OE") == RETROK_SEMICOLON, "QWERTZ: Ö");
    check(codeOf(true, "AE") == RETROK_QUOTE, "QWERTZ: Ä");
    check(codeOf(true, "#") == RETROK_BACKSLASH, "QWERTZ: #");
    check(codeOf(true, "V") == RETROK_v, "QWERTZ: V ist der Buchstabe, nicht Pfeil runter");
    // QWERTY: Buchstaben und Zeichen wie beschriftet.
    for (char c = 'A'; c <= 'Z'; ++c) {
        const char label[2] = {c, 0};
        check(codeOf(false, label) == RETROK_a + (c - 'A'), std::string("QWERTY: ") + c);
    }
    check(codeOf(false, "/") == RETROK_SLASH, "QWERTY: /");
    check(codeOf(false, "-") == RETROK_MINUS, "QWERTY: -");
    check(codeOf(false, "[") == RETROK_LEFTBRACKET, "QWERTY: [");
    check(codeOf(false, "\\") == RETROK_BACKSLASH, "QWERTY: Backslash");
    // Pfeil runter: beide Belegungen, Beschriftung V, aber eigener Code (zweite „V“-Taste)
    for (bool german : {false, true}) {
        const kblayout::Row& bottom = kblayout::rows(german)[kblayout::kRowCount - 1];
        bool down = false;
        for (int i = 0; i < bottom.count; ++i) down |= bottom.keys[i].code == RETROK_DOWN;
        check(down, "Pfeil runter vorhanden");
    }
}

void testLayoutChoice() {
    check(kblayout::isGerman("gr"), "gr → QWERTZ");
    check(kblayout::isGerman("de"), "de → QWERTZ");
    check(!kblayout::isGerman("us"), "us → QWERTY");
    check(!kblayout::isGerman("uk"), "uk → QWERTY");
    check(!kblayout::isGerman(nullptr), "nicht gesetzt (Core-Standard us) → QWERTY");
    check(!kblayout::isGerman(""), "leer → QWERTY");
}

} // namespace

int main() {
    testKeysReachGuest();
    testPositions();
    testLayoutChoice();
    std::printf("test_keyboard_layout: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
