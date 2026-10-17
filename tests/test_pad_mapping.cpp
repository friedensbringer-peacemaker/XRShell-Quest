// Host-Test: Controller-Belegung (src/pad_mapping.*) — Standards, Tausch, Datei-Format, Klemmen.
//   tests/run-host-tests.sh

#include "pad_mapping.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

using namespace padmap;

namespace {
int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 20) std::printf("FEHLER: %s\n", what.c_str());
}
constexpr int B = 0, Y = 1, SELECT = 2, START = 3, A = 8, X = 9, L = 10, R = 11;

bool hasSource(const Mapping& m, int target, Source s) {
    for (Source x : sourcesFor(m, target)) if (x == s) return true;
    return false;
}
} // namespace

int main() {
    // Unbekanntes System: allgemeine RetroPad-Vollbelegung (B/Y auf Trigger/Griff, Start auf Y).
    const System& generic = systemFor("xyz");
    const Mapping& pad = generic.defaults;
    check(pad.button[kRightTrigger] == B && pad.button[kRightGrip] == Y && pad.button[kA] == A &&
          pad.button[kB] == X && pad.button[kLeftTrigger] == L && pad.button[kX] == R &&
          pad.button[kY] == START && pad.button[kRightStickClick] == SELECT, "Rückfall: allgemeine Belegung");
    check(std::string(generic.label) == "Gamepad" && !generic.analog && !generic.analogDevice,
          "Unbekanntes System: Rückfall");
    check(systemFor("").defaults == pad, "Leerer Ordner: Rückfall");
    // Jede Taste des Rückfalls hat im Standard mindestens eine Quest-Taste.
    for (const ConsoleButton& b : generic.buttons)
        check(!sourcesFor(pad, b.id).empty(), "Standard belegt jede Taste");

    // Shooter und DOS-Pad (frühere feste Layouts von xr-dos): keine RetroPad-Taste doppelt, kein
    // Steuerkreuz auf Tasten, jede benannte Spielfunktion erreichbar, Feuer auf dem Trigger.
    for (const char* folder : { "doom", "wolf3d", "quake", "dospad" }) {
        const System& sys = systemFor(folder);
        check(sys.analog && !sys.analogDevice, std::string(folder) == "doom" ? "Doom: Analog, festes Gerät"
                                                                              : "Shooter: Analog, festes Gerät");
        uint16_t seen = 0;
        for (int s = 0; s < kSourceCount; ++s) {
            const int id = sys.defaults.button[s];
            if (id < 0) continue;
            check(!(seen & (1u << id)), "Shooter: Taste nicht doppelt");
            check(id < 4 || id > 7, "Shooter: kein Steuerkreuz auf Tasten");
            seen = static_cast<uint16_t>(seen | (1u << id));
        }
        if (std::string(folder) != "dospad")   // DOS-Pad: R2/Select bewusst frei (Mapper des Spiels)
            for (const ConsoleButton& b : sys.buttons)
                check(!sourcesFor(sys.defaults, b.id).empty(), "Shooter: jede Spielfunktion belegt");
    }
    constexpr int R2 = 13;
    check(systemFor("doom").defaults.button[kRightTrigger] == R2, "Doom: Trigger = R2 Feuer");
    check(systemFor("quake").defaults.button[kRightTrigger] == R2, "Quake: Trigger = R2 Feuer");
    check(systemFor("wolf3d").defaults.button[kRightTrigger] == A, "Wolf3D: Trigger = A Feuer");
    check(std::string(systemFor("doom").buttons[0].name) == "Feuer", "Doom: Spielfunktionen als Namen");
    // Stick-Modi: Steuerkreuz-Achsen und Analog-Ziel (Wolf: nur hoch/runter, Quake: beide).
    check(stickDpadAxes(kStickDpad) == 3 && stickAnalogTarget(kStickDpad) < 0, "Modus Steuerkreuz");
    check(stickDpadAxes(kStickAnalogLeftDpadY) == 2 && stickAnalogTarget(kStickAnalogLeftDpadY) == 0,
          "Modus Analog + hoch/runter");
    check(stickDpadAxes(kStickAnalogLeftDpad) == 3 && stickAnalogTarget(kStickAnalogRight) == 1 &&
          stickDpadAxes(kStickOff) == 0 && stickAnalogTarget(kStickOff) < 0, "Modi Analog + Kreuz, rechts, aus");
    check(systemFor("wolf3d").defaults.stick[kLeftStick] == kStickAnalogLeftDpadY &&
          systemFor("quake").defaults.stick[kLeftStick] == kStickAnalogLeftDpad &&
          systemFor("doom").defaults.stick[kLeftStick] == kStickAnalogLeftDpadY, "Shooter: Stick-Standards (Doom-Menü per Steuerkreuz)");
    check(parse("left_stick = analog_left_dpad_y\n", systemFor("xyz").defaults).stick[kLeftStick] ==
          kStickAnalogLeftDpadY, "Parse: neue Stick-Modi");

    // Zuweisen mit Tausch: Start soll auf A → A gibt Start, die bisherige Start-Taste (Y) bekommt A.
    Mapping m = systemFor("xyz").defaults;
    assign(m, START, kA);
    check(m.button[kA] == START, "Zuweisen: A erzeugt Start");
    check(m.button[kY] == A, "Zuweisen: frühere Start-Taste tauscht auf A");
    check(hasSource(m, START, kA) && !hasSource(m, START, kY), "Zuweisen: Start nur noch auf A");
    check(!sourcesFor(m, A).empty(), "Zuweisen: keine Konsolentaste geht verloren");
    Mapping same = m;
    assign(same, START, kA);
    check(same == m, "Zuweisen derselben Taste ändert nichts");

    // Bitmaske.
    bool pressed[kSourceCount] = {};
    pressed[kA] = pressed[kB] = true;
    const Mapping quake = systemFor("quake").defaults;
    check(buttonMask(quake, pressed) == ((1u << A) | (1u << R)), "Bitmaske A+R");
    pressed[kA] = pressed[kB] = false;
    pressed[kLeftStickClick] = true;   // im Quake-Standard frei
    check(buttonMask(quake, pressed) == 0, "Freie Quest-Taste ohne Wirkung");

    // Datei-Format: Rundreise, Kommentare, Klemmen ungültiger Werte, fehlende Schlüssel aus base.
    Mapping custom = systemFor("xyz").defaults;
    assign(custom, START, kRightStickClick);
    custom.stick[kLeftStick] = kStickOff;
    check(parse(serialize(custom), systemFor("doom").defaults) == custom, "Rundreise serialize/parse");
    const Mapping p = parse("# Kommentar\n A = Start  # Kommentar\nright_trigger = quatsch\n"
                            "LEFT_STICK = analog_right\nright_stick = nix\nunbekannt = a\nb=none\n",
                            systemFor("doom").defaults);
    check(p.button[kA] == START, "Parse: Groß/klein, Leerzeichen, Kommentar");
    check(p.button[kRightTrigger] == systemFor("doom").defaults.button[kRightTrigger],
          "Parse: ungültiger Wert behält base");
    check(p.stick[kLeftStick] == kStickAnalogRight &&
          p.stick[kRightStick] == systemFor("doom").defaults.stick[kRightStick], "Parse: Sticks klemmen");
    check(p.button[kB] == kNone, "Parse: none = ohne Wirkung");

    // Ablage: Spiel neben der Datei, Systemstandard im Bibliotheksordner; Vorrang Spiel > System > eingebaut.
    check(gameFile("/x/games/doom/Freedoom") == "/x/games/doom/Freedoom.pad", "Spieldatei .pad");
    check(systemFile("/x", "doom") == "/x/games/doom/_standard.pad", "Systemdatei _standard.pad");
    // Eigener Ordner pro Lauf (nicht direkt in TEMP): zwei Sitzungen dürfen gleichzeitig testen.
    const std::string dir = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : "/tmp") +
                            "/xrshell-test-pad-mapping-" + std::to_string(std::random_device{}());
    const std::string sys = dir + "/games/doom/_standard.pad";
    std::error_code ec;
    std::filesystem::create_directories(dir + "/games/doom", ec);
    check(!ec, "Testordner anlegen");
    Mapping sysMap = systemFor("doom").defaults;
    sysMap.button[kLeftTrigger] = START;
    check(saveFile(sys, sysMap), "Systemstandard speichern");
    const std::string game = dir + "/games/doom/Freedoom";
    check(loadFor(game, dir, "doom") == sysMap, "Ohne Spieldatei gilt der Systemstandard");
    Mapping gameMap = sysMap;
    assign(gameMap, SELECT, kA);
    check(saveFile(gameFile(game), gameMap), "Spielbelegung speichern");
    check(loadFor(game, dir, "doom") == gameMap, "Spieldatei hat Vorrang");
    std::remove(gameFile(game).c_str());
    std::remove(sys.c_str());
    check(loadFor(game, dir, "doom") == systemFor("doom").defaults, "Ohne Dateien: eingebaute Belegung");
    std::filesystem::remove_all(dir, ec);

    std::printf("test_pad_mapping: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
