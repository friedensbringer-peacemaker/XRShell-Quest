// XRShell — M1a: libretro-Core-Loader, Implementierung. Siehe libretro_adapter.h
// für Architektur und Thread-Modell.

#include "libretro_adapter.h"
#include "core_defaults.h"
#include "core_select.h"
#include "frame_pacer.h"

#include "game_folders.h"
#include "libretro.h"
#include "pointer_map.h"
#include "scumm_setup.h"

#include <android/asset_manager.h>
#include <android/log.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

using namespace core_select;

// Die App hat genau einen Adapter — Callbacks sind C-Funktionen ohne Userdata.
LibretroAdapter* gAdapter = nullptr;

// Bibliotheksordner games/<system>/<Spiel>/ eines Cores mit Startregel, sonst nullptr.
const CoreSpec* folderCoreFor(const std::string& folder) {
    const size_t slash = folder.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return nullptr;
    const std::string parent = folder.substr(0, slash);
    const std::string system = parent.substr(parent.find_last_of('/') + 1);
    for (const CoreSpec* core : kCores)
        if (core->startRule != StartRule::None && core->folder && system == core->folder) return core;
    return nullptr;
}

// Systemdateien des ScummVM-Cores im APK (build.gradle: fetchScummvmSystem).
// AAssetDir listet nur Dateien, keine Unterordner — deshalb feste Liste.
const char* const kScummSystemDirs[] = {"scummvm/extra", "scummvm/theme"};

// Funktionszeigertypen der Core-Einstiegspunkte (Signatur-Schnittstellen aus libretro.h).
using set_environment_t       = void (*)(retro_environment_t);
using set_video_refresh_t     = void (*)(retro_video_refresh_t);
using set_audio_sample_t      = void (*)(retro_audio_sample_t);
using set_audio_sample_batch_t = void (*)(retro_audio_sample_batch_t);
using set_input_poll_t        = void (*)(retro_input_poll_t);
using set_input_state_t       = void (*)(retro_input_state_t);
using retro_init_t            = void (*)(void);
using retro_deinit_t        = void (*)(void);
using retro_run_t           = void (*)(void);
using retro_api_version_t   = unsigned (*)(void);
using get_system_info_t     = void (*)(struct retro_system_info*);
using get_system_av_info_t  = void (*)(struct retro_system_av_info*);
using load_game_t           = bool (*)(const struct retro_game_info*);
using unload_game_t         = void (*)(void);

// Takt des Run-Loops aus dem fps-Wert des Cores (Start und SET_SYSTEM_AV_INFO gleich): nach
// oben auf 1000 begrenzt (PrBoom bietet bis 540 fps), damit ein Ausreißer den Loop nicht zum
// Busy-Loop macht. Werte ≤ 1 übergehen die Aufrufer (alter Takt bleibt).
double clampCoreFps(double fps) {
    if (fps <= 1000.0) return fps;
    LOGE("Core meldet %.0f fps — Takt auf 1000 begrenzt", fps);
    return 1000.0;
}

// Pixel eines 16-Bit-Frames → XRGB8888 (Bytes B,G,R,X wie beim 32-Bit-Core).
inline uint32_t rgb565ToXrgb(uint16_t p) {
    const uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}
inline uint32_t rgb1555ToXrgb(uint16_t p) {
    const uint32_t r = (p >> 10) & 0x1F, g = (p >> 5) & 0x1F, b = p & 0x1F;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

// Letzte Warn-/Fehlerzeile des Cores für die Fehlerseite (Core-Thread bzw. Lade-Thread schreibt).
std::mutex gCoreMessageMutex;
std::string gCoreMessage;

// libretro-Log des Cores ins Logcat (Debug-Ausgaben weglassen).
void coreLog(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_INFO) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    __android_log_print(level >= RETRO_LOG_WARN ? ANDROID_LOG_WARN : ANDROID_LOG_INFO,
                        "XRShell-Core", "%s", buf);
    if (level >= RETRO_LOG_WARN) {
        std::string line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        std::lock_guard<std::mutex> lock(gCoreMessageMutex);
        gCoreMessage = line;
    }
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool makeDir(const std::string& path) {
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

// Bibliotheksordner (files/games/…): die App legt sie mit ihrer umask an (Quest: 0750) — dann
// darf adb (Gruppe ext_data_rw) keine Spiele hineinlegen (tools/push-game.sh: „Permission
// denied“). Darum ausdrücklich für alle schreibbar; Fehler (fremder Ordner) sind egal.
void makeSharedDir(const std::string& path) {
    makeDir(path);
    ::chmod(path.c_str(), 0777);
}

bool isDirectory(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// #28: erste „*.scummvm“-Datei (alphabetisch) im Ordner, leer = keine.
std::string scummTargetIn(const std::string& dirPath) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return {};
    std::vector<std::string> hits;
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = dirPath + "/" + entry->d_name;
        if (!isDirectory(path) && coreForFile(entry->d_name) == &kScummCore) hits.push_back(path);
    }
    closedir(dir);
    std::sort(hits.begin(), hits.end());
    return hits.empty() ? std::string() : hits.front();
}

// Asset → Datei, nur wenn die Größe abweicht (Normalstart: ein stat()).
// Erst .tmp, dann rename — ein Abbruch hinterlässt nie eine halbe Datei.
bool extractAsset(AAssetManager* mgr, const std::string& assetPath, const std::string& dest) {
    AAsset* asset = AAssetManager_open(mgr, assetPath.c_str(), AASSET_MODE_STREAMING);
    if (!asset) return false;
    struct stat st {};
    if (::stat(dest.c_str(), &st) == 0 &&
        static_cast<int64_t>(st.st_size) == AAsset_getLength64(asset)) {
        AAsset_close(asset);
        return true;
    }
    const std::string tmp = dest + ".tmp";
    FILE* out = std::fopen(tmp.c_str(), "wb");
    if (!out) {
        AAsset_close(asset);
        return false;
    }
    char buf[64 * 1024];
    int n;
    bool ok = true;
    while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0) {
        if (std::fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
            ok = false;
            break;
        }
    }
    if (n < 0) ok = false;
    if (std::fclose(out) != 0) ok = false;
    AAsset_close(asset);
    if (!ok || ::rename(tmp.c_str(), dest.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace

// ==============================================================================
// libretro-Callbacks (laufen auf dem Core-Thread, kein GLES hier)
// ==============================================================================

bool LibretroAdapter::environmentCallback(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto* fmt = static_cast<const retro_pixel_format*>(data);
        // #16: alle drei libretro-Formate; 16-Bit wandelt der Video-Callback.
        const bool ok = fmt && gAdapter &&
                        (*fmt == RETRO_PIXEL_FORMAT_XRGB8888 ||
                         *fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                         *fmt == RETRO_PIXEL_FORMAT_0RGB1555);
        if (ok) gAdapter->mPixelFormat.store(static_cast<int>(*fmt));
        LOGI("Core-Pixelformat: %d (%s)", fmt ? static_cast<int>(*fmt) : -1,
             ok ? "akzeptiert" : "abgelehnt");
        return ok;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
        auto* cb = static_cast<retro_log_callback*>(data);
        if (!cb) return false;
        cb->log = coreLog;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
        if (!gAdapter || !data) return false;
        const std::string& dir = cmd == RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY
                                     ? gAdapter->mSaveDir : gAdapter->mSystemDir;
        if (dir.empty()) return false;
        *static_cast<const char**>(data) = dir.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        // Kern-Optionen aus xrshell.opt (+ Nutzerwerte aus dem VR-Menü); sonst Core-Standard.
        // „xrshell_*“ wertet XRShell selbst aus — nie an den Core.
        auto* var = static_cast<retro_variable*>(data);
        if (!gAdapter || !var || !var->key) return false;
        if (std::strncmp(var->key, "xrshell_", 8) == 0) { var->value = nullptr; return false; }
        var->value = gAdapter->coreOption(var->key);
        return var->value != nullptr;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
        // true genau einmal nach einer Live-Änderung im VR-Menü (xr.dosgames); der Core liest
        // seine Optionen dann im nächsten retro_run neu (DOSBox-Pure: Zyklen, Maus, …).
        auto* updated = static_cast<bool*>(data);
        if (!gAdapter || !updated) return false;
        *updated = gAdapter->mOptions.takeUpdate();
        if (*updated) LOGI("Kern-Optionen geändert — Core liest neu");
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        // Optionsliste des Cores nur mitlesen (Werte fürs VR-Menü). Antwort wie bisher
        // „nicht unterstützt“ — die Cores verhalten sich dadurch unverändert.
        if (gAdapter && data) gAdapter->addCoreOptionDefinitions(cmd, data);
        return false;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        // Pad-Modus beantwortet JOYPAD_MASK in einem Aufruf.
        return gAdapter && gAdapter->mPadMode;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK: {
        // #28: ScummVM liest Tasten nur über diesen Callback, nicht per Polling.
        const auto* cb = static_cast<const retro_keyboard_callback*>(data);
        if (!gAdapter || !cb) return false;
        gAdapter->mKeyboardCallback = cb->callback;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: {
        // Rumble-Zustand nur merken (Core-Thread); main.cpp vibriert pro Frame.
        auto* rumble = static_cast<retro_rumble_interface*>(data);
        if (!rumble) return false;
        rumble->set_rumble_state = [](unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
            if (!gAdapter || port != 0 || effect > RETRO_RUMBLE_WEAK) return false;
            gAdapter->mRumble[effect].store(strength);
            return true;
        };
        return true;
    }
    case RETRO_ENVIRONMENT_GET_TARGET_SAMPLE_RATE:
        // Ausgaberate von AAudio (Quest: 48 kHz) — Cores mit „Auto“-Rate wählen dann ohne Resampling.
        if (!data) return false;
        *static_cast<unsigned*>(data) = gAdapter ? static_cast<unsigned>(gAdapter->mAudio.outputRate()) : 48000u;
        return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE:
        // Aktuelle Anzeige-Rate (72 Hz, sonst was die Runtime meldet).
        if (!data) return false;
        *static_cast<float*>(data) = gAdapter ? gAdapter->mDisplayRefresh.load() : 72.0f;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        // Arbeitsspeicher des Cores (DOSBox-Pure: Programm, DOS-Bereich, erweiterter Speicher) —
        // „Zeiger lernen“ sucht darin die Zeigerposition des Spiels. Kommt bei jedem Programmwechsel neu.
        const auto* map = static_cast<const retro_memory_map*>(data);
        if (!gAdapter || !map) return false;
        gAdapter->mMemRegions.clear();
        ++gAdapter->mMemGeneration;
        size_t total = 0;
        for (unsigned i = 0; i < map->num_descriptors; ++i) {
            const retro_memory_descriptor& d = map->descriptors[i];
            if (!d.ptr || d.len == 0 || d.select != 0) continue;
            curlock::Region r;
            r.ptr = static_cast<const uint8_t*>(d.ptr) + d.offset;
            r.start = static_cast<uint32_t>(d.start);
            r.len = static_cast<uint32_t>(d.len);
            gAdapter->mMemRegions.push_back(r);
            total += d.len;
        }
        LOGI("Speicherkarte des Cores: %zu Bereiche, %zu KB", gAdapter->mMemRegions.size(), total / 1024);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MESSAGE:
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
        // Nur mitlesen (Antwort wie bisher „nicht unterstützt“). DOSBox-Pure meldet einen Absturz
        // des DOS-Programms hier — dessen Speicher gilt dann nicht mehr (Zeiger lernen/folgen).
        const char* text = nullptr;
        if (cmd == RETRO_ENVIRONMENT_SET_MESSAGE && data) text = static_cast<const retro_message*>(data)->msg;
        if (cmd == RETRO_ENVIRONMENT_SET_MESSAGE_EXT && data) text = static_cast<const retro_message_ext*>(data)->msg;
        if (text) {
            LOGI("Core-Meldung: %s", text);
            std::string lower(text);
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (gAdapter && lower.find("crash") != std::string::npos) gAdapter->dropMemoryMap("Absturz gemeldet");
        }
        return false;
    }
    case RETRO_ENVIRONMENT_SHUTDOWN:
        // Core will beenden (Doom „Quit Game“ → Ja, DOSBox-Pure „Beenden“): main.cpp stoppt das
        // Spiel und zeigt die Spielauswahl (Einzelspiel-Apps: App beenden). Der Speicher des
        // Programms gilt ab jetzt nicht mehr.
        LOGI("Core fordert Beenden an (SHUTDOWN)");
        if (gAdapter) {
            gAdapter->dropMemoryMap("Core beendet");
            gAdapter->mShutdownRequested.store(true);
        }
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        // DOS-Spiele wechseln häufig den Videomodus (#12) — Geometrie nachführen.
        const auto* geo = static_cast<const retro_game_geometry*>(data);
        if (!gAdapter || !geo) return false;
        gAdapter->setGeometry(*geo);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        // Geometrie, Bildrate und Samplerate übernehmen (z. B. PrBoom: Bildrate im Doom-Menü,
        // DOSBox-Pure: Videomodus 60/70 Hz). Der Run-Loop liest den Takt pro Frame neu;
        // der AAudio-Stream bleibt offen, nur die Umrechnung folgt.
        const auto* av = static_cast<const retro_system_av_info*>(data);
        if (!gAdapter || !av) return false;
        gAdapter->setGeometry(av->geometry);
        if (av->timing.sample_rate > 0.0) gAdapter->mAudio.setInputRate(av->timing.sample_rate);
        if (av->timing.fps > 1.0) gAdapter->mTimingFps = clampCoreFps(av->timing.fps);
        LOGI("Core-AV neu: %.2f fps, %.0f Hz (Takt %.2f)",
             static_cast<double>(av->timing.fps), av->timing.sample_rate, gAdapter->mTimingFps.load());
        return true;
    }
    default:
        // RetroArch-Muster: false = Erweiterung nicht unterstützt.
        return false;
    }
}

void LibretroAdapter::videoRefreshCallback(const void* data, unsigned width,
                                           unsigned height, size_t pitch) {
    if (!gAdapter || !data || width == 0 || height == 0) return;
    if (width > kTextureW || height > kTextureH) {
        LOGE("Core-Frame %ux%u passt nicht in %dx%d-Textur", width, height,
             kTextureW, kTextureH);
        return;
    }
    std::lock_guard<std::mutex> lock(gAdapter->mMutex);
    const uint32_t back = 1 - gAdapter->mFront;
    uint8_t* dst = gAdapter->mPixels[back].data();
    const auto* src = static_cast<const uint8_t*>(data);
    const int format = gAdapter->mPixelFormat.load();
    if (format == RETRO_PIXEL_FORMAT_XRGB8888) {
        if (pitch == static_cast<size_t>(width) * 4) {
            std::memcpy(dst, src, static_cast<size_t>(height) * pitch);
        } else {
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            for (unsigned y = 0; y < height; ++y)
                std::memcpy(dst + static_cast<size_t>(y) * rowBytes,
                            src + static_cast<size_t>(y) * pitch, rowBytes);
        }
    } else {
        // #16: 16-Bit-Frames (RGB565/0RGB1555) zeilenweise nach XRGB8888.
        const bool is565 = format == RETRO_PIXEL_FORMAT_RGB565;
        auto* out = reinterpret_cast<uint32_t*>(dst);
        for (unsigned y = 0; y < height; ++y) {
            uint16_t row[kTextureW];
            std::memcpy(row, src + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(width) * 2);   // Quelle nicht zwingend ausgerichtet
            uint32_t* line = out + static_cast<size_t>(y) * width;
            for (unsigned x = 0; x < width; ++x)
                line[x] = is565 ? rgb565ToXrgb(row[x]) : rgb1555ToXrgb(row[x]);
        }
    }
    if (gAdapter->mAnalyze.load())
        gAdapter->analyzeFrame(reinterpret_cast<const uint32_t*>(dst), static_cast<int>(width),
                               static_cast<int>(height));
    gAdapter->mFront = back;
    gAdapter->mFrameW = static_cast<int>(width);
    gAdapter->mFrameH = static_cast<int>(height);
    gAdapter->mFrameCounter.fetch_add(1);
}

void LibretroAdapter::audioSampleCallback(int16_t left, int16_t right) {
    // Einzel-Frames sind selten — gleicher Weg wie der Batch-Callback.
    if (gAdapter) {
        const int16_t frame[2] = {left, right};
        gAdapter->mAudio.write(frame, 1);
    }
}

size_t LibretroAdapter::audioSampleBatchCallback(const int16_t* data, size_t frames) {
    if (gAdapter) gAdapter->mAudio.write(data, frames);
    return frames;   // Framezahl melden, sonst wiederholt der Core.
}

void LibretroAdapter::inputPollCallback() {
    // Läuft auf dem Core-Thread am Anfang jedes retro_run: Snapshot aus den
    // Atomics ziehen, Absolutposition → Frame-Delta umrechnen.
    if (gAdapter) gAdapter->pollSnapshot();
}

int16_t LibretroAdapter::inputStateCallback(unsigned port, unsigned device,
                                            unsigned index, unsigned id) {
    if (!gAdapter) return 0;
    return gAdapter->queryInputState(port, device, index, id);
}

// ==============================================================================
// Input-Schnittstelle (Main-Thread) und Snapshot/Query (Core-Thread)
// ==============================================================================

void LibretroAdapter::setPointerUV(float u, float v, bool valid) {
    mPointerU.store(u);
    mPointerV.store(v);
    mPointerValid.store(valid);
}

void LibretroAdapter::setMouseButtons(bool left, bool right) {
    mMouseLeft.store(left);
    mMouseRight.store(right);
}

void LibretroAdapter::setStick(float x, float y) {
    // leichte Deadzone, damit der Cursor nicht zittert
    const float dead = 0.08f;
    mStickX.store(std::fabs(x) < dead ? 0.0f : x);
    mStickY.store(std::fabs(y) < dead ? 0.0f : y);
}

void LibretroAdapter::setKey(enum retro_key key, bool pressed) {
    if (key >= RETROK_LAST) return;
    mKeys[key].store(pressed);
    // Callback-Cores (ScummVM) bekommen das Ereignis im nächsten input_poll
    // auf dem Core-Thread — nie direkt vom Main-Thread.
    std::lock_guard<std::mutex> lock(mKeyEventMutex);
    mKeyEvents.push_back({key, pressed});
}

// Experimentell „Umgebung ausblenden“: grobe Bildanalyse auf Stichproben.
//   Rand    = schwarze Zeilen/Spalten an den Kanten (Overscan), je bis 16 Texel
//   Himmel  = häufigste Farbe im Band 20–45 % der Bildhöhe (muss ≥ 30 % haben)
//   skyEnd  = letzte Zeile ab 20 %, in der der Himmel noch überwiegt
//   Boden   = bis zu 4 häufigste Farben an den Bildrändern im unteren Viertel
//   Horizont = erste Zeile unterhalb, in der Bodenfarben überwiegen
// Reine Heuristik für Spiele mit Himmel oben und Boden unten.
void LibretroAdapter::analyzeFrame(const uint32_t* px, int w, int h) {
    EnvironmentKey key;
    if (w < 64 || h < 64) {
        std::lock_guard<std::mutex> lock(mKeyMutex);
        mKey = key;
        return;
    }
    auto rgb = [&](int x, int y) { return px[y * w + x] & 0xFFFFFFu; };
    auto isBlack = [](uint32_t c) {
        return ((c >> 16) & 0xFF) < 16 && ((c >> 8) & 0xFF) < 16 && (c & 0xFF) < 16;
    };
    // Häufigkeiten sortiert absteigend (Farbe, Anzahl)
    auto histogram = [](std::vector<uint32_t>& samples) {
        std::sort(samples.begin(), samples.end());
        std::vector<std::pair<uint32_t, int>> counts;
        for (size_t i = 0; i < samples.size();) {
            size_t j = i;
            while (j < samples.size() && samples[j] == samples[i]) ++j;
            counts.emplace_back(samples[i], static_cast<int>(j - i));
            i = j;
        }
        std::sort(counts.begin(), counts.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        return counts;
    };

    // Schwarzer Rand: Zeile/Spalte zählt, wenn ≥ 95 % der Stichproben schwarz sind.
    auto rowBlack = [&](int y) {
        int hits = 0, total = 0;
        for (int x = 0; x < w; x += 2, ++total) hits += isBlack(rgb(x, y));
        return hits * 100 >= total * 95;
    };
    auto colBlack = [&](int x) {
        int hits = 0, total = 0;
        for (int y = 0; y < h; y += 2, ++total) hits += isBlack(rgb(x, y));
        return hits * 100 >= total * 95;
    };
    int top = 0, bottom = 0, left = 0, right = 0;
    while (top < 16 && rowBlack(top)) ++top;
    while (bottom < 16 && rowBlack(h - 1 - bottom)) ++bottom;
    while (left < 16 && colBlack(left)) ++left;
    while (right < 16 && colBlack(w - 1 - right)) ++right;
    key.border[0] = static_cast<float>(top);
    key.border[1] = static_cast<float>(bottom);
    key.border[2] = static_cast<float>(left);
    key.border[3] = static_cast<float>(right);

    std::vector<uint32_t> samples;
    samples.reserve(2048);
    for (int y = h * 20 / 100; y < h * 45 / 100; y += 3)
        for (int x = left; x < w - right; x += 4) samples.push_back(rgb(x, y));
    const size_t skySamples = samples.size();
    const auto skyCounts = histogram(samples);
    if (skyCounts.empty() || skyCounts[0].second * 100 < static_cast<int>(skySamples) * 30) {
        std::lock_guard<std::mutex> lock(mKeyMutex);
        mKey = key;   // nur Rand, kein Himmel erkannt
        return;
    }
    key.sky = skyCounts[0].first;
    auto share = [&](int y, const uint32_t* colors, int n) {
        int hits = 0, total = 0;
        for (int x = left; x < w - right; x += 4, ++total) {
            const uint32_t c = rgb(x, y);
            for (int i = 0; i < n; ++i)
                if (c == colors[i]) { ++hits; break; }
        }
        return hits * 100 / std::max(total, 1);
    };
    int skyEnd = h * 20 / 100;
    for (int y = skyEnd; y < h * 85 / 100 && share(y, &key.sky, 1) >= 50; ++y) skyEnd = y + 1;

    samples.clear();
    for (int y = h * 75 / 100; y < h - bottom; y += 2)
        for (int x = 0; x < 24; x += 2) {
            samples.push_back(rgb(left + x, y));
            samples.push_back(rgb(w - 1 - right - x, y));
        }
    const size_t groundSamples = samples.size();
    const auto groundCounts = histogram(samples);
    for (const auto& gc : groundCounts) {
        if (key.groundCount >= 4 || gc.second * 100 < static_cast<int>(groundSamples) * 8) break;
        if (!isBlack(gc.first) && gc.first != key.sky) key.ground[key.groundCount++] = gc.first;
    }
    int horizon = skyEnd;
    if (key.groundCount > 0)
        for (int y = skyEnd; y < h * 90 / 100; ++y)
            if (share(y, key.ground, key.groundCount) >= 40) { horizon = y; break; }

    key.skyEnd = static_cast<float>(skyEnd);
    key.horizon = static_cast<float>(horizon);
    key.valid = true;
    std::lock_guard<std::mutex> lock(mKeyMutex);
    mKey = key;
}

EnvironmentKey LibretroAdapter::environmentKey() const {
    std::lock_guard<std::mutex> lock(mKeyMutex);
    return mKey;
}

// <saves>/<Systemordner>/<Spiel> — siehe save_ram.h (gleichnamige Spiele verschiedener Systeme
// und Ordner mit Punkt im Namen bekommen eigene Dateien).
std::string LibretroAdapter::saveBasePath() const {
    return saveram::basePath(std::string(mApp->activity->externalDataPath) + "/saves", mCoreFolder,
                             mGamePath, isDirectory(mGamePath));
}

// Beim Spielstart: Ordner anlegen und Spielstände vom bisherigen Ort (ohne Systemordner)
// übernehmen, sofern am neuen noch nichts liegt.
void LibretroAdapter::prepareSaves() {
    const std::string saves = std::string(mApp->activity->externalDataPath) + "/saves";
    makeDir(saves);
    if (!mCoreFolder.empty()) makeDir(saves + "/" + mCoreFolder);
    std::vector<std::string> suffixes = {".srm"};
    for (int slot = 1; slot <= 9; ++slot) suffixes.push_back(".state" + std::to_string(slot));
    const int moved = saveram::migrate(saveram::legacyBasePath(saves, mGamePath), saveBasePath(), suffixes);
    if (moved > 0) LOGI("Spielstände übernommen (%d Dateien) nach %s", moved, saveBasePath().c_str());
}

std::string LibretroAdapter::statePath(int slot) const {
    return saveBasePath() + ".state" + std::to_string(slot);
}

void LibretroAdapter::beginSaveRam() {
    mSaveRam.reset();
    if (!mGetMemoryData || !mGetMemorySize) return;
    const size_t size = mGetMemorySize(RETRO_MEMORY_SAVE_RAM);
    auto* mem = static_cast<uint8_t*>(mGetMemoryData(RETRO_MEMORY_SAVE_RAM));
    if (!mem || size == 0) return;   // Spiel/Core ohne Batteriespeicher (z. B. DOS)
    const std::string path = saveBasePath() + ".srm";
    const bool loaded = mSaveRam.begin(path, mem, size);
    LOGI("Batteriespeicher %zu Bytes: %s %s", size, loaded ? "geladen aus" : "neu, Ziel", path.c_str());
}

void LibretroAdapter::flushSaveRamLocked() {
    if (mSaveRam.path().empty() || !mGetMemoryData || !mGetMemorySize) return;
    const size_t size = mGetMemorySize(RETRO_MEMORY_SAVE_RAM);
    const auto* mem = static_cast<const uint8_t*>(mGetMemoryData(RETRO_MEMORY_SAVE_RAM));
    errno = 0;
    const int r = mSaveRam.flush(mem, size);
    if (r > 0) LOGI("Batteriespeicher gesichert (%zu Bytes): %s", size, mSaveRam.path().c_str());
    // Fehler nur beim Wechsel melden — die Sicherung läuft alle ~2 s und hätte das Log geflutet.
    if (r < 0 && !mSaveRamFailing) LOGE("Batteriespeicher nicht schreibbar: %s", mSaveRam.path().c_str());
    else if (r > 0 && mSaveRamFailing) LOGI("Batteriespeicher wieder schreibbar");
    if (r != 0) mSaveRamFailing = r < 0;
}

void LibretroAdapter::flushSaveRam() {
    if (!mCoreLoaded) return;
    std::lock_guard<std::mutex> run(mRunMutex);   // nie mitten in retro_run
    flushSaveRamLocked();
}

bool LibretroAdapter::hasState(int slot) const {
    return mCoreLoaded && ::access(statePath(slot).c_str(), F_OK) == 0;
}

void LibretroAdapter::setPadDevice(unsigned device) {
    if (!mCoreLoaded || !mSetPortDevice || device == mPadDevice) return;
    std::lock_guard<std::mutex> run(mRunMutex);   // zwischen zwei retro_run
    mSetPortDevice(0, device);
    mPadDevice = device;
    LOGI("Pad-Typ Port 0: %s", device == RETRO_DEVICE_ANALOG ? "Analog" : "Digital");
}

bool LibretroAdapter::statesSupported() {
    if (mStatesChecked) return mStatesSupported;
    if (!mCoreLoaded || mFrameCounter.load() == mStatesFrameBase) return true;   // noch kein Frame
    if (!mSerializeSize || !mSerialize || !mUnserialize) {
        mStatesSupported = false;
    } else {
        std::unique_lock<std::mutex> run(mRunMutex, std::try_to_lock);   // nie mitten in retro_run
        if (!run.owns_lock()) return mStatesSupported;                  // nächster Frame
        mStatesSupported = mSerializeSize() > 0;
    }
    mStatesChecked = true;
    LOGI("Spielstände (Savestates): %s", mStatesSupported ? "ja" : "nein — Speichern im Spiel");
    return mStatesSupported;
}

bool LibretroAdapter::saveState(int slot) {
    if (!mCoreLoaded || !mSerializeSize || !mSerialize) return false;
    std::lock_guard<std::mutex> run(mRunMutex);   // nie mitten in retro_run
    const size_t size = mSerializeSize();
    if (size == 0) { LOGE("Core unterstützt keine Spielstände"); return false; }
    std::vector<uint8_t> data(size);
    if (!mSerialize(data.data(), size)) { LOGE("retro_serialize fehlgeschlagen"); return false; }
    const std::string saves = std::string(mApp->activity->externalDataPath) + "/saves";
    makeDir(saves);
    if (!mCoreFolder.empty()) makeDir(saves + "/" + mCoreFolder);
    const std::string path = statePath(slot), tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { LOGE("Spielstand nicht schreibbar: %s", tmp.c_str()); return false; }
    const bool ok = std::fwrite(data.data(), 1, size, f) == size;
    if (std::fclose(f) != 0 || !ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        LOGE("Spielstand speichern fehlgeschlagen: %s", path.c_str());
        return false;
    }
    LOGI("Spielstand %d gespeichert (%zu Bytes): %s", slot, size, path.c_str());
    return true;
}

bool LibretroAdapter::loadState(int slot) {
    if (!mCoreLoaded || !mUnserialize) return false;
    const std::string path = statePath(slot);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { LOGI("Kein Spielstand %d: %s", slot, path.c_str()); return false; }
    std::vector<uint8_t> data;
    uint8_t buf[64 * 1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    std::lock_guard<std::mutex> run(mRunMutex);
    const bool ok = !data.empty() && mUnserialize(data.data(), data.size());
    LOGI("Spielstand %d laden: %s", slot, ok ? "ok" : "fehlgeschlagen");
    return ok;
}

void LibretroAdapter::setPauseReason(PauseReason reason, bool on) {
    bool paused;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mPauseReasons = on ? (mPauseReasons | reason) : (mPauseReasons & ~static_cast<unsigned>(reason));
        paused = mPauseReasons != 0;
        if (mPaused == paused) return;
        mPaused = paused;
    }
    mCv.notify_all();
    // Ton: der Ring hält ~80 ms — stumm schalten, damit nach dem Anhalten nichts nachklingt.
    mAudio.setVolume(paused ? 0.0f : 1.0f);
    if (paused) {
        for (auto& r : mRumble) r.store(0);   // Controller nicht weiter vibrieren lassen
        flushSaveRam();   // VR-Menü offen / Brille ab: oft folgt Beenden
    }
    if (paused)
        LOGI("Spiel pausiert (Grund: %s%s%s)", mPauseReasons & kPauseMenu ? "Menü" : "",
             (mPauseReasons & kPauseMenu) && (mPauseReasons & kPauseFocus) ? ", " : "",
             mPauseReasons & kPauseFocus ? "Fokus/Session" : "");
    else
        LOGI("Spiel fortgesetzt");
}

void LibretroAdapter::setPadButtons(unsigned port, uint16_t mask) {
    if (port < 2) mPad[port].store(mask);
}

void LibretroAdapter::setPadAnalog(unsigned port, const padmap::Analog& analog) {
    if (port >= 2) return;
    mPadAnalog[port][0].store(analog.lx);
    mPadAnalog[port][1].store(analog.ly);
    mPadAnalog[port][2].store(analog.rx);
    mPadAnalog[port][3].store(analog.ry);
}

void LibretroAdapter::setGeometry(const retro_game_geometry& geo) {
    mGameW.store(static_cast<int>(geo.base_width));
    mGameH.store(static_cast<int>(geo.base_height));
    // aspect_ratio <= 0 heißt laut libretro: base_width / base_height.
    float aspect = geo.aspect_ratio;
    if (aspect <= 0.0f && geo.base_height > 0)
        aspect = static_cast<float>(geo.base_width) / static_cast<float>(geo.base_height);
    if (aspect > 0.0f) mAspect.store(aspect);
    LOGI("Core-Geometrie: %ux%u, Seitenverhältnis %.3f", geo.base_width,
         geo.base_height, static_cast<double>(aspect));
}

void LibretroAdapter::pollSnapshot() {
    // libretro-Mausprotokoll ist relativ: X/Y liefern die Bewegung seit dem
    // letzten Poll. UV (Screen) → Core-Pixel, Delta gegen den Vorsnapshot.
    // #11: Der Screen zeigt genau den letzten Frame — also auf dessen Größe
    // abbilden. mFrameW/H schreibt der Video-Callback auf demselben
    // (Core-)Thread; vor dem ersten Frame gilt die gemeldete Geometrie.
    int w = mFrameW, h = mFrameH;
    if (w <= 0 || h <= 0) { w = mGameW.load(); h = mGameH.load(); }
    const bool valid = mPointerValid.load() && w > 0 && h > 0;
    // Zeigerverschiebung des Spiels (VR-Menü → DOS) vor Delta und absoluter Position.
    const float u = ptroff::apply(mPointerU.load(), mOffsetX.load());
    const float v = ptroff::apply(mPointerV.load(), mOffsetY.load());
    const float x = valid ? u * static_cast<float>(w) : 0.0f;
    const float y = valid ? v * static_cast<float>(h) : 0.0f;
    // #28: Subpixel-Rest behalten — sonst gehen langsame Bewegungen (< 1 px pro Frame)
    // verloren. xr.dosgames, relativer Modus: Faktor pro Achse und „Zeiger fangen“
    // (mouse_relative.h); sonst Faktor 1 und kein Fangen = bisheriges Verhalten.
    // xr.adventure (ScummVM): Fangen beim Start und bei jedem Bildgrößenwechsel (Spiel ↔ Menü),
    // Faktor 1 — siehe mouse_relative.h.
    bool catchEnabled = mScummCore;
    // Optionen, die jeder Poll braucht, nur bei neuer Revision lesen (sonst Mutex + String-Kopie
    // pro Core-Frame).
    const unsigned rev = mOptions.revision();
    if (rev != mPollOptRevision) {
        mPollOptRevision = rev;
        std::string mode, log;
        mPollRelative = mOptions.active("dosbox_pure_mouse_input", mode) && (mode == "virtual" || mode == "true");
        mPollLogPointer = mOptions.active("xrshell_log_pointer", log) && log == "1";
    }
    if (mUserOptionsEnabled && mDosboxCore) catchEnabled = mPollRelative;
    // Gekoppelt bzw. am Lernen: Zeigerposition kommt aus cursor_lock.h — kein Fangen (es sperrte
    // nur kurz die Maustasten).
    if (catchEnabled && mDosboxCore && (mFollower.active() || mLearner.running())) catchEnabled = false;
    // Bildgröße gewechselt (DOS-Videomodus, ScummVM-Menü 640×480 über dem Spiel 320×200): letzten
    // Laserpunkt umrechnen statt im ersten Poll um die Größendifferenz zu springen.
    if (w > 0 && h > 0 && (w != mPollW || h != mPollH)) {
        if (mPollW > 0 && mPollH > 0) {
            mRelMouse.resized(static_cast<float>(w) / static_cast<float>(mPollW),
                              static_cast<float>(h) / static_cast<float>(mPollH), mScummCore);
            LOGI("Maus: Bildgröße %dx%d → %dx%d%s", mPollW, mPollH, w, h,
                 mScummCore ? ", Zeiger wird neu gefangen" : "");
        }
        mPollW = w;
        mPollH = h;
    }
    if (catchEnabled) {
        mRelMouse.gainX = mScummCore ? 1.0f : mGainX.load();
        mRelMouse.gainY = mScummCore ? 1.0f : mGainY.load();
        mRelMouse.frozen = mPointerFrozen.load();
        if (mCatchRequest.exchange(false)) {
            const float tu = mCatchTargetU.load(), tv = mCatchTargetV.load();
            mRelMouse.targetX = tu >= 0.0f ? tu * static_cast<float>(w) : -1.0f;
            mRelMouse.targetY = tv >= 0.0f ? tv * static_cast<float>(h) : -1.0f;
            mRelMouse.request();
        }
    } else {
        mRelMouse.gainX = mRelMouse.gainY = 1.0f;
        mRelMouse.frozen = false;
        mCatchRequest.store(false);
    }
    const unsigned catchesBefore = mRelMouse.catches;
    mRelMouse.poll(x, y, valid, catchEnabled, mMouseDeltaX, mMouseDeltaY);
    if (mRelMouse.catches != catchesBefore && w > 0 && h > 0) {
        mCaughtU.store(mRelMouse.caughtX / static_cast<float>(w));
        mCaughtV.store(mRelMouse.caughtY / static_cast<float>(h));
        mCatchCount.fetch_add(1);
        LOGI("Zeiger gefangen bei %.0f/%.0f (Faktor %.2f/%.2f)", static_cast<double>(mRelMouse.caughtX),
             static_cast<double>(mRelMouse.caughtY),
             static_cast<double>(mRelMouse.gainX), static_cast<double>(mRelMouse.gainY));
    }
    flushKeyEvents();
    // Absolut für RETRO_DEVICE_POINTER: letzte gültige Laserposition halten.
    // DOSBox-Pure „direct“ setzt daraus den Zeiger absolut (pointer_map.h).
    if (mPointerValid.load())
        pointerFromUv(u, v, mPointerAbsX, mPointerAbsY);
    // Zeiger gekoppelt bzw. am Lernen: Deltas kommen aus cursor_lock.h statt aus den Laser-Deltas.
    if (mDosboxCore && mUserOptionsEnabled) cursorPoll(valid, u, v, w, h);
    logPointer(valid, u, v, w, h, catchEnabled);
}

// Core-Thread: Speicherbereiche verwerfen (Zeiger zeigen evtl. in freigegebenen Speicher).
void LibretroAdapter::dropMemoryMap(const char* why) {
    if (mMemRegions.empty()) return;
    mMemRegions.clear();
    ++mMemGeneration;
    LOGI("Speicherkarte verworfen: %s", why);
}

// Ergebnis des Lernens an den Main-Thread (takeCursorLearnResult); bei Erfolg wird die neue
// Kopplung vorn eingereiht, bisherige (z. B. Menü ↔ Spielrunde) bleiben für den Wechsel.
void LibretroAdapter::finishLearn(bool ok, const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mLockMutex);
        mLearnOk = ok;
        mLearnMessage = message;
        if (ok) {
            const std::vector<curlock::Lock> all = curlock::addLock(mFollower.locks(), mLearner.lock());
            mFollower.setAll(all, 0);
            mLearnEncoded = curlock::encodeList(all);
        } else {
            mLearnEncoded.clear();
        }
    }
    // Fehlgeschlagen: bisherige Kopplungen laufen unverändert weiter.
    mCursorState.store(mFollower.active() ? kCursorLocked : mFollower.lost() ? kCursorLost : kCursorOff);
    mLearnDone.store(true);
    LOGI("Zeiger lernen %s: %s", ok ? "fertig" : "fehlgeschlagen", message.c_str());
}

bool LibretroAdapter::cursorPoll(bool valid, float u, float v, int w, int h) {
    if (mLockPending.exchange(false)) {
        std::string enc;
        {
            std::lock_guard<std::mutex> lock(mLockMutex);
            enc = mLockIn;
        }
        const std::vector<curlock::Lock> locks = curlock::decodeList(enc);
        if (!locks.empty()) {
            mFollower.setAll(locks);   // pausiert: koppelt, sobald eine Kopplung mitläuft
            mCursorState.store(kCursorLost);
            LOGI("Zeiger-Kopplungen geladen (%zu), warte auf passende: %s", locks.size(), enc.c_str());
        } else {
            mFollower.clear();
            if (mCursorState.load() != kCursorLearning) mCursorState.store(kCursorOff);
            if (!enc.empty()) LOGE("Zeiger-Kopplung unlesbar: %s", enc.c_str());
        }
    }
    if (mLearnRequest.exchange(false)) {
        if (mMemRegions.empty()) {
            finishLearn(false, "keine Speicherkarte des Cores (Programm abgestürzt/beendet?)");
        } else {
            mLearner.start(w, h);
            mLearnGeneration = mMemGeneration;
            mCursorState.store(kCursorLearning);
            LOGI("Zeiger lernen: %zu Speicherbereiche, Bild %dx%d", mMemRegions.size(), w, h);
        }
    }
    int dx = 0, dy = 0;
    if (mLearner.running()) {
        if (mLearnGeneration != mMemGeneration) {
            mLearner = curlock::Learner{};
            finishLearn(false, "Speicher gewechselt (Programmwechsel/Absturz) — bitte erneut lernen");
            return false;
        }
        mLearner.poll(mMemRegions, dx, dy);
        if (!mLearner.running()) {
            const bool ok = mLearner.state() == curlock::Learner::State::Ok;
            finishLearn(ok, mLearner.message());
            mLearner = curlock::Learner{};   // Kandidatenlisten freigeben
        }
    } else if (mFollower.active()) {
        mFollower.poll(mMemRegions, u, v, valid, dx, dy);
        if (mFollower.lost()) {
            mCursorState.store(kCursorLost);
            LOGI("Zeiger-Kopplung pausiert (Spielwert reagiert nicht) — relative Maus, beobachte weiter");
            return false;
        }
    } else if (mFollower.lost()) {
        // Pausiert: relative Maus fährt (mMouseDelta*), die gelernten Werte werden beobachtet; bei
        // ruhigem Laser schickt observe() reihum Probebewegungen Richtung Laser.
        int pdx = 0, pdy = 0;
        const bool resumed = mFollower.observe(mMemRegions, u, v, valid, mMouseDeltaX, mMouseDeltaY, pdx, pdy);
        if (resumed) {
            mCursorState.store(kCursorLocked);
            LOGI("Zeiger-Kopplung aktiv: %s", mFollower.lock().encode().c_str());
        }
        if (pdx == 0 && pdy == 0) return false;
        dx = pdx;
        dy = pdy;
    } else {
        return false;
    }
    mMouseDeltaX = dx;
    mMouseDeltaY = dy;
    return true;
}

// Diagnose (xrshell.opt: xrshell_log_pointer = 1): Laser im Spielbild alle ~0,2 s ins Log, wenn er
// sich bewegt hat — Screenshots lassen sich über die Uhrzeit der Laserposition zuordnen.
void LibretroAdapter::logPointer(bool valid, float u, float v, int w, int h, bool relative) {
    if (++mPointerLogTick % 14 != 0) return;
    if (!mPollLogPointer || !valid) return;
    const int px = static_cast<int>(u * static_cast<float>(w)), py = static_cast<int>(v * static_cast<float>(h));
    if (px == mPointerLogX && py == mPointerLogY) return;
    mPointerLogX = px;
    mPointerLogY = py;
    LOGI("Laser: Spiel %d/%d von %dx%d (u %.3f v %.3f, Versatz %.2f/%.2f %%, %s, abs %d/%d)", px, py, w, h,
         static_cast<double>(u), static_cast<double>(v), static_cast<double>(mOffsetX.load()),
         static_cast<double>(mOffsetY.load()), relative ? "relativ" : "absolut",
         static_cast<int>(mPointerAbsX), static_cast<int>(mPointerAbsY));
}

void LibretroAdapter::flushKeyEvents() {
    std::vector<KeyEvent> events;
    {
        std::lock_guard<std::mutex> lock(mKeyEventMutex);
        events.swap(mKeyEvents);
    }
    if (!mKeyboardCallback) return;
    for (const KeyEvent& e : events) {
        // Zeichen: druckbares ASCII entspricht dem RETROK-Code (Kleinbuchstaben),
        // Shift macht Buchstaben groß.
        const bool shift = mKeys[RETROK_LSHIFT].load() || mKeys[RETROK_RSHIFT].load();
        uint16_t mods = 0;
        if (shift) mods |= RETROKMOD_SHIFT;
        if (mKeys[RETROK_LCTRL].load() || mKeys[RETROK_RCTRL].load()) mods |= RETROKMOD_CTRL;
        if (mKeys[RETROK_LALT].load() || mKeys[RETROK_RALT].load()) mods |= RETROKMOD_ALT;
        uint32_t ch = 0;
        if (e.pressed && e.key >= 32 && e.key < 127) {
            ch = static_cast<uint32_t>(e.key);
            if (shift && ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        } else if (e.pressed && (e.key == RETROK_RETURN || e.key == RETROK_BACKSPACE ||
                                 e.key == RETROK_TAB || e.key == RETROK_ESCAPE)) {
            ch = static_cast<uint32_t>(e.key);
        }
        mKeyboardCallback(e.pressed, static_cast<unsigned>(e.key), ch, mods);
    }
}

float LibretroAdapter::rumbleAmplitude() const {
    // „Schwach“ (Aufsammeln, Berühren) gedämpft, damit Schüsse/Treffer deutlich bleiben.
    const float strong = mRumble[RETRO_RUMBLE_STRONG].load() / 65535.0f;
    const float weak = mRumble[RETRO_RUMBLE_WEAK].load() / 65535.0f * 0.6f;
    return strong > weak ? strong : weak;
}

int16_t LibretroAdapter::queryInputState(unsigned port, unsigned device,
                                         unsigned index, unsigned id) const {
    // #16: Pad-Core — nur RetroPad, Port 0/1, Bitmaske vom Main-Thread.
    // Dazu beide Analogsticks (Totzone in main.cpp, padmap) und Tasten als „Analog-Tasten“
    // (die Trigger sind in XRShell digital: 0 oder Vollausschlag).
    if (mPadMode) {
        if (port >= 2) return 0;
        const uint16_t mask = mPad[port].load();
        if (device == RETRO_DEVICE_ANALOG) {
            if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
                return id < 16 && (mask & (1u << id)) ? 0x7FFF : 0;
            if (index > RETRO_DEVICE_INDEX_ANALOG_RIGHT || id > RETRO_DEVICE_ID_ANALOG_Y) return 0;
            return mPadAnalog[port][index * 2 + id].load();
        }
        if (device != RETRO_DEVICE_JOYPAD) return 0;
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(mask);
        return id < 16 && (mask & (1u << id)) ? 1 : 0;
    }
    if (port != 0) return 0;

    switch (device) {
    case RETRO_DEVICE_MOUSE:
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X: return static_cast<int16_t>(mMouseDeltaX);
        case RETRO_DEVICE_ID_MOUSE_Y: return static_cast<int16_t>(mMouseDeltaY);
        // Während „Zeiger fangen“ gelten die Tasten als los — der Klick kommt danach an der
        // richtigen Stelle an statt unterwegs.
        case RETRO_DEVICE_ID_MOUSE_LEFT:  return mMouseLeft.load() && !mRelMouse.busy() && !mLearner.running() ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return mMouseRight.load() && !mRelMouse.busy() && !mLearner.running() ? 1 : 0;
        default: return 0;
        }
    case RETRO_DEVICE_POINTER:
        // Nur ein Zeiger (index 0); „gedrückt“ = linke Maustaste.
        if (index != 0) return 0;
        switch (id) {
        case RETRO_DEVICE_ID_POINTER_X:       return mPointerAbsX;
        case RETRO_DEVICE_ID_POINTER_Y:       return mPointerAbsY;
        case RETRO_DEVICE_ID_POINTER_PRESSED: return mMouseLeft.load() ? 1 : 0;
        case RETRO_DEVICE_ID_POINTER_COUNT:   return 1;
        default: return 0;
        }
    case RETRO_DEVICE_KEYBOARD:
        return id < RETROK_LAST && mKeys[static_cast<enum retro_key>(id)].load() ? 1 : 0;
    case RETRO_DEVICE_ANALOG:
        // rechter Touch-Thumbstick → linker Analogstick des Cores
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
            const float v = (id == RETRO_DEVICE_ID_ANALOG_X) ? mStickX.load()
                        : (id == RETRO_DEVICE_ID_ANALOG_Y) ? mStickY.load() : 0.0f;
            return static_cast<int16_t>(v * 32767.0f);
        }
        return 0;
    case RETRO_DEVICE_JOYPAD:
        // Stick mit 0.5-Schwelle zusätzlich als digitales DPad — DOS-Spiele
        // lesen meist Pfeiltasten/Joypad, nicht Analog.
        switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_LEFT:  return mStickX.load() < -0.5f ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: return mStickX.load() >  0.5f ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_UP:    return mStickY.load() < -0.5f ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:  return mStickY.load() >  0.5f ? 1 : 0;
        default: return 0;
        }
    default:
        return 0;
    }
}

// ==============================================================================
// Core-Bezug: Assets → filesDir, Spielsuche
// ==============================================================================

bool LibretroAdapter::extractCoreFromAssets(android_app* app, const char* soName) {
    const std::string dir = std::string(app->activity->internalDataPath) + "/cores";
    const std::string dest = dir + "/" + soName;
    const std::string tmp = dest + ".tmp";
    const std::string assetPath = std::string("cores/") + soName;   // im APK unter assets/

    AAsset* asset = AAssetManager_open(app->activity->assetManager,
                                       assetPath.c_str(), AASSET_MODE_STREAMING);
    if (!asset) {
        // Kein Core im APK — ein früher entpackter bleibt nutzbar.
        return ::access(dest.c_str(), F_OK) == 0;
    }

    // #13: Nach einem APK-Update mit neuem Core neu entpacken. Größenvergleich
    // genügt (Buildbot-Nightlies ändern praktisch immer die Größe) und kostet
    // beim Normalstart nur ein stat().
    struct stat st {};
    if (::stat(dest.c_str(), &st) == 0 &&
        static_cast<int64_t>(st.st_size) == AAsset_getLength64(asset)) {
        AAsset_close(asset);
        return true;   // aktueller Core schon entpackt
    }

    if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
        LOGE("Konnte Core-Verzeichnis nicht anlegen: %s", dir.c_str());
        AAsset_close(asset);
        return false;
    }
    // Erst in .tmp schreiben, dann umbenennen — ein abgebrochenes Entpacken
    // hinterlässt nie einen halben Core unter dem echten Namen.
    FILE* out = std::fopen(tmp.c_str(), "wb");
    if (!out) {
        LOGE("Konnte Core-Datei nicht schreiben: %s", tmp.c_str());
        AAsset_close(asset);
        return false;
    }
    char buf[64 * 1024];
    int n;
    bool ok = true;
    while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0) {
        if (std::fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
            ok = false;
            break;
        }
    }
    if (n < 0) ok = false;
    if (std::fclose(out) != 0) ok = false;
    AAsset_close(asset);
    if (!ok || ::rename(tmp.c_str(), dest.c_str()) != 0) {
        LOGE("Core-Entpacken fehlgeschlagen: %s", dest.c_str());
        ::unlink(tmp.c_str());
        return false;
    }
    ::chmod(dest.c_str(), 0755);   // dlopen braucht kein +x, aber sauber ist sauber
    mCoreExtracted.store(true);    // Ladeanzeige: erster Start nach Installation/Update
    LOGI("Core aus Assets entpackt: %s", dest.c_str());
    return true;
}

// #28: assets/system/scummvm/{extra,theme}/* → <System-Dir>/scummvm/… (einmalig,
// danach nur stat()). Fehlende Dateien meldet der Core selbst („extra folder not found“).
void LibretroAdapter::extractScummSystem(android_app* app) {
    AAssetManager* mgr = app->activity->assetManager;
    int ready = 0, failed = 0;
    makeDir(mSystemDir + "/scummvm");
    for (const char* sub : kScummSystemDirs) {
        const std::string assetDir = std::string("system/") + sub;
        const std::string destDir = mSystemDir + "/" + sub;
        AAssetDir* dir = AAssetManager_openDir(mgr, assetDir.c_str());
        if (!dir) continue;
        makeDir(destDir);
        while (const char* name = AAssetDir_getNextFileName(dir)) {
            if (extractAsset(mgr, assetDir + "/" + name, destDir + "/" + name)) ++ready;
            else ++failed;
        }
        AAssetDir_close(dir);
    }
    LOGI("ScummVM-Systemdateien: %d bereit, %d fehlgeschlagen (%s/scummvm)", ready, failed,
         mSystemDir.c_str());
}

// xr.adventure: <System-Dir>/scummvm.ini mit Gerätesprache und Untertiteln, nur falls sie fehlt
// (scumm_setup.h). Erst .tmp, dann rename — ScummVM liest nie eine halbe Datei.
void LibretroAdapter::writeScummIni() {
    const std::string path = mSystemDir + "/scummvm.ini";
    const std::string gui = scummsetup::guiLanguage(mLanguage, mCountry);
    std::string text;
    if (::access(path.c_str(), F_OK) == 0) {
        // Vorhanden: nur gui_language nachtragen (Menüsprache), sonst unverändert lassen.
        std::string old;
        FILE* in = std::fopen(path.c_str(), "rb");
        if (!in) {   // nicht lesbar: lieber nichts tun als die Einstellungen überschreiben
            LOGE("scummvm.ini nicht lesbar, bleibt unverändert: %s (%s)", path.c_str(), strerror(errno));
            return;
        }
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) old.append(buf, n);
        std::fclose(in);
        text = scummsetup::ensureGuiLanguage(old, gui);
        if (text == old) {
            LOGI("scummvm.ini vorhanden, bleibt unverändert: %s", path.c_str());
            return;
        }
        LOGI("scummvm.ini: Menüsprache %s nachgetragen", gui.c_str());
    } else {
        text = scummsetup::iniText(scummsetup::scummLanguage(mLanguage, mCountry), gui);
    }
    const std::string lang = scummsetup::scummLanguage(mLanguage, mCountry);
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f && std::fclose(f) != 0) ok = false;
    if (ok && ::rename(tmp.c_str(), path.c_str()) == 0) {
        LOGI("scummvm.ini geschrieben: Sprache %s, Menü %s (Gerät %s-%s), Untertitel an",
             lang.empty() ? "wie Spieldaten" : lang.c_str(), gui.empty() ? "Englisch" : gui.c_str(),
             mLanguage.c_str(), mCountry.c_str());
    } else {
        ::unlink(tmp.c_str());
        LOGE("scummvm.ini nicht anlegbar: %s (%s)", path.c_str(), strerror(errno));
    }
}

std::string LibretroAdapter::findGameFile(android_app* app) {
    const std::string gameDir = std::string(app->activity->externalDataPath) + "/game";
    // Die App legt game/ selbst an — ein per adb angelegter Ordner gehört
    // „shell“ und ist für die App ohne chmod nicht lesbar.
    ::mkdir(app->activity->externalDataPath, 0770);
    ::mkdir(gameDir.c_str(), 0770);
    DIR* dir = opendir(gameDir.c_str());
    if (!dir) {
        LOGE("Spielordner nicht lesbar: %s (%s)%s", gameDir.c_str(), strerror(errno),
             errno == EACCES ? " — per adb angelegt? adb shell chmod 0777 auf den Ordner" : "");
        return {};
    }
    std::vector<std::string> files, dirs;
    while (dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = gameDir + "/" + entry->d_name;
        if (isDirectory(path)) dirs.push_back(path);
        else if (coreForFile(entry->d_name)) files.push_back(path);
    }
    closedir(dir);
    // readdir-Reihenfolge ist zufällig — sortieren, damit die Wahl stabil ist.
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());

    // 1. Datei direkt in game/ = ausdrückliche Wahl (M1a: kein Browser).
    if (!files.empty()) return files.front();
    // #28: Spielordner mit „*.scummvm“ → ScummVM (vor dem DOSBox-Fallback;
    //      es gewinnt wie dort der alphabetisch erste Ordner).
    if (!dirs.empty()) {
        const std::string target = scummTargetIn(dirs.front());
        if (!target.empty()) return target;
    }
    // 2. Spielordner (#13): DOSBox-Pure lädt Verzeichnisse als Laufwerk C:
    //    und zeigt sein Startmenü zur Auswahl der EXE.
    if (!dirs.empty()) {
        LOGI("Keine Startdatei in game/ — Ordner als Laufwerk C: %s",
             dirs.front().c_str());
        return dirs.front();
    }
    return {};
}

bool LibretroAdapter::readGameData(const std::string& path) {
    FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) return false;
    mGameData.clear();
    uint8_t buf[64 * 1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
        mGameData.insert(mGameData.end(), buf, buf + n);
    const bool ok = !std::ferror(in) && !mGameData.empty();
    std::fclose(in);
    return ok;
}

// xrshell.opt: eine Option pro Zeile, `schlüssel = wert` (Anführungszeichen
// optional), `#` leitet Kommentare ein. Suche wie beim Tastenprofil: im
// Spielordner bzw. neben der Spieldatei, sonst im übergeordneten Ordner (game/).
// Danach (nur wenn eingeschaltet, xr.dosgames) die Nutzerwerte aus dem VR-Menü.
void LibretroAdapter::loadCoreOptions(const std::string& gamePath) {
    OptionStore::Entries profile, user;
    std::string dir = gamePath;
    if (!isDirectory(dir)) dir = dir.substr(0, dir.rfind('/'));
    const std::string candidates[] = {dir + "/xrshell.opt",
                                      dir.substr(0, dir.rfind('/')) + "/xrshell.opt"};
    for (const std::string& path : candidates) {
        std::string text;
        if (!optstore::readFile(path, text)) continue;
        profile = optstore::parse(text);
        for (const auto& e : profile) LOGI("Kern-Option: %s = %s", e.first.c_str(), e.second.c_str());
        LOGI("Kern-Optionen aus %s (%zu)", path.c_str(), profile.size());
        break;
    }
    mUserOptionsPath = mUserOptionsEnabled ? userOptionsPath(gamePath) : std::string();
    if (!mUserOptionsPath.empty()) {
        std::string text;
        if (optstore::readFile(mUserOptionsPath, text)) {
            user = optstore::parse(text);
            for (const auto& e : user) LOGI("Nutzer-Option: %s = %s", e.first.c_str(), e.second.c_str());
            LOGI("Nutzer-Optionen aus %s (%zu)", mUserOptionsPath.c_str(), user.size());
        }
    }
    mOptions.begin(profile, user);
}

// <externalDataPath>/settings/<Spielordner bzw. Spieldatei ohne Endung>.user.opt — im
// App-eigenen Ordner (immer beschreibbar), nicht im per adb angelegten Spielordner.
std::string LibretroAdapter::userOptionsPath(const std::string& gamePath) const {
    if (!mApp || gamePath.empty()) return {};
    std::string name = gamePath;
    while (!name.empty() && name.back() == '/') name.pop_back();
    const bool folder = isDirectory(name);
    name = name.substr(name.rfind('/') + 1);
    const size_t dot = name.rfind('.');
    if (!folder && dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    if (name.empty()) return {};
    const std::string dir = std::string(mApp->activity->externalDataPath) + "/settings";
    makeDir(mApp->activity->externalDataPath);
    makeDir(dir);
    ::chmod(dir.c_str(), 0775);   // adb darf die Nutzerwerte lesen (Dateien 0664, optstore::writeFile)
    return dir + "/" + name + ".user.opt";
}

bool LibretroAdapter::saveUserOptions() {
    if (mUserOptionsPath.empty() || !mOptions.dirty()) return true;
    const OptionStore::Entries user = mOptions.userEntries();
    bool ok;
    if (user.empty()) {
        ok = ::unlink(mUserOptionsPath.c_str()) == 0 || errno == ENOENT;
        LOGI("Nutzer-Optionen gelöscht (Spiel-Standard): %s", mUserOptionsPath.c_str());
    } else {
        ok = optstore::writeFile(mUserOptionsPath, optstore::serialize(user,
            "# XRShell: im VR-Menü geändert, gilt nach xrshell.opt des Spielordners.\n"
            "# Löschen = Spiel-Standard (VR-Menü → DOS → Spiel-Standard).\n"));
        LOGI("Nutzer-Optionen gespeichert (%zu): %s", user.size(), mUserOptionsPath.c_str());
    }
    if (ok) mOptions.clearDirty();
    else LOGE("Nutzer-Optionen nicht speicherbar: %s (%s)", mUserOptionsPath.c_str(), strerror(errno));
    return ok;
}

// Optionsliste des Cores (alle libretro-Varianten) → Definitionen für das VR-Menü.
void LibretroAdapter::addCoreOptionDefinitions(unsigned cmd, const void* data) {
    size_t count = 0;
    auto add = [&](const char* key, const retro_core_option_value* values, const char* def) {
        optstore::Definition d;
        d.key = key;
        for (int i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && values[i].value; ++i)
            d.values.emplace_back(values[i].value, values[i].label ? values[i].label : "");
        d.defaultValue = def ? def : (d.values.empty() ? "" : d.values.front().first);
        mOptions.addDefinition(d);
        ++count;
    };
    auto addV1 = [&](const retro_core_option_definition* defs) {
        for (; defs && defs->key; ++defs) add(defs->key, defs->values, defs->default_value);
    };
    auto addV2 = [&](const retro_core_options_v2* opts) {
        if (!opts) return;
        for (const retro_core_option_v2_definition* defs = opts->definitions; defs && defs->key; ++defs)
            add(defs->key, defs->values, defs->default_value);
    };
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (const auto* var = static_cast<const retro_variable*>(data); var->key; ++var) {
            optstore::Definition d;
            if (optstore::parseLegacyDefinition(var->key, var->value, d)) {
                mOptions.addDefinition(d);
                ++count;
            }
        }
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        addV1(static_cast<const retro_core_option_definition*>(data));
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        addV1(static_cast<const retro_core_options_intl*>(data)->us);
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        addV2(static_cast<const retro_core_options_v2*>(data));
        break;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        addV2(static_cast<const retro_core_options_v2_intl*>(data)->us);
        break;
    default:
        break;
    }
    LOGI("Core meldet %zu Optionen (Umgebungsbefehl %u)", count, cmd);
    // Werte der Optionen, die das VR-Menü (xr.dosgames, Reiter DOS) anbietet, ins Log.
    for (const char* key : {"dosbox_pure_cycles", "dosbox_pure_mouse_input", "dosbox_pure_mouse_speed_factor",
                            "dosbox_pure_aspect_correction", "dosbox_pure_svga"}) {
        optstore::Definition d;
        if (!mOptions.definition(key, d)) continue;
        std::string list;
        for (const auto& v : d.values) list += (list.empty() ? "" : "|") + v.first;
        LOGI("Core-Option %s: %s (Standard %s)", key, list.c_str(), d.defaultValue.c_str());
    }
}

// ==============================================================================
// Lebenszyklus
// ==============================================================================

bool LibretroAdapter::init(android_app* app) {
    mApp = app;
    gAdapter = this;

    // #16: Erst das Spiel suchen — seine Endung bestimmt den Core (Ordner =
    // DOSBox-Laufwerk C:). Ohne Spiel wird kein Core geladen, Testtextur bleibt.
    const std::string gamePath = findGameFile(app);
    if (gamePath.empty()) {
        LOGI("Kein Spiel in %s/game — Fallback-Testtextur bleibt aktiv",
             app->activity->externalDataPath);
        return false;
    }
    return start(gamePath);
}

// --- Ladeanzeige -----------------------------------------------------------------------------

void LibretroAdapter::setStage(LoadStage stage) {
    mStageTime[stage].store(steadySeconds());
    mLoadStage.store(stage);
}

bool LibretroAdapter::fail(loadst::Failure failure, const std::string& detail) {
    mStartFailure = failure;
    mStartDetail = detail;
    LOGE("Start fehlgeschlagen: %s", detail.c_str());
    return false;
}

std::string LibretroAdapter::lastCoreMessage() {
    std::lock_guard<std::mutex> lock(gCoreMessageMutex);
    return gCoreMessage;
}

std::string LibretroAdapter::coreSoFor(const std::string& gamePath) {
    const CoreSpec* core = nullptr;
    if (isDirectory(gamePath)) {
        core = folderCoreFor(gamePath);
        if (!core && !scummTargetIn(gamePath).empty()) core = &kScummCore;
    } else {
        core = coreForPath(gamePath);
    }
    return (core ? core : &kDosboxCore)->soName;
}

bool LibretroAdapter::coreAvailable(const std::string& soName) const {
    if (!mApp || soName.empty()) return false;
    if (AAsset* asset = AAssetManager_open(mApp->activity->assetManager, ("cores/" + soName).c_str(),
                                           AASSET_MODE_UNKNOWN)) {
        AAsset_close(asset);
        return true;
    }
    return ::access((std::string(mApp->activity->internalDataPath) + "/cores/" + soName).c_str(), R_OK) == 0;
}

bool LibretroAdapter::start(const std::string& gamePath) {
    android_app* app = mApp;
    mStartFailure = loadst::Failure::None;
    mStartDetail.clear();
    mCoreExtracted.store(false);
    for (auto& t : mStageTime) t.store(0.0);   // Etappen früherer Starts gelten nicht
    {
        std::lock_guard<std::mutex> lock(gCoreMessageMutex);
        gCoreMessage.clear();     // „letzte Core-Zeile“ gehört zu diesem Start
    }
    if (!app) return fail(loadst::Failure::CoreBroken, "Adapter ohne App (attach fehlt)");
    if (mCoreHandle) {
        setStage(kStageStop);
        saveUserOptions();         // Menü-Änderungen des alten Spiels nicht verlieren
        stopGame();                // Spielwechsel: alten Core sauber entladen
    }
    setStage(kStageData);
    gAdapter = this;               // Callbacks sind C-Funktionen ohne Userdata
    mGameData.clear();             // Reste eines fehlgeschlagenen Starts nicht weiterreichen
    mShutdownRequested.store(false);   // Beenden-Wunsch des vorigen Spiels gilt nicht weiter
    // Ordner: DOSBox-Laufwerk C:, außer unter games/doom|wolf3d|quake/ (xr.shooter) —
    // dort lädt der Core die Startdatei im Ordner. mGamePath bleibt der Ordner
    // (Spielstände, Tastenprofil, „zuletzt gespielt“), contentPath geht an den Core.
    std::string contentPath = gamePath;
    const CoreSpec* core = nullptr;
    if (isDirectory(gamePath)) {
        core = folderCoreFor(gamePath);
        const std::string scummTarget = core ? std::string() : scummTargetIn(gamePath);
        if (core) {
            contentPath = findStartFile(gamePath, core->startRule);
            if (contentPath.empty()) {
                LOGE("Keine Startdatei (%s) in %s", core->systemLabel, gamePath.c_str());
                return fail(loadst::Failure::StartFileMissing,
                            std::string("Keine Startdatei (") + core->systemLabel + ") in " + gamePath);
            }
        } else if (!scummTarget.empty()) {
            // xr.adventure: alter Spielordner unter game/ (aus der Zeit vor der Bibliothek).
            core = &kScummCore;
            contentPath = scummTarget;
        } else {
            core = &kDosboxCore;
        }
    } else {
        core = coreForPath(gamePath);   // Bibliotheksordner entscheidet (cue/chd/bin)
    }
    if (!core) core = &kDosboxCore;
    loadCoreOptions(gamePath);   // vor retro_set_environment — der Core fragt früh
    // xrshell_input = pad|mouse (xrshell.opt, wertet XRShell aus): DOSBox-Pure als Gamepad
    // (Gamepad-Mapper des Cores) statt Maus/Laser. Andere Cores haben einen festen Modus.
    if (const char* input = coreOption("xrshell_input")) {
        const bool padWanted = std::strcmp(input, "pad") == 0;
        if (!padWanted && std::strcmp(input, "mouse") != 0)
            LOGE("xrshell_input = %s unbekannt (pad|mouse)", input);
        else if (core == &kDosboxCore && padWanted)
            core = &kDosboxPadCore;
        else if (core != &kDosboxCore)
            LOGI("xrshell_input = %s: %s hat einen festen Eingabemodus", input, core->systemLabel);
    }
    mPadMode = core->padMode;
    mCoreFolder = core == &kDosboxPadCore ? "dospad" : core->folder ? core->folder : "";
    // DOSBox-Pure auch im Pad-Modus: Optionen-Reiter und Nutzerwerte gelten weiter; die
    // relative Maus (Faktor, Fangen) wirkt nur ohne Pad, weil dann keine Maus geliefert wird.
    mDosboxCore = core == &kDosboxCore || core == &kDosboxPadCore;
    mScummCore = core == &kScummCore;
    mRelMouse = mouserel::RelativeMouse{};
    mMemRegions.clear();          // kommt mit retro_load_game neu (SET_MEMORY_MAPS)
    ++mMemGeneration;
    mLearner = curlock::Learner{};
    mFollower.clear();
    mLearnRequest.store(false);   // Anforderung/Ergebnis des alten Spiels nicht übernehmen
    mLearnDone.store(false);
    mCursorState.store(kCursorOff);
    mPollW = mPollH = 0;
    mPollOptRevision = ~0u;       // Optionen des neuen Spiels beim ersten Poll lesen
    if (mScummCore) {
        // ScummVM: Zeiger beim Start unter den Laser holen, danach nur bei Größenwechsel.
        mRelMouse.catchOnReentry = false;
        mRelMouse.request();
    }
    // Vorgaben je Core (DOSBox-Maus „virtual“, ScummVM-Software-Renderer, Shooter-Optionen),
    // core_defaults.h — xrshell.opt im Spielordner überschreibt jeden Wert.
    for (const auto& option : coredefaults::forCore(core)) mOptions.addDefault(option.first, option.second);
    if (core == &kDosboxCore && !std::strcmp(coreOption("dosbox_pure_mouse_input"), "direct")) {
        // „direct“ bildet den Laser auf den ganzen Frame ab; einen Overscan-Rand bzw. den
        // „padded“-Rand rechnet DOSBox-Pure nicht heraus → Zeiger säße versetzt.
        const char* overscan = coreOption("dosbox_pure_overscan");
        const char* aspect = coreOption("dosbox_pure_aspect_correction");
        if ((overscan && std::strcmp(overscan, "0")) || (aspect && !std::strncmp(aspect, "padded", 6)))
            LOGE("Maus „direct“ mit Overscan/„padded“: Zeiger sitzt um den Rand versetzt");
        else
            LOGI("Maus „direct“: Spielzeiger absolut unter dem Laser");
    }
    if (core->provideDirs) {
        mSystemDir = std::string(app->activity->internalDataPath) + "/system";
        mSaveDir = std::string(app->activity->externalDataPath) + "/saves";
        if (!makeDir(mSystemDir)) mSystemDir.clear();
        if (!makeDir(mSaveDir)) mSaveDir.clear();
    }
    if (core == &kScummCore && !mSystemDir.empty()) {
        extractScummSystem(app);
        writeScummIni();   // vor retro_init: der Core liest die ini beim Start
    }
    LOGI("Spiel: %s → Core %s (%s)%s%s", gamePath.c_str(), core->soName,
         mPadMode ? "Gamepad" : "Maus", contentPath != gamePath ? ", Startdatei " : "",
         contentPath != gamePath ? contentPath.c_str() : "");

    setStage(kStageCore);
    if (!extractCoreFromAssets(app, core->soName)) {
        LOGI("Core %s nicht in den Assets — Fallback-Testtextur bleibt aktiv", core->soName);
        return fail(loadst::Failure::CoreMissing, std::string("Core ") + core->soName + " nicht im APK bzw. nicht entpackbar");
    }

    const std::string corePath =
        std::string(app->activity->internalDataPath) + "/cores/" + core->soName;

    // dlopen aus dem App-Privatverzeichnis: ab Android-31-Target dürfen Cores
    // nicht mehr direkt aus dem APK-Asset geladen werden (W^X) — deshalb Extract.
    // Ein früher geladener Core (Spielwechsel) bleibt geladen (stopGame) — dlopen liefert dann
    // dasselbe Abbild.
    mCoreHandle = dlopen(corePath.c_str(), RTLD_NOW);
    if (!mCoreHandle) {
        const char* why = dlerror();
        LOGE("dlopen(%s) fehlgeschlagen: %s", corePath.c_str(), why ? why : "?");
        return fail(loadst::Failure::CoreBroken, std::string("dlopen: ") + (why ? why : "?"));
    }

    // Kern-API auflösen. Ein fehlendes Pflicht-Symbol → Core unbrauchbar.
    auto* set_environment  = reinterpret_cast<set_environment_t>(dlsym(mCoreHandle, "retro_set_environment"));
    auto* set_video_refresh = reinterpret_cast<set_video_refresh_t>(dlsym(mCoreHandle, "retro_set_video_refresh"));
    auto* set_audio_sample = reinterpret_cast<set_audio_sample_t>(dlsym(mCoreHandle, "retro_set_audio_sample"));
    auto* set_audio_batch  = reinterpret_cast<set_audio_sample_batch_t>(dlsym(mCoreHandle, "retro_set_audio_sample_batch"));
    auto* set_input_poll   = reinterpret_cast<set_input_poll_t>(dlsym(mCoreHandle, "retro_set_input_poll"));
    auto* set_input_state  = reinterpret_cast<set_input_state_t>(dlsym(mCoreHandle, "retro_set_input_state"));
    auto* retro_init_fn    = reinterpret_cast<retro_init_t>(dlsym(mCoreHandle, "retro_init"));
    auto* retro_deinit_fn  = reinterpret_cast<retro_deinit_t>(dlsym(mCoreHandle, "retro_deinit"));
    auto* api_version_fn   = reinterpret_cast<retro_api_version_t>(dlsym(mCoreHandle, "retro_api_version"));
    auto* get_info_fn      = reinterpret_cast<get_system_info_t>(dlsym(mCoreHandle, "retro_get_system_info"));
    auto* get_av_fn        = reinterpret_cast<get_system_av_info_t>(dlsym(mCoreHandle, "retro_get_system_av_info"));
    auto* load_game_fn     = reinterpret_cast<load_game_t>(dlsym(mCoreHandle, "retro_load_game"));
    auto* retro_run_fn     = reinterpret_cast<retro_run_t>(dlsym(mCoreHandle, "retro_run"));

    if (!set_environment || !set_video_refresh || !set_audio_sample || !set_audio_batch ||
        !set_input_poll || !set_input_state || !retro_init_fn || !retro_deinit_fn ||
        !api_version_fn || !get_info_fn || !get_av_fn || !load_game_fn || !retro_run_fn) {
        LOGE("Core-API unvollständig — dlsym: %s", dlerror());
        dlclose(mCoreHandle);
        mCoreHandle = nullptr;
        return fail(loadst::Failure::CoreBroken, "Core-API unvollständig (Symbole fehlen)");
    }

    // Optional: Spielstände (Savestates) — ohne diese Symbole bleibt das Menü wirkungslos.
    mStatesChecked = false;                    // statesSupported() prüft nach dem ersten Frame neu
    mStatesSupported = true;
    mStatesFrameBase = mFrameCounter.load();
    mSerializeSize = reinterpret_cast<size_t (*)(void)>(dlsym(mCoreHandle, "retro_serialize_size"));
    mSerialize = reinterpret_cast<bool (*)(void*, size_t)>(dlsym(mCoreHandle, "retro_serialize"));
    mUnserialize = reinterpret_cast<bool (*)(const void*, size_t)>(dlsym(mCoreHandle, "retro_unserialize"));
    mSetPortDevice = reinterpret_cast<void (*)(unsigned, unsigned)>(dlsym(mCoreHandle, "retro_set_controller_port_device"));
    // Optional: Batteriespeicher (SRAM) — Pad-Cores sichern ihn nicht selbst.
    mGetMemoryData = reinterpret_cast<void* (*)(unsigned)>(dlsym(mCoreHandle, "retro_get_memory_data"));
    mGetMemorySize = reinterpret_cast<size_t (*)(unsigned)>(dlsym(mCoreHandle, "retro_get_memory_size"));

    if (api_version_fn() != RETRO_API_VERSION) {
        LOGE("Core-API-Version %u != erwartete %u", api_version_fn(), RETRO_API_VERSION);
        dlclose(mCoreHandle);
        mCoreHandle = nullptr;
        return fail(loadst::Failure::CoreBroken, "Core-API-Version passt nicht");
    }

    // Callbacks registrieren. set_environment MUSS vor retro_init erfolgen —
    // der Core ruft darüber u. a. RETRO_ENVIRONMENT_SET_PIXEL_FORMAT.
    set_environment(environmentCallback);
    set_video_refresh(videoRefreshCallback);
    set_audio_sample(audioSampleCallback);
    set_audio_batch(audioSampleBatchCallback);
    set_input_poll(inputPollCallback);
    set_input_state(inputStateCallback);

    struct retro_system_info sysInfo = {};
    get_info_fn(&sysInfo);
    LOGI("Core geladen: %s %s (need_fullpath=%d)",
         sysInfo.library_name ? sysInfo.library_name : "?",
         sysInfo.library_version ? sysInfo.library_version : "",
         sysInfo.need_fullpath ? 1 : 0);

    setStage(kStageGame);
    // need_fullpath=false: Core erwartet den Inhalt im Speicher.
    // Vor retro_init prüfen — retro_deinit nur nach retro_init (#12).
    if (!sysInfo.need_fullpath && !readGameData(contentPath)) {
        LOGE("Spieldatei nicht lesbar: %s", contentPath.c_str());
        dlclose(mCoreHandle);
        mCoreHandle = nullptr;
        return fail(loadst::Failure::DataUnreadable, "Spieldatei nicht lesbar: " + contentPath);
    }

    retro_init_fn();

    struct retro_game_info game = {};
    game.path = contentPath.c_str();   // auch bei data: Cores benennen Saves danach
    if (!mGameData.empty()) {
        game.data = mGameData.data();
        game.size = mGameData.size();
    }
    if (!load_game_fn(&game)) {
        LOGE("retro_load_game fehlgeschlagen: %s", contentPath.c_str());
        retro_deinit_fn();
        mCoreHandle = nullptr;   // nach retro_init nie dlclose (siehe stopGame)
        mGameData.clear();   // sonst bekäme der nächste Start die Daten dieses Spiels
        const std::string coreLine = lastCoreMessage();
        return fail(loadst::Failure::LoadFailed, coreLine.empty() ? "retro_load_game: " + contentPath : coreLine);
    }
    LOGI("retro_load_game ok: %s", contentPath.c_str());
    // Eingabegerät an Port 0 wie RetroArch nach dem Laden setzen (TyrQuake führt dabei
    // Konsolenbefehle aus — erst nach retro_load_game gültig).
    if (core->padDevice) {
        using set_port_device_t = void (*)(unsigned, unsigned);
        if (auto* setPortDevice = reinterpret_cast<set_port_device_t>(
                dlsym(mCoreHandle, "retro_set_controller_port_device"))) {
            setPortDevice(0, core->padDevice);
            mPadDevice = core->padDevice;
            LOGI("Eingabegerät Port 0: 0x%x", core->padDevice);
        }
    }
    mCoreLoaded = true;
    mGamePath = gamePath;
    mGameRunning.store(true);
    prepareSaves();
    mSaveRamFailing = false;
    beginSaveRam();   // Core-Thread läuft noch nicht → ohne mRunMutex

    // AV-Info ist laut libretro-API erst nach retro_load_game gültig (#12).
    struct retro_system_av_info avInfo = {};
    get_av_fn(&avInfo);
    LOGI("Core-AV: %ux%u (max %ux%u) @ %.2f fps, %.0f Hz Audio",
         avInfo.geometry.base_width, avInfo.geometry.base_height,
         avInfo.geometry.max_width, avInfo.geometry.max_height,
         static_cast<double>(avInfo.timing.fps),
         static_cast<double>(avInfo.timing.sample_rate));
    if (avInfo.geometry.max_width > kTextureW || avInfo.geometry.max_height > kTextureH) {
        // Kein Abbruch: größere Frames verwirft der Video-Callback mit Log.
        LOGE("Core-Maximalgeometrie größer als %dx%d-Textur — große Modi fehlen",
             kTextureW, kTextureH);
    }
    // Fixpoint-Takt des Run-Loops: der Core meldet echte Werte (DOSBox-Pure
    // typisch 60 Hz NTSC/59.94 bzw. 70 Hz DOS-Textmodus).
    if (avInfo.timing.fps > 1.0) mTimingFps = clampCoreFps(avInfo.timing.fps);
    // Spielgeometrie + Seitenverhältnis (Pointer-Fallback, Screen-Proportion).
    setGeometry(avInfo.geometry);

    // Audio nach erfolgreichem Laden (Samplerate kommt aus retro_system_av_info).
    // Startet nicht → Core läuft trotzdem, nur stumm.
    mAudio.start(avInfo.timing.sample_rate);

    // Run-Loop auf dem Core-Thread: Fixpoint-Timing auf dem vom Core gemeldeten
    // fps-Wert. Tick-Messung VOR retro_run, danach bis zum nächsten Takt
    // schlafen — kein Busy-Waiting, kein Zeitdrift.
    mCoreThread = std::thread([this, retro_run_fn] {
        using clock = std::chrono::steady_clock;
        const auto epoch = clock::now();
        auto seconds = [&] { return std::chrono::duration<double>(clock::now() - epoch).count(); };
        // Feste Fristen (frame_pacer.h): die Aufwachverzögerung summiert sich nicht mehr auf
        // (Headset 0.3.0/0.3.1: 71,6 statt 72 fps); nach einer Pause setzt der Takt neu auf.
        pacing::FramePacer pacer;
        int sramFrames = 0;
        while (true) {
            {
                // Pause (VR-Menü, Fokus weg, Brille ab): schlafen, bis fortgesetzt oder beendet wird.
                std::unique_lock<std::mutex> lock(mMutex);
                if (mPaused) pacer.reset();
                mCv.wait(lock, [this] { return mStopRequested || !mPaused; });
                if (mStopRequested) break;
            }
            {
                std::lock_guard<std::mutex> run(mRunMutex);   // Savestates warten darauf
                retro_run_fn();
                // SRAM etwa alle 2 s vergleichen (Vergleich ~8–128 KB, Schreiben nur bei Änderung):
                // auch ein harter Prozess-Abbruch verliert höchstens die letzten Sekunden.
                if (++sramFrames >= 120) {
                    sramFrames = 0;
                    flushSaveRamLocked();
                }
            }
            // Takt nach retro_run lesen: ein SET_SYSTEM_AV_INFO darin gilt ab diesem Frame.
            const double deadline = pacer.next(seconds(), 1.0 / mTimingFps.load());
            const auto until = epoch + std::chrono::duration_cast<clock::duration>(
                                           std::chrono::duration<double>(deadline));
            std::unique_lock<std::mutex> lock(mMutex);
            const bool stopped = mCv.wait_until(lock, until, [this] { return mStopRequested; });
            if (stopped) break;
        }
    });
    LOGI("Core-Thread gestartet — %s", gamePath.c_str());
    setStage(kStageReady);
    return true;
}

bool LibretroAdapter::updateTexture(GLuint texture) {
    const uint64_t count = mFrameCounter.load();
    if (count == mLastUploaded) return false;
    int w, h;
    uint32_t front;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        front = mFront;
        w = mFrameW;
        h = mFrameH;
    }
    if (w <= 0 || h <= 0) return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                    mPixels[front].data());
    mLastUploaded = count;
    mUploadedW = w;
    mUploadedH = h;
    // Ladeanzeige: Inhalt = irgendein Pixel heller als fast schwarz (Raster 64 × 48, Bytes B,G,R,X).
    const auto* px = reinterpret_cast<const uint32_t*>(mPixels[front].data());
    mLastContent = false;
    for (int gy = 0; gy < 48 && !mLastContent; ++gy) {
        const int y = (2 * gy + 1) * h / 96;
        for (int gx = 0; gx < 64; ++gx) {
            const uint32_t p = px[static_cast<size_t>(y) * w + (2 * gx + 1) * w / 128];
            if ((p & 0xFF) > 40 || ((p >> 8) & 0xFF) > 40 || ((p >> 16) & 0xFF) > 40) { mLastContent = true; break; }
        }
    }
    return true;
}

void LibretroAdapter::stopGame() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStopRequested = true;
    }
    mCv.notify_all();
    if (mCoreThread.joinable()) mCoreThread.join();

    // Erst Core-Thread stoppen (kein Producer mehr), dann den Audio-Stream.
    mAudio.stop();

    // Batteriespeicher vor retro_unload_game sichern (danach ist der Zeiger ungültig).
    if (mCoreLoaded) flushSaveRam();
    mSaveRam.reset();
    mGetMemoryData = nullptr;
    mGetMemorySize = nullptr;

    if (mCoreHandle) {
        auto* unload = reinterpret_cast<unload_game_t>(dlsym(mCoreHandle, "retro_unload_game"));
        auto* deinit = reinterpret_cast<retro_deinit_t>(dlsym(mCoreHandle, "retro_deinit"));
        if (mGameRunning.load() && unload) unload();
        if (deinit) deinit();
        // Kein dlclose: DOSBox-Pure beendet seinen losgelösten Emulationsthread nach retro_deinit
        // erst kurz danach — ein dlclose sofort entfernt den Code, den dieser Thread noch ausführt
        // (Absturz beim Spielwechsel/Beenden). Warten wäre geraten (wie lange?); den Core geladen zu
        // lassen ist dagegen sicher und von libretro ausdrücklich vorgesehen (retro_deinit: „may be
        // retained in memory by the frontend“, Globals setzt der Core dort zurück). Ein späteres
        // dlopen desselben Pfads liefert dasselbe Abbild, retro_init läuft dann erneut. Kosten: das
        // Abbild bleibt bis Prozessende gemappt (einige MB je benutztem Core).
        mCoreHandle = nullptr;
    }
    mGameData.clear();   // erst nach retro_unload_game freigeben
    mCoreLoaded = false;
    mGameRunning.store(false);

    // Zustand für das nächste Spiel (Spielwechsel) zurücksetzen.
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStopRequested = false;
        mPaused = mPauseReasons != 0;   // Gründe (Menü, Fokus) gelten auch fürs nächste Spiel
        mFrameW = mFrameH = 0;
    }
    mSerializeSize = nullptr;
    mSerialize = nullptr;
    mUnserialize = nullptr;
    mPixelFormat.store(RETRO_PIXEL_FORMAT_0RGB1555);
    mTimingFps = 60.0;
    for (auto& r : mRumble) r.store(0);
    mPadMode = false;
    mDosboxCore = false;
    mMemRegions.clear();          // Zeiger in den Speicher des entladenen Cores
    ++mMemGeneration;
    mLearner = curlock::Learner{};
    mFollower.clear();
    mLearnRequest.store(false);
    mLearnDone.store(false);
    mCursorState.store(kCursorOff);
    mScummCore = false;
    mPollW = mPollH = 0;
    mCoreFolder.clear();
    mSetPortDevice = nullptr;
    mPadDevice = RETRO_DEVICE_JOYPAD;
    mSystemDir.clear();
    mSaveDir.clear();
    mGamePath.clear();
    mPad[0].store(0);
    mPad[1].store(0);
    for (auto& port : mPadAnalog)
        for (auto& axis : port) axis.store(0);
    for (auto& key : mKeys) key.store(false);
    mKeyboardCallback = nullptr;
    {
        std::lock_guard<std::mutex> lock(mKeyEventMutex);
        mKeyEvents.clear();
    }
    mAnalyze.store(false);
    {
        std::lock_guard<std::mutex> lock(mKeyMutex);
        mKey = EnvironmentKey{};
    }
}

void LibretroAdapter::shutdown() {
    saveUserOptions();
    stopGame();
    if (gAdapter == this) gAdapter = nullptr;
}

// ==============================================================================
// Bibliothek: Spiele unter files/games/<system>/
// ==============================================================================

std::vector<LibretroAdapter::GameEntry> LibretroAdapter::scanLibrary() const {
    std::vector<GameEntry> games;
    if (!mApp) return games;
    const std::string root = std::string(mApp->activity->externalDataPath) + "/games";
    makeDir(std::string(mApp->activity->externalDataPath));
    makeSharedDir(root);
    for (const CoreSpec* core : kCores) {
        if (!core->folder) continue;
        // Nur Systeme, deren Core in dieser App steckt (xr.dosgames: DOS, xr.shooter: Shooter).
        if (AAsset* asset = AAssetManager_open(mApp->activity->assetManager,
                                               (std::string("cores/") + core->soName).c_str(),
                                               AASSET_MODE_UNKNOWN))
            AAsset_close(asset);
        else
            continue;
        const std::string dirPath = root + "/" + core->folder;
        makeSharedDir(dirPath);
        if (core->folderGames) {
            // xr.dosgames: jeder Unterordner ist ein Spiel (Titel aus xrshell.title).
            for (const FolderGame& game : scanGameFolders(dirPath))
                games.push_back({game.path, game.title, core->systemLabel});
            // xr.adventure: Spielordner aus der Zeit vor der Bibliothek (files/game/<Ordner>/ mit
            // *.scummvm) erscheinen weiter — nach dem Update muss nichts verschoben werden.
            if (core == &kScummCore)
                for (const FolderGame& game : scanGameFolders(std::string(mApp->activity->externalDataPath) + "/game"))
                    if (!scummTargetIn(game.path).empty()) games.push_back({game.path, game.title, core->systemLabel});
            continue;
        }
        DIR* dir = opendir(dirPath.c_str());
        if (!dir) continue;
        while (dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            const std::string path = dirPath + "/" + entry->d_name;
            if (isDirectory(path) || !hasExtension(core, entry->d_name)) continue;
            std::string title = entry->d_name;
            const size_t dot = title.rfind('.');
            if (dot != std::string::npos && dot > 0) title = title.substr(0, dot);
            games.push_back({path, title, core->systemLabel});
        }
        closedir(dir);
    }
    std::sort(games.begin(), games.end(), [](const GameEntry& a, const GameEntry& b) {
        return a.system != b.system ? a.system < b.system : a.title < b.title;
    });
    return games;
}
