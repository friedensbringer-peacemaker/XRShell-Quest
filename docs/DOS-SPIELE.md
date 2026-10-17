# DOS-Spiele in `xr.dosgames` (DOSBox-Pure, Spielauswahl)

Stand 2026-10-04 (Zeiger lernen als Weg für Spiele mit relativer Maus, Zeiger verschieben, HD-Filter, Einstellungen pro Spiel im VR-Menü, Karte ziehen mit dem rechten Griff; 0.3.3: Pause bei Brille ab, Core-Takt ohne Drift). `xr.dosgames` ist eine XRShell-App (Flavor `dos`, nur DOSBox-Pure) mit einer
Spielauswahl im VR-Menü. **Jeder Ordner** unter
`/sdcard/Android/data/xr.dosgames/files/games/dos/<Ordner>/` ist ein Spiel und wird als
Laufwerk C: gestartet. BYOA: die Ordner baut der Spieler aus seinen eigenen GOG-Käufen,
nichts davon liegt im Repo oder APK.

> Im Headset gelaufen sind bisher Pirates! Gold und Worms United (Befunde zum Mauszeiger, siehe
> `STATUS.md`); **abgenommen ist nichts** — alles hier ist gebaut / offline vorbereitet.
> Prüfpunkte am Ende.

## So funktioniert die Spielauswahl

| Was | Verhalten |
|---|---|
| Spiele finden | jeder Unterordner von `games/dos/` (versteckte `.`-Ordner nicht) |
| Titel | erste Zeile von `xrshell.title` im Ordner (UTF-8), sonst der Ordnername; sortiert nach Titel |
| Start | Menü (linke Menütaste) → Reiter **SPIELE** → Spiel anklicken bzw. mit Stick + A wählen |
| Spielwechsel / zurück zur Auswahl | jederzeit Menü → **SPIELE** → anderes Spiel; das laufende wird beendet (DOSBox-Pure schreibt bei Ordnern direkt in den Spielordner, siehe „Spielstände“ unten) |
| laufendes Spiel gewählt | Menü schließt, das Spiel läuft weiter (kein Neustart) |
| App-Start | öffnet die Auswahl; Reiter „Spiel“ → „Weiterspielen: <Titel>“ (letztes Spiel, in der Liste „zuletzt“). „Beim Start: Letztes Spiel“ setzt es stattdessen sofort fort |
| Profile | `xrshell.keys`, `xrshell.opt`, `xrshell.theme` werden **pro Spielordner** geladen (sonst eine Ebene höher in `games/dos/`) |
| Import | gibt es in `xr.dosgames` nicht (Ordner per `push-game.sh`), daher keine Zeile „Importieren“ |
| Beenden | Schnellleiste „Beenden“ (zweiter Klick „Sicher?“ innerhalb 3 s), Pflichtumfang wie alle XRShell-Apps |

Startlogik je Ordner: `AUTOBOOT.DBP` (eine Zeile `C:\XRSTART.BAT`, ASCII + CRLF) startet das
Spiel ohne DOSBox-Pure-Startmenü. Nur so hängt DOSBox-Pure ein CD-Image auf C:
(`*.ISO`, `GAME.INS` + `game.gog`) als Laufwerk D: ein.

## Start und Ladeanzeige (0.3.7)

- **Startbild:** die Quest zeigt beim Antippen `xr.dosgames 0.3.7 · Start dauert etwa 5 s`.
- **Startseite:** ab dem ersten XR-Frame steht an der Bildstelle eine Tafel mit Systemcheck
  (OpenXR, Controller, Spieldaten, Core, Speicher, Akku, Sitzungslog, letzter Start); davor öffnet
  sich die Spieleliste mit „Weiterspielen“ wie bisher.
- **Laden:** nach der Wahl zeigt die Tafel die Schritte (Vorheriges Spiel beenden beim Wechsel,
  Spieldaten prüfen, Core laden, Spiel laden, Erstes Bild) mit Balken, Lichtpunkt und Restzeit. DOSBox-Pure
  lädt in einem eigenen Thread.
- **Wann ist das Spiel „da“?** DOSBox-Pure liefert sofort Bilder (DOS-Start im Textmodus 640×400).
  Als Startsignal gilt der erste Frame mit Inhalt **in einer anderen Bildgröße als der erste Frame**
  — der Grafikmodus des Spiels nach `AUTOBOOT.DBP` (Smoke-Test Doom II/Quake: nach 84–95 Frames,
  ≈ 1,2–1,4 s). Bleibt das Programm im Textmodus (Setup, DOSBox-Pure-Startmenü), geht die Tafel
  3 s nach dem ersten Inhalt weg; nur schwarze Bilder: nach 8 s. So sieht man nicht den DOS-Start,
  wartet aber nie auf ein Bild, das nicht kommt.
- **Fehlerseite** (Spielordner fehlt/nicht lesbar, Core fehlt, Laden scheitert, Start hängt > 30 s):
  Klartext, `APP_ID=xr.dosgames tools/push-game.sh <Spielordner> <Name>`, Logpfad; **B** zeigt das Bild
  trotzdem und öffnet die Spieleliste, die Menütaste das VR-Menü.
- Ladezeiten pro Core in `files/xr-load-times.json`; Prüfpunkte in `TEST.md` (Ladeanzeige).

## Befund je Spiel (GOG-Installer, Offline-Analyse)

| Spiel (`-Game`) | Ordner | DOS? | Sprache | Sound (Effekte / Musik) | CD | Größe |
|---|---|---|---|---|---|---|
| Pirates! Gold (`pirates-gold`) | `PiratesGold` | ja (1993, X32-Extender, SVGA/VESA) | **Deutsch** (`piratesg.exe G`) | SB 220h/7/1 (GOG-`CONFIG.PIR` schon so) / SB-FM | ja: `DATA.DAT` → `PIRATESG.ISO` | 25 MB |
| Pirates! (`pirates`, Installer `432.02`) | `Pirates` | **ja** – das Original von 1987 (nicht der 3D-Titel von 2004) | Englisch (nur) | PC-Lautsprecher | nein | 1 MB |
| Populous + Promised Lands (`populous`) | `Populous` | ja (1989) | Englisch (nur) | Musik Roland MT-32 über MPU-401 → Soundfont `DOSBOX.SF2` | nein | 32 MB (davon 31 MB Soundfont) |
| Populous II (`populous2`) | `Populous2` | ja (1991) | Englisch (nur) | SB 220h/**5**/1 wie GOG (`POP2 /ss`), Intro AdLib/OPL | nein | 10 MB |
| SimCity 2000 SE (`simcity2000`) | `SimCity2000` | **ja** – DOS-Fassung (`SC2K.EXE`, DOS/4GW, VESA), keine Win95-Fassung | Englisch (nur) | SB16 220h/7/1 (`SC2000.CFG` von IRQ 5 gepatcht) / Roland MT-32 → Soundfont | ja: `SC2000SE.DAT` → `SC2000SE.ISO` (Intro-/WillTV-Videos) | 121 MB (31 MB Soundfont) |
| Syndicate Wars (`syndicate-wars`) | `SyndicateWars` | **ja** – Original-DOS-Fassung (`MAIN.EXE /w /g`), nicht die swars-Portierung | **Deutsch** (GOG-Sprachpaket `LANGUAGE\GER`, `CONFIG.INI` = `_GER`) | SB16 220h, IRQ/DMA aus `BLASTER` (7/1) / OPL (`SBPRO2.MDI`) | ja: `GAME.INS` + `game.gog` (mit CD-Audio-Spuren) | 916 MB |
| Worms United (`worms`, **Steam** App 70640) | `WormsUnited` | ja (1995/96, DOS/4GW) | Englisch (nur) | SB16 über `BLASTER` (220h/7/1) / Musik = CD-Audio | ja: `WORMS.CUE` = `WORMS.BIN` + `CDAUDIO\TRACK02–18.OGG` | 204 MB |

Nicht aufgenommen: *Populous: The Beginning* (Windows/Direct3D).

GOG-Startbefehle, aus denen `XRSTART.BAT` abgeleitet ist:

| Spiel | GOG (`dosbox*_single.conf`) | `XRSTART.BAT` in xr.dosgames |
|---|---|---|
| Pirates! Gold | `imgmount D DATA.DAT`, `D:`, `cd piratesg.cd`, `lh cdpatch`, `piratesg.exe [F\|G]` (Sprachmenü) | gleich, fest `piratesg.exe G` |
| Pirates! | `STUFFIT.COM +1 '1' +1 '1' +0 '3' +0 '1' +0 '2'`, `PIR.exe` (EGA-Variante, `machine=ega`) | gleich |
| Populous | `populous Vga Roland MT-32` (`cycles=7500`) | gleich |
| Populous II | `GO.BAT` = `Intro`, `Pop2 /ss /h` (`cycles=20000`) | gleich |
| SimCity 2000 | `imgmount d SC2000SE.DAT`, Launcher → `LOADFIX -1 SC2K` | `LOADFIX -1 SC2K`, Rücksprung 55 → `WILLTV` (wie `SC2000.BAT`) |
| Syndicate Wars | `imgmount d swars\game.ins`, `PLAY.BAT` (= Intro + `main /w /g`, Zyklen 20000/120000) | `SMACKPLY … INTRO.SMK`, `MAIN.EXE /w /g` (Zyklen `max`) |
| Worms United (Steam) | `imgmount d worms.cue`, `goworms.bat` → `WORMS.BAT` (`SET wormscfg/wormscd`, `BLACK`, `WRMS`) | gleich, feste Pfade `C:\WORMS\…` |

## Spielstände: DOSBox-Pure schreibt direkt in den Spielordner

Core-Smoke-Test 2026-10-04 (Quest, DOS-Doom II) und DOSBox-Pure-Quelle (`DBP_Mount`): Ist der Inhalt ein
**Ordner**, registriert DOSBox-Pure ihn direkt als beschreibbares Laufwerk C: — **kein** Union-Overlay,
**keine** `saves/<Ordner>.pure.zip` (die gibt es nur bei Images/ZIPs). Spielstände, `DEFAULT.CFG`,
`SETUP`-Ergebnisse landen also im Spielordner selbst. Per adb übertragene Ordner gehören `shell`; die App
greift als „andere“ zu → `tools/push-game.sh` setzt seit 2026-10-04 `chmod -R a+rwX` (vorher `a+rX`:
Speichern im Spiel schlug still fehl). Ältere Übertragungen auf der Quest nachträglich freigeben:
`adb shell chmod -R a+rwX /sdcard/Android/data/xr.dosgames/files/games/dos/<Ordner>`.

## Spielordner bauen

```powershell
# 1. Installer kopieren (Originale bleiben in Downloads) und entpacken
$X = '<XR-Ordner>'   # gemeinsamer Ordner aller XR-Projekte
$I = "$X\XR-Ports-Platform\original-data\_installer"
$G = "$X\XR-Ports-Platform\original-data\gog"
$IE = "$X\_tools\innoextract\innoextract.exe"
& $IE -e -d "$G\pirates-gold"   "$I\setup_pirates_gold_1.0_(28044).exe"
& $IE -e -d "$G\pirates"        "$I\setup_pirates_432.02_gog_(77605).exe"
& $IE -e -d "$G\populous"       "$I\setup_populous_with_promised_lands_1.0_(28044).exe"
& $IE -e -d "$G\populous2"      "$I\setup_populous_2_1.0_(28044).exe"
& $IE -e -d "$G\simcity2000"    "$I\setup_simcity_2000_special_edition_1.0se_(28187).exe"
& $IE -e --language=german -d "$G\syndicate-wars" "$I\setup_syndicate_wars_2.1.0.23.exe"

# 2. Startfertige Ordner → original-data\quest\dos\<Ordner>\
powershell -File tools\prepare-dos-games.ps1 -Source $G -Game all
#    einzeln: -Source $G\populous -Game populous [-Out <Ziel>]
```

Das Skript legt je Spiel ab: Spieldateien, `XRSTART.BAT`, `AUTOBOOT.DBP`, `xrshell.title`,
`xrshell.keys`/`xrshell.opt` aus `profiles\`, bei MT-32-Musik `DOSBOX.SF2`
(GeneralUser GS, Standard aus `XR-HolidayIsland\original-data\soundfont\`, `-SoundFont` ändert ihn).
Spiel-Konfiguration (auch bei `-ProfilesOnly`): SimCity 2000 `SC2000.CFG` = Effekte SB16 220h/7/1 und
Auflösung nach `-SimCityResolution 640x480|800x600|1024x768` (Standard **800x600** = `ve:32`).

## Auf die Quest (nur nach Ankündigung, Nutzer testet im Headset)

```bash
cd <XR-Ordner>/XR-Ports-Platform/XR-Ports-Platform   # gemeinsamer Ordner aller XR-Projekte
ADB=$ANDROID_HOME/platform-tools/adb
$ADB install -r android/build/outputs/apk/dos/debug/XRShell-dos-debug.apk
Q=$PWD/../original-data/quest/dos
for g in Pirates PiratesGold Populous Populous2 SimCity2000 SyndicateWars; do
    APP_ID=xr.dosgames bash tools/push-game.sh "$Q/$g" "$g"     # → files/games/dos/<g>/ (tar-Weg)
done
```

Syndicate Wars braucht beim Übertragen kurz ~2 GB frei (tar in `/data/local/tmp` + entpackt).

Nur Profile geändert (z. B. `xrshell.opt`): Ordner lokal erneuern und einzelne Dateien schieben
(Einzelne Dateien gehen mit `adb push` auch unter Windows):

```bash
powershell -File tools/prepare-dos-games.ps1 -Game all -ProfilesOnly   # ohne GOG-Quelle
for g in Pirates PiratesGold Populous Populous2 SimCity2000 SyndicateWars; do
    $ADB push "$Q/$g/xrshell.opt" "/sdcard/Android/data/xr.dosgames/files/games/dos/$g/xrshell.opt"
done
# SimCity 2000: Auflösung steht in der Spieldatei SC2000.CFG (nicht im Profil) – mitschieben
$ADB push "$Q/SimCity2000/SC2000.CFG" "/sdcard/Android/data/xr.dosgames/files/games/dos/SimCity2000/SC2000.CFG"
```

## Maus: Zeiger unter dem Laser (`direct` bzw. relativ mit Fangen)

Befund im Headset (2026-10-03): Strahl „oft meilenweit neben dem Mauszeiger“. Ursache: XRShell
setzt für DOSBox-Pure ohne Angabe `virtual`. Dann rechnet DOSBox-Pure die Frame-Pixel-Deltas des
Lasers über Mausgeschwindigkeit × Mickey-Rate (`pixelPerMickey`, `senv`) des Spiel-Maustreibers um
(`Mouse_CursorMoved(…, emulate = true)`): je nach Spiel läuft der Zeiger schneller/langsamer als der
Laser, klemmt am Rand und behält danach einen dauerhaften Versatz.

Erster Fix: die Profile setzen **`dosbox_pure_mouse_input = direct`** (für VESA-Spiele inzwischen
wieder `virtual`, siehe unten). DOSBox-Pure setzt dann die
INT-33-Position absolut aus `RETRO_DEVICE_POINTER` (`mouse.x = abs · max_x`, Textmodus
`abs · Spalten · 8`); Mickeys laufen relativ weiter (Spiele, die nur Mickeys lesen, bewegen sich wie
bisher). Abbildungskette geprüft (`src/pointer_map.h`, Host-Test `tests/test_pointer_map.cpp`):

| Glied | Ergebnis |
|---|---|
| Laser → Bildschirm-UV | gleiche Geometrie für Netz, Quad- und Zylinder-Ebene (Zylinder: Bogenlänge ∝ u, `centralAngle = Breite/Radius`) |
| Bildschirm-UV → Frame-UV | ganzer Frame, kein Rand-Beschnitt; Bildzoom (X) = derselbe Ausschnitt wie im Shader (Test über 4 Zoomstufen × 101 Mitten) |
| Seitenverhältnis | Fläche folgt dem Core-Seitenverhältnis, die UV-Abbildung bleibt linear → kein Fehler durch 320×200 → 16:10/4:3 |
| Frame-UV → Zeiger | −0x7FFF..0x7FFF, Rundweg wie DOSBox-Pure < 0,02 Mauspixel (Bereiche 199…1279), Spielpixel ±1 (320er-Spiel, Mausbereich 639) |
| Bildmitte | DOSBox-Pure verwirft (0, 0) ohne Druck → XRShell sendet dort x = 1 |
| Overscan / „padded“ | rechnet DOSBox-Pure **nicht** heraus → in den Profilen aus lassen (Standard); Log warnt bei der Kombination |

**Grenze von `direct`** (Headset 2026-10-03, Pirates! Gold in VESA 640×480: Spielzeiger so weit
neben dem Laser, dass kein Spiel startbar war). DOSBox-Pure `src/ints/mouse.cpp`:
`Mouse_AfterNewVideoMode` kennt nur die Modi 0x00–0x13 („Unhandled videomode“ → `max_x`/`max_y`
bleiben vom vorigen Modus, z. B. 639×199), und `direct` rechnet `mouse.x = abs · max_x` ohne
`min_x`/`min_y`. Den vom Spiel gesetzten Bereich (INT 33 Fkt. 7/8) kann XRShell nicht lesen – der
Mauszustand liegt im Core, nicht im emulierten Speicher, und libretro hat dafür keine Schnittstelle.
Ohne Core-Änderung bleibt daher: **VESA- und Mischspiele = relativ + Zeiger fangen**, `direct` nur für
Spiele, die durchgehend in Modus 13h/EGA laufen und keinen eigenen Bereich setzen.

**Relativ (`virtual`) in `xr.dosgames`** (`src/mouse_relative.h`, Host-Test `tests/test_mouse_relative.cpp`):

| Teil | Verhalten |
|---|---|
| Faktor pro Achse | Laser-Deltas × „Maus waagerecht“ / „Maus senkrecht“ (0,10–5,00, Schritt 0,05; VR-Menü → DOS, live, pro Spiel `xrshell_mouse_gain_x/_y`). Ersetzt im Menü `dosbox_pure_mouse_speed_factor` (eine Zahl für beide Achsen; Y-Verhältnis ist oft 2:1) – per `xrshell.opt` bleibt die Core-Option nutzbar |
| Zeiger fangen | −6000/−6000 senden (das Spiel klemmt den Zeiger links oben), 8 Frames warten (Spiele lesen die Maus seltener, als der Core läuft), dann Laserposition × Faktor als ein Delta. Ausgelöst beim **Schließen des VR-Menüs** und wenn der **Laser wieder aufs Bild kommt** (Laser kurz vom Bild nehmen = neu fangen). Während des Fangens (~0,13 s) gelten die Maustasten als los – ein Klick kommt danach an der richtigen Stelle an |
| Abgleichen | VR-Menü → DOS → „Abgleichen: Laser auf Spielzeiger + Trigger“: Menü schließt, der Spielzeiger wird auf 30 %/30 % des Bildes gefangen und eingefroren; mit dem Laser auf den **tatsächlichen** Spielzeiger zielen, Trigger (geht nicht ans Spiel) → Faktor · 0,3 / Zeigerposition je Achse, gespeichert, neu gefangen. Liegt der Faktor mehr als ~3× daneben, klemmt der Zeiger am Rand → einmal wiederholen. Ohne Trigger bricht der Abgleich nach 20 s ab |
| andere Apps | Faktor 1, kein Fangen – unverändert (`xr.shell`/Theme Park, `xr.island`) |

| Spiel | Grafik | Profil (Stand jetzt) | Erwartung |
|---|---|---|---|
| Pirates! Gold | VESA 640×480 (Log `Core-Geometrie: 640x480`) | **`virtual`** (vorher `direct`) | fangen + abgleichen; Fechten mit Zeigerhöhe prüfen |
| Pirates! (1987) | EGA 320×200 | `direct` | keine Maus (Tastatur) – ohne Wirkung |
| Populous | VGA 13h 320×200 | `direct` | sollte passen; setzt das Spiel einen eigenen Bereich (Fkt. 7/8), liegt der Zeiger versetzt → Menü: Mausmodus „Relativ“ |
| Populous II | VGA 13h 320×200 | `direct` | wie Populous |
| SimCity 2000 | VESA 800×600 (`VIDEOCARD=ve:32`) | **`virtual`** | fangen + abgleichen |
| Syndicate Wars | 13h im Einsatz, 640×480 in Menüs | **`virtual`** | fangen + abgleichen; nach Modus-/Auflösungswechsel Laser kurz vom Bild nehmen |

**Adapter-Standard bleibt `virtual`** (betrifft `xr.shell`, `xr.island`): Theme Park nutzt den
Standard und ist so im Headset bekannt, `xr.island` setzt `direct` selbst (Win 3.1 + `vmwmouse.drv`).

### Zeiger lernen (Spielpfeil an den Laser koppeln, seit 2026-10-03)

**Der vorgesehene Weg** für Spiele, deren Pfeil mit „Laser-genau“ (`direct`) nicht unter dem Strahl
liegt; „Abgleichen“ (Faktoren) bleibt nur der Rückfall, wenn das Lernen keinen Zeigerwert findet.

Für Spiele, die nur relative Mausbewegung lesen (Worms: mit „Laser-genau“ bleibt der Pfeil
stehen, relativ driftet er). VR-Menü → Reiter **Zeiger** → **„Zeiger lernen“** (Pfeil muss
sichtbar sein): das Menü schließt, XRShell schiebt die Maus ~1,5–2 s nach festem Muster (links oben,
kleine Schritte ±x/±y, dann bis zum Anschlag — Laser und Tasten gehen solange nicht ans Spiel) und
sucht im Arbeitsspeicher des Cores die Zeigerposition des Spiels (`src/cursor_lock.h`). Danach
regelt XRShell den Spielpfeil in jedem Bild unter den Laser — Faktor, Beschleunigung, Drift und
gebremste Zeiger spielen keine Rolle. Gespeichert pro Spiel (`xrshell_cursor_lock` in
`settings/<Ordner>.user.opt`, bis zu 4 Kopplungen), schaltet den Mausmodus auf „Relativ“.

Verhalten (am PC gegen das echte Worms geprüft, `tests/host/cursor_lock_harness.cpp`):

| Situation | Was passiert |
|---|---|
| Bildschirmwechsel (Worms setzt den Pfeil selbst, z. B. auf „EXIT“, und verwirft Mausbewegung) | Bewegung gilt nach ~0,2 s als verworfen und wird neu gesendet; Pfeil ist ~0,7 s nach dem Klick wieder am Laser |
| Film, Demo, Bildschirm ohne Pfeil | Kopplung **pausiert** (relative Maus), sucht selbst weiter |
| zurück auf einem Bildschirm mit Pfeil | koppelt wieder an: nach drei passenden Laserbewegungen oder per kleiner Probebewegung bei ruhigem Laser |
| App-Start mit gespeicherter Kopplung | erst pausiert, koppelt per Probebewegung in < 0,5 s an |
| mehrere Zeiger im Spiel (Worms: Menü, Waffenleiste in der Runde) | jeweils dort einmal lernen; XRShell wählt die Kopplung, die gerade mitläuft |

Worms: im Hauptmenü lernen (Pfeil-Bereich 0–317 × 0–167, Leiste darunter) und in der Runde mit
geöffneter Waffenleiste (rechte Maustaste; Bereich 16–296). In der Runde selbst scrollt die Maus
die Landschaft — dort nicht lernen. Menüzeile: „Zeiger gekoppelt – neu lernen“, „Kopplung pausiert
(sucht) – neu lernen“. Feinabgleich mit „Zeiger waagerecht/senkrecht“. Laser-Log zur Kontrolle:
`xrshell_log_pointer = 1` in der `xrshell.opt`.

Seit 0.3.3: Beim Schließen des Menüs über „Zeiger lernen“ wird der Zeiger **nicht** zusätzlich
gefangen (beides bewegte die Maus gleichzeitig); scheitert das Lernen, wird danach gefangen.
Stürzt das DOS-Programm ab (Core-Meldung mit „crash“) oder beendet sich der Core, verwirft XRShell
die Speicherkarte; ein Lernen, das über einen Programmwechsel läuft, bricht mit Meldung ab
(„Speicher gewechselt … erneut lernen“). Log: `Core-Meldung: …`, `Speicherkarte verworfen: …`.

### Zeiger verschieben (alle Maus-Spiele, seit 2026-10-03)

VR-Menü → Reiter **Zeiger** → **Zeiger waagerecht / Zeiger senkrecht**: schiebt die Eingabe in
0,25-%-Schritten der Bildbreite/-höhe (±15 %), bis der Spielpfeil genau unter dem Strahl liegt
(‹ = links/oben, › = rechts/unten; wirkt nach dem Schließen des Menüs). Gilt für `direct` und
`virtual`, gespeichert pro Spiel (`settings/<Ordner>.user.opt`: `xrshell_pointer_offset_x/y`),
Vorgabe auch per `xrshell.opt`. Erst den Mausmodus richtig wählen, dann fein nachschieben.

## Bild: HD-Filter (xBR) und Standardfilter pro Spiel

DOS-Frames sind meist 320×200; auf einem 2,4-m-Schirm wird jedes Spielpixel ~5 Displaypixel groß.
Neuer Filter **HD** (VR-Menü → Ansicht → Filter): Pixel-Art-Upscaler **xBR level 2** (Hyllian,
MIT) im Kopierpass, einstufig, beliebiger Maßstab, Kantenbreite = ein Ausgabepixel (`fwidth`),
nur innerhalb des Frames abgetastet (`texelFetch`, geklemmt). Dazu **HD-Scanlines** (HD + dunkle
Zeilen). Läuft als eigenes Shader-Programm; scheitert es beim Kompilieren, gilt HD → Scharf,
HD-Scanlines → Scanlines (Logzeile `HD-Filter (xBR) nicht verfügbar`).

Filterliste: Scharf (0) · Weich (1) · Scanlines (2) · CRT (3) · **HD (4)** · **HD-Scanlines (5)** –
gespeicherte Werte 0–3 bleiben gültig.

Standardfilter: `xrshell_filter = hd` in der `xrshell.opt` des Spielordners (Werte `scharf`,
`weich`, `scanlines`, `crt`, `hd`, `hd-scanlines` oder 0–5). Ohne Eintrag: `xr.dosgames` HD, alle anderen
Apps Scharf. In `xr.dosgames` gilt eine Filterwahl im Menü **pro Spiel** (Nutzerdatei, siehe
„Einstellungen pro Spiel“); ein früher global gewählter Filter (`filter_chosen = 1` in
`xrshell-view.cfg`) wird beim ersten Start einmal als Wahl des zuletzt gespielten Spiels übernommen.
Andere Apps: wer im Menü einen Filter wählt, behält ihn (`filter_chosen = 1`, global).

Kosten (Schätzung, nicht gemessen): 21 Texel-Lesezugriffe + ~150 Rechenoperationen je Ausgabepixel,
bei 320×200 × 6 = 1920×1200 ≈ 2,3 Mpx pro XR-Frame; Größenordnung 1 ms GPU auf der Quest 3.
Messen über die Leistungszeile bzw. OVR Metrics, falls das Bild ruckelt.

Referenzbilder (WebGL2 = GLSL ES 3.00, Shader-Quelltext direkt aus `src/quad_renderer.cpp`,
Testmuster 320×200, Ebenen-Pass mit Maßstab 6):

```
python tests/shader-preview/serve.py            # Repo-Stamm, dann im Browser öffnen:
http://localhost:8765/tests/shader-preview/     # rendert und speichert tests/out/shader-*.png
```

`tests/out/shader-hd-320x200.png`, `shader-hd-scanlines-320x200.png`, `shader-scharf-320x200.png`,
`shader-vergleich-scharf-hd.png` (Ausschnitt links Scharf, rechts HD), `shader-log.txt`.

## Höhere Spielauflösung (Befund offline, ohne Spielstart)

Textur-Grenze: Frames über **1024×768** verwirft XRShell (`Core-Frame … passt nicht`) → Bild steht.

| Spiel | Bildmodus | höhere Auflösung? | Umsetzung |
|---|---|---|---|
| SimCity 2000 SE | VESA, 256 Farben | **ja, per `SC2000.CFG`**. `VIDEOCARD`-Codes aus der Treibertabelle in `SC2K.EXE` (Code → Modusbeschreibung mit Breite/Höhe/Farbtiefe; Muster bestätigt an den Tseng-Codes `ts:30/31/32/34` = 640×350, 640×480, 800×600, 1024×768): `ve:20` 800×600×16, `ve:21` 1024×768×16, **`ve:31` 640×480×256** (GOG), **`ve:32` 800×600×256**, **`ve:33` 1024×768×256**, `ve:53`/`ve:55` 1024×768×256 (Variante) | **`ve:32` = 800×600** (Skript, `-SimCityResolution`). Begründung: auf dem 66°-Schirm ≈ 2 Displaypixel je Spielpixel (1024: ≈ 1,4 → dünne Schrift flimmert), 56 % mehr Stadt als 640×480; 1024×768 liegt genau an der Texturgrenze und bleibt Option. Profil: `dosbox_pure_machine = svga`, `dosbox_pure_svga = svga_s3` (GOG `machine=svga_s3`; S3 Trio64 mit VESA-BIOS, Core-Standard 2 MB Video-RAM reicht für 1024×768×256) |
| Syndicate Wars | Einsatz 320×200, Menüs/Briefing 640×480 | nur per **F8** sicher. `CONFIG.INI` kennt nur `CD`, `InstallDrive`, `Language`, `Data`, `Intro`, `Anims`, `Maps`, `Levels`, `Sound`, `DOS` (Parser-Strings in `MAIN.EXE`). Den Startschalter **`/H` gibt es im Original** (Sprungtabelle der Schalterauswertung: A–I, M, N, P–U, w), er setzt aber nur den aktuellen Bildmodus auf 13 = `MODE_640_480_8`. Den Einsatz-Modus wählt beim Missionsstart ein eigener Hi-Res-Merker (sonst Modus 1 = 320×200), den F8 umschaltet (F8-Abfrage: 320×200 ↔ 640×480 + Merker) | unverändert `MAIN.EXE /w /g` + F8 (VR-Menü → DOS → „Hohe Auflösung umschalten“). `/H` nicht gesetzt: Wirkung auf den Einsatz offline nicht belegbar |
| Pirates! Gold | VESA 640×480×256 fest (Handbuch „Video Cards and VESA Drivers“: 640×480, 256 Farben SVGA bzw. 16 Farben VGA) | nein | HD-Filter |
| Populous / Populous II | 320×200 VGA fest (`/h` = Detailstufe, nicht Auflösung) | nein | HD-Filter |
| Pirates! (1987) | EGA 320×200 | nein | HD-Filter |

Bildgröße: Standard 2,4 m Breite bei 2,0 m Abstand ≈ 62° Blickwinkel → ein 320er-Pixel ≈ 0,19° ≈
5 Displaypixel. Knopf **Bild → „Größe passend zum Spiel“**: 53° für Spiele unter 600 Pixel Breite
(320×200 bei 2,0 m Abstand → 2,0 m breit, alles ohne Kopfdrehen im Blick), 66° ab 640 Pixel. In
`xr.dosgames` gehören Größe/Abstand zum Spiel, sobald sie bewusst verstellt werden (Menü, Griff links,
„passend“, „Fenster zentrieren“); sonst bleibt das Fenster beim Spielwechsel, wo es ist.

## VR-Menü: Einstellungen pro Spiel (ohne PC)

Alles, was bisher nur per `xrshell.opt` ging, ist in `xr.dosgames` im VR-Menü einstellbar und wird
**pro Spielordner** gespeichert. Die mitgelieferten Profile bleiben unverändert.

Reiter (Vorschau ohne Headset: `tests/out/xrdos-menu-*.png`, erzeugt von `tests/run-host-tests.sh`):

| Reiter | Zeilen |
|---|---|
| Spiele | Spielauswahl (unverändert) |
| **Bild** (bisher „Ansicht“) | Passthrough · Wölbung · Filter (Scharf/Weich/Scanlines/CRT/HD/HD-Scanlines) · Sättigung · Kontrast · Bildgröße · Abstand · Umgebung (exp.) · **Größe passend zum Spiel** · Fenster zentrieren |
| Zeiger | Strahlfarbe · Strahl (… / Aus) · Zielpunkt (… / Aus) · Spiel-Mauszeiger · **Zeiger waagerecht / senkrecht** (Versatz ±15 %) · **Zeiger lernen** (bzw. „Zeiger gekoppelt – neu lernen“ / „Zeiger wird gelernt …“, nur mit laufendem Spiel) · Bildzoom |
| **Steuerung** (neu) | Rechter Griff (Karte ziehen / Rechtsklick / Aus) · Zieh-Art (Joystick / Hand folgt) · Zieh-Richtung (Natürlich / Wie Stick) · Zieh-Tempo 1–5 bzw. bei „Hand folgt“ Pixel je Tastendruck · Rechter Stick (Pfeiltasten / Aus); Zieh-Zeilen gedimmt, solange der Griff nicht „Karte ziehen“ ist — Abschnitt „Karte ziehen“ |
| **DOS** (neu) | Mausmodus (Laser-genau = `direct` / Relativ = `virtual`) · Maus waagerecht · Maus senkrecht (Faktoren, nur bei „Relativ“, sonst gedimmt) · CPU-Tempo (`dosbox_pure_cycles`: Auto, Maximum, 8088 … Athlon) · Seitenverhältnis (Pixel 1:1 / 4:3) · Grafikkarte (`dosbox_pure_svga`) · **Hohe Auflösung umschalten (Taste)** – nur wenn das Profil `hires = …` hat · **Abgleichen: Laser auf Spielzeiger + Trigger** (nur „Relativ“) · **Spiel neu starten** · **Spiel-Standard wiederherstellen** |
| Spiel | Spielstände, Ansichtsprofile, Tastatur (unverändert) |

Kopfzeile rechts: aktuelle Spielauflösung und Seitenverhältnis, z. B. `320×200 · 4:3`. Die Tafel ist
in `xr.dosgames` eine Zeile höher (880 statt 816 Texturpixel, ≈ 1,03 m), damit „Bild“ alles Optische fasst;
alle anderen Apps behalten ihr Menü unverändert.

| Option | wirkt | Wert-Quelle |
|---|---|---|
| Mausmodus, CPU-Tempo, Seitenverhältnis | **sofort** (`RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE` → DOSBox-Pure liest seine Optionen im nächsten Frame neu) | Liste, die der Core selbst meldet (`SET_VARIABLES`/`SET_CORE_OPTIONS*`, Log `Core-Option …`), Rückfall: aus der gebündelten `.so` gelesene Liste |
| Grafikkarte (SVGA-Chip) | **nach Neustart** (Core: „SVGA Mode (restart required)“) – Wert orange, Knopf heißt dann „Neu starten: Änderung übernehmen“ | wie oben |
| Hohe Auflösung | Menü schließt, die Profiltaste wird 0,15 s gedrückt, sobald das Spiel weiterläuft | `hires = F8` in `xrshell.keys` (Kombinationen wie `LALT+R` gehen) |
| Filter, Sättigung, Kontrast, Wölbung, Größe/Abstand, Mausfaktoren X/Y | sofort | Nutzerwert → `xrshell_…` in `xrshell.opt` → App-Standard |

Nicht angeboten (bewusst): Mausmodus „pad“/„aus“, Seitenverhältnis „padded“ (Rand, den die Maus
`direct` nicht herausrechnet), Speichergröße/Maschine (Neustart, riskant), Lautstärke.

**Neustart** und **Zurücksetzen** brauchen wie „Beenden“ einen zweiten Klick innerhalb 3 s (Knopf rot
gefüllt „Sicher? Nochmal klicken“). Neustart = Spiel beenden und denselben Ordner neu booten;
Schreibzugriffe auf C: liegen schon im Spielordner (direkt beschreibbares C:), **nicht im Spiel gespeicherter
Fortschritt geht verloren** (Statuszeile sagt das beim ersten Klick).

Speicherorte:

| Was | Datei | Inhalt |
|---|---|---|
| pro Spiel | `files/settings/<Ordner>.user.opt` (App-eigen, nicht im Spielordner; seit 0.3.3 Datei 0664/Ordner 0775, per adb lesbar) | Kern-Optionen aus dem Reiter DOS, `xrshell_filter`, `xrshell_saturation`, `xrshell_contrast`, `xrshell_curve`, `xrshell_width`/`xrshell_distance`, `xrshell_mouse_gain_x`/`_y`, Reiter Zeiger: `xrshell_pointer_offset_x`/`_y`, `xrshell_cursor_lock` (gelernte Kopplungen, bis zu 4), Reiter Steuerung: `xrshell_grip_right`, `xrshell_drag_mode`, `xrshell_drag_direction`, `xrshell_drag_speed`, `xrshell_drag_step`, `xrshell_stick_right` |
| global (App) | `xrshell-view.cfg` (intern) | Passthrough, Umgebung, Strahlfarbe/Strahl/Zielpunkt, Spiel-Mauszeiger; Ansichtsprofile 1–3 |
| flüchtig | – | Bildzoom (X) |

Ladereihenfolge: `xrshell.opt` des Spielordners, dann die Nutzerdatei (überschreibt). „Spiel-Standard
wiederherstellen“ löscht die Nutzerdatei; Live-Optionen springen sofort zurück, die Grafikkarte erst
nach dem Neustart. Profile dürfen Bild-Standards setzen (`xrshell_saturation = 110` usw.).
Logzeilen: `Nutzer-Optionen aus …`, `DOS-Option … (sofort|nach Neustart)`, `Kern-Optionen geändert —
Core liest neu`, `Bild des Spiels: …`.

Andere Apps mit DOSBox-Pure (`xr.shell` mit Theme Park, `xr.island` mit Win 3.1) bekommen den Reiter
bewusst nicht: dort hängen Maus (`direct` + VMware-Treiber), Speicher und Tempo an festen, im
Headset bekannten Profilen; ein Menüwert pro Spiel würde dort nur neue Fehlerquellen öffnen.

## Karte ziehen mit dem rechten Griff

Nutzerwunsch aus dem Headset (SimCity 2000): das Spielfeld nicht nur mit dem rechten Stick, sondern
durch **Griff halten und Hand bewegen** verschieben. DOS-Spiele scrollen nur über Tasten; XRShell
übersetzt den Handweg deshalb in die Scroll-Tasten des Spiels (`src/map_drag.h`, Host-Test
`tests/test_map_drag.cpp`).

| Was | Verhalten |
|---|---|
| Greifen | rechten Griff drücken und halten: der Greifpunkt ist die Mitte eines unsichtbaren Sticks, kurzer Haptik-Impuls |
| Joystick (Standard) | Handweg auf der Bildebene (waagerecht/senkrecht, Vor/Zurück zählt nicht) über der Totzone → Scroll-Taste im Takt: 60 ms gedrückt, dann Pause; knapp über der Totzone alle 0,40 s, mit mehr Weg schneller bis alle 0,12 s, ab 85 % des Vollausschlags **Dauerdruck**. Jeder Takt gibt einen leisen Haptik-Tick |
| Diagonale | Winkel innerhalb ±22,5° um 45° → zwei Tasten im selben Takt (Hysterese: bleibt bis ~17°) |
| Zieh-Tempo (Totzone / Vollausschlag) | 1 ruhig 3,0 / 20 cm · 2: 2,5 / 15 cm · **3 mittel 2,0 / 12 cm** · 4: 1,5 / 9 cm · 5 flink 1,2 / 7 cm; losgelassen wird erst unter 75 % der Totzone |
| Richtung | **Natürlich** (Standard): die Karte folgt der Hand — Hand nach links = Ansicht nach rechts (Taste →), Hand hoch = Ansicht runter (↓). **Wie Stick**: Hand nach links = Taste ← |
| Hand folgt (experimentell) | „Karte klebt an der Hand“: Handweg mit demselben Hebel wie beim Fenster greifen (Kopf→Bild / Kopf→Hand, 1–5×) in Spielpixel, geteilt durch **Pixel je Tastendruck** (2–64, Standard 16) = Zahl der Tastendrücke; ein Schritt, sobald 0,75 Schritte offen sind (Zittern löst nichts aus), zurück zum Greifpunkt = alle Schritte zurück. Höchstens ~8 Schritte/s je Achse (60 ms Druck + 60 ms Pause) |
| Während des Ziehens | keine Maustasten (Trigger/B wirken erst nach dem Loslassen), der Spielzeiger bleibt stehen; rechter Stick ruht (gleiche Tasten) |
| Loslassen | Griff los → Tasten sofort aus (jede gedrückte Taste mindestens 50 ms, damit der Core sie sieht), Haptik-Impuls. Menü öffnen, Spielwechsel, Pause, Fokusverlust, Controller verliert Tracking → sofort alles los; danach wirkt der Griff erst nach erneutem Drücken |
| Konflikte | Linker Griff zuerst + rechter Griff = Fenster skalieren (wie bisher), kein Ziehen. Ziehen beginnt nicht, solange eine Maustaste gehalten wird. Rechtsklick bleibt auf **B** |
| Scroll-Tasten | Standard Pfeiltasten; pro Spiel in `xrshell.keys`: `scroll_left`, `scroll_right`, `scroll_up`, `scroll_down` (Kombinationen wie `LCTRL+LEFT` möglich, Ziffernblock `KP0`–`KP9`) |
| andere Apps | unverändert: Griff rechts = Rechtsklick, kein Reiter Steuerung |

Warum „Hand folgt“ nicht Standard ist: Ein Tastendruck scrollt nur in manchen Spielen einen festen
Weg; viele scrollen, **solange** die Taste unten ist (Weg hängt am Spieltakt), und der Core sieht
Tastendrücke unter ~50 ms nicht. Mehr als ~8 Schritte/s gehen deshalb nicht, und der Weg pro Schritt
muss je Spiel mit „Pixel je Tastendruck“ eingestellt werden. Der Joystick braucht nichts davon.

Profil-Standards (`xrshell_grip_right` in `xrshell.opt`, Scroll-Tasten in `xrshell.keys`):

| Spiel | Rechter Griff | Scroll-Tasten | Quelle |
|---|---|---|---|
| SimCity 2000 | Karte ziehen | Pfeiltasten | `README.TXT` „Cursor Keys - scroll around your city“ |
| Syndicate Wars | Karte ziehen | Pfeiltasten | Steuerungsübersicht der Fan-Seite (Pfeiltasten = Ansicht hoch/runter/links/rechts); Handbuch nur als Scan |
| Populous II | Karte ziehen | Pfeiltasten | Handbuch S. 29/30 „scroll with the cursor keys or the numeric keypad“ |
| Populous | Karte ziehen | **Ziffernblock 4/6/8/2** | Handbuch S. 12 „Direction Icons“: Ziffernblock statt der Richtungspfeile (laut Handbuch langsamer); Pfeiltasten nennt es nicht. Richtung auf der isometrischen Karte im Headset prüfen |
| Pirates! Gold | **Rechtsklick** | – | keine Karte zum Ziehen; Griff = Stoß beim Fechten wie bisher |
| Pirates! (1987) | **Aus** | – | kein Mausspiel |

Ohne Eintrag gilt in `xr.dosgames` „Karte ziehen“, Joystick, natürlich, Tempo 3, rechter Stick =
Pfeiltasten. Logzeilen: `Steuerung des Spiels: …`, `Steuerung: xrshell_… = …`, `Karte ziehen: gegriffen
(Joystick, Hebel …)`, `Karte ziehen: losgelassen`.

## Tastenbelegung

Grundbelegung aller DOS-Spiele (XRShell): **Laser** = Mauszeiger, **Trigger rechts** = linke
Maustaste, **B** = rechte Maustaste, **Griff rechts** halten = Karte ziehen (pro Spiel: Rechtsklick
oder aus, siehe oben), **rechter Stick** = Pfeiltasten (pro Spiel abschaltbar),
**X** = Bildzoom um den Strahlpunkt, **Y** = Bildschirmtastatur, **linke Menütaste** = VR-Menü,
**Griff links** + Stick links = Fenster verschieben/skalieren, Griff links + dann Griff rechts =
Fenster skalieren. Die Profile belegen nur A, Trigger links, rechten Stick drücken und den linken
Stick (und die Scroll-Tasten); X und Y bleiben überall frei.

### Pirates! Gold (`profiles/pirates-gold.keys`)

| Eingabe | Taste | Wirkung (Handbuch Teil II) |
|---|---|---|
| Stick links ← → | ← → | auf See drehen, in Menüs wählen |
| Stick links ↑ ↓ | ↑ ↓ | Segel setzen / reffen |
| A | Enter | auswählen, Kanone feuern |
| Trigger links | Leertaste | Kapitänskajüte (im Seegefecht: feuern) |
| rechten Stick drücken | Esc | Bildschirm verlassen |
| Trigger / B oder Griff rechts | linke / rechte Maustaste | Fechten mit der Maus: Zeigerhöhe = Linie, LMB Hieb, RMB Stoß |

Optionen: SB16 `A220 I7 D1 H5`, 32 MB Speicher (GOG: 30 MB), `xrshell_grip_right = rightclick`.

### Pirates! 1987 (`profiles/pirates.keys`)

| Eingabe | Taste | Wirkung |
|---|---|---|
| Stick links / Stick rechts | Pfeiltasten | Richtung (Schiff, Figur, Auswahl) |
| A | Enter | Feuerknopf (bestätigen, Säbel) – rechte Hand mit rechtem Stick |
| Trigger links | Enter | Feuerknopf für die linke Hand (linker Stick + Feuer, z. B. Fechten) |
| rechten Stick drücken | Esc | zurück |

Kein Mausspiel in der GOG-Einstellung (Startfrage „1) JOYSTICK 2) KEYBOARD“, `STUFFIT` antwortet 2;
Laser/Trigger ohne Wirkung). **Feuerknopf = Enter**: im Tastaturmodus zeigt das Spiel „Press ENTER“
statt „Press TRIGGER“ (Textpaar `TRIGGER`/`ENTER` in `DISKP`); Richtung = Pfeiltasten (Handbuch S. 5
„keyboard cursor keys“). Die Tastenkarte „Captain's Broadsheet“ fehlt im GOG-PDF.
Optionen: `dosbox_pure_machine = ega` (wie GOG), **`dosbox_pure_cycles = 2750`** (GOG `cycles=auto`
= 3000 Zyklen für Real-Mode-Spiele laut DOSBox-0.74-Handbuch; nächste DOSBox-Pure-Stufe),
`xrshell_grip_right = off` (Griff ohne Funktion).

### Populous (`profiles/populous.keys`)

| Eingabe | Wirkung |
|---|---|
| Trigger | linke Maustaste: Land heben, Symbole wählen |
| B | rechte Maustaste: Land senken |
| Griff rechts halten + Hand | Karte ziehen über Ziffernblock 4/6/8/2 (`scroll_* = KP…`, Handbuch S. 12) |
| Y | Bildschirmtastatur (Weltname) |

Außer dem Ziffernblock (Kartenrichtung) nennt das Handbuch Tasten nur für den Malmodus (S. 9:
F1–F8, Entf, Shift+F1/F2/F6/F7, 1–4) – über die Bildschirmtastatur; das Profil belegt keine
Controller-Taste. Optionen: `cycles = 7800` (GOG 7500 fest; DOSBox-Pure kennt nur Stufen
315/1320/2750/4720/7800/13400/26800/…, 7800 = +4 %, 4720 wäre −37 %), SB16 220/7/1, Musik über
`DOSBOX.SF2`, `xrshell_grip_right = drag`.

### Populous II (`profiles/populous-2.keys`)

| Eingabe | Taste | Wirkung („Keyboard Command Summary For The IBM Version“) |
|---|---|---|
| Stick links ↑ | Ziffernblock − | heranzoomen |
| Stick links ↓ | Ziffernblock + | herauszoomen |
| Stick links ← | − | Spiel langsamer |
| Stick links → | Shift + = (= „+“) | Spiel schneller |
| A | F7 | Vollbild / normale Ansicht |
| Trigger links | F10 | Pause |
| Stick rechts bzw. Griff rechts halten + Hand | Pfeiltasten | Karte scrollen |
| Trigger / B | Maus links / rechts | Land heben / senken, Symbole |

Weitere Kürzel des Handbuchs (F1–F6 Effektgruppen, 1–5 Eingriffe) über die Bildschirmtastatur (Y).
Optionen: `cycles = 26800` (GOG 20000; Stufen daneben 13400 = −33 % / 26800 = +34 %; das Spieltempo
regelt Populous II selbst mit −/+, mehr Zyklen glätten nur Bildaufbau und Scrollen), SB16 `A220 I5 D1 H5`
(GOG-Werte, `SB.BIN` ohne Setup-Datei).

### SimCity 2000 (`profiles/simcity-2000.keys`)

| Eingabe | Taste | Wirkung |
|---|---|---|
| Trigger links (halten) | Shift | Shift-Klick: Abfrage/Hilfe, Bauen abbrechen |
| A (halten) | Strg | Strg-Klick: Stadt zentrieren |
| rechten Stick drücken | Esc | Dialog schließen |
| Trigger | linke Maustaste | bauen, ziehen, Menüs |
| B | rechte Maustaste | |
| Griff rechts halten + Hand / Stick rechts | Pfeiltasten | Stadt scrollen (README „Cursor Keys“) |

Optionen: SB16 220/7/1, `cycles = max`, `machine = svga`, `svga = svga_s3`; Grafik 800×600
(`SC2000.CFG` `VIDEOCARD=ve:32`, siehe „Höhere Spielauflösung“); Musik MT-32 über `DOSBOX.SF2`.

### Syndicate Wars (`profiles/syndicate-wars.keys`)

| Eingabe | Taste | Wirkung (Handbuch „Main Display“) |
|---|---|---|
| Stick links ← / → | Entf / Bild ab | Ansicht drehen |
| Stick links ↑ / ↓ | Pos1 / Ende | zoomen (Richtung im Headset prüfen) |
| A | Bild auf | Blickwinkel kippen |
| Trigger links | Einfg | Blickwinkel zurückkippen |
| rechten Stick drücken | Esc | Missionsmenü |
| Trigger | linke Maustaste | Agent gehen lassen (doppelt = rennen), aufheben, wählen |
| B | rechte Maustaste | feuern (halten = aufladen) |
| Griff rechts halten + Hand / Stick rechts | Pfeiltasten | Ansicht verschieben |
| VR-Menü → DOS → Hohe Auflösung | F8 (`hires = F8`) | 320×200 ↔ 640×480 im Einsatz (kein Startschalter, siehe „Höhere Spielauflösung“) |

Optionen: `cycles = max`, SB16 220/7/1.

### Worms United (`profiles/worms.keys`)

| Eingabe | Taste | Wirkung |
|---|---|---|
| Stick rechts ← / → | Pfeiltasten | laufen |
| Stick rechts ↑ / ↓ | Pfeiltasten | zielen |
| A (halten) | Leertaste | feuern, Halten lädt die Kraft auf |
| Trigger links | Enter | springen, im Menü bestätigen |
| Stick links ↓ | Rücktaste | Rückwärtssalto |
| Stick links ← | Tab | nächster Wurm (wenn der Spielmodus es erlaubt) |
| rechten Stick drücken | Esc | Rundenmenü / zurück |
| Trigger / B | linke / rechte Maustaste | Waffe wählen, Ziel setzen (Luftschlag, Teleport …) |
| Y → Bildschirmtastatur | 1–5 | Zündzeit |

Optionen: `cycles = max`, SB16 220/7/1, Maus `virtual` + „Abgleichen“ (Worms liest nur Mickeys – mit `direct` bleibt der Pfeil oben links stehen), Filter HD.

### Quiz-Reihe unter Windows 3.1 (`profiles/ydkj.keys`)

Ordner `YDKJ2` baut `XR-YDKJ\scripts\prepare-quest.ps1` aus einer am PC in DOSBox-X installierten
Windows-3.1-Kopie (Vorlage XR-HolidayIsland), nicht dieses Skript. Reine Tastatur, eine Person =
Spieler 1.

| Eingabe | Taste | Wirkung |
|---|---|---|
| A | Q | Buzzer Spieler 1 (auch Jack Attack) |
| Stick links ↑ → ↓ ← | 1 2 3 4 | Antwort, Spielerzahl, Kategorie (im Uhrzeigersinn ab oben) |
| Trigger links | Enter | Eingabe bestätigen |
| rechten Stick drücken | Leertaste | Zwischenspiel überspringen |
| Y → Bildschirmtastatur (QWERTZ) | Buchstaben | „Fill in the blank“-Antworten tippen |

Optionen: Maus `direct` (VMware-Treiber in Windows), `keyboard_layout = gr` (wie Windows `kbdgr.dll`,
sonst passen Y/Z der Bildschirmtastatur nicht), 16 MB, SB16 220/7/1. Im Paket ist die
Windows-Auslagerungsdatei abgeschaltet (`Paging=No`), damit Windows bei jedem Start keine 10+ MB
`WIN386.SWP` in den Spielordner schreibt (DOSBox-Pure hängt den Ordner direkt als beschreibbares C: ein,
siehe „Spielstände“). Dieselbe Begründung gilt für Holiday Island (`xr.island`), dort ist die
Auslagerungsdatei noch an. Nach ~20 s ohne Eingabe in der Spielerauswahl startet das Spiel eine
Vorführrunde.

## Im Headset zu prüfen

1. `xr.dosgames` startet ohne Spiel mit offener Auswahl; Hinweis bei leerer Liste.
2. Alle Titel (sechs GOG-Spiele + Worms United) erscheinen mit Namen aus `xrshell.title`, Plakette „DOS“.
3. Je Spiel: Start über AUTOBOOT (kein DOSBox-Pure-Menü), Bild, Ton (Effekte **und** Musik), Maus unter dem Laser.
4. CD-Spiele: D: ist eingehängt (Pirates! Gold startet überhaupt; SimCity-Intro-Video; Syndicate-Wars-Intro + CD-Musik).
5. Sprache: Pirates! Gold und Syndicate Wars deutsch.
6. Worms United: Intro-Film von D:, CD-Musik, Waffenwahl/Zielen mit dem Laser (Maus `virtual` trifft?), Feuern mit gehaltenem A lädt die Kraft auf.
6. Spielwechsel über Menü → SPIELE; Tastenprofil wechselt mit (z. B. Populous II: A = Vollbild).
7. Laufendes Spiel in der Auswahl anklicken → läuft weiter.
8. App neu starten → Auswahl mit „Weiterspielen: <Titel>“; Knopf setzt das Spiel fort, Spielstände (DOS-Saves) bleiben.
9. Pirates! 1987: Enter (A / Trigger links) wirkt als Feuerknopf; Tempo mit 2750 Zyklen wie bei GOG?
10. Populous / Populous II: Tempo mit 7800 bzw. 26800 Zyklen angemessen?
11. Maus `direct`: je Mausspiel Spielzeiger unter dem Laser – Mitte, alle vier Ränder/Ecken, nach
    schnellem Schwenken; mit Bildzoom (X) ebenfalls. Log `Maus „direct“: Spielzeiger absolut …`.
12. Filter: neue Spiele starten mit **HD** (Menü zeigt „HD“); Wechsel auf Scharf/… bleibt nach
    Neustart erhalten. Kein Saum an den Bildrändern, kein Ruckeln (Leistungszeile).
13. Auflösung: SimCity 2000 → Log `Core-Geometrie: 800x600`, Texte lesbar? (Sonst bzw. für mehr
    Stadt: `-SimCityResolution 640x480|1024x768`, nur `SC2000.CFG` neu schieben.) Pirates! Gold
    `640x480`. Syndicate Wars: Einsatz startet in 320×200 (Erwartung), F8 → 640×480.
14. Menü in `xr.dosgames`: Reiter Spiele · Bild · Zeiger · DOS · Steuerung · Spiel, Kopfzeile zeigt `320×200 · 4:3`;
    nichts ruckelt beim Durchschalten (Teil-Redraw).
15. DOS → CPU-Tempo ändern: Spieltempo ändert sich **ohne** Neustart (Log `Kern-Optionen geändert —
    Core liest neu`); ebenso Mausmodus Laser-genau ↔ Relativ und Seitenverhältnis (Bild wird 4:3 bzw. 16:10).
16. DOS → Grafikkarte: Wert orange, „Neu starten: Änderung übernehmen“ → nach Neustart aktiv.
17. Pro Spiel: Filter/Größe in Spiel A ändern, Spiel B starten (andere Werte), zurück zu A → Werte von
    A wieder da; App neu starten → bleibt. „Spiel-Standard wiederherstellen“ → Profilwerte.
18. Syndicate Wars: DOS → „Hohe Auflösung umschalten (F8)“ im Einsatz → 640×480.
19. Relativ (Pirates! Gold, SimCity 2000, Syndicate Wars): nach Menü-Schließen springt der Zeiger kurz
    nach links oben und dann unter den Laser (Log `Zeiger gefangen bei …`); Laser vom Bild und zurück
    fängt erneut; ein Klick direkt beim Zurückkommen landet am Laser, nicht unterwegs.
20. Abgleichen: DOS → Abgleichen, auf den stehenden Spielzeiger zielen, Trigger → Log `Maus
    abgeglichen: … → Faktor x/y`; danach folgt der Zeiger dem Laser über das ganze Bild (Ecken prüfen).

21. Karte ziehen (SimCity 2000, Syndicate Wars, Populous II): rechten Griff halten, Hand ein paar cm
    nach links → Karte wandert mit der Hand (Ansicht nach rechts), weiter weg → schneller, ab ~10 cm
    Dauerlauf; diagonal (schräg oben links) → zwei Richtungen. Griff los → Scrollen stoppt sofort,
    nichts läuft nach. Log `Karte ziehen: gegriffen` / `losgelassen`.
22. Beim Ziehen klickt nichts: Trigger/B während des Haltens ohne Wirkung, Spielzeiger springt nicht;
    nach dem Loslassen wieder normal klicken (B = Rechtsklick, z. B. SimCity-Hilfe).
23. Haptik: kurzer Impuls beim Greifen und Loslassen, leiser Tick je Tastentakt (bei Dauerdruck keiner).
24. Menü → Steuerung: Zieh-Richtung „Wie Stick“ → Hand links = Ansicht links; Zieh-Tempo 1 und 5
    spürbar verschieden; Rechter Griff „Rechtsklick“ → Griff wieder rechte Maustaste, Zieh-Zeilen grau;
    Rechter Stick „Aus“ → Stick scrollt nicht mehr. Werte pro Spiel (anderes Spiel, zurück → bleiben),
    „Spiel-Standard wiederherstellen“ (Reiter DOS) setzt sie zurück.
25. Konflikte: linker Griff (Fenster greifen) und linker Griff + dann rechter Griff (Fenster skalieren)
    wie bisher; Menü öffnen mitten im Ziehen → nichts scrollt weiter, nach dem Schließen erst neu greifen.
26. Populous: scrollt der Ziffernblock (Ziehen) die Karte (laut Handbuch langsamer als die
    Richtungspfeile)? In welche Bildrichtung geht KP8? Wenn nichts scrollt: Menü → Steuerung →
    Rechtsklick und im Bericht vermerken. Pirates! Gold: Griff = Stoß beim Fechten; Pirates!: Griff ohne Wirkung.
27. „Hand folgt“ (exp.) in SimCity 2000: „Pixel je Tastendruck“ so einstellen, dass die Karte an der
    Hand klebt; notieren, welcher Wert passt bzw. ob das Spiel pro Druck überhaupt einen festen Weg scrollt.

## Symptom → Fix

| Symptom | Fix |
|---|---|
| DOSBox-Pure-Startmenü statt Spiel | `AUTOBOOT.DBP` fehlt oder ohne CRLF → Skript neu laufen lassen |
| „CD nicht gefunden“ (Pirates! Gold, Syndicate Wars) | Image muss auf C: (Ordnerwurzel) liegen und Start über AUTOBOOT |
| keine Musik bei Populous/SimCity 2000 | `DOSBOX.SF2` im Spielordner fehlt (Skript-Warnung „Keine Soundfont“) |
| kein Ton bei Populous II | `xrshell.opt` auf `A220 I7 D1 H5` probieren |
| Spiel fehlt in der Auswahl | Ordner muss direkt unter `files/games/dos/` liegen; per `APP_ID=xr.dosgames tools/push-game.sh` (chmod) |
| Tasten des vorigen Spiels wirken noch | darf nicht sein (Profil wird beim Wechsel ersetzt) → Log `Tastenprofil` |
| Spielzeiger klebt/springt mit `direct` | Spiel ignoriert die INT-33-Position bzw. VESA-Modus → VR-Menü → DOS → Mausmodus „Relativ“, dann „Abgleichen“ |
| Relativ: Zeiger läuft dem Laser davon/hinterher | VR-Menü → DOS → „Abgleichen“ (ggf. zweimal), feiner mit „Maus waagerecht/senkrecht“ |
| Relativ: Zeiger nach Moduswechsel versetzt | Laser kurz vom Bild nehmen (fängt neu) bzw. Menü auf/zu |
| Zeiger um einen Rand versetzt | `dosbox_pure_overscan`/„padded“ aus der `xrshell.opt` nehmen |
| Bild steht nach Auflösungswechsel | Frame > 1024×768 (Log `passt nicht`) → kleinere Spielauflösung |
| Karte ziehen scrollt nicht | Spiel nimmt andere Scroll-Tasten → `scroll_left/right/up/down` in `xrshell.keys`; Log `Karte ziehen: gegriffen` fehlt → Griff startet nicht (Maustaste gehalten? linker Griff zuerst?) bzw. Menü → Steuerung → „Karte ziehen“ |
| Karte zieht in die falsche Richtung | Menü → Steuerung → Zieh-Richtung umstellen |
| Karte zieht zu hektisch / träge | Menü → Steuerung → Zieh-Tempo (1 ruhig … 5 flink) |
| Rechtsklick fehlt auf dem Griff | Absicht in `xr.dosgames` (Griff = Karte ziehen) → B nutzen oder Menü → Steuerung → Rechtsklick |
| Menüwert für ein Spiel „klemmt“ | `files/settings/<Ordner>.user.opt` löschen bzw. DOS → Spiel-Standard |
| CPU-Tempo/Maus ändert sich erst nach Neustart | Core fragt `GET_VARIABLE_UPDATE` nicht ab (kein Log `Core liest neu`) → DOS → Spiel neu starten |
| Zeile im Reiter DOS fehlt | Core meldet die Option nicht (Log `Core meldet N Optionen`) |
| HD sieht aus wie Scharf | Log `HD-Filter (xBR) nicht verfügbar` → Shader-Fehlertext im Log |

## Shooter (Pad-Modus, „Weg 1“)

**Steam „Ultimate Doom“ (Paket „DOOM + DOOM II“, 2026-10-03 mit echten Daten gebaut):** DOS-Fassungen
liegen unter `base\`: `DOOM.EXE`+`DOOM.WAD`, `doom2\DOOM2.EXE`+`DOOM2.WAD`, `tnt\final.exe`+`TNT.WAD`,
`plutonia\final.exe`+`PLUTONIA.WAD` (alle WAD-Größen = v1.9). Final Doom startet über die eigene
`FINAL.EXE` (das Skript nimmt sie vor `DOOM2.EXE`). `rerelease\` (KEX) wird übergangen.
`prepare-dos-games.ps1 -Source "<Steam>\Ultimate Doom" -Game doom|doom2|doom-tnt|doom-plutonia`
→ `Doom` 13 MB, `Doom2` 15 MB, `FinalDoomTNT` 18 MB, `FinalDoomPlutonia` 17 MB.
Quake ebenso aus Steam „Quake“ gebaut (Tabelle unten). **Im Headset ist noch nichts davon gelaufen.**

Ursprünglicher Stand 2026-10-03 (vor den Steam-Kopien): vorbereitet aus Recherche, nicht im Headset. Ziel: die DOS-Originale der klassischen Shooter in `xr.dosgames`
mit Gamepad-Steuerung. Wo eine Kaufversion **keine DOS-EXE** mehr enthält, bleibt nur die
Daten-WAD/PAK → **Weg 2: App `xr.shooter`** (PrBoom für Doom, ECWolf für Wolfenstein 3D,
TyrQuake für Quake; wird parallel gebaut). Priorität: Doom, Doom II, Final Doom (der Spieler
besitzt sie); die übrigen Titel haben nur Profile (`profiles/<spiel>.opt/.keys/.yml`), keinen
Zweig in `prepare-dos-games.ps1`.

### Steuerung im Pad-Modus

`xrshell.opt` mit `xrshell_input = pad`: XRShell schickt ein RetroPad mit Analogsticks an Port 0,
kein Laser-Mauszeiger. Die Tastenbelegung macht **DOSBox Pure selbst** – aus `C:\DOS.YML` im
Spielordner (Quelle: `profiles/<profil>.yml`). Befund im DOSBox-Pure-Quellcode
(`schellingb/dosbox-pure` main, Stand 2026-09-14):

- `DOS.YML` auf Laufwerk C: wird beim Start gelesen (`src/dos/drive_patch.cpp` `AppendYML`,
  `dosbox_pure_run.h` `DOSYMLLoader`). Zeilen `input_pad_<taste>: <Taste>[+<Taste>…] <Name>` und
  `input_wheel_<n>: <Taste> <Name>` werden zur Port-0-Belegung „Content Provided Mapping“
  (`dosbox_pure_pad.h` `ParseInputYML`) und **ersetzen** die eingebaute Keyb2Joypad-Belegung.
  Tastennamen wie in `DBP_YMLKeyCommands` (`leftctrl`, `space`, `comma`, `quote`, `f6` …),
  `wheel` = Aktionsrad (Taste halten, mit Stick wählen, loslassen).
- Vorrang: `C:\PADMAP.DBP` (im DOSBox-Pure-Menü selbst geänderte Belegung, liegt im Spielstand)
  vor `DOS.YML` vor der Keyb2Joypad-Datenbank (`keyb2joypad.cpp`, Erkennung über Dateiname + Größe).
- Eingebaute Belegungen (decodiert aus `keyb2joypad.cpp`): DOOM, Heretic, Hexen, Wolfenstein 3D,
  Spear of Destiny, Duke Nukem 3D, Blood, Shadow Warrior, Quake haben je einen Eintrag. Erkannt
  werden u. a. `DOOM.WAD` 12 408 292 B (Ultimate Doom 1.9), `WOLF3D.EXE` 98 402 B, `GAMEMAPS.WL6`
  150 652 B, `VSWAP.SOD` 1 616 544 B, `QUAKE.EXE` 396 288/409 600 B; **`DOOM2.WAD` 1.9, `TNT.WAD`,
  `PLUTONIA.WAD` nicht** (Doom II/Final Doom hätten ohne `DOS.YML` keine Belegung). Die eingebauten
  Belegungen folgen keinem einheitlichen Schema (DOOM: Esc auf RetroPad X, Karte auf A) – deshalb
  eigene `DOS.YML` je Spiel mit einheitlicher Quest-Belegung.
- Port-Gerät: XRShell setzt im Pad-Modus nach dem Laden `retro_set_controller_port_device(0,
  RETRO_DEVICE_JOYPAD)` = „Use Gamepad Mapper“ (`dosbox_pure_pad.h` `SetPortMode` → `MODE_MAPPER`).
  Port 0 nimmt dann `PADMAP.DBP`, sonst `dbp_auto_mapping` – das ist die `DOS.YML`-Belegung, falls
  vorhanden, sonst die Keyb2Joypad-Belegung –, sonst „Generic Keyboard Bindings“. Ohne den Aufruf
  bliebe Port 0 „Disabled“. `DOS.YML` passt also zu diesem Gerät; `dosbox_pure_auto_mapping`
  darf nicht `false` sein.
- Alle neun `profiles/*.yml` offline gegen den Quellcode geprüft (Zeilensyntax, Tasten- und
  Padnamen, fortlaufende Radnummern): ohne Fehler. Ob der Kern im Gerät sie liest, ist offen.

Quest-Controller → RetroPad im DOS-Pad-Modus (`padmap::kDosPadLayout` in `src/pad_analog.h`,
Host-Test `tests/test_pad_analog.cpp`; Details zu den Sticks: `docs/SHOOTER.md`):

| Quest | Trigger rechts | Griff rechts | A | B | X | Y | Trigger links | rechten Stick drücken | linken Stick drücken | Sticks |
|---|---|---|---|---|---|---|---|---|---|---|
| RetroPad (`DOS.YML`) | `r` | `l2` | `y` | `b` | `x` | `start` | `l` | `r3` | `l3` (frei = DOSBox-Pure-Menü/Tastatur) | `lstick_*`, `rstick_*` (analog) |

Das Steuerkreuz (`up/down/left/right`) sendet XRShell im DOS-Pad-Modus nicht. Der linke Stick
gehört dem Spiel nur ohne linken Griff (mit Griff: Fenster), die linke Menütaste bleibt VR-Menü.

Doom-Belegung (`profiles/doom.yml`, gilt für alle vier Doom-Ordner):

| Quest | Taste | Wirkung |
|---|---|---|
| Stick links ↑ ↓ | ↑ ↓ | laufen vor / zurück |
| Stick links ← → | `,` `.` | seitwärts |
| Stick rechts ← → | ← → | drehen (rechter Stick ↑ ↓ frei) |
| Trigger rechts | Strg | feuern |
| Griff rechts (halten) | Shift | rennen |
| A | Leertaste + Enter | benutzen / öffnen, im Menü bestätigen |
| B (halten) | Aktionsrad | Waffe 1–7 wählen, F9 Schnellladen |
| X | Tab | Karte |
| Y | Esc | Menü |
| Trigger links | Y | „Ja“ in Abfragen (Beenden, Nightmare, Schnellladen) |
| rechten Stick drücken | F6 | Schnellspeichern |
| linken Stick drücken | – | DOSBox-Pure-Menü / Bildschirmtastatur |

Doom 1.9 (DOS) hat **kein Springen und keine „nächste Waffe“-Taste** – daher B = Waffenrad. Die
Belegung setzt die Doom-Standardtasten voraus; das Skript schreibt dazu eine neue `DEFAULT.CFG`
(Tastencodes 172–175 Pfeile, 157 Strg, 182 Shift, 184 Alt, `,`/`.` seitwärts, `use_mouse 0`,
`use_joystick 0`, `snd_sfxdevice 3`, `snd_musicdevice 3` = Sound Blaster/OPL, `snd_sbport 544`
= 220h, `snd_sbirq 7`, `snd_sbdma 1`, `snd_channels 8`).

Rückfall Maus-Modus (`xrshell.keys`, ohne `xrshell_input = pad`): Stick links = vor/zurück/
seitwärts, Trigger links = feuern, A = benutzen/OK, X = Karte, rechten Stick drücken = Esc,
rechter Stick = Pfeiltasten (Grundbelegung), Y = Bildschirmtastatur. **Im Pad-Modus darf XRShell
`xrshell.keys` nicht zusätzlich auswerten** (Tasten kämen doppelt, Tab schaltet die Karte zweimal).

### Befund Doom-Reihe (Kaufversionen)

| Spiel (`-Game`) | Ordner | Kaufversion mit DOS-Dateien | nötige Dateien | Start | nur Daten → Weg 2 |
|---|---|---|---|---|---|
| The Ultimate Doom (`doom`) | `Doom` | GOG „The Ultimate DOOM“ (bis 2024, DOSBox-Fassung); seit 08/2024 GOG/Steam „DOOM + DOOM II“ (KEX) – laut PCGamingWiki weiter mit den DOS-Originalen; Steam: `Ultimate Doom\base\` | `DOOM.EXE` (1.9, DOS/4GW eingebunden), `DOOM.WAD` (12 408 292 B) | `DOOM.EXE` | Unity-Port (2019), Doom 3 BFG (`base\wads`), Konsolen/Bethesda.net → `xr.shooter` |
| Doom II (`doom2`) | `Doom2` | GOG „DOOM II“ (DOSBox), „DOOM + DOOM II“ (2024); Steam: `Doom 2\base\` | `DOOM2.EXE` (1.9), `DOOM2.WAD` (14 604 584 B) | `DOOM2.EXE` | wie oben → `xr.shooter` |
| Final Doom: TNT (`doom-tnt`) | `FinalDoomTNT` | GOG „Final DOOM“ (Unterordner `TNT\`), „DOOM + DOOM II“; Steam: `Doom 2\finaldoombase\` (früher `Final Doom\base\`) | `DOOM2.EXE` **von Final Doom**, `TNT.WAD` (18 195 736 B; id-Anthology-Fix 18 654 796 B) | `DOOM2.EXE` | → `xr.shooter` |
| Final Doom: Plutonia (`doom-plutonia`) | `FinalDoomPlutonia` | wie TNT (`PLUTONIA\`) | `DOOM2.EXE` von Final Doom, `PLUTONIA.WAD` (17 420 824 / 18 240 172 B) | `DOOM2.EXE` | → `xr.shooter` |

Nicht für die DOS-EXE verwendbar: die WADs aus `rerelease\` (KEX 2024, erweitert) – das Skript
übergeht Ordner namens `rerelease`. Pfade nach Chocolate Doom `src/d_iwad.c` (Steam: `doom 2\base`,
`doom 2\finaldoombase`, `ultimate doom\base`, `final doom\base`; GOG: `Plutonia`, `TNT`, `Doom2`,
`Final Doom`, `Ultimate Doom`), dem Steam-Guide „Fixing the MS-DOS Doom games on Steam“ und
PCGamingWiki – **die genaue Lage der EXE in der 2024er-Fassung ist ungeprüft**; deshalb sucht das
Skript rekursiv.

Ton: SB16 220/7/1 (`xrshell.opt` + `DEFAULT.CFG`), Musik OPL-FM über „Sound Blaster“ – keine
Soundfont. Tempo: `cycles = max` (Spiellogik fest 35 Hz). Jeder Ordner enthält genau ein IWAD
(sonst nimmt `DOOM2.EXE` `DOOM2.WAD` vor `TNT.WAD`). Master Levels: nicht aufgenommen (PWADs per
`-file`, eigener Startdialog).

```powershell
# entpackter GOG-Ordner bzw. Steam-Installationsordner (wird rekursiv durchsucht):
powershell -File tools\prepare-dos-games.ps1 -Source '<…>\Ultimate Doom' -Game doom
powershell -File tools\prepare-dos-games.ps1 -Source '<…>\Doom 2' -Game doom2
powershell -File tools\prepare-dos-games.ps1 -Source '<…>\Doom 2' -Game doom-tnt     # Steam: finaldoombase
powershell -File tools\prepare-dos-games.ps1 -Source '<…>\Final DOOM' -Game doom-plutonia
# oder -Source <entpackt> -Game all mit Unterordnern doom, doom2, final-doom (fehlende werden übersprungen)
```

Je Ordner: EXE, IWAD, `DEFAULT.CFG`, `XRSTART.BAT`, `AUTOBOOT.DBP`, `DOS.YML`, `xrshell.opt`,
`xrshell.keys`, `xrshell.title`. Findet das Skript nur die WAD, bricht es mit dem Hinweis auf
`xr.shooter` ab; liegt `DOOM2.EXE` nicht neben `TNT.WAD`/`PLUTONIA.WAD`, warnt es (dann ist es die
Doom-II-EXE – ob sie TNT/Plutonia erkennt, ist ungeprüft). Unbekannte WAD-Größe = Warnung.
Offline getestet mit leeren Platzhalterdateien (GOG- und Steam-Layout, nur-WAD-Fall).

### Übrige Shooter (nur Profile, kein Skript-Zweig)

Ordner von Hand bauen: Dateien der DOS-Fassung nach C:, `XRSTART.BAT` + `AUTOBOOT.DBP`
(`C:\XRSTART.BAT`, CRLF), `profiles/<p>.yml` → `DOS.YML`, `.opt` → `xrshell.opt`, `.keys` →
`xrshell.keys`. Im Setup des Spiels Sound Blaster 220/7/1 wählen, Maus aus.

| Spiel (Profil) | Kaufversion mit DOS-EXE (ungeprüft) | nötige Dateien / Start | Sound | Pad-Belegung (`DOS.YML`) | nur Daten → Weg 2 |
|---|---|---|---|---|---|
| Heretic (`heretic`) | GOG/Steam „Heretic: Shadow of the Serpent Riders“ (DOSBox, Steam `base\`); „Heretic + Hexen“ (KEX) mit `dos\base\heretic` | `HERETIC.EXE`, `HERETIC.WAD`, `DEFAULT.CFG` | SB, Musik OPL (`SETUP`) | wie Doom; Stick rechts ↑↓ = sehen, Trigger links = Artefakt/Enter, rechten Stick drücken = Blick zentrieren, B = Rad (Waffen, Inventar, Fliegen, „Ja“) | – |
| Hexen (`hexen`) | GOG/Steam „Hexen“ (DOSBox); „Heretic + Hexen“ `dos\base\hexen` | `HEXEN.EXE`, `HEXEN.WAD`, `HEXEN.CFG` | SB, OPL | wie Heretic, rechten Stick drücken = springen (`/`), Blick zentrieren im Rad | – |
| Wolfenstein 3D (`wolf3d`) | GOG/Steam „Wolfenstein 3D“ (1.4, DOSBox, Steam `base\`) | `WOLF3D.EXE`, `*.WL6` | im Spiel SB + AdLib (OPL) | Stick links ←→ = Alt+Pfeil, Griff = rechte Shift (rennen), B = Rad 1–4, X = Pause, Trigger links = „Ja“ | `xr.shooter` (ECWolf) |
| Spear of Destiny (`spear-of-destiny`) | GOG/Steam „Spear of Destiny“ (mit Wolf3D) | `SPEAR.EXE`, `*.SOD` (Missionen `*.SD2/.SD3`) | wie Wolf3D | wie Wolf3D | `xr.shooter` (ECWolf) |
| Duke Nukem 3D (`duke3d`) | GOG/Steam „Atomic Edition“ (1.5, DOSBox; seit 2016 nicht mehr im Verkauf, Besitzer behalten den Download). „20th Anniversary World Tour“: Windows, **keine DOS-EXE** | `DUKE3D.EXE`, `DUKE3D.GRP`, `DUKE3D.CFG` (`SETUP.EXE`) | SB, OPL/GM | Trigger links = springen (`A`), rechten Stick drücken = nächste Waffe (`'`), Stick rechts ↓ = ducken, B = Rad | – |
| Blood (`blood`) | GOG/Steam „One Unit Whole Blood“ (1.21, DOSBox). „Fresh Supply“ (2019): **keine DOS-EXE** | `BLOOD.EXE`, `*.RFF`, `BLOOD.CFG` | SB, OPL/GM | wie Duke, rechten Stick drücken = Zweitfeuer (`X`) | – |
| Shadow Warrior (`shadow-warrior`) | „Shadow Warrior Classic Complete“ (DOSBox, `SW.EXE`). „Classic Redux“: neue Engine, **keine DOS-EXE** | `SW.EXE`, `SW.GRP`, `SW.CFG` | SB, OPL/GM | wie Duke (Feuern-Taste in `SW.CFG` prüfen – Keyb2Joypad nimmt Leertaste) | – |
| Quake (`quake`) | Steam „Quake“ (geprüft 2026-10-03: `quake.exe` 1.06 + `CWSDPMI.EXE` + `Id1\` in der Wurzel, daneben `rerelease\` der Neuauflage) | `QUAKE.EXE`, `CWSDPMI.EXE`, `ID1\PAK0.PAK`, `ID1\PAK1.PAK` | SB über `BLASTER`; Musik: `QUAKE.CUE` = Datenspur `QUAKE.ISO` (IMAPI2) + `CDAUDIO\TRACK02–11.OGG` aus `rerelease\id1\music\` → DOSBox Pure hängt sie als CD D: ein | Stick rechts ↑↓ = sehen, Trigger links = springen, rechten Stick drücken = nächste Waffe (`/`), A = Enter, B = Rad, X = Punktestand | `xr.shooter` (TyrQuake) |

Quake braucht einen schnellen Pentium: `cycles = max`, `cpu_type = pentium_slow`, 32 MB,
CPU-Level der Quest hoch (Skill `xr-image-quality`), nur 320×200. Ob das auf der Quest flüssig
läuft, ist offen – sonst Weg 2.

### Im Headset zu prüfen (Shooter)

1. Pad-Modus aktiv: kein Laser-Zeiger; DOSBox Pure meldet die Belegung als „Content Provided
   Mapping“ (nicht „Game: DOOM“).
2. Doom/Doom II/TNT/Plutonia starten direkt (AUTOBOOT), richtiges IWAD (Titelbild/Levelnamen).
3. Stick links = laufen/seitwärts, Stick rechts = drehen, Trigger rechts = feuern, A = Tür öffnen,
   Griff rechts = rennen, X = Karte, Y = Menü, A bestätigt im Menü, Trigger links = „Y“ beim
   Beenden, linken Stick drücken = DOSBox-Pure-Menü.
4. Waffenrad auf B: öffnet sich, Auswahl per Stick, Waffe wechselt; dreht der rechte Stick dabei weiter?
5. Ton: Effekte und OPL-Musik (Sound Blaster).
6. Tempo flüssig mit `cycles = max`; Leistungszeile mitloggen.
7. Kommen Tasten doppelt (`xrshell.keys` im Pad-Modus)? Die Karte darf nicht flackern.
8. Spielstände (F2/F6) bleiben nach App-Neustart (Dateien im Spielordner, z. B. `DOOMSAV0.DSG`).
9. TNT/Plutonia mit Doom-II-EXE (Skript-Warnung): welches IWAD startet?
