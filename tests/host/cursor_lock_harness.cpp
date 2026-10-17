// Host-Testrahmen für „Zeiger lernen“ (src/cursor_lock.h) gegen ein echtes DOS-Spiel: lädt den
// DOSBox-Pure-Core als Windows-DLL (libretro-Buildbot, nicht im Repo), startet einen Spielordner
// ohne Fenster und Ton und arbeitet ein Skript ab. Pro Poll wie XRShell: input_poll → Lernen bzw.
// Folgen liefert die Maus-Deltas. Screenshots als PNG mit Fadenkreuz an der Laserposition —
// Pfeilspitze auf dem Kreuz = gekoppelt.
//
//   Bauen (llvm-mingw, Windows):
//     clang++ -std=c++17 -O2 -Wall -Wextra -Isrc -isystem <stb-src> tests/host/cursor_lock_harness.cpp \
//             stb_image_write_impl.o -o cursor_lock_harness.exe
//   Aufruf:
//     cursor_lock_harness <core.dll> <spielordner> <skript> <ausgabeordner>
//   Skript (eine Anweisung pro Zeile, # = Kommentar):
//     run N            N Frames, Laser aus (Maus steht)
//     aim U V N        Laser auf U/V (0..1 des Bildes), N Frames folgen bzw. relativ bewegen
//     sweep U0 V0 U1 V1 N   Laser in N Frames von A nach B
//     click [N]        linke Maustaste N Frames (Standard 8, am aktuellen Laserpunkt)
//     rclick           rechte Maustaste 4 Frames
//     key NAME         Taste 4 Frames (enter, esc, space, …)
//     learn            Zeiger lernen (bis fertig)
//     lock CODE        gespeicherte Kopplung(en) setzen (wie beim Spielstart)
//     shot NAME        Bild als <ausgabe>/NAME.png (Fadenkreuz am Laser)
//     state            Kopplungszustand und Spielwerte ausgeben
// Nur für die Entwicklung; Spieldaten bleiben im Spielordner des Nutzers (BYOA).

#define private public   // Diagnose: Reglerzustand ausgeben
#include "cursor_lock.h"
#undef private
#include "libretro.h"
#include "mouse_relative.h"

#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern "C" int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

namespace {

int gFormat = RETRO_PIXEL_FORMAT_0RGB1555;
unsigned gW = 0, gH = 0;
std::vector<uint32_t> gFrame;
std::vector<curlock::Region> gRegions;
std::string gSystemDir, gSaveDir;
std::map<std::string, std::string> gVars = {
    { "dosbox_pure_mouse_input", "virtual" },
    { "dosbox_pure_sblaster_type", "sb16" },
    { "dosbox_pure_sblaster_conf", "A220 I7 D1 H5" },
    { "dosbox_pure_cycles", "max" },
};

// Eingabe des aktuellen Polls
int gDx = 0, gDy = 0;
bool gLeft = false, gRight = false;
int gKey = -1;
long gFrameNo = 0;
retro_keyboard_event_t gKeyboard = nullptr;   // DOSBox-Pure: Tasten per Callback (SET_KEYBOARD_CALLBACK)

uint32_t toXrgb(uint16_t p) {
    if (gFormat == RETRO_PIXEL_FORMAT_RGB565) {
        const uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        return (r << 19) | (g << 10) | (b << 3);
    }
    const uint32_t r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
    return (r << 19) | (g << 11) | (b << 3);
}

void logPrintf(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_WARN && !std::strstr(fmt, "STATUS")) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::printf("[core %ld] %s", gFrameNo, buf);
    if (!*buf || buf[std::strlen(buf) - 1] != '\n') std::printf("\n");
}

bool environment(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        gFormat = *static_cast<const retro_pixel_format*>(data);
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = logPrintf;
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
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = static_cast<retro_variable*>(data);
        auto it = gVars.find(v->key);
        if (it == gVars.end()) return false;
        v->value = it->second.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        gKeyboard = static_cast<const retro_keyboard_callback*>(data)->callback;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        const auto* map = static_cast<const retro_memory_map*>(data);
        gRegions.clear();
        size_t total = 0;
        for (unsigned i = 0; i < map->num_descriptors; ++i) {
            const retro_memory_descriptor& d = map->descriptors[i];
            if (!d.ptr || !d.len || d.select) continue;
            gRegions.push_back({ static_cast<const uint8_t*>(d.ptr) + d.offset, static_cast<uint32_t>(d.start),
                                 static_cast<uint32_t>(d.len) });
            total += d.len;
        }
        std::printf("[frame %ld] Speicherkarte: %zu Bereiche, %zu KB\n", gFrameNo, gRegions.size(), total / 1024);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
        return true;
    default:
        return false;
    }
}

void video(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;
    gW = w;
    gH = h;
    gFrame.resize(static_cast<size_t>(w) * h);
    for (unsigned y = 0; y < h; ++y) {
        const uint8_t* row = static_cast<const uint8_t*>(data) + y * pitch;
        for (unsigned x = 0; x < w; ++x)
            gFrame[y * w + x] = gFormat == RETRO_PIXEL_FORMAT_XRGB8888 ? reinterpret_cast<const uint32_t*>(row)[x]
                                                                     : toXrgb(reinterpret_cast<const uint16_t*>(row)[x]);
    }
}
void audioSample(int16_t, int16_t) {}
size_t audioBatch(const int16_t*, size_t frames) { return frames; }

// --- XRShell-Nachbau: Laser → Deltas ---------------------------------------------------------
float gU = 0.5f, gV = 0.5f;
bool gValid = false;
mouserel::RelativeMouse gRel;
curlock::Learner gLearner;
curlock::Follower gFollower;
std::string gLockState = "aus";

void inputPoll() {
    const float w = gW ? static_cast<float>(gW) : 320.0f, h = gH ? static_cast<float>(gH) : 200.0f;
    int dx = 0, dy = 0;
    gRel.poll(gU * w, gV * h, gValid, false, dx, dy);   // relative Maus wie XRShell (ohne Fangen)
    int ldx = 0, ldy = 0;
    if (gLearner.running()) {
        gLearner.poll(gRegions, ldx, ldy);
        dx = ldx;
        dy = ldy;
        if (!gLearner.running()) {
            const bool ok = gLearner.state() == curlock::Learner::State::Ok;
            std::printf("[frame %ld] Lernen %s: %s\n", gFrameNo, ok ? "fertig" : "fehlgeschlagen", gLearner.message().c_str());
            if (ok) {
                const auto all = curlock::addLock(gFollower.locks(), gLearner.lock());
                gFollower.setAll(all, 0);
                std::printf("           Kopplungen: %s\n", curlock::encodeList(all).c_str());
                gLockState = "gekoppelt";
            }
        }
    } else if (gFollower.active()) {
        gFollower.poll(gRegions, gU, gV, gValid, ldx, ldy);
        if (gFollower.lost()) {
            std::printf("[frame %ld] Kopplung pausiert\n", gFrameNo);
            gLockState = "pausiert";
        } else {
            dx = ldx;
            dy = ldy;
        }
    } else if (gFollower.lost()) {
        int pdx = 0, pdy = 0;
        const bool resumed = gFollower.observe(gRegions, gU, gV, gValid, dx, dy, pdx, pdy);
        if (pdx || pdy) { dx = pdx; dy = pdy; }
        if (resumed) {
            std::printf("[frame %ld] Kopplung aktiv: X %X Y %X\n", gFrameNo, gFollower.lock().x.addr, gFollower.lock().y.addr);
            gLockState = "gekoppelt";
        }
    }
    gDx = dx;
    gDy = dy;
}

int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    static std::map<unsigned long long, bool> seen;   // jede Abfrage einmal protokollieren (Diagnose)
    const unsigned long long k = (static_cast<unsigned long long>(port) << 48) | (static_cast<unsigned long long>(device) << 32) |
                                 (static_cast<unsigned long long>(index) << 16) | id;
    if (std::getenv("HARNESS_INPUTLOG") && !seen[k]) { seen[k] = true; std::printf("[abfrage] port %u dev %u idx %u id %u\n", port, device, index, id); }
    if (port != 0) return 0;
    if (device == RETRO_DEVICE_MOUSE) {
        switch (id) {
        case RETRO_DEVICE_ID_MOUSE_X: return static_cast<int16_t>(gDx);
        case RETRO_DEVICE_ID_MOUSE_Y: return static_cast<int16_t>(gDy);
        case RETRO_DEVICE_ID_MOUSE_LEFT: return gLeft && !gLearner.running() ? 1 : 0;
        case RETRO_DEVICE_ID_MOUSE_RIGHT: return gRight && !gLearner.running() ? 1 : 0;
        default: return 0;
        }
    }
    if (device == RETRO_DEVICE_KEYBOARD) return gKey >= 0 && static_cast<int>(id) == gKey ? 1 : 0;
    return 0;
}

void shot(const std::string& path) {
    if (gFrame.empty()) return;
    std::vector<uint8_t> rgb(static_cast<size_t>(gW) * gH * 3);
    for (size_t i = 0; i < gFrame.size(); ++i) {
        rgb[i * 3] = static_cast<uint8_t>(gFrame[i] >> 16);
        rgb[i * 3 + 1] = static_cast<uint8_t>(gFrame[i] >> 8);
        rgb[i * 3 + 2] = static_cast<uint8_t>(gFrame[i]);
    }
    if (gValid) {   // Fadenkreuz am Laser (Magenta), Mitte frei
        const int cx = static_cast<int>(gU * gW), cy = static_cast<int>(gV * gH);
        for (int d = -6; d <= 6; ++d) {
            if (std::abs(d) < 2) continue;
            for (auto [x, y] : { std::pair<int, int>{ cx + d, cy }, { cx, cy + d } }) {
                if (x < 0 || y < 0 || x >= static_cast<int>(gW) || y >= static_cast<int>(gH)) continue;
                uint8_t* p = &rgb[(static_cast<size_t>(y) * gW + x) * 3];
                p[0] = 255; p[1] = 0; p[2] = 255;
            }
        }
    }
    stbi_write_png(path.c_str(), static_cast<int>(gW), static_cast<int>(gH), 3, rgb.data(), static_cast<int>(gW) * 3);
}

int keyCode(const std::string& name) {
    static const std::map<std::string, int> keys = { { "enter", RETROK_RETURN }, { "esc", RETROK_ESCAPE },
        { "space", RETROK_SPACE }, { "tab", RETROK_TAB }, { "up", RETROK_UP }, { "down", RETROK_DOWN },
        { "left", RETROK_LEFT }, { "right", RETROK_RIGHT }, { "y", RETROK_y }, { "n", RETROK_n } };
    auto it = keys.find(name);
    return it == keys.end() ? -1 : it->second;
}

template <typename T> T sym(HMODULE h, const char* name) { return reinterpret_cast<T>(GetProcAddress(h, name)); }

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "Aufruf: %s <core.dll> <spielordner> <skript> <ausgabe>\n", argv[0]);
        return 2;
    }
    const std::string out = argv[4];
    CreateDirectoryA(out.c_str(), nullptr);
    gSystemDir = out + "\\system";
    gSaveDir = out + "\\saves";
    CreateDirectoryA(gSystemDir.c_str(), nullptr);
    CreateDirectoryA(gSaveDir.c_str(), nullptr);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { std::fprintf(stderr, "Core nicht ladbar: %s\n", argv[1]); return 1; }
    sym<void (*)(retro_environment_t)>(h, "retro_set_environment")(environment);
    sym<void (*)(retro_video_refresh_t)>(h, "retro_set_video_refresh")(video);
    sym<void (*)(retro_audio_sample_t)>(h, "retro_set_audio_sample")(audioSample);
    sym<void (*)(retro_audio_sample_batch_t)>(h, "retro_set_audio_sample_batch")(audioBatch);
    sym<void (*)(retro_input_poll_t)>(h, "retro_set_input_poll")(inputPoll);
    sym<void (*)(retro_input_state_t)>(h, "retro_set_input_state")(inputState);
    sym<void (*)()>(h, "retro_init")();
    auto setPort = sym<void (*)(unsigned, unsigned)>(h, "retro_set_controller_port_device");
    retro_game_info info{};
    info.path = argv[2];
    if (!sym<bool (*)(const retro_game_info*)>(h, "retro_load_game")(&info)) {
        std::fprintf(stderr, "retro_load_game fehlgeschlagen\n");
        return 1;
    }
    (void)setPort;   // wie XRShell: Port-Gerät nicht setzen (Standardbelegung inkl. Maustasten)
    auto run = sym<void (*)()>(h, "retro_run");
    auto frames = [&](int n) { for (int i = 0; i < n; ++i, ++gFrameNo) run(); };

    std::ifstream script(argv[3]);
    std::string line;
    while (std::getline(script, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        if (cmd == "run") { int n = 0; in >> n; gValid = false; frames(n); }
        else if (cmd == "aim") { int n = 0; in >> gU >> gV >> n; gValid = true; frames(n); }
        else if (cmd == "sweep") {
            float u0, v0, u1, v1; int n = 0;
            in >> u0 >> v0 >> u1 >> v1 >> n;
            gValid = true;
            for (int i = 0; i < n; ++i, ++gFrameNo) {
                const float t = n > 1 ? static_cast<float>(i) / (n - 1) : 1.0f;
                gU = u0 + (u1 - u0) * t;
                gV = v0 + (v1 - v0) * t;
                run();
            }
        }
        else if (cmd == "click") { int n = 8; in >> n; gLeft = true; frames(n); gLeft = false; frames(4); }
        else if (cmd == "rclick") { gRight = true; frames(4); gRight = false; frames(2); }
        else if (cmd == "key") {
            std::string k; int n = 8; in >> k >> n;
            gKey = keyCode(k);
            if (gKeyboard && gKey >= 0) gKeyboard(true, static_cast<unsigned>(gKey), 0, 0);
            frames(n);
            if (gKeyboard && gKey >= 0) gKeyboard(false, static_cast<unsigned>(gKey), 0, 0);
            gKey = -1;
            frames(4);
        }
        else if (cmd == "learn") {
            gLearner.start(static_cast<int>(gW ? gW : 320), static_cast<int>(gH ? gH : 200));
            for (int i = 0; i < 400 && gLearner.running(); ++i, ++gFrameNo) run();
        }
        else if (cmd == "lock") {
            std::string code; in >> code;
            gFollower.setAll(curlock::decodeList(code));
            gLockState = gFollower.empty() ? "aus" : "pausiert";
        }
        else if (cmd == "shot") { std::string n; in >> n; shot(out + "\\" + n + ".png"); std::printf("[frame %ld] Bild %s (%ux%u, %s)\n", gFrameNo, n.c_str(), gW, gH, gLockState.c_str()); }
        else if (cmd == "trace") {   // trace N K: N Frames, alle K Frames Reglerzustand
            int n = 0, k = 5; in >> n >> k;
            for (int i = 0; i < n; ++i, ++gFrameNo) {
                run();
                if (i % k) continue;
                int x = -1, y = -1;
                if (!gFollower.empty()) { curlock::read16(gRegions, gFollower.lock().x.addr, x); curlock::read16(gRegions, gFollower.lock().y.addr, y); }
                std::printf("  [%ld] Spiel %d/%d Ziel %.0f/%.0f aktiv %d pausiert %d unterwegs %d/%d alter %d/%d gesendet %d/%d\n", gFrameNo, x, y,
                            gU * gW, gV * gH, gFollower.mActive, gFollower.mLost, gFollower.mPendX, gFollower.mPendY,
                            gFollower.mAge, gFollower.mRetries, gDx, gDy);
            }
        }
        else if (cmd == "state") {
            std::printf("[frame %ld] Zustand %s, Bild %ux%u", gFrameNo, gLockState.c_str(), gW, gH);
            if (!gFollower.empty()) {
                int x = -1, y = -1;
                curlock::read16(gRegions, gFollower.lock().x.addr, x);
                curlock::read16(gRegions, gFollower.lock().y.addr, y);
                std::printf(", Spielzeiger %d/%d, Laser %.0f/%.0f", x, y, gU * gW, gV * gH);
            }
            std::printf("\n");
        }
        else std::printf("Unbekannt: %s\n", line.c_str());
    }
    sym<void (*)()>(h, "retro_unload_game")();
    sym<void (*)()>(h, "retro_deinit")();
    return 0;
}
