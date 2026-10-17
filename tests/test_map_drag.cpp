// Host-Test für src/map_drag.h (Karte ziehen mit dem rechten Griff, xr.dosgames):
// Auslenkung → Scroll-Tasten. Gebaut und ausgeführt von tests/run-host-tests.sh.
//
// Sweeps: Totzone je Tempo 1–5 × Richtung × 360 Winkel; Diagonale (8 Sektoren, Hysterese);
// Takt (Drücke pro Sekunde steigen mit der Auslenkung, Dauerdruck ab 85 %); Mindestzeiten
// Druck/Pause ≥ 50 ms bei 30/72/90/120 Hz; Lösen (Griff los → sofort alles los, auch mitten im
// Druck); Hand folgt (Schritte = Weg / Pixel je Druck, Rückweg, Zittern ohne Tasten, Rate);
// Einstellungen lesen/schreiben/Menü-Stufen.

#include "map_drag.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 40) std::printf("FEHLER: %s\n", what.c_str());
}

using mapdrag::Driver;
using mapdrag::Settings;

constexpr float kPi = 3.14159265f;

// Statistik einer Simulation: Tastendrücke, Haltezeiten, Pausen.
struct Stats {
    int presses[mapdrag::kDirs] = {};
    float downTime[mapdrag::kDirs] = {};
    float minOn = 1e9f, minGap = 1e9f;   // kürzester Druck / kürzeste Pause (gleiche Taste)
    bool anyDown = false;
};

// Hält die Auslenkung (ox, oy) `seconds` lang bei `hz`, danach active = false.
Stats simulate(const Settings& s, float ox, float oy, float seconds, float hz, Driver* reuse = nullptr) {
    Driver local;
    Driver& d = reuse ? *reuse : local;
    Stats st;
    const float dt = 1.0f / hz;
    bool prev[mapdrag::kDirs] = {};
    float since[mapdrag::kDirs] = {};   // Zeit seit letzter Flanke
    bool seenRelease[mapdrag::kDirs] = {};
    const int frames = static_cast<int>(seconds * hz + 0.5f);
    for (int f = 0; f <= frames; ++f) {
        bool out[mapdrag::kDirs];
        d.update(f < frames, ox, oy, f == 0 ? 0.0f : dt, s, out);
        for (int k = 0; k < mapdrag::kDirs; ++k) {
            if (f > 0) since[k] += dt;
            if (out[k]) { st.anyDown = true; st.downTime[k] += dt; }
            if (out[k] && !prev[k]) {
                ++st.presses[k];
                if (seenRelease[k]) st.minGap = std::fmin(st.minGap, since[k]);
                since[k] = 0.0f;
            } else if (!out[k] && prev[k]) {
                if (f < frames) st.minOn = std::fmin(st.minOn, since[k]);   // Abbruch am Ende zählt nicht
                seenRelease[k] = true;
                since[k] = 0.0f;
            }
            prev[k] = out[k];
        }
        if (f == frames)
            for (int k = 0; k < mapdrag::kDirs; ++k)
                check(!out[k], "Griff los: alle Tasten los");
    }
    return st;
}

// --- 1. Totzone: darunter nie eine Taste, knapp darüber immer ---
void testDeadzone() {
    for (int speed = mapdrag::kSpeedMin; speed <= mapdrag::kSpeedMax; ++speed)
        for (int dir = 0; dir < 2; ++dir)
            for (int deg = 0; deg < 360; deg += 1) {
                Settings s;
                s.grip = mapdrag::kGripDrag;
                s.speed = speed;
                s.direction = dir ? mapdrag::kLikeStick : mapdrag::kNatural;
                const float dz = mapdrag::joystickParams(speed).deadzone;
                const float a = static_cast<float>(deg) * kPi / 180.0f;
                const Stats in = simulate(s, 0.95f * dz * std::cos(a), 0.95f * dz * std::sin(a), 1.0f, 72.0f);
                check(!in.anyDown, "Totzone Tempo " + std::to_string(speed) + " Winkel " + std::to_string(deg));
                // Totzone gilt je Achse: die stärkere Achse 30 % darüber.
                const float r = 1.3f * dz / std::fmax(std::fabs(std::cos(a)), std::fabs(std::sin(a)));
                const Stats out = simulate(s, r * std::cos(a), r * std::sin(a), 1.0f, 72.0f);
                check(out.anyDown, "knapp über der Totzone Tempo " + std::to_string(speed) + " Winkel " +
                                       std::to_string(deg));
            }
}

// --- 2. Richtung und Diagonale: 8 Sektoren, natürlich = gegen die Hand ---
void testDirections() {
    for (int dir = 0; dir < 2; ++dir)
        for (int deg = 0; deg < 360; ++deg) {
            Settings s;
            s.direction = dir ? mapdrag::kLikeStick : mapdrag::kNatural;
            const float a = static_cast<float>(deg) * kPi / 180.0f;
            const float r = 0.10f;   // Tempo 3: 2–12 cm → kräftig, aber unter Dauerdruck
            const float hx = r * std::cos(a), hy = r * std::sin(a);
            Driver d;
            bool out[mapdrag::kDirs];
            d.update(true, hx, hy, 0.0f, s, out);   // erster Frame: Druck sofort
            const float sign = dir ? 1.0f : -1.0f;
            const float kx = sign * hx, ky = sign * hy;
            // Sektor der Tastenrichtung: 0 = rechts, 45 = rechts+oben …
            float kdeg = std::atan2(ky, kx) * 180.0f / kPi;
            if (kdeg < 0) kdeg += 360.0f;
            const float toAxis = std::fmod(kdeg, 90.0f);   // 0..90 innerhalb des Quadranten
            const bool clearlyDiag = toAxis > 24.0f && toAxis < 66.0f;
            const bool clearlyStraight = toAxis < 21.0f || toAxis > 69.0f;
            const int n = out[0] + out[1] + out[2] + out[3];
            const std::string tag = std::string(dir ? "wie Stick" : "natürlich") + " " + std::to_string(deg) + "°";
            if (clearlyDiag) check(n == 2, "Diagonale = zwei Tasten: " + tag);
            if (clearlyStraight) check(n == 1, "gerade = eine Taste: " + tag);
            check(!(out[mapdrag::kLeft] && out[mapdrag::kRight]) && !(out[mapdrag::kUp] && out[mapdrag::kDown]),
                  "nie Gegenrichtungen zugleich: " + tag);
            if (kx > 0.03f) check(!out[mapdrag::kLeft], "Richtung x: " + tag);
            if (kx < -0.03f) check(!out[mapdrag::kRight], "Richtung x: " + tag);
            if (ky > 0.03f) check(!out[mapdrag::kDown], "Richtung y: " + tag);
            if (ky < -0.03f) check(!out[mapdrag::kUp], "Richtung y: " + tag);
        }
    // Ausdrücklich: natürlich, Hand nach links → Taste RECHTS; Hand nach oben → Taste RUNTER.
    Settings nat;
    bool out[mapdrag::kDirs];
    Driver d;
    d.update(true, -0.08f, 0.0f, 0.0f, nat, out);
    check(out[mapdrag::kRight] && !out[mapdrag::kLeft], "natürlich: Hand links → Ansicht rechts");
    d.reset();
    d.update(true, 0.0f, 0.08f, 0.0f, nat, out);
    check(out[mapdrag::kDown], "natürlich: Hand hoch → Ansicht runter");
    Settings stick;
    stick.direction = mapdrag::kLikeStick;
    d.reset();
    d.update(true, -0.08f, 0.0f, 0.0f, stick, out);
    check(out[mapdrag::kLeft], "wie Stick: Hand links → Taste LINKS");
}

// --- 3. Diagonale mit Hysterese: Nebenachse flattert nicht an der Sektorgrenze ---
void testDiagonalHysteresis() {
    Settings s;
    s.direction = mapdrag::kLikeStick;
    Driver d;
    bool out[mapdrag::kDirs];
    // Bei ~22,5° hin und her (±1°): höchstens ein Wechsel der Nebenachse.
    int toggles = 0;
    bool prevUp = false;
    for (int f = 0; f < 200; ++f) {
        const float deg = (f % 2 ? 23.6f : 21.6f) * kPi / 180.0f;
        d.update(true, 0.20f * std::cos(deg), 0.20f * std::sin(deg), 1.0f / 72.0f, s, out);   // Dauerdruck
        if (f > 0 && out[mapdrag::kUp] != prevUp) ++toggles;
        prevUp = out[mapdrag::kUp];
    }
    check(toggles <= 1, "Diagonale an der Sektorgrenze flattert nicht (" + std::to_string(toggles) + " Wechsel)");
    // Totzone: um die Grenze pendeln → kein Flattern.
    d.reset();
    toggles = 0;
    bool prevRight = false;
    const float dz = mapdrag::joystickParams(3).deadzone;
    for (int f = 0; f < 200; ++f) {
        const float x = dz * (f % 2 ? 1.05f : 0.95f);
        d.update(true, x, 0.0f, 1.0f / 72.0f, s, out);
        const bool active = d.engaged();
        if (f > 0 && active != prevRight) ++toggles;
        prevRight = active;
    }
    check(toggles <= 1, "Totzone mit Hysterese (" + std::to_string(toggles) + " Wechsel)");
}

// --- 4. Takt: mehr Auslenkung → mehr Drücke, ab 85 % Dauerdruck; Mindestzeiten ---
void testPulse() {
    const float rates[] = {30.0f, 72.0f, 90.0f, 120.0f};
    for (float hz : rates)
        for (int speed = 1; speed <= 5; ++speed) {
            Settings s;
            s.direction = mapdrag::kLikeStick;
            s.speed = speed;
            const auto p = mapdrag::joystickParams(speed);
            int lastPresses = 0;
            float lastDown = 0.0f;
            for (int i = 1; i <= 20; ++i) {
                const float a = static_cast<float>(i) / 20.0f;   // Auslenkung 5 … 100 %
                const float x = p.deadzone + a * (p.full - p.deadzone) + 1e-4f;
                const Stats st = simulate(s, x, 0.0f, 3.0f, hz);
                const std::string tag = "Tempo " + std::to_string(speed) + " " + std::to_string(int(hz)) +
                                        " Hz Auslenkung " + std::to_string(i * 5) + " %";
                const int presses = st.presses[mapdrag::kRight];
                check(st.presses[mapdrag::kLeft] == 0 && st.presses[mapdrag::kUp] == 0, "nur RECHTS: " + tag);
                if (a >= mapdrag::kHoldAt) {
                    check(presses == 1 && st.downTime[mapdrag::kRight] > 2.9f, "Dauerdruck: " + tag);
                } else {
                    // 3 s: mindestens 3 s / 0,40 s, höchstens 3 s / 0,12 s (+1 für den Sofort-Druck)
                    check(presses >= 7 && presses <= 26, "Takt im Rahmen (" + std::to_string(presses) + "): " + tag);
                    check(presses >= lastPresses, "Takt steigt mit der Auslenkung: " + tag);
                    check(st.minOn >= 0.05f, "Druck ≥ 50 ms (" + std::to_string(st.minOn) + "): " + tag);
                    check(st.minGap >= 0.05f, "Pause ≥ 50 ms (" + std::to_string(st.minGap) + "): " + tag);
                }
                check(st.downTime[mapdrag::kRight] + 1e-4f >= lastDown, "Haltezeit steigt: " + tag);
                lastPresses = a >= mapdrag::kHoldAt ? 1000 : presses;
                lastDown = st.downTime[mapdrag::kRight];
            }
        }
    // Erster Druck kommt sofort (im ersten Frame), nicht erst nach einer Periode.
    Settings s;
    Driver d;
    bool out[mapdrag::kDirs];
    d.update(true, 0.04f, 0.0f, 0.0f, s, out);
    check(out[mapdrag::kLeft], "erster Druck sofort");
    // Von Dauerdruck zurück in den Takt: erst eine Pause.
    d.reset();
    d.update(true, 0.20f, 0.0f, 0.0f, s, out);
    d.update(true, 0.20f, 0.0f, 0.1f, s, out);
    check(out[mapdrag::kLeft], "Dauerdruck");
    d.update(true, 0.04f, 0.0f, 1.0f / 72.0f, s, out);
    check(!out[mapdrag::kLeft], "nach Dauerdruck zurück in den Takt: erst Pause");
}

// --- 5. Lösen: Griff los, auch mitten im Druck/Dauerdruck; ungültige Werte ---
void testRelease() {
    Settings s;
    for (int f = 1; f < 60; ++f) {
        Driver d;
        bool out[mapdrag::kDirs];
        const float x = 0.02f + 0.002f * static_cast<float>(f);
        for (int i = 0; i < f; ++i) d.update(true, x, -x, 1.0f / 90.0f, s, out);
        d.update(false, x, -x, 1.0f / 90.0f, s, out);
        check(!out[0] && !out[1] && !out[2] && !out[3], "Griff los nach " + std::to_string(f) + " Frames");
        // Danach neu greifen: Druck sofort wieder möglich, kein Rest aus dem alten Zug.
        d.update(true, 0.0f, 0.0f, 1.0f / 90.0f, s, out);
        check(!out[0] && !out[1] && !out[2] && !out[3], "neuer Griff im Greifpunkt: nichts gedrückt");
    }
    Driver d;
    bool out[mapdrag::kDirs];
    d.update(true, NAN, 0.1f, 0.01f, s, out);
    check(!out[0] && !out[1] && !out[2] && !out[3], "NaN → nichts");
    d.update(true, 0.1f, 0.0f, NAN, s, out);
    d.update(true, 0.1f, 0.0f, -1.0f, s, out);
    d.update(true, 0.1f, 0.0f, 1e9f, s, out);
    check(std::isfinite(d.intensity()) && d.intensity() >= 0.0f && d.intensity() <= 1.0f, "ungültiges dt: Zustand bleibt gültig");
}

// --- 6. Hand folgt: Schritte = Weg / Pixel je Druck, Rückweg, Zittern, Rate ---
void testFollow() {
    for (int step : {4, 8, 16, 32})
        for (int dir = 0; dir < 2; ++dir)
            for (int target = -6; target <= 6; ++target) {
                Settings s;
                s.model = mapdrag::kFollow;
                s.stepPx = step;
                s.direction = dir ? mapdrag::kLikeStick : mapdrag::kNatural;
                Driver d;
                bool out[mapdrag::kDirs];
                const float px = static_cast<float>(target * step);
                int pressesL = 0, pressesR = 0;
                bool prev[mapdrag::kDirs] = {};
                // Hand in 0,3 s an die Stelle, dann 2 s halten (genug Zeit zum Aufholen).
                for (int f = 0; f < 72 * 3; ++f) {
                    const float t = static_cast<float>(f) / 72.0f;
                    const float pos = px * std::fmin(t / 0.3f, 1.0f);
                    d.update(true, pos, 0.0f, 1.0f / 72.0f, s, out);
                    if (out[mapdrag::kLeft] && !prev[mapdrag::kLeft]) ++pressesL;
                    if (out[mapdrag::kRight] && !prev[mapdrag::kRight]) ++pressesR;
                    for (int k = 0; k < mapdrag::kDirs; ++k) prev[k] = out[k];
                }
                const int expect = dir ? target : -target;   // natürlich: gegen die Hand
                const std::string tag = "Hand folgt " + std::to_string(step) + " px, Ziel " +
                                        std::to_string(target) + (dir ? " wie Stick" : " natürlich");
                check(d.emitted(0) == expect, tag + ": gesendet " + std::to_string(d.emitted(0)));
                check(pressesR - pressesL == expect && (pressesL == 0 || pressesR == 0), tag + ": Drücke");
                check(d.emitted(1) == 0, tag + ": keine senkrechten Schritte");
            }
    // Rückweg: hin 5 Schritte, zurück zum Greifpunkt → Summe 0.
    Settings s;
    s.model = mapdrag::kFollow;
    s.direction = mapdrag::kLikeStick;
    Driver d;
    bool out[mapdrag::kDirs];
    for (int f = 0; f < 200; ++f) d.update(true, 80.0f, 40.0f, 1.0f / 72.0f, s, out);
    check(d.emitted(0) == 5 && d.emitted(1) == 2, "Hand folgt: 80/40 px bei 16 px = 5/2 Schritte (Schritt erst ab 0,75 offen)");
    for (int f = 0; f < 200; ++f) d.update(true, 0.0f, 0.0f, 1.0f / 72.0f, s, out);
    check(d.emitted(0) == 0 && d.emitted(1) == 0, "Hand folgt: zurück zum Greifpunkt = 0");
    // Zittern ±0,4 Schritte um eine Stelle: keine weiteren Drücke.
    d.reset();
    for (int f = 0; f < 200; ++f) d.update(true, 32.0f, 0.0f, 1.0f / 72.0f, s, out);
    const int settled = d.emitted(0);
    for (int f = 0; f < 400; ++f) d.update(true, 32.0f + (f % 2 ? 6.4f : -6.4f), 0.0f, 1.0f / 72.0f, s, out);
    check(d.emitted(0) == settled, "Hand folgt: Zittern löst nichts aus");
    // Zwischen zwei Schritten (2,5 Schritte) mit ±0,2 Schritten zittern: kein Hin und Her (Hysterese).
    d.reset();
    for (int f = 0; f < 200; ++f) d.update(true, 40.0f * std::fmin(f / 50.0f, 1.0f), 0.0f, 1.0f / 72.0f, s, out);
    const int mid = d.emitted(0);
    int changes = 0;
    for (int f = 0; f < 400; ++f) {
        const int before = d.emitted(0);
        d.update(true, 40.0f + (f % 2 ? 3.2f : -3.2f), 0.0f, 1.0f / 72.0f, s, out);
        changes += d.emitted(0) != before;
    }
    check(mid == 2 && changes == 0, "Hand folgt: zwischen zwei Schritten kein Hin und Her (" + std::to_string(changes) + ")");
    // Rate: 1 s weit weg → höchstens 1 s / (60 + 60 ms) + 1 Schritte, Druck/Pause ≥ 50 ms.
    for (float hz : {30.0f, 72.0f, 90.0f, 120.0f}) {
        const Stats st = simulate(s, 10000.0f, 0.0f, 1.0f, hz);
        check(st.presses[mapdrag::kRight] >= 6 && st.presses[mapdrag::kRight] <= 9,
              "Hand folgt: Rate " + std::to_string(st.presses[mapdrag::kRight]) + "/s bei " + std::to_string(int(hz)) + " Hz");
        check(st.minOn >= 0.05f && st.minGap >= 0.05f, "Hand folgt: Druck/Pause ≥ 50 ms bei " + std::to_string(int(hz)) + " Hz");
    }
}

// --- 7. Einstellungen: lesen, schreiben, Menü-Stufen ---
void testSettings() {
    std::map<std::string, std::string> opts;
    auto lookup = [&](const char* key, std::string& out) {
        auto it = opts.find(key);
        if (it == opts.end()) return false;
        out = it->second;
        return true;
    };
    Settings def = mapdrag::settingsFrom(lookup, mapdrag::kGripDrag);
    check(def.grip == mapdrag::kGripDrag && def.model == mapdrag::kJoystick && def.direction == mapdrag::kNatural &&
              def.speed == 3 && def.stepPx == 16 && def.stickArrows,
          "App-Standard xr.dosgames: Karte ziehen, Joystick, natürlich, Tempo 3, Stick = Pfeiltasten");
    check(Settings{}.grip == mapdrag::kGripRightClick, "andere Apps: Griff = Rechtsklick");
    opts = {{mapdrag::kGripKey, " RightClick "}, {mapdrag::kModelKey, "follow"}, {mapdrag::kDirectionKey, "stick"},
            {mapdrag::kSpeedKey, "9"}, {mapdrag::kStepKey, "1"}, {mapdrag::kStickKey, "off"}};
    Settings s = mapdrag::settingsFrom(lookup, mapdrag::kGripDrag);
    check(s.grip == mapdrag::kGripRightClick && s.model == mapdrag::kFollow && s.direction == mapdrag::kLikeStick &&
              s.speed == 5 && s.stepPx == 2 && !s.stickArrows,
          "Profilwerte gelesen, Zahlen geklemmt");
    opts = {{mapdrag::kGripKey, "aus"}};
    check(mapdrag::settingsFrom(lookup, mapdrag::kGripDrag).grip == mapdrag::kGripOff, "„aus“");
    opts = {{mapdrag::kGripKey, "Fenster"}};
    check(mapdrag::settingsFrom(lookup, mapdrag::kGripDrag).grip == mapdrag::kGripWindow &&
              std::string(mapdrag::gripKey(mapdrag::kGripWindow)) == "window",
          "„Fenster“ ↔ window");
    opts = {{mapdrag::kGripKey, "quatsch"}, {mapdrag::kSpeedKey, "x"}};
    s = mapdrag::settingsFrom(lookup, mapdrag::kGripDrag);
    check(s.grip == mapdrag::kGripDrag && s.speed == 3, "Unbekanntes → Standard");

    // Rundweg: jede Kombination → changes → wieder gelesen = gleich.
    int combos = 0;
    for (int g = 0; g < mapdrag::kGripModes; ++g)
        for (int m = 0; m < mapdrag::kModels; ++m)
            for (int d = 0; d < mapdrag::kDirections; ++d)
                for (int sp = 1; sp <= 5; ++sp)
                    for (int st : {2, 16, 64})
                        for (int a = 0; a < 2; ++a) {
                            Settings want;
                            want.grip = static_cast<mapdrag::GripMode>(g);
                            want.model = static_cast<mapdrag::Model>(m);
                            want.direction = static_cast<mapdrag::Direction>(d);
                            want.speed = sp;
                            want.stepPx = st;
                            want.stickArrows = a != 0;
                            opts.clear();
                            const Settings base = mapdrag::settingsFrom(lookup, mapdrag::kGripDrag);
                            for (const auto& kv : mapdrag::changes(base, want)) opts[kv.first] = kv.second;
                            check(mapdrag::settingsFrom(lookup, mapdrag::kGripDrag) == want, "Rundweg Einstellungen");
                            ++combos;
                        }
    check(combos == mapdrag::kGripModes * 2 * 2 * 5 * 3 * 2 && mapdrag::kGripModes == 4, "alle Kombinationen geprüft");
    check(mapdrag::changes(def, def).empty(), "unverändert → keine Nutzerwerte");

    // Menü: Griff läuft um, Tempo klemmt, Pixel je Druck im Raster, gedimmte Zeilen.
    Settings m;
    m.grip = mapdrag::kGripDrag;
    m = mapdrag::stepRow(m, mapdrag::kRowGrip, 1);
    check(m.grip == mapdrag::kGripRightClick, "Griff › Rechtsklick");
    m = mapdrag::stepRow(m, mapdrag::kRowGrip, 1);
    check(m.grip == mapdrag::kGripOff, "Griff › Aus");
    m = mapdrag::stepRow(m, mapdrag::kRowGrip, 1);
    check(m.grip == mapdrag::kGripWindow && mapdrag::valueLabel(mapdrag::kRowGrip, m) == "Fenster greifen",
          "Griff › Fenster greifen");
    m = mapdrag::stepRow(m, mapdrag::kRowGrip, 1);
    check(m.grip == mapdrag::kGripDrag, "Griff › Karte ziehen (Umlauf)");
    for (int i = 0; i < 10; ++i) m = mapdrag::stepRow(m, mapdrag::kRowSpeed, 1);
    check(m.speed == 5, "Tempo klemmt oben");
    for (int i = 0; i < 10; ++i) m = mapdrag::stepRow(m, mapdrag::kRowSpeed, -1);
    check(m.speed == 1, "Tempo klemmt unten");
    m.model = mapdrag::kFollow;
    m.stepPx = 16;
    m = mapdrag::stepRow(m, mapdrag::kRowSpeed, 1);
    check(m.stepPx == 24, "16 px › 24 px");
    m = mapdrag::stepRow(m, mapdrag::kRowSpeed, -1);
    m = mapdrag::stepRow(m, mapdrag::kRowSpeed, -1);
    check(m.stepPx == 12, "‹ ‹ → 12 px");
    m.stepPx = 20;   // Zwischenwert aus xrshell.opt
    check(mapdrag::stepRow(m, mapdrag::kRowSpeed, 1).stepPx == 24 && mapdrag::stepRow(m, mapdrag::kRowSpeed, -1).stepPx == 16,
          "Zwischenwert 20 px: › 24, ‹ 16");
    for (int i = 0; i < 20; ++i) m = mapdrag::stepRow(m, mapdrag::kRowSpeed, 1);
    check(m.stepPx == 64, "Pixel je Druck klemmt oben");
    check(std::string(mapdrag::rowLabel(mapdrag::kRowSpeed, m)) == "Pixel je Tastendruck" &&
              mapdrag::valueLabel(mapdrag::kRowSpeed, m) == "64 px",
          "Hand folgt: Zeile heißt „Pixel je Tastendruck“");
    m.model = mapdrag::kJoystick;
    check(std::string(mapdrag::rowLabel(mapdrag::kRowSpeed, m)) == "Zieh-Tempo", "Joystick: „Zieh-Tempo“");
    m.grip = mapdrag::kGripRightClick;
    check(mapdrag::rowDim(mapdrag::kRowModel, m) && mapdrag::rowDim(mapdrag::kRowSpeed, m) &&
              !mapdrag::rowDim(mapdrag::kRowGrip, m) && !mapdrag::rowDim(mapdrag::kRowStick, m),
          "ohne Karte ziehen: Zieh-Zeilen gedimmt, Griff/Stick nicht");
    m = mapdrag::stepRow(m, mapdrag::kRowStick, 1);
    check(!m.stickArrows && mapdrag::valueLabel(mapdrag::kRowStick, m) == "Aus", "Stick › Aus");
}

} // namespace

int main() {
    testDeadzone();
    testDirections();
    testDiagonalHysteresis();
    testPulse();
    testRelease();
    testFollow();
    testSettings();
    std::printf("test_map_drag: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
