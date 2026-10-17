// XRShell — Belegung der Bildschirmtastatur (#5), passend zur Tastaturbelegung im Gast.
//
// DOSBox-Pure übersetzt RETROK_* rein nach Tastenposition (RETROK_y → Scancode der US-Y-Taste),
// die Belegung wählt erst das Gastsystem (dosbox_pure_keyboard_layout bzw. Windows-kbdgr.dll).
// Deshalb gibt es zwei Tabellen: QWERTZ für eine deutsche Gastbelegung (Beschriftung = was der
// Gast an dieser Position liefert), QWERTY für alle anderen (US-Standard des Cores).
// Abhängigkeitsfrei (Host-Test tests/test_keyboard_layout.cpp).

#pragma once

#include "libretro.h"

#include <cstring>

namespace kblayout {

struct KeyDef {
    const char* label;     // nur Zeichen der 3×5-Pixelschrift (pixel_font.h)
    enum retro_key code;
    float span;            // relative Breite (Leertaste = 5)
};

struct Row { const KeyDef* keys; int count; };
constexpr int kRowCount = 6;

// Gast-Belegung „gr“ (DOSBox-Pure-Wert für Deutschland) → QWERTZ.
inline bool isGerman(const char* dosboxPureLayout) {
    return dosboxPureLayout && (std::strcmp(dosboxPureLayout, "gr") == 0 ||
                                std::strcmp(dosboxPureLayout, "de") == 0);
}

inline const Row* rows(bool german) {
    static const KeyDef kFn[] = {
        {"ESC", RETROK_ESCAPE, 1.6f},
        {"F1", RETROK_F1, 1}, {"F2", RETROK_F2, 1}, {"F3", RETROK_F3, 1},
        {"F4", RETROK_F4, 1}, {"F5", RETROK_F5, 1}, {"F6", RETROK_F6, 1},
        {"F7", RETROK_F7, 1}, {"F8", RETROK_F8, 1}, {"F9", RETROK_F9, 1},
        {"F10", RETROK_F10, 1}, {"F11", RETROK_F11, 1}, {"F12", RETROK_F12, 1},
    };
    static const KeyDef kBottom[] = {
        {"SPACE", RETROK_SPACE, 5.0f}, {"ENTER", RETROK_RETURN, 1.8f},
        {"<", RETROK_LEFT, 1}, {"^", RETROK_UP, 1},
        {"V", RETROK_DOWN, 1}, {">", RETROK_RIGHT, 1},
    };

    // QWERTZ: Beschriftung = Zeichen des deutschen Gasts an dieser Position.
    static const KeyDef kDe1[] = {
        {"1", RETROK_1, 1}, {"2", RETROK_2, 1}, {"3", RETROK_3, 1},
        {"4", RETROK_4, 1}, {"5", RETROK_5, 1}, {"6", RETROK_6, 1},
        {"7", RETROK_7, 1}, {"8", RETROK_8, 1}, {"9", RETROK_9, 1},
        {"0", RETROK_0, 1}, {"SS", RETROK_MINUS, 1}, {"'", RETROK_EQUALS, 1},
    };
    static const KeyDef kDe2[] = {
        {"Q", RETROK_q, 1}, {"W", RETROK_w, 1}, {"E", RETROK_e, 1},
        {"R", RETROK_r, 1}, {"T", RETROK_t, 1}, {"Z", RETROK_y, 1},
        {"U", RETROK_u, 1}, {"I", RETROK_i, 1}, {"O", RETROK_o, 1},
        {"P", RETROK_p, 1}, {"UE", RETROK_LEFTBRACKET, 1}, {"+", RETROK_RIGHTBRACKET, 1},
    };
    static const KeyDef kDe3[] = {
        {"A", RETROK_a, 1}, {"S", RETROK_s, 1}, {"D", RETROK_d, 1},
        {"F", RETROK_f, 1}, {"G", RETROK_g, 1}, {"H", RETROK_h, 1},
        {"J", RETROK_j, 1}, {"K", RETROK_k, 1}, {"L", RETROK_l, 1},
        {"OE", RETROK_SEMICOLON, 1}, {"AE", RETROK_QUOTE, 1}, {"#", RETROK_BACKSLASH, 1},
    };
    static const KeyDef kDe4[] = {
        {"<", RETROK_OEM_102, 1}, {"Y", RETROK_z, 1}, {"X", RETROK_x, 1},
        {"C", RETROK_c, 1}, {"V", RETROK_v, 1}, {"B", RETROK_b, 1},
        {"N", RETROK_n, 1}, {"M", RETROK_m, 1}, {",", RETROK_COMMA, 1},
        {".", RETROK_PERIOD, 1}, {"-", RETROK_SLASH, 1},
    };

    // QWERTY (US-Standard des Cores): Beschriftung = US-Zeichen.
    static const KeyDef kUs1[] = {
        {"1", RETROK_1, 1}, {"2", RETROK_2, 1}, {"3", RETROK_3, 1},
        {"4", RETROK_4, 1}, {"5", RETROK_5, 1}, {"6", RETROK_6, 1},
        {"7", RETROK_7, 1}, {"8", RETROK_8, 1}, {"9", RETROK_9, 1},
        {"0", RETROK_0, 1}, {"-", RETROK_MINUS, 1}, {"=", RETROK_EQUALS, 1},
    };
    static const KeyDef kUs2[] = {
        {"Q", RETROK_q, 1}, {"W", RETROK_w, 1}, {"E", RETROK_e, 1},
        {"R", RETROK_r, 1}, {"T", RETROK_t, 1}, {"Y", RETROK_y, 1},
        {"U", RETROK_u, 1}, {"I", RETROK_i, 1}, {"O", RETROK_o, 1},
        {"P", RETROK_p, 1}, {"[", RETROK_LEFTBRACKET, 1}, {"]", RETROK_RIGHTBRACKET, 1},
    };
    static const KeyDef kUs3[] = {
        {"A", RETROK_a, 1}, {"S", RETROK_s, 1}, {"D", RETROK_d, 1},
        {"F", RETROK_f, 1}, {"G", RETROK_g, 1}, {"H", RETROK_h, 1},
        {"J", RETROK_j, 1}, {"K", RETROK_k, 1}, {"L", RETROK_l, 1},
        {";", RETROK_SEMICOLON, 1}, {"'", RETROK_QUOTE, 1}, {"\\", RETROK_BACKSLASH, 1},
    };
    static const KeyDef kUs4[] = {
        {"Z", RETROK_z, 1}, {"X", RETROK_x, 1}, {"C", RETROK_c, 1},
        {"V", RETROK_v, 1}, {"B", RETROK_b, 1}, {"N", RETROK_n, 1},
        {"M", RETROK_m, 1}, {",", RETROK_COMMA, 1}, {".", RETROK_PERIOD, 1},
        {"/", RETROK_SLASH, 1},
    };

    static const Row kDe[kRowCount] = {
        {kFn, 13}, {kDe1, 12}, {kDe2, 12}, {kDe3, 12}, {kDe4, 11}, {kBottom, 6},
    };
    static const Row kUs[kRowCount] = {
        {kFn, 13}, {kUs1, 12}, {kUs2, 12}, {kUs3, 12}, {kUs4, 10}, {kBottom, 6},
    };
    return german ? kDe : kUs;
}

} // namespace kblayout
