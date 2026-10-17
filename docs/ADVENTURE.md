# xr.adventure — Point-and-Click-Adventures (ScummVM)

App `xr.adventure` (Flavor `adventure`, #28): nur der ScummVM-libretro-Core (GPL-3.0+). Spieldaten
kommen ausschließlich aus eigenen Kopien des Spielers (BYOA), nie ins Repo oder ins APK.

## Spiele auf die Brille

Jedes Spiel ist ein Ordner unter `Android/data/xr.adventure/files/games/adventure/<Ordner>/` mit
den Spieldaten und **einer** Datei `*.scummvm`, deren Inhalt die ScummVM-Spiel-ID ist (z. B. `sky`
für Beneath a Steel Sky, Vorlage `profiles/beneath-a-steel-sky.scummvm`). Titel in der
Spielauswahl: `xrshell.title` im Ordner, sonst der Ordnername.

```bash
APP_ID=xr.adventure tools/push-game.sh <Spielordner> <Name>   # → files/games/adventure/<Name>/
```

Ordner aus der Zeit vor der Spielauswahl (`files/game/<Ordner>/` mit `*.scummvm`) erscheinen
weiterhin in der Liste — nichts muss verschoben werden. Später kommen weitere Adventures (z. B.
Syberia, Larry, Ultima) einfach als weitere Ordner dazu.

Start: das VR-Menü öffnet mit der Spieleliste; im Reiter „Spiel“ setzt **„Weiterspielen: <Titel>“**
das zuletzt gespielte Spiel fort (in der Liste mit „zuletzt“ markiert). **„Beim Start: Letztes Spiel“**
im selben Reiter startet es stattdessen sofort (früheres Verhalten).

## Sprache und Untertitel

Beim ersten Spielstart legt die App `files/system/scummvm.ini` (app-intern) an, falls sie fehlt:

```ini
[scummvm]
language=<Gerätesprache als ScummVM-Code>
subtitles=true
```

Dazu `gui_language=<Code>` (z. B. `de_DE`): Sprache des ScummVM-Menüs aus `translations.dat`
(`system/scummvm/theme/`, steckt in ScummVM.zip). Fehlt der Eintrag in einer vorhandenen ini,
trägt die App ihn nach; eine im ScummVM-Menü gewählte Sprache bleibt.

Gerätesprache aus der Android-Konfiguration; ScummVM-Codes weichen teils ab (ja → jp, ko → kr,
sv → se, cs → cz, pt-BR → br, no → nb, `src/scumm_setup.h`). Unbekannte Sprache: kein
`language`-Eintrag. Eine vorhandene Datei bleibt unangetastet (Log: `scummvm.ini vorhanden`).
Zurücksetzen: Datei löschen (`adb shell run-as xr.adventure rm files/system/scummvm.ini`).

## Bild

Standardfilter **HD (xBR)** wie bei xr.dosgames — für 320×200-Adventures. Filter, Sättigung,
Kontrast, Wölbung und Fenstermaße stellt das VR-Menü ein; sie gelten **pro Spiel**
(`files/settings/<Ordner>.user.opt`, Schlüssel `xrshell_*`). `xrshell_filter = scharf` in einer
`xrshell.opt` im Spielordner setzt einen anderen Spiel-Standard.

## Maus

Der ScummVM-Core liest die Maus relativ (`RETRO_DEVICE_MOUSE`, `scummvm_mouse_speed = 1.0`).
XRShell liefert Laser-Deltas in Pixeln des zuletzt gezeigten Bildes, mit Nachkomma-Rest (langsame
Bewegungen gehen nicht verloren). Damit bleibt der Spielzeiger unter dem Laser, solange beide
einmal übereinstimmen.

Analyse der Abweichung Laser ↔ Zeiger:

- **Start:** ScummVM setzt seinen Zeiger irgendwohin (meist Bildmitte) → Abstand zum Laser bleibt
  für immer. Abhilfe: beim Spielstart einmal **fangen** (Zeiger weit nach links oben schieben, das
  Backend klemmt ihn auf 0/0, dann Laserposition als ein Delta).
- **Wechsel ins ScummVM-Menü** (F5 bzw. Menüknopf): das Bild springt von 320×200 auf 640×480. Bisher
  rechnete der erste Poll danach `neu − alt` aus zwei verschiedenen Pixelräumen → Sprung um die
  halbe Bildbreite; je nach Backend-Version wird die Zeigerposition zudem nicht oder anders
  umgerechnet. Abhilfe: letzter Laserpunkt wird auf die neue Größe umgerechnet (gilt für alle
  Cores, auch DOS-Videomoduswechsel) und ScummVM fängt den Zeiger nach jedem Größenwechsel neu.
- **Laser verlässt das Bild:** kein Fangen (anders als xr.dosgames), denn ohne Klemmen bleibt die
  Übereinstimmung erhalten — und jedes Fangen schiebt den Zeiger kurz in die linke obere Ecke
  (Beneath a Steel Sky blendet dort die Inventarleiste ein).

Host-Test: `tests/test_mouse_relative.cpp` (`testScummSwitch`, drei Backend-Modelle: Pixel bleiben,
werden skaliert, springen in die Mitte). Restrisiko: ein Backend, das den Zeiger **ohne**
Größenwechsel versetzt (Menü in derselben Größe wie das Spiel, z. B. 640×480-Spiele), ist von außen
nicht erkennbar — dann hilft Laser kurz aus dem Bild und zurück nicht; im Headset beobachten.

## Im Headset zu prüfen

1. Neuinstallation: Spiel startet deutsch mit Untertiteln, Log `scummvm.ini angelegt: Sprache de`.
2. Bild startet mit HD-Filter; Filter im Menü ändern → nach Neustart der App noch so, ein zweites
   Spiel behält seinen eigenen Filter.
3. Zeiger sitzt nach dem Start unter dem Laser; ScummVM-Menü öffnen/schließen → Zeiger danach
   weiterhin unter dem Laser (Log `Maus: Bildgröße 320x200 → 640x480, Zeiger wird neu gefangen`).
4. Zweites Spiel in `games/adventure/` erscheint in der Spielauswahl; alter Ordner unter `game/`
   ebenfalls.

## Baphomets Fluch 1 und 2 (Broken Sword)

Beide Engines (`sword1`, `sword2`) stecken im mitgelieferten ScummVM-Core; Extra-Dateien sind
nicht nötig. Vorlagen: `profiles/broken-sword-1.scummvm` und `profiles/broken-sword-2.scummvm`.

**Nur die Originalfassungen (1996/1997) laufen.** „Director's Cut“ (BS1) und „Remastered“ (BS2)
sind andere Engines und werden von ScummVM nicht unterstützt. Die GOG-Pakete der neuen
Fassungen enthalten laut ScummVM-Spieleliste die Originalfassung als Beigabe — die wird genommen.

Benötigte Dateien (Groß-/Kleinschreibung egal, Unterordner wie im Original):

- **BS1:** `swordres.rif`, `clusters/` (u. a. `*.clu`, `*.cmp`), `speech/` und `music/`,
  Filme `video/` bzw. `smackshi/` (Smacker/DXA); beide CDs in einen Ordner.
- **BS2:** `players.clu`, `resource.inf`, `resource.tab`, `cd.inf`, `speech1.clu`, `music1.clu`,
  von CD 2 `speech2.clu` und `music2.clu` (auf CD 2 heißen sie `speech.clu`/`music.clu` →
  umbenennen), Filme aus `video/`.

`tools/prepare-adventure.sh` erledigt Prüfen, Umbenennen und Profil (Ziel standardmäßig
`../original-data/quest/adventure/<Ordner>`, nie im Repo):

```bash
tools/prepare-adventure.sh broken-sword-1 <BS1-Ordner bzw. CD 1> [<CD 2>]
tools/prepare-adventure.sh broken-sword-2 <CD 1> <CD 2>
APP_ID=xr.adventure tools/push-game.sh ../original-data/quest/adventure/BaphometsFluch2 BaphometsFluch2
```

Das Skript legt `sword1.scummvm`/`sword2.scummvm`, `xrshell.title` und `xrshell.opt` mit
`xrshell_filter = scharf` an (640×480-Spiele; HD/xBR ist für 320×200 gedacht). Das ScummVM-Menü
hat dieselbe Größe wie das Spiel; siehe Restrisiko unter „Maus“ (Zeiger nach Menü prüfen).

**CD-Images:** ISO mit 7-Zip entpacken (`7z x -o<ziel> <cd>.iso`, unter Git Bash Windows-Pfad per
`cygpath -w`). Die deutschen BS2-Images (Virgin, A707197) haben **2336 Byte pro Sektor** (Mode 2
ohne Sync/Header; 7-Zip: „Cannot open the file as archive“, Kennung `CD001` bei Offset 37385) →
je Sektor Byte 8–2055 übernehmen, dann ist es eine normale ISO:
`python -c "import sys;i=open(sys.argv[1],'rb');o=open(sys.argv[2],'wb');[o.write(b[8:2056]) for b in iter(lambda:i.read(2336),b'')]" cd1.img cd1.iso`.
Befund 2026-10-09 (Megamedia-BS1 + Virgin-BS2, deutsch): beide Spiele im ScummVM-Kern am PC
erkannt (sword1/sword2), Intro mit Bild 640×480 und Ton; Ordner je 1,1 GB.

## Bedienung

Laser = Zeiger, rechter Trigger = Linksklick, B = Rechtsklick, **linker oder rechter Griff = Fenster
greifen** (beide zusammen = größer/kleiner), X = Bildzoom, Y = Bildschirmtastatur. Spieltasten aus
`xrshell.keys` im Spielordner; Baphomets Fluch: A = ScummVM-Menü (Strg+F5), rechten Stick drücken =
Zwischensequenz überspringen (Esc).

## Spielstände

Der ScummVM-Core kann keine Schnellspeicherstände (`retro_serialize_size` = 0). Gespeichert wird
im ScummVM-Menü (Strg+F5: Laden, Speichern, Optionen): Reiter „Spiel“ → **„Spielmenü öffnen
(Speichern/Laden)“** oder A (mit Tastenprofil). Andere Taste: `game_menu = …` in `xrshell.keys`.
Spielstände liegen unter `files/saves/`.

## Lokaler Test am PC (ohne Quest)

`tests/core_smoketest.cpp` lädt den Core wie der Adapter (Startdatei `*.scummvm`, Systemordner,
Kern-Optionen) und lässt ihn N Frames laufen:

```bash
T=../../_tools; $T/llvm-mingw/bin/x86_64-w64-mingw32-clang++ -std=c++17 -O2 -Isrc -static     tests/core_smoketest.cpp -lwinmm -o $T/libretro-win/core_smoketest.exe
# Arbeitsordner: system/scummvm/{extra,theme} (aus android/core-download/scummvm-system),
# system/scummvm.ini, core.opt mit den scummDefaults aus libretro_adapter.cpp
SMOKE_LOG=info core_smoketest.exe scummvm_libretro.dll <Spielordner>/<id>.scummvm 900 bild.ppm
```

Befund 2026-10-03 (BASS, Core fcbce3ae-2026.3.1git, Windows): Spiel wird erkannt, Bild 320×200,
Ton läuft. „Spielstand-Rundlauf FEHLER“ ist erwartet (s. o.). Die Startgeometrie 1280×720 ist
nur der Vorgabewert des Cores, das erste Spielbild meldet 320×200. Der Core taktet sich selbst
(ScummVM-Thread bis zu ~8 ms je `retro_run`, Ton nach Uhrzeit) — die am PC gemessenen
Frames/s sind darum kein Maß für die Quest; dort zählt die Leistungszeile im Sitzungslog.
