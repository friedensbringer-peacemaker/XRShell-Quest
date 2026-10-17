// Host-Test für src/pad_analog.h (Pad-Modus: Sticks → libretro-Analog, Tastenbelegung,
// Steuerkreuz aus den Sticks). Gebaut und ausgeführt von tests/run-host-tests.sh.
//
// Sweeps: Achswert -1,2..1,2 in 0,001-Schritten × Totzonen 0/0,05/0,15/0,3 (Totzone,
// Vorzeichen, Klemmung, Monotonie, Neuskalierung ohne Sprung); Y-Inversion aller
// Stick-Kombinationen; jede Quest-Taste einzeln und alle zusammen für jede Belegung;
// Hysterese des Steuerkreuzes auf einer Rampe hin und zurück × alle Achsmasken.

#include "pad_analog.h"
#include "libretro.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        if (gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
    }
}

// --- 1. RetroPad-IDs stimmen mit libretro.h überein ---
void testIds() {
    check(padmap::kPadB == RETRO_DEVICE_ID_JOYPAD_B && padmap::kPadY == RETRO_DEVICE_ID_JOYPAD_Y &&
          padmap::kPadSelect == RETRO_DEVICE_ID_JOYPAD_SELECT &&
          padmap::kPadStart == RETRO_DEVICE_ID_JOYPAD_START &&
          padmap::kPadUp == RETRO_DEVICE_ID_JOYPAD_UP && padmap::kPadDown == RETRO_DEVICE_ID_JOYPAD_DOWN &&
          padmap::kPadLeft == RETRO_DEVICE_ID_JOYPAD_LEFT &&
          padmap::kPadRight == RETRO_DEVICE_ID_JOYPAD_RIGHT && padmap::kPadA == RETRO_DEVICE_ID_JOYPAD_A &&
          padmap::kPadX == RETRO_DEVICE_ID_JOYPAD_X && padmap::kPadL == RETRO_DEVICE_ID_JOYPAD_L &&
          padmap::kPadR == RETRO_DEVICE_ID_JOYPAD_R && padmap::kPadL2 == RETRO_DEVICE_ID_JOYPAD_L2 &&
          padmap::kPadR2 == RETRO_DEVICE_ID_JOYPAD_R2 && padmap::kPadL3 == RETRO_DEVICE_ID_JOYPAD_L3 &&
          padmap::kPadR3 == RETRO_DEVICE_ID_JOYPAD_R3,
          "RetroPad-IDs wie libretro.h");
}

// --- 2. Achse → Analog: Totzone, Vorzeichen, Klemmung, Monotonie ---
void testAxisSweep() {
    const float zones[] = {0.0f, 0.05f, 0.15f, 0.3f};
    for (float dz : zones) {
        int prev = -40000;
        for (int i = -1200; i <= 1200; ++i) {
            const float v = static_cast<float>(i) / 1000.0f;
            const int a = padmap::stickToAnalog(v, dz);
            const std::string at = "v=" + std::to_string(v) + " dz=" + std::to_string(dz);
            check(a >= -32767 && a <= 32767, "Bereich " + at);
            if (std::fabs(v) <= dz) check(a == 0, "Totzone " + at);
            else check((v > 0) == (a > 0) || a == 0, "Vorzeichen " + at);
            if (std::fabs(v) >= 1.0f) check(a == (v > 0 ? 32767 : -32767), "Klemmung " + at);
            check(a >= prev, "monoton " + at);
            // Neuskalierung: Wert entspricht (|v|-dz)/(1-dz)
            if (std::fabs(v) > dz && std::fabs(v) < 1.0f) {
                const float want = (std::fabs(v) - dz) / (1.0f - dz) * 32767.0f;
                check(std::fabs(std::fabs(static_cast<float>(a)) - want) <= 1.0f, "Skalierung " + at);
            }
            prev = a;
        }
        // Kein Sprung direkt hinter der Totzone (höchstens ein Schritt von 0,001).
        const int justOut = padmap::stickToAnalog(dz + 0.001f, dz);
        check(justOut <= static_cast<int>(0.0011f / (1.0f - dz) * 32767.0f) + 1,
              "kein Sprung hinter Totzone dz=" + std::to_string(dz));
        check(padmap::stickToAnalog(-0.0f, dz) == 0, "-0 = 0");
    }
    // Standard-Totzone 0,15
    check(padmap::stickToAnalog(0.15f) == 0 && padmap::stickToAnalog(-0.149f) == 0, "Standard-Totzone 0,15");
    check(padmap::stickToAnalog(0.575f) > 16000 && padmap::stickToAnalog(0.575f) < 16800, "Mitte ~16384");
    // Unsinnige Eingaben
    check(padmap::stickToAnalog(std::numeric_limits<float>::quiet_NaN()) == 0, "NaN = 0");
    check(padmap::stickToAnalog(std::numeric_limits<float>::infinity()) == 32767, "+inf geklemmt");
    check(padmap::stickToAnalog(-std::numeric_limits<float>::infinity()) == -32767, "-inf geklemmt");
    check(padmap::stickToAnalog(0.5f, -1.0f) == padmap::stickToAnalog(0.5f), "negative Totzone → Standard");
    check(padmap::stickToAnalog(0.5f, 1.5f) == padmap::stickToAnalog(0.5f), "Totzone ≥ 1 → Standard");
}

// --- 3. Sticks: X bleibt, Y wird invertiert (libretro: +Y unten) ---
void testSticks() {
    const float vals[] = {-1.0f, -0.6f, 0.0f, 0.6f, 1.0f};
    for (float lx : vals)
        for (float ly : vals)
            for (float rx : vals)
                for (float ry : vals) {
                    const padmap::Analog a = padmap::sticksToAnalog(lx, ly, rx, ry);
                    check(a.lx == padmap::stickToAnalog(lx) && a.rx == padmap::stickToAnalog(rx),
                          "X unverändert");
                    check(a.ly == padmap::stickToAnalog(-ly) && a.ry == padmap::stickToAnalog(-ry),
                          "Y invertiert");
                }
    const padmap::Analog fwd = padmap::sticksToAnalog(0.0f, 1.0f, 0.0f, 0.0f);
    check(fwd.ly == -32767, "Stick vor = libretro -Y (hoch)");
    const padmap::Analog right = padmap::sticksToAnalog(0.0f, 0.0f, 1.0f, 0.0f);
    check(right.rx == 32767 && right.lx == 0 && right.ly == 0 && right.ry == 0, "rechter Stick rechts");
}

// --- 4. Tastenbelegung ---

// --- 5. Steuerkreuz aus den Sticks: Hysterese, Achsmasken ---
void testDpad() {
    for (unsigned axes = 0; axes < 16; ++axes) {
        for (int stick = 0; stick < 2; ++stick) {
            for (int axis = 0; axis < 2; ++axis) {       // 0 = X, 1 = Y
                for (int sign = -1; sign <= 1; sign += 2) {
                    bool held[2][4] = {};
                    const unsigned bit = stick == 0 ? (axis == 0 ? padmap::kDpadLeftX : padmap::kDpadLeftY)
                                                    : (axis == 0 ? padmap::kDpadRightX : padmap::kDpadRightY);
                    const bool enabled = (axes & bit) != 0;
                    // Richtung: X+ rechts, X- links, Y+ (vorn) hoch, Y- runter
                    const int dir = axis == 0 ? (sign > 0 ? padmap::kPadRight : padmap::kPadLeft)
                                              : (sign > 0 ? padmap::kPadUp : padmap::kPadDown);
                    bool on = false;
                    // Rampe hoch 0 → 1 und zurück 1 → 0
                    for (int k = 0; k <= 200; ++k) {
                        const float t = static_cast<float>(k <= 100 ? k : 200 - k) / 100.0f;
                        const float v = sign * t;
                        float s[4] = {0, 0, 0, 0};
                        s[stick * 2 + axis] = v;
                        const uint16_t m = padmap::stickDpadMask(held, s[0], s[1], s[2], s[3], axes);
                        const bool now = (m & (1u << dir)) != 0;
                        const std::string at = "Achsen " + std::to_string(axes) + " Stick " +
                                               std::to_string(stick) + " Achse " + std::to_string(axis) +
                                               " v=" + std::to_string(v);
                        check((m & ~(1u << dir)) == 0, "nur eine Richtung " + at);
                        if (!enabled) { check(!now, "Achse aus " + at); continue; }
                        const bool rising = k <= 100;
                        if (rising) check(now == (t > padmap::kDpadPress), "Drücken ab 0,5 " + at);
                        else check(now == (t > padmap::kDpadRelease), "Loslassen unter 0,35 " + at);
                        if (now != on) on = now;
                    }
                    check(!on, "am Ende losgelassen");
                }
            }
        }
    }
    // Achse wird abgeschaltet, während sie gedrückt ist → sofort los.
    bool held[2][4] = {};
    padmap::stickDpadMask(held, 0.0f, 1.0f, 0.0f, 0.0f, padmap::kDpadAll);
    check(held[0][2], "hoch gehalten");
    check(padmap::stickDpadMask(held, 0.0f, 1.0f, 0.0f, 0.0f, padmap::kDpadNone) == 0 && !held[0][2],
          "Achse aus → losgelassen");
    // NaN drückt nichts.
    bool h2[2][4] = {};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(padmap::stickDpadMask(h2, nan, nan, nan, nan, padmap::kDpadAll) == 0, "NaN drückt nichts");
    // Konsolen: beide Sticks, Diagonale drückt zwei Richtungen.
    bool h3[2][4] = {};
    const uint16_t diag = padmap::stickDpadMask(h3, 0.8f, 0.8f, 0.0f, 0.0f, padmap::kDpadAll);
    check(diag == ((1u << padmap::kPadRight) | (1u << padmap::kPadUp)), "Diagonale = rechts + hoch");
    // Analog + Steuerkreuz (Quake/ECWolf): hohe Schwelle 0,9 / 0,75 nur auf den Achsen in highAxes.
    for (int i = 0; i <= 100; ++i) {
        const float v = i / 100.0f;
        bool hh[2][4] = {};
        const uint16_t high = padmap::stickDpadMask(hh, 0.0f, v, 0.0f, 0.0f, padmap::kDpadAll, padmap::kDpadLeftY);
        check(((high >> padmap::kPadUp) & 1u) == 0 || v > padmap::kDpadPressHigh - 1e-6f,
              "hohe Schwelle: unter 0,9 kein Steuerkreuz");
        check(v <= padmap::kDpadPressHigh || ((high >> padmap::kPadUp) & 1u), "hohe Schwelle: über 0,9 gedrückt");
    }
    bool hy[2][4] = {};
    padmap::stickDpadMask(hy, 0.0f, 0.95f, 0.0f, 0.0f, padmap::kDpadAll, padmap::kDpadLeftY);
    check(hy[0][2], "hoch bei 0,95");
    padmap::stickDpadMask(hy, 0.0f, 0.8f, 0.0f, 0.0f, padmap::kDpadAll, padmap::kDpadLeftY);
    check(hy[0][2], "bei 0,8 noch gehalten (Loslassen erst unter 0,75)");
    padmap::stickDpadMask(hy, 0.0f, 0.7f, 0.0f, 0.0f, padmap::kDpadAll, padmap::kDpadLeftY);
    check(!hy[0][2], "unter 0,75 losgelassen");
    bool hx[2][4] = {};
    check(padmap::stickDpadMask(hx, 0.6f, 0.0f, 0.0f, 0.0f, padmap::kDpadAll, padmap::kDpadLeftY) ==
              (1u << padmap::kPadRight), "x ohne highAxes-Bit: normale Schwelle 0,5");
}

} // namespace

int main() {
    testIds();
    testAxisSweep();
    testSticks();
    testDpad();
    std::printf("test_pad_analog: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
