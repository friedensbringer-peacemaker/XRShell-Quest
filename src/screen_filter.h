// XRShell — Bildfilter für Core-Frames (VR-Menü „Filter“, Standard pro Spiel/App).
// Abhängigkeitsfrei (Host-Tests): Aufzählung, Namen und Rückfall ohne HD-Shader.

#pragma once

#include <cctype>
#include <cstring>
#include <string>

// Werte werden in xrshell-view.cfg gespeichert („filter = N“) — nur anhängen, nie umnummern.
enum class ScreenFilter { Sharp = 0, Soft, Scanlines, Crt, Hd, HdScanlines, Count };

constexpr int kScreenFilterCount = static_cast<int>(ScreenFilter::Count);

// Standardfilter aus dem Spielordner: `xrshell_filter = hd` in xrshell.opt.
// Akzeptiert deutsche und englische Namen sowie die Zahl; false = unbekannt.
inline bool parseScreenFilter(const char* text, int& out) {
    if (!text) return false;
    std::string s;
    for (const char* c = text; *c; ++c)
        if (!std::isspace(static_cast<unsigned char>(*c)))
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    struct Name { const char* name; ScreenFilter filter; };
    static const Name kNames[] = {
        {"scharf", ScreenFilter::Sharp},      {"sharp", ScreenFilter::Sharp},
        {"weich", ScreenFilter::Soft},        {"soft", ScreenFilter::Soft},
        {"scanlines", ScreenFilter::Scanlines},
        {"crt", ScreenFilter::Crt},
        {"hd", ScreenFilter::Hd},             {"xbr", ScreenFilter::Hd},
        {"hd-scanlines", ScreenFilter::HdScanlines}, {"hd+scanlines", ScreenFilter::HdScanlines},
    };
    for (const Name& n : kNames)
        if (s == n.name) { out = static_cast<int>(n.filter); return true; }
    if (s.size() == 1 && s[0] >= '0' && s[0] < '0' + kScreenFilterCount) {
        out = s[0] - '0';
        return true;
    }
    return false;
}

// Schlüssel für xrshell.opt bzw. die Nutzerdatei pro Spiel (Gegenstück zu parseScreenFilter).
inline const char* screenFilterKey(int filter) {
    static const char* const kKeys[] = {"scharf", "weich", "scanlines", "crt", "hd", "hd-scanlines"};
    static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == kScreenFilterCount, "Filterschlüssel");
    return filter >= 0 && filter < kScreenFilterCount ? kKeys[filter] : kKeys[0];
}

// HD-Filter brauchen das eigene Shader-Programm; scheitert es (Kompilierfehler),
// gilt der nächstliegende einfache Filter: HD → Scharf, HD + Scanlines → Scanlines.
inline ScreenFilter withoutHd(ScreenFilter f) {
    if (f == ScreenFilter::Hd) return ScreenFilter::Sharp;
    if (f == ScreenFilter::HdScanlines) return ScreenFilter::Scanlines;
    return f;
}

inline bool isHdFilter(ScreenFilter f) {
    return f == ScreenFilter::Hd || f == ScreenFilter::HdScanlines;
}
