#!/usr/bin/env bash
# Bereitet Adventures aus eigenen Kopien (CD, GOG, Steam) für xr.adventure (ScummVM) vor.
# BYOA: alles kommt aus den Daten des Spielers und landet außerhalb des Repos; nichts davon
# kommt ins Repo oder ins APK.
#
#   tools/prepare-adventure.sh <spiel> <quelle> [<quelle CD 2>] [--out <ziel>]
#   APP_ID=xr.adventure tools/push-game.sh <ziel> <Ordner>     # → files/games/adventure/<Ordner>/
#
# Spiele (<spiel>):          ScummVM-ID  Ordner auf der Quest
#   broken-sword-1           sword1      BaphometsFluch1   (Baphomets Fluch, 1996)
#   broken-sword-2           sword2      BaphometsFluch2   (Baphomets Fluch 2, 1997)
#   beneath-a-steel-sky      sky         BASS
#
# <quelle> = Spielordner bzw. CD 1 (Laufwerk, kopierte CD oder GOG-Ordner); bei zwei CDs die
# zweite als <quelle CD 2>. Liegen speech.clu/music.clu auf beiden CDs unter gleichem Namen,
# werden sie zu speech1/speech2 bzw. music1/music2 umbenannt (Baphomets Fluch 2, so verlangt es
# ScummVM). Programmdateien, Installer und GOG-Hülle bleiben weg.
#
# Nur die Originalfassungen laufen: „Director's Cut“ (BS1) und „Remastered“ (BS2) sind andere
# Engines — das Skript bricht ab, wenn die Pflichtdateien der Originalfassung fehlen.
#
# Ergebnis: Spieldaten, <id>.scummvm (Inhalt = ScummVM-ID), xrshell.title (Name in der
# Spielauswahl), xrshell.opt (Bildfilter: 640×480-Spiele „scharf“, 320×200 bleibt HD),
# xrshell.keys aus profiles/<spiel>.keys (Controller-Tasten, z. B. A = Speichermenü).
# Zieleordner muss leer sein oder fehlen (nie über alte Daten kopieren). Befunde: docs/ADVENTURE.md.
set -euo pipefail

die() { echo "Fehler: $*" >&2; exit 1; }
usage() { sed -n '2,24p' "$0"; exit 1; }

[ $# -ge 2 ] || usage
GAME=$1; SRC1=$2; shift 2
SRC2=""
OUT=""
while [ $# -gt 0 ]; do
    case $1 in
        --out) [ $# -ge 2 ] || die "--out braucht einen Ordner"; OUT=$2; shift 2 ;;
        --*) die "Unbekannte Option: $1" ;;
        *) [ -z "$SRC2" ] || die "Mehr als zwei Quellen"; SRC2=$1; shift ;;
    esac
done

# Spiel → ScummVM-ID, Ordner, Titel, Filter, Pflichtdateien (Pfadmuster ohne Groß/Klein),
# Hinweisdateien (fehlen = Warnung).
case $GAME in
    broken-sword-1)
        ID=sword1; FOLDER=BaphometsFluch1; TITLE="Baphomets Fluch"; FILTER=scharf
        REQUIRED="swordres.rif general.clu compacts.clu scripts.clu"
        OPTIONAL="speech1.clu speech2.clu"
        SKIP="*/install/* */smackslo/* */directx/* *.dig *.lst dig.ini psetup.* univbe.drv uvconfig.dat" ;;
    broken-sword-2)
        ID=sword2; FOLDER=BaphometsFluch2; TITLE="Baphomets Fluch 2"; FILTER=scharf
        REQUIRED="players.clu resource.inf resource.tab cd.inf"
        OPTIONAL="speech1.clu music1.clu speech2.clu music2.clu"
        SKIP="*/install/* */bs_demo/* */directx/*" ;;
    beneath-a-steel-sky)
        ID=sky; FOLDER=BASS; TITLE="Beneath a Steel Sky"; FILTER=""
        REQUIRED="sky.dnr sky.dsk"
        OPTIONAL=""
        SKIP="" ;;
    *) die "Unbekanntes Spiel: $GAME (broken-sword-1, broken-sword-2, beneath-a-steel-sky)" ;;
esac

[ -d "$SRC1" ] || die "Quelle fehlt: $SRC1"
[ -z "$SRC2" ] || [ -d "$SRC2" ] || die "Quelle CD 2 fehlt: $SRC2"
REPO=$(cd "$(dirname "$0")/.." && pwd)
[ -n "$OUT" ] || OUT="$(dirname "$REPO")/original-data/quest/adventure/$FOLDER"
if [ -d "$OUT" ] && [ -n "$(ls -A "$OUT" 2>/dev/null)" ]; then
    die "Ziel ist nicht leer: $OUT (erst leeren oder --out wählen)"
fi
# Ziel nie innerhalb des Repos (Spieldaten gehören nie hinein).
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
case "$OUT/" in "$REPO"/*) rmdir "$OUT" 2>/dev/null || true; die "Ziel liegt im Repo: $OUT" ;; esac

# Dateien, die nie mitkommen (Programm, Installer, GOG-/Steam-Hülle, Autorun).
# Ohne Unterprozesse je Datei (Git Bash startet Programme langsam; Spielordner haben Hunderte).
# nocasematch statt ${x,,}: läuft auch mit Bash 3.2 (macOS).
shopt -s nocasematch
skip_file() {
    local path=$1 base
    base=${path##*/}
    case $base in
        *.exe|*.dll|*.com|*.bat|*.pif|*.lnk|*.url|*.ico|*.inf.bak|autorun.inf|goggame-*|unins*|setup*|\
        *.msi|*.cab|webcache.zip|gog.ico|support.ico|steam_api*.dll|*.vdf) return 0 ;;
    esac
    case $path in
        */__support/*|*/__redist/*|*/_commonredist/*|*/directx/*|*/dosbox/*|*/commonappdata/*) return 0 ;;
    esac
    # Spielbezogen (SKIP): Installer, Low-Res-Videos, Demo, DOS-Treiber — ScummVM braucht sie nicht.
    local pat hit=1
    set -f   # Muster nicht gegen das aktuelle Verzeichnis auflösen
    for pat in $SKIP; do
        # shellcheck disable=SC2254
        case $path in $pat|*/$pat) hit=0; break ;; esac
    done
    set +f
    return $hit
}

# copy_tree <quelle> <cd-nummer|leer>: alle Spieldateien mit Unterordnern übernehmen;
# speech.clu/music.clu werden bei zwei CDs zu speech<n>.clu/music<n>.clu.
copied=0
copy_tree() {
    local src=$1 cd=$2 rel dest base lower f
    while IFS= read -r -d '' f; do
        rel=${f#"$src"/}
        skip_file "/$rel" && continue   # führender / → Muster */ordner/* greifen auch ganz oben
        base=${rel##*/}
        lower=""
        case $base in speech.clu) lower=speech ;; music.clu) lower=music ;; esac
        if [ -n "$cd" ] && [ -n "$lower" ]; then
            case $rel in */*) rel="${rel%/*}/$lower$cd.clu" ;; *) rel="$lower$cd.clu" ;; esac
            echo "  umbenannt: $base (CD $cd) → $rel"
        fi
        dest="$OUT/$rel"
        if [ -e "$dest" ]; then
            cmp -s "$f" "$dest" || echo "  Hinweis: $rel liegt auf beiden Quellen verschieden — CD 1 bleibt"
            continue
        fi
        case $rel in */*) mkdir -p "${dest%/*}" ;; esac
        cp "$f" "$dest"
        copied=$((copied + 1))
    done < <(find "$src" -type f -print0 | sort -z)
}

# Neuere Fassungen früh erkennen (ScummVM: „Remastered edition is not supported“).
for marker in bs1dc.dat texture.dat texture.dic; do
    if [ -n "$(find "$SRC1" ${SRC2:+"$SRC2"} -type f -iname "$marker" -print -quit)" ]; then
        rmdir "$OUT" 2>/dev/null || true
        die "$marker gefunden — Director's Cut/Remastered. ScummVM braucht die Originalfassung (1996/97, CD)."
    fi
done

echo "== $TITLE ($ID) → $OUT"
if [ -n "$SRC2" ]; then
    copy_tree "$(cd "$SRC1" && pwd)" 1
    copy_tree "$(cd "$SRC2" && pwd)" 2
else
    copy_tree "$(cd "$SRC1" && pwd)" ""
fi
echo "  $copied Dateien übernommen"

# Prüfen (Quest-Dateisystem und ScummVM ignorieren Groß/Klein; Dateien dürfen in Unterordnern
# wie clusters/, speech/, music/ liegen).
has_file() { [ -n "$(find "$OUT" -type f -iname "$1" -print -quit)" ]; }
missing=""
for f in $REQUIRED; do has_file "$f" || missing="$missing $f"; done
if [ -n "$missing" ]; then
    echo "Fehler: Pflichtdateien fehlen:$missing" >&2
    echo "  Director's Cut / Remastered? Die laufen in ScummVM nicht — Originalfassung nehmen." >&2
    echo "  Teilergebnis bleibt zur Ansicht in $OUT" >&2
    exit 1
fi
for f in $OPTIONAL; do has_file "$f" || echo "  Warnung: $f fehlt (Sprache/Musik/zweite CD unvollständig?)"; done
case $ID in
    sword1|sword2)
        n=$(find "$OUT" -type f \( -iname '*.smk' -o -iname '*.dxa' -o -iname '*.mp2' \) | wc -l)
        [ "$n" -gt 0 ] || echo "  Warnung: keine Filme (*.smk/*.dxa) — Zwischensequenzen fehlen"
        ;;
esac

printf '%s\n' "$ID" > "$OUT/$ID.scummvm"
printf '%s\n' "$TITLE" > "$OUT/xrshell.title"
# Tastenprofil (Controller → Spieltasten), falls es eine Vorlage gibt.
if [ -f "$REPO/profiles/$GAME.keys" ]; then cp "$REPO/profiles/$GAME.keys" "$OUT/xrshell.keys"; fi
if [ -n "$FILTER" ]; then
    printf '# xr.adventure: Spiel-Standard (VR-Menü „Bild“ überschreibt pro Spiel)\nxrshell_filter = %s\n' \
        "$FILTER" > "$OUT/xrshell.opt"
fi
size=$(du -sh "$OUT" | cut -f1)
echo "Fertig: $OUT ($size)"
echo "Auf die Quest:  APP_ID=xr.adventure tools/push-game.sh \"$OUT\" $FOLDER"
