// XRShell — Standard-Kernoptionen je Core (abhängigkeitsfrei bis auf core_select.h).
//
// Was der Adapter beim Spielstart als Vorgabe setzt (OptionStore::addDefault: gilt nur, wenn
// xrshell.opt des Spielordners bzw. der Nutzer nichts sagt). Gemeinsam genutzt von
// libretro_adapter.cpp und dem Geräte-Test tests/device/core_smoke.cpp, damit der Test genau
// die Werte der App prüft.

#pragma once

#include "core_select.h"

#include <utility>
#include <vector>

namespace coredefaults {

using Option = std::pair<const char*, const char*>;

inline std::vector<Option> forCore(const core_select::CoreSpec* core) {
    using namespace core_select;
    // DOSBox-Pure-Standard auf Android ist „pad“ (Touchpad): relative Mausbewegung wird dort
    // ignoriert. XRShell liefert Maus-Deltas → „virtual“, sofern xrshell.opt nichts anderes sagt
    // (Windows 3.x: „direct“ + VMware-Maustreiber).
    // cycle_limit: bei „max“ nutzt DOSBox-Pure standardmäßig 100 % der Bildzeit (70 Hz = 14,3 ms) —
    // Core-Smoke-Test auf der Quest: retro_run Mittel 12,4 ms, p95 16 ms > 72-Hz-Budget 13,9 ms.
    // 85 % hält den Takt; Spiele mit festen Zyklen betrifft das nicht.
    if (core == &kDosboxCore || core == &kDosboxPadCore)
        return {{"dosbox_pure_mouse_input", "virtual"}, {"dosbox_pure_cycle_limit", "0.85"}};
    // #28: kein SET_HW_RENDER in XRShell → Software-Renderer; Launcher/Menüs in 640×480 (4:3),
    // damit sie in die 1024×768-Textur passen (HD = verworfen).
    if (core == &kScummCore)
        return {{"scummvm_video_hw_acceleration", "disabled"}, {"scummvm_gui_aspect_ratio", "0"},
                {"scummvm_gui_h_res", "480"},                  {"scummvm_browsing_mode", "local"},
                {"scummvm_pointer_device", "mouse"},           {"scummvm_mouse_speed", "1.0"}};
    // xr.shooter: eine Totzone (XRShell, 0,15 mit Neuskalierung) statt doppelt; Auflösung
    // 960×600 (3× Doom/Wolf, passt in die 1024×768-Textur, etwa so fein wie ein Bildschirm
    // von ~50° im Quest-3-Display); Bildrate 72 = Quest-Rate, wo der Core sie wählen lässt
    // (PrBoom: uncapped_framerate in <IWAD>.prboom.cfg; ECWolf bleibt bei 70 = feste Tic-Rate,
    // 72 brächte nur doppelte Bilder wie 70 auf 72 Hz). PrBoom in Echtfarben (24 Bit, ohne
    // Banding der 16-Bit-Lichtrampe), Füllen auf 2 Threads, Audio direkt in 48 kHz (AAudio-
    // Rate, kein Resampling; TyrQuake ebenso), Rumble an (Vibration rechts bei Schuss/Aufsammeln).
    if (core == &kDoomCore)
        return {{"prboom-analog_deadzone", "0"},           {"prboom-resolution", "960x600"},
                {"prboom-mouse_on", "disabled"},           {"prboom-color_format", "24bits (truecolor)"},
                {"prboom-render_threads", "2"},            {"prboom-sound_samplerate", "48000"},
                {"prboom-rumble", "enabled"}};
    if (core == &kWolfCore)
        return {{"ecwolf-analog-deadzone", "0%"}, {"ecwolf-resolution", "960x600"},
                {"ecwolf-fps", "70"},             {"ecwolf-alwaysrun", "enabled"}};
    if (core == &kQuakeCore)
        return {{"tyrquake_analog_deadzone", "0"}, {"tyrquake_resolution", "640x480"},
                {"tyrquake_framerate", "72"},      {"tyrquake_rumble", "enabled"},
                {"tyrquake_sound_samplerate", "48000"}};
    return {};
}

} // namespace coredefaults
