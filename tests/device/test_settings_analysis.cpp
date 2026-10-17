// Geräte-Test (Quest, arm64):
//  1. ViewSettings (src/vr_menu.cpp): Speichern/Laden rundtrip, Grenzen, kaputte Dateien,
//     Wölbungsradius (Bogenlänge = Bildbreite).
//  2. Frame-Analyse „Umgebung ausblenden“ (src/libretro_adapter.cpp, analyzeFrame):
//     synthetische Bilder mit schwarzem Rand, Himmel, Anzeige, Horizont, Boden.

#include <android/log.h>
#include <android_native_app_glue.h>
#include <GLES3/gl3.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "vr_menu.h"
#define private public   // analyzeFrame/mKey für den Test erreichbar
#include "libretro_adapter.h"
#undef private

namespace {
int gChecks = 0, gFailures = 0;
void check(bool ok, const char* what, double a = 0, double b = 0) {
    ++gChecks;
    if (!ok && ++gFailures <= 20) std::printf("FEHLER: %s (%.3f / %.3f)\n", what, a, b);
}

// Testbild: Rand oben/unten/links/rechts schwarz, Himmel, HUD-Ziffern im Himmel,
// Bergband bis zum Horizont, darunter Gras (2 Streifenfarben) mit grauer Straße.
std::vector<uint32_t> makeFrame(int w, int h, int top, int bottom, int left, int right,
                                uint32_t sky, int horizon, uint32_t grassA, uint32_t grassB) {
    std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0xFF000000u);
    for (int y = top; y < h - bottom; ++y)
        for (int x = left; x < w - right; ++x) {
            uint32_t c;
            if (y < horizon - 20) c = sky;
            else if (y < horizon) c = 0xFF708090u;                  // Berge
            else {
                const int half = (y - horizon) * (w / 2) / std::max(1, h - horizon);   // Straße wird breiter
                const bool road = std::abs(x - w / 2) < half / 2 + 4;
                c = road ? 0xFF606060u : (((y / 6) & 1) ? grassA : grassB);
            }
            px[static_cast<size_t>(y) * w + x] = c;
        }
    // HUD-Kästchen im Himmel (dürfen die Himmelsfarbe nicht verdrängen)
    for (int y = top + 4; y < top + 14; ++y)
        for (int x = left + 10; x < left + 40; ++x) px[static_cast<size_t>(y) * w + x] = 0xFFFFD000u;
    return px;
}
} // namespace

int main() {
    // --- 1. ViewSettings ----------------------------------------------------------
    const std::string path = "/data/local/tmp/xrshell-test-view.cfg";
    ViewSettings a;
    a.passthrough = 70; a.curve = 2; a.filter = 3; a.saturation = 130; a.contrast = 90;
    a.environment = 3; a.width = 2.6f; a.distance = 1.9f;
    check(a.save(path), "Einstellungen speichern");
    ViewSettings b;
    check(b.load(path), "Einstellungen laden");
    check(!(a != b), "Rundtrip identisch");

    // Werte außerhalb der Grenzen werden beim Laden begrenzt, Unbekanntes ignoriert.
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fputs("passthrough = 250\ncurve = 7\nsaturation = -5\ncontrast = 999\nunsinn = 3\n", f);
        std::fclose(f);
    }
    ViewSettings c;
    check(c.load(path), "grenzwertige Datei laden");
    check(c.passthrough == 100, "Passthrough begrenzt", c.passthrough, 100);
    check(c.curve >= 0 && c.curve < 4, "Wölbung gültig", c.curve, 0);
    check(c.saturation == 0, "Sättigung begrenzt", c.saturation, 0);
    check(c.contrast == 150, "Kontrast begrenzt", c.contrast, 150);
    ViewSettings d;
    check(!d.load("/data/local/tmp/gibt-es-nicht.cfg"), "fehlende Datei → false");
    std::remove(path.c_str());

    // Wölbungsradius: Bogenlänge r·θ = Breite; Stufe 0 = flach.
    const float degrees[] = { 0.0f, 30.0f, 60.0f, 90.0f };
    for (int level = 0; level < 4; ++level)
        for (float w : { 1.0f, 2.4f, 6.0f }) {
            ViewSettings v;
            v.curve = level;
            const float r = v.curveRadius(w);
            if (level == 0) check(r == 0.0f, "flach → Radius 0");
            else check(std::fabs(r * degrees[level] * 3.14159265f / 180.0f - w) < 1e-4f, "Bogenlänge = Breite", r, w);
        }

    // --- 2. Frame-Analyse -----------------------------------------------------------
    LibretroAdapter adapter;
    const uint32_t sky = 0xFFB0C8F0u, grassA = 0xFF30A040u, grassB = 0xFF208830u;
    struct Case { int w, h, top, bottom, left, right, horizon; };
    const Case cases[] = {
        { 256, 224, 8, 0, 0, 0, 120 },      // 256×224 mit Overscan-Zeilen oben
        { 256, 224, 0, 0, 0, 0, 140 },      // ohne Rand
        { 256, 239, 8, 7, 8, 0, 130 },      // PAL-Höhe, Rand oben/unten/links
        { 512, 448, 16, 0, 0, 0, 250 },     // Hires
    };
    for (const Case& cs : cases) {
        const auto px = makeFrame(cs.w, cs.h, cs.top, cs.bottom, cs.left, cs.right, sky, cs.horizon, grassA, grassB);
        adapter.analyzeFrame(px.data(), cs.w, cs.h);
        const EnvironmentKey key = adapter.environmentKey();
        check(key.valid, "Key erkannt");
        check((key.sky & 0xFFFFFF) == (sky & 0xFFFFFF), "Himmelsfarbe", key.sky & 0xFFFFFF, sky & 0xFFFFFF);
        check(static_cast<int>(key.border[0]) == std::min(cs.top, 16), "Rand oben", key.border[0], cs.top);
        check(static_cast<int>(key.border[1]) == std::min(cs.bottom, 16), "Rand unten", key.border[1], cs.bottom);
        check(static_cast<int>(key.border[2]) == std::min(cs.left, 16), "Rand links", key.border[2], cs.left);
        check(static_cast<int>(key.border[3]) == std::min(cs.right, 16), "Rand rechts", key.border[3], cs.right);
        check(std::abs(static_cast<int>(key.horizon) - cs.horizon) <= 2, "Horizont", key.horizon, cs.horizon);
        check(key.skyEnd <= key.horizon, "Himmelsband endet vor dem Horizont", key.skyEnd, key.horizon);
        bool greenA = false, greenB = false;
        for (int i = 0; i < key.groundCount; ++i) {
            greenA |= (key.ground[i] & 0xFFFFFF) == (grassA & 0xFFFFFF);
            greenB |= (key.ground[i] & 0xFFFFFF) == (grassB & 0xFFFFFF);
            check((key.ground[i] & 0xFFFFFF) != 0x606060u, "Straße nie Bodenfarbe");
        }
        check(greenA && greenB, "beide Grasfarben als Boden", key.groundCount, 2);
    }
    // Bild ohne erkennbaren Himmel (Rauschen) → kein Himmels-Key.
    std::vector<uint32_t> noise(256 * 224);
    uint32_t seed = 1;
    for (auto& p : noise) { seed = seed * 1664525u + 1013904223u; p = 0xFF000000u | (seed >> 8); }
    adapter.analyzeFrame(noise.data(), 256, 224);
    check(!adapter.environmentKey().valid, "Rauschen → kein Key");
    // Zu kleines Bild → kein Key, kein Absturz.
    adapter.analyzeFrame(noise.data(), 32, 32);
    check(!adapter.environmentKey().valid, "zu klein → kein Key");

    std::printf("test_settings_analysis: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
