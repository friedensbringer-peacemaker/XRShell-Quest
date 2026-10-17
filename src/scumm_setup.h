// XRShell — xr.adventure: Grundeinstellungen für den ScummVM-Core (abhängigkeitsfrei).
//
// Beim ersten Start legt der Adapter <System-Dir>/scummvm.ini an, falls sie fehlt:
//   [scummvm]
//   language=<Gerätesprache als ScummVM-Code>
//   subtitles=true
//   gui_language=<Gerätesprache als Code aus translations.dat, z. B. de_DE>
// Eine vorhandene ini ohne gui_language bekommt die Zeile nachgetragen (ensureGuiLanguage);
// eine im ScummVM-Menü gewählte Sprache bleibt. Englisch/unbekannt: keine Zeile (ScummVM-Standard).
// Bisher per run-as von Hand angelegt — nach einer Neuinstallation war das weg. Eine vorhandene
// Datei (auch eine vom Core selbst erweiterte) bleibt unangetastet.
//
// ScummVM-Sprachcodes weichen teils von ISO 639 ab (common/language.cpp): ja → jp, ko → kr,
// sv → se, cs → cz, pt-BR → br, no → nb. Nicht sicher zuordenbare Sprachen: kein language-
// Eintrag (ScummVM nimmt dann die Sprache der Spieldaten). Host-Test: tests/test_scumm_setup.cpp.

#pragma once

#include <cctype>
#include <string>

namespace scummsetup {

// ISO-639-Sprache (+ optional ISO-3166-Land) des Geräts → ScummVM-Code, leer = unbekannt.
inline std::string scummLanguage(std::string lang, std::string country) {
    for (char& c : lang) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : country) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (lang == "pt") return country == "BR" ? "br" : "pt";
    struct Map { const char* iso; const char* scumm; };
    static const Map kMap[] = {
        {"de", "de"}, {"en", "en"}, {"fr", "fr"}, {"it", "it"}, {"es", "es"}, {"nl", "nl"},
        {"pl", "pl"}, {"ru", "ru"}, {"ja", "jp"}, {"ko", "kr"}, {"sv", "se"}, {"cs", "cz"},
        {"da", "da"}, {"fi", "fi"}, {"hu", "hu"}, {"tr", "tr"}, {"nb", "nb"}, {"no", "nb"},
    };
    for (const Map& m : kMap)
        if (lang == m.iso) return m.scumm;
    return {};
}

// Gerätesprache → Sprache der ScummVM-Oberfläche (Codes aus gui/themes/translations.dat, mit
// Länderkürzel, wo die Datei eines führt). Leer = Englisch/unbekannt (keine Übersetzung nötig).
inline std::string guiLanguage(std::string lang, std::string country) {
    for (char& c : lang) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : country) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (lang == "pt") return country == "BR" ? "pt_BR" : "pt_PT";
    struct Map { const char* iso; const char* gui; };
    static const Map kMap[] = {
        {"de", "de_DE"}, {"fr", "fr_FR"}, {"it", "it_IT"}, {"es", "es_ES"}, {"nl", "nl_NL"},
        {"pl", "pl_PL"}, {"ru", "ru_RU"}, {"sv", "sv_SE"}, {"cs", "cs_CZ"}, {"fi", "fi_FI"},
        {"hu", "hu_HU"}, {"uk", "uk_UA"}, {"ca", "ca_ES"}, {"gl", "gl_ES"}, {"be", "be_BY"},
        {"nb", "nb_NO"}, {"no", "nb_NO"}, {"nn", "nn_NO"}, {"da", "da"}, {"el", "el"}, {"he", "he"},
        {"ja", "ja"}, {"ko", "ko"}, {"tr", "tr"}, {"eu", "eu"}, {"ka", "ka"}, {"hi", "hi"},
    };
    for (const Map& m : kMap)
        if (lang == m.iso) return m.gui;
    return {};
}

// Inhalt der neuen scummvm.ini.
inline std::string iniText(const std::string& scummLang, const std::string& guiLang = {}) {
    std::string text = "[scummvm]\n";
    if (!scummLang.empty()) text += "language=" + scummLang + "\n";
    text += "subtitles=true\n";
    if (!guiLang.empty()) text += "gui_language=" + guiLang + "\n";
    return text;
}

// Vorhandene ini: gui_language im Abschnitt [scummvm] nachtragen, falls er fehlt (ältere
// xr.adventure-Versionen schrieben ihn nicht). Eigene Wahl bleibt; ohne Abschnitt wird er angehängt.
inline std::string ensureGuiLanguage(const std::string& ini, const std::string& guiLang) {
    if (guiLang.empty()) return ini;
    bool inMain = false;
    size_t mainEnd = std::string::npos;   // Zeilenende der Kopfzeile [scummvm]
    for (size_t pos = 0; pos < ini.size();) {
        size_t eol = ini.find('\n', pos);
        if (eol == std::string::npos) eol = ini.size();
        std::string line = ini.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '[') {
            inMain = line == "[scummvm]";
            if (inMain) mainEnd = eol;
        } else if (inMain && line.compare(0, 13, "gui_language=") == 0) {
            return ini;   // schon gesetzt (auch eine eigene Wahl)
        }
        pos = eol + 1;
    }
    const std::string entry = "gui_language=" + guiLang + "\n";
    if (mainEnd == std::string::npos) {
        std::string out = ini;
        if (!out.empty() && out.back() != '\n') out += "\n";
        return out + "[scummvm]\n" + entry;
    }
    if (mainEnd >= ini.size()) return ini + "\n" + entry;   // Kopfzeile ohne Zeilenende am Dateiende
    return ini.substr(0, mainEnd + 1) + entry + ini.substr(mainEnd + 1);
}

} // namespace scummsetup
