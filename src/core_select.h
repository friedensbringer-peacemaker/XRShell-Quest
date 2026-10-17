// XRShell — Core-Wahl für libretro-Spiele (ohne Abhängigkeiten).
//
// Welche Datei startet welchen Core. Reine Zeichenkettenlogik — Host-Tests: tests/test_core_select.cpp.

#pragma once

#include "game_folders.h"   // StartRule (xr.shooter)
#include "libretro.h"       // RETRO_DEVICE_* (Eingabegerät nach dem Laden)

#include <cctype>
#include <cstddef>
#include <string>

namespace core_select {

// #16: Cores und ihre Spieldateien. Die .so kommen zur Build-Zeit vom
// libretro-Buildbot (build.gradle) nach assets/cores/ — nie ins Repo.
struct CoreSpec {
    const char* soName;
    const char* const* extensions;   // nullptr-terminiert, kleingeschrieben
    bool padMode;                    // Controller als RetroPad statt Maus
    bool provideDirs;                // System-/Save-Verzeichnis anbieten
    const char* folder;              // Bibliothek: files/games/<folder>/ (nullptr = keine)
    const char* systemLabel;         // Anzeige in der Bibliothek
    bool folderGames = false;        // Bibliothek: Spiel = Unterordner (DOS, Shooter, Adventures)
    // Gerät für retro_set_controller_port_device(0, …) nach dem Laden (0 = nicht aufrufen).
    // Die Tastenbelegung liegt in pad_mapping.cpp (Systemordner bzw. „dospad“).
    unsigned padDevice = 0;
    StartRule startRule = StartRule::None;   // Spielordner → Startdatei (xr.shooter)
};

inline constexpr const char* kDosboxExts[] = {"zip", "dosz", "exe", "com", "bat", "iso",
                                                "chd", "cue", "ins", "img", "ima", "vhd",
                                                "m3u", "m3u8", "conf", nullptr};
// #28: ScummVM — „.scummvm“-Datei im Spielordner, Inhalt = Spiel-ID (z. B. „sky“).
inline constexpr const char* kScummExts[] = {"scummvm", nullptr};
// xr.shooter: Spiele sind Ordner (games/doom|wolf3d|quake/<Ordner>/), die Startdatei
// bestimmt findStartFile. Die Endungen dienen nur der Zuordnung einzelner Dateien.
inline constexpr const char* kDoomExts[] = {"wad", "iwad", nullptr};
inline constexpr const char* kWolfExts[] = {"wl6", "sod", "wl1", "sdm", "n3d", nullptr};
inline constexpr const char* kQuakeExts[] = {"pak", nullptr};

// DOSBox-Pure braucht das Save-Verzeichnis: Laufwerk C: ist ein Union-Laufwerk,
// jeder Schreibzugriff (Spielstände, SETTINGS.DAT, SAVE\*) landet in
// <saves>/<Spielordner>.pure.zip — nie im Spielordner. Ohne Verzeichnis nimmt der
// Core <cwd>/…, unter Android „/“ → nicht beschreibbar, alles weg nach Beenden.
// GPL-2.0-only-Cores passen nicht zum Apache-2.0-OpenXR-Loader.
// Ein fehlender Core (Variante ohne ihn) meldet init.
// Bibliothek (xr.dosgames): games/dos/<Ordner>/ = Laufwerk C: — nur in Apps mit DOSBox-Pure-Core
// (scanLibrary überspringt Cores, die nicht im APK liegen).
inline constexpr CoreSpec kDosboxCore = {"dosbox_pure_libretro_android.so", kDosboxExts, false, true,
                                         "dos", "DOS", true};
// ScummVM (GPL-3.0+): Maus-Modus, System-Dir mit scummvm/extra + scummvm/theme
// (aus den Assets entpackt) und scummvm.ini, Save-Dir für Spielstände.
// Bibliothek (xr.adventure): games/adventure/<Ordner>/ mit *.scummvm (StartRule::ScummTarget).
inline constexpr CoreSpec kScummCore  = {"scummvm_libretro_android.so", kScummExts, false, true,
                                         "adventure", "Adventure", true, 0, StartRule::ScummTarget};
// xr.shooter (alle GPL-2.0-oder-später): Pad-Modus mit Analogsticks. Ohne
// retro_set_controller_port_device lesen PrBoom und TyrQuake gar keine Eingaben
// (Gerät 0 = „none“) — RetroArch ruft es immer, XRShell daher auch.
//   PrBoom: „Doom Gamepad Modern“ = RETRO_DEVICE_SUBCLASS(ANALOG, 2); prboom.wad steckt im Core.
//   ECWolf: nur RetroPad; Menüs nur per Steuerkreuz (Belegung: linker Stick vor/zurück
//           zusätzlich als Steuerkreuz). ecwolf.pk3 steckt im Core.
//   TyrQuake: „Modern“ = RETRO_DEVICE_SUBCLASS(ANALOG, 2); Menüs per Steuerkreuz.
inline constexpr CoreSpec kDoomCore   = {"prboom_libretro_android.so", kDoomExts, true, true, "doom", "Doom",
                                         true, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2), StartRule::DoomIwad};
inline constexpr CoreSpec kWolfCore   = {"ecwolf_libretro_android.so", kWolfExts, true, true, "wolf3d",
                                         "Wolf3D", true, RETRO_DEVICE_JOYPAD, StartRule::WolfData};
inline constexpr CoreSpec kQuakeCore  = {"tyrquake_libretro_android.so", kQuakeExts, true, true, "quake",
                                         "Quake", true, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2),
                                         StartRule::QuakePak};
// DOSBox-Pure mit „xrshell_input = pad“ (xrshell.opt): Gamepad-Mapper des Cores an Port 0.
// RETRO_DEVICE_JOYPAD = „Use Gamepad Mapper“ → eigene PADMAP.DBP, sonst Auto-Mapping des
// Spiels (keyb2joypad, z. B. DOOM, Duke Nukem 3D), sonst „Generic Keyboard Bindings“.
// Ohne Aufruf bleibt der Port bei DOSBox-Pure „Disabled“. Kein eigener Bibliothekseintrag.
inline constexpr CoreSpec kDosboxPadCore = {"dosbox_pure_libretro_android.so", kDosboxExts, true, true,
                                            nullptr, "DOS", true, RETRO_DEVICE_JOYPAD, StartRule::None};
// Reihenfolge = Vorrang bei gleicher Endung ohne Bibliotheksordner (DOSBox zuerst).
inline constexpr const CoreSpec* kCores[] = {&kDosboxCore, &kScummCore, &kDoomCore,
                                             &kWolfCore, &kQuakeCore};

inline std::string lowerCase(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// Dateiname ohne Pfad.
inline std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Endung kleingeschrieben ohne Punkt; leer = keine.
inline std::string extensionOf(const std::string& name) {
    const std::string base = baseName(name);
    const size_t dot = base.rfind('.');
    if (dot == std::string::npos || dot == 0) return {};
    return lowerCase(base.substr(dot + 1));
}

inline bool hasExtension(const CoreSpec* core, const std::string& name) {
    const std::string ext = extensionOf(name);
    if (ext.empty()) return false;
    for (const char* const* e = core->extensions; *e; ++e)
        if (ext == *e) return true;
    return false;
}

// Core zur Dateiendung (libretro-Info: supported_extensions); nullptr = kein Spiel.
inline const CoreSpec* coreForFile(const std::string& name) {
    for (const CoreSpec* core : kCores)
        if (hasExtension(core, name)) return core;
    return nullptr;
}

// Spiel in der Bibliothek (…/games/<ordner>/datei): bei Cores, deren Spiele einzelne Dateien sind,
// bestimmt der Ordner den Core, damit gleiche Endungen eindeutig bleiben. Sonst nach
// Endung — Ordnerspiele (DOS, Shooter, Adventures) wählen den Core über die Endung.
inline const CoreSpec* coreForPath(const std::string& path) {
    for (const CoreSpec* core : kCores) {
        if (!core->folder || core->folderGames || !hasExtension(core, path)) continue;
        const std::string marker = std::string("games/") + core->folder + "/";
        const size_t at = path.rfind(marker);
        if (at != std::string::npos && (at == 0 || path[at - 1] == '/') &&
            path.find('/', at + marker.size()) == std::string::npos)
            return core;
    }
    return coreForFile(path);
}

} // namespace core_select
