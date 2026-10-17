// XRShell — Karte ziehen mit dem rechten Griff (xr.dosgames): Handweg ab dem Greifpunkt →
// Scroll-Tasten des Spiels. Abhängigkeitsfrei (Host-Test tests/test_map_drag.cpp).
//
// DOS-Spiele scrollen ihre Karte nur über Tasten (meist Pfeiltasten), nicht über eine Position.
// Zwei Modelle:
//
//   Joystick (Standard): Griff halten, die Hand ab dem Greifpunkt ist ein Stick. Weg auf der
//     Bildebene (x rechts, y oben) über eine Totzone → Taste(n) im Takt: kurz drücken (60 ms),
//     Pause, mit wachsender Auslenkung schneller (0,40 s → 0,12 s), ab 85 % Dauerdruck.
//     Diagonal (Winkel innerhalb ±22,5° um 45°) = zwei Tasten im selben Takt.
//   Hand folgt (experimentell): die Karte klebt an der Hand — Handweg (mit Hebel auf die
//     Bildebene, in Spielpixeln) / „Pixel pro Tastendruck“ = Zahl der Tastendrücke; jeder Druck
//     60 ms, danach 60 ms Pause (höchstens ~8 Schritte/s je Achse). Geht nur so gut, wie das Spiel
//     pro Tastendruck einen festen Weg scrollt; viele Spiele scrollen, solange die Taste unten ist
//     (Weg hängt dann am Spieltakt) — deshalb einstellbar und nicht Standard.
//
// Richtung: „natürlich“ = die Karte folgt der Hand (Hand nach links → Ansicht nach rechts →
// Taste RECHTS), „wie Stick“ = Hand nach links → Taste LINKS.
//
// Tasten-Timing: jeder Druck ≥ 60 ms, jede Pause ≥ 60 ms — der Core liest die Tastatur einmal pro
// Frame, kürzere Tipps gingen verloren (gleiche Regel wie key_profile.cpp, 50 ms).
// active = false (Griff los, Menü, Spielwechsel, Fokus weg) löst sofort alle Tasten.
//
// Einstellungen pro Spiel (Nutzerdatei bzw. xrshell.opt, VR-Menü → Steuerung):
//   xrshell_grip_right     = drag | rightclick | off
//   xrshell_drag_mode      = joystick | follow
//   xrshell_drag_direction = natural | stick
//   xrshell_drag_speed     = 1 … 5          (Joystick: Totzone/Vollausschlag)
//   xrshell_drag_step      = 2 … 64          (Hand folgt: Spielpixel pro Tastendruck)
//   xrshell_stick_right    = arrows | off    (rechter Stick als Pfeiltasten)

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace mapdrag {

enum Dir { kLeft, kRight, kUp, kDown, kDirs };   // Scroll-Taste (Ansicht bewegt sich so)
// kGripWindow: rechter Griff greift das Fenster wie der linke (xr.adventure-Standard).
enum GripMode { kGripDrag, kGripRightClick, kGripOff, kGripWindow, kGripModes };
enum Model { kJoystick, kFollow, kModels };
enum Direction { kNatural, kLikeStick, kDirections };

constexpr const char* kGripKey = "xrshell_grip_right";
constexpr const char* kModelKey = "xrshell_drag_mode";
constexpr const char* kDirectionKey = "xrshell_drag_direction";
constexpr const char* kSpeedKey = "xrshell_drag_speed";
constexpr const char* kStepKey = "xrshell_drag_step";
constexpr const char* kStickKey = "xrshell_stick_right";

constexpr int kSpeedMin = 1, kSpeedMax = 5, kSpeedDefault = 3;
constexpr int kStepMin = 2, kStepMax = 64, kStepDefault = 16;

// Takt (Sekunden)
constexpr float kPulseOn = 0.06f;      // Druckdauer
constexpr float kMinGap = 0.06f;       // Pause zwischen zwei Drücken
constexpr float kSlowPeriod = 0.40f;   // knapp über der Totzone
constexpr float kFastPeriod = 0.12f;   // kurz vor Dauerdruck (= Druck + Pause)
constexpr float kHoldAt = 0.85f;       // ab dieser Auslenkung (0..1) Dauerdruck
constexpr float kReleaseFactor = 0.75f;   // Hysterese: los erst unter 75 % der Totzone
constexpr float kDiagOn = 0.414f;      // tan 22,5°: Nebenachse zählt ab diesem Verhältnis
constexpr float kDiagOff = 0.30f;      // … und bleibt bis hierher (Hysterese)
constexpr float kFollowOn = 0.75f;     // Hand folgt: Schritt, sobald 0,75 Schritte offen sind
constexpr float kMaxDt = 0.1f;

struct Settings {
    GripMode grip = kGripRightClick;   // ohne Angabe: wie bisher (andere Apps)
    Model model = kJoystick;
    Direction direction = kNatural;
    int speed = kSpeedDefault;
    int stepPx = kStepDefault;
    bool stickArrows = true;
    bool operator==(const Settings& o) const {
        return grip == o.grip && model == o.model && direction == o.direction && speed == o.speed &&
               stepPx == o.stepPx && stickArrows == o.stickArrows;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// Totzone und Vollausschlag (Meter Handweg) je Zieh-Tempo 1 (ruhig) … 5 (flink).
struct JoystickParams { float deadzone; float full; };
inline JoystickParams joystickParams(int speed) {
    static const JoystickParams kTable[kSpeedMax] = {
        {0.030f, 0.20f}, {0.025f, 0.15f}, {0.020f, 0.12f}, {0.015f, 0.09f}, {0.012f, 0.07f}};
    return kTable[std::min(std::max(speed, kSpeedMin), kSpeedMax) - 1];
}

// --- Werte lesen/schreiben ------------------------------------------------------------

inline std::string lowerTrim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    std::string out = s.substr(b, e - b + 1);
    for (char& c : out) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

inline bool parseGrip(const std::string& raw, GripMode& out) {
    const std::string v = lowerTrim(raw);
    if (v == "drag" || v == "karte" || v == "map") { out = kGripDrag; return true; }
    if (v == "rightclick" || v == "rechtsklick" || v == "rmb") { out = kGripRightClick; return true; }
    if (v == "off" || v == "aus" || v == "none") { out = kGripOff; return true; }
    if (v == "window" || v == "fenster") { out = kGripWindow; return true; }
    return false;
}
inline const char* gripKey(GripMode g) {
    return g == kGripDrag ? "drag" : g == kGripOff ? "off" : g == kGripWindow ? "window" : "rightclick";
}

inline int parseIntIn(const std::string& s, int lo, int hi, int fallback) {
    char* end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return fallback;
    return static_cast<int>(std::min<long>(std::max<long>(n, lo), hi));
}

// lookup(key, out) → true, wenn gesetzt (Nutzerwert vor xrshell.opt). appGrip = App-Standard für
// den rechten Griff (xr.dosgames: Karte ziehen). Unbekanntes bleibt beim Standard.
template <typename Lookup>
Settings settingsFrom(Lookup lookup, GripMode appGrip) {
    Settings s;
    s.grip = appGrip;
    std::string v;
    GripMode g = appGrip;
    if (lookup(kGripKey, v) && parseGrip(v, g)) s.grip = g;
    if (lookup(kModelKey, v)) {
        const std::string m = lowerTrim(v);
        if (m == "follow" || m == "hand") s.model = kFollow;
        else if (m == "joystick" || m == "stick") s.model = kJoystick;
    }
    if (lookup(kDirectionKey, v)) {
        const std::string d = lowerTrim(v);
        if (d == "stick" || d == "inverted") s.direction = kLikeStick;
        else if (d == "natural" || d == "natürlich") s.direction = kNatural;
    }
    if (lookup(kSpeedKey, v)) s.speed = parseIntIn(v, kSpeedMin, kSpeedMax, kSpeedDefault);
    if (lookup(kStepKey, v)) s.stepPx = parseIntIn(v, kStepMin, kStepMax, kStepDefault);
    if (lookup(kStickKey, v)) {
        const std::string a = lowerTrim(v);
        if (a == "off" || a == "aus") s.stickArrows = false;
        else if (a == "arrows" || a == "pfeiltasten" || a == "on") s.stickArrows = true;
    }
    return s;
}

// Geänderte Werte als Nutzer-Optionen (Schlüssel, Wert).
inline std::vector<std::pair<std::string, std::string>> changes(const Settings& before, const Settings& after) {
    std::vector<std::pair<std::string, std::string>> out;
    if (after.grip != before.grip) out.emplace_back(kGripKey, gripKey(after.grip));
    if (after.model != before.model) out.emplace_back(kModelKey, after.model == kFollow ? "follow" : "joystick");
    if (after.direction != before.direction)
        out.emplace_back(kDirectionKey, after.direction == kLikeStick ? "stick" : "natural");
    if (after.speed != before.speed) out.emplace_back(kSpeedKey, std::to_string(after.speed));
    if (after.stepPx != before.stepPx) out.emplace_back(kStepKey, std::to_string(after.stepPx));
    if (after.stickArrows != before.stickArrows) out.emplace_back(kStickKey, after.stickArrows ? "arrows" : "off");
    return out;
}

// --- VR-Menü, Reiter „Steuerung“ -------------------------------------------------------

enum Row { kRowGrip, kRowModel, kRowDirection, kRowSpeed, kRowStick, kRows };

inline const char* rowLabel(Row r, const Settings& s) {
    switch (r) {
    case kRowGrip: return "Rechter Griff";
    case kRowModel: return "Zieh-Art";
    case kRowDirection: return "Zieh-Richtung";
    case kRowSpeed: return s.model == kFollow ? "Pixel je Tastendruck" : "Zieh-Tempo";
    case kRowStick: return "Rechter Stick";
    default: return "";
    }
}

inline std::string valueLabel(Row r, const Settings& s) {
    static const char* const kSpeedNames[kSpeedMax] = {"1 ruhig", "2", "3 mittel", "4", "5 flink"};
    switch (r) {
    case kRowGrip:
        return s.grip == kGripDrag ? "Karte ziehen" : s.grip == kGripOff ? "Aus"
             : s.grip == kGripWindow ? "Fenster greifen" : "Rechtsklick";
    case kRowModel: return s.model == kFollow ? "Hand folgt" : "Joystick";
    case kRowDirection: return s.direction == kLikeStick ? "Wie Stick" : "Natürlich";
    case kRowSpeed:
        if (s.model == kFollow) return std::to_string(s.stepPx) + " px";
        return kSpeedNames[std::min(std::max(s.speed, kSpeedMin), kSpeedMax) - 1];
    case kRowStick: return s.stickArrows ? "Pfeiltasten" : "Aus";
    default: return "";
    }
}

// Zieh-Zeilen wirken nur, solange der rechte Griff die Karte zieht.
inline bool rowDim(Row r, const Settings& s) {
    return (r == kRowModel || r == kRowDirection || r == kRowSpeed) && s.grip != kGripDrag;
}

inline int wrapIndex(int v, int n) { return (v % n + n) % n; }

// Eine Stufe weiter (step ±1): Auswahl mit Umlauf, Zahlen geklemmt.
inline Settings stepRow(Settings s, Row r, int step) {
    switch (r) {
    case kRowGrip: s.grip = static_cast<GripMode>(wrapIndex(s.grip + step, kGripModes)); break;
    case kRowModel: s.model = static_cast<Model>(wrapIndex(s.model + step, kModels)); break;
    case kRowDirection: s.direction = static_cast<Direction>(wrapIndex(s.direction + step, kDirections)); break;
    case kRowSpeed:
        if (s.model == kFollow) {
            // 2, 4, 8, 12, 16, 24, 32, 48, 64 — feiner, wo es zählt
            static const int kSteps[] = {2, 4, 8, 12, 16, 24, 32, 48, 64};
            constexpr int n = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));
            int i = 0;
            while (i < n - 1 && kSteps[i] < s.stepPx) ++i;   // erster Rasterwert ≥ aktuellem Wert
            // Zwischenwert (aus xrshell.opt): › landet auf dem nächstgrößeren, ‹ auf dem nächstkleineren.
            const int next = kSteps[i] == s.stepPx ? i + step : step > 0 ? i : i - 1;
            s.stepPx = kSteps[std::min(std::max(next, 0), n - 1)];
        } else {
            s.speed = std::min(std::max(s.speed + step, kSpeedMin), kSpeedMax);
        }
        break;
    case kRowStick: s.stickArrows = !s.stickArrows; break;
    default: break;
    }
    return s;
}

// --- Auslenkung → Tasten ----------------------------------------------------------------

class Driver {
public:
    // Pro Frame. active: Griff hält die Karte. offX/offY: Joystick = Handweg seit dem Greifpunkt
    // auf der Bildebene (m, x rechts, y oben); Hand folgt = derselbe Weg in Spielpixeln.
    // dt in s. out[kDirs]: welche Scroll-Tasten jetzt unten sein sollen.
    void update(bool active, float offX, float offY, float dt, const Settings& s, bool out[kDirs]) {
        for (int i = 0; i < kDirs; ++i) out[i] = false;
        if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
        dt = std::min(dt, kMaxDt);
        if (!active || !std::isfinite(offX) || !std::isfinite(offY)) {
            reset();
            return;
        }
        // In Tastenrichtung drehen: natürlich = Ansicht gegen die Hand (Karte folgt der Hand).
        const float sign = s.direction == kNatural ? -1.0f : 1.0f;
        const float kx = sign * offX, ky = sign * offY;
        if (s.model == kFollow) follow(kx, ky, dt, s, out);
        else joystick(kx, ky, dt, s, out);
    }

    void reset() {
        mAxisOn[0] = mAxisOn[1] = false;
        mPulsing = false;
        mHolding = false;
        mClock = 0.0f;
        for (Tap& t : mTap) t = Tap{};
    }

    // Hand folgt: bisher gesendete Schritte je Achse (+ = rechts/oben) — für Tests/Log.
    int emitted(int axis) const { return mTap[axis].emitted; }
    float intensity() const { return mIntensity; }
    bool engaged() const { return mAxisOn[0] || mAxisOn[1]; }   // Joystick: über der Totzone

private:
    void joystick(float kx, float ky, float dt, const Settings& s, bool out[kDirs]) {
        const JoystickParams p = joystickParams(s.speed);
        const float mag[2] = {std::fabs(kx), std::fabs(ky)};
        const float major = std::max(mag[0], mag[1]);
        bool on[2];
        for (int a = 0; a < 2; ++a) {
            const float dz = mAxisOn[a] ? p.deadzone * kReleaseFactor : p.deadzone;
            const float ratio = mAxisOn[a] ? kDiagOff : kDiagOn;
            on[a] = mag[a] > dz && mag[a] >= ratio * major;
        }
        mAxisOn[0] = on[0];
        mAxisOn[1] = on[1];
        if (!on[0] && !on[1]) {
            mPulsing = mHolding = false;
            mClock = 0.0f;
            mIntensity = 0.0f;
            return;
        }
        float m = 0.0f;
        for (int a = 0; a < 2; ++a)
            if (on[a]) m = std::max(m, mag[a]);
        mIntensity = std::min(std::max((m - p.deadzone) / (p.full - p.deadzone), 0.0f), 1.0f);

        bool down;
        if (mIntensity >= kHoldAt) {
            down = true;
            mHolding = true;
            mPulsing = false;
        } else {
            if (!mPulsing) {
                // Erster Druck sofort; nach Dauerdruck erst eine Pause (Taste muss los sein).
                mClock = mHolding ? kPulseOn : 0.0f;
                mPulsing = true;
                mHolding = false;
            } else {
                mClock += dt;
            }
            const float period = kSlowPeriod + (kFastPeriod - kSlowPeriod) * (mIntensity / kHoldAt);
            // Neuer Druck beginnt immer bei 0 (kein angebrochener Druck nach dem Umlauf).
            if (mClock >= period) mClock = 0.0f;
            down = mClock < kPulseOn;
        }
        if (!down) return;
        if (on[0]) out[kx > 0.0f ? kRight : kLeft] = true;
        if (on[1]) out[ky > 0.0f ? kUp : kDown] = true;
    }

    void follow(float kx, float ky, float dt, const Settings& s, bool out[kDirs]) {
        const float step = static_cast<float>(std::min(std::max(s.stepPx, kStepMin), kStepMax));
        const float target[2] = {kx / step, ky / step};
        mIntensity = 0.0f;
        for (int a = 0; a < 2; ++a) {
            Tap& t = mTap[a];
            t.clock += dt;
            if (t.phase == Tap::kOn && t.clock >= kPulseOn) { t.phase = Tap::kGap; t.clock = 0.0f; }
            if (t.phase == Tap::kGap && t.clock >= kMinGap) { t.phase = Tap::kIdle; t.clock = 0.0f; }
            if (t.phase == Tap::kIdle) {
                const float open = target[a] - static_cast<float>(t.emitted);
                if (open >= kFollowOn || open <= -kFollowOn) {
                    t.dir = open > 0.0f ? 1 : -1;
                    t.emitted += t.dir;
                    t.phase = Tap::kOn;
                    t.clock = 0.0f;
                }
            }
            if (t.phase == Tap::kOn) {
                if (a == 0) out[t.dir > 0 ? kRight : kLeft] = true;
                else out[t.dir > 0 ? kUp : kDown] = true;
            }
        }
    }

    struct Tap {
        enum Phase { kIdle, kOn, kGap } phase = kIdle;
        float clock = 0.0f;
        int dir = 0;
        int emitted = 0;
    };
    bool mAxisOn[2] = {false, false};
    bool mPulsing = false, mHolding = false;
    float mClock = 0.0f;
    float mIntensity = 0.0f;
    Tap mTap[2];
};

} // namespace mapdrag
