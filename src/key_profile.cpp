// XRShell — Tastenprofil pro Spiel. Siehe key_profile.h.

#include "key_profile.h"

#include "libretro_adapter.h"

#include <android/log.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr char kProfileName[] = "xrshell.keys";
constexpr double kComboStep = 0.05;   // Abstand zwischen Tasten einer Kombination (s)

const char* const kInputNames[KeyProfile::kInputCount] = {
    "a", "x", "y", "left_trigger", "right_stick_click",
    "left_stick_left", "left_stick_right", "left_stick_up", "left_stick_down",
    "hires", "game_menu",
    "scroll_left", "scroll_right", "scroll_up", "scroll_down",
};

struct NamedKey { const char* name; enum retro_key key; };
const NamedKey kNamedKeys[] = {
    {"space", RETROK_SPACE}, {"enter", RETROK_RETURN}, {"return", RETROK_RETURN},
    {"esc", RETROK_ESCAPE}, {"escape", RETROK_ESCAPE}, {"tab", RETROK_TAB},
    {"backspace", RETROK_BACKSPACE}, {"insert", RETROK_INSERT}, {"delete", RETROK_DELETE},
    {"home", RETROK_HOME}, {"end", RETROK_END},
    {"pageup", RETROK_PAGEUP}, {"pagedown", RETROK_PAGEDOWN},
    {"up", RETROK_UP}, {"down", RETROK_DOWN}, {"left", RETROK_LEFT}, {"right", RETROK_RIGHT},
    {"lshift", RETROK_LSHIFT}, {"rshift", RETROK_RSHIFT},
    {"lctrl", RETROK_LCTRL}, {"rctrl", RETROK_RCTRL},
    {"lalt", RETROK_LALT}, {"ralt", RETROK_RALT},
    {"pause", RETROK_PAUSE},
    {"minus", RETROK_MINUS}, {"equals", RETROK_EQUALS},
    {"comma", RETROK_COMMA}, {"period", RETROK_PERIOD},
    {"kp_plus", RETROK_KP_PLUS}, {"kp_minus", RETROK_KP_MINUS},
    {"kp0", RETROK_KP0}, {"kp1", RETROK_KP1}, {"kp2", RETROK_KP2}, {"kp3", RETROK_KP3},
    {"kp4", RETROK_KP4}, {"kp5", RETROK_KP5}, {"kp6", RETROK_KP6}, {"kp7", RETROK_KP7},
    {"kp8", RETROK_KP8}, {"kp9", RETROK_KP9},
};

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

enum retro_key keyFromName(const std::string& raw) {
    const std::string name = lower(trim(raw));
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') return static_cast<enum retro_key>(RETROK_a + (c - 'a'));
        if (c >= '0' && c <= '9') return static_cast<enum retro_key>(RETROK_0 + (c - '0'));
    }
    if (name.size() >= 2 && name[0] == 'f') {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) return static_cast<enum retro_key>(RETROK_F1 + (n - 1));
    }
    for (const NamedKey& k : kNamedKeys)
        if (name == k.name) return k.key;
    return RETROK_UNKNOWN;
}

bool isFile(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool isDir(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string parentOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

} // namespace

KeyProfile::KeyProfile() {
    const enum retro_key arrows[4] = { RETROK_LEFT, RETROK_RIGHT, RETROK_UP, RETROK_DOWN };
    for (int i = 0; i < 4; ++i) {
        mBindings[kScrollLeft + i] = { arrows[i] };
        mDefault[kScrollLeft + i] = true;
    }
}

bool KeyProfile::load(const std::string& gamePath) {
    // Spielwechsel (Bibliothek): Belegung des vorigen Spiels verwerfen. Gehaltene Tasten
    // löst der Aufrufer vorher (Menü offen → releaseGameInputs, stopGame leert die Tastatur).
    *this = KeyProfile();
    if (gamePath.empty()) return false;
    // Ordner: erst darin suchen; Datei: daneben. Danach eine Ebene höher (game/).
    const std::string first = isDir(gamePath) ? gamePath : parentOf(gamePath);
    for (const std::string& dir : {first, parentOf(first)}) {
        const std::string path = dir + "/" + kProfileName;
        if (isFile(path)) return parseFile(path);
    }
    LOGI("Kein Tastenprofil (%s) — freie Tasten ohne Funktion", kProfileName);
    return false;
}

bool KeyProfile::parseFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) {
        LOGE("Tastenprofil nicht lesbar: %s", path.c_str());
        return false;
    }
    char buf[256];
    int lineNo = 0;
    while (std::fgets(buf, sizeof(buf), f)) {
        ++lineNo;
        std::string line(buf);
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        line = trim(line);
        if (line.empty()) continue;

        const size_t eq = line.find('=');
        const std::string input = eq == std::string::npos ? line : lower(trim(line.substr(0, eq)));
        int index = -1;
        for (int i = 0; i < kInputCount; ++i)
            if (input == kInputNames[i]) index = i;
        if (eq == std::string::npos || index < 0) {
            LOGE("Tastenprofil Zeile %d: unbekannte Eingabe '%s'", lineNo, input.c_str());
            continue;
        }

        std::vector<enum retro_key> keys;
        std::string rest = line.substr(eq + 1);
        bool ok = true;
        size_t start = 0;
        while (start <= rest.size()) {
            const size_t plus = rest.find('+', start);
            const std::string part = rest.substr(start, plus == std::string::npos
                                                            ? std::string::npos : plus - start);
            const enum retro_key key = keyFromName(part);
            if (key == RETROK_UNKNOWN) {
                LOGE("Tastenprofil Zeile %d: unbekannte Taste '%s'", lineNo, trim(part).c_str());
                ok = false;
                break;
            }
            keys.push_back(key);
            if (plus == std::string::npos) break;
            start = plus + 1;
        }
        if (ok && !keys.empty()) {
            mBindings[index] = keys;
            mDefault[index] = false;
            mBindingText[index] = trim(rest);
            LOGI("Tastenprofil: %s = %s", kInputNames[index], trim(rest).c_str());
        }
    }
    std::fclose(f);
    LOGI("Tastenprofil geladen: %s", path.c_str());
    return !empty();
}

void KeyProfile::setDefault(Input input, const std::string& text) {
    if (!mBindings[input].empty()) return;   // Profil hat Vorrang
    std::vector<enum retro_key> keys;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t plus = text.find('+', start);
        const enum retro_key key = keyFromName(text.substr(start, plus == std::string::npos ? std::string::npos
                                                                                               : plus - start));
        if (key == RETROK_UNKNOWN) return;
        keys.push_back(key);
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    mBindings[input] = keys;
    mDefault[input] = true;
    mBindingText[input] = text;
}

bool KeyProfile::empty() const {
    for (int i = 0; i < kInputCount; ++i)
        if (bound(static_cast<Input>(i))) return false;
    return true;
}

void KeyProfile::releaseNow(Input input, LibretroAdapter& adapter) {
    const auto& keys = mBindings[input];
    while (mKeysDown[input] > 0) releaseKey(keys[--mKeysDown[input]], adapter);
    mHeld[input] = false;
}

void KeyProfile::pressKey(enum retro_key key, LibretroAdapter& adapter) {
    if (mRefs[key]++ == 0) adapter.setKey(key, true);
}

void KeyProfile::releaseKey(enum retro_key key, LibretroAdapter& adapter) {
    if (mRefs[key] > 0 && --mRefs[key] == 0) adapter.setKey(key, false);
}

void KeyProfile::update(const bool pressed[kInputCount], double now,
                        LibretroAdapter& adapter) {
    for (int i = 0; i < kInputCount; ++i) {
        const auto& keys = mBindings[i];
        if (keys.empty()) continue;
        // mPressTime = Zeitpunkt des letzten Tastendrucks dieser Eingabe. Jeder
        // Schritt zählt ab dem tatsächlichen Druck, nicht ab dem Kombi-Start —
        // sonst verkürzt ein Frame Verzug die Haltezeit der letzten Taste.
        if (pressed[i] && !mHeld[i]) {
            mHeld[i] = true;
            pressKey(keys[0], adapter);
            mKeysDown[i] = 1;
            mPressTime[i] = now;
        }
        if (!mHeld[i]) continue;
        const bool stepDone = now >= mPressTime[i] + kComboStep;
        // Kombination: nächste Taste, wenn die vorige kComboStep lag.
        if (mKeysDown[i] < keys.size() && stepDone) {
            pressKey(keys[mKeysDown[i]++], adapter);
            mPressTime[i] = now;
            continue;
        }
        // Loslassen erst, wenn die Kombination komplett ist und die letzte Taste
        // mindestens kComboStep lag — ein kurzer Tipp geht sonst zwischen zwei
        // Core-Frames verloren. In umgekehrter Reihenfolge, Modifikator zuletzt.
        if (!pressed[i] && mKeysDown[i] == keys.size() && stepDone) {
            while (mKeysDown[i] > 0) releaseKey(keys[--mKeysDown[i]], adapter);
            mHeld[i] = false;
        }
    }
}
