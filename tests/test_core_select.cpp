// Host-Test für src/core_select.h: Core-Wahl nach Endung und Bibliotheksordner.
// Kein Android nötig:
//
//   clang++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_core_select.cpp -o test && ./test
//
// Parameter-Sweep: jede Endung × Schreibweise × Pfadform, dazu Bibliothekspfade.

#include "core_select.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace core_select;

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FEHLER: %s\n", what.c_str());
    }
}

const char* name(const CoreSpec* core) { return core ? core->soName : "(keiner)"; }

// Schreibweisen einer Endung: klein, GROSS, Gemischt.
std::vector<std::string> spellings(const std::string& ext) {
    std::string upper = ext, mixed = ext;
    for (auto& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (!mixed.empty()) mixed[0] = static_cast<char>(toupper(static_cast<unsigned char>(mixed[0])));
    return {ext, upper, mixed};
}

// Pfadformen ohne Bibliotheksbedeutung.
const char* const kPrefixes[] = {"", "/sdcard/Android/data/xr.shell/files/game/",
                                 "/storage/emulated/0/Download/sub/", "rel/dir/"};

struct ExtCase {
    const char* ext;
    const CoreSpec* core;   // coreForFile(name)
};

} // namespace

int main() {
    // --- 1. Endung → Core (DOSBox hat Vorrang ohne Bibliotheksordner) ------------
    const ExtCase cases[] = {
        {"zip", &kDosboxCore},   {"dosz", &kDosboxCore},  {"exe", &kDosboxCore},
        {"com", &kDosboxCore},   {"bat", &kDosboxCore},   {"iso", &kDosboxCore},
        {"cue", &kDosboxCore},   {"chd", &kDosboxCore},   {"img", &kDosboxCore},
        {"m3u", &kDosboxCore},   {"conf", &kDosboxCore},  {"scummvm", &kScummCore},
        {"wad", &kDoomCore},     {"iwad", &kDoomCore},    {"wl6", &kWolfCore},
        {"sod", &kWolfCore},     {"pak", &kQuakeCore},
        {"bin", nullptr},        {"txt", nullptr},        {"srm", nullptr},
        {"state0", nullptr},     {"tmp", nullptr},
    };
    for (const ExtCase& c : cases)
        for (const std::string& ext : spellings(c.ext))
            for (const char* prefix : kPrefixes)
                for (const char* stem : {"Spiel", "a.b.c", "Titel (Europe) [!]"}) {
                    const std::string file = std::string(prefix) + stem + "." + ext;
                    check(coreForFile(file) == c.core,
                          file + " → " + name(coreForFile(file)) + ", erwartet " + name(c.core));
                }

    // Kein Punkt, Punkt am Anfang, Punkt im Ordnernamen: nie ein Spiel.
    for (const char* file : {"README", ".zip", "/a/.cue", "/ordner.exe/datei", "datei.", ""})
        check(coreForFile(file) == nullptr, std::string("ohne Endung: '") + file + "'");

    // --- 2. Bibliothekspfade: Ordnerspiele wählen den Core über die Endung ---------
    const std::string lib = "/sdcard/Android/data/xr.dosgames/files/games/";
    struct PathCase { std::string path; const CoreSpec* core; } paths[] = {
        {lib + "dos/Spiel/GAME.EXE", &kDosboxCore},   {lib + "dos/Spiel/cd.cue", &kDosboxCore},
        {lib + "doom/Freedoom/freedoom1.wad", &kDoomCore},
        {lib + "quake/id1/pak0.pak", &kQuakeCore},     {lib + "wolf3d/W/vswap.wl6", &kWolfCore},
        {lib + "adventure/Spiel/spiel.scummvm", &kScummCore},
        {"/sdcard/Android/data/xr.shell/files/game/Spiel.cue", &kDosboxCore},
        {"/x/mygames/dos/Spiel.cue", &kDosboxCore},   {"Rennen.cue", &kDosboxCore},
        {lib + "dos/Spiel/readme.txt", nullptr},
    };
    for (const PathCase& c : paths)
        check(coreForPath(c.path) == c.core,
              c.path + " → " + name(coreForPath(c.path)) + ", erwartet " + name(c.core));

    // --- 3. Jeder Core: jede seiner Endungen wählt ihn (außer DOSBox-Vorrang) ------
    for (const CoreSpec* core : kCores)
        for (const char* const* e = core->extensions; *e; ++e) {
            const std::string file = std::string("Test.") + *e;
            const CoreSpec* got = coreForFile(file);
            check(got == core || got == &kDosboxCore, file + " startet " + name(got));
            check(coreForPath(lib + "x/" + file) == got, file + ": Pfad ändert die Wahl nicht");
        }
    // Alle Bibliotheks-Cores sind Ordnerspiele (keine Spiele als einzelne Datei im Bibliotheksordner).
    for (const CoreSpec* core : kCores)
        check(!core->folder || core->folderGames, std::string(name(core)) + ": Ordnerspiel");

    std::printf("%d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
