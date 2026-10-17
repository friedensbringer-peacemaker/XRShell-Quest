// XRShell — Controller-Belegung für Pad-Cores. Siehe pad_mapping.h.

#include "pad_mapping.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace padmap {

namespace {

// RetroPad-IDs (libretro.h, RETRO_DEVICE_ID_JOYPAD_*) — hier ohne Abhängigkeit.
enum : int8_t { B = 0, Y = 1, SELECT = 2, START = 3, UP = 4, DOWN = 5, LEFT = 6, RIGHT = 7,
                A = 8, X = 9, L = 10, R = 11, L2 = 12, R2 = 13, L3 = 14, R3 = 15 };
constexpr int kPadIds = 16;
const char* const kPadKeys[kPadIds] = { "b", "y", "select", "start", "up", "down", "left", "right",
                                        "a", "x", "l", "r", "l2", "r2", "l3", "r3" };
const char* const kSourceKeys[kSourceCount] = { "a", "b", "x", "y", "right_trigger", "right_grip",
                                                "left_trigger", "right_stick_click", "left_stick_click" };
const char* const kSourceNames[kSourceCount] = { "A", "B", "X", "Y", "Trigger R", "Griff R",
                                                 "Trigger L", "R-Stick", "L-Stick" };
const char* const kStickKeys[kStickCount] = { "left_stick", "right_stick" };
const char* const kStickModeKeys[kStickModeCount] = { "dpad", "analog_left", "analog_right", "off",
                                                       "analog_left_dpad", "analog_left_dpad_y" };
const char* const kStickModeNames[kStickModeCount] = { "Steuerkreuz", "Analog links", "Analog rechts", "Aus",
                                                       "Analog + Kreuz", "Analog + hoch/runter" };

//                                 A     B     X       Y      TrigR  GriffR  TrigL  R-Stick L-Stick
// Unbekanntes System (allgemeines RetroPad): B/Y auf Trigger/Griff, L/R auf Trigger L/X, Start auf Y.
constexpr Mapping kGenericPad = {{ A, X, R, START, B, Y, L, SELECT, kNone }, { kStickDpad, kStickDpad }};
// xr.shooter (aus den festen Layouts von xr-dos, docs/SHOOTER.md), Sticks analog:
//                                 A     B     X     Y      TrigR  GriffR TrigL   R-Stick L-Stick
// PrBoom „Gamepad Modern“: R2 Feuer, L2 Rennen, Y Benutzen, A/X Waffe vor/zurück, Start Menü,
// Select Karte, R3 180°, L3 Dauerlauf.
// Doom: Menüs (PrBoom) gehen nur übers Steuerkreuz → linker Stick hoch/runter zusätzlich als
// Steuerkreuz, erst ab 0,9 Ausschlag (Core-Smoke-Test 2026-10-04: mit reinem Analog bewegte sich
// der Menü-Cursor nicht). Im Spiel ist Steuerkreuz hoch = „vor“ → Vollausschlag ≈ Tempo-Schub.
constexpr Mapping kDoom = {{ Y, A, X, START, R2, L2, SELECT, R3, L3 }, { kStickAnalogLeftDpadY, kStickAnalogRight }};
// ECWolf: A Feuer (Menü: bestätigen), X Rennen, B Benutzen (Menü: zurück), L2/R2 Waffe,
// Start Menü, Select Karte, Y Statusleiste; linker Stick vor/zurück auch als Steuerkreuz (Menüs).
constexpr Mapping kWolf = {{ B, R2, L2, START, A, X, SELECT, Y, kNone },
                           { kStickAnalogLeftDpadY, kStickAnalogRight }};
// TyrQuake „Modern“: R2 Feuer, L2 Springen, A Menü bestätigen, R/L Waffe, B Abtauchen,
// Start Menü, Select Punktestand; linker Stick auch als Steuerkreuz (Menüs).
constexpr Mapping kQuake = {{ A, R, L, START, R2, L2, B, SELECT, kNone },
                            { kStickAnalogLeftDpad, kStickAnalogRight }};
// DOSBox-Pure-Gamepad-Mapper (xrshell_input = pad): Auto-Mappings für Shooter — R Feuer,
// L2 Rennen, Y Benutzen, L Waffenrad, X Menü, Start Enter, R3 Treten. Linken Stick drücken = L3:
// DOSBox-Pure legt dort sein Menü/die Bildschirmtastatur ab, solange die Belegung (DOS.YML,
// Auto-Mapping) L3 nicht nutzt — vorher stand hier A, damit war das Menü im Pad-Modus nicht
// erreichbar (Core-Smoke-Test 2026-10-04). Sticks rein analog.
constexpr Mapping kDosPad = {{ Y, B, X, START, R, L2, L, R3, L3 }, { kStickAnalogLeft, kStickAnalogRight }};

const std::vector<System>& systems() {
    static const std::vector<System> list = {
        // Shooter: Spielfunktionen statt Padnamen (die Belegung des Cores ist fest).
        { "doom", "Doom", {{ R2, "Feuer" }, { L2, "Rennen" }, { Y, "Benutzen" }, { A, "Waffe vor" },
                           { X, "Waffe zurück" }, { START, "Menü" }, { SELECT, "Karte" }, { R3, "180°" },
                           { L3, "Dauerlauf" }}, true, kDoom },
        { "wolf3d", "Wolf3D", {{ A, "Feuer" }, { X, "Rennen" }, { B, "Benutzen" }, { R2, "Waffe vor" },
                               { L2, "Waffe zurück" }, { START, "Menü" }, { SELECT, "Karte" },
                               { Y, "Statusleiste" }}, true, kWolf },
        { "quake", "Quake", {{ R2, "Feuer" }, { L2, "Springen" }, { A, "Bestätigen" }, { R, "Waffe vor" },
                             { L, "Waffe zurück" }, { B, "Abtauchen" }, { START, "Menü" },
                             { SELECT, "Punkte" }}, true, kQuake },
        { "dospad", "DOS", {{ A, "A" }, { B, "B" }, { X, "X" }, { Y, "Y" }, { L, "L" }, { R, "R" },
                            { L2, "L2" }, { R2, "R2" }, { R3, "R3" }, { START, "Start" }, { SELECT, "Select" }},
          true, kDosPad },
    };
    return list;
}

int padIdFromKey(const std::string& key) {
    if (key == "none" || key == "-") return kNone;
    for (int i = 0; i < kPadIds; ++i)
        if (key == kPadKeys[i]) return i;
    return -2;   // ungültig
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

bool Mapping::operator==(const Mapping& o) const {
    return std::equal(std::begin(button), std::end(button), std::begin(o.button)) &&
           std::equal(std::begin(stick), std::end(stick), std::begin(o.stick));
}

const System& systemFor(const std::string& folder) {
    for (const System& s : systems())
        if (folder == s.folder) return s;
    static const System fallback = { "", "Gamepad", {{ A, "A" }, { B, "B" }, { X, "X" }, { Y, "Y" }, { L, "L" },
                                                      { R, "R" }, { START, "Start" }, { SELECT, "Select" }},
                                     false, kGenericPad };
    return fallback;
}

unsigned stickDpadAxes(StickMode m) {
    switch (m) {
    case kStickDpad: case kStickAnalogLeftDpad: return 3;
    case kStickAnalogLeftDpadY: return 2;
    default: return 0;
    }
}

bool stickDpadHigh(StickMode m) {
    return m == kStickAnalogLeftDpad || m == kStickAnalogLeftDpadY;
}

int stickAnalogTarget(StickMode m) {
    switch (m) {
    case kStickAnalogLeft: case kStickAnalogLeftDpad: case kStickAnalogLeftDpadY: return 0;
    case kStickAnalogRight: return 1;
    default: return -1;
    }
}

const char* sourceName(Source s) { return s >= 0 && s < kSourceCount ? kSourceNames[s] : "?"; }
const char* stickModeName(StickMode m) { return m >= 0 && m < kStickModeCount ? kStickModeNames[m] : "?"; }

void assign(Mapping& m, int target, Source src) {
    if (src < 0 || src >= kSourceCount) return;
    const int previous = m.button[src];
    if (previous == target) return;
    for (int s = 0; s < kSourceCount; ++s)
        if (s != src && m.button[s] == target) m.button[s] = static_cast<int8_t>(previous);   // Tausch
    m.button[src] = static_cast<int8_t>(target);
}

std::vector<Source> sourcesFor(const Mapping& m, int target) {
    std::vector<Source> out;
    for (int s = 0; s < kSourceCount; ++s)
        if (m.button[s] == target) out.push_back(static_cast<Source>(s));
    return out;
}

uint16_t buttonMask(const Mapping& m, const bool pressed[kSourceCount]) {
    uint16_t mask = 0;
    for (int s = 0; s < kSourceCount; ++s)
        if (pressed[s] && m.button[s] >= 0 && m.button[s] < kPadIds) mask |= static_cast<uint16_t>(1u << m.button[s]);
    return mask;
}

std::string serialize(const Mapping& m) {
    std::string out = "# XRShell-Tastenbelegung (Quest-Taste = Konsolentaste)\n";
    for (int s = 0; s < kSourceCount; ++s) {
        out += kSourceKeys[s];
        out += " = ";
        out += m.button[s] >= 0 && m.button[s] < kPadIds ? kPadKeys[m.button[s]] : "none";
        out += '\n';
    }
    for (int k = 0; k < kStickCount; ++k) {
        out += kStickKeys[k];
        out += " = ";
        out += kStickModeKeys[m.stick[k] < kStickModeCount ? m.stick[k] : kStickDpad];
        out += '\n';
    }
    return out;
}

Mapping parse(const std::string& text, const Mapping& base) {
    Mapping m = base;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = lower(trim(line.substr(eq + 1)));
        for (int s = 0; s < kSourceCount; ++s) {
            if (key != kSourceKeys[s]) continue;
            const int id = padIdFromKey(value);
            if (id != -2) m.button[s] = static_cast<int8_t>(id);
        }
        for (int k = 0; k < kStickCount; ++k) {
            if (key != kStickKeys[k]) continue;
            for (int mode = 0; mode < kStickModeCount; ++mode)
                if (value == kStickModeKeys[mode]) m.stick[k] = static_cast<uint8_t>(mode);
        }
    }
    return m;
}

bool loadFile(const std::string& path, const Mapping& base, Mapping& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 64 * 1024) text.append(buf, n);
    std::fclose(f);
    out = parse(text, base);
    return true;
}

bool saveFile(const std::string& path, const Mapping& m) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::string text = serialize(m);
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) return false;
#ifndef _WIN32
    // App-Dateien im per adb angelegten Bibliotheksordner: für adb (Gruppe ext_data_rw bzw. andere)
    // lesbar, sonst scheitert z. B. pull-logs.sh/Sichern mit „Permission denied“ (wie Sitzungslog).
    ::chmod(path.c_str(), 0664);
#endif
    return true;
}

std::string gameFile(const std::string& gamePath) {
    std::string p = gamePath;
    while (!p.empty() && p.back() == '/') p.pop_back();   // Spielordner (DOS) ohne Schrägstrich
    return p + ".pad";
}

std::string systemFile(const std::string& externalDataPath, const std::string& folder) {
    return externalDataPath + "/games/" + (folder.empty() ? std::string("other") : folder) + "/_standard.pad";
}

Mapping loadFor(const std::string& gamePath, const std::string& externalDataPath, const std::string& folder) {
    const Mapping builtIn = systemFor(folder).defaults;
    Mapping system = builtIn;
    loadFile(systemFile(externalDataPath, folder), builtIn, system);
    Mapping game = system;
    if (!gamePath.empty()) loadFile(gameFile(gamePath), system, game);
    return game;
}

} // namespace padmap
