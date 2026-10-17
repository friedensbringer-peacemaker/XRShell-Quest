#!/usr/bin/env bash
# Geräte-Tests: tests/device/*.cpp mit dem NDK für die Quest (arm64) bauen, per adb nach
# /data/local/tmp schieben, dort ausführen und wieder löschen. Für Logik, die an
# GLES-/AAudio-/Android-Headern hängt (Geometrie, Ton-Umrechnung, Frame-Analyse).
#   tests/run-device-tests.sh            (aus dem Repo-Stamm; braucht eine verbundene Quest)
#   BUILD_ONLY=1 tests/run-device-tests.sh   nur bauen (ohne Quest)
# Vorher einmal bauen (android/gradlew assembleDebug), damit OpenXR-/stb-Header da sind.
set -euo pipefail
cd "$(dirname "$0")/.."
export MSYS_NO_PATHCONV=1

# Rückfall: <XR-Ordner>/_tools (Repo liegt unter <XR-Ordner>/XR-Ports-Platform/XR-Ports-Platform).
# pwd -W liefert unter Git Bash einen Windows-Pfad (D:/...), sonst normales pwd.
XR_TOOLS=${XR_TOOLS:-$(cd ../.. && { pwd -W 2>/dev/null || pwd; })/_tools}
SDK=${ANDROID_HOME:-$XR_TOOLS/android-sdk}
NDK=${ANDROID_NDK:-$SDK/ndk/30.0.16248370}
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) HOST=windows-x86_64; EXE=.exe; CXXEXE=clang++.exe ;;
                      Darwin) HOST=darwin-x86_64; EXE=; CXXEXE=clang++ ;; *) HOST=linux-x86_64; EXE=; CXXEXE=clang++ ;; esac
CXX="$NDK/toolchains/llvm/prebuilt/$HOST/bin/$CXXEXE"
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang$EXE"
ADB="$SDK/platform-tools/adb$EXE"
[ -x "$CXX" ] || { echo "NDK-Compiler fehlt: $CXX" >&2; exit 1; }
if [ -z "${BUILD_ONLY:-}" ]; then
    "$ADB" devices | grep -q "device$" || { echo "Keine Quest verbunden (BUILD_ONLY=1 zum reinen Bauen)" >&2; exit 1; }
fi

OPENXR_INC=$(find android/.cxx -type d -path "*openxr-src/include" | head -1)
STB_INC=$(find android/.cxx -type d -name "stb-src" | head -1)
[ -n "$OPENXR_INC" ] && [ -n "$STB_INC" ] || { echo "Erst bauen (OpenXR-/stb-Header fehlen)" >&2; exit 1; }

OUT=$(mktemp -d)
case "$HOST" in windows-*) OUT=$(cygpath -m "$OUT") ;; esac   # Windows-Compiler braucht Windows-Pfad
trap 'rm -rf "$OUT"' EXIT
TARGET=(--target=aarch64-linux-android29)
FLAGS=("${TARGET[@]}" -std=c++17 -O1 -Wall -Wextra -Wno-missing-field-initializers -Werror
       -DXR_USE_PLATFORM_ANDROID -DXR_USE_GRAPHICS_API_OPENGL_ES
       -Isrc -I"$NDK/sources/android/native_app_glue" -I"$OPENXR_INC" -isystem "$STB_INC")
LIBS=(-static-libstdc++ -lGLESv3 -lEGL -landroid -llog -laaudio -ldl)

# Fremdcode (stb) einmal als Objekt, ohne Warnungen
"$CC" "${TARGET[@]}" -O1 -w -isystem "$STB_INC" -c src/stb_truetype_impl.c -o "$OUT/stb_tt.o"
"$CC" "${TARGET[@]}" -O1 -w -isystem "$STB_INC" -c src/stb_image_impl.c -o "$OUT/stb_img.o"

build() { # build <name> <quellen…>
    local name=$1; shift
    "$CXX" "${FLAGS[@]}" "$@" "${LIBS[@]}" -o "$OUT/$name"
}
build test_geometry tests/device/test_geometry.cpp src/quad_renderer.cpp
build test_audio tests/device/test_audio.cpp src/core_audio.cpp
build test_settings_analysis tests/device/test_settings_analysis.cpp src/vr_menu.cpp src/pad_mapping.cpp src/text_raster.cpp \
    src/menu_theme.cpp src/quad_renderer.cpp src/libretro_adapter.cpp src/core_audio.cpp src/game_folders.cpp \
    "$OUT/stb_tt.o" "$OUT/stb_img.o"
build test_menu_preview tests/device/test_menu_preview.cpp src/vr_menu.cpp src/pad_mapping.cpp src/text_raster.cpp \
    src/menu_theme.cpp src/quad_renderer.cpp "$OUT/stb_tt.o" "$OUT/stb_img.o"

if [ -n "${BUILD_ONLY:-}" ]; then ls -la "$OUT" | grep test_; echo "Geräte-Tests gebaut (nicht ausgeführt)"; exit 0; fi
fail=0
for t in test_geometry test_audio test_settings_analysis test_menu_preview; do
    "$ADB" push "$OUT/$t" /data/local/tmp/ >/dev/null
    if "$ADB" shell "chmod 755 /data/local/tmp/$t && /data/local/tmp/$t"; then :; else fail=1; fi
    "$ADB" shell "rm -f /data/local/tmp/$t"
done
# Menü-Vorschauen (BMP) nach tests/out/ holen und auf der Quest löschen.
mkdir -p tests/out
for b in $("$ADB" shell "ls /data/local/tmp/" | grep -o 'xrshell-menu-[^[:space:]]*.bmp'); do
    "$ADB" pull "/data/local/tmp/$b" "tests/out/$b" >/dev/null && "$ADB" shell "rm -f /data/local/tmp/$b"
done
echo "Menü-Vorschau: tests/out/xrshell-menu-*.bmp"
[ "$fail" = 0 ] && echo "Geräte-Tests: alle bestanden" || { echo "Geräte-Tests: FEHLER" >&2; exit 1; }
