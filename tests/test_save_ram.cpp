// Host-Test für src/save_ram.h (Batteriespeicher als <Spiel>.srm). Abhängigkeitsfrei,
// gebaut von tests/run-host-tests.sh.
//
//  1. Neues Spiel ohne Datei: leerer Speicher → keine Datei; erster echter Spielstand → Datei
//  2. Rundweg: schreiben, neu „starten“, Inhalt kommt im Core-Speicher an
//  3. Nur bei Änderung schreiben (flush liefert 0), Änderung an jeder Stelle wird erkannt (Sweep)
//  4. Falsche Dateigröße: Core-Speicher bleibt unangetastet, keine Überschreibung beim Laden
//  5. Datei existiert: auch ein „gelöschter“ (leerer) Stand wird geschrieben
//  6. Atomar: keine .tmp-Reste, unbeschreibbarer Pfad → -1, Tracker bleibt dirty
//  7. Größen-Sweep (1 B … 128 KB, übliche Speichergrößen) und beide Leer-Muster (0x00/0xFF)

#include "save_ram.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>
#include <unistd.h>   // getpid

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

bool exists(const std::string& p) { return ::access(p.c_str(), F_OK) == 0; }

std::string tmpPath(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    if (!dir) dir = std::getenv("TEMP");
    if (!dir) dir = "/tmp";
    // Eindeutig pro Lauf: zwei Sitzungen dürfen die Host-Tests gleichzeitig laufen lassen.
    static const std::string run = std::to_string(std::random_device{}());
    return std::string(dir) + "/xrshell_test_" + run + "_" + name + ".srm";
}

void testFreshGame() {
    const std::string p = tmpPath("fresh");
    std::remove(p.c_str());
    std::vector<uint8_t> mem(8192, 0x00);
    saveram::Tracker t;
    check(!t.begin(p, mem.data(), mem.size()), "fresh: keine Datei geladen");
    check(t.flush(mem.data(), mem.size()) == 0, "fresh: unveränderter Start schreibt nichts");
    check(!exists(p), "fresh: keine Datei angelegt");
    mem[100] = 0x42;
    check(t.flush(mem.data(), mem.size()) == 1, "fresh: Spielstand geschrieben");
    check(exists(p), "fresh: Datei existiert");
    check(!exists(p + ".tmp"), "fresh: keine .tmp-Reste");
    check(t.flush(mem.data(), mem.size()) == 0, "fresh: zweites flush ohne Änderung");
    std::remove(p.c_str());
}

void testRoundTrip() {
    const std::string p = tmpPath("roundtrip");
    std::remove(p.c_str());
    std::vector<uint8_t> mem(32768);
    for (size_t i = 0; i < mem.size(); ++i) mem[i] = static_cast<uint8_t>(i * 7 + 3);
    saveram::Tracker a;
    std::vector<uint8_t> start(mem.size(), 0xFF);
    a.begin(p, start.data(), start.size());
    check(a.flush(mem.data(), mem.size()) == 1, "roundtrip: geschrieben");

    std::vector<uint8_t> reload(mem.size(), 0xFF);
    saveram::Tracker b;
    check(b.begin(p, reload.data(), reload.size()), "roundtrip: geladen");
    check(reload == mem, "roundtrip: Inhalt gleich");
    check(!b.dirty(reload.data(), reload.size()), "roundtrip: nach Laden nicht dirty");
    std::remove(p.c_str());
}

void testChangeSweep() {
    const std::string p = tmpPath("sweep");
    std::remove(p.c_str());
    std::vector<uint8_t> mem(2048, 0x11);
    saveram::Tracker t;
    t.begin(p, mem.data(), mem.size());
    check(t.flush(mem.data(), mem.size()) == 0, "sweep: Startzustand des Cores gilt als gesichert");
    int missed = 0;
    for (size_t i = 0; i < mem.size(); i += 13) {
        mem[i] ^= 0x80;
        if (t.flush(mem.data(), mem.size()) != 1) ++missed;
        if (t.flush(mem.data(), mem.size()) != 0) ++missed;
    }
    check(missed == 0, "sweep: jede Änderung genau einmal geschrieben");
    std::vector<uint8_t> back = saveram::readExact(p, mem.size());
    check(back == mem, "sweep: Datei = letzter Stand");
    std::remove(p.c_str());
}

void testWrongSize() {
    const std::string p = tmpPath("size");
    std::vector<uint8_t> other(1000, 0x5A);
    check(saveram::writeAtomic(p, other.data(), other.size()), "size: Fremddatei angelegt");
    std::vector<uint8_t> mem(8192, 0x00);
    mem[0] = 9;
    saveram::Tracker t;
    check(!t.begin(p, mem.data(), mem.size()), "size: falsche Größe nicht geladen");
    check(mem[0] == 9 && mem[1] == 0, "size: Core-Speicher unverändert");
    check(saveram::readExact(p, 1000).size() == 1000, "size: Fremddatei unverändert");
    check(saveram::readExact(p, 999).empty(), "size: längere Datei abgelehnt");
    check(saveram::readExact(p, 1001).empty(), "size: kürzere Datei abgelehnt");
    std::remove(p.c_str());
}

void testClearedSave() {
    const std::string p = tmpPath("cleared");
    std::vector<uint8_t> mem(8192, 0x00);
    mem[5] = 1;
    check(saveram::writeAtomic(p, mem.data(), mem.size()), "cleared: Datei angelegt");
    saveram::Tracker t;
    std::vector<uint8_t> core(8192, 0x00);
    check(t.begin(p, core.data(), core.size()), "cleared: geladen");
    core[5] = 0;   // Spiel löscht seinen Stand
    check(t.flush(core.data(), core.size()) == 1, "cleared: leerer Stand bei vorhandener Datei geschrieben");
    check(saveram::readExact(p, core.size()) == core, "cleared: Datei leer");
    std::remove(p.c_str());
}

void testUnwritable() {
    const std::string p = tmpPath("x") + ".dir/does/not/exist.srm";
    std::vector<uint8_t> mem(64, 0x00);
    saveram::Tracker t;
    t.begin(p, mem.data(), mem.size());
    mem[1] = 3;
    check(t.flush(mem.data(), mem.size()) == -1, "unwritable: Fehler gemeldet");
    check(t.dirty(mem.data(), mem.size()), "unwritable: bleibt dirty (nächster Versuch)");
}

void testSizeAndBlankSweep() {
    const size_t sizes[] = {1, 2, 64, 512, 2048, 8192, 32768, 65536, 131072};
    for (size_t size : sizes) {
        for (uint8_t blank : {uint8_t(0x00), uint8_t(0xFF)}) {
            const std::string p = tmpPath("sizes");
            std::remove(p.c_str());
            std::vector<uint8_t> mem(size, blank);
            check(saveram::isBlank(mem.data(), size), "blank: " + std::to_string(size));
            saveram::Tracker t;
            t.begin(p, mem.data(), size);
            check(t.flush(mem.data(), size) == 0, "blank: keine Datei " + std::to_string(size));
            mem[size - 1] = static_cast<uint8_t>(blank ^ 1);
            check(!saveram::isBlank(mem.data(), size), "nicht leer: " + std::to_string(size));
            check(t.flush(mem.data(), size) == 1, "geschrieben: " + std::to_string(size));
            std::vector<uint8_t> reload(size, 0);
            saveram::Tracker r;
            check(r.begin(p, reload.data(), size) && reload == mem, "rundweg: " + std::to_string(size));
            std::remove(p.c_str());
        }
    }
    check(saveram::isBlank(nullptr, 0), "blank: Größe 0");
    saveram::Tracker none;
    check(!none.begin("", nullptr, 0), "kein Speicher: begin");
    check(none.flush(nullptr, 0) == 0, "kein Speicher: flush");
}

// 8. Ablageort: System trennt gleichnamige Spiele, Ordner behalten Punkte, Endung nur bei Dateien
void testPaths() {
    using saveram::basePath;
    check(basePath("/s", "doom", "/g/games/doom/Spiel.wad", false) == "/s/doom/Spiel", "Doom-Datei");
    check(basePath("/s", "quake", "/g/games/quake/Spiel.pak", false) == "/s/quake/Spiel", "Quake-Datei (eigene Datei)");
    check(basePath("/s", "doom", "/g/games/doom/Spiel.wad", false) !=
          basePath("/s", "quake", "/g/games/quake/Spiel.pak", false), "gleicher Name, anderes System → getrennt");
    check(basePath("/s", "doom", "/g/games/doom/Doom v1.9", true) == "/s/doom/Doom v1.9", "Ordner mit Punkt bleibt");
    check(basePath("/s", "doom", "/g/games/doom/Doom v1.9", true) !=
          basePath("/s", "doom", "/g/games/doom/Doom v1.666", true), "Doom v1.9 ≠ Doom v1.666");
    check(basePath("/s", "dos", "/g/game/ThemePark/", true) == "/s/dos/ThemePark", "Ordner mit Schrägstrich");
    check(basePath("/s", "", "/g/game/sky.scummvm", false) == "/s/sky", "ohne System: direkt in saves");
    check(basePath("/s", "doom", "/g/x/.wad", false) == "/s/doom/.wad", "nur Endung: Name bleibt");
    check(basePath("/s", "doom", "", false) == "/s/doom/spiel", "leer → spiel");
    check(saveram::legacyBasePath("/s", "/g/games/doom/Doom v1.9") == "/s/Doom v1", "alter Ort (Punkt abgeschnitten)");
    check(saveram::legacyBasePath("/s", "/g/games/doom/Spiel.wad") == "/s/Spiel", "alter Ort ohne System");
}

// 9. Übernahme vom alten Ort: verschiebt nur, was am neuen Ort fehlt, überschreibt nie
void testMigrate() {
    namespace fs = std::filesystem;
    std::random_device rd;
    const fs::path dir = fs::temp_directory_path() / ("xrshell-test-saves-" + std::to_string(rd()));
    fs::create_directories(dir / "doom");
    const std::string from = (dir / "Spiel").string(), to = (dir / "doom" / "Spiel").string();
    auto put = [](const std::string& path, const char* text) {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (f) { std::fputs(text, f); std::fclose(f); }
    };
    auto read = [](const std::string& path) {
        std::string out;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            char buf[64];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
            std::fclose(f);
        }
        return out;
    };
    put(from + ".srm", "alt-srm");
    put(from + ".state1", "alt-1");
    put(from + ".state2", "alt-2");
    put(to + ".state2", "neu-2");   // am neuen Ort schon da → bleibt
    const int moved = saveram::migrate(from, to, {".srm", ".state1", ".state2", ".state3"});
    check(moved == 2, "Übernahme: zwei Dateien verschoben");
    check(read(to + ".srm") == "alt-srm" && read(to + ".state1") == "alt-1", "Übernahme: Inhalt am neuen Ort");
    check(read(to + ".state2") == "neu-2", "Übernahme: vorhandene Datei nicht überschrieben");
    check(exists(from + ".state2") && !exists(from + ".srm"), "Übernahme: nicht übernommene Datei bleibt am alten Ort");
    check(saveram::migrate(from, to, {".srm"}) == 0, "Übernahme: zweiter Lauf tut nichts");
    check(saveram::migrate(to, to, {".srm"}) == 0, "Übernahme: gleicher Ort tut nichts");
    std::error_code ec;
    fs::remove_all(dir, ec);
    check(!ec, "Testordner aufgeräumt");
}

}  // namespace

int main() {
    testPaths();
    testMigrate();
    testFreshGame();
    testRoundTrip();
    testChangeSweep();
    testWrongSize();
    testClearedSave();
    testUnwritable();
    testSizeAndBlankSweep();
    std::printf("test_save_ram: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
