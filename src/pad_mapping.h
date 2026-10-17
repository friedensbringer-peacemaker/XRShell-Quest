// XRShell — Controller-Belegung für Pad-Cores (RetroPad), pro Spiel speicherbar.
//
// Jede Quest-Taste (A, B, X, Y, Trigger, rechter Griff, Stick drücken) erzeugt eine
// Konsolentaste (RetroPad-ID); jeder Stick wirkt als Steuerkreuz, als linker oder
// rechter Analogstick oder gar nicht. Pro System gibt es eine eingebaute Belegung
// (Shooter: Doom, Wolf3D, Quake; DOSBox-Pure im Pad-Modus).
//
// Gespeichert wird als Textdatei `key = value`, Laden klemmt auf gültige Werte:
//   <Spieldatei>.pad                     — nur dieses Spiel
//   games/<system>/_standard.pad         — „für alle <System>-Spiele“
//   a = a                                # Quest-Taste = Konsolentaste (RetroPad-Name)
//   right_trigger = start
//   left_stick = dpad                    # dpad | analog_left | analog_right | off
// Der linke Griff bleibt das Fenster-Greifen und ist nicht belegbar.
//
// Reine Logik ohne Android-Abhängigkeit — Host-Test: tests/test_pad_mapping.cpp.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace padmap {

enum Source {
    kA, kB, kX, kY, kRightTrigger, kRightGrip, kLeftTrigger, kRightStickClick, kLeftStickClick,
    kSourceCount
};
// Analog links + Steuerkreuz: Menüs per Steuerkreuz, im Spiel analog (Quake); nur hoch/runter
// als Steuerkreuz (ECWolf: links/rechts wäre drehen und kollidiert mit seitwärts).
enum StickMode {
    kStickDpad, kStickAnalogLeft, kStickAnalogRight, kStickOff, kStickAnalogLeftDpad, kStickAnalogLeftDpadY,
    kStickModeCount
};
// Welche Achsen eines Sticks das Steuerkreuz drücken (Bit 0 = x, Bit 1 = y) und ob/wohin er
// analog geht (0 = links, 1 = rechts, -1 = nicht).
unsigned stickDpadAxes(StickMode m);
// Analog + Steuerkreuz: Steuerkreuz nur mit der hohen Schwelle (pad_analog.h, kDpadPressHigh).
bool stickDpadHigh(StickMode m);
int stickAnalogTarget(StickMode m);
enum Stick { kLeftStick, kRightStick, kStickCount };
constexpr int kNone = -1;   // Quest-Taste ohne Wirkung

struct Mapping {
    int8_t button[kSourceCount];   // RetroPad-ID 0..15 oder kNone
    uint8_t stick[kStickCount];    // StickMode
    bool operator==(const Mapping& o) const;
    bool operator!=(const Mapping& o) const { return !(*this == o); }
};

struct ConsoleButton { int id; const char* name; };   // RetroPad-ID + Name am Gerät

struct System {
    const char* folder;                      // "doom", "wolf3d", "quake", "dospad"
    const char* label;                       // „Doom“
    std::vector<ConsoleButton> buttons;      // in Anzeige-Reihenfolge (ohne Steuerkreuz)
    bool analog;                             // Core liest Analogsticks
    Mapping defaults;
    // Analog-Stick gewählt → Pad-Typ RETRO_DEVICE_ANALOG. Shooter haben ein festes Gerät.
    bool analogDevice = false;
};

// System zum Bibliotheksordner; unbekannt → allgemeine RetroPad-Vollbelegung.
const System& systemFor(const std::string& folder);

const char* sourceName(Source s);   // Anzeige: „A“, „Trigger R“, „R-Stick“ …
const char* stickModeName(StickMode m);

// Zuweisen (Antippen + Drücken): Quest-Taste src erzeugt künftig die Konsolentaste
// target. Hatte target vorher eine andere Quest-Taste, bekommt diese die bisherige
// Konsolentaste von src (Tausch) — so geht keine Taste verloren.
void assign(Mapping& m, int target, Source src);
// Quest-Tasten, die target erzeugen (für die Anzeige).
std::vector<Source> sourcesFor(const Mapping& m, int target);

// RetroPad-Bitmaske aus den gedrückten Quest-Tasten (ohne Sticks).
uint16_t buttonMask(const Mapping& m, const bool pressed[kSourceCount]);

std::string serialize(const Mapping& m);
// Liest Text über base: fehlende/ungültige Schlüssel behalten den Wert aus base.
Mapping parse(const std::string& text, const Mapping& base);

bool loadFile(const std::string& path, const Mapping& base, Mapping& out);
bool saveFile(const std::string& path, const Mapping& m);

// Ablageorte: Belegung des Spiels neben der Spieldatei, Systemstandard im Bibliotheksordner.
std::string gameFile(const std::string& gamePath);
std::string systemFile(const std::string& externalDataPath, const std::string& folder);

// Spiel → Systemstandard-Datei → eingebaute Belegung.
Mapping loadFor(const std::string& gamePath, const std::string& externalDataPath, const std::string& folder);

} // namespace padmap
