// XRShell — Batteriespeicher (libretro RETRO_MEMORY_SAVE_RAM) als <Spiel>.srm sichern.
// Abhängigkeitsfrei, Host-Test tests/test_save_ram.cpp.
//
// Manche libretro-Cores schreiben ihren Batteriespeicher (RETRO_MEMORY_SAVE_RAM) NICHT selbst —
// das ist Aufgabe des Frontends. Ohne diese Datei wären die normalen Spielstände solcher Spiele
// beim Beenden verloren; nur Savestates blieben.
//
//   Laden: nach retro_load_game, nur wenn die Dateigröße genau zum Core-Speicher passt
//          (falsche/fremde Datei → nichts überschreiben, Core behält seinen Startzustand).
//   Schreiben: atomar über <pfad>.tmp + rename, nur wenn sich der Inhalt geändert hat
//          (Tracker hält den zuletzt geschriebenen Stand). Ein leerer Speicher (lauter 0x00 oder
//          0xFF) wird erst geschrieben, wenn schon eine Datei existiert — so legt ein Spiel ohne
//          eigene Spielstände keine sinnlosen Dateien an, ein gelöschter Spielstand bleibt gelöscht.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace saveram {

// --- Ablageort ------------------------------------------------------------------------------
// Spielstände eines Spiels (Batteriespeicher .srm, Savestates .stateN) liegen unter
// <saves>/<system>/<name>: system = Bibliotheksordner des Cores („dos“, „doom“, „quake“ …; leer →
// direkt in <saves>), name = Spieldatei ohne Endung bzw. der Ordnername unverändert.
// Früher: <saves>/<name> ohne System, Endung auch bei Ordnern abgeschnitten — gleichnamige
// Spiele verschiedener Systeme bzw. „Doom v1.9“ und „Doom v1.666“ teilten sich Dateien
// (die fremde .srm wurde beim nächsten Sichern überschrieben).

inline std::string gameName(std::string gamePath, bool folder) {
    while (!gamePath.empty() && gamePath.back() == '/') gamePath.pop_back();
    const size_t slash = gamePath.rfind('/');
    std::string name = slash == std::string::npos ? gamePath : gamePath.substr(slash + 1);
    const size_t dot = name.rfind('.');
    if (!folder && dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name.empty() ? "spiel" : name;
}

inline std::string basePath(const std::string& savesDir, const std::string& system,
                            const std::string& gamePath, bool folder) {
    return savesDir + "/" + (system.empty() ? std::string() : system + "/") + gameName(gamePath, folder);
}

// Bisheriger Ort (bis 0.2.1), nur für die Übernahme vorhandener Dateien.
inline std::string legacyBasePath(const std::string& savesDir, const std::string& gamePath) {
    std::string name = gamePath;
    const size_t slash = name.rfind('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    if (name.empty()) name = "spiel";
    return savesDir + "/" + name;
}

// Vorhandene Dateien <from><suffix> nach <to><suffix> verschieben, wenn dort noch nichts liegt
// (wer zuerst startet, bekommt den alten Stand; nichts wird überschrieben). Anzahl verschoben.
inline int migrate(const std::string& from, const std::string& to, const std::vector<std::string>& suffixes) {
    if (from == to) return 0;
    int moved = 0;
    for (const std::string& s : suffixes) {
        const std::string src = from + s, dst = to + s;
        if (::access(src.c_str(), F_OK) == 0 && ::access(dst.c_str(), F_OK) != 0 &&
            std::rename(src.c_str(), dst.c_str()) == 0)
            ++moved;
    }
    return moved;
}

// Datei vollständig lesen; leer, wenn sie fehlt oder nicht genau `expected` Bytes hat.
inline std::vector<uint8_t> readExact(const std::string& path, size_t expected) {
    std::vector<uint8_t> data;
    if (expected == 0) return data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    data.resize(expected);
    const size_t n = std::fread(data.data(), 1, expected, f);
    const bool longer = std::fgetc(f) != EOF;
    std::fclose(f);
    if (n != expected || longer) data.clear();
    return data;
}

inline bool writeAtomic(const std::string& path, const uint8_t* data, size_t size) {
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data, 1, size, f) == size;
    bool closed = std::fclose(f) == 0;
#ifdef _WIN32
    // Nur Host-Test unter Windows: rename ersetzt dort keine vorhandene Datei (Android/POSIX schon).
    if (closed && ok) std::remove(path.c_str());
#endif
    if (!closed || !ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

inline bool isBlank(const uint8_t* data, size_t size) {
    if (size == 0) return true;
    const uint8_t first = data[0];
    if (first != 0x00 && first != 0xFF) return false;
    for (size_t i = 1; i < size; ++i)
        if (data[i] != first) return false;
    return true;
}

// Merkt sich den zuletzt geladenen/geschriebenen Stand eines Spiels.
class Tracker {
public:
    // Neues Spiel: Pfad merken, vorhandene Datei in `mem` laden. true = Datei geladen.
    bool begin(const std::string& path, uint8_t* mem, size_t size) {
        mPath = path;
        mWritten.clear();
        mFileExists = false;
        if (!mem || size == 0) return false;
        std::vector<uint8_t> file = readExact(path, size);
        mFileExists = !file.empty() || ::access(path.c_str(), F_OK) == 0;
        if (file.empty()) {
            mWritten.assign(mem, mem + size);   // Startzustand des Cores gilt als „gesichert“
            return false;
        }
        std::memcpy(mem, file.data(), size);
        mWritten = std::move(file);
        return true;
    }

    // Wurde seit dem letzten Schreiben etwas geändert (und lohnt es sich zu schreiben)?
    bool dirty(const uint8_t* mem, size_t size) const {
        if (mPath.empty() || !mem || size == 0) return false;
        if (mWritten.size() == size && std::memcmp(mWritten.data(), mem, size) == 0) return false;
        return mFileExists || !isBlank(mem, size);
    }

    // Schreibt nur bei Änderung. Rückgabe: 1 geschrieben, 0 nichts zu tun, -1 Fehler.
    int flush(const uint8_t* mem, size_t size) {
        if (!dirty(mem, size)) return 0;
        if (!writeAtomic(mPath, mem, size)) return -1;
        mWritten.assign(mem, mem + size);
        mFileExists = true;
        return 1;
    }

    void reset() { mPath.clear(); mWritten.clear(); mFileExists = false; }
    const std::string& path() const { return mPath; }

private:
    std::string mPath;
    std::vector<uint8_t> mWritten;
    bool mFileExists = false;
};

}  // namespace saveram
