// XRShell — Tastenprofil pro Spiel (xrshell.keys).
//
// Legt die sonst freien Controller-Eingaben (A, X, Y, Trigger links, rechten
// Stick drücken, linker Stick ohne Griff) auf Spieltasten. Das Profil liegt als
// Textdatei beim Spiel — BYOA: der Spieler legt es neben seine Daten, Vorlagen
// liegen im Repo unter profiles/.
//
//   # Kommentar
//   x               = LSHIFT        # halten = Taste halten
//   right_stick_click = LALT+R      # Kombination: Tasten nacheinander drücken
//   left_stick_left = DELETE
//   hires = F8                      # keine Controller-Taste: VR-Menü → DOS → „Hohe Auflösung“
//   game_menu = LCTRL+F5            # keine Controller-Taste: VR-Menü → Spiel → „Spielmenü öffnen“
//                                   # (xr.adventure: ohne Angabe Strg+F5 = ScummVM-Menü, setDefault)
//   scroll_left = KP4               # Scroll-Tasten für „Karte ziehen“ (rechter Griff, xr.dosgames);
//                                   # ohne Angabe Pfeiltasten (zählt nicht als Profilbelegung)
//
// Kombinationen werden im Abstand von ~50 ms gedrückt: der Core liest die
// Tastatur einmal pro Frame, gleichzeitig gedrückte Tasten kämen dort in
// Keycode-Reihenfolge an (R vor ALT) und der Modifikator würde verpasst.
// Ohne Profil sind die Eingaben wirkungslos.

#pragma once

#include "libretro.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class LibretroAdapter;

class KeyProfile {
public:
    enum Input {
        kA, kX, kY, kLeftTrigger, kRightStickClick,
        kLeftStickLeft, kLeftStickRight, kLeftStickUp, kLeftStickDown,
        kHires,     // Menü-Aktion „Hohe Auflösung umschalten“ (xr.dosgames), keine Controller-Taste
        kGameMenu,  // Menü-Aktion „Spielmenü öffnen (Speichern/Laden)“, keine Controller-Taste
        // Karte ziehen (rechter Griff, map_drag.h): Scroll-Tasten, Standard Pfeiltasten
        kScrollLeft, kScrollRight, kScrollUp, kScrollDown,
        kInputCount
    };

    KeyProfile();   // Scroll-Tasten = Pfeiltasten, sonst nichts belegt

    // Sucht xrshell.keys im Spielordner bzw. neben der Spieldatei, dann im
    // übergeordneten Ordner (game/). false = kein Profil gefunden oder leer.
    bool load(const std::string& gamePath);
    bool empty() const;
    // Eingabe im Profil belegt? (Sonst darf XRShell sie selbst nutzen, z. B. X = Bildzoom.)
    // Standard-Scroll-Tasten (Pfeiltasten) zählen nicht als belegt.
    bool bound(Input input) const { return !mBindings[input].empty() && !mDefault[input]; }
    // Belegung wie im Profil geschrieben (z. B. „F8“, „LALT+R“), leer = nicht belegt.
    const std::string& bindingText(Input input) const { return mBindingText[input]; }
    // Eingebauter Standard für eine im Profil nicht belegte Eingabe (z. B. game_menu = LCTRL+F5
    // beim ScummVM-Core); zählt wie die Scroll-Tasten nicht als Profilbelegung. Text wie im Profil.
    void setDefault(Input input, const std::string& text);

    // Pro Frame vom Main-Thread: aktueller Zustand aller Eingaben, now in s.
    void update(const bool pressed[kInputCount], double now, LibretroAdapter& adapter);
    // Sofort loslassen (ohne Mindest-Haltezeit) — Menü öffnet, Spiel pausiert, Spielwechsel.
    void releaseNow(Input input, LibretroAdapter& adapter);
    // Hält diese Eingabe gerade (mindestens) eine Taste?
    bool holding(Input input) const { return mHeld[input]; }

private:
    bool parseFile(const std::string& path);
    void pressKey(enum retro_key key, LibretroAdapter& adapter);
    void releaseKey(enum retro_key key, LibretroAdapter& adapter);

    std::vector<enum retro_key> mBindings[kInputCount];
    std::string mBindingText[kInputCount];
    bool mDefault[kInputCount] = {};        // Belegung ist der eingebaute Standard (nicht aus der Datei)
    double mPressTime[kInputCount] = {};
    size_t mKeysDown[kInputCount] = {};     // wie viele Tasten der Kombination schon unten
    bool   mHeld[kInputCount] = {};
    // Mehrere Eingaben dürfen dieselbe Taste halten (z. B. zweimal SHIFT).
    std::array<uint8_t, RETROK_LAST> mRefs{};
};
