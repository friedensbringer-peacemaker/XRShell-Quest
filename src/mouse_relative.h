// XRShell — relative Maus (DOSBox-Pure „virtual“) mit Faktoren pro Achse und „Zeiger fangen“.
// Abhängigkeitsfrei (Host-Test tests/test_mouse_relative.cpp), läuft im input_poll des Cores.
//
// Warum: Mit „direct“ setzt DOSBox-Pure die INT-33-Position als abs · max_x/max_y. In VESA-Modi
// (> 0x13) führt es max_x/max_y nicht nach („Unhandled videomode“, src/ints/mouse.cpp) und
// ignoriert min_x/min_y → der Spielzeiger liegt weit neben dem Laser (Pirates! Gold 640×480).
// Relativ bewegt sich der Spielzeiger um Laser-Deltas × Faktor; ohne Bezugspunkt driftet er weg.
//
// Zeiger fangen: einmal weit nach links oben schieben (das Spiel klemmt den Zeiger an seinem
// Rand), kurz warten (das Spiel liest die Maus seltener als der Core läuft), dann die Laser-
// position × Faktor als ein Delta. Danach sitzt der Zeiger unter dem Laser, solange die Faktoren
// stimmen. Ausgelöst beim Schließen des VR-Menüs, wenn der Laser wieder aufs Bild kommt, und im
// Abgleich. Während des Fangens gelten die Maustasten als losgelassen (Klick verrutscht nicht,
// er kommt danach an).
//
// xr.adventure (ScummVM, liest RETRO_DEVICE_MOUSE relativ, Faktor 1): Fangen nur beim Spielstart
// und wenn die Bildgröße wechselt (Spiel 320×200 ↔ ScummVM-Menü 640×480) — nicht beim
// Wiedereintritt des Lasers (catchOnReentry = false). Solange nichts klemmt, bleibt der Zeiger
// mit Faktor 1 und Nachkomma-Rest ohnehin unter dem Laser; bei jedem Fangen springt er kurz in
// die linke obere Ecke (dort öffnet z. B. Beneath a Steel Sky die Inventarleiste).
// Bildgrößenwechsel (alle Cores): der letzte Laserpunkt wird auf die neue Größe umgerechnet —
// sonst entstünde im ersten Poll ein Sprung um die Größendifferenz (320 → 640: halbe Bildbreite).
//
// Abgleich (Faktor pro Achse): Zeiger auf einen festen Punkt (30 % / 30 % des Bildes) fangen und
// dort einfrieren (Laserbewegung geht nicht ans Spiel); der Nutzer zeigt mit dem Laser auf den
// tatsächlichen Spielzeiger. Gefangen bei L0, Zeiger bei c → neuer Faktor = Faktor · L0 / c.
// Der feste Punkt liegt weit genug vom Rand, damit der Zeiger auch bei dreifach zu großem Faktor
// nicht am rechten/unteren Rand klemmt (sonst wäre c verfälscht).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace mouserel {

constexpr float kGainMin = 0.1f, kGainMax = 5.0f, kGainStep = 0.05f;
constexpr const char* kGainXKey = "xrshell_mouse_gain_x";
constexpr const char* kGainYKey = "xrshell_mouse_gain_y";

inline float clampGain(float g) {
    if (!std::isfinite(g)) return 1.0f;
    g = std::round(g / kGainStep) * kGainStep;   // aufs Raster (kein 1,0500001)
    return std::min(std::max(g, kGainMin), kGainMax);
}
inline float stepGain(float g, int step) { return clampGain(g + kGainStep * static_cast<float>(step)); }
inline std::string formatGain(float g) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(clampGain(g)));
    return buf;
}
inline float parseGain(const std::string& s) {
    char* end = nullptr;
    const float g = std::strtof(s.c_str(), &end);
    return end == s.c_str() ? 1.0f : clampGain(g);
}
inline std::string gainLabel(float g) {
    std::string s = formatGain(g);
    for (char& c : s) if (c == '.') c = ',';
    return s + "×";
}

// Abgleich: gefangen bei caught, Spielzeiger tatsächlich bei aimed (gleiche Einheit, 0..1 des
// Bildes). Zu nah am Rand/an der Ecke oder unplausibel → Faktor bleibt.
inline float calibratedGain(float gain, float caught, float aimed) {
    if (caught < 0.15f || aimed < 0.03f) return gain;
    const float ratio = caught / aimed;
    if (ratio < 0.2f || ratio > 5.0f) return gain;
    return clampGain(gain * ratio);
}

struct RelativeMouse {
    static constexpr int kCatchDelta = -6000;   // weit über jeden Bildschirm hinaus
    static constexpr int kCatchWait = 8;        // Polls (~0,11 s bei 70 Hz) bis zum Zieldelta

    float gainX = 1.0f, gainY = 1.0f;
    float lastX = 0.0f, lastY = 0.0f;   // Laser in Frame-Pixeln beim letzten Poll
    float remX = 0.0f, remY = 0.0f;     // Nachkomma-Rest (langsame Bewegungen gehen nicht verloren)
    bool prevValid = false;
    int phase = 0, wait = 0;            // 0 normal, 1 wegschieben, 2 warten, 3 Ziel
    float caughtX = 0.0f, caughtY = 0.0f;   // Zielpunkt beim letzten Fangen (Frame-Pixel)
    unsigned catches = 0;
    float targetX = -1.0f, targetY = -1.0f; // fester Fangpunkt (Frame-Pixel), < 0 = Laser
    bool frozen = false;                    // Abgleich: Laserbewegung nicht ans Spiel
    bool catchOnReentry = true;             // Fangen, wenn der Laser zurück aufs Bild kommt

    void request() { if (phase == 0) phase = 1; }
    // Bildgröße wechselt um sx/sy (neu/alt): letzten Laserpunkt mitskalieren (kein Sprung),
    // Rest verwerfen; recatch = danach neu fangen (ScummVM: Spiel ↔ Menü).
    void resized(float sx, float sy, bool recatch) {
        if (!(sx > 0.0f) || !(sy > 0.0f)) return;
        lastX *= sx;
        lastY *= sy;
        remX = remY = 0.0f;
        if (recatch) request();
    }
    bool busy() const { return phase != 0; }

    // Ein Poll. x/y = Laser in Frame-Pixeln, valid = Laser auf dem Bild. catchEnabled = Fangen
    // erlaubt (xr.dosgames, relativer Modus); sonst verhält es sich wie die bisherige Delta-Maus.
    void poll(float x, float y, bool valid, bool catchEnabled, int& dx, int& dy) {
        dx = dy = 0;
        if (!catchEnabled) phase = 0;
        else if (catchOnReentry && valid && !prevValid) request();   // Laser kommt zurück aufs Bild
        prevValid = valid;
        switch (phase) {
        case 1:
            dx = dy = kCatchDelta;
            wait = kCatchWait;
            phase = 2;
            return;
        case 2:
            if (--wait <= 0) phase = 3;
            return;
        case 3:
            if (!valid) return;   // auf den Laser warten
            caughtX = targetX >= 0.0f ? targetX : x;
            caughtY = targetY >= 0.0f ? targetY : y;
            dx = static_cast<int>(std::lround(caughtX * gainX));
            dy = static_cast<int>(std::lround(caughtY * gainY));
            lastX = x;
            lastY = y;
            remX = remY = 0.0f;
            ++catches;
            phase = 0;
            return;
        default:
            break;
        }
        if (!valid) return;   // Laser weg: Zeiger bleibt stehen
        if (frozen) {           // Abgleich: Zeiger steht, Laser darf wandern
            lastX = x;
            lastY = y;
            remX = remY = 0.0f;
            return;
        }
        const float fx = (x - lastX) * gainX + remX, fy = (y - lastY) * gainY + remY;
        dx = static_cast<int>(fx);
        dy = static_cast<int>(fy);
        remX = fx - static_cast<float>(dx);
        remY = fy - static_cast<float>(dy);
        lastX = x;
        lastY = y;
    }
};

} // namespace mouserel
