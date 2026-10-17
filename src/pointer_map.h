// XRShell — Laser → Spielzeiger (abhängigkeitsfrei, Host-Test tests/test_pointer_map.cpp).
//
// Kette: Strahltreffer (Bildschirm-UV, 0..1) → Frame-UV (Bildzoom-Ausschnitt) →
// RETRO_DEVICE_POINTER (−0x7FFF..0x7FFF). Der Bildschirm (Netz, Quad- und Zylinder-Ebene)
// zeigt genau den Frame bzw. den Zoom-Ausschnitt, ohne Rand-Beschnitt; Seitenverhältnis
// und Wölbung ändern nur die Fläche, nicht die UV-Abbildung (Wölbung: Bogenlänge ∝ u).
//
// DOSBox-Pure „direct“ (dosbox_pure_libretro.cpp, Mouse_CursorMoved(emulate = false)):
//   abs = (p + 0x7FFF) / 0xFFFE,  mouse.x = abs · max_x  (Textmodus: abs · Spalten · 8)
// Dabei zählt (0, 0) ohne Druck als „kein Zeiger“ (absvalid = absx || absy || pressed).
// Overscan-Rand bzw. „padded“-Seitenverhältnis rechnet DOSBox-Pure NICHT heraus → mit
// „direct“ beide aus lassen (Standard), sonst sitzt der Zeiger um den Rand versetzt.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

// Bildschirm-UV → Frame-UV beim Bildzoom (Ausschnitt ab zoom0 mit Kantenlänge span = 1/zoom).
// Gleiche Formel wie der Bildschirm-Shader (uUvScale/zoom, uUvOffset = zoom0 · uUvScale).
inline float frameUvFromScreen(float screenUv, float zoom0, float span) {
    return zoom0 + screenUv * span;
}

// Frame-UV (0..1, geklemmt) → libretro-Zeigerkoordinate −0x7FFF..0x7FFF.
inline int16_t uvToPointer(float uv) {
    const float c = std::fmin(std::fmax(uv, 0.0f), 1.0f);
    return static_cast<int16_t>(std::lround((c * 2.0f - 1.0f) * 32767.0f));
}

// Beide Achsen; genau die Bildmitte (0, 0) würde DOSBox-Pure ohne Druck verwerfen →
// x um eine Einheit (1/65534 der Bildbreite) verschieben.
inline void pointerFromUv(float u, float v, int16_t& x, int16_t& y) {
    x = uvToPointer(u);
    y = uvToPointer(v);
    if (x == 0 && y == 0) x = 1;
}

// Rückweg wie DOSBox-Pure („direct“): Zeigerkoordinate → 0..1 des Mausbereichs.
inline float dosboxPureAbs(int16_t p) {
    return (static_cast<float>(p) + 32767.0f) / 65534.0f;
}

// Zeigerverschiebung pro Spiel (VR-Menü → DOS, „Zeiger waagerecht/senkrecht“): Liegt der
// Spielzeiger trotz „direct“ bzw. Abgleich sichtbar neben dem Strahl (Hotspot des Spielpfeils,
// Rand, Mausbereich des Spiels), schiebt der Spieler die Eingabe um einen festen Anteil des
// Bildes nach. Gilt für beide Mausmodi (wirkt auf die Laser-UV, bevor Delta bzw. absolute
// Position entstehen). Wert in Prozent der Bildbreite/-höhe, positiv = nach rechts/unten.
namespace ptroff {

constexpr float kStep = 0.25f, kMax = 15.0f;   // Prozent
constexpr const char* kXKey = "xrshell_pointer_offset_x";
constexpr const char* kYKey = "xrshell_pointer_offset_y";

inline float clampPercent(float p) {
    if (!std::isfinite(p)) return 0.0f;
    p = std::round(p / kStep) * kStep;   // aufs Raster
    if (p == 0.0f) p = 0.0f;             // kein „-0,00“
    return std::min(std::max(p, -kMax), kMax);
}
inline float stepPercent(float p, int step) { return clampPercent(p + kStep * static_cast<float>(step)); }
inline std::string formatPercent(float p) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(clampPercent(p)));
    return buf;
}
inline float parsePercent(const std::string& s) {
    char* end = nullptr;
    const float p = std::strtof(s.c_str(), &end);
    return end == s.c_str() ? 0.0f : clampPercent(p);
}
// Anzeige: „0“, „+1,25 %“, „−0,50 %“ (Komma, echtes Minus).
inline std::string label(float p) {
    p = clampPercent(p);
    if (p == 0.0f) return "0";
    std::string s = formatPercent(std::fabs(p));
    for (char& c : s) if (c == '.') c = ',';
    return std::string(p > 0.0f ? "+" : "−") + s + " %";
}
// Laser-UV (0..1) → verschobene UV für das Spiel, geklemmt aufs Bild.
inline float apply(float uv, float percent) {
    return std::min(std::max(uv + percent / 100.0f, 0.0f), 1.0f);
}

} // namespace ptroff
