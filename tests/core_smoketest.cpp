// Core-Smoke-Test für die Quest (ohne Headset, ohne App): lädt einen libretro-Core
// wie der Adapter (dlopen, Callbacks, need_fullpath), startet ein Spiel, lässt N Frames
// ungebremst laufen und meldet Bild, Ton und Tempo. Das letzte Bild landet als PPM.
//
//   Bauen (NDK, arm64):
//     clang++ --target=aarch64-linux-android29 -std=c++17 -O2 -Wall -Wextra \
//             -Isrc -static-libstdc++ tests/core_smoketest.cpp -ldl -o core_smoketest
//   Am PC (Windows, llvm-mingw unter <XR-Ordner>/_tools, Core-DLL vom libretro-Buildbot):
//     x86_64-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -Isrc -static //             tests/core_smoketest.cpp -o core_smoketest.exe
//   Auf der Quest (Binary per adb nach /data/local/tmp):
//     ./core_smoketest <core.so> <spiel> [frames] [bild.ppm] [start-frame]
//
// start-frame: ab diesem Frame wird alle 60 Frames kurz Start gedrückt (Titelbild weiter).
// Ausgabe endet mit „ERGEBNIS: OK“ oder „ERGEBNIS: FEHLER …“; Exitcode 0/1.
// Tasten (Cores mit Tastatur-Callback, z. B. ScummVM): SMOKE_KEYS="300:f5,900:lctrl+f5" drückt die
// Tasten ab dem Frame der Reihe nach und lässt sie 6 Frames später los. Schnappschüsse:
// SMOKE_SHOTS="320,920" schreibt shot-<frame>.ppm.
// Kern-Optionen wie im Adapter: ./core.opt (Zeilen `schlüssel = "wert"`) beantwortet
// GET_VARIABLE; SMOKE_LOG=info zeigt auch Info-Meldungen des Cores.
// Grenzen: kein GLES, kein Audio-Gerät, kein Frame-Pacing — prüft nur den Core-Pfad.

#include "libretro.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <timeapi.h>
#else
#include <dlfcn.h>
#endif

namespace {

int gFormat = RETRO_PIXEL_FORMAT_0RGB1555;
unsigned gW = 0, gH = 0;
std::vector<uint32_t> gFrame;       // letztes Bild als XRGB8888
long gVideoFrames = 0, gDupes = 0;
long gAudioFrames = 0;
long gAudioNonZero = 0;
long gRun = 0;                      // aktueller retro_run
long gStartFrom = -1;
std::string gSystemDir, gSaveDir;
retro_keyboard_event_t gKeyboard = nullptr;
struct KeyEvent { long frame; std::vector<unsigned> keys; };
std::vector<KeyEvent> gKeyEvents;   // aus SMOKE_KEYS
std::vector<long> gShots;           // aus SMOKE_SHOTS

unsigned keyCode(const std::string& name) {
    struct Named { const char* n; unsigned k; };
    static const Named kNames[] = {
        {"esc", RETROK_ESCAPE}, {"enter", RETROK_RETURN}, {"space", RETROK_SPACE},
        {"period", RETROK_PERIOD}, {"lctrl", RETROK_LCTRL}, {"lshift", RETROK_LSHIFT},
        {"lalt", RETROK_LALT}, {"tab", RETROK_TAB},
    };
    for (const Named& k : kNames) if (name == k.n) return k.k;
    if (name.size() >= 2 && name[0] == 'f') {
        const int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 12) return RETROK_F1 + static_cast<unsigned>(n - 1);
    }
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return RETROK_a + static_cast<unsigned>(name[0] - 'a');
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') return RETROK_0 + static_cast<unsigned>(name[0] - '0');
    return RETROK_UNKNOWN;
}

void parseKeys(const char* spec) {
    std::string all = spec ? spec : "";
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(',', pos);
        if (end == std::string::npos) end = all.size();
        const std::string item = all.substr(pos, end - pos);
        const size_t colon = item.find(':');
        if (colon != std::string::npos) {
            KeyEvent e;
            e.frame = std::atol(item.substr(0, colon).c_str());
            std::string rest = item.substr(colon + 1);
            size_t p = 0;
            while (p <= rest.size()) {
                size_t q = rest.find('+', p);
                if (q == std::string::npos) q = rest.size();
                const unsigned k = keyCode(rest.substr(p, q - p));
                if (k != RETROK_UNKNOWN) e.keys.push_back(k);
                p = q + 1;
            }
            if (!e.keys.empty()) gKeyEvents.push_back(e);
        }
        pos = end + 1;
    }
}

uint16_t modsFor(const std::vector<unsigned>& keys, size_t upTo) {
    uint16_t m = 0;
    for (size_t i = 0; i < upTo && i < keys.size(); ++i) {
        if (keys[i] == RETROK_LCTRL) m |= RETROKMOD_CTRL;
        if (keys[i] == RETROK_LSHIFT) m |= RETROKMOD_SHIFT;
        if (keys[i] == RETROK_LALT) m |= RETROKMOD_ALT;
    }
    return m;
}

// Vor jedem retro_run: Tasten der Ereignisse dieses Frames drücken bzw. 6 Frames später lösen.
void feedKeys(long frame) {
    if (!gKeyboard) return;
    for (const KeyEvent& e : gKeyEvents) {
        if (frame == e.frame) {
            for (size_t i = 0; i < e.keys.size(); ++i) gKeyboard(true, e.keys[i], 0, modsFor(e.keys, i + 1));
            std::printf("Frame %ld: Tasten gedrückt (%zu)\n", frame, e.keys.size());
        } else if (frame == e.frame + 6) {
            for (size_t i = e.keys.size(); i-- > 0;) gKeyboard(false, e.keys[i], 0, modsFor(e.keys, i));
        }
    }
}
std::vector<std::pair<std::string, std::string>> gOptions;   // aus ./core.opt
int gLogLevel = RETRO_LOG_WARN;

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n\"");
    const size_t b = s.find_last_not_of(" \t\r\n\"");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

void loadOptions(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        const std::string l = line;
        const size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        gOptions.emplace_back(trim(l.substr(0, eq)), trim(l.substr(eq + 1)));
    }
    std::fclose(f);
    std::printf("Kern-Optionen: %zu aus %s\n", gOptions.size(), path);
}

uint32_t toXrgb(uint16_t p) {
    if (gFormat == RETRO_PIXEL_FORMAT_RGB565) {
        const uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
    }
    const uint32_t r = (p >> 10) & 0x1F, g = (p >> 5) & 0x1F, b = p & 0x1F;
    return (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

void coreLog(enum retro_log_level level, const char* fmt, ...) {
    if (static_cast<int>(level) < gLogLevel) return;
    va_list args;
    va_start(args, fmt);
    std::fputs("  [core] ", stdout);
    std::vprintf(fmt, args);
    va_end(args);
}

bool environment(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const int fmt = *static_cast<const int*>(data);
        const bool ok = fmt == RETRO_PIXEL_FORMAT_XRGB8888 || fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                        fmt == RETRO_PIXEL_FORMAT_0RGB1555;
        if (ok) gFormat = fmt;
        std::printf("Pixelformat %d %s\n", fmt, ok ? "akzeptiert" : "abgelehnt");
        return ok;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = coreLog;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = gSystemDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = gSaveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        gKeyboard = static_cast<const retro_keyboard_callback*>(data)->callback;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* var = static_cast<retro_variable*>(data);
        for (const auto& o : gOptions)
            if (var->key && o.first == var->key) { var->value = o.second.c_str(); return true; }
        if (gLogLevel <= RETRO_LOG_INFO) std::printf("  Option ohne Wert: %s\n", var->key ? var->key : "?");
        return false;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const auto& g = cmd == RETRO_ENVIRONMENT_SET_GEOMETRY
                            ? *static_cast<const retro_game_geometry*>(data)
                            : static_cast<const retro_system_av_info*>(data)->geometry;
        std::printf("Geometrie (%s): %ux%u, max %ux%u\n",
                    cmd == RETRO_ENVIRONMENT_SET_GEOMETRY ? "SET_GEOMETRY" : "SET_SYSTEM_AV_INFO",
                    g.base_width, g.base_height, g.max_width, g.max_height);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        // Alte Optionsliste („Beschreibung; Standard|Wert|…“) ausgeben; Antwort wie der Adapter.
        if (gLogLevel <= RETRO_LOG_INFO)
            for (const auto* v = static_cast<const retro_variable*>(data); v && v->key; ++v)
                std::printf("  Option %s: %s\n", v->key, v->value ? v->value : "?");
        return false;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        // Optionsliste ausgeben (Schlüssel, Standard, Werte); Antwort wie der Adapter: false.
        const auto* opts = cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2
                               ? static_cast<const retro_core_options_v2*>(data)
                               : static_cast<const retro_core_options_v2_intl*>(data)->us;
        if (gLogLevel <= RETRO_LOG_INFO && opts)
            for (const auto* d = opts->definitions; d && d->key; ++d) {
                std::string list;
                for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && d->values[i].value; ++i)
                    list += std::string(list.empty() ? "" : "|") + d->values[i].value;
                std::printf("  Option %s = %s [%s]\n", d->key, d->default_value ? d->default_value : "?",
                            list.c_str());
            }
        return false;
    }
    default:
        if (gLogLevel <= RETRO_LOG_INFO) {
            static std::vector<unsigned> seen;
            bool known = false;
            for (unsigned c : seen) known = known || c == cmd;
            if (!known) { seen.push_back(cmd); std::printf("  Umgebung %u nicht unterstützt\n", cmd); }
        }
        return false;   // wie der Adapter: unbekannt = nicht unterstützt
    }
}

void video(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) { ++gDupes; return; }
    ++gVideoFrames;
    gW = w;
    gH = h;
    gFrame.resize(static_cast<size_t>(w) * h);
    const auto* src = static_cast<const uint8_t*>(data);
    for (unsigned y = 0; y < h; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * pitch;
        for (unsigned x = 0; x < w; ++x) {
            uint32_t px;
            if (gFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
                std::memcpy(&px, row + x * 4, 4);
                px &= 0xFFFFFFu;
            } else {
                uint16_t p16;
                std::memcpy(&p16, row + x * 2, 2);
                px = toXrgb(p16);
            }
            gFrame[static_cast<size_t>(y) * w + x] = px;
        }
    }
}

void audioSample(int16_t l, int16_t r) {
    ++gAudioFrames;
    if (l || r) ++gAudioNonZero;
}

size_t audioBatch(const int16_t* data, size_t frames) {
    gAudioFrames += static_cast<long>(frames);
    for (size_t i = 0; i < frames * 2; ++i)
        if (data[i]) { ++gAudioNonZero; break; }
    return frames;
}

void inputPoll() {}

int16_t inputState(unsigned port, unsigned device, unsigned, unsigned id) {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    const bool start = gStartFrom >= 0 && gRun >= gStartFrom && gRun % 60 < 6;
    const uint16_t mask = start ? (1u << RETRO_DEVICE_ID_JOYPAD_START) : 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(mask);
    return id < 16 && (mask & (1u << id)) ? 1 : 0;
}

#ifdef _WIN32
// Windows-Lauf am PC (gleiche Logik, nur Laden/Ordner anders).
void* openCore(const char* path) { return reinterpret_cast<void*>(LoadLibraryA(path)); }
void* coreSymbol(void* h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), name));
}
void closeCore(void* h) { FreeLibrary(static_cast<HMODULE>(h)); }
std::string openError() { return "LoadLibrary-Fehler " + std::to_string(GetLastError()); }
void makeDir(const std::string& path) { _mkdir(path.c_str()); }
#else
void* openCore(const char* path) { return dlopen(path, RTLD_NOW); }
void* coreSymbol(void* h, const char* name) { return dlsym(h, name); }
void closeCore(void* h) { dlclose(h); }
std::string openError() { return dlerror(); }
void makeDir(const std::string& path) { ::mkdir(path.c_str(), 0755); }
#endif

template <typename T> T sym(void* h, const char* name) {
    return reinterpret_cast<T>(coreSymbol(h, name));
}

int fail(const char* why) {
    std::printf("ERGEBNIS: FEHLER — %s\n", why);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Aufruf: %s <core.so> <spiel> [frames] [bild.ppm] [start-frame]\n", argv[0]);
        return 2;
    }
    const std::string corePath = argv[1], gamePath = argv[2];
    const long frames = argc > 3 ? std::atol(argv[3]) : 600;
    const std::string ppm = argc > 4 ? argv[4] : "";
    gStartFrom = argc > 5 ? std::atol(argv[5]) : -1;
    if (const char* lvl = std::getenv("SMOKE_LOG"))
        if (!std::strcmp(lvl, "info")) gLogLevel = RETRO_LOG_INFO;
    loadOptions("core.opt");
    parseKeys(std::getenv("SMOKE_KEYS"));
    if (const char* shots = std::getenv("SMOKE_SHOTS"))
        for (const char* p = shots; *p;) {
            gShots.push_back(std::atol(p));
            while (*p && *p != ',') ++p;
            if (*p == ',') ++p;
        }
    gSystemDir = "./system";
    gSaveDir = "./saves";
#ifdef _WIN32
    timeBeginPeriod(1);   // Sleep() sonst in 15,6-ms-Schritten — verfälscht Cores, die selbst warten
#endif
    makeDir(gSystemDir);
    makeDir(gSaveDir);

    void* h = openCore(corePath.c_str());
    if (!h) { std::printf("Laden: %s\n", openError().c_str()); return fail("Core lädt nicht"); }
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
    auto run = sym<void (*)()>(h, "retro_run");
    auto serializeSize = sym<size_t (*)()>(h, "retro_serialize_size");
    auto serialize = sym<bool (*)(void*, size_t)>(h, "retro_serialize");
    auto unserialize = sym<bool (*)(const void*, size_t)>(h, "retro_unserialize");
    if (!setEnv || !setVideo || !setSample || !setBatch || !setPoll || !setState || !init ||
        !deinit || !apiVersion || !getInfo || !getAv || !loadGame || !run)
        return fail("Core-API unvollständig");
    if (apiVersion() != RETRO_API_VERSION) return fail("falsche API-Version");

    setEnv(environment);
    setVideo(video);
    setSample(audioSample);
    setBatch(audioBatch);
    setPoll(inputPoll);
    setState(inputState);

    retro_system_info info{};
    getInfo(&info);
    std::printf("Core: %s %s, Endungen %s, need_fullpath=%d\n", info.library_name,
                info.library_version, info.valid_extensions ? info.valid_extensions : "?",
                info.need_fullpath ? 1 : 0);

    std::vector<uint8_t> data;
    if (!info.need_fullpath) {
        FILE* f = std::fopen(gamePath.c_str(), "rb");
        if (!f) return fail("Spieldatei nicht lesbar");
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
        std::fclose(f);
    }
    init();
    retro_game_info game{};
    game.path = gamePath.c_str();
    if (!data.empty()) { game.data = data.data(); game.size = data.size(); }
    if (!loadGame(&game)) { deinit(); return fail("retro_load_game fehlgeschlagen"); }

    retro_system_av_info av{};
    getAv(&av);
    std::printf("AV: %ux%u (max %ux%u), Seitenverhältnis %.3f, %.3f fps, %.0f Hz\n",
                av.geometry.base_width, av.geometry.base_height, av.geometry.max_width,
                av.geometry.max_height, static_cast<double>(av.geometry.aspect_ratio),
                av.timing.fps, av.timing.sample_rate);

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<double> runMs;   // Dauer je retro_run (schläft der Core oder rechnet er?)
    runMs.reserve(static_cast<size_t>(frames > 0 ? frames : 0));
    for (gRun = 0; gRun < frames; ++gRun) {
        feedKeys(gRun);
        const auto r0 = std::chrono::steady_clock::now();
        run();
        for (long shot : gShots)
            if (shot == gRun && gW && gH) {
                char name[64];
                std::snprintf(name, sizeof(name), "shot-%ld.ppm", shot);
                if (FILE* f = std::fopen(name, "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", gW, gH);
                    for (uint32_t px : gFrame) {
                        const unsigned char rgb[3] = {static_cast<unsigned char>(px >> 16),
                                                      static_cast<unsigned char>(px >> 8),
                                                      static_cast<unsigned char>(px)};
                        std::fwrite(rgb, 1, 3, f);
                    }
                    std::fclose(f);
                }
            }
        runMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count());
    }
    if (!runMs.empty()) {
        std::vector<double> sorted = runMs;
        std::sort(sorted.begin(), sorted.end());
        std::printf("retro_run: min %.1f ms, Median %.1f ms, 95%% %.1f ms, max %.1f ms\n", sorted.front(),
                    sorted[sorted.size() / 2], sorted[sorted.size() * 95 / 100], sorted.back());
    }
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double speed = secs > 0 ? frames / secs : 0.0;
    std::printf("Lauf: %ld Frames in %.2f s = %.0f Frames/s (%.1f× Echtzeit)\n", frames, secs,
                speed, av.timing.fps > 0 ? speed / av.timing.fps : 0.0);
    std::printf("Bild: %ld neue Frames, %ld Wiederholungen, zuletzt %ux%u\n", gVideoFrames,
                gDupes, gW, gH);
    const double expectedAudio = av.timing.sample_rate * frames / (av.timing.fps > 0 ? av.timing.fps : 60);
    std::printf("Ton: %ld Frames (erwartet ~%.0f), nicht still: %s\n", gAudioFrames, expectedAudio,
                gAudioNonZero ? "ja" : "nein");

    // Bildinhalt: Anzahl verschiedener Farben (1 = leer/einfarbig).
    std::vector<uint32_t> colors;
    for (size_t i = 0; i < gFrame.size() && colors.size() < 64; i += 7) {
        bool seen = false;
        for (uint32_t c : colors) if (c == gFrame[i]) { seen = true; break; }
        if (!seen) colors.push_back(gFrame[i]);
    }
    std::printf("Bildinhalt: %zu+ verschiedene Farben\n", colors.size());
    if (!ppm.empty() && gW && gH) {
        if (FILE* f = std::fopen(ppm.c_str(), "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", gW, gH);
            for (uint32_t px : gFrame) {
                const unsigned char rgb[3] = {static_cast<unsigned char>(px >> 16),
                                              static_cast<unsigned char>(px >> 8),
                                              static_cast<unsigned char>(px)};
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
            std::printf("Bild gespeichert: %s\n", ppm.c_str());
        }
    }

    // Spielstand-Rundlauf wie das VR-Menü (Speichern → Laden).
    bool stateOk = false;
    if (serializeSize && serialize && unserialize) {
        const size_t size = serializeSize();
        std::vector<uint8_t> state(size);
        stateOk = size > 0 && serialize(state.data(), size) && unserialize(state.data(), size);
        std::printf("Spielstand: %zu Bytes, Speichern/Laden %s\n", size, stateOk ? "ok" : "FEHLER");
    }
    for (int i = 0; i < 10; ++i) run();   // nach dem Laden weiterlaufen

    if (unloadGame) unloadGame();
    deinit();
    closeCore(h);

    if (gVideoFrames == 0) return fail("kein Bild");
    if (colors.size() < 2) return fail("Bild einfarbig");
    if (gAudioFrames == 0) return fail("kein Ton-Datenstrom");
    if (!stateOk) return fail("Spielstand-Rundlauf");
    if (av.timing.fps > 0 && speed < av.timing.fps * 1.5)
        return fail("zu langsam (unter 1,5× Echtzeit)");
    std::printf("ERGEBNIS: OK\n");
    return 0;
}
