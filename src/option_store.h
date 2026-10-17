// XRShell — Kern-Optionen eines Spiels in Ebenen (abhängigkeitsfrei, Host-Test
// tests/test_option_store.cpp).
//
//   Profil = xrshell.opt des Spielordners (+ Vorgaben des Adapters, z. B. mouse_input = virtual).
//            Doppelte Schlüssel: der erste gilt (wie bisher).
//   Nutzer = im VR-Menü geändert, pro Spielordner gespeichert (xr.dosgames:
//            <files>/settings/<Ordner>.user.opt), nach dem Profil geladen, überschreibt es.
//            „Spiel-Standard wiederherstellen“ verwirft diese Ebene (Datei wird gelöscht).
//   Aktiv  = was der Core über GET_VARIABLE sieht. Beim Spielstart = Profil + Nutzer. Danach
//            wirken nur Live-Änderungen sofort (GET_VARIABLE_UPDATE meldet sie einmal); Optionen,
//            die der Core nur beim Start liest, bleiben bis zum Neustart „ausstehend“.
//
// Threads: Main-Thread schreibt, Core-Thread liest — alles unter einem Mutex. Werte an den
// Core zeigen auf internierte Strings (std::set, nie geleert): der Zeiger bleibt gültig,
// auch wenn sich der Wert später ändert (der Core darf ihn behalten).
//
// Daneben die Optionsliste, die der Core selbst meldet (SET_VARIABLES / SET_CORE_OPTIONS*):
// Schlüssel, erlaubte Werte mit Beschriftung, Standardwert.

#pragma once

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace optstore {

using Entries = std::vector<std::pair<std::string, std::string>>;

inline std::string trim(const std::string& t, const char* ws = " \t\r\n\"") {
    const size_t b = t.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    return t.substr(b, t.find_last_not_of(ws) - b + 1);
}

// Eine Option pro Zeile `schlüssel = wert`, Anführungszeichen optional, `#` = Kommentar.
inline Entries parse(const std::string& text) {
    Entries out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key.empty() || value.empty()) continue;
        out.emplace_back(std::move(key), std::move(value));
    }
    return out;
}

inline std::string serialize(const Entries& entries, const std::string& header) {
    std::string out = header;
    for (const auto& e : entries) out += e.first + " = " + e.second + "\n";
    return out;
}

inline bool readFile(const std::string& path, std::string& text) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    text.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return true;
}

// Erst <path>.tmp, dann umbenennen — ein Abbruch hinterlässt nie eine halbe Datei.
inline bool writeFile(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) { std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());   // Windows-Host: rename ersetzt nicht
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
#ifndef _WIN32
    ::chmod(path.c_str(), 0664);   // für adb lesbar (settings/*.user.opt), wie das Sitzungslog
#endif
    return true;
}

// Vom Core gemeldete Option.
struct Definition {
    std::string key;
    std::vector<std::pair<std::string, std::string>> values;   // Wert, Beschriftung (leer = Wert)
    std::string defaultValue;
};

// SET_VARIABLES (altes Format): "Beschreibung; wert1|wert2|…", der erste Wert ist der Standard.
inline bool parseLegacyDefinition(const char* key, const char* text, Definition& out) {
    if (!key || !text) return false;
    const std::string s(text);
    const size_t semi = s.find(';');
    if (semi == std::string::npos) return false;
    out = Definition{};
    out.key = key;
    const std::string list = trim(s.substr(semi + 1), " \t");
    size_t start = 0;
    while (start <= list.size()) {
        const size_t bar = list.find('|', start);
        const std::string v = list.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
        if (!v.empty()) out.values.emplace_back(v, std::string());
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (out.values.empty()) return false;
    out.defaultValue = out.values.front().first;
    return true;
}

} // namespace optstore

class OptionStore {
public:
    using Entries = optstore::Entries;
    using LiveFn = bool (*)(const std::string& key);

    // Neues Spiel: Profil (erster Eintrag je Schlüssel gilt) und gespeicherte Nutzerwerte.
    void begin(const Entries& profile, const Entries& user) {
        std::lock_guard<std::mutex> lock(mMutex);
        mProfile.clear();
        mUser.clear();
        mActive.clear();
        for (const auto& e : profile) mProfile.emplace(e.first, e.second);   // emplace: erster gewinnt
        for (const auto& e : user) mUser[e.first] = e.second;               // letzter gewinnt
        mActive = mProfile;
        for (const auto& e : mUser) mActive[e.first] = e.second;
        mUpdated = false;
        mDirty = false;
        mDefinitions.clear();
        mRevision.fetch_add(1);
    }

    // Vorgabe des Adapters (z. B. mouse_input = virtual), nur wenn das Spiel nichts sagt.
    void addDefault(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mProfile.count(key) || mUser.count(key)) return;
        mProfile[key] = value;
        mActive[key] = value;
        mRevision.fetch_add(1);
    }

    // Zählt jede Änderung der aktiven Werte — ohne Lock lesbar. Wer einen aktiven Wert pro Frame
    // braucht (Core-Thread), liest ihn nur neu, wenn sich die Revision geändert hat.
    unsigned revision() const { return mRevision.load(std::memory_order_acquire); }

    // Aktiver Wert für den Core; nullptr = nicht gesetzt (Core-Standard). Zeiger bleibt gültig.
    const char* get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto it = mActive.find(key);
        if (it == mActive.end()) return nullptr;
        return mPool.insert(it->second).first->c_str();
    }

    bool active(const std::string& key, std::string& out) const { return find(mActive, key, out); }
    bool profile(const std::string& key, std::string& out) const { return find(mProfile, key, out); }
    bool user(const std::string& key, std::string& out) const { return find(mUser, key, out); }
    // Wert, der nach einem Neustart gilt (Nutzer vor Profil).
    bool effective(const std::string& key, std::string& out) const {
        return find(mUser, key, out) || find(mProfile, key, out);
    }

    Entries userEntries() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return Entries(mUser.begin(), mUser.end());
    }
    bool hasUserValues() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return !mUser.empty();
    }

    // Nutzerwert setzen. live = sofort aktiv; Kern-Optionen (nicht „xrshell_…“) meldet dann
    // GET_VARIABLE_UPDATE. Sonst wirkt der Wert erst nach dem Neustart.
    void setUser(const std::string& key, const std::string& value, bool live) {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mUser.find(key);
        if (it != mUser.end() && it->second == value) return;
        mUser[key] = value;
        mDirty = true;
        if (!live) return;
        std::string& a = mActive[key];
        if (a == value) return;
        a = value;
        mRevision.fetch_add(1);
        if (key.compare(0, 8, "xrshell_") != 0) mUpdated = true;
    }

    // Alle Nutzerwerte verwerfen. Live-Schlüssel kehren sofort zum Profilwert zurück.
    void resetUser(LiveFn isLive) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& e : mUser) {
            if (!isLive || !isLive(e.first)) continue;
            const auto p = mProfile.find(e.first);
            const auto a = mActive.find(e.first);
            if (p != mProfile.end()) {
                if (a == mActive.end() || a->second != p->second) {
                    mActive[e.first] = p->second;
                    mRevision.fetch_add(1);
                    if (e.first.compare(0, 8, "xrshell_") != 0) mUpdated = true;
                }
            } else if (a != mActive.end()) {
                mActive.erase(a);   // Core-Standard
                mRevision.fetch_add(1);
                if (e.first.compare(0, 8, "xrshell_") != 0) mUpdated = true;
            }
        }
        if (!mUser.empty()) mDirty = true;
        mUser.clear();
    }

    // Nach Neustart wirksam: der Nutzerwert weicht vom aktiven Wert ab.
    bool pending(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto u = mUser.find(key);
        if (u == mUser.end()) {
            // Zurückgesetzt, aber noch nicht neu gestartet: aktiv ≠ Profil.
            const auto a = mActive.find(key), p = mProfile.find(key);
            const bool hasA = a != mActive.end(), hasP = p != mProfile.end();
            return hasA != hasP || (hasA && a->second != p->second);
        }
        const auto a = mActive.find(key);
        return a == mActive.end() || a->second != u->second;
    }

    // GET_VARIABLE_UPDATE: true genau einmal nach einer Live-Änderung.
    bool takeUpdate() {
        std::lock_guard<std::mutex> lock(mMutex);
        const bool u = mUpdated;
        mUpdated = false;
        return u;
    }

    // Nutzerwerte seit dem Laden/Speichern geändert?
    bool dirty() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mDirty;
    }
    void clearDirty() {
        std::lock_guard<std::mutex> lock(mMutex);
        mDirty = false;
    }

    // --- Optionsliste des Cores ---
    void addDefinition(const optstore::Definition& d) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& existing : mDefinitions)
            if (existing.key == d.key) { existing = d; return; }
        mDefinitions.push_back(d);
    }
    bool definition(const std::string& key, optstore::Definition& out) const {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& d : mDefinitions)
            if (d.key == key) { out = d; return true; }
        return false;
    }
    size_t definitionCount() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mDefinitions.size();
    }

private:
    bool find(const std::map<std::string, std::string>& m, const std::string& key, std::string& out) const {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto it = m.find(key);
        if (it == m.end()) return false;
        out = it->second;
        return true;
    }

    mutable std::mutex mMutex;
    std::map<std::string, std::string> mProfile, mUser, mActive;
    std::set<std::string> mPool;   // internierte Werte für den Core (nie geleert)
    std::vector<optstore::Definition> mDefinitions;
    bool mUpdated = false, mDirty = false;
    std::atomic<unsigned> mRevision{0};
};
