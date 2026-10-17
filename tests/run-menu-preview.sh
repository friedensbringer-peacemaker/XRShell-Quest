#!/usr/bin/env bash
# VR-Menü am Entwicklungsrechner prüfen (ohne Quest): tests/device/test_menu_preview.cpp
# mit GL-/Android-Stubs für den Host bauen, Logik-Prüfungen laufen lassen und die
# Tafel-Vorschauen als BMP nach tests/out/ schreiben.
#   tests/run-menu-preview.sh            (aus dem Repo-Stamm)
# Schrift: Segoe UI (Windows) bzw. XRSHELL_FONT/XRSHELL_FONT_BOLD — auf der Quest Roboto,
# Laufweiten weichen also leicht ab. Header: OpenXR/stb aus einem früheren Gradle-Build.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX=${CXX:-}
XR_TOOLS=${XR_TOOLS:-$(cd ../.. && pwd)/_tools}   # <XR-Ordner>/_tools, zwei Ebenen über dem Repo
if [ -z "$CXX" ]; then
    for c in clang++ g++ "$XR_TOOLS/llvm-mingw/bin/clang++"; do
        if command -v "$c" >/dev/null 2>&1; then CXX=$c; break; fi
    done
fi
[ -n "$CXX" ] || { echo "Kein C++-Compiler gefunden (CXX setzen)" >&2; exit 1; }
CC=${CC:-${CXX%++}}

find_inc() { # find_inc <find-Ausdruck…>: erst eigener, dann jeder Worktree des Repos
    local dir
    for root in android/.cxx $(git worktree list --porcelain | sed -n 's/^worktree //p' | sed 's|$|/android/.cxx|'); do
        [ -d "$root" ] || continue
        dir=$(find "$root" "$@" 2>/dev/null | head -1)
        [ -n "$dir" ] && { echo "$dir"; return; }
    done
}
OPENXR_INC=$(find_inc -type d -path "*openxr-src/include")
STB_INC=$(find_inc -type d -name "stb-src")
[ -n "$OPENXR_INC" ] && [ -n "$STB_INC" ] || { echo "Erst bauen (OpenXR-/stb-Header fehlen)" >&2; exit 1; }

if [ -z "${XRSHELL_FONT:-}" ] && [ -f /c/Windows/Fonts/segoeui.ttf ]; then
    export XRSHELL_FONT=C:/Windows/Fonts/segoeui.ttf XRSHELL_FONT_BOLD=C:/Windows/Fonts/segoeuib.ttf
fi

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
FLAGS=(-std=c++17 -O1 -Wall -Wextra -Wno-missing-field-initializers -Werror
       -Itests/stubs -Isrc -I"$OPENXR_INC" -isystem "$STB_INC")
case $("$CXX" -dumpmachine) in *mingw*|*windows-gnu*) FLAGS+=(-static) ;; esac

"$CC" -O1 -w -isystem "$STB_INC" -c src/stb_truetype_impl.c -o "$OUT/stb_tt.o"
"$CC" -O1 -w -isystem "$STB_INC" -c src/stb_image_impl.c -o "$OUT/stb_img.o"
"$CXX" "${FLAGS[@]}" tests/device/test_menu_preview.cpp src/vr_menu.cpp src/text_raster.cpp \
    src/menu_theme.cpp src/quad_renderer.cpp src/pad_mapping.cpp "$OUT/stb_tt.o" "$OUT/stb_img.o" \
    -o "$OUT/test_menu_preview"
mkdir -p tests/out
"$OUT/test_menu_preview" tests/out
