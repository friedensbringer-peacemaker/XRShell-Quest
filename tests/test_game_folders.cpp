// Host-Test für src/game_folders.cpp (Spielauswahl xr.dosgames/xr.shooter): Ordnersuche,
// Titel aus xrshell.title, Sortierung, Startdatei-Regeln. Gebaut und ausgeführt von
// tests/run-host-tests.sh.
//
// Sweeps: Titeldatei mit LF/CRLF/ohne Zeilenende × mit/ohne UTF-8-BOM × führende
// Leerzeilen/Leerzeichen; überlange Titel mit Umlauten an jeder Schnittposition;
// Doom-IWAD-Priorität über alle Teilmengen der IWAD-Namen × Groß/Kleinschreibung ×
// Ablage direkt/in base/ bzw. rerelease/; Wolf-Endungen; Quake id1/pak0.pak.

#include "game_folders.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <random>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FEHLER: %s\n", what.c_str());
    }
}

void writeFile(const fs::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary);
    f << text;
}

bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

// --- 1. Titel: Datei-Varianten ---
void testTitleSweep(const fs::path& tmp) {
    const fs::path game = tmp / "titel" / "PiratesGold";
    fs::create_directories(game);
    check(folderGameTitle(game.generic_string()) == "PiratesGold", "Titel: ohne Datei = Ordnername");
    const char* eols[] = {"\n", "\r\n", ""};
    const char* boms[] = {"", "\xEF\xBB\xBF"};
    const char* leads[] = {"", "\n", "\r\n\r\n", "   ", "\t \n  "};
    for (const char* eol : eols)
        for (const char* bom : boms)
            for (const char* lead : leads) {
                // Ohne Zeilenende: nur eine Zeile, sonst folgt eine zweite (wird ignoriert).
                writeFile(game / "xrshell.title", std::string(bom) + lead + "Pirates! Gold  " + eol +
                                                  (*eol ? std::string("zweite Zeile") + eol : ""));
                const std::string t = folderGameTitle(game.generic_string());
                check(t == "Pirates! Gold", "Titel-Sweep: '" + t + "'");
            }
    writeFile(game / "xrshell.title", "\n  \r\n");
    check(folderGameTitle(game.generic_string()) == "PiratesGold", "Titel: nur Leerzeilen = Ordnername");
    writeFile(game / "xrshell.title", "\xEF\xBB\xBF");
    check(folderGameTitle(game.generic_string()) == "PiratesGold", "Titel: nur BOM = Ordnername");
}

// --- 2. Überlange Titel: auf 80 Byte gekürzt, nie ein halbes UTF-8-Zeichen ---
void testLongTitles(const fs::path& tmp) {
    const fs::path game = tmp / "lang" / "Spiel";
    fs::create_directories(game);
    for (int prefix = 70; prefix <= 82; ++prefix) {
        std::string title(prefix, 'x');
        for (int i = 0; i < 10; ++i) title += "\xC3\xA4\xE2\x80\xA6";   // „ä…“ (2 + 3 Byte)
        writeFile(game / "xrshell.title", title + "\n");
        const std::string t = folderGameTitle(game.generic_string());
        check(t.size() <= 80 && t.size() >= 76, "Langer Titel: Länge " + std::to_string(t.size()) +
                                                    " bei Präfix " + std::to_string(prefix));
        check(validUtf8(t), "Langer Titel: gültiges UTF-8 bei Präfix " + std::to_string(prefix));
    }
}

// --- 3. Ordnersuche: nur Ordner, keine versteckten, sortiert nach Titel ---
void testScan(const fs::path& tmp) {
    const fs::path dos = tmp / "games" / "dos";
    check(scanGameFolders((tmp / "fehlt").generic_string()).empty(), "Suche: fehlender Ordner = leer");
    fs::create_directories(dos);
    check(scanGameFolders(dos.generic_string()).empty(), "Suche: leerer Ordner = leer");
    for (const char* d : {"SyndicateWars", "Populous2", "Populous", "PiratesGold", "Pirates", "SimCity2000",
                          ".versteckt", "zzz"})
        fs::create_directories(dos / d);
    writeFile(dos / "lose-datei.exe", "MZ");
    writeFile(dos / "xrshell.keys", "a = R\n");
    writeFile(dos / "PiratesGold" / "xrshell.title", "Pirates! Gold\n");
    writeFile(dos / "Pirates" / "xrshell.title", "Pirates! (1987)\r\n");
    writeFile(dos / "Populous2" / "xrshell.title", "Populous II\n");
    writeFile(dos / "zzz" / "xrshell.title", "aaa kleingeschrieben\n");
    const auto games = scanGameFolders(dos.generic_string());
    std::vector<std::string> titles;
    for (const auto& g : games) titles.push_back(g.title);
    const std::vector<std::string> expect = {"aaa kleingeschrieben", "Pirates! (1987)", "Pirates! Gold",
                                             "Populous", "Populous II", "SimCity2000", "SyndicateWars"};
    check(titles == expect, "Suche: Titel und Reihenfolge (Groß/klein egal)");
    bool pathsOk = games.size() == expect.size();
    for (const auto& g : games)
        pathsOk = pathsOk && g.path.rfind(dos.generic_string() + "/", 0) == 0 && fs::is_directory(g.path);
    check(pathsOk, "Suche: Pfade zeigen auf die Spielordner");
    // Gleicher Titel in zwei Ordnern: beide gelistet, nach Pfad geordnet.
    writeFile(dos / "SimCity2000" / "xrshell.title", "Populous\n");
    const auto twice = scanGameFolders(dos.generic_string());
    int same = 0;
    for (const auto& g : twice) same += g.title == "Populous";
    check(same == 2, "Suche: doppelte Titel bleiben beide");
}

// --- 4. Startdatei-Regeln (xr.shooter) ---
std::string nameOf(const std::string& path) {
    return path.substr(path.find_last_of('/') + 1);
}

std::string relOf(const fs::path& base, const std::string& path) {
    const std::string b = base.generic_string() + "/";
    return path.rfind(b, 0) == 0 ? path.substr(b.size()) : "<" + path + ">";
}

void testDoomRule(const fs::path& tmp) {
    // Priorität laut game_folders.h
    const std::vector<std::string> prio = {"doom2.wad", "doom.wad", "doomu.wad", "tnt.wad",
                                           "plutonia.wad", "doom1.wad", "freedoom2.wad",
                                           "freedoom1.wad", "freedm.wad"};
    const int n = static_cast<int>(prio.size());
    int caseNo = 0;
    for (int subset = 1; subset < (1 << n); ++subset) {
        for (int where = 0; where < 3; ++where) {   // direkt, base/, rerelease/
            const fs::path game = tmp / "doom" / ("g" + std::to_string(caseNo++));
            const fs::path dir = where == 0 ? game : game / (where == 1 ? "base" : "rerelease");
            fs::create_directories(dir);
            writeFile(game / "readme.txt", "x");
            writeFile(dir / "SIGIL.WAD", "PWAD");   // PWAD wird nie gewählt
            std::string expect;
            for (int i = 0; i < n; ++i) {
                if (!(subset & (1 << i))) continue;
                // Groß/klein wechselt je Fall: DOOM2.WAD, Doom2.wad, doom2.wad
                std::string name = prio[i];
                if ((subset + i) % 3 == 0)
                    for (auto& c : name) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                else if ((subset + i) % 3 == 1)
                    name[0] = static_cast<char>(toupper(static_cast<unsigned char>(name[0])));
                writeFile(dir / name, "IWAD");
                if (expect.empty()) expect = name;
            }
            const std::string got = findStartFile(game.generic_string(), StartRule::DoomIwad);
            const std::string wantRel = std::string(where == 0 ? "" : where == 1 ? "base/" : "rerelease/") + expect;
            check(relOf(game, got) == wantRel, "Doom-Regel Teilmenge " + std::to_string(subset) +
                                                   " Ort " + std::to_string(where) + ": '" +
                                                   relOf(game, got) + "' statt '" + wantRel + "'");
            fs::remove_all(game);
        }
    }
    // Direkt im Ordner geht vor Unterordnern, base/ vor rerelease/ (alphabetisch).
    const fs::path game = tmp / "doom" / "steam";
    fs::create_directories(game / "base");
    fs::create_directories(game / "rerelease");
    writeFile(game / "base" / "DOOM.WAD", "IWAD");
    writeFile(game / "rerelease" / "DOOM2.WAD", "IWAD");
    check(relOf(game, findStartFile(game.generic_string(), StartRule::DoomIwad)) == "base/DOOM.WAD",
          "Doom-Regel: base/ vor rerelease/");
    writeFile(game / "TNT.WAD", "IWAD");
    check(relOf(game, findStartFile(game.generic_string(), StartRule::DoomIwad)) == "TNT.WAD",
          "Doom-Regel: direkt vor Unterordner");
    // xrshell.start wählt eine andere Datei (Final Doom: Plutonia neben TNT).
    writeFile(game / "PLUTONIA.WAD", "IWAD");
    for (const char* line : {"PLUTONIA.WAD\n", "\xEF\xBB\xBF  PLUTONIA.WAD \r\n", "\n/PLUTONIA.WAD"}) {
        writeFile(game / "xrshell.start", line);
        check(relOf(game, findStartFile(game.generic_string(), StartRule::DoomIwad)) == "PLUTONIA.WAD",
              std::string("xrshell.start: ") + line);
    }
    writeFile(game / "xrshell.start", "rerelease\\DOOM2.WAD\n");
    check(relOf(game, findStartFile(game.generic_string(), StartRule::DoomIwad)) == "rerelease/DOOM2.WAD",
          "xrshell.start: Backslash-Pfad");
    for (const char* bad : {"FEHLT.WAD\n", "../steam/TNT.WAD\n", "\n\n", "base\n"}) {
        writeFile(game / "xrshell.start", bad);
        check(relOf(game, findStartFile(game.generic_string(), StartRule::DoomIwad)) == "TNT.WAD",
              std::string("xrshell.start ungültig → Regel: ") + bad);
    }
    // Ohne IWAD: leer; fehlender Ordner/keine Regel: leer.
    const fs::path empty = tmp / "doom" / "leer";
    fs::create_directories(empty / "base");
    writeFile(empty / "base" / "SIGIL.WAD", "PWAD");
    check(findStartFile(empty.generic_string(), StartRule::DoomIwad).empty(), "Doom-Regel: ohne IWAD leer");
    check(findStartFile((tmp / "fehlt").generic_string(), StartRule::DoomIwad).empty(), "fehlender Ordner leer");
    check(findStartFile(game.generic_string(), StartRule::None).empty(), "StartRule::None leer");
}

void testWolfRule(const fs::path& tmp) {
    const fs::path game = tmp / "wolf3d" / "w";
    fs::create_directories(game);
    check(findStartFile(game.generic_string(), StartRule::WolfData).empty(), "Wolf: leer");
    writeFile(game / "WOLF3D.EXE", "MZ");
    writeFile(game / "AUDIOHED.WL1", "x");
    check(nameOf(findStartFile(game.generic_string(), StartRule::WolfData)) == "AUDIOHED.WL1",
          "Wolf: erste .wl1 ohne vswap");
    writeFile(game / "VSWAP.WL1", "x");
    check(nameOf(findStartFile(game.generic_string(), StartRule::WolfData)) == "VSWAP.WL1",
          "Wolf: vswap.wl1 vor anderen .wl1");
    writeFile(game / "audiohed.sod", "x");
    check(nameOf(findStartFile(game.generic_string(), StartRule::WolfData)) == "audiohed.sod",
          "Wolf: .sod vor .wl1");
    writeFile(game / "GAMEMAPS.WL6", "x");
    writeFile(game / "vswap.wl6", "x");
    check(nameOf(findStartFile(game.generic_string(), StartRule::WolfData)) == "vswap.wl6",
          "Wolf: vswap.wl6 zuerst");
    fs::create_directories(game / "sub.wl6");   // Ordner zählen nicht
    check(nameOf(findStartFile(game.generic_string(), StartRule::WolfData)) == "vswap.wl6",
          "Wolf: Ordner mit Endung ignoriert");
}

void testQuakeRule(const fs::path& tmp) {
    const fs::path game = tmp / "quake" / "q";
    fs::create_directories(game);
    writeFile(game / "pak0.pak", "PACK");   // direkt im Ordner reicht nicht (TyrQuake braucht id1/)
    check(findStartFile(game.generic_string(), StartRule::QuakePak).empty(), "Quake: ohne id1 leer");
    fs::create_directories(game / "ID1");
    check(findStartFile(game.generic_string(), StartRule::QuakePak).empty(), "Quake: leeres id1 leer");
    writeFile(game / "ID1" / "PAK1.PAK", "PACK");
    check(findStartFile(game.generic_string(), StartRule::QuakePak).empty(), "Quake: nur pak1 leer");
    writeFile(game / "ID1" / "PAK0.PAK", "PACK");
    check(relOf(game, findStartFile(game.generic_string(), StartRule::QuakePak)) == "ID1/PAK0.PAK",
          "Quake: ID1/PAK0.PAK (Großschreibung)");
}

// xr.adventure: erste *.scummvm im Ordner (Endung Groß/klein egal), Unterordner zählen nicht.
void testScummRule(const fs::path& tmp) {
    const fs::path game = tmp / "adventure" / "a";
    fs::create_directories(game / "sub");
    writeFile(game / "sub" / "aaa.scummvm", "sky");
    writeFile(game / "SKY.DSK", "x");
    check(findStartFile(game.generic_string(), StartRule::ScummTarget).empty(), "Scumm: ohne .scummvm leer");
    fs::create_directories(game / "ordner.scummvm");   // Ordner mit Endung zählt nicht
    check(findStartFile(game.generic_string(), StartRule::ScummTarget).empty(), "Scumm: Ordner ignoriert");
    writeFile(game / "sky.ScummVM", "sky");
    check(nameOf(findStartFile(game.generic_string(), StartRule::ScummTarget)) == "sky.ScummVM",
          "Scumm: Endung Groß/klein egal");
    writeFile(game / "Beneath.scummvm", "sky");
    check(nameOf(findStartFile(game.generic_string(), StartRule::ScummTarget)) == "Beneath.scummvm",
          "Scumm: alphabetisch erste");
    writeFile(game / "xrshell.start", "sky.ScummVM\n");
    check(nameOf(findStartFile(game.generic_string(), StartRule::ScummTarget)) == "sky.ScummVM",
          "Scumm: xrshell.start geht vor");
}

} // namespace

int main() {
    // Eigener Name pro Lauf: zwei Sitzungen dürfen die Host-Tests gleichzeitig laufen lassen.
    const fs::path tmp = fs::temp_directory_path() /
        ("xrshell-test-game-folders-" + std::to_string(std::random_device{}()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    testTitleSweep(tmp);
    testLongTitles(tmp);
    testScan(tmp);
    testDoomRule(tmp);
    testWolfRule(tmp);
    testQuakeRule(tmp);
    testScummRule(tmp);
    fs::remove_all(tmp);
    std::printf("test_game_folders: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
