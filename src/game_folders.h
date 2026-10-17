// XRShell — Spielordner der Bibliothek (xr.dosgames, xr.shooter, xr.adventure): jeder Unterordner von
// files/games/<system>/ ist ein Spiel (DOSBox-Pure lädt ihn als Laufwerk C:, die
// Shooter-Cores eine Startdatei darin, siehe findStartFile).
//
// Titel in der Spielauswahl: erste nicht leere Zeile von „xrshell.title“ im Ordner
// (UTF-8, BOM/CR werden entfernt), sonst der Ordnername. Versteckte Ordner („.“)
// zählen nicht. Abhängigkeitsfrei (POSIX dirent/stat) — Host-Test in tests/.

#pragma once

#include <string>
#include <vector>

struct FolderGame {
    std::string path;    // <dir>/<Ordner>
    std::string title;   // xrshell.title bzw. Ordnername
};

// Alle Spielordner in dir, sortiert nach Titel (Groß/klein egal), bei gleichem Titel
// nach Pfad. Leere Liste, wenn dir fehlt oder nicht lesbar ist.
std::vector<FolderGame> scanGameFolders(const std::string& dir);

// Titel eines Spielordners (siehe oben).
std::string folderGameTitle(const std::string& folder);

// xr.shooter: welche Datei eines Spielordners der Core lädt.
//   DoomIwad: IWAD nach Priorität doom2, doom, doomu, tnt, plutonia, doom1, freedoom2,
//             freedoom1, freedm (.wad, Groß/klein egal) — erst direkt im Ordner, dann in
//             Unterordnern (alphabetisch, z. B. base/ vor rerelease/).
//   WolfData: Wolfenstein-3D-/Spear-Daten nach Endung wl6, sod, wl1, sdm, n3d; je Endung
//             vswap.* zuerst, sonst die alphabetisch erste Datei (nur direkt im Ordner).
//   QuakePak: id1/pak0.pak (Groß/klein egal).
//   ScummTarget (xr.adventure): erste „*.scummvm“-Datei (alphabetisch, Endung Groß/klein egal)
//             direkt im Ordner; Inhalt = ScummVM-Spiel-ID, die Spieldaten liegen daneben.
// „xrshell.start“ im Ordner (erste nicht leere Zeile = Pfad relativ zum Ordner) geht
// jeder Regel vor, wenn die Datei existiert. Leer = nichts Passendes gefunden.
enum class StartRule { None, DoomIwad, WolfData, QuakePak, ScummTarget };
std::string findStartFile(const std::string& folder, StartRule rule);
