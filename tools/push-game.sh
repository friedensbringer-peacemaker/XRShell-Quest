#!/usr/bin/env bash
# Legt einen eigenen Spielordner (BYOA) auf die Quest: game/<Name>/ in XRShell.
#
#   tools/push-game.sh <Quellordner> <Name> [Tastenprofil] [--exclude MUSTER ...] [--mkdir ORDNER ...]
#   APP_ID=xr.dosgames tools/push-game.sh <Spielordner> <Name>   # DOS-Sammlung: files/games/dos/<Name>/
#   APP_ID=xr.shooter SYSTEM=doom tools/push-game.sh <Ordner> <Name>   # games/<doom|wolf3d|quake>/<Name>/
#   APP_ID=xr.adventure tools/push-game.sh <Ordner> <Name>   # Adventures: files/games/adventure/<Name>/ (mit *.scummvm)
#
# Überträgt als tar und entpackt auf dem Gerät — adb push eines Ordners bricht
# unter Windows (adb 37) mitten in der Übertragung ab. Das Profil wird als
# xrshell.keys in den Spielordner gelegt. XRShell startet den alphabetisch
# ersten Ordner in game/; das Skript zeigt am Ende, was dort liegt.
set -euo pipefail

if [ $# -lt 2 ]; then
    sed -n '2,11p' "$0"
    exit 1
fi
SRC=$1; NAME=$2; shift 2
PROFILE=""
if [ $# -gt 0 ] && [ "${1:0:2}" != "--" ]; then PROFILE=$1; shift; fi
EXCLUDES=(); MKDIRS=()
while [ $# -gt 0 ]; do
    case $1 in
        --exclude) EXCLUDES+=(--exclude="$2"); shift 2 ;;
        --mkdir) MKDIRS+=("$2"); shift 2 ;;
        *) echo "Unbekanntes Argument: $1" >&2; exit 1 ;;
    esac
done

ADB=${ADB:-${ANDROID_HOME:-${LOCALAPPDATA:-}/Android/Sdk}/platform-tools/adb}
[ -x "$ADB" ] || [ -x "$ADB.exe" ] || ADB=$(command -v adb)
APP_ID=${APP_ID:-xr.shell}   # andere App: APP_ID=xr.island
# xr.dosgames hat eine Spielauswahl: jeder Ordner unter files/games/dos/ ist ein Spiel.
# Andere Apps starten den ersten Ordner in files/game/. GAME_SUBDIR überschreibt beides.
# xr.shooter: Ordner je Engine unter files/games/<SYSTEM>/ (Startdatei sucht die App,
# siehe docs/SHOOTER.md).
case $APP_ID in
    xr.dosgames) GAME_SUBDIR=${GAME_SUBDIR:-games/dos} ;;
    # xr.adventure: jeder Ordner unter files/games/adventure/ mit einer *.scummvm (Spiel-ID) ist ein Spiel.
    xr.adventure)
        GAME_SUBDIR=${GAME_SUBDIR:-games/adventure}
        ls "$SRC"/*.scummvm >/dev/null 2>&1 ||
            echo "Hinweis: keine *.scummvm in $SRC — ohne sie erscheint das Spiel nicht (z. B. profiles/beneath-a-steel-sky.scummvm)" >&2 ;;
    xr.shooter)
        case ${SYSTEM:-} in
            doom|wolf3d|quake) GAME_SUBDIR=${GAME_SUBDIR:-games/$SYSTEM} ;;
            *) echo "APP_ID=xr.shooter braucht SYSTEM=doom, wolf3d oder quake" >&2; exit 1 ;;
        esac ;;
    *)      GAME_SUBDIR=${GAME_SUBDIR:-game} ;;
esac
GAME_DIR=/sdcard/Android/data/$APP_ID/files/$GAME_SUBDIR
TMP_TAR=/data/local/tmp/xrshell-game.tar

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
SRC_ABS=$(cd "$SRC" && pwd)
if [ -z "$PROFILE" ] && [ ${#EXCLUDES[@]} -eq 0 ] && [ ${#MKDIRS[@]} -eq 0 ] &&
   [ "$(basename "$SRC_ABS")" = "$NAME" ]; then
    # Fertig vorbereiteter Ordner (z. B. aus prepare-*.sh) mit passendem Namen:
    # direkt packen — die Zwischenkopie kostet unter Windows sonst Minuten.
    tar -C "$(dirname "$SRC_ABS")" -cf "$STAGE/game.tar" "$NAME"
else
    mkdir -p "$STAGE/$NAME"
    tar -C "$SRC" ${EXCLUDES[@]+"${EXCLUDES[@]}"} -cf - . | tar -C "$STAGE/$NAME" -xf -
    if [ -n "$PROFILE" ]; then cp "$PROFILE" "$STAGE/$NAME/xrshell.keys"; fi
    for d in ${MKDIRS[@]+"${MKDIRS[@]}"}; do mkdir -p "$STAGE/$NAME/$d"; done   # z. B. leerer Spielstand-Ordner
    tar -C "$STAGE" -cf "$STAGE/game.tar" "$NAME"
fi
echo "Paket: $(du -h "$STAGE/game.tar" | cut -f1) → $GAME_DIR/$NAME"

# Git Bash würde /sdcard/... sonst in Windows-Pfade umschreiben.
export MSYS_NO_PATHCONV=1
# Damit kommt aber auch /tmp/… unübersetzt bei adb.exe an → als Windows-Pfad übergeben.
LOCAL_TAR=$STAGE/game.tar
if command -v cygpath >/dev/null 2>&1; then LOCAL_TAR=$(cygpath -m "$LOCAL_TAR"); fi
"$ADB" push "$LOCAL_TAR" "$TMP_TAR"
# chmod: per adb angelegte Ordner gehören „shell“ — sonst kann die App sie nicht lesen
# (legt die App game/ selbst an, gehört er ihr; dann schlägt chmod still fehl).
# Spielordner a+rwX: DOSBox-Pure hängt einen Ordner direkt als beschreibbares C: ein (kein
# Overlay, keine .pure.zip) — Spielstände/Einstellungen landen IM Spielordner. Die App greift als
# „andere“ auf die shell-eigenen Dateien zu und braucht dafür Schreibrecht (Core-Smoke-Test
# 2026-10-04: DOS-Doom schreibt DEFAULT.CFG beim Beenden).
# Vor dem ersten App-Start legt adb auch <id>/ und files/ an → ebenfalls freigeben.
APP_DATA=/sdcard/Android/data/$APP_ID
# Zwischenebenen (z. B. games/ bei games/dos) ebenfalls freigeben.
PARENTS="$APP_DATA $APP_DATA/files"
sub=$APP_DATA/files
IFS=/ read -r -a parts <<< "$GAME_SUBDIR"
for part in "${parts[@]}"; do sub=$sub/$part; PARENTS="$PARENTS $sub"; done
"$ADB" shell "mkdir -p $GAME_DIR && { chmod 0777 $PARENTS 2>/dev/null || true; } && cd $GAME_DIR && rm -rf '$NAME' && tar -xf $TMP_TAR && chmod -R a+rwX '$NAME' && rm $TMP_TAR"
if [ "$GAME_SUBDIR" = game ]; then
    echo "Inhalt von game/ (der erste Ordner startet):"
else
    echo "Inhalt von $GAME_SUBDIR/ (Spielauswahl im VR-Menü, Reiter „Spiele“):"
fi
"$ADB" shell ls "$GAME_DIR"
