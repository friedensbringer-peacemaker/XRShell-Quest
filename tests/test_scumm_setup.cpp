// Host-Test für src/scumm_setup.h (xr.adventure: Sprache und Untertitel in scummvm.ini).
// Sweep: Sprachcodes in Groß/klein × Länder. Gebaut von tests/run-host-tests.sh.

#include "scumm_setup.h"

#include <cstdio>
#include <string>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return s;
}

void testLanguages() {
    const struct { const char* iso; const char* scumm; } expect[] = {
        {"de", "de"}, {"en", "en"}, {"fr", "fr"}, {"it", "it"}, {"es", "es"}, {"nl", "nl"}, {"pl", "pl"},
        {"ru", "ru"}, {"ja", "jp"}, {"ko", "kr"}, {"sv", "se"}, {"cs", "cz"}, {"da", "da"}, {"fi", "fi"},
        {"hu", "hu"}, {"tr", "tr"}, {"nb", "nb"}, {"no", "nb"}, {"xx", ""}, {"", ""}, {"zh", ""}, {"el", ""},
    };
    const char* countries[] = {"", "DE", "at", "CH", "US", "GB", "BR"};
    for (const auto& e : expect)
        for (const char* country : countries) {
            const std::string what = std::string(e.iso) + "-" + country;
            check(scummsetup::scummLanguage(e.iso, country) == e.scumm, what);
            check(scummsetup::scummLanguage(upper(e.iso), country) == e.scumm, what + " (groß)");
        }
    check(scummsetup::scummLanguage("pt", "BR") == "br" && scummsetup::scummLanguage("pt", "br") == "br",
          "Portugiesisch Brasilien");
    check(scummsetup::scummLanguage("pt", "PT") == "pt" && scummsetup::scummLanguage("pt", "") == "pt",
          "Portugiesisch");
}

void testIni() {
    check(scummsetup::iniText("de") == "[scummvm]\nlanguage=de\nsubtitles=true\n", "ini Deutsch");
    check(scummsetup::iniText("") == "[scummvm]\nsubtitles=true\n", "ini ohne Sprache");
    for (const char* lang : {"de", "jp", "br", "nb"}) {
        const std::string t = scummsetup::iniText(lang);
        check(t.rfind("[scummvm]\n", 0) == 0 && t.find("subtitles=true\n") != std::string::npos &&
                  t.find(std::string("language=") + lang + "\n") != std::string::npos,
              std::string("ini ") + lang);
    }
}

void testGuiLanguage() {
    check(scummsetup::guiLanguage("de", "DE") == "de_DE" && scummsetup::guiLanguage("DE", "at") == "de_DE",
          "Menü Deutsch (auch Österreich, Groß/klein)");
    check(scummsetup::guiLanguage("en", "US").empty() && scummsetup::guiLanguage("xx", "").empty(),
          "Englisch/unbekannt: keine Menüsprache");
    check(scummsetup::guiLanguage("pt", "BR") == "pt_BR" && scummsetup::guiLanguage("pt", "PT") == "pt_PT",
          "Portugiesisch je Land");
    check(scummsetup::guiLanguage("no", "") == "nb_NO" && scummsetup::guiLanguage("ja", "JP") == "ja",
          "Norwegisch → nb_NO, Japanisch ohne Land");
    check(scummsetup::iniText("de", "de_DE") == "[scummvm]\nlanguage=de\nsubtitles=true\ngui_language=de_DE\n",
          "neue ini mit Menüsprache");

    // Nachtragen in vorhandene ini (wie sie ScummVM nach dem ersten Lauf schreibt).
    const std::string seen = "[scummvm]\nlastselectedgame=sky\nlanguage=de\nsubtitles=true\n\n[sky]\npath=/x\n";
    const std::string added = scummsetup::ensureGuiLanguage(seen, "de_DE");
    check(added == "[scummvm]\ngui_language=de_DE\nlastselectedgame=sky\nlanguage=de\nsubtitles=true\n\n[sky]\npath=/x\n",
          "gui_language direkt unter [scummvm] nachgetragen");
    check(scummsetup::ensureGuiLanguage(added, "de_DE") == added, "zweiter Lauf ändert nichts");
    const std::string own = "[scummvm]\ngui_language=fr_FR\n";
    check(scummsetup::ensureGuiLanguage(own, "de_DE") == own, "eigene Wahl bleibt");
    const std::string other = "[sky]\ngui_language=xx\n";
    check(scummsetup::ensureGuiLanguage(other, "de_DE") == "[sky]\ngui_language=xx\n[scummvm]\ngui_language=de_DE\n",
          "Eintrag in anderem Abschnitt zählt nicht, Abschnitt wird angehängt");
    check(scummsetup::ensureGuiLanguage("", "de_DE") == "[scummvm]\ngui_language=de_DE\n", "leere ini");
    check(scummsetup::ensureGuiLanguage("[scummvm]", "de_DE") == "[scummvm]\ngui_language=de_DE\n",
          "Kopfzeile ohne Zeilenende");
    check(scummsetup::ensureGuiLanguage("[scummvm]\r\nlanguage=de\r\n", "de_DE") ==
              "[scummvm]\r\ngui_language=de_DE\nlanguage=de\r\n",
          "CRLF-ini: Kopfzeile erkannt");
    check(scummsetup::ensureGuiLanguage(seen, "") == seen, "ohne Menüsprache unverändert");
}

} // namespace

int main() {
    testLanguages();
    testIni();
    testGuiLanguage();
    std::printf("test_scumm_setup: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
