// Host-Test für src/option_store.h und src/dos_options.h (xr.dosgames: Kern-Optionen und Bildwerte
// pro Spiel aus dem VR-Menü). Abhängigkeitsfrei, gebaut von tests/run-host-tests.sh.
//
//  1. Ebenen: Profil (erster Eintrag gilt) < Nutzer, Adapter-Vorgaben nur ohne Angabe
//  2. GET_VARIABLE_UPDATE: genau einmal nach Live-Änderung, nie für Neustart-Optionen/xrshell_*
//  3. Neustart-Optionen: ausstehend bis begin(), Zurücksetzen, Zeiger an den Core bleiben gültig
//  4. Persistenz: Datei schreiben/lesen/löschen, Kommentare, CRLF, Anführungszeichen
//  5. Core-Optionsliste: SET_VARIABLES-Format, Werte-Auswahl und Weiterschalten (Sweep)
//  6. Bildwerte pro Spiel: Rundweg Optionen ↔ GameView (Sweep), Fenstermaße auf cm
//  7. „Größe passend“: Blickwinkel-Sweep über Abstand und Breite
//  8. Threads: Core liest, Main schreibt (kein Absturz, Update kommt an)

#include "dos_options.h"
#include "option_store.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

std::string val(const OptionStore& s, const char* key) {
    std::string v;
    return s.active(key, v) ? v : std::string("<keiner>");
}

void testLayers() {
    OptionStore s;
    const auto profile = optstore::parse(
        "# Kommentar\n"
        "dosbox_pure_cycles = 26800   # GOG 20000\r\n"
        "dosbox_pure_cycles = max\n"                          // doppelt: erster gilt
        "  dosbox_pure_sblaster_conf = \"A220 I7 D1 H5\"\n"
        "kaputt\n= ohne Schlüssel\nleer =\n"
        "xrshell_filter = hd\n");
    check(profile.size() == 4, "Profil: 4 gültige Zeilen (Kommentar/kaputt/leer übersprungen)");
    check(profile[2].second == "A220 I7 D1 H5", "Profil: Anführungszeichen entfernt, Inhalt bleibt");
    const auto user = optstore::parse("dosbox_pure_cycles = 7800\ndosbox_pure_svga = svga_et4000\n");
    s.begin(profile, user);
    s.addDefault("dosbox_pure_mouse_input", "virtual");
    s.addDefault("dosbox_pure_cycles", "auto");           // Spiel/Nutzer sagen schon etwas
    check(val(s, "dosbox_pure_cycles") == "7800", "Nutzer überschreibt Profil");
    check(val(s, "dosbox_pure_mouse_input") == "virtual", "Vorgabe ohne Angabe im Spiel");
    check(val(s, "dosbox_pure_svga") == "svga_et4000", "Nutzerwert ohne Profilwert aktiv");
    std::string p;
    check(s.profile("dosbox_pure_cycles", p) && p == "26800", "Profil: erster Eintrag gilt");
    check(s.get("gibts_nicht") == nullptr, "unbekannter Schlüssel → nullptr (Core-Standard)");
    check(s.hasUserValues() && !s.dirty(), "frisch geladen: Nutzerwerte da, nicht geändert");
    check(!s.takeUpdate(), "frisch geladen: kein Update");
}

void testUpdatesAndPending() {
    OptionStore s;
    s.begin(optstore::parse("dosbox_pure_cycles = max\ndosbox_pure_svga = svga_s3\n"), {});
    const char* before = s.get("dosbox_pure_cycles");
    // Live-Änderung: sofort aktiv, Update genau einmal.
    s.setUser("dosbox_pure_cycles", "13400", dosopt::isLiveKey("dosbox_pure_cycles"));
    check(val(s, "dosbox_pure_cycles") == "13400", "live: sofort aktiv");
    check(s.takeUpdate() && !s.takeUpdate(), "live: Update genau einmal");
    check(std::strcmp(before, "max") == 0, "alter Zeiger an den Core bleibt gültig und unverändert");
    check(std::strcmp(s.get("dosbox_pure_cycles"), "13400") == 0, "neuer Zeiger zeigt neuen Wert");
    check(!s.pending("dosbox_pure_cycles"), "live: nicht ausstehend");
    // Gleicher Wert nochmal: keine Änderung, kein Update.
    s.clearDirty();
    s.setUser("dosbox_pure_cycles", "13400", true);
    check(!s.dirty() && !s.takeUpdate(), "gleicher Wert: weder dirty noch Update");
    // Neustart-Option: bleibt aktiv beim alten Wert, ausstehend, kein Update.
    s.setUser("dosbox_pure_svga", "svga_et4000", dosopt::isLiveKey("dosbox_pure_svga"));
    check(val(s, "dosbox_pure_svga") == "svga_s3", "Neustart-Option: Core sieht noch den alten Wert");
    check(s.pending("dosbox_pure_svga") && !s.takeUpdate(), "Neustart-Option: ausstehend, kein Update");
    std::string eff;
    check(s.effective("dosbox_pure_svga", eff) && eff == "svga_et4000", "nach Neustart gilt der Nutzerwert");
    // xrshell_* (Bildwerte): live, aber nie GET_VARIABLE_UPDATE (Core kennt sie nicht).
    s.setUser("xrshell_filter", "crt", dosopt::isLiveKey("xrshell_filter"));
    check(val(s, "xrshell_filter") == "crt" && !s.takeUpdate(), "xrshell_*: aktiv ohne Core-Update");
    // Neustart: begin() mit gespeicherten Nutzerwerten → alles aktiv, nichts ausstehend.
    const auto saved = s.userEntries();
    s.begin(optstore::parse("dosbox_pure_cycles = max\ndosbox_pure_svga = svga_s3\n"), saved);
    check(val(s, "dosbox_pure_svga") == "svga_et4000" && !s.pending("dosbox_pure_svga"), "nach Neustart aktiv");
    // Zurücksetzen: live zurück zum Profil (mit Update), Neustart-Option ausstehend zurück.
    s.resetUser(dosopt::isLiveKey);
    check(!s.hasUserValues() && s.dirty(), "Zurücksetzen: keine Nutzerwerte mehr, speichern nötig");
    check(val(s, "dosbox_pure_cycles") == "max" && s.takeUpdate(), "Zurücksetzen: live sofort Profilwert + Update");
    check(val(s, "dosbox_pure_svga") == "svga_et4000" && s.pending("dosbox_pure_svga"),
          "Zurücksetzen: Neustart-Option wartet auf den Neustart");
    check(val(s, "xrshell_filter") == "<keiner>", "Zurücksetzen: Bildwert ohne Profil → Standard");
    // Live-Option ohne Profilwert zurücksetzen → wieder Core-Standard.
    s.begin({}, optstore::parse("dosbox_pure_mouse_speed_factor = 2.0\n"));
    s.resetUser(dosopt::isLiveKey);
    check(s.get("dosbox_pure_mouse_speed_factor") == nullptr && s.takeUpdate(), "ohne Profil: Core-Standard + Update");
}

void testPersistence() {
    const std::string path = "xrshell-test-user.opt";
    OptionStore s;
    s.begin({}, {});
    s.setUser("dosbox_pure_cycles", "26800", true);
    s.setUser("xrshell_width", "2.00", true);
    const std::string text = optstore::serialize(s.userEntries(), "# Kopf\n");
    check(optstore::writeFile(path, text), "Nutzerdatei schreiben");
    check(optstore::writeFile(path, text), "Nutzerdatei überschreiben (rename über bestehende Datei)");
    std::string back;
    check(optstore::readFile(path, back) && back == text, "Nutzerdatei lesen = geschrieben");
    const auto entries = optstore::parse(back);
    check(entries.size() == 2 && entries[0].first == "dosbox_pure_cycles" && entries[1].second == "2.00",
          "Rundweg Nutzerwerte");
    std::remove(path.c_str());
    check(!optstore::readFile(path, back), "gelöschte Datei = Spiel-Standard");
    std::FILE* tmp = std::fopen((path + ".tmp").c_str(), "rb");
    check(tmp == nullptr, "keine .tmp-Reste");
    if (tmp) std::fclose(tmp);
}

void testDefinitionsAndChoices() {
    optstore::Definition d;
    check(optstore::parseLegacyDefinition("dosbox_pure_cycles", "Emulated Performance; auto|max|315|26800", d) &&
              d.values.size() == 4 && d.defaultValue == "auto" && d.values[3].first == "26800",
          "SET_VARIABLES: Liste und Standard (erster Wert)");
    check(!optstore::parseLegacyDefinition("x", "ohne Semikolon", d), "SET_VARIABLES ohne Liste abgelehnt");

    // Mausmodus: nur „Laser-genau“/„Relativ“, „pad“/„false“/„auto“ nie.
    const std::vector<std::string> core = {"auto", "true", "virtual", "direct", "pad", "false"};
    const auto mouse = dosopt::choices(dosopt::kMouse, core, "direct");
    check(mouse == std::vector<std::string>({"virtual", "direct"}), "Maus: nur virtual/direct aus der Core-Liste");
    check(dosopt::label(dosopt::kMouse, "direct") == "Laser-genau" && dosopt::label(dosopt::kMouse, "true") == "Relativ",
          "Maus: Beschriftung");
    check(dosopt::stepValue(mouse, "direct", 1) == "virtual" && dosopt::stepValue(mouse, "virtual", 1) == "direct",
          "Maus: Umschalten im Kreis");
    // Seitenverhältnis: „padded“ (Rand, stört „direct“) wird nicht angeboten.
    const auto aspect = dosopt::choices(dosopt::kAspect, {"false", "true", "padded", "doublescan"}, "false");
    check(aspect == std::vector<std::string>({"false", "true"}), "Seitenverhältnis: ohne padded/doublescan");
    // Zyklen: Profilwert außerhalb der Liste wird numerisch einsortiert.
    const auto cycles = dosopt::choices(dosopt::kCycles, {}, "20000");
    const auto at = std::find(cycles.begin(), cycles.end(), "20000") - cycles.begin();
    check(at > 0 && cycles[static_cast<size_t>(at) - 1] == "13400" && cycles[static_cast<size_t>(at) + 1] == "26800",
          "Zyklen: 20000 zwischen 13400 und 26800");
    check(dosopt::label(dosopt::kCycles, "26800") == "486DX2 66 MHz" && dosopt::label(dosopt::kCycles, "20000") == "20000 cps",
          "Zyklen: Beschriftung bekannt/unbekannt");
    // Sweep: jeder Wert jeder Option → ±1 bleibt in der Liste, n Schritte = Ausgangswert.
    for (int i = 0; i < dosopt::kCount; ++i) {
        const auto id = static_cast<dosopt::Id>(i);
        const auto list = dosopt::choices(id, {}, "");
        check(!list.empty(), "Rückfall-Liste nicht leer");
        for (const std::string& v : list) {
            std::string a = v, b = v;
            for (size_t k = 0; k < list.size(); ++k) {
                a = dosopt::stepValue(list, a, 1);
                b = dosopt::stepValue(list, b, -1);
                check(std::find(list.begin(), list.end(), a) != list.end(), "Schritt bleibt in der Liste");
            }
            check(a == v && b == v, "n Schritte vor/zurück = Ausgangswert");
            check(!dosopt::label(id, v).empty() && dosopt::label(id, v).size() < 40, "Beschriftung passt in die Kapsel");
        }
        check(dosopt::stepValue(list, "unbekannt", 1) == list.front(), "unbekannter Wert → erster");
    }
    check(dosopt::isLiveKey("dosbox_pure_cycles") && dosopt::isLiveKey("dosbox_pure_mouse_input") &&
              dosopt::isLiveKey("dosbox_pure_aspect_correction") && !dosopt::isLiveKey("dosbox_pure_svga") &&
              !dosopt::isLiveKey("dosbox_pure_memory_size") && dosopt::isLiveKey("xrshell_contrast"),
          "live/Neustart-Einteilung");
}

void testGameView() {
    // Sweep: Bildwerte → Nutzer-Optionen → Bildwerte (Rundweg) über alle Filter und Stufen.
    int cases = 0;
    for (int f = 0; f < kScreenFilterCount; ++f)
        for (int sat = 0; sat <= 200; sat += 40)
            for (int con = 50; con <= 150; con += 50)
                for (int curve = 0; curve < 4; ++curve)
                    for (float w : {1.0f, 2.0f, 2.4f, 6.0f}) {
                        OptionStore s;
                        s.begin({}, {});
                        auto lookup = [&](const char* key, std::string& out) { return s.active(key, out); };
                        const dosopt::GameView before = dosopt::gameViewFrom(lookup, 4);   // Spielstart ohne Angaben
                        dosopt::GameView after = before;
                        after.filter = f; after.saturation = sat; after.contrast = con; after.curve = curve;
                        after.width = w; after.distance = 2.0f;
                        for (const auto& kv : dosopt::gameViewChanges(before, after)) s.setUser(kv.first, kv.second, true);
                        const dosopt::GameView back = dosopt::gameViewFrom(lookup, 4);
                        check(back.filter == f && back.saturation == sat && back.contrast == con && back.curve == curve &&
                                  std::fabs(back.width - w) < 0.006f && std::fabs(back.distance - 2.0f) < 0.006f,
                              "Bildwerte-Rundweg");
                        check(dosopt::gameViewChanges(back, back).empty(), "keine Änderung → keine Option");
                        ++cases;
                    }
    check(cases == 6 * 6 * 3 * 4 * 4, "Sweep vollständig");
    // Zeigerverschiebung pro Spiel: Schritte → Prozent-Option → Schritte, ganzer Bereich.
    for (int ox = -60; ox <= 60; ox += 7)
        for (int oy : {-60, -1, 0, 1, 60}) {
            OptionStore s;
            s.begin({}, {});
            auto lookup = [&](const char* key, std::string& out) { return s.active(key, out); };
            const dosopt::GameView before = dosopt::gameViewFrom(lookup, 4);
            check(before.offsetX == 0 && before.offsetY == 0, "ohne Angabe keine Verschiebung");
            dosopt::GameView after = before;
            after.offsetX = ox; after.offsetY = oy;
            for (const auto& kv : dosopt::gameViewChanges(before, after)) s.setUser(kv.first, kv.second, true);
            const dosopt::GameView back = dosopt::gameViewFrom(lookup, 4);
            check(back.offsetX == ox && back.offsetY == oy, "Zeigerverschiebung-Rundweg");
        }
    {
        OptionStore s;   // Vorgabe im Spielprofil (xrshell.opt), außerhalb des Bereichs geklemmt
        s.begin(optstore::parse("xrshell_pointer_offset_x = -1.25\nxrshell_pointer_offset_y = 40\n"), {});
        auto lookup = [&](const char* key, std::string& out) { return s.active(key, out); };
        const dosopt::GameView v = dosopt::gameViewFrom(lookup, 4);
        check(v.offsetX == -5 && v.offsetY == 60, "Zeigerverschiebung aus xrshell.opt");
    }
    // Profil-Standard (xrshell_filter in xrshell.opt), Nutzer überschreibt, App-Standard ohne Angabe.
    OptionStore s;
    s.begin(optstore::parse("xrshell_filter = crt\nxrshell_saturation = 999\nxrshell_width = 9\n"), {});
    auto lookup = [&](const char* key, std::string& out) { return s.active(key, out); };
    dosopt::GameView v = dosopt::gameViewFrom(lookup, 4);
    check(v.filter == 3 && v.saturation == 200 && v.width == 0.0f, "Profil: Filter, geklemmt, ungültige Breite ignoriert");
    s.setUser("xrshell_filter", "scharf", true);
    check(dosopt::gameViewFrom(lookup, 4).filter == 0, "Nutzerfilter vor Profilfilter");
    s.begin({}, {});
    check(dosopt::gameViewFrom(lookup, 4).filter == 4 && dosopt::gameViewFrom(lookup, 4).width == 0.0f,
          "ohne Angabe: App-Standard (HD), Fenster bleibt");
    // Fenstermaße: Kopfbewegung < 5 mm erzeugt keinen neuen Wert.
    dosopt::GameView a, b;
    a.width = b.width = 2.0f; a.distance = 2.0f; b.distance = 2.004f;
    check(dosopt::gameViewChanges(a, b).empty(), "Fenster: < 5 mm keine Änderung");
    b.distance = 2.1f;
    check(dosopt::gameViewChanges(a, b).size() == 2, "Fenster: Breite und Abstand zusammen gespeichert");
    for (int f = 0; f < kScreenFilterCount; ++f) {
        int back = -1;
        check(parseScreenFilter(screenFilterKey(f), back) && back == f, "Filterschlüssel ↔ Name");
    }
}

void testFitSize() {
    for (float d = 0.7f; d <= 3.5f; d += 0.1f)
        for (int w : {320, 360, 640, 800, 1024}) {
            const float width = dosopt::fitWidth(w, d);
            check(width >= 1.0f && width <= 6.0f, "passend: 1–6 m");
            const float deg = 2.0f * std::atan(width / (2.0f * d)) * 180.0f / 3.14159265f;
            if (width > 1.0f && width < 6.0f)
                check(std::fabs(deg - dosopt::fitAngleDegrees(w)) < 3.5f, "passend: Blickwinkel getroffen (±3,5°)");
        }
    check(std::fabs(dosopt::fitWidth(320, 2.0f) - 2.0f) < 0.05f, "320×200 bei 2 m Abstand ≈ 2,0 m breit");
}

void testThreads() {
    OptionStore s;
    s.begin(optstore::parse("dosbox_pure_cycles = max\n"), {});
    std::atomic<bool> stop{false};
    std::atomic<int> updates{0}, reads{0};
    std::thread core([&] {
        while (!stop.load()) {
            const char* v = s.get("dosbox_pure_cycles");
            if (v && v[0]) reads.fetch_add(1);
            if (s.takeUpdate()) updates.fetch_add(1);
        }
    });
    const char* values[] = {"auto", "max", "7800", "26800"};
    for (int i = 0; i < 2000; ++i) s.setUser("dosbox_pure_cycles", values[i % 4], true);
    while (reads.load() < 1000) std::this_thread::yield();
    stop.store(true);
    core.join();
    if (s.takeUpdate()) updates.fetch_add(1);
    check(updates.load() >= 1 && reads.load() >= 1000, "Threads: Core liest, Updates kommen an");
    check(val(s, "dosbox_pure_cycles") == "26800", "Threads: letzter Wert gilt");
}

// Revision: jede Änderung eines aktiven Werts zählt, Lesen und reine Nutzer-/Pending-Änderungen
// nicht (Core-Thread liest den Mausmodus nur bei neuer Revision).
void testRevision() {
    OptionStore s;
    const unsigned r0 = s.revision();
    s.begin({{"dosbox_pure_mouse_input", "virtual"}}, {});
    const unsigned r1 = s.revision();
    check(r1 != r0, "Revision: begin zählt");
    std::string v;
    s.active("dosbox_pure_mouse_input", v);
    (void)s.get("dosbox_pure_mouse_input");
    check(s.revision() == r1, "Revision: Lesen zählt nicht");
    s.addDefault("dosbox_pure_mouse_input", "pad");
    check(s.revision() == r1, "Revision: Vorgabe für gesetzten Schlüssel zählt nicht");
    s.addDefault("dosbox_pure_cycles", "max");
    const unsigned r2 = s.revision();
    check(r2 != r1, "Revision: neue Vorgabe zählt");
    s.setUser("dosbox_pure_mouse_input", "direct", false);
    check(s.revision() == r2, "Revision: Wert erst nach Neustart (nicht live) zählt nicht");
    s.setUser("dosbox_pure_mouse_input", "true", true);
    const unsigned r3 = s.revision();
    check(r3 != r2 && s.active("dosbox_pure_mouse_input", v) && v == "true", "Revision: Live-Änderung zählt");
    s.setUser("dosbox_pure_mouse_input", "true", true);
    check(s.revision() == r3, "Revision: gleicher Wert zählt nicht");
    s.resetUser([](const std::string&) { return true; });
    check(s.revision() != r3 && s.active("dosbox_pure_mouse_input", v) && v == "virtual",
          "Revision: Zurücksetzen auf Profil zählt");
    const unsigned r4 = s.revision();
    s.setUser("xrshell_log_pointer", "1", true);
    check(s.revision() != r4, "Revision: xrshell_-Schlüssel zählen auch (XRShell liest sie selbst)");
    const unsigned r5 = s.revision();
    s.resetUser([](const std::string&) { return true; });
    check(s.revision() != r5 && !s.active("xrshell_log_pointer", v), "Revision: Löschen (Core-Standard) zählt");
}

} // namespace

int main() {
    testLayers();
    testUpdatesAndPending();
    testPersistence();
    testDefinitionsAndChoices();
    testGameView();
    testFitSize();
    testThreads();
    testRevision();
    std::printf("test_option_store: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
