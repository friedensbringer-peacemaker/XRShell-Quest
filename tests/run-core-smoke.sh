#!/usr/bin/env bash
# Core-Smoke-Test auf der Quest: echte libretro-Cores mit den echten Spieldaten des Spielers,
# ohne App, ohne OpenXR/GL (tests/device/core_smoke.cpp). Startet keine App, installiert nichts.
#
#   tests/run-core-smoke.sh                       alle Standardspiele (Liste unten)
#   tests/run-core-smoke.sh shooter:doom/Doom2    einzelne Spiele (<app>:<system>/<Ordner>)
#   BUILD_ONLY=1 tests/run-core-smoke.sh          nur bauen
#   KEEP=1 tests/run-core-smoke.sh …              Kopien/Spielstände auf der Quest lassen (Analyse)
#
# Ablauf je Spiel: Spielordner aus /sdcard/Android/data/xr.<app>/files/games/<system>/<Ordner>
# nach /data/local/tmp/coresmoke/games/<system>/ KOPIEREN (das Original wird nur gelesen), Test
# dort ausführen, Ergebnis nach tests/out/core-smoke/<app>-<system>-<Ordner>/ holen, Kopie löschen.
# Eingabe-Skript: tests/device/core_smoke/<system>-<ordner klein>.txt, sonst <system>.txt.
# Cores: android/core-download/<core>/assets/cores/*.so (nach einem Gradle-Build vorhanden).
set -euo pipefail
cd "$(dirname "$0")/.."
export MSYS_NO_PATHCONV=1

XR_TOOLS=${XR_TOOLS:-$(cd ../.. && { pwd -W 2>/dev/null || pwd; })/_tools}
SDK=${ANDROID_HOME:-$XR_TOOLS/android-sdk}
NDK=${ANDROID_NDK:-$SDK/ndk/30.0.16248370}
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) HOST=windows-x86_64; EXE=.exe ;;
                      Darwin) HOST=darwin-x86_64; EXE= ;; *) HOST=linux-x86_64; EXE= ;; esac
CXX="$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang++$EXE"
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/clang$EXE"
ADB="$SDK/platform-tools/adb$EXE"
[ -x "$CXX" ] || { echo "NDK-Compiler fehlt: $CXX" >&2; exit 1; }
STB_INC=$(find android/.cxx -type d -name "stb-src" 2>/dev/null | head -1)
[ -n "$STB_INC" ] || { echo "Erst bauen (android/gradlew assembleDebug) — stb-Header fehlen" >&2; exit 1; }

BUILD=$(mktemp -d)
case "$HOST" in windows-*) BUILD=$(cygpath -m "$BUILD") ;; esac
trap 'rm -rf "$BUILD"' EXIT
TARGET=(--target=aarch64-linux-android29)
"$CC" "${TARGET[@]}" -O1 -w -isystem "$STB_INC" -c tests/host/stb_image_write_impl.c -o "$BUILD/stb_write.o"
"$CXX" "${TARGET[@]}" -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -Werror -Isrc -isystem "$STB_INC" \
    tests/device/core_smoke.cpp src/game_folders.cpp src/pad_mapping.cpp "$BUILD/stb_write.o" \
    -static-libstdc++ -ldl -o "$BUILD/core_smoke"
echo "core_smoke gebaut"
[ -n "${BUILD_ONLY:-}" ] && exit 0

"$ADB" devices | grep -q "device$" || { echo "Keine Quest verbunden (BUILD_ONLY=1 zum reinen Bauen)" >&2; exit 1; }

GAMES=("$@")
[ ${#GAMES[@]} -gt 0 ] || GAMES=(shooter:doom/Doom2 shooter:doom/Doom shooter:doom/FinalDoomTNT shooter:doom/Sigil
    shooter:doom/NoRestForTheLiving shooter:doom/MasterLevels shooter:quake/Quake shooter:quake/QuakeMP1
    dosgames:dos/Doom2 dosgames:dos/Quake)

DEV=/data/local/tmp/coresmoke
OUTROOT=tests/out/core-smoke
mkdir -p "$OUTROOT"
"$ADB" shell "mkdir -p $DEV/cores $DEV/games $DEV/run $DEV/scripts"
"$ADB" push "$BUILD/core_smoke" "$DEV/" >/dev/null
"$ADB" shell "chmod 755 $DEV/core_smoke"
pushed=" "
coreFor() { case $1 in doom) echo prboom ;; quake) echo tyrquake ;; wolf3d) echo ecwolf ;; *) echo dosbox_pure ;; esac; }

summary=()
for g in "${GAMES[@]}"; do
    app=${g%%:*}; rest=${g#*:}; sys=${rest%%/*}; name=${rest#*/}
    src=/sdcard/Android/data/xr.$app/files/games/$sys/$name
    tag=$app-$sys-$name
    if ! "$ADB" shell "[ -d '$src' ]"; then
        echo "--- $tag: Spielordner fehlt ($src) — übersprungen"; summary+=("$tag: fehlt"); continue
    fi
    core=$(coreFor "$sys")
    so=$(ls android/core-download/$core/assets/cores/*.so 2>/dev/null | head -1)
    [ -n "$so" ] || { echo "Core $core fehlt (Gradle-Build)" >&2; exit 1; }
    case $pushed in *" $core "*) ;; *) "$ADB" push "$so" "$DEV/cores/" >/dev/null; pushed="$pushed$core " ;; esac
    lname=$(echo "$name" | tr '[:upper:]' '[:lower:]')
    script=tests/device/core_smoke/$sys-$lname.txt
    [ -f "$script" ] || script=tests/device/core_smoke/$sys.txt
    "$ADB" push "$script" "$DEV/scripts/$tag.txt" >/dev/null
    echo "--- $tag: kopiere Spielordner (nur lesen) …"
    "$ADB" shell "rm -rf $DEV/games/$sys/$name $DEV/run/$tag && mkdir -p $DEV/games/$sys && cp -r '$src' $DEV/games/$sys/"
    set +e
    "$ADB" shell "cd $DEV && ./core_smoke $DEV/cores/$(basename "$so") $DEV/games/$sys/$name $DEV/run/$tag $DEV/scripts/$tag.txt" \
        > "$BUILD/$tag.stdout" 2>&1
    rc=$?
    set -e
    rm -rf "$OUTROOT/$tag"
    mkdir -p "$OUTROOT/$tag"
    "$ADB" pull "$DEV/run/$tag/out/." "$OUTROOT/$tag/" >/dev/null || true
    cp "$BUILD/$tag.stdout" "$OUTROOT/$tag/stdout.txt"
    [ -n "${KEEP:-}" ] || "$ADB" shell "rm -rf $DEV/games/$sys/$name $DEV/run/$tag"
    res=$(grep -a "^ERGEBNIS\|^ABBRUCH" "$OUTROOT/$tag/stdout.txt" | tail -1)
    echo "    Exit $rc — ${res:-kein Ergebnis}"
    summary+=("$tag: ${res:-kein Ergebnis (Exit $rc)}")
done
if [ -n "${KEEP:-}" ]; then echo "KEEP=1: Gerätedaten bleiben unter $DEV (danach: adb shell rm -rf $DEV)"
else "$ADB" shell "rm -rf $DEV"; fi
echo
echo "Zusammenfassung (Berichte: $OUTROOT/<spiel>/bericht.txt, Bilder *.png):"
for s in "${summary[@]}"; do echo "  $s"; done
