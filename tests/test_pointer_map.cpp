// Host-Test für src/pointer_map.h (Laser → Spielzeiger) und src/screen_filter.h.
// Gebaut und ausgeführt von tests/run-host-tests.sh.
//
// Sweeps:
//  - Frame-UV 0..1 in 1/4096-Schritten × Mausbereich (max_x 199…1279, Textmodus 640):
//    Rückweg wie DOSBox-Pure „direct“ trifft die Laserposition auf < 0,02 Mauspixel,
//    der Spielpixel unter dem Laser weicht höchstens 1 Pixel ab (max_x = 639 statt 640).
//  - Ränder, Klemmen außerhalb 0..1, Monotonie, (0, 0) wird nie gesendet.
//  - Bildzoom 1×/1,5×/2×/3× × Zoom-Mitte über das ganze Bild: Ausschnitt bleibt im Frame und
//    entspricht der Shader-Abbildung (uUvScale/zoom, uUvOffset).
//  - Filternamen, Zahlen, Unbekanntes; Rückfall ohne HD-Programm.

#include "pointer_map.h"
#include "screen_filter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        if (gFailures <= 20) std::printf("FEHLER: %s\n", what.c_str());
    }
}

void testRoundTrip() {
    const int ranges[] = {199, 239, 319, 399, 479, 639, 767, 1023, 1279};
    for (int maxX : ranges) {
        double worst = 0.0;
        int prev = -40000;
        for (int k = 0; k <= 4096; ++k) {
            const float uv = static_cast<float>(k) / 4096.0f;
            const int16_t p = uvToPointer(uv);
            check(p >= prev, "monoton bei max_x " + std::to_string(maxX));
            prev = p;
            const double mouse = static_cast<double>(dosboxPureAbs(p)) * maxX;   // mouse.x = x · max_x
            worst = std::max(worst, std::fabs(mouse - static_cast<double>(uv) * maxX));
        }
        check(worst < 0.02, "Rundweg max_x " + std::to_string(maxX) + ": " + std::to_string(worst) + " px");
    }
    // Spielpixel unter dem Laser: 320×200-Spiel mit DOS-Standardbereich 640×200 (x halbiert).
    for (int k = 0; k < 3200; ++k) {
        const float uv = (static_cast<float>(k) + 0.5f) / 3200.0f;
        const int game = static_cast<int>(dosboxPureAbs(uvToPointer(uv)) * 639.0f) / 2;
        const int want = static_cast<int>(uv * 320.0f);
        check(std::abs(game - want) <= 1, "Spielpixel 320 bei uv " + std::to_string(uv));
    }
    // Textmodus: abs · Spalten · 8 (80 Spalten = 640).
    for (int k = 0; k <= 80; ++k) {
        const float uv = static_cast<float>(k) / 80.0f;
        const double x = static_cast<double>(dosboxPureAbs(uvToPointer(uv))) * 640.0;
        check(std::fabs(x - k * 8.0) < 0.02, "Textmodus Spalte " + std::to_string(k));
    }
}

void testEdges() {
    check(uvToPointer(0.0f) == -32767, "linker Rand −32767");
    check(uvToPointer(1.0f) == 32767, "rechter Rand 32767");
    check(uvToPointer(-0.3f) == -32767 && uvToPointer(1.7f) == 32767, "außerhalb geklemmt");
    check(dosboxPureAbs(-32767) == 0.0f && dosboxPureAbs(32767) == 1.0f, "Rückweg Ränder 0/1");
    for (int a = 0; a <= 64; ++a)
        for (int b = 0; b <= 64; ++b) {
            int16_t x = 0, y = 0;
            const float u = 0.5f + (static_cast<float>(a) - 32.0f) * 1e-6f;
            const float v = 0.5f + (static_cast<float>(b) - 32.0f) * 1e-6f;
            pointerFromUv(u, v, x, y);
            check(!(x == 0 && y == 0), "(0, 0) würde DOSBox-Pure verwerfen");
            check(std::abs(x) <= 2 && std::abs(y) <= 2, "Mitte bleibt Mitte");
        }
    int16_t x = 0, y = 0;
    pointerFromUv(0.25f, 0.75f, x, y);
    check(x == uvToPointer(0.25f) && y == uvToPointer(0.75f), "pointerFromUv = uvToPointer je Achse");
}

void testZoom() {
    const float zooms[] = {1.0f, 1.5f, 2.0f, 3.0f};
    const float uvScale = 320.0f / 1024.0f;   // Frame 320 breit in der 1024er-Textur
    for (float zoom : zooms) {
        const float span = 1.0f / zoom;
        for (int c = 0; c <= 100; ++c) {
            const float center = static_cast<float>(c) / 100.0f;
            // wie main.cpp: Ausschnitt um die Mitte, am Bildrand geklemmt
            const float zoom0 = std::min(std::max(center - span * 0.5f, 0.0f), 1.0f - span);
            const float lo = frameUvFromScreen(0.0f, zoom0, span);
            const float hi = frameUvFromScreen(1.0f, zoom0, span);
            check(lo >= -1e-6f && hi <= 1.0f + 1e-6f, "Zoom-Ausschnitt im Frame");
            for (int s = 0; s <= 16; ++s) {
                const float screen = static_cast<float>(s) / 16.0f;
                // Shader: vUv = aUv · uvScale/zoom + zoom0 · uvScale (Textur-UV) → Frame-UV
                const float shader = (screen * uvScale / zoom + zoom0 * uvScale) / uvScale;
                check(std::fabs(shader - frameUvFromScreen(screen, zoom0, span)) < 1e-5f,
                      "Laser = Bildausschnitt bei Zoom " + std::to_string(zoom));
            }
        }
    }
}

void testFilterNames() {
    struct Case { const char* text; bool ok; ScreenFilter want; };
    const Case cases[] = {
        {"hd", true, ScreenFilter::Hd},        {"HD", true, ScreenFilter::Hd},
        {" hd \r", true, ScreenFilter::Hd},    {"xbr", true, ScreenFilter::Hd},
        {"hd-scanlines", true, ScreenFilter::HdScanlines},
        {"HD + Scanlines", true, ScreenFilter::HdScanlines},
        {"scharf", true, ScreenFilter::Sharp}, {"sharp", true, ScreenFilter::Sharp},
        {"weich", true, ScreenFilter::Soft},   {"CRT", true, ScreenFilter::Crt},
        {"scanlines", true, ScreenFilter::Scanlines},
        {"0", true, ScreenFilter::Sharp},      {"4", true, ScreenFilter::Hd},
        {"5", true, ScreenFilter::HdScanlines},
        {"6", false, ScreenFilter::Sharp},     {"", false, ScreenFilter::Sharp},
        {"ultra", false, ScreenFilter::Sharp}, {"-1", false, ScreenFilter::Sharp},
    };
    for (const Case& c : cases) {
        int out = -1;
        const bool ok = parseScreenFilter(c.text, out);
        check(ok == c.ok, std::string("Filtername erkannt: '") + c.text + "'");
        if (ok && c.ok) check(out == static_cast<int>(c.want), std::string("Filterwert: ") + c.text);
        if (!ok) check(out == -1, std::string("unbekannt lässt Wert stehen: ") + c.text);
    }
    int unused = 0;
    check(!parseScreenFilter(nullptr, unused), "nullptr wird abgelehnt");
    // Gespeicherte Werte 0–3 (vor HD) bleiben dieselben Filter.
    check(static_cast<int>(ScreenFilter::Sharp) == 0 && static_cast<int>(ScreenFilter::Soft) == 1 &&
          static_cast<int>(ScreenFilter::Scanlines) == 2 && static_cast<int>(ScreenFilter::Crt) == 3,
          "alte Filterwerte unverändert");
    for (int f = 0; f < kScreenFilterCount; ++f) {
        const ScreenFilter sf = static_cast<ScreenFilter>(f);
        check(!isHdFilter(withoutHd(sf)), "Rückfall ohne HD-Programm ist kein HD-Filter");
        if (!isHdFilter(sf)) check(withoutHd(sf) == sf, "Nicht-HD-Filter bleibt");
    }
    check(withoutHd(ScreenFilter::HdScanlines) == ScreenFilter::Scanlines, "HD + Scanlines → Scanlines");
}

void testOffset() {
    using namespace ptroff;
    check(clampPercent(0.3f) == 0.25f, "Raster 0,25 %");
    check(clampPercent(-99.0f) == -kMax && clampPercent(99.0f) == kMax, "Grenzen ±15 %");
    check(clampPercent(NAN) == 0.0f, "NaN → 0");
    check(stepPercent(0.0f, 3) == 0.75f && stepPercent(0.0f, -1) == -0.25f, "Schritte");
    check(stepPercent(kMax, 1) == kMax, "obere Grenze hält");
    check(parsePercent(formatPercent(-1.25f)) == -1.25f, "Speichern/Laden −1,25");
    check(parsePercent("abc") == 0.0f && parsePercent("") == 0.0f, "Unsinn → 0");
    check(label(0.0f) == "0" && label(-0.0f) == "0", "Null ohne Vorzeichen");
    check(label(1.25f) == "+1,25 %", "Anzeige positiv");
    check(label(-0.5f) == "−" "0,50 %", "Anzeige negativ");
    check(std::fabs(apply(0.5f, 2.0f) - 0.52f) < 1e-6f, "Verschiebung um 2 % der Breite");
    check(apply(0.995f, 2.0f) == 1.0f && apply(0.005f, -2.0f) == 0.0f, "am Rand geklemmt");
    check(apply(0.4f, 0.0f) == 0.4f, "ohne Verschiebung unverändert");
    // Kette bis DOSBox-Pure „direct“: 320 px breites Spiel, +1,25 % = 4 px weiter rechts.
    int16_t x = 0, y = 0;
    pointerFromUv(apply(0.5f, 1.25f), apply(0.5f, 0.0f), x, y);
    check(std::fabs(dosboxPureAbs(x) * 320.0f - 164.0f) < 0.05f, "320 px: +1,25 % = +4 px");
}

} // namespace

int main() {
    testOffset();
    testRoundTrip();
    testEdges();
    testZoom();
    testFilterNames();
    std::printf("test_pointer_map: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
