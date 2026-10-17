// XRShell — Pad-Modus: Quest-Controller → libretro-RetroPad (Tasten, Steuerkreuz, Analog).
//
// Abhängigkeitsfrei (nur <cmath>/<cstdint>), Host-Test: tests/test_pad_analog.cpp.
//
// Analog: Stick-Achse -1..1 → int16 -32767..32767 mit Totzone (Standard 0,15) und
// Neuskalierung — direkt hinter der Totzone beginnt der Wert bei 0, Vollausschlag
// ergibt 32767. OpenXR zählt Y nach vorn/oben positiv, libretro nach unten → Y
// wird invertiert. NaN und Werte über 1 werden abgefangen.
//
// Tasten: welche Quest-Taste welche RetroPad-Taste drückt, regelt pad_mapping.h (Belegung
// je System, pro Spiel änderbar). Steuerkreuz aus den Sticks (Hysterese) nur für die
// Achsen, die der Stick-Modus der Belegung vorsieht — Shooter bekommen die Sticks sonst
// rein analog, damit Steuerkreuz und Analogwert nicht doppelt wirken.

#pragma once

#include <cmath>
#include <cstdint>

namespace padmap {

constexpr float kAnalogDeadzone = 0.15f;
constexpr float kDpadPress = 0.5f, kDpadRelease = 0.35f;   // Hysterese Steuerkreuz
// Stick wirkt analog UND als Steuerkreuz (Quake/ECWolf: Menüs kennen nur das Steuerkreuz):
// Steuerkreuz erst fast am Anschlag, sonst addiert der Core beides (Quake: Tempo springt bei
// halbem Ausschlag von ~80 auf ~280) bzw. das Steuerkreuz überdeckt den Analogwert (ECWolf).
constexpr float kDpadPressHigh = 0.9f, kDpadReleaseHigh = 0.75f;

// RetroPad-IDs (Werte wie RETRO_DEVICE_ID_JOYPAD_* in libretro.h; hier ohne
// Abhängigkeit, der Host-Test prüft die Übereinstimmung).
enum RetroPadId : int8_t {
    kPadB = 0, kPadY = 1, kPadSelect = 2, kPadStart = 3, kPadUp = 4, kPadDown = 5,
    kPadLeft = 6, kPadRight = 7, kPadA = 8, kPadX = 9, kPadL = 10, kPadR = 11,
    kPadL2 = 12, kPadR2 = 13, kPadL3 = 14, kPadR3 = 15, kPadNone = -1
};

// Stick-Achsen, die zusätzlich das Steuerkreuz drücken.
enum StickDpad : unsigned {
    kDpadNone = 0, kDpadLeftX = 1, kDpadLeftY = 2, kDpadRightX = 4, kDpadRightY = 8,
    kDpadLeft = kDpadLeftX | kDpadLeftY, kDpadAll = 15
};

// Achse → libretro-Analogwert mit Totzone und Neuskalierung.
inline int16_t stickToAnalog(float v, float deadzone = kAnalogDeadzone) {
    if (!(v == v)) return 0;                       // NaN
    if (!(deadzone >= 0.0f) || deadzone >= 1.0f) deadzone = kAnalogDeadzone;
    const float a = std::fabs(v);
    if (a <= deadzone) return 0;
    float s = (a - deadzone) / (1.0f - deadzone);
    if (s > 1.0f) s = 1.0f;
    const long r = std::lround(s * 32767.0f);
    return static_cast<int16_t>(v < 0.0f ? -r : r);
}

struct Analog {
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// Sticks (OpenXR: +X rechts, +Y vorn) → libretro (+X rechts, +Y unten).
inline Analog sticksToAnalog(float lx, float ly, float rx, float ry,
                             float deadzone = kAnalogDeadzone) {
    Analog a;
    a.lx = stickToAnalog(lx, deadzone);
    a.ly = stickToAnalog(-ly, deadzone);
    a.rx = stickToAnalog(rx, deadzone);
    a.ry = stickToAnalog(-ry, deadzone);
    return a;
}

// Steuerkreuz aus den Sticks (nur die Achsen in `axes`, StickDpad-Bits) mit Hysterese.
// held[stick][links, rechts, hoch, runter] trägt den Zustand von Frame zu Frame.
// Achsen außerhalb von `axes` lassen ihren Zustand los. Achsen in `highAxes` schalten erst mit
// der hohen Schwelle (kDpadPressHigh/kDpadReleaseHigh).
inline uint16_t stickDpadMask(bool held[2][4], float lx, float ly, float rx, float ry,
                              unsigned axes, unsigned highAxes = 0) {
    const float sticks[2][2] = {{lx, ly}, {rx, ry}};
    const unsigned xBit[2] = {kDpadLeftX, kDpadRightX}, yBit[2] = {kDpadLeftY, kDpadRightY};
    const int8_t dirs[4] = {kPadLeft, kPadRight, kPadUp, kPadDown};
    uint16_t mask = 0;
    for (int s = 0; s < 2; ++s) {
        // OpenXR: +Y = vorn → Steuerkreuz hoch.
        const float x = sticks[s][0] == sticks[s][0] ? sticks[s][0] : 0.0f;
        const float y = sticks[s][1] == sticks[s][1] ? sticks[s][1] : 0.0f;
        const float axis[4] = {-x, x, y, -y};
        for (int i = 0; i < 4; ++i) {
            const unsigned bit = i < 2 ? xBit[s] : yBit[s];
            const bool enabled = (axes & bit) != 0;
            const bool high = (highAxes & bit) != 0;
            bool& h = held[s][i];
            h = enabled && (h ? axis[i] > (high ? kDpadReleaseHigh : kDpadRelease)
                              : axis[i] > (high ? kDpadPressHigh : kDpadPress));
            if (h) mask = static_cast<uint16_t>(mask | (1u << dirs[i]));
        }
    }
    return mask;
}

} // namespace padmap
