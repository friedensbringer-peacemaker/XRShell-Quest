#!/usr/bin/env bash
# Holt die Sitzungslogs der XRShell-Apps von der Quest (ohne root, ohne run-as).
#
#   tools/pull-logs.sh              # alle XRShell-Apps, die auf dem Gerät Logs haben
#   tools/pull-logs.sh xr.dosgames  # nur eine App
#
# Quelle: /sdcard/Android/data/<APP_ID>/files/logs/session-*.log (schreibt jede XRShell-App,
# src/session_log.h). Ziel: <XR>/Meta Quest 3/Logs/<JJJJ-MM-TT>/<APP_ID>/ (QUEST_DIR überschreibt
# <XR>/Meta Quest 3). Dateien einzeln — adb pull ganzer Ordner bricht unter Windows (adb 37) ab.
# Schon geholte Dateien gleicher Größe werden übersprungen; die laufende Sitzung wird jedes Mal
# neu geholt (sie wächst). Auf dem Gerät wird nichts gelöscht oder gestartet.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
QUEST_DIR=${QUEST_DIR:-$(cd "$ROOT/../.." && pwd)/Meta Quest 3}
ADB=${ADB:-${ANDROID_HOME:-}/platform-tools/adb}
[ -x "$ADB" ] || [ -x "$ADB.exe" ] || ADB=$(command -v adb || true)
[ -n "$ADB" ] || { echo "adb nicht gefunden (ADB oder ANDROID_HOME setzen)" >&2; exit 1; }
# Git Bash würde /sdcard/... sonst in Windows-Pfade umschreiben.
export MSYS_NO_PATHCONV=1
to_native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }

ALL_APPS=(xr.shell xr.island xr.adventure xr.dosgames xr.shooter)
if [ $# -gt 0 ]; then APPS=("$@"); else APPS=("${ALL_APPS[@]}"); fi

"$ADB" get-state >/dev/null 2>&1 || { echo "Keine Quest verbunden (adb get-state)" >&2; exit 1; }

DAY=$(date +%Y-%m-%d)
total=0
for app in "${APPS[@]}"; do
    case $app in xr.*) ;; *) echo "Unbekannte App-ID: $app (erwartet xr.<name>)" >&2; exit 1 ;; esac
    remote=/sdcard/Android/data/$app/files/logs
    # „ls -l“ liefert Größe und Name; fehlender Ordner/keine Rechte → leer.
    listing=$("$ADB" shell "ls -l $remote 2>/dev/null" | tr -d '\r' || true)
    names=$(printf '%s\n' "$listing" | awk '$NF ~ /^session-.*\.log$/ {print $5 " " $NF}')
    if [ -z "$names" ]; then
        [ $# -gt 0 ] && echo "$app: keine Sitzungslogs in $remote"
        continue
    fi
    out=$QUEST_DIR/Logs/$DAY/$app
    mkdir -p "$out"
    newest=$(printf '%s\n' "$names" | awk '{print $2}' | sort | tail -1)
    count=0
    while read -r size name; do
        [ -n "$name" ] || continue
        local_file=$out/$name
        if [ -f "$local_file" ] && [ "$name" != "$newest" ] &&
           [ "$(wc -c < "$local_file" | tr -d ' ')" = "$size" ]; then
            continue
        fi
        if "$ADB" pull "$remote/$name" "$(to_native "$local_file")" >/dev/null; then
            count=$((count + 1))
        else
            echo "$app: $name nicht lesbar (Rechte? siehe src/session_log.h, Setgid)" >&2
        fi
    done <<< "$names"
    total=$((total + count))
    echo "$app: $count Datei(en) → $out (neueste: $newest)"
done
[ "$total" -gt 0 ] || echo "Nichts Neues geholt."
