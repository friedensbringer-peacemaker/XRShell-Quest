// XRShell — Bildwiederholrate der Quest passend zur Bildrate des Cores (XR_FB_display_refresh_rate).
//
// Läuft der Core mit 72 fps (PrBoom, TyrQuake) und die Quest mit 90 Hz, zeigt das Display jedes
// vierte Spielbild doppelt — sichtbares Ruckeln beim Drehen. Gewählt wird die niedrigste
// angebotene Rate bis `maxRate`, die ein ganzzahliges Vielfaches der Core-Bildrate ist (±3 %):
// 72 fps → 72 Hz, DOSBox-Pure 70,09 fps → 72 Hz, 35 fps → 72 Hz (2×), 60 fps → keine bis 90 Hz
// (120 Hz kostet zu viel GPU) → 0 = Standardrate der Runtime behalten.
// Abhängigkeitsfrei (Host-Test tests/test_refresh_rate.cpp).

#pragma once

#include <cmath>
#include <vector>

namespace refresh {

constexpr float kTolerance = 0.03f;   // relative Abweichung Rate ↔ k · fps
constexpr float kMaxRate = 90.0f;     // höhere Raten (120 Hz) nicht automatisch wählen

// Beste Rate aus `rates` für `fps`, 0 = keine passt (Standard behalten).
inline float pick(double fps, const std::vector<float>& rates, float maxRate = kMaxRate) {
    if (!(fps > 1.0)) return 0.0f;
    float best = 0.0f;
    for (float rate : rates) {
        if (!(rate > 1.0f) || rate > maxRate + 0.01f) continue;
        const double k = std::round(rate / fps);
        if (k < 1.0) continue;
        if (std::fabs(rate - k * fps) / rate > kTolerance) continue;
        if (best == 0.0f || rate < best) best = rate;
    }
    return best;
}

} // namespace refresh
