// XRShell — DOS-Einstellungen im VR-Menü (xr.dosgames): welche DOSBox-Pure-Optionen, welche Werte,
// wie beschriftet, live oder erst nach Neustart; dazu die Bildwerte pro Spiel („xrshell_…“).
// Abhängigkeitsfrei (Host-Test tests/test_option_store.cpp).
//
// Optionsliste des gebündelten Cores (DOSBox-Pure, libretro-Buildbot, Stand 2026-10-03), aus
// den Zeichenketten der .so gelesen: dosbox_pure_mouse_input (… „virtual“, „direct“ …),
// dosbox_pure_mouse_speed_factor, dosbox_pure_cycles (auto, max, 315 … 1000000 cps),
// dosbox_pure_aspect_correction (false, true, padded, doublescan, padded-doublescan),
// dosbox_pure_svga (svga_s3, vesa_nolfb, vesa_oldvbe, svga_et3000, svga_et4000, svga_paradise).
// „(restart required)“ steht dort u. a. bei „SVGA Mode“, „Memory Size“, „Emulated Graphics Chip“
// — nicht bei Zyklen, Mausmodus, Mausempfindlichkeit, Seitenverhältnis.
// Zur Laufzeit gilt die Liste, die der Core selbst meldet (SET_VARIABLES / SET_CORE_OPTIONS*);
// die Liste hier ist nur der Rückfall, falls er nichts meldet.

#pragma once

#include "pointer_map.h"
#include "screen_filter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace dosopt {

enum Id { kMouse, kMouseSpeed, kCycles, kAspect, kSvga, kCount };

struct Spec {
    const char* key;
    bool live;                       // wirkt sofort (GET_VARIABLE_UPDATE), sonst nach Neustart
    std::vector<const char*> values; // Rückfall-Liste bzw. erlaubte Teilmenge
    bool onlyListed;                 // nur Werte aus `values` anbieten (Rest der Core-Liste nicht)
    const char* defaultValue;        // Core-Standard, falls weder Spiel noch Core etwas sagen
};

inline const Spec& spec(Id id) {
    static const Spec kSpecs[kCount] = {
        // „pad“ (Touchpad), „false“ (aus), „auto“ passen nicht zum Laser → nicht angeboten.
        {"dosbox_pure_mouse_input", true, {"direct", "virtual", "true"}, true, "virtual"},
        {"dosbox_pure_mouse_speed_factor", true,
         {"0.5", "0.75", "1.0", "1.25", "1.5", "2.0", "2.5", "3.0"}, false, "1.0"},
        {"dosbox_pure_cycles", true,
         {"auto", "max", "315", "1320", "2750", "4720", "7800", "13400", "26800", "77000",
          "200000", "500000", "1000000"}, false, "auto"},
        // „padded“ setzt einen Rand, den die Maus „direct“ nicht herausrechnet → nicht angeboten.
        {"dosbox_pure_aspect_correction", true, {"false", "true"}, true, "false"},
        {"dosbox_pure_svga", false,
         {"svga_s3", "vesa_nolfb", "vesa_oldvbe", "svga_et3000", "svga_et4000", "svga_paradise"},
         false, "svga_s3"},
    };
    return kSpecs[id];
}

inline bool isLiveKey(const std::string& key) {
    if (key.compare(0, 8, "xrshell_") == 0) return true;   // Bildwerte von XRShell selbst
    for (int i = 0; i < kCount; ++i)
        if (key == spec(static_cast<Id>(i)).key) return spec(static_cast<Id>(i)).live;
    return false;   // unbekannte Kern-Option: sicherheitshalber erst nach Neustart
}

// Anzeige eines Werts (deutsch, kurz genug für die Wertkapsel).
inline std::string label(Id id, const std::string& v) {
    switch (id) {
    case kMouse:
        if (v == "direct") return "Laser-genau";
        if (v == "virtual" || v == "true") return "Relativ";
        break;
    case kMouseSpeed: {
        std::string s = v;
        for (char& c : s) if (c == '.') c = ',';
        return s + "×";
    }
    case kCycles: {
        struct Name { const char* value; const char* text; };
        static const Name kNames[] = {
            {"auto", "Auto"}, {"max", "Maximum"}, {"315", "8088 4,77 MHz"}, {"1320", "286 6 MHz"},
            {"2750", "286 12 MHz"}, {"4720", "386 20 MHz"}, {"7800", "386DX 33 MHz"},
            {"13400", "486DX 33 MHz"}, {"26800", "486DX2 66 MHz"}, {"77000", "Pentium 100"},
            {"200000", "Pentium II 300"}, {"500000", "Pentium III 600"}, {"1000000", "Athlon 1,2 GHz"},
        };
        for (const Name& n : kNames)
            if (v == n.value) return n.text;
        return v + " cps";
    }
    case kAspect:
        if (v == "false") return "Pixel 1:1";
        if (v == "true") return "4:3";
        break;
    case kSvga:
        if (v == "svga_s3") return "S3 Trio64";
        if (v == "vesa_nolfb") return "S3 ohne LFB";
        if (v == "vesa_oldvbe") return "S3 VESA 1.3";
        if (v == "svga_et3000") return "Tseng ET3000";
        if (v == "svga_et4000") return "Tseng ET4000";
        if (v == "svga_paradise") return "Paradise";
        break;
    default:
        break;
    }
    return v;
}

// Werte, die das Menü durchschaltet: Liste des Cores (falls gemeldet), sonst die Rückfall-Liste;
// bei onlyListed nur die erlaubte Teilmenge. Der aktuelle Wert ist immer dabei (Profilwert
// außerhalb der Liste, z. B. „cycles = 20000“).
inline std::vector<std::string> choices(Id id, const std::vector<std::string>& announced,
                                        const std::string& current) {
    const Spec& s = spec(id);
    std::vector<std::string> out;
    auto listed = [&](const std::string& v) {
        for (const char* allowed : s.values) if (v == allowed) return true;
        return false;
    };
    if (!announced.empty()) {
        for (const std::string& v : announced)
            if (!s.onlyListed || listed(v)) out.push_back(v);
    } else {
        for (const char* v : s.values) out.emplace_back(v);
    }
    // Maus: „virtual“ und „true“ meinen beide „Relativ“ — nur einen anbieten.
    if (id == kMouse && std::find(out.begin(), out.end(), "virtual") != out.end())
        out.erase(std::remove(out.begin(), out.end(), std::string("true")), out.end());
    if (!current.empty() && std::find(out.begin(), out.end(), current) == out.end()) {
        // Zahlenwerte einsortieren, sonst vorn anhängen.
        const double cur = std::atof(current.c_str());
        size_t at = 0;
        if (cur > 0.0)
            while (at < out.size() && (std::atof(out[at].c_str()) == 0.0 || std::atof(out[at].c_str()) < cur)) ++at;
        out.insert(out.begin() + static_cast<long>(at), current);
    }
    return out;
}

// Nächster Wert (step ±1) mit Umlauf; unbekannter aktueller Wert → erster.
inline std::string stepValue(const std::vector<std::string>& list, const std::string& current, int step) {
    if (list.empty()) return current;
    const auto it = std::find(list.begin(), list.end(), current);
    if (it == list.end()) return list.front();
    const int n = static_cast<int>(list.size());
    const int i = static_cast<int>(it - list.begin());
    return list[static_cast<size_t>(((i + step) % n + n) % n)];
}

// --- Bildwerte pro Spiel (xrshell_… in xrshell.opt bzw. der Nutzerdatei) ------------------

struct GameView {
    int filter = 0, saturation = 100, contrast = 100, curve = 0;
    float width = 0.0f, distance = 0.0f;   // 0 = keine Vorgabe (Fenster bleibt, wie es ist)
    int offsetX = 0, offsetY = 0;          // Zeigerverschiebung in Schritten zu ptroff::kStep %
};

constexpr const char* kFilterKey = "xrshell_filter";
constexpr const char* kSaturationKey = "xrshell_saturation";
constexpr const char* kContrastKey = "xrshell_contrast";
constexpr const char* kCurveKey = "xrshell_curve";
constexpr const char* kWidthKey = "xrshell_width";
constexpr const char* kDistanceKey = "xrshell_distance";

// lookup(key, out) → true, wenn gesetzt. Ungültiges wird ignoriert bzw. geklemmt.
template <typename Lookup>
GameView gameViewFrom(Lookup lookup, int appFilter) {
    GameView v;
    v.filter = appFilter;
    std::string s;
    int f = 0;
    if (lookup(kFilterKey, s) && parseScreenFilter(s.c_str(), f)) v.filter = f;
    auto intIn = [&](const char* key, int lo, int hi, int& out) {
        if (!lookup(key, s)) return;
        char* end = nullptr;
        const long n = std::strtol(s.c_str(), &end, 10);
        if (end != s.c_str()) out = static_cast<int>(std::min<long>(std::max<long>(n, lo), hi));
    };
    intIn(kSaturationKey, 0, 200, v.saturation);
    intIn(kContrastKey, 50, 150, v.contrast);
    intIn(kCurveKey, 0, 3, v.curve);
    float w = 0.0f, d = 0.0f;
    if (lookup(kWidthKey, s)) w = std::strtof(s.c_str(), nullptr);
    if (lookup(kDistanceKey, s)) d = std::strtof(s.c_str(), nullptr);
    if (std::isfinite(w) && std::isfinite(d) && w >= 1.0f && w <= 6.0f && d >= 0.7f && d <= 3.5f) {
        v.width = w;
        v.distance = d;
    }
    // Zeigerverschiebung in Prozent (xrshell_pointer_offset_x = 1.25) → Schritte.
    if (lookup(ptroff::kXKey, s)) v.offsetX = static_cast<int>(std::lround(ptroff::parsePercent(s) / ptroff::kStep));
    if (lookup(ptroff::kYKey, s)) v.offsetY = static_cast<int>(std::lround(ptroff::parsePercent(s) / ptroff::kStep));
    return v;
}

inline std::string formatMeters(float m) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(m));
    return buf;
}

// Geänderte Bildwerte als Nutzer-Optionen (Schlüssel, Wert). Fenstermaße auf cm gerundet,
// damit Kopfbewegungen beim Öffnen des Menüs nicht jedes Mal einen neuen Wert erzeugen.
inline std::vector<std::pair<std::string, std::string>> gameViewChanges(const GameView& before,
                                                                         const GameView& after) {
    std::vector<std::pair<std::string, std::string>> out;
    if (after.filter != before.filter)
        out.emplace_back(kFilterKey, screenFilterKey(after.filter));
    if (after.saturation != before.saturation) out.emplace_back(kSaturationKey, std::to_string(after.saturation));
    if (after.contrast != before.contrast) out.emplace_back(kContrastKey, std::to_string(after.contrast));
    if (after.curve != before.curve) out.emplace_back(kCurveKey, std::to_string(after.curve));
    if (after.offsetX != before.offsetX)
        out.emplace_back(ptroff::kXKey, ptroff::formatPercent(after.offsetX * ptroff::kStep));
    if (after.offsetY != before.offsetY)
        out.emplace_back(ptroff::kYKey, ptroff::formatPercent(after.offsetY * ptroff::kStep));
    if (after.width > 0.0f && after.distance > 0.0f &&
        (formatMeters(after.width) != formatMeters(before.width) ||
         formatMeters(after.distance) != formatMeters(before.distance))) {
        out.emplace_back(kWidthKey, formatMeters(after.width));
        out.emplace_back(kDistanceKey, formatMeters(after.distance));
    }
    return out;
}

// „Größe passend zum Spiel“: Bildbreite für einen Blickwinkel, bei dem das ganze Bild ohne
// Kopfdrehen im Blick ist — 53° für 320er-Spiele (ein Spielpixel ≈ 0,17°), 66° ab 640 Pixeln
// Breite (Text bleibt lesbar). Breite = 2 · Abstand · tan(Winkel / 2), geklemmt auf 1–6 m.
inline float fitAngleDegrees(int frameWidth) { return frameWidth >= 600 ? 66.0f : 53.0f; }
inline float fitWidth(int frameWidth, float distance) {
    const float a = fitAngleDegrees(frameWidth) * 3.14159265f / 180.0f;
    const float w = 2.0f * distance * std::tan(a * 0.5f);
    return std::min(std::max(std::round(w * 10.0f) / 10.0f, 1.0f), 6.0f);
}

} // namespace dosopt
