// Host-Test für src/mouse_relative.h (xr.dosgames: relative Maus, Faktor pro Achse, Zeiger fangen,
// Abgleich). Simuliert den Spielzeiger zweier Spieltypen:
//   „Position“: INT 33 Fkt. 3 — DOSBox klemmt den Zeiger bei jeder Bewegung an den Bereich
//   „Mickeys“:  das Spiel summiert Fkt. 0Bh und liest nur alle k Core-Frames (klemmt selbst)
// Sweep: Verhältnis Core/Spiel pro Achse (0,5 … 2,5) × Frame-Größen × Lesetakt; Laserpfade.
// Gebaut von tests/run-host-tests.sh.

#include "mouse_relative.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

struct Game {
    float rx, ry;            // Spielzeiger-Pixel je Delta-Einheit (Core-Skalierung, Mickey-Verhältnis)
    float w, h;              // Bereich des Spielzeigers = Frame-Pixel
    int readEvery;           // 1 = Position (sofort geklemmt), > 1 = Mickeys, alle k Frames gelesen
    float x = 0, y = 0;
    float pendX = 0, pendY = 0;
    int frame = 0;
    void move(int dx, int dy) {
        pendX += dx * rx;
        pendY += dy * ry;
        if (readEvery <= 1 || ++frame % readEvery == 0) {
            x = std::fmin(std::fmax(x + pendX, 0.0f), w - 1);
            y = std::fmin(std::fmax(y + pendY, 0.0f), h - 1);
            pendX = pendY = 0;
        }
    }
};

// Laser auf (lx, ly) halten, n Frames pollen.
void hold(mouserel::RelativeMouse& m, Game& g, float lx, float ly, int n, bool enabled = true) {
    for (int i = 0; i < n; ++i) {
        int dx, dy;
        m.poll(lx, ly, true, enabled, dx, dy);
        g.move(dx, dy);
    }
}

void testCatchAndTrack() {
    const float ratios[] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f};
    const struct { float w, h; } frames[] = {{320, 200}, {640, 400}, {640, 480}};
    for (float rx : ratios)
        for (float ry : ratios)
            for (const auto& f : frames)
                for (int every : {1, 3, 7}) {
                    Game g{rx, ry, f.w, f.h, every};
                    g.x = f.w * 0.77f;   // Zeiger irgendwo, Laser woanders
                    g.y = f.h * 0.13f;
                    mouserel::RelativeMouse m;
                    m.gainX = mouserel::clampGain(1.0f / rx);
                    m.gainY = mouserel::clampGain(1.0f / ry);
                    int dx, dy;
                    m.poll(0, 0, false, true, dx, dy);   // Laser noch nicht auf dem Bild
                    const float lx = f.w * 0.4f, ly = f.h * 0.6f;
                    hold(m, g, lx, ly, 30);               // Eintritt → automatisch fangen
                    // Rest: Faktor-Raster 0,05 → |1 − Faktor·r| · Weg, dazu Rundung (1 Delta-Einheit · r).
                    const float tolX = std::fabs(1.0f - m.gainX * rx) * lx + rx + 0.5f;
                    const float tolY = std::fabs(1.0f - m.gainY * ry) * ly + ry + 0.5f;
                    check(m.catches == 1, "Eintritt fängt genau einmal");
                    check(std::fabs(g.x - lx) <= tolX && std::fabs(g.y - ly) <= tolY,
                          "nach dem Fangen unter dem Laser (r " + std::to_string(rx) + "/" + std::to_string(ry) +
                              ", Lesetakt " + std::to_string(every) + ", Fehler " + std::to_string(g.x - lx) + "/" +
                              std::to_string(g.y - ly) + ")");
                    // Pfad über das ganze Bild: Zeiger folgt (Fehler wächst nur mit dem Faktor-Raster).
                    float maxErr = 0;
                    for (int i = 0; i <= 200; ++i) {
                        const float t = static_cast<float>(i) / 200.0f;
                        const float px = f.w * (0.1f + 0.8f * t), py = f.h * (0.5f + 0.35f * std::sin(t * 9.0f));
                        hold(m, g, px, py, 1);
                        if (every == 1) maxErr = std::fmax(maxErr, std::fmax(std::fabs(g.x - px) / rx, std::fabs(g.y - py) / ry));
                    }
                    hold(m, g, f.w * 0.9f, f.h * 0.85f, every + 1);
                    const float ex = std::fabs(g.x - f.w * 0.9f), ey = std::fabs(g.y - f.h * 0.85f);
                    // Rasterfehler des Faktors: |1 − g·r| · Weg (≤ 0,025·r relativ)
                    check(ex <= f.w * 0.03f + 2 && ey <= f.h * 0.03f + 2, "Zeiger folgt dem Laser über das Bild");
                    if (every == 1) check(maxErr <= std::fmax(f.w, f.h) * 0.03f + 2, "unterwegs dicht am Laser");
                }
}

void testButtonsAndRequest() {
    mouserel::RelativeMouse m;
    int dx, dy;
    m.poll(100, 100, true, true, dx, dy);
    check(m.busy() && dx == mouserel::RelativeMouse::kCatchDelta, "Eintritt: erst weit nach links oben");
    int frames = 1;
    while (m.busy() && frames < 100) { m.poll(100, 100, true, true, dx, dy); ++frames; }
    check(frames == mouserel::RelativeMouse::kCatchWait + 2, "Fangen dauert Wegschieben + Warten + Ziel");
    check(dx == 100 && dy == 100, "Zieldelta = Laserposition × Faktor");
    m.request();
    check(m.busy(), "Anforderung (Menü zu) startet das Fangen");
    // Laser verlässt das Bild während des Fangens → Ziel erst, wenn er zurück ist.
    m.poll(0, 0, false, true, dx, dy);
    for (int i = 0; i < 20; ++i) m.poll(0, 0, false, true, dx, dy);
    check(m.busy() && dx == 0 && dy == 0, "ohne Laser kein Zieldelta");
    m.poll(50, 60, true, true, dx, dy);
    check(!m.busy() && dx == 50 && dy == 60, "Laser zurück → Ziel");
    // Ohne Fangen (andere Apps, „direct“): bisheriges Verhalten, Summe der Deltas = Weg.
    mouserel::RelativeMouse plain;
    plain.poll(10.0f, 10.0f, true, false, dx, dy);
    int sx = 0;
    float x = 10.0f;
    for (int i = 0; i < 1000; ++i) {
        x += 0.37f;
        plain.poll(x, 10.0f, true, false, dx, dy);
        sx += dx;
        check(!plain.busy(), "ohne Fangen nie beschäftigt");
    }
    check(std::abs(sx - static_cast<int>(1000 * 0.37f)) <= 1, "langsame Bewegung geht nicht verloren (Rest)");
    plain.poll(0, 0, false, false, dx, dy);
    check(dx == 0 && dy == 0, "Laser weg: keine Bewegung");
}

void testCalibration() {
    // Falscher Faktor → Abgleich (Fangposition vs. tatsächlicher Zeiger) → Zeiger folgt.
    for (float r : {0.5f, 0.8f, 1.0f, 1.6f, 2.0f, 2.5f})
        for (float start : {0.5f, 1.0f, 2.0f}) {
            Game g{r, r * 2.0f, 640, 480, 1};   // Y oft doppelt so schnell (Mickey-Verhältnis)
            mouserel::RelativeMouse m;
            m.gainX = m.gainY = start;
            int dx, dy;
            m.poll(0, 0, false, true, dx, dy);
            hold(m, g, 400, 300, 30);   // Spiel läuft, Zeiger irgendwo
            // Abgleich: auf 30 %/30 % fangen und einfrieren; Laser wandert, Zeiger steht. Liegt der
            // Faktor mehr als ~3× daneben, klemmt der Zeiger am Rand → zweite Runde (Nutzer wiederholt).
            float gx = start, gy = start;
            int rounds = 0;
            for (bool clamped = true; clamped && rounds < 3; ++rounds) {
                m.gainX = gx; m.gainY = gy;
                m.targetX = 0.3f * 640;
                m.targetY = 0.3f * 480;
                m.frozen = true;
                m.request();
                hold(m, g, 500, 350, 30);
                const float before = g.x;
                hold(m, g, 100, 400, 10);
                check(g.x == before, "eingefroren: Laser bewegt den Zeiger nicht");
                clamped = g.x >= 639 || g.y >= 479;
                gx = mouserel::calibratedGain(gx, 0.3f, g.x / 640.0f);   // Nutzer zielt auf den Spielzeiger
                gy = mouserel::calibratedGain(gy, 0.3f, g.y / 480.0f);
            }
            const bool farOff = std::fmax(start * r, start * r * 2.0f) > 3.0f;
            check(rounds == 1 || farOff, "eine Runde genügt, solange der Faktor < 3× daneben liegt");
            check(rounds <= 2, "spätestens zwei Runden");
            check(std::fabs(gx * r - 1.0f) <= 0.05f * r + 0.01f, "Abgleich X trifft 1/r");
            check(std::fabs(gy * r * 2.0f - 1.0f) <= 0.1f * r + 0.01f, "Abgleich Y trifft 1/r");
            // Danach normal fangen: Zeiger unter dem Laser.
            m.gainX = gx; m.gainY = gy;
            m.frozen = false;
            m.targetX = m.targetY = -1.0f;
            m.request();
            hold(m, g, 400, 300, 30);
            check(std::fabs(g.x - 400) <= 640 * 0.04f && std::fabs(g.y - 300) <= 480 * 0.04f,
                  "nach dem Abgleich unter dem Laser (r " + std::to_string(r) + ", Start " + std::to_string(start) +
                      ", Zeiger " + std::to_string(g.x) + "/" + std::to_string(g.y) + ")");
        }
    check(mouserel::calibratedGain(1.5f, 0.05f, 0.5f) == 1.5f, "Fangen zu nah an der Ecke → Faktor bleibt");
    check(mouserel::calibratedGain(1.5f, 0.5f, 0.01f) == 1.5f, "Zeiger in der Ecke → Faktor bleibt");
    check(mouserel::calibratedGain(1.0f, 0.9f, 0.1f) == 1.0f, "unplausibles Verhältnis → Faktor bleibt");
    check(mouserel::clampGain(9.0f) == mouserel::kGainMax && mouserel::clampGain(0.0f) == mouserel::kGainMin, "Faktor geklemmt");
    check(mouserel::formatGain(1.05f) == "1.05" && mouserel::gainLabel(2.0f) == "2,00×", "Faktor-Text");
    check(mouserel::parseGain("x") == 1.0f && mouserel::parseGain("2.5") == 2.5f, "Faktor lesen");
    float g = 1.0f;
    for (int i = 0; i < 200; ++i) g = mouserel::stepGain(g, 1);
    check(g == mouserel::kGainMax, "› bis zum Maximum");
    for (int i = 0; i < 200; ++i) g = mouserel::stepGain(g, -1);
    check(g == mouserel::kGainMin, "‹ bis zum Minimum");
}

// xr.adventure (ScummVM): Zeiger in Pixeln der aktuellen Oberfläche, Faktor 1, am Rand geklemmt.
// Beim Wechsel Spiel (320×200) ↔ ScummVM-Menü (640×480) rechnet das Backend die Position je nach
// Version um — hier drei Modelle: behält die Pixel, skaliert mit, springt in die Mitte. Die Schleife
// bildet LibretroAdapter::pollSnapshot nach (Laser in Frame-Pixeln, resized() bei Größenwechsel).
struct Scumm {
    float w, h, x, y;
    void move(int dx, int dy) {
        x = std::fmin(std::fmax(x + static_cast<float>(dx), 0.0f), w - 1);
        y = std::fmin(std::fmax(y + static_cast<float>(dy), 0.0f), h - 1);
    }
    void resize(float nw, float nh, int model) {
        if (model == 1) { x *= nw / w; y *= nh / h; }
        if (model == 2) { x = nw / 2; y = nh / 2; }
        w = nw;
        h = nh;
        x = std::fmin(x, w - 1);
        y = std::fmin(y, h - 1);
    }
};

struct ScummLoop {
    mouserel::RelativeMouse m;
    float pw = 0, ph = 0;
    void poll(Scumm& g, float u, float v, bool valid) {
        if (g.w != pw || g.h != ph) {
            if (pw > 0) m.resized(g.w / pw, g.h / ph, true);
            pw = g.w;
            ph = g.h;
        }
        int dx, dy;
        m.poll(u * g.w, v * g.h, valid, true, dx, dy);
        g.move(dx, dy);
    }
};

void testScummSwitch() {
    const struct { float w, h; } sizes[] = {{320, 200}, {640, 480}, {320, 240}, {640, 400}};
    for (int model = 0; model < 3; ++model)
        for (const auto& a : sizes)
            for (const auto& b : sizes)
                for (float u = 0.05f; u < 1.0f; u += 0.15f)
                    for (float v = 0.05f; v < 1.0f; v += 0.15f) {
                        Scumm g{a.w, a.h, a.w * 0.5f, a.h * 0.5f};   // ScummVM startet mittig
                        ScummLoop loop;
                        loop.m.catchOnReentry = false;
                        loop.m.request();                            // Start: einmal fangen
                        for (int i = 0; i < 30; ++i) loop.poll(g, u, v, true);
                        char what[128];
                        std::snprintf(what, sizeof(what), "ScummVM Modell %d %gx%g→%gx%g Laser %.2f/%.2f", model,
                                      static_cast<double>(a.w), static_cast<double>(a.h), static_cast<double>(b.w),
                                      static_cast<double>(b.h), static_cast<double>(u), static_cast<double>(v));
                        check(std::fabs(g.x - u * a.w) <= 1.0f && std::fabs(g.y - v * a.h) <= 1.0f,
                              std::string(what) + ": nach dem Start unter dem Laser");
                        // Laser wandert langsam (Subpixel) — bleibt darunter.
                        float lu = u, lv = v;
                        for (int i = 0; i < 60; ++i) {
                            lu = std::fmin(lu + 0.0013f, 0.98f);
                            lv = std::fmax(lv - 0.0009f, 0.02f);
                            loop.poll(g, lu, lv, true);
                        }
                        check(std::fabs(g.x - lu * a.w) <= 1.0f && std::fabs(g.y - lv * a.h) <= 1.0f,
                              std::string(what) + ": folgt langsam");
                        // Menü auf/zu: Größe wechselt, nach dem Fangen wieder unter dem Laser. Ein Backend,
                        // das ohne Größenwechsel umsetzt (Modell 2, gleiche Größe), ist von außen nicht
                        // erkennbar — Restrisiko, siehe docs/ADVENTURE.md.
                        if (model == 2 && a.w == b.w && a.h == b.h) continue;
                        g.resize(b.w, b.h, model);
                        for (int i = 0; i < 30; ++i) loop.poll(g, lu, lv, true);
                        check(std::fabs(g.x - lu * b.w) <= 1.0f && std::fabs(g.y - lv * b.h) <= 1.0f,
                              std::string(what) + ": nach dem Wechsel unter dem Laser");
                        // Laser verlässt das Bild und kommt woanders zurück: kein Fangen, Zeiger folgt.
                        loop.poll(g, 0, 0, false);
                        loop.poll(g, 0, 0, false);
                        loop.poll(g, 0.5f, 0.5f, true);
                        check(!loop.m.busy(), std::string(what) + ": kein Fangen beim Wiedereintritt");
                    }
    // Ohne Fangen (andere Apps): Größenwechsel springt nicht mehr um die Größendifferenz.
    mouserel::RelativeMouse plain;
    int dx, dy;
    plain.poll(160, 100, true, false, dx, dy);
    plain.resized(2.0f, 2.4f, false);
    plain.poll(320, 240, true, false, dx, dy);
    check(dx == 0 && dy == 0, "Größenwechsel ohne Laserbewegung: kein Delta");
    plain.resized(0.0f, 1.0f, false);   // ungültig → nichts
    plain.poll(320, 240, true, false, dx, dy);
    check(dx == 0 && dy == 0, "ungültiger Faktor ignoriert");
}

} // namespace

int main() {
    testCatchAndTrack();
    testButtonsAndRequest();
    testCalibration();
    testScummSwitch();
    std::printf("test_mouse_relative: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
