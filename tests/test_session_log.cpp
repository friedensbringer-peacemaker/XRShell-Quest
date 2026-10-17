// Host-Test für src/session_log.h (Sitzungslog: Dateinamen, Aufräumen auf 10 Sitzungen, Teile
// bei 5 MB). Sweep: Anzahl vorhandener Sitzungen × Teile je Sitzung × fremde Dateien; dazu eine
// simulierte Sitzung, die über viele Teile wächst. Gebaut von tests/run-host-tests.sh.

#include "session_log.h"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

// Sitzung i (0 = älteste, i < 1000): Tag/Monat/Jahr steigen mit i, über Monats- und Jahresgrenzen.
std::string keyOf(int i) {
    const int year = 2025 + i / 100, month = 1 + (i / 10) % 10, day = 1 + i % 10;
    return slog::sessionKey(year, month, day, (i * 7) % 24, (i * 13) % 60, (i * 17) % 60);
}

void testNames() {
    check(slog::sessionKey(2026, 10, 3, 9, 5, 7) == "session-20261003-090507", "Schlüssel");
    check(slog::partFileName("session-20261003-090507", 1) == "session-20261003-090507.log", "Teil 1");
    check(slog::partFileName("session-20261003-090507", 3) == "session-20261003-090507-3.log", "Teil 3");
    for (int part = 1; part <= 120; ++part) {
        const std::string name = slog::partFileName("session-20261231-235959", part);
        std::string key;
        int p = 0;
        check(slog::parseName(name, key, p) && key == "session-20261231-235959" && p == part,
              "Rundweg " + name);
    }
    const char* bad[] = {"session-20261003-090507", "session-2026100-090507.log", "session-20261003_090507.log",
                         "session-20261003-090507-1.log", "session-20261003-090507-.log",
                         "session-20261003-090507-12345.log", "session-20261003-090507-x.log",
                         "xr-20261003-090507.log", "session-20261003-090507.log.tmp", "logcat.txt", "",
                         "session-2026a003-090507.log"};
    for (const char* b : bad) {
        std::string key;
        int p = 0;
        check(!slog::parseName(b, key, p), std::string("kein Sitzungslog: ") + b);
    }
}

void testPrune() {
    const char* foreign[] = {"notiz.txt", "xr-20250101-000000.log", "session-x.log"};
    for (int existing = 0; existing <= 25; ++existing)
        for (int parts = 1; parts <= 4; ++parts)
            for (int withForeign = 0; withForeign <= 1; ++withForeign) {
                std::vector<std::string> names;
                for (int i = 0; i < existing; ++i)
                    for (int p = 1; p <= parts; ++p) names.push_back(slog::partFileName(keyOf(i), p));
                if (withForeign)
                    for (const char* f : foreign) names.push_back(f);
                // Reihenfolge wie von readdir: unsortiert (rückwärts)
                std::vector<std::string> shuffled(names.rbegin(), names.rend());
                const std::vector<std::string> del = slog::sessionsToDelete(shuffled);
                std::set<std::string> gone(del.begin(), del.end());
                std::set<std::string> keptKeys;
                for (const std::string& n : names) {
                    std::string key;
                    int p = 0;
                    const bool session = slog::parseName(n, key, p);
                    if (!session) check(!gone.count(n), "fremde Datei bleibt: " + n);
                    else if (!gone.count(n)) keptKeys.insert(key);
                }
                const int expectKept = existing < slog::kKeepSessions - 1 ? existing : slog::kKeepSessions - 1;
                char what[96];
                std::snprintf(what, sizeof(what), "%d Sitzungen × %d Teile: %d bleiben", existing, parts, expectKept);
                check(static_cast<int>(keptKeys.size()) == expectKept, what);
                // Es bleiben genau die neuesten — und von ihnen alle Teile.
                for (int i = 0; i < existing; ++i) {
                    const bool newest = i >= existing - expectKept;
                    for (int p = 1; p <= parts; ++p)
                        check(gone.count(slog::partFileName(keyOf(i), p)) == (newest ? 0u : 1u),
                              std::string(what) + ", Sitzung " + std::to_string(i));
                }
                check(static_cast<int>(del.size()) == (existing - expectKept) * parts, std::string(what) + ", Anzahl");
            }
    // Mit der neuen Sitzung sind es dann höchstens 10.
    std::vector<std::string> names;
    for (int i = 0; i < 30; ++i) names.push_back(slog::partFileName(keyOf(i), 1));
    check(names.size() - slog::sessionsToDelete(names).size() + 1 == static_cast<size_t>(slog::kKeepSessions),
          "nach dem Start genau 10 Sitzungen");
}

// Eine Sitzung schreibt Zeilen; bei Überlauf neuer Teil, alte Teile fallen weg (Teil 1 bleibt).
void testGrowth() {
    const size_t lineSizes[] = {1, 80, 1000, 4096, 70000};
    const size_t limits[] = {1000, 64 * 1024, slog::kMaxFileBytes};
    for (size_t lineBytes : lineSizes)
        for (size_t limit : limits)
            for (int maxParts = 2; maxParts <= 5; ++maxParts) {
                std::map<int, size_t> files;   // Teil → Bytes
                int part = 1;
                files[1] = 0;
                const size_t total = limit * 9 + lineBytes * 3;
                for (size_t written = 0; written < total; written += lineBytes) {
                    if (!slog::fits(files[part], lineBytes, limit)) {
                        ++part;
                        files[part] = 0;
                        for (int p : slog::partsToDelete(part, maxParts)) files.erase(p);
                    }
                    files[part] += lineBytes;
                }
                char what[96];
                std::snprintf(what, sizeof(what), "Zeile %zu B, Grenze %zu B, %d Teile", lineBytes, limit, maxParts);
                check(files.count(1) == 1, std::string(what) + ": Teil 1 bleibt");
                check(files.count(part) == 1, std::string(what) + ": neuester Teil da");
                check(static_cast<int>(files.size()) <= maxParts, std::string(what) + ": höchstens maxParts");
                for (const auto& f : files) {
                    check(f.second <= limit || f.second == lineBytes, std::string(what) + ": Größengrenze");
                    if (f.first != 1 && part > maxParts) check(f.first >= part - maxParts + 2, std::string(what) + ": nur neueste");
                }
            }
    check(slog::partsToDelete(2).empty() && slog::partsToDelete(4).empty(), "bis 4 Teile nichts löschen");
    check(slog::partsToDelete(5) == std::vector<int>{2}, "Teil 5 → Teil 2 weg");
    check(slog::fits(0, slog::kMaxFileBytes * 2), "erste Zeile passt immer");
}

void testTagsAndLevels() {
    check(slog::ownTag("XRShell") && slog::ownTag("XRShell-Core"), "eigene Tags");
    check(!slog::ownTag("OpenXR") && !slog::ownTag(nullptr) && !slog::ownTag("XRShel") && !slog::ownTag(""),
          "fremde Tags");
    check(slog::levelLetter(4) == 'I' && slog::levelLetter(6) == 'E' && slog::levelLetter(5) == 'W' &&
              slog::levelLetter(99) == '?',
          "Stufen");
}

} // namespace

int main() {
    testNames();
    testPrune();
    testGrowth();
    testTagsAndLevels();
    std::printf("test_session_log: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
