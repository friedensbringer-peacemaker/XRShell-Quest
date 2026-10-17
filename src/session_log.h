// XRShell — Sitzungslog auf dem Gerät (alle XRShell-Apps).
//
// Warum: Der Nutzer testet oft ohne USB-Kabel — was nur in logcat steht, ist danach weg.
// Jede Zeile mit Tag „XRShell…“ (XRShell, XRShell-Core) geht deshalb zusätzlich in
//   <externalDataPath>/logs/session-JJJJMMTT-HHMMSS.log
// Abholen am PC: tools/pull-logs.sh [APP_ID] (adb pull, ohne root).
//
// Ablauf (session_log.cpp): Der Linker leitet jedes __android_log_print der Bibliothek auf
// __wrap___android_log_print um (CMakeLists: -Wl,--wrap) — kein Quelltext muss seine
// LOGI/LOGE-Makros ändern. Zeilen vor slog::start() (Kopfzeile noch unbekannt) werden
// gesammelt und nach der Kopfzeile geschrieben. Schreiben unter Mutex, gepuffert; flush
// spätestens alle 2 s, sofort bei Fehlerzeilen und bei Pause/Stop/Beenden.
//
// Grenzen: 5 MB je Datei; darüber geht es in session-…-2.log usw. weiter (höchstens 4 Teile
// je Sitzung — Teil 1 mit Kopfzeile bleibt, dazwischen fallen die ältesten weg). Beim Start
// bleiben nur die Dateien der letzten 10 Sitzungen (inklusive der neuen).
//
// Rechte: Der Ordner wird nur per mkdir angelegt und erbt so das Setgid-Bit von files/ — dann
// gehören neue Dateien der Gruppe ext_data_rw und adb darf sie lesen. Nie chmod auf den Ordner
// (löscht Setgid, XR-Stratagus 0.2.2). Ordner ohne Setgid werden neu angelegt (repariert).
//
// Dieser Header enthält nur die abhängigkeitsfreie Logik (Namen, Aufräumen, Größengrenze);
// Host-Test: tests/test_session_log.cpp.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace slog {

constexpr size_t kMaxFileBytes = 5u * 1024u * 1024u;   // je Datei
constexpr int kKeepSessions = 10;                       // inklusive der laufenden
constexpr int kMaxParts = 4;                            // Dateien je Sitzung
constexpr double kFlushSeconds = 2.0;                   // spätestens so oft auf die Karte

// Sitzungsschlüssel „session-JJJJMMTT-HHMMSS“ aus Datum/Uhrzeit.
inline std::string sessionKey(int year, int month, int day, int hour, int minute, int second) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "session-%04d%02d%02d-%02d%02d%02d", year, month, day, hour,
                  minute, second);
    return buf;
}

// Dateiname von Teil part (1 = ohne Zusatz) einer Sitzung.
inline std::string partFileName(const std::string& key, int part) {
    return part <= 1 ? key + ".log" : key + "-" + std::to_string(part) + ".log";
}

// Zerlegt einen Dateinamen. true nur für „session-JJJJMMTT-HHMMSS[-N].log“ (N ≥ 2).
inline bool parseName(const std::string& name, std::string& key, int& part) {
    constexpr size_t kKeyLen = 23;   // „session-“ + 8 + „-“ + 6
    if (name.size() < kKeyLen + 4 || name.compare(0, 8, "session-") != 0) return false;
    for (size_t i = 8; i < kKeyLen; ++i) {
        const char c = name[i];
        if (i == 16 ? c != '-' : (c < '0' || c > '9')) return false;
    }
    if (name.compare(name.size() - 4, 4, ".log") != 0) return false;
    const std::string rest = name.substr(kKeyLen, name.size() - 4 - kKeyLen);
    if (rest.empty()) {
        part = 1;
    } else {
        if (rest[0] != '-' || rest.size() < 2 || rest.size() > 4) return false;
        int n = 0;
        for (size_t i = 1; i < rest.size(); ++i) {
            if (rest[i] < '0' || rest[i] > '9') return false;
            n = n * 10 + (rest[i] - '0');
        }
        if (n < 2) return false;
        part = n;
    }
    key = name.substr(0, kKeyLen);
    return true;
}

// Beim Start einer neuen Sitzung: welche vorhandenen Dateien weg müssen, damit mit der neuen
// höchstens keepSessions Sitzungen bleiben (älteste zuerst; fremde Dateien bleiben unberührt).
inline std::vector<std::string> sessionsToDelete(const std::vector<std::string>& names,
                                                 int keepSessions = kKeepSessions) {
    std::vector<std::string> keys;
    for (const std::string& n : names) {
        std::string key;
        int part = 0;
        if (parseName(n, key, part)) keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const size_t keepOld = keepSessions > 1 ? static_cast<size_t>(keepSessions - 1) : 0;
    std::vector<std::string> out;
    if (keys.size() <= keepOld) return out;
    const std::vector<std::string> doomed(keys.begin(), keys.end() - static_cast<long>(keepOld));
    for (const std::string& n : names) {
        std::string key;
        int part = 0;
        if (parseName(n, key, part) && std::binary_search(doomed.begin(), doomed.end(), key))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Nach dem Anlegen von Teil newPart: welche Teile derselben Sitzung wegfallen, damit höchstens
// maxParts bleiben. Teil 1 (Kopfzeile, Start) bleibt immer, sonst fallen die ältesten weg.
inline std::vector<int> partsToDelete(int newPart, int maxParts = kMaxParts) {
    std::vector<int> out;
    if (maxParts < 2) maxParts = 2;
    // Bleiben: 1 und die (maxParts − 1) neuesten bis newPart.
    const int firstKept = newPart - (maxParts - 1) + 1;
    for (int p = 2; p < firstKept; ++p) out.push_back(p);
    return out;
}

// Passt eine Zeile von lineBytes noch in die Datei (written Bytes)? Die erste Zeile einer
// Datei passt immer (sonst entstünde nie ein neuer Teil).
inline bool fits(size_t written, size_t lineBytes, size_t maxBytes = kMaxFileBytes) {
    return written == 0 || written + lineBytes <= maxBytes;
}

// Stufe aus der Android-Priorität (2 = VERBOSE … 6 = ERROR, 7 = FATAL).
inline char levelLetter(int prio) {
    switch (prio) {
    case 2: return 'V';
    case 3: return 'D';
    case 4: return 'I';
    case 5: return 'W';
    case 6: return 'E';
    case 7: return 'F';
    default: return '?';
    }
}

// Geht eine Zeile mit diesem Tag ins Sitzungslog? Nur die eigenen Tags (XRShell, XRShell-Core).
inline bool ownTag(const char* tag) {
    return tag && std::string(tag).compare(0, 7, "XRShell") == 0;
}

} // namespace slog

// --- Laufzeit (session_log.cpp, nur Android) ----------------------------------------------
namespace slog {

// Datei anlegen (Ordner logDir, Aufräumen, Kopfzeile) und gesammelte Zeilen schreiben.
// Ein zweiter Aufruf tut nichts. false = kein Sitzungslog (logcat läuft weiter).
bool start(const std::string& logDir, const std::string& header);
// Gepuffertes auf die Karte bringen; flushIfDue nur, wenn kFlushSeconds vergangen sind
// (billig, pro Frame aufrufbar).
void flush();
void flushIfDue();
// Pfad der aktuellen Datei (leer = keine).
std::string currentPath();

} // namespace slog
