// Host-Test für src/core_defaults.h: Standard-Kernoptionen je Core (die der Adapter setzt und der
// Geräte-Test tests/device/core_smoke.cpp prüft). Sweep über alle Cores der Tabelle:
// Schlüssel gehören zum Core (Präfix), keine doppelten Schlüssel, keine leeren Werte, keine
// „xrshell_“-Schlüssel; dazu die Werte, an denen Verhalten hängt (Totzone 0, Textur 1024×768).
//
//   clang++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_core_defaults.cpp -o t && ./t

#include "core_defaults.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace core_select;

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FEHLER: %s\n", what.c_str());
    }
}

const char* valueOf(const CoreSpec* core, const char* key) {
    for (const auto& o : coredefaults::forCore(core))
        if (!std::strcmp(o.first, key)) return o.second;
    return nullptr;
}

bool is(const CoreSpec* core, const char* key, const char* value) {
    const char* v = valueOf(core, key);
    return v && !std::strcmp(v, value);
}

} // namespace

int main() {
    const CoreSpec* all[] = {&kDosboxCore, &kDosboxPadCore, &kScummCore, &kDoomCore, &kWolfCore, &kQuakeCore};
    struct Prefix { const CoreSpec* core; const char* prefix; };
    const Prefix prefixes[] = {{&kDosboxCore, "dosbox_pure_"}, {&kDosboxPadCore, "dosbox_pure_"},
                               {&kScummCore, "scummvm_"},      {&kDoomCore, "prboom-"},
                               {&kWolfCore, "ecwolf-"},        {&kQuakeCore, "tyrquake_"}};
    for (const CoreSpec* core : all) {
        const auto opts = coredefaults::forCore(core);
        const char* prefix = nullptr;
        for (const Prefix& p : prefixes)
            if (p.core == core) prefix = p.prefix;
        check(prefix ? !opts.empty() : opts.empty(), std::string(core->soName) + ": Vorgaben nur für bekannte Cores");
        std::set<std::string> seen;
        for (const auto& o : opts) {
            const std::string key = o.first, what = std::string(core->soName) + " " + key;
            check(o.second && *o.second, what + ": Wert nicht leer");
            check(seen.insert(key).second, what + ": Schlüssel doppelt");
            check(key.compare(0, 8, "xrshell_") != 0, what + ": xrshell_-Schlüssel gehen nie an den Core");
            check(prefix && key.compare(0, std::strlen(prefix), prefix) == 0, what + ": Präfix des Cores");
        }
    }
    // Verhalten, an dem XRShell hängt.
    check(is(&kDosboxCore, "dosbox_pure_mouse_input", "virtual"), "DOSBox: Maus virtual");
    check(is(&kDosboxPadCore, "dosbox_pure_mouse_input", "virtual"), "DOSBox-Pad: Maus virtual");
    check(is(&kScummCore, "scummvm_video_hw_acceleration", "disabled"), "ScummVM: Software-Renderer");
    check(is(&kDoomCore, "prboom-analog_deadzone", "0"), "PrBoom: Totzone macht XRShell");
    check(is(&kWolfCore, "ecwolf-analog-deadzone", "0%"), "ECWolf: Totzone macht XRShell");
    check(is(&kQuakeCore, "tyrquake_analog_deadzone", "0"), "TyrQuake: Totzone macht XRShell");
    check(is(&kDoomCore, "prboom-color_format", "24bits (truecolor)"), "PrBoom: 24 Bit");
    check(is(&kDoomCore, "prboom-sound_samplerate", "48000") && is(&kQuakeCore, "tyrquake_sound_samplerate", "48000"),
          "Ton direkt in 48 kHz (AAudio-Rate der Quest)");
    check(is(&kDoomCore, "prboom-rumble", "enabled") && is(&kQuakeCore, "tyrquake_rumble", "enabled"), "Rumble an");
    // Auflösungen passen in die 1024×768-Textur (QuadRenderer::kTextureW/H).
    for (const char* key : {"prboom-resolution", "ecwolf-resolution", "tyrquake_resolution"}) {
        const CoreSpec* core = key[0] == 'p' ? &kDoomCore : key[0] == 'e' ? &kWolfCore : &kQuakeCore;
        const char* v = valueOf(core, key);
        check(v != nullptr, std::string(key) + " gesetzt");
        if (!v) continue;
        char* end = nullptr;
        const long w = std::strtol(v, &end, 10);
        const long h = end && *end == 'x' ? std::strtol(end + 1, nullptr, 10) : 0;
        check(w > 0 && w <= 1024 && h > 0 && h <= 768, std::string(key) + " passt in 1024x768: " + v);
    }
    std::printf("test_core_defaults: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
