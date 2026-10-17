# TEST.md — XRShell Headset-Test (Quest 2/3)

Erster On-Device-Test nach M0 + M1. Ziel: virtueller Bildschirm, Controller-Interaktion
und ein DOS-Spiel per DOSBox-Pure in VR verifizieren.

## 1. Voraussetzungen

- Quest 2/3 mit **Developer Mode** (Meta-App → Gerät → Entwicklermodus)
- USB-Kabel, Quest eingeschaltet, am PC/Mac bestätigen („USB-Debugging erlauben")
- `adb` aus dem SDK: `$ANDROID_HOME/platform-tools/adb`
  (macOS: `/opt/homebrew/share/android-commandlinetools/platform-tools/adb`,
  Windows: `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`)

## 2. Sideload

Alle Befehle aus dem Repo-Stammverzeichnis, in einer POSIX-Shell (macOS-Terminal bzw.
Git Bash unter Windows):

```bash
ADB="$ANDROID_HOME/platform-tools/adb"
APK=android/build/outputs/apk/shell/debug/XRShell-shell-debug.apk   # andere Apps: <flavor>/debug/XRShell-<flavor>-debug.apk

$ADB devices                                  # Gerät muss als device gelistet sein
$ADB install -r "$APK"
```

In der Quest: **Bibliothek → Dropdown „Unbekannte Quellen" → xr.shell** (bzw. die jeweilige App).

## 3. Spiel ablegen (BYOA — eigenes Original)

XRShell sucht im Spiel-Verzeichnis `game/` in dieser Reihenfolge (alphabetisch sortiert):

1. **Datei direkt in `game/`** — ausdrückliche Wahl, wird direkt gestartet. Die Endung
   wählt den Core:
   - DOSBox-Pure: `.zip .dosz .exe .com .bat .iso .chd .cue .ins .img .ima .vhd .m3u .conf`
2. **Sonst der erste Unterordner** — DOSBox-Pure bindet ihn als Laufwerk C: ein und
   zeigt sein Startmenü, dort die Start-EXE per Cursor/Trigger auswählen
   (DOSBox-Pure merkt sich die Wahl).

Beispiel: DOS-Version von **Theme Park** als Ordner ablegen:

```bash
$ADB shell mkdir -p /sdcard/Android/data/xr.shell/files/game
$ADB push "/pfad/zu/deinem/ThemePark" /sdcard/Android/data/xr.shell/files/game/
# → game/ThemePark/…  (Startmenü von DOSBox-Pure)
# oder als ZIP / einzelne Startdatei direkt in game/:
$ADB push /pfad/ThemePark.zip /sdcard/Android/data/xr.shell/files/game/
```

Hinweis: Liegt eine Datei direkt in `game/`, gewinnt sie immer vor Ordnern. CD-Versionen
als `.iso/.cue/.img` funktionieren; der Ordnername ist frei wählbar.

Unter Windows bricht `adb push` eines Ordners ab — dann `tools/push-game.sh` nehmen
(überträgt als tar, legt optional ein Tastenprofil dazu und listet `game/`).

### 3a. Tastenprofil (`xrshell.keys`)

Eine Textdatei `xrshell.keys` im Spielordner (oder direkt in `game/`) legt die sonst
freien Controller-Eingaben auf Spieltasten. Vorlagen: `profiles/`.

```
x = LSHIFT                  # halten = Taste halten
right_stick_click = LALT+R  # Kombination, Tasten im Abstand von 50 ms
```

Eingaben: `a`, `x`, `y`, `left_trigger`, `right_stick_click`, `left_stick_left|right|up|down`
(linker Stick nur ohne linken Griff — mit Griff verstellt er weiter das Fenster), dazu ohne
Controller-Taste `hires` (VR-Menü → DOS) und `scroll_left|right|up|down` (Karte ziehen mit dem
rechten Griff in `xr.dosgames`, Standard Pfeiltasten — `docs/DOS-SPIELE.md`).
Ohne Belegung gilt im DOS-Mausmodus: **X** = Bildzoom, **Y** = Bildschirmtastatur an/aus
(für Namens- und Speicherfelder; DOS meldet keine Textfelder, daher kein automatisches Öffnen).
Vorlagen für Theme Park: `profiles/theme-park.keys` (A = R, Hi-Res 640×480) und
`profiles/theme-park.opt` (Soundblaster 16; dazu `MUSIC = SB16FM 388` in `THEME.CD/SNDSETUP.INF`).
Tasten: `A`–`Z`, `0`–`9`, `F1`–`F12`, `SPACE ENTER ESC TAB BACKSPACE INSERT DELETE HOME END
PAGEUP PAGEDOWN UP DOWN LEFT RIGHT LSHIFT RSHIFT LCTRL RCTRL LALT RALT PAUSE MINUS EQUALS COMMA PERIOD
KP_PLUS KP_MINUS`, Ziffernblock `KP0`–`KP9`.
Das Log zeigt jede erkannte Zeile (`Tastenprofil: …`) und Fehler mit Zeilennummer.

### 3b. Dungeon Keeper 1 (DOS-Version, z. B. GOG „Gold“)

Spielordner bauen und übertragen (Skript im Projektordner `XR-KeeperFX`, Daten aus der
eigenen GOG-Kopie unter `original-data/`):

```bash
../../XR-KeeperFX/quest/prepare-dk1.sh --lang german          # nur lokal bauen
../../XR-KeeperFX/quest/prepare-dk1.sh --lang german --push   # + auf die Quest (xr.shell)
```

Das Skript lässt GOG-DOSBox und Installer-Reste weg, legt `SAVE/` an, übernimmt die Sprache
wie GOGs Sprachmenü (Texte, Sprachausgabe aus dem eigenen CD-Image), kopiert
`profiles/dungeon-keeper-1.keys|.opt` als `xrshell.keys|.opt` und schreibt `AUTOBOOT.DBP`
(`C:\KEEPER.EXE`, `--start deeper` für Deeper Dungeons, `--start menu` = DOSBox-Pure-Startmenü).
`GAME.INS` + `game.gog` + `keeper0x.ogg` sind das CD-Image mit Musik (GOG-Cue-Format);
DOSBox Pure hängt es beim Start über Autostart/Startmenü als D: ein (OGG trotz „MP3“ in der Cue).

| Controller | Dungeon Keeper |
| --- | --- |
| Zeiger / Trigger / Griff oder B | Maus / Linksklick (greifen) / Rechtsklick (ablegen, schlagen) |
| Rechter Stick | Karte scrollen (Pfeiltasten); in Besessenheit laufen |
| Linker Stick links/rechts | Ansicht drehen (Entf / Bild ab) |
| Linker Stick vor/zurück | Zoom (Pos1 / Ende) |
| X halten | Shift (Shift+Klick = Kreatur übernehmen/Info) — ersetzt hier den Bildzoom |
| Y | Bildschirmtastatur (XRShell-Standard, z. B. Speichernamen) |
| A | hohe Auflösung an/aus (Alt+R) |
| Trigger links | Zum Dungeon-Herz (H) |
| Rechten Stick drücken | Optionen (Esc) |

Spielstände und Einstellungen: DOSBox Pure schreibt nie in den Spielordner, sondern in
`Android/data/xr.shell/files/saves/DungeonKeeper.pure.zip` (Union-Laufwerk C:) — ein erneutes
Übertragen des Spielordners lässt sie stehen. Gegenprobe am PC: `XR-KeeperFX/quest/desktop-check.ps1`
(GOG-DOSBox mit demselben Ordner; zeigt das deutsche Hauptmenü).

Zu prüfen: Musik (CD-Audio aus `.ogg`), Tempo in 640×480, Speichern → App beenden → Laden,
Mausblick in Besessenheit
(der Zeiger ist absolut und stößt am Bildrand an — ob das Umsehen reicht, klärt der Test).

### 3c. Rechte-Falle beim Ablegen

Ordner, die adb unter `Android/data/<paket>/` anlegt, gehören `shell` und sind für die App nicht
lesbar (Log: `Spielordner nicht lesbar … Permission denied`). Am einfachsten die App einmal starten
(sie legt `files/game/` selbst an), dann ablegen. Ordner-Spiele: `APP_ID=<App-ID> tools/push-game.sh …`
(Skript setzt die Rechte).

### 3d. VR-Menü (linke Menütaste, alle Apps)

Öffnet eine Tafel vor dem Bildschirm (sie hängt am Bild und dreht mit), das Spiel
pausiert. Bedienung mit Laser + Trigger: `<`/`>` stellen um, Klick auf die Zeile = eine
Stufe weiter. Menütaste erneut oder „Schließen“ = weiter spielen. Schrift: System-TTF der
Quest (Roboto) über stb_truetype, Rückfall 3×5-Pixelschrift. Einstellungen gelten pro App
und bleiben gespeichert (`xrshell-view.cfg` im App-Speicher), ebenso Größe und Abstand.

Reiter **Ansicht**:

| Eintrag | Werte |
| --- | --- |
| Passthrough | 0–100 % in 10er-Schritten (Kamerasicht als Hintergrund; „nicht verfügbar“ ohne Runtime-Support) |
| Wölbung | keine / leicht / mittel / stark (Bogen 0/30/60/90° über die Bildbreite) |
| Filter | scharf (Standard) / weich / Scanlines / CRT |
| Sättigung / Kontrast | 0–200 % / 50–150 % |
| Bildgröße / Abstand | 1,0–6,0 m in 0,2-m-Schritten / 0,7–3,5 m in 0,1-m-Schritten |
| Umgebung (exp.) | aus / Himmel / + Horizont / Nur Strecke — macht Himmel, dann alles bis zum Horizont, dann auch die Bodenfarben durchsichtig; der schwarze Bildrand verschwindet in allen Stufen (am besten mit Passthrough 100 %). Heuristik: häufigste Farbe im Himmelsband, bis zu 4 Bodenfarben an den Bildrändern, schwarze Randzeilen/-spalten |
| Fenster zentrieren | Standardgröße vor die aktuelle Blickrichtung |

Reiter **Zeiger** (gespeichert wie die Ansicht, außer Bildzoom):

| Eintrag | Werte |
| --- | --- |
| Farbe | Orange (Standard) / Weiß / Cyan / Grün / Magenta / Gelb — Strahl und Zielpunkt |
| Strahl | Normal / Dünn / Dick / Kurz (15 cm am Controller) / Aus (nur Zielpunkt) |
| Zielpunkt | Normal / Groß / Klein / Aus — „Aus“, wenn das Spiel selbst einen Mauszeiger zeigt |
| Bildzoom (X) | 1× / 1,5× / 2× / 3× — vergrößert einen Ausschnitt des Spielbilds |

**Taste X** (DOS-Modus, wenn `xrshell.keys` X nicht belegt) schaltet den Bildzoom weiter
und legt die Mitte des Ausschnitts auf die Stelle unter dem Strahl; bei 1× wieder das ganze
Bild. Klicks werden in den Ausschnitt umgerechnet (Frame-UV = Ausschnitt + Screen-UV / Zoom).

Die Quest-CPU läuft während der Session auf „sustained high“ (`XR_EXT_performance_settings`,
Log `CPU-Leistungsstufe`); ohne Vorgabe blieb sie auf Stufe 4 (1382 MHz), was Emulatoren bremst.

Reiter **Spiel**:

| Eintrag | Wirkung |
| --- | --- |
| Spielstand (Platz 1–5) + speichern / laden | libretro-Savestate unter `files/saves/<Spiel>.state<N>` |
| Profil (1–3) + speichern / laden | komplette Ansicht inkl. Größe/Abstand (`xrshell-profile<N>.cfg`) |
| Tastatur ein/aus | nur DOS-Spiele |
| Spiel beenden | zweimal klicken (Schutz vor Versehen) |

Fenster greifen: **linker Griff** halten — das Bild folgt der Hand mit Hebel (kleine
Bewegung = großer Weg, bis 5×) und dreht sich zum Kopf. **Rechten Griff dazu** und die
Hände auseinander/zusammen = größer/kleiner (bis 6 m Breite).

Zu prüfen: Schrift scharf, Laser zeigt in Zielrichtung und endet am Treffer, Greifen/Drehen,
Spielstand speichern → laden, Profil speichern → laden, Umgebung „Himmel“ und „+ Horizont“
mit Passthrough 100 %, Beenden.

## 4. Erwartetes Verhalten (Checkliste)

| # | Aktion | Erwartung |
| --- | --- | --- |
| 1 | App starten | Dunkler Raum, Bildschirm 2 m **genau vor deiner Blickrichtung**, Mitte knapp unter Augenhöhe (steht, wenn du dich bewegst) |
| 2 | Kein Spiel abgelegt | Schachbrett-Testmuster auf dem Screen (Fallback) |
| 3 | Rechten Controller bewegen | Orangener Laser + Cursor nur bei Treffer auf der Screen-Fläche |
| 4 | Linken Griff halten + linken Stick | links/rechts = breiter/schmaler, vor/zurück = weiter weg/näher; ohne Griff passiert nichts |
| 4b | Linken Griff halten + linken Stick drücken, vorher Kopf drehen | Bildschirm springt auf Standardgröße vor die neue Blickrichtung |
| 5 | Linke Menu-Taste → TASTATUR EIN/AUS | Tastatur-Overlay erscheint unterhalb des Screens (halbdurchsichtig) |
| 6 | Strahl auf Overlay-Taste + Trigger | Taste „drückt" sich; Eingabe geht ans Spiel, Maus wird unterdrückt |
| 7 | Spiel abgelegt + Neustart | DOSBox-Pure-Frame erscheint auf dem Screen statt Testmuster |
| 8 | Spiel mit Maussteuerung | Trigger = Linksklick, rechter Griff oder B = Rechtsklick, Aim = Mausposition; kurzes Vibrieren bei jedem Klick; Zeiger ruhig, Klick verreißt nicht |
| 8b | Trigger neben dem Screen drücken, gedrückt auf den Screen ziehen | Kein Klick im Spiel (erst loslassen, dann neu drücken) |
| 9 | Spiel mit Sound | Audio über AAudio (leichtes Crackeln bei Underruns = bekannt) |
| 10 | Rechter Stick | Pfeiltasten im Spiel (Theme Park: Karte scrollen), zusätzlich Joypad (vor = hoch) |

Beenden jederzeit: **Volume-Up**.

## 4a. Startbild und Ladeanzeige (alle Apps, Skill xr-loading-screen §5)

Zuerst mit xr.shooter 0.3.6 und xr.dosgames 0.3.7 (übrige Apps beim nächsten Build).

| # | Prüfpunkt | Erwartet | Log-Beleg |
|---|---|---|---|
| L1 | App in der Quest-Bibliothek antippen | Startbild mit `xr.<name>`, Version und „Start dauert etwa N s“ statt drei Punkten | – |
| L2/L5 | erste Sekunde in der App | Tafel an der Stelle des Spielfensters ≤ 1 s nach dem Startbild; Lichtpunkt wandert im Balken, „seit N s“ zählt | `Ladeanzeige sichtbar nach` |
| L3/L4 | Spiel aus der Liste wählen | Schritte mit Haken und Dauer, Restzeit; **zweiter Start** desselben Spiels: Restzeit stimmt auf ±30 % | `Ladeschritt …: N ms` |
| L6 | Systemcheck rechts | alles ✓; Controller ablegen → nach 3 s ⚠ „Controller in die Hand nehmen“ | `Systemcheck …` |
| L7 | Spielordner absichtlich umbenennen (z. B. Startdatei `DOOM2.WAD` → `X.WAD` am PC) | rote Fehlerseite mit `push-game.sh`-Befehl und Logpfad; **B** zeigt das Bild bzw. die Spieleliste; Menütaste öffnet das VR-Menü, Beenden geht | `Fehlerseite:` |
| L8 | Übergang zum Spiel | kein schwarzes Loch, Spiel-Bild direkt nach der Tafel; Trigger beim Übergang gehalten → kein Klick/Schuss, erst nach Loslassen | `Spiel sichtbar nach X s (…)` |
| DOS | DOS-Spiel mit AUTOBOOT (Doom II, Pirates! Gold) | Tafel bleibt über dem DOS-Textstart, verschwindet mit dem Grafikmodus; Textmodus-Programm: nach 3 s | `Spiel sichtbar nach … (Grafikmodus des Spiels)` |
| Wechsel | im Spiel Menü → anderes Spiel | Tafel mit „Vorheriges Spiel beenden“, dann das neue Spiel | `Laden: …` |
| Watchdog | (nur falls ein Start hängt) | nach 10 s gelber Hinweis, nach 30 s „Start hängt?“, Beenden im VR-Menü funktioniert | `Watchdog:` |

## 5. Logcat-Verifizierung (parallel laufen lassen)

```bash
$ADB logcat -s XRShell:*   # unser Log-Tag
```

**Ohne USB-Kabel getestet?** Jede XRShell-App schreibt dieselben Zeilen (Tags `XRShell`,
`XRShell-Core`) zusätzlich ins Sitzungslog auf der Brille:
`Android/data/<APP_ID>/files/logs/session-JJJJMMTT-HHMMSS.log` (Kopf: App-ID, Version, Gerät,
Sprache, Startzeit; letzte 10 Sitzungen, je Datei höchstens 5 MB, spätestens alle 2 s
gespeichert). Später abholen:

```bash
tools/pull-logs.sh               # alle XRShell-Apps → <XR>/Meta Quest 3/Logs/<Datum>/<APP_ID>/
tools/pull-logs.sh xr.dosgames   # nur eine App
```

Alle 10 s eine Leistungszeile: `Leistung: Spiel 70.0/70.1 fps, XR 72.0 fps, Bild 320x200, Filter hd`.

Erwartete Schlüsselmeldungen (Reihenfolge):

```
XRShell: XRShell startet: <APP_ID> <versionName> (versionCode <n>), <Gerät>, <Android>
XRShell: Sitzungslog: /storage/emulated/0/Android/data/<APP_ID>/files/logs/session-….log
XRShell: EGL bereit (GLES 3.0)
XRShell: XRShell initialisiert
XRShell: Core extrahiert / gefunden            (nur erster Start / danach: "Core gefunden")
XRShell: Core-Pixelformat akzeptiert           (XRGB8888 verhandelt)
XRShell: retro_load_game ok: <datei>
XRShell: Core-Thread gestartet (fps=...)
XRShell: AAudio läuft (<samplerate> Hz)
XRShell: Session läuft
```

Fehlerbehebung schnell:

| Log | Bedeutung |
| --- | --- |
| `Spielverzeichnis leer` / `kein Spiel` | Verzeichnis aus Schritt 3 prüfen ( Groß-/Kleinschreibung bei Dateiendung) |
| `dlsym fehlgeschlagen` | Core-Versionskonflikt → Issue anlegen |
| `Core-Pixelformat abgelehnt` | DOSBox-Pure-Meldet anderes Format → Issue anlegen |
| `AAudio Fehler` | Stream konnte nicht starten → Audio-Teil separat testen |

## 6. Bekannte Risiken / bewusste Einschränkungen (M1-Stand)

- **Maus-Glättung:** UV-Raycast pro Frame ist bei zitternder Hand sprunghaft; DOS-Menüs
  können empfindlich reagieren. Workaround beim Testen: Hand ruhig, langsam zielen.
- **Overlay-Lesbarkeit:** 3×5-Pixelfont in 2 m Abstand — falls zu klein, Issue #6 anlegen
  (Skalierung/Farben).
- **Kein Pause-Handling:** Session-Stop (z. B. Guardian-Loss) pausiert Core/Audio nicht.
- **Boolean-on-Float-Bindings** (`trigger/value`, `squeeze/value`) funktionieren auf der
  Quest, sind aber spezifikationswidrig — falls Pico/Android XR getestet wird, zuerst auf
  `/click`-Pfade umstellen.
- **Kein Core-Log:** DOSBox-Pure-Logs gehen aktuell verloren (`GET_LOG_INTERFACE`
  nicht verdrahtet). Falls Core-debugging nötig: Issue anlegen.

## 7. Nach dem Test

Ergebnisse (auch Teilerfolge/Absturz mit logcat-Auszug) als Kommentar ins passende
Issue oder neues Issue mit Label `bug`. Danach: M2 (SDL2→OpenXR-Shim) oder
Komfort-Fixes nach Befund.

### 3e. Tastenbelegung pro Spiel (Pad-Cores, Reiter „Tasten“)

VR-Menü → Reiter **Tasten** zeigt die Tasten des laufenden Pad-Cores (xr.shooter, DOS-Spiele mit
`xrshell_input = pad`). Taste antippen, dann die gewünschte Quest-Taste drücken (5 s Zeit; war die
Quest-Taste schon belegt, tauschen die beiden). Darunter je Stick: Steuerkreuz / Analog / Aus.
Änderungen gelten sofort und landen in `<Spiel>.pad`; **Für alle <System>-Spiele** schreibt
`games/<system>/_standard.pad`, **Zurücksetzen** verwirft erst die Spielbelegung, beim zweiten Mal
auch den Systemstandard. Belegbar: A, B, X, Y, beide Trigger, rechter Griff, beide Stick-Klicks
(der linke Griff bleibt Fenster-Greifen, die linke Menütaste das VR-Menü).

Host-Test der Core-Wahl: `clang++ -std=c++17 -Wall -Wextra -Werror -Isrc
tests/test_core_select.cpp -o t && ./t` (unter Windows llvm-mingw mit `-static`).

### 3i. Shooter — xr.shooter (PrBoom/ECWolf/TyrQuake) und DOS-Doom in xr.dosgames

Vorbereitung am PC (eigene GOG-/Steam-Kopie, siehe `docs/SHOOTER.md`, `docs/DOS-SPIELE.md` „Shooter“):

```powershell
powershell -File tools\prepare-shooter.ps1 -Source <Installer oder Spielordner>          # Weg 2
powershell -File tools\prepare-dos-games.ps1 -Source <entpackter Ordner> -Game doom2      # Weg 1 (nur mit DOS-EXE)
```

Auf die Quest (erst nach Ankündigung, Logcat läuft mit):

```bash
adb install -r android/build/outputs/apk/fps/debug/XRShell-fps-debug.apk     # xr.shooter
adb install -r android/build/outputs/apk/dos/debug/XRShell-dos-debug.apk     # xr.dosgames
S=../original-data/quest/shooter
APP_ID=xr.shooter SYSTEM=doom bash tools/push-game.sh "$S/doom/Doom2" Doom2
APP_ID=xr.dosgames bash tools/push-game.sh ../original-data/quest/dos/Doom2 Doom2
adb logcat -s XRShell XRShell-Core
```

Reihenfolge im Headset (je Punkt Screenshot/Log):

1. xr.shooter → Menü „Spiele“ → Doom II: Titelbild, Ton, Log `Spiel: …DOOM2.WAD → Core prboom`.
2. Laufen/drehen mit beiden Sticks stufenlos, Trigger rechts feuert, A öffnet Türen, B/X wechseln Waffe.
3. Doom-Menü (Y): Totenkopf mit dem linken Stick **ganz durchgedrückt** bewegen (ab 0,9), A wählt; Speichern/
   Laden; nach App-Neustart Spielstand da (`files/saves/DOOM2/`). „Quit Game“ → A (= Ja) → zurück in der
   Spielauswahl (Log `Spiel beendet sich selbst`).
4. Beenden über VR-Menü (Bestätigung), Laser nur bei offenem Menü.
5. xr.dosgames → Doom II (DOS): Belegung laut `docs/DOS-SPIELE.md` (DOS.YML), B-Waffenrad, **linken Stick
   drücken = DOSBox-Pure-Menü**, Ton (SB16/OPL), flüssig (`cycles = max`, `cycle_limit 0.85`).
   **Speichern:** F2 im Spiel (Bildschirmtastatur) → App neu starten → F3 lädt (Spielstand liegt im
   Spielordner, `DOOMSAV0.DSG`). DOS-Quake: Musik von der CD (`QUAKE.CUE`).
6. Vergleich beider Wege (Bildschärfe, Steuergefühl, Tempo) als Notiz für die Entscheidung.
7. **Bildwiederholrate (0.3.3):** Leistungszeile `Leistung: Spiel 72.0/72.0 fps, XR 72.0 fps` (Doom II,
   Quake) — vorher 71,6. Log `Bildwiederholrate 72 Hz für Core 72.00 fps`. Gleichmäßiges Drehen mit
   dem rechten Stick ohne Ruckler.
8. **Rumble:** Feuern (Trigger rechts) vibriert rechts kurz, Aufsammeln schwach; bei offenem VR-Menü
   und ohne Fokus keine Vibration.
9. **Brille ab/auf:** Brille absetzen → Log `Spiel pausiert (Grund: … Fokus/Session)`, kein Ton mehr,
   Spielzeit steht (Gegner bewegen sich nicht weiter); aufsetzen → `Spiel fortgesetzt`, `Session läuft`,
   wieder 72 Hz (`Bildwiederholrate …`), Leistungszeile wieder 72,0 fps ohne Aufhol-Sprung. Gleiches
   mit dem Quest-Systemmenü (Meta-Taste).
10. **Spielwechsel zwischen Cores:** xr.shooter Doom II → Quake → Doom II über „Spiele“ (ohne
    App-Neustart): jeder Start mit Bild und Ton, kein Absturz; xr.dosgames zwei Spiele nacheinander
    und „Spiel neu starten“ (DOSBox-Pure bleibt geladen, Log ohne `dlopen … fehlgeschlagen`).
    Bildschirmtastatur (Y in einem Maus-Spiel geöffnet) verschwindet beim Wechsel in ein Pad-Spiel.
11. **Start im Menü (0.3.6/0.3.5):** App starten → Spieleliste statt Spiel; Reiter „Spiel“ oben
    „Weiterspielen: <Titel>“ startet das zuletzt gespielte; Einstellung „Beim Start: Letztes Spiel“
    stellt das alte Verhalten her (überlebt App-Neustart). Quake: Reiter „Spiel“ ohne Spielstand-Knöpfe
    (TyrQuake kann keine Savestates) → Speichern im Quake-Menü.

Vor dem Headset ohne Brille: `tests/run-core-smoke.sh` (zuletzt 2026-10-04: 10 Spiele OK; Quest per USB; Berichte und PNGs unter
`tests/out/core-smoke/`, siehe `docs/SHOOTER.md` „Core-Smoke-Test“).

### 3f. Kern-Optionen (`xrshell.opt`) und xr.island (Windows 3.1 in DOSBox-Pure)

Eine Textdatei `xrshell.opt` im Spielordner (bzw. neben der Spieldatei, sonst in `game/`)
setzt libretro-Kern-Optionen (`RETRO_ENVIRONMENT_GET_VARIABLE`), eine pro Zeile:

```
dosbox_pure_mouse_input = direct     # Laser setzt den Cursor absolut
dosbox_pure_keyboard_layout = gr
```

Ohne Datei gilt der Core-Standard — mit einer Ausnahme: DOSBox-Pure steht auf Android
standardmäßig auf `pad` (Touchpad) und ignoriert dort relative Mausbewegung; XRShell
setzt deshalb `dosbox_pure_mouse_input = virtual`, solange `xrshell.opt` nichts anderes sagt.
Das Log zeigt jede gelesene Option (`Kern-Option: …`).

`direct` + `RETRO_DEVICE_POINTER`: XRShell meldet die Laserposition als absoluten Zeiger.
DOSBox-Pure reicht sie über das VMware-Mausprotokoll weiter — in Windows 3.x mit dem
Treiber `vmwmouse.drv` sitzt der Windows-Cursor damit genau unter dem Laser.

**xr.island** (Variante `island`, nur DOSBox-Pure) ist die App für ein Windows-3.1-
Aufbauspiel; Spielpaket und Ablauf im Nachbarprojekt `XR-HolidayIsland` (`docs/QUEST.md`):

```bash
ADB="$ANDROID_HOME/platform-tools/adb"
$ADB install -r android/build/outputs/apk/island/debug/XRShell-island-debug.apk
APP_ID=xr.island tools/push-game.sh "../../XR-HolidayIsland/original-data/quest/island" island
```

### 3g. Menü-Optik aus der Spielgrafik (`xrshell.theme`)

Liegt `xrshell.theme` im Spielordner (bzw. neben der Spieldatei), zeichnet das VR-Menü mit
Bildern aus den **eigenen Spieldateien** statt der Glas-Optik: Tafel, Kopfzeile, Zierleiste,
Knopf (dreiteilig gestreckt für Zeilen, Reiter und Aktionen), Pfeile normal/gedrückt, dazu
Farben für Text, Akzent (aktiver Reiter) und Hover. BMP, Schlüsselfarbe `transparent`
(Standard Magenta) wird durchsichtig. Format und alle Schlüssel: `src/menu_theme.h`.
Ohne Tafelbild oder ohne Datei bleibt die Standardoptik. Log: `Menü-Optik aus …: N Bilder`.

Beispiel (xr.island): `XR-HolidayIsland/scripts/prepare-quest.ps1` schneidet die Teile aus
der CD des Spielers nach `C:\XRTHEME\` und schreibt die Datei — nichts davon liegt im Repo.

**Bitmap-Schrift** (optional): `font` (Bild mit Zeichenzellen), `font_cell = BxH`,
`font_chars` (Zeichen in Zellenreihenfolge, UTF-8), `font_upper = 1`. Die Schrift wird
ganzzahlig vergrößert und behält ihre Farben; fehlende Zeichen zeichnet die Systemschrift.
Log: `Menü-Schrift: N Zeichen, Zelle BxH`.

**Theme Park (1994, GOG):** `tools/prepare-theme-park.ps1 -Source <entpackter GOG-Ordner>`
baut den kompletten Spielordner außerhalb des Repos (`../original-data/quest/1-ThemePark`):
Spieldaten, `THEME.BAT` (Sprache `-l1` = Deutsch), `SNDSETUP.INF` (SB16 + FM-Musik),
`xrshell.keys`/`.opt` aus `profiles/` und die Menü-Optik — Rasen aus dem Titelbild,
gelbe Knöpfe, blaue Spielschrift mit Umlauten. Danach
`bash tools/push-game.sh ../original-data/quest/1-ThemePark 1-ThemePark`.
