#!/usr/bin/env bash
# Host-Test für tools/prepare-adventure.sh mit nachgebauten Ordnern (leere Platzhalter mit den
# Dateinamen der Originale — keine Spieldaten). Aufruf aus dem Repo-Stamm oder run-host-tests.sh.
set -uo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
PREP=tools/prepare-adventure.sh
checks=0; errors=0
check() { checks=$((checks + 1)); if eval "$1"; then :; else errors=$((errors + 1)); echo "FEHLER: $2"; fi; }
mk() { mkdir -p "$(dirname "$1")"; printf '%s' "${2:-x}" > "$1"; }

# Baphomets Fluch 2: zwei CDs, gleiche Namen speech.clu/music.clu, Programm + Autorun.
for cd in 1 2; do
    mk "$T/bs2cd$cd/SPEECH.CLU" "s$cd"; mk "$T/bs2cd$cd/MUSIC.CLU" "m$cd"
    mk "$T/bs2cd$cd/SWORD2.EXE"; mk "$T/bs2cd$cd/AUTORUN.INF"
    mk "$T/bs2cd$cd/VIDEO/INTRO$cd.SMK"
done
for f in PLAYERS.CLU RESOURCE.INF RESOURCE.TAB CD.INF; do mk "$T/bs2cd1/$f"; mk "$T/bs2cd2/$f"; done
out=$("$PREP" broken-sword-2 "$T/bs2cd1" "$T/bs2cd2" --out "$T/out-bs2" 2>&1); rc=$?
check '[ $rc -eq 0 ]' "BS2 mit zwei CDs läuft durch ($out)"
check '[ "$(cat "$T/out-bs2/speech1.clu")" = s1 ] && [ "$(cat "$T/out-bs2/speech2.clu")" = s2 ]' "speech.clu → speech1/2 je CD"
check '[ "$(cat "$T/out-bs2/music1.clu")" = m1 ] && [ "$(cat "$T/out-bs2/music2.clu")" = m2 ]' "music.clu → music1/2 je CD"
check '[ ! -e "$T/out-bs2/SPEECH.CLU" ] && [ ! -e "$T/out-bs2/SWORD2.EXE" ] && [ ! -e "$T/out-bs2/AUTORUN.INF" ]' "Original-Namen, Programm und Autorun bleiben weg"
check '[ -e "$T/out-bs2/VIDEO/INTRO1.SMK" ] && [ -e "$T/out-bs2/VIDEO/INTRO2.SMK" ]' "Filme beider CDs mit Unterordner"
check '[ "$(cat "$T/out-bs2/sword2.scummvm")" = sword2 ]' "sword2.scummvm mit ScummVM-ID"
check '[ "$(cat "$T/out-bs2/xrshell.title")" = "Baphomets Fluch 2" ]' "Titel für die Spielauswahl"
check 'grep -q "^xrshell_filter = scharf$" "$T/out-bs2/xrshell.opt"' "640×480: Filter scharf"
check 'grep -q "^game_menu = LCTRL+F5" "$T/out-bs2/xrshell.keys"' "Tastenprofil als xrshell.keys dabei"
check '! printf "%s" "$out" | grep -q Warnung' "vollständige Daten: keine Warnung"

# Baphomets Fluch 1 als GOG-Ordner (eine Quelle, Unterordner, GOG-Hülle).
for f in clusters/SWORDRES.RIF clusters/GENERAL.CLU clusters/COMPACTS.CLU clusters/SCRIPTS.CLU \
         speech/SPEECH1.CLU speech/SPEECH2.CLU music/1M1.WAV smackshi/INTRO.SMK \
         goggame-1234.info goggame-1234.ico __support/app/dosbox.conf unins000.exe          SMACKSLO/INTRO.SMK INSTALL/TEST.SMK SB16.DIG AILDRVR.LST; do mk "$T/bs1/$f"; done
out=$("$PREP" broken-sword-1 "$T/bs1" --out "$T/out-bs1" 2>&1); rc=$?
check '[ $rc -eq 0 ]' "BS1 aus GOG-Ordner läuft durch ($out)"
check '[ -e "$T/out-bs1/clusters/SWORDRES.RIF" ] && [ -e "$T/out-bs1/speech/SPEECH2.CLU" ]' "BS1: Unterordner bleiben"
check '[ -z "$(find "$T/out-bs1" -iname "goggame-*" -o -iname "unins*" -o -path "*__support*")" ]' "GOG-Hülle bleibt weg"
check '[ "$(cat "$T/out-bs1/sword1.scummvm")" = sword1 ]' "sword1.scummvm"
check '[ ! -e "$T/out-bs1/SMACKSLO" ] && [ ! -e "$T/out-bs1/INSTALL" ] && [ ! -e "$T/out-bs1/SB16.DIG" ] && [ ! -e "$T/out-bs1/AILDRVR.LST" ] && [ -e "$T/out-bs1/smackshi/INTRO.SMK" ]' "BS1: Low-Res-Videos, Installer, DOS-Treiber bleiben weg"

# Fehlende Teile: nur CD 1 von BS2 → Warnung, aber Erfolg.
out=$("$PREP" broken-sword-2 "$T/bs2cd1" --out "$T/out-bs2a" 2>&1); rc=$?
check '[ $rc -eq 0 ]' "BS2 nur CD 1 läuft durch"
check 'printf "%s" "$out" | grep -q "Warnung: speech1.clu fehlt"' "eine Quelle: speech.clu bleibt → Warnung"

# Director's Cut (andere Dateien) → Abbruch mit Hinweis.
mk "$T/dc/bs1dc.dat"; mk "$T/dc/BS1DC.exe"
out=$("$PREP" broken-sword-1 "$T/dc" --out "$T/out-dc" 2>&1); rc=$?
check '[ $rc -ne 0 ] && printf "%s" "$out" | grep -q "Director"' "Director's Cut: Abbruch mit Hinweis"

# BS2 Remastered (GOG 2022: Game/*.clu + TEXTURE.DAT) → Abbruch vor dem Kopieren.
mk "$T/bs2r/Game/Players.clu"; mk "$T/bs2r/Game/TEXTURE.DAT"; mk "$T/bs2r/Data.dat"
out=$("$PREP" broken-sword-2 "$T/bs2r" --out "$T/out-bs2r" 2>&1); rc=$?
check '[ $rc -ne 0 ] && printf "%s" "$out" | grep -q "Remastered" && [ ! -e "$T/out-bs2r" ]' "Remastered: Abbruch ohne Kopie"

# Schutz: Ziel nicht leer, Ziel im Repo, unbekanntes Spiel.
out=$("$PREP" broken-sword-2 "$T/bs2cd1" --out "$T/out-bs2" 2>&1); rc=$?
check '[ $rc -ne 0 ] && printf "%s" "$out" | grep -q "nicht leer"' "volles Ziel wird nicht überschrieben"
out=$("$PREP" broken-sword-2 "$T/bs2cd1" --out "tests/out/prepare-im-repo" 2>&1); rc=$?
check '[ $rc -ne 0 ] && [ ! -e tests/out/prepare-im-repo ]' "Ziel im Repo wird abgelehnt und nicht angelegt"
out=$("$PREP" monkey-island "$T/bs1" 2>&1); rc=$?
check '[ $rc -ne 0 ]' "unbekanntes Spiel bricht ab"

echo "test_prepare_adventure: $checks Prüfungen, $errors Fehler"
[ "$errors" -eq 0 ]
