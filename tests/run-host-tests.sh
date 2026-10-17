#!/usr/bin/env bash
# Host-Tests (ohne Android/Quest). Aus dem Repo-Stamm starten:
#   tests/run-host-tests.sh
# Compiler: $CXX, sonst clang++/g++ aus dem PATH, unter Windows llvm-mingw aus
# $XR_TOOLS/llvm-mingw (Standard: <XR-Ordner>/_tools, zwei Ebenen über dem Repo).
set -euo pipefail
cd "$(dirname "$0")/.."

CXX=${CXX:-}
XR_TOOLS=${XR_TOOLS:-$(cd ../.. && pwd)/_tools}
if [ -z "$CXX" ]; then
    for c in clang++ g++ "$XR_TOOLS/llvm-mingw/bin/clang++"; do
        if command -v "$c" >/dev/null 2>&1; then CXX=$c; break; fi
    done
fi
[ -n "$CXX" ] || { echo "Kein C++-Compiler gefunden (CXX setzen)" >&2; exit 1; }

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS=(-std=c++17 -Wall -Wextra -Werror -O1)
# llvm-mingw: Laufzeit (libc++/libunwind) statisch, sonst fehlen DLLs beim Start.
case $("$CXX" -dumpmachine) in *mingw*|*windows-gnu*) FLAGS+=(-static) ;; esac

# key_profile.cpp bindet "libretro_adapter.h" relativ zu src/ ein → Quelle mit den
# Stubs in ein eigenes Verzeichnis kopieren, damit der Stub gewinnt.
mkdir -p "$OUT/kp"
cp src/key_profile.cpp src/key_profile.h src/libretro.h "$OUT/kp/"
cp -r tests/stubs/. "$OUT/kp/"
"$CXX" "${FLAGS[@]}" -I"$OUT/kp" "$OUT/kp/key_profile.cpp" tests/test_key_profile.cpp \
    -o "$OUT/test_key_profile"
"$OUT/test_key_profile"

# Core-Wahl/Import-Ziel und Controller-Belegung: reine Logik, direkt aus src/.
"$CXX" "${FLAGS[@]}" -Isrc tests/test_core_select.cpp -o "$OUT/test_core_select"
"$OUT/test_core_select"
"$CXX" "${FLAGS[@]}" -Isrc tests/test_core_defaults.cpp -o "$OUT/test_core_defaults"
"$OUT/test_core_defaults"
"$CXX" "${FLAGS[@]}" -Isrc src/pad_mapping.cpp tests/test_pad_mapping.cpp -o "$OUT/test_pad_mapping"
"$OUT/test_pad_mapping"

# Spielauswahl xr.dosgames/xr.shooter: Spielordner, Titel, Startdatei-Regeln (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc src/game_folders.cpp tests/test_game_folders.cpp -o "$OUT/test_game_folders"
"$OUT/test_game_folders"

# Laser → Spielzeiger (DOSBox-Pure „direct“), Bildzoom, Filternamen (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_pointer_map.cpp -o "$OUT/test_pointer_map"
"$OUT/test_pointer_map"

# Batteriespeicher (SRAM) der Pad-Cores als <Spiel>.srm (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_save_ram.cpp -o "$OUT/test_save_ram"
"$OUT/test_save_ram"

# Bildschirmtastatur: QWERTZ/QWERTY je Gastbelegung, jede Taste erreicht DOSBox-Pure (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_keyboard_layout.cpp -o "$OUT/test_keyboard_layout"
"$OUT/test_keyboard_layout"

# xr.dosgames: Kern-Optionen/Bildwerte pro Spiel aus dem VR-Menü (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_option_store.cpp -o "$OUT/test_option_store"
"$OUT/test_option_store"

# xr.dosgames: „Zeiger lernen“ — Spielzeiger im Speicher finden und an den Laser regeln
"$CXX" "${FLAGS[@]}" -Isrc tests/test_cursor_lock.cpp -o "$OUT/test_cursor_lock"
"$OUT/test_cursor_lock"

# xr.dosgames: relative Maus — Faktor pro Achse, Zeiger fangen, Abgleich (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_mouse_relative.cpp -o "$OUT/test_mouse_relative"
"$OUT/test_mouse_relative"

# xr.dosgames: Karte ziehen mit dem rechten Griff — Auslenkung → Scroll-Tasten (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_map_drag.cpp -o "$OUT/test_map_drag"
"$OUT/test_map_drag"

# Pad-Modus (xr.shooter, DOSBox-Pad): Sticks → Analog, Belegungen, Steuerkreuz
"$CXX" "${FLAGS[@]}" -Isrc tests/test_pad_analog.cpp -o "$OUT/test_pad_analog"
"$OUT/test_pad_analog"

# Quest-Bildwiederholrate passend zur Core-Bildrate (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_refresh_rate.cpp -o "$OUT/test_refresh_rate"

# Takt des Core-Threads: feste Fristen, kein Drift, keine Salven (simulierte Uhr)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_frame_pacer.cpp -o "$OUT/test_frame_pacer"
"$OUT/test_frame_pacer"
"$OUT/test_refresh_rate"

# xr.adventure: Sprache/Untertitel für die erste scummvm.ini (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_scumm_setup.cpp -o "$OUT/test_scumm_setup"
"$OUT/test_scumm_setup"

# xr.adventure: tools/prepare-adventure.sh (Umbenennen der CD-Dateien, Filter, Schutz vor Repo-Ziel)
bash tests/test_prepare_adventure.sh

# Ladeanzeige: gleitendes Mittel, xr-load-times.json, Fortschritt/Restzeit, Startsignal, Watchdog (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_load_status.cpp -o "$OUT/test_load_status"
"$OUT/test_load_status"

# Sitzungslog auf dem Gerät: Dateinamen, Aufräumen auf 10 Sitzungen, Teile bei 5 MB (abhängigkeitsfrei)
"$CXX" "${FLAGS[@]}" -Isrc tests/test_session_log.cpp -o "$OUT/test_session_log"
"$OUT/test_session_log"

# VR-Menü (xr.dosgames, Reiter DOS) auf dem Rechner: Logik-Test + PNG-Vorschau nach tests/out/.
# Braucht OpenXR-/stb-Header aus einem Gradle-Build (android/.cxx); ohne sie übersprungen.
OPENXR_INC=$(find android/.cxx -type d -path "*openxr-src/include" 2>/dev/null | head -1)
STB_INC=$(find android/.cxx -type d -name "stb-src" 2>/dev/null | head -1)
if [ -n "$OPENXR_INC" ] && [ -n "$STB_INC" ]; then
    case "$CXX" in *clang++*) CC=${CXX%++} ;; *g++*) CC=${CXX%g++}gcc ;; *) CC=cc ;; esac
    for c in src/stb_truetype_impl.c src/stb_image_impl.c tests/host/stb_image_write_impl.c; do
        "$CC" -O1 -w -isystem "$STB_INC" -c "$c" -o "$OUT/$(basename "$c" .c).o"
    done
    mkdir -p tests/out
    "$CXX" "${FLAGS[@]}" -Wno-missing-field-initializers -DXRSHELL_HOST_PREVIEW \
        -Isrc -Itests/host-stubs -Itests/stubs -isystem "$OPENXR_INC" -isystem "$STB_INC" \
        tests/host/menu_preview.cpp src/vr_menu.cpp src/pad_mapping.cpp src/text_raster.cpp src/menu_theme.cpp src/quad_renderer.cpp \
        "$OUT"/stb_truetype_impl.o "$OUT"/stb_image_impl.o "$OUT"/stb_image_write_impl.o -o "$OUT/menu_preview"
    "$OUT/menu_preview"
    # Ladeanzeige: Logik (Lichtpunkt, Teil-Redraw) + PNG-Vorschau tests/out/xrshell-load-*.png
    "$CXX" "${FLAGS[@]}" -Wno-missing-field-initializers -DXRSHELL_HOST_PREVIEW \
        -Isrc -Itests/host-stubs -Itests/stubs -isystem "$OPENXR_INC" -isystem "$STB_INC" \
        tests/host/load_preview.cpp src/load_screen.cpp src/text_raster.cpp src/quad_renderer.cpp \
        "$OUT"/stb_truetype_impl.o "$OUT"/stb_image_write_impl.o -o "$OUT/load_preview"
    "$OUT/load_preview"
else
    echo "Menü-Vorschau übersprungen (erst android/gradlew assembleDebug für OpenXR-/stb-Header)"
fi
