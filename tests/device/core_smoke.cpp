// Core-Smoke-Test auf der Quest (arm64, ohne OpenXR/GL/AAudio): lädt einen echten libretro-Core
// mit den echten Spieldaten des Spielers genau so, wie XRShell es tut, spielt ein zeitgesteuertes
// Eingabe-Skript (Quest-Tasten → RetroPad über dieselben Funktionen wie die App) und misst Bild,
// Ton, Tempo, Rumble, Savestates und Schreibzugriffe. Läuft ungebremst (schneller als Echtzeit).
//
//   core_smoke <core.so> <spielordner> <arbeitsordner> <skript.txt>
//
// Spielordner: …/games/<system>/<Name> (der Elternordner bestimmt den Core wie in der App:
// doom|quake|wolf3d → Shooter-Core mit Startdatei-Regel, sonst DOSBox-Pure mit dem Ordner als
// Laufwerk C:). Arbeitsordner: system/, saves/ (Core-Verzeichnisse), out/ (Bericht, Bilder,
// Core-Log). Gleiche Abläufe wie src/libretro_adapter.cpp (start, environmentCallback,
// queryInputState) und src/main.cpp (updateGamepad) — bei Änderungen dort hier nachziehen.
//
// Skript (eine Zeile pro Schritt, „#“ = Kommentar):
//   <name> <halten s> <warten s> [taste …]
//   Tasten: A B X Y TR (Trigger rechts) GR (Griff rechts) TL (Trigger links) RS LS (Stick-Klicks),
//           lx=… ly=… rx=… ry=… (OpenXR: +x rechts, +y vorn, -1..1)
//   Nach jedem Schritt: PNG <nr>-<name>.png und eine Berichtszeile.
//   Sonderschritte: @savestate (Savestate-Rundlauf mit Determinismus-Vergleich)
//
// Ergebnis: out/bericht.txt (auch stdout), out/bericht.json, out/core.log, out/*.png.
// Keine Headset-Abnahme — prüft nur den Core-Pfad.

#include "core_defaults.h"
#include "core_select.h"
#include "game_folders.h"
#include "libretro.h"
#include "option_store.h"
#include "pad_analog.h"
#include "pad_mapping.h"

#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

using namespace core_select;
using Clock = std::chrono::steady_clock;

constexpr int kTextureW = 1024, kTextureH = 768;   // QuadRenderer::kTextureW/H

// ---------------------------------------------------------------- Ausgabe
FILE* gReport = nullptr;   // bericht.txt
FILE* gCoreLog = nullptr;  // core.log
std::string gOut;

void say(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fputs(buf, stdout);
    std::fflush(stdout);
    if (gReport) { std::fputs(buf, gReport); std::fflush(gReport); }
}

// ---------------------------------------------------------------- Zustand
OptionStore gOptions;
std::vector<optstore::Definition> gDefs;
std::map<std::string, std::string> gQueried;     // GET_VARIABLE: Schlüssel → Antwort (erste)
std::map<unsigned, int> gUnknownEnv;             // nicht bediente Umgebungsbefehle
std::map<unsigned, int> gEnvCount;
std::string gSystemDir, gSaveDir;
bool gPadMode = false;
int gFormat = RETRO_PIXEL_FORMAT_0RGB1555;
std::vector<int> gFormatChanges;
double gFps = 60.0, gRate = 44100.0;
struct AvChange { long frame; double fps, rate; unsigned w, h; };
std::vector<AvChange> gAvChanges;
std::map<std::string, long> gGeometries;         // "WxH" → erster Frame
long gGeometryCalls = 0;
bool gShutdown = false;
long gShutdownFrame = -1;
long gRun = 0;
bool gDone = false;

// Bild
std::vector<uint32_t> gFrame;                    // XRGB8888 des letzten Frames
unsigned gW = 0, gH = 0;
long gVideoFrames = 0, gDupes = 0, gTooBig = 0;
std::map<std::string, long> gFrameSizes;         // "WxH" → Anzahl

// Ton
long gAudioFrames = 0, gAudioLoud = 0;           // Frames, davon hörbar (|s| > 64)
long gStepAudio = 0, gStepLoud = 0;
double gExpectedAudio = 0.0;
double gFill = 0.0, gFillMin = 1e9;              // simulierter Ringpuffer (wie CoreAudio: ~80 ms Vorrat)
long gUnderruns = 0;
bool gAudioStarted = false;

// Rumble
long gRumbleCalls = 0, gRumbleOn = 0, gStepRumble = 0;
uint16_t gRumbleMax[2] = {0, 0};

// Eingabe (Pad-Modus wie LibretroAdapter::queryInputState)
uint16_t gPadMask = 0;
padmap::Analog gPadAnalog;
long gInputQueries = 0;
std::set<unsigned> gDevicesQueried;

// ---------------------------------------------------------------- Callbacks
void coreLog(enum retro_log_level level, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    static const char* const kLevel[] = {"D", "I", "W", "E"};
    if (gCoreLog) { std::fprintf(gCoreLog, "[%ld %s] %s", gRun, kLevel[level & 3], buf); std::fflush(gCoreLog); }
    if (level >= RETRO_LOG_WARN) say("  [core %s] %s", kLevel[level & 3], buf);
}

void addDefinition(const optstore::Definition& d) {
    for (auto& e : gDefs)
        if (e.key == d.key) { e = d; return; }
    gDefs.push_back(d);
}

// Wie LibretroAdapter::addCoreOptionDefinitions.
void addCoreOptionDefinitions(unsigned cmd, const void* data) {
    auto add = [&](const char* key, const retro_core_option_value* values, const char* def) {
        optstore::Definition d;
        d.key = key;
        for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && values[i].value; ++i)
            d.values.emplace_back(values[i].value, values[i].label ? values[i].label : "");
        d.defaultValue = def ? def : (d.values.empty() ? "" : d.values.front().first);
        addDefinition(d);
    };
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (const auto* var = static_cast<const retro_variable*>(data); var->key; ++var) {
            optstore::Definition d;
            if (optstore::parseLegacyDefinition(var->key, var->value, d)) addDefinition(d);
        }
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        for (auto* d = static_cast<const retro_core_option_definition*>(data); d && d->key; ++d)
            add(d->key, d->values, d->default_value);
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        for (auto* d = static_cast<const retro_core_options_intl*>(data)->us; d && d->key; ++d)
            add(d->key, d->values, d->default_value);
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const retro_core_options_v2* o = cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2
            ? static_cast<const retro_core_options_v2*>(data)
            : static_cast<const retro_core_options_v2_intl*>(data)->us;
        for (auto* d = o ? o->definitions : nullptr; d && d->key; ++d) add(d->key, d->values, d->default_value);
        break;
    }
    default:
        break;
    }
}

void setGeometry(const retro_game_geometry& g) {
    ++gGeometryCalls;
    const std::string key = std::to_string(g.base_width) + "x" + std::to_string(g.base_height);
    if (!gGeometries.count(key)) gGeometries[key] = gRun;
}

// Antworten wie LibretroAdapter::environmentCallback (gleiche true/false), dazu Zähler.
bool environment(unsigned cmd, void* data) {
    ++gEnvCount[cmd];
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto* fmt = static_cast<const retro_pixel_format*>(data);
        const bool ok = fmt && (*fmt == RETRO_PIXEL_FORMAT_XRGB8888 || *fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                                *fmt == RETRO_PIXEL_FORMAT_0RGB1555);
        if (ok) { gFormat = *fmt; gFormatChanges.push_back(gFormat); }
        return ok;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) return false;
        static_cast<retro_log_callback*>(data)->log = coreLog;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data) return false;
        *static_cast<const char**>(data) =
            (cmd == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY ? gSaveDir : gSystemDir).c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* var = static_cast<retro_variable*>(data);
        if (!var || !var->key) return false;
        if (std::strncmp(var->key, "xrshell_", 8) == 0) { var->value = nullptr; return false; }
        var->value = gOptions.get(var->key);
        if (!gQueried.count(var->key)) gQueried[var->key] = var->value ? var->value : "(Core-Standard)";
        return var->value != nullptr;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (!data) return false;
        *static_cast<bool*>(data) = gOptions.takeUpdate();
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        if (data) addCoreOptionDefinitions(cmd, data);
        return false;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return gPadMode;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        return data != nullptr;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: {
        auto* rumble = static_cast<retro_rumble_interface*>(data);
        if (!rumble) return false;
        rumble->set_rumble_state = [](unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
            ++gRumbleCalls;
            if (port != 0 || effect > RETRO_RUMBLE_WEAK) return false;
            if (strength > 0) { ++gRumbleOn; ++gStepRumble; }
            gRumbleMax[effect] = std::max(gRumbleMax[effect], strength);
            return true;
        };
        return true;
    }
    case RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE:   // wie XRShell: AAudio-Rate der Quest
        if (!data) return false;
        *static_cast<unsigned*>(data) = 48000u;
        return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:  // wie XRShell: Anzeige-Rate (72 Hz)
        if (!data) return false;
        *static_cast<float*>(data) = 72.0f;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (!data) return false;
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        return data != nullptr;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (!data) return false;
        setGeometry(*static_cast<const retro_game_geometry*>(data));
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const auto* av = static_cast<const retro_system_av_info*>(data);
        if (!av) return false;
        setGeometry(av->geometry);
        if (av->timing.sample_rate > 0.0) gRate = av->timing.sample_rate;
        if (av->timing.fps > 1.0) gFps = std::min(av->timing.fps, 1000.0);
        gAvChanges.push_back({gRun, av->timing.fps, av->timing.sample_rate, av->geometry.base_width,
                              av->geometry.base_height});
        say("  SET_SYSTEM_AV_INFO bei Frame %ld: %.2f fps, %.0f Hz, %ux%u\n", gRun, av->timing.fps,
            av->timing.sample_rate, av->geometry.base_width, av->geometry.base_height);
        return true;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
        // Wie XRShell: annehmen (true) — die App stoppt dann das Spiel und zeigt die Auswahl.
        if (!gShutdown) say("  Core fordert Beenden an (RETRO_ENVIRONMENT_SHUTDOWN) bei Frame %ld\n", gRun);
        gShutdown = true;
        if (gShutdownFrame < 0) gShutdownFrame = gRun;
        return true;
    default:
        ++gUnknownEnv[cmd];
        return false;
    }
}

void video(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) { ++gDupes; return; }
    if (w == 0 || h == 0) return;
    ++gFrameSizes[std::to_string(w) + "x" + std::to_string(h)];
    if (w > static_cast<unsigned>(kTextureW) || h > static_cast<unsigned>(kTextureH)) {
        ++gTooBig;   // XRShell verwirft solche Frames (Bild steht)
        return;
    }
    ++gVideoFrames;
    gW = w;
    gH = h;
    gFrame.resize(static_cast<size_t>(w) * h);
    const auto* src = static_cast<const uint8_t*>(data);
    if (gFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
        for (unsigned y = 0; y < h; ++y)
            std::memcpy(&gFrame[static_cast<size_t>(y) * w], src + y * pitch, static_cast<size_t>(w) * 4);
        return;
    }
    const bool is565 = gFormat == RETRO_PIXEL_FORMAT_RGB565;
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            uint16_t p;
            std::memcpy(&p, src + y * pitch + x * 2, 2);
            uint32_t r, g, b;
            if (is565) { r = (p >> 11) & 0x1F; g = (p >> 5) & 0x3F; b = p & 0x1F;
                         r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2; }
            else       { r = (p >> 10) & 0x1F; g = (p >> 5) & 0x1F; b = p & 0x1F;
                         r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2; }
            gFrame[static_cast<size_t>(y) * w + x] = r << 16 | g << 8 | b;
        }
    }
}

void countAudio(const int16_t* s, size_t frames) {
    gAudioFrames += static_cast<long>(frames);
    gStepAudio += static_cast<long>(frames);
    gFill += static_cast<double>(frames);
    long loud = 0;
    for (size_t i = 0; i < frames; ++i)
        if (std::abs(s[2 * i]) > 64 || std::abs(s[2 * i + 1]) > 64) ++loud;
    gAudioLoud += loud;
    gStepLoud += loud;
}

void audioSample(int16_t l, int16_t r) {
    const int16_t f[2] = {l, r};
    countAudio(f, 1);
}

size_t audioBatch(const int16_t* data, size_t frames) {
    countAudio(data, frames);
    return frames;
}

void inputPoll() {}

// Wie LibretroAdapter::queryInputState im Pad-Modus (nur Port 0 belegt).
int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    ++gInputQueries;
    gDevicesQueried.insert(device);
    if (!gPadMode) return 0;   // Maus-Modus (DOS ohne pad) bekommt im Test keine Eingaben
    if (port >= 2) return 0;
    const uint16_t mask = port == 0 ? gPadMask : 0;
    if (device == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) return id < 16 && (mask & (1u << id)) ? 0x7FFF : 0;
        if (index > RETRO_DEVICE_INDEX_ANALOG_RIGHT || id > RETRO_DEVICE_ID_ANALOG_Y || port != 0) return 0;
        const int16_t v[4] = {gPadAnalog.lx, gPadAnalog.ly, gPadAnalog.rx, gPadAnalog.ry};
        return v[index * 2 + id];
    }
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(mask);
    return id < 16 && (mask & (1u << id)) ? 1 : 0;
}

// ---------------------------------------------------------------- Quest-Eingabe → RetroPad
struct QuestInput {
    bool src[padmap::kSourceCount] = {};
    float lx = 0, ly = 0, rx = 0, ry = 0;
};

padmap::Mapping gMap;
bool gStickHeld[2][4] = {};

// Wie updateGamepad in src/main.cpp (fokussiert, ohne linken Griff, rechter Griff frei).
void applyInput(const QuestInput& in) {
    const auto modeL = static_cast<padmap::StickMode>(gMap.stick[padmap::kLeftStick]);
    const auto modeR = static_cast<padmap::StickMode>(gMap.stick[padmap::kRightStick]);
    const unsigned axes = padmap::stickDpadAxes(modeL) | (padmap::stickDpadAxes(modeR) << 2);
    const unsigned highAxes = (padmap::stickDpadHigh(modeL) ? padmap::stickDpadAxes(modeL) : 0u) |
                              ((padmap::stickDpadHigh(modeR) ? padmap::stickDpadAxes(modeR) : 0u) << 2);
    uint16_t mask = padmap::stickDpadMask(gStickHeld, in.lx, in.ly, in.rx, in.ry, axes, highAxes);
    const padmap::Analog raw = padmap::sticksToAnalog(in.lx, in.ly, in.rx, in.ry);
    padmap::Analog analog;
    const int16_t values[2][2] = {{raw.lx, raw.ly}, {raw.rx, raw.ry}};
    int16_t* slots[2][2] = {{&analog.lx, &analog.ly}, {&analog.rx, &analog.ry}};
    const int targets[2] = {padmap::stickAnalogTarget(modeL), padmap::stickAnalogTarget(modeR)};
    for (int s = 0; s < 2; ++s) {
        const int t = targets[s];
        if (t < 0) continue;
        if (std::abs(values[s][0]) + std::abs(values[s][1]) >= std::abs(*slots[t][0]) + std::abs(*slots[t][1])) {
            *slots[t][0] = values[s][0];
            *slots[t][1] = values[s][1];
        }
    }
    mask = static_cast<uint16_t>(mask | padmap::buttonMask(gMap, in.src));
    gPadMask = mask;
    gPadAnalog = analog;
}

// ---------------------------------------------------------------- Skript
struct Step {
    std::string name;
    double hold = 0, wait = 0;
    QuestInput in;
    std::string keys;   // für den Bericht
};

bool parseScript(const std::string& path, std::vector<Step>& out) {
    std::string text;
    if (!optstore::readFile(path, text)) return false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::vector<std::string> tok;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            size_t j = i;
            while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
            if (j > i) tok.push_back(line.substr(i, j - i));
            i = j;
        }
        if (tok.empty()) continue;
        Step s;
        s.name = tok[0];
        if (tok.size() > 1) s.hold = std::atof(tok[1].c_str());
        if (tok.size() > 2) s.wait = std::atof(tok[2].c_str());
        static const char* const kNames[padmap::kSourceCount] = {"A", "B", "X", "Y", "TR", "GR", "TL", "RS", "LS"};
        for (size_t k = 3; k < tok.size(); ++k) {
            const std::string& t = tok[k];
            bool known = false;
            for (int n = 0; n < padmap::kSourceCount; ++n)
                if (t == kNames[n]) { s.in.src[n] = true; known = true; }
            if (t.size() > 3 && t[2] == '=') {
                const float v = static_cast<float>(std::atof(t.c_str() + 3));
                const std::string axis = t.substr(0, 2);
                known = true;
                if (axis == "lx") s.in.lx = v;
                else if (axis == "ly") s.in.ly = v;
                else if (axis == "rx") s.in.rx = v;
                else if (axis == "ry") s.in.ry = v;
                else known = false;
            }
            if (!known) { std::fprintf(stderr, "Skript: unbekannte Taste %s\n", t.c_str()); return false; }
            s.keys += (s.keys.empty() ? "" : " ") + t;
        }
        out.push_back(s);
    }
    return !out.empty();
}

// ---------------------------------------------------------------- Bildmaße
uint64_t frameHash() {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t p : gFrame) { h ^= (p & 0xFFFFFFu); h *= 1099511628211ull; }
    return h ^ (static_cast<uint64_t>(gW) << 32 | gH);
}

double nonBlack() {
    if (gFrame.empty()) return 0.0;
    size_t n = 0;
    for (uint32_t p : gFrame)
        if (((p >> 16) & 0xFF) >= 16 || ((p >> 8) & 0xFF) >= 16 || (p & 0xFF) >= 16) ++n;
    return static_cast<double>(n) / static_cast<double>(gFrame.size());
}

void writePng(const std::string& name) {
    if (gFrame.empty()) return;
    std::vector<uint8_t> rgb(gFrame.size() * 3);
    for (size_t i = 0; i < gFrame.size(); ++i) {
        rgb[i * 3] = static_cast<uint8_t>(gFrame[i] >> 16);
        rgb[i * 3 + 1] = static_cast<uint8_t>(gFrame[i] >> 8);
        rgb[i * 3 + 2] = static_cast<uint8_t>(gFrame[i]);
    }
    const std::string path = gOut + "/" + name + ".png";
    stbi_write_png(path.c_str(), static_cast<int>(gW), static_cast<int>(gH), 3, rgb.data(), static_cast<int>(gW) * 3);
}

// ---------------------------------------------------------------- Dateibaum
// Inhalt (FNV-1a 64) statt nur Zeitstempel: DOSBox-Pure öffnet Originale nur lesend und schreibt
// per Copy-on-Write ins Overlay, berührt dabei aber das Datum (Fehlalarm „DEFAULT.CFG geändert“).
struct FileInfo { long long size; long long mtime; uint64_t hash; };

uint64_t fileHash(const std::string& path) {
    uint64_t h = 1469598103934665603ull;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    static unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        for (size_t i = 0; i < n; ++i) h = (h ^ buf[i]) * 1099511628211ull;
    std::fclose(f);
    return h;
}

void walk(const std::string& dir, const std::string& rel, std::map<std::string, FileInfo>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        const std::string path = dir + "/" + e->d_name, r = rel.empty() ? e->d_name : rel + "/" + e->d_name;
        struct stat st {};
        if (::stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walk(path, r, out);
        else out[r] = {static_cast<long long>(st.st_size), static_cast<long long>(st.st_mtime), fileHash(path)};
    }
    closedir(d);
}

// ---------------------------------------------------------------- Laufzeit
struct Stats { double mean = 0, p95 = 0, max = 0; size_t n = 0; };
Stats stats(std::vector<double> v) {
    Stats s;
    if (v.empty()) return s;
    s.n = v.size();
    double sum = 0;
    for (double x : v) sum += x;
    s.mean = sum / static_cast<double>(v.size());
    std::sort(v.begin(), v.end());
    s.p95 = v[std::min(v.size() - 1, static_cast<size_t>(static_cast<double>(v.size()) * 0.95))];
    s.max = v.back();
    return s;
}

std::vector<double> gRunMs;          // alle retro_run-Zeiten
void (*gRetroRun)() = nullptr;

// Ein Frame: Eingabe setzen, retro_run, Zeit und simulierten Tonpuffer führen.
double runFrame(const QuestInput& in) {
    applyInput(in);
    const auto t0 = Clock::now();
    gRetroRun();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    gRunMs.push_back(ms);
    // Ton: Verbraucher zieht in Echtzeit rate/fps pro Frame; Start mit ~80 ms Vorrat
    // (CoreAudio::mTargetFill), sobald der erste Ton kam.
    const double perFrame = gRate / gFps;
    gExpectedAudio += perFrame;
    if (!gAudioStarted && gAudioFrames > 0) { gAudioStarted = true; gFill = 4096.0; }
    if (gAudioStarted) {
        gFill -= perFrame;
        if (gFill < 0.0) { ++gUnderruns; gFill = 0.0; }
        gFillMin = std::min(gFillMin, gFill);
    }
    ++gRun;
    return ms;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) < 32) continue;
        o += c;
    }
    return o;
}

template <typename T> T sym(void* h, const char* name) { return reinterpret_cast<T>(dlsym(h, name)); }

void atExit() {
    if (!gDone) say("ABBRUCH: Prozess endet in Frame %ld ohne Abschluss (Core ruft exit()?)\n", gRun);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("Aufruf: %s <core.so> <spielordner> <arbeitsordner> <skript.txt>\n", argv[0]);
        return 2;
    }
    const std::string corePath = argv[1], gamePath = argv[2], work = argv[3], scriptPath = argv[4];
    gSystemDir = work + "/system";
    gSaveDir = work + "/saves";
    gOut = work + "/out";
    for (const std::string& d : {work, gSystemDir, gSaveDir, gOut}) ::mkdir(d.c_str(), 0755);
    gReport = std::fopen((gOut + "/bericht.txt").c_str(), "w");
    gCoreLog = std::fopen((gOut + "/core.log").c_str(), "w");
    std::atexit(atExit);

    std::vector<Step> script;
    if (!parseScript(scriptPath, script)) { say("FEHLER: Skript %s unlesbar/leer\n", scriptPath.c_str()); return 2; }

    // --- Core und Startdatei wie LibretroAdapter::start
    std::string contentPath = gamePath;
    const CoreSpec* core = nullptr;
    {
        const std::string parent = gamePath.substr(0, gamePath.rfind('/'));
        const std::string system = parent.substr(parent.rfind('/') + 1);
        for (const CoreSpec* c : kCores)
            if (c->startRule != StartRule::None && c->folder && system == c->folder) core = c;
    }
    if (core) {
        contentPath = findStartFile(gamePath, core->startRule);
        if (contentPath.empty()) { say("FEHLER: keine Startdatei (%s) in %s\n", core->systemLabel, gamePath.c_str()); return 1; }
    } else {
        core = &kDosboxCore;
    }
    // Kern-Optionen: xrshell.opt im Ordner, sonst im Elternordner (wie loadCoreOptions).
    optstore::Entries profile;
    std::string optPath;
    for (const std::string& p : {gamePath + "/xrshell.opt", gamePath.substr(0, gamePath.rfind('/')) + "/xrshell.opt"}) {
        std::string text;
        if (!optstore::readFile(p, text)) continue;
        profile = optstore::parse(text);
        optPath = p;
        break;
    }
    gOptions.begin(profile, {});
    if (const char* input = gOptions.get("xrshell_input"))
        if (core == &kDosboxCore && !std::strcmp(input, "pad")) core = &kDosboxPadCore;
    gPadMode = core->padMode;
    const std::string padFolder = core == &kDosboxPadCore ? "dospad" : core->folder ? core->folder : "";
    for (const auto& o : coredefaults::forCore(core)) gOptions.addDefault(o.first, o.second);
    gMap = padmap::loadFor(gamePath, work, padFolder);

    say("=== Core-Smoke-Test: %s\n", gamePath.c_str());
    say("Core: %s (%s), Startdatei: %s\n", core->soName, gPadMode ? "Gamepad" : "Maus", contentPath.c_str());
    say("Kern-Optionen: %s (%zu Einträge)%s\n", optPath.empty() ? "keine xrshell.opt" : optPath.c_str(),
        profile.size(), "");
    say("Belegung: %s\n", padmap::serialize(gMap).c_str());

    std::map<std::string, FileInfo> before;
    walk(gamePath, "", before);

    // --- Core laden
    void* h = dlopen(corePath.c_str(), RTLD_NOW);
    if (!h) { say("FEHLER: dlopen: %s\n", dlerror()); return 1; }
    auto setEnv = sym<void (*)(retro_environment_t)>(h, "retro_set_environment");
    auto setVideo = sym<void (*)(retro_video_refresh_t)>(h, "retro_set_video_refresh");
    auto setSample = sym<void (*)(retro_audio_sample_t)>(h, "retro_set_audio_sample");
    auto setBatch = sym<void (*)(retro_audio_sample_batch_t)>(h, "retro_set_audio_sample_batch");
    auto setPoll = sym<void (*)(retro_input_poll_t)>(h, "retro_set_input_poll");
    auto setState = sym<void (*)(retro_input_state_t)>(h, "retro_set_input_state");
    auto init = sym<void (*)()>(h, "retro_init");
    auto deinit = sym<void (*)()>(h, "retro_deinit");
    auto apiVersion = sym<unsigned (*)()>(h, "retro_api_version");
    auto getInfo = sym<void (*)(retro_system_info*)>(h, "retro_get_system_info");
    auto getAv = sym<void (*)(retro_system_av_info*)>(h, "retro_get_system_av_info");
    auto loadGame = sym<bool (*)(const retro_game_info*)>(h, "retro_load_game");
    auto unloadGame = sym<void (*)()>(h, "retro_unload_game");
    gRetroRun = sym<void (*)()>(h, "retro_run");
    auto serializeSize = sym<size_t (*)()>(h, "retro_serialize_size");
    auto serialize = sym<bool (*)(void*, size_t)>(h, "retro_serialize");
    auto unserialize = sym<bool (*)(const void*, size_t)>(h, "retro_unserialize");
    auto setPortDevice = sym<void (*)(unsigned, unsigned)>(h, "retro_set_controller_port_device");
    if (!setEnv || !setVideo || !setSample || !setBatch || !setPoll || !setState || !init || !deinit ||
        !apiVersion || !getInfo || !getAv || !loadGame || !gRetroRun) {
        say("FEHLER: Core-API unvollständig\n");
        return 1;
    }
    setEnv(environment);
    setVideo(video);
    setSample(audioSample);
    setBatch(audioBatch);
    setPoll(inputPoll);
    setState(inputState);
    retro_system_info info{};
    getInfo(&info);
    say("Core-Info: %s %s, need_fullpath=%d\n", info.library_name, info.library_version, info.need_fullpath ? 1 : 0);
    std::string blob;
    if (!info.need_fullpath && !optstore::readFile(contentPath, blob)) { say("FEHLER: Spieldatei unlesbar\n"); return 1; }

    const auto tLoad = Clock::now();
    init();
    retro_game_info game{};
    game.path = contentPath.c_str();
    if (!blob.empty()) { game.data = blob.data(); game.size = blob.size(); }
    if (!loadGame(&game)) { say("FEHLER: retro_load_game fehlgeschlagen\n"); deinit(); return 1; }
    if (core->padDevice && setPortDevice) setPortDevice(0, core->padDevice);
    retro_system_av_info av{};
    getAv(&av);
    const double loadMs = std::chrono::duration<double, std::milli>(Clock::now() - tLoad).count();
    if (av.timing.fps > 1.0) gFps = std::min(av.timing.fps, 1000.0);
    if (av.timing.sample_rate > 0.0) gRate = av.timing.sample_rate;
    const double startFps = gFps, startRate = gRate;
    setGeometry(av.geometry);
    say("Laden: %.0f ms; AV: %ux%u (max %ux%u) @ %.2f fps, %.0f Hz; Gerät Port 0: 0x%x\n", loadMs,
        av.geometry.base_width, av.geometry.base_height, av.geometry.max_width, av.geometry.max_height,
        av.timing.fps, av.timing.sample_rate, core->padDevice);
    if (av.geometry.max_width > static_cast<unsigned>(kTextureW) || av.geometry.max_height > static_cast<unsigned>(kTextureH))
        say("WARNUNG: Maximalgeometrie größer als %dx%d-Textur\n", kTextureW, kTextureH);

    // --- Skript
    say("\n%-3s %-18s %-14s %6s %8s %8s %8s %7s %7s %6s %5s\n", "Nr", "Schritt", "Tasten", "Frames",
        "ms Mitte", "ms p95", "ms max", "Bild", "hell%", "Ton%", "Rumb");
    struct StepResult { std::string name, keys; long frames; Stats ms; bool changed; double bright, loud; long rumble; };
    std::vector<StepResult> results;
    std::string stateResult = "nicht geprüft";
    size_t stateSize = 0;
    int stateMatch = -1, stateFrames = 0;
    const QuestInput idle;
    int nr = 0;
    for (const Step& st : script) {
        ++nr;
        if (gShutdown && gRun > gShutdownFrame + static_cast<long>(gFps)) break;
        if (st.name == "@savestate") {
            if (!serializeSize || !serialize || !unserialize) { stateResult = "Core ohne Savestates"; continue; }
            stateSize = serializeSize();
            std::vector<uint8_t> buf(stateSize);
            const auto t0 = Clock::now();
            const bool saved = stateSize > 0 && serialize(buf.data(), stateSize);
            const double saveMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            stateFrames = static_cast<int>(gFps);   // 1 s mit fester Eingabe (vorwärts + drehen)
            QuestInput move;
            move.ly = 0.6f;
            move.rx = 0.4f;
            std::vector<uint64_t> a, b;
            for (int i = 0; i < stateFrames; ++i) { runFrame(move); a.push_back(frameHash()); }
            const auto t1 = Clock::now();
            const bool loaded = saved && unserialize(buf.data(), stateSize);
            const double loadStateMs = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
            for (int i = 0; i < stateFrames; ++i) { runFrame(move); b.push_back(frameHash()); }
            stateMatch = 0;
            for (int i = 0; i < stateFrames; ++i) stateMatch += a[static_cast<size_t>(i)] == b[static_cast<size_t>(i)];
            char line[256];
            std::snprintf(line, sizeof(line), "%zu Bytes, speichern %s (%.1f ms), laden %s (%.1f ms), "
                          "danach %d/%d Frames bildgleich", stateSize, saved ? "ok" : "FEHLER", saveMs,
                          loaded ? "ok" : "FEHLER", loadStateMs, stateMatch, stateFrames);
            stateResult = line;
            say("%-3d %-18s Savestate: %s\n", nr, "@savestate", line);
            continue;
        }
        const uint64_t h0 = frameHash();
        gStepAudio = gStepLoud = gStepRumble = 0;
        std::vector<double> ms;
        const long holdFrames = std::lround(st.hold * gFps), waitFrames = std::lround(st.wait * gFps);
        for (long i = 0; i < holdFrames; ++i) ms.push_back(runFrame(st.in));
        for (long i = 0; i < waitFrames; ++i) ms.push_back(runFrame(idle));
        char png[64];
        std::snprintf(png, sizeof(png), "%02d-%s", nr, st.name.c_str());
        writePng(png);
        StepResult r{st.name, st.keys, holdFrames + waitFrames, stats(ms), frameHash() != h0, nonBlack() * 100.0,
                     gStepAudio > 0 ? 100.0 * static_cast<double>(gStepLoud) / static_cast<double>(gStepAudio) : 0.0,
                     gStepRumble};
        results.push_back(r);
        say("%-3d %-18s %-14s %6ld %8.2f %8.2f %8.2f %7s %7.1f %6.1f %5ld\n", nr, r.name.c_str(),
            r.keys.empty() ? "-" : r.keys.c_str(), r.frames, r.ms.mean, r.ms.p95, r.ms.max,
            r.changed ? "neu" : "gleich", r.bright, r.loud, r.rumble);
    }

    // --- Auswertung
    const long skip = std::min(static_cast<long>(gRunMs.size()), static_cast<long>(startFps));
    const Stats all = stats(std::vector<double>(gRunMs.begin() + skip, gRunMs.end()));
    const double budget = 1000.0 / 72.0;
    say("\nFrames: %ld retro_run, %ld Bilder, %ld Wiederholungen (dupe), %ld zu groß für %dx%d (verworfen)\n",
        gRun, gVideoFrames, gDupes, gTooBig, kTextureW, kTextureH);
    std::string sizes;
    for (const auto& s : gFrameSizes) sizes += s.first + " (" + std::to_string(s.second) + ") ";
    say("Bildgrößen: %s\n", sizes.c_str());
    std::string fmts;
    for (int f : gFormatChanges) fmts += std::to_string(f) + " ";
    say("Pixelformat: %s(0=0RGB1555 1=XRGB8888 2=RGB565)\n", fmts.c_str());
    say("retro_run ab 2. Sekunde: Mittel %.2f ms, p95 %.2f ms, max %.2f ms — Budget 72 Hz %.1f ms: %s\n",
        all.mean, all.p95, all.max, budget,
        all.p95 < budget * 0.6 ? "reicht mit Reserve" : all.p95 < budget ? "reicht knapp" : "REICHT NICHT");
    const double dev = gExpectedAudio > 0 ? (static_cast<double>(gAudioFrames) / gExpectedAudio - 1.0) * 100.0 : 0.0;
    say("Ton: %ld Frames, erwartet %.0f (%.0f Hz / %.2f fps) → Abweichung %+.2f %%; hörbar %.1f %%; "
        "simulierte Unterläufe %ld (min. Vorrat %.0f Frames)\n", gAudioFrames, gExpectedAudio, gRate, gFps, dev,
        gAudioFrames ? 100.0 * static_cast<double>(gAudioLoud) / static_cast<double>(gAudioFrames) : 0.0,
        gUnderruns, gFillMin > 1e8 ? 0.0 : gFillMin);
    say("Rumble: %ld Aufrufe, %ld mit Stärke > 0, max. stark %u / schwach %u\n", gRumbleCalls, gRumbleOn,
        gRumbleMax[0], gRumbleMax[1]);
    say("AV-Wechsel (SET_SYSTEM_AV_INFO): %zu; Start %.2f fps / %.0f Hz, Ende %.2f fps / %.0f Hz\n",
        gAvChanges.size(), startFps, startRate, gFps, gRate);
    std::string geos;
    for (const auto& g : gGeometries) geos += g.first + "@" + std::to_string(g.second) + " ";
    say("Geometrien: %s(%ld Meldungen)\n", geos.c_str(), gGeometryCalls);
    say("Savestate: %s\n", stateResult.c_str());
    say("Beenden angefordert (SHUTDOWN): %s\n", gShutdown ? ("ja, Frame " + std::to_string(gShutdownFrame)).c_str() : "nein");
    std::string devs;
    for (unsigned d : gDevicesQueried) devs += std::to_string(d) + " ";
    say("Eingabe: %ld Abfragen, Geräte %s\n", gInputQueries, devs.c_str());

    say("\nKern-Optionen (aktiv → Core):\n");
    std::set<std::string> keys;
    for (const auto& e : profile) keys.insert(e.first);
    for (const auto& o : coredefaults::forCore(core)) keys.insert(o.first);
    int optProblems = 0;
    for (const std::string& k : keys) {
        if (k.compare(0, 8, "xrshell_") == 0) continue;
        std::string v;
        gOptions.active(k, v);
        const optstore::Definition* def = nullptr;
        for (const auto& d : gDefs) if (d.key == k) def = &d;
        std::string verdict;
        if (!def) { verdict = "UNBEKANNT beim Core"; ++optProblems; }
        else {
            bool ok = false;
            for (const auto& val : def->values) ok |= val.first == v;
            verdict = ok ? "gültig" : "UNGÜLTIGER WERT";
            if (!ok) {
                ++optProblems;
                verdict += " (erlaubt:";
                for (const auto& val : def->values) verdict += " " + val.first;
                verdict += ")";
            }
        }
        say("  %-36s = %-22s %s, gefragt: %s\n", k.c_str(), v.c_str(), verdict.c_str(),
            gQueried.count(k) ? "ja" : "nein");
    }
    say("Core meldet %zu Optionen, fragt %zu ab\n", gDefs.size(), gQueried.size());
    std::string unknown;
    for (const auto& u : gUnknownEnv) {
        char b[48];
        std::snprintf(b, sizeof(b), "%u%s×%d ", u.first & 0xFFFF, (u.first & RETRO_ENVIRONMENT_EXPERIMENTAL) ? "(exp)" : "", u.second);
        unknown += b;
    }
    say("Nicht bediente Umgebungsbefehle (false): %s\n", unknown.c_str());

    std::map<std::string, FileInfo> after;
    walk(gamePath, "", after);
    int changedFiles = 0;
    for (const auto& a : after) {
        const auto b = before.find(a.first);
        if (b == before.end()) { say("SPIELORDNER: neue Datei %s (%lld Bytes)\n", a.first.c_str(), a.second.size); ++changedFiles; }
        else if (b->second.size != a.second.size || b->second.hash != a.second.hash) {
            say("SPIELORDNER: geändert %s\n", a.first.c_str());
            ++changedFiles;
        } else if (b->second.mtime != a.second.mtime) {
            say("Spielordner: nur Zeitstempel berührt %s (Inhalt gleich)\n", a.first.c_str());
        }
    }
    for (const auto& b : before)
        if (!after.count(b.first)) { say("SPIELORDNER: gelöscht %s\n", b.first.c_str()); ++changedFiles; }
    say("Spielordner: %s\n", changedFiles ? "VERÄNDERT" : "unverändert");
    std::map<std::string, FileInfo> saves, sys;
    walk(gSaveDir, "", saves);
    walk(gSystemDir, "", sys);
    for (const auto& s : saves) say("  saves/%s (%lld Bytes)\n", s.first.c_str(), s.second.size);
    for (const auto& s : sys) say("  system/%s (%lld Bytes)\n", s.first.c_str(), s.second.size);

    // JSON
    if (FILE* j = std::fopen((gOut + "/bericht.json").c_str(), "w")) {
        std::fprintf(j, "{\n \"spiel\": \"%s\",\n \"core\": \"%s\",\n \"startdatei\": \"%s\",\n",
                     jsonEscape(gamePath).c_str(), core->soName, jsonEscape(contentPath).c_str());
        std::fprintf(j, " \"frames\": %ld, \"bilder\": %ld, \"zuGross\": %ld,\n", gRun, gVideoFrames, gTooBig);
        std::fprintf(j, " \"fpsStart\": %.3f, \"fpsEnde\": %.3f, \"rate\": %.1f, \"avWechsel\": %zu,\n", startFps, gFps,
                     gRate, gAvChanges.size());
        std::fprintf(j, " \"runMs\": {\"mittel\": %.3f, \"p95\": %.3f, \"max\": %.3f},\n", all.mean, all.p95, all.max);
        std::fprintf(j, " \"ton\": {\"frames\": %ld, \"erwartet\": %.0f, \"abweichungProzent\": %.3f, \"hoerbarProzent\": %.2f, "
                        "\"unterlaeufe\": %ld},\n", gAudioFrames, gExpectedAudio, dev,
                     gAudioFrames ? 100.0 * static_cast<double>(gAudioLoud) / static_cast<double>(gAudioFrames) : 0.0, gUnderruns);
        std::fprintf(j, " \"rumble\": {\"ein\": %ld, \"maxStark\": %u, \"maxSchwach\": %u},\n", gRumbleOn, gRumbleMax[0], gRumbleMax[1]);
        std::fprintf(j, " \"savestate\": {\"bytes\": %zu, \"gleich\": %d, \"von\": %d},\n", stateSize, stateMatch, stateFrames);
        std::fprintf(j, " \"shutdown\": %s, \"optionProbleme\": %d, \"spielordnerVeraendert\": %d,\n",
                     gShutdown ? "true" : "false", optProblems, changedFiles);
        std::fprintf(j, " \"schritte\": [\n");
        for (size_t i = 0; i < results.size(); ++i) {
            const StepResult& r = results[i];
            std::fprintf(j, "  {\"name\": \"%s\", \"frames\": %ld, \"msMittel\": %.3f, \"msP95\": %.3f, \"bildNeu\": %s, "
                            "\"hellProzent\": %.2f, \"tonHoerbarProzent\": %.2f, \"rumble\": %ld}%s\n",
                         jsonEscape(r.name).c_str(), r.frames, r.ms.mean, r.ms.p95, r.changed ? "true" : "false",
                         r.bright, r.loud, r.rumble, i + 1 < results.size() ? "," : "");
        }
        std::fprintf(j, " ]\n}\n");
        std::fclose(j);
    }

    if (unloadGame) unloadGame();
    deinit();
    gDone = true;
    say("ERGEBNIS: %s\n", gVideoFrames > 0 && gAudioFrames > 0 && optProblems == 0 && changedFiles == 0
                              ? "OK" : "AUFFÄLLIG (siehe oben)");
    if (gReport) std::fclose(gReport);
    if (gCoreLog) std::fclose(gCoreLog);
    std::fflush(stdout);
    // dlclose bewusst nicht: manche Cores hinterlassen Threads/Destruktoren (PrBoom-Renderthreads).
    std::_Exit(0);
}
