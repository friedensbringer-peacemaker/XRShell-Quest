# Shooter in XRShell (xr.shooter, „Weg 2“)

Klassische Ego-Shooter laufen mit modernen Open-Source-Engines als libretro-Cores auf dem
virtuellen Bildschirm. Die Spieldaten bringt der Spieler aus seiner eigenen Kopie mit
(BYOA) — **nie ins Repo, nie ins APK**.

Stand 2026-10-04: Doom II (0.3.0) und Quake (0.3.1) sind im Headset gelaufen (Befunde unten unter
„Headset-Befunde“); alles Weitere ist gebaut, **nicht im Headset abgenommen**. Vor dem nächsten
Headset-Test prüft der Core-Smoke-Test (`tests/run-core-smoke.sh`) die echten Cores mit den echten
Spieldaten auf der Quest ohne Brille (siehe „Core-Smoke-Test“).

## Apps und Cores

| App | Flavor | Core | Spiele | Lizenz |
| --- | --- | --- | --- | --- |
| `xr.shooter` | `fps` | PrBoom (`prboom_libretro_android.so`) | Doom, Doom II, Final Doom (TNT/Plutonia), Freedoom | GPL-2.0-oder-später |
| | | ECWolf (`ecwolf_libretro_android.so`) | Wolfenstein 3D, Spear of Destiny | GPL-2.0-oder-später (libretro-Fassung mit `dbopl`) |
| | | TyrQuake (`tyrquake_libretro_android.so`) | Quake (id1) | GPL-2.0-oder-später |
| `xr.dosgames` u. a. | `dos` … | DOSBox-Pure mit `xrshell_input = pad` | DOS-Originale (DOOM.EXE, Duke 3D …) | GPL-2.0 |

GPL-2.0-oder-später ist über GPL-3.0 mit dem Apache-2.0-OpenXR-Loader verträglich. Die Cores
kommen zur Build-Zeit vom libretro-Buildbot (`android/build.gradle`, nur in den Flavor `fps`).

Nicht aufgenommen: **vitaquake2** (Quake II) braucht `RETRO_ENVIRONMENT_SET_HW_RENDER`, das
XRShell nicht anbietet (Software-Frames in eine Textur) — offener Punkt.

### Systemdateien der Cores

- **prboom.wad**: seit Juni 2026 in den PrBoom-Core eingebaut (Commit „Bake prboom.wad into the
  core; drop external loading“); eine externe `prboom.wad` wird ignoriert. Im Buildbot-Core vom
  2026-10-01 geprüft (eingebettete Lumps vorhanden). Kein Systemdatei-Download nötig.
- **ecwolf.pk3**: in den ECWolf-Core eingebaut (`ecwolf_pk3_data`), nichts nachzuladen.
- **TyrQuake**: braucht nur die Spieldaten (`id1/pak0.pak`, für die Vollversion `pak1.pak`).

## Ordnerlayout auf der Quest

```
/sdcard/Android/data/xr.shooter/files/games/
  doom/<Ordner>/      DOOM2.WAD | DOOM.WAD | TNT.WAD | PLUTONIA.WAD …  (auch in base/ oder rerelease/)
  wolf3d/<Ordner>/    VSWAP.WL6, GAMEMAPS.WL6, … (bzw. .SOD/.WL1/.SDM)
  quake/<Ordner>/id1/ PAK0.PAK, PAK1.PAK
```

Jeder Ordner ist ein Spiel in der Spielauswahl des VR-Menüs (Reiter „Spiele“). Pro Ordner
optional:

| Datei | Wirkung |
| --- | --- |
| `xrshell.title` | Titel in der Spielauswahl (erste Zeile, UTF-8) |
| `xrshell.start` | Startdatei relativ zum Ordner, z. B. `PLUTONIA.WAD` oder `rerelease/DOOM2.WAD` (geht der Regel vor) |
| `xrshell.opt` | Kern-Optionen `schlüssel = wert` (überschreiben die Standards unten) |

Übertragen: `APP_ID=xr.shooter SYSTEM=doom tools/push-game.sh <Quellordner> <Name>`
(`SYSTEM` = `doom`, `wolf3d` oder `quake`).

### Spielordner bauen (`tools/prepare-shooter.ps1`)

Aus dem eigenen GOG-Installer (wird mit innoextract nach `original-data\gog\` entpackt) oder einem
entpackten GOG-/Steam-Spielordner. Das Skript sucht rekursiv alle bekannten Datendateien und legt je
Spiel einen Ordner mit `xrshell.title` unter `original-data\quest\shooter\<system>\<Ordner>\` an —
ein Kauf „DOOM + DOOM II“ ergibt so mehrere Spiele. Originaldateien (Wurzel, `base\`) gehen vor der
Neuauflage (`rerelease\`, nur mit Warnung). Liegt eine DOS-EXE dabei, nennt es zusätzlich den Aufruf
für `xr.dosgames` (Weg 1, `tools\prepare-dos-games.ps1`).

```powershell
powershell -File tools\prepare-shooter.ps1 -Source <XR-Ordner>\XR-Ports-Platform\original-data\_installer\setup_doom_….exe
powershell -File tools\prepare-shooter.ps1 -Source "C:\Program Files (x86)\Steam\steamapps\common\Ultimate Doom" -List
```

| Fund (Datei) | Ordner | Titel |
| --- | --- | --- |
| `DOOM.WAD` / `DOOMU.WAD` | `doom/Doom` | The Ultimate Doom |
| `DOOM2.WAD` | `doom/Doom2` | Doom II |
| `TNT.WAD` / `PLUTONIA.WAD` | `doom/FinalDoomTNT`, `doom/FinalDoomPlutonia` | Final Doom |
| `SIGIL.WAD` / `SIGIL2.WAD` + `DOOM.WAD` | `doom/Sigil`, `doom/Sigil2` (`xrshell.start`) | Sigil, Sigil II (Episode 5/6) |
| `NERVE.WAD` + `DOOM2.WAD` | `doom/NoRestForTheLiving` (`xrshell.start`) | Doom II: No Rest for the Living |
| `MASTERLEVELS.WAD` + `DOOM2.WAD` | `doom/MasterLevels` (`xrshell.start`) | Master Levels for Doom II |
| `FREEDOOM1/2.WAD` | `doom/Freedoom1`, `doom/Freedoom2` | Freedoom |
| `VSWAP.WL6` (+ `*.WL6`) | `wolf3d/Wolf3D` | Wolfenstein 3D |
| `VSWAP.SOD` (+ `*.SOD`) | `wolf3d/SpearOfDestiny` | Spear of Destiny |
| `id1\PAK0.PAK` (+ `PAK*.PAK`) | `quake/Quake/id1` | Quake |
| `hipnotic\PAK0.PAK` + `id1\` | `quake/QuakeMP1` (`xrshell.start`) | Quake: Scourge of Armagon |
| `rogue\PAK0.PAK` + `id1\` | `quake/QuakeMP2` (`xrshell.start`) | Quake: Dissolution of Eternity |
| `rerelease\<dir>\music\trackNN.ogg` | `<dir>/music/` im jeweiligen Ordner | Soundtrack (statt CD-Audio) |

**Mit echten Daten geprüft (2026-10-03, Steam „Ultimate Doom“ = Paket „DOOM + DOOM II“):**
IWADs aus `base\` (DOS-Originale v1.9, Größen bekannt): `DOOM.WAD`, `doom2\DOOM2.WAD`,
`tnt\TNT.WAD`, `plutonia\PLUTONIA.WAD`. Zusatz-WADs gibt es nur in `rerelease\` (KEX-Fassung):
`sigil.wad` (E5M1–8), `sigil2.wad` (E6M1–9), `nerve.wad` (9 Karten), `masterlevels.wad`
(MAP01–21) — alle mit `UMAPINFO` (PrBoom-libretro liest UMAPINFO/ZMAPINFO, `src/u_mapinfo.c`),
dazu `GAMECONF`/`DEHACKED`. Ob PrBoom sie vollständig spielt, zeigt erst das Headset.
Nicht übernommen: `id1.wad` (Legacy of Rust, braucht id24), `extras.wad`, `iddm1.wad`, die
einzelnen Master-Levels-WADs aus `base\master\wads\` (je eine Karte; `masterlevels.wad` fasst sie zusammen).

**Mit echten Daten geprüft (2026-10-03, Steam „Quake“ mit Neuauflage):** `Id1\PAK0/PAK1.PAK`
(Original, nicht `rerelease\id1\pak0.pak`), Missionspakete aus `hipnotic\`/`rogue\`, 10 bzw. 8
Musikstücke → `Quake` 129 MB, `QuakeMP1` 190 MB, `QuakeMP2` 192 MB. `Quake` lief im Headset (0.3.1).

Die Doom- und Quake-Ordner oben sind aus echten Steam-Kopien gebaut. Nur mit Platzhalter-Dateien
geprüft: GOG-Installer und Wolfenstein 3D/Spear of Destiny (dafür liegen keine Daten vor).

### Startdatei-Regel (`findStartFile`, Host-Test `tests/test_game_folders.cpp`)

- **Doom**: IWAD nach Priorität `doom2`, `doom`, `doomu`, `tnt`, `plutonia`, `doom1`,
  `freedoom2`, `freedoom1`, `freedm` (`.wad`, Groß/klein egal). Erst direkt im Ordner, dann in
  Unterordnern (alphabetisch: `base/` vor `rerelease/`). Final Doom: TNT und Plutonia am besten
  als zwei Ordner — oder ein Ordner mit `xrshell.start`.
- **Wolfenstein 3D**: Endung `wl6`, `sod`, `wl1`, `sdm`, `n3d` (in dieser Reihenfolge), je Endung
  `VSWAP.*` zuerst. Nur direkt im Ordner. Spear-Missionspakete (`.sd2/.sd3`) kennt ECWolf-libretro
  nicht als Startdatei — offener Punkt.
- **Quake**: `id1/pak0.pak` (Groß/klein egal). TyrQuake erkennt `id1` am Pfad und nimmt den
  Spielordner als Basisverzeichnis. Missionspakete: eigener Spielordner mit `id1/` + `hipnotic/`
  bzw. `rogue/` und `xrshell.start` = `hipnotic/pak0.pak` (TyrQuake erkennt das Paket am Pfad und
  startet mit `-hipnotic`/`-rogue`). Deshalb dürfen Spielordner-Namen weder `id1` noch `hipnotic`,
  `rogue` oder `quoth` enthalten (`QuakeMP1`, `QuakeMP2`).
- **Quake-Musik**: TyrQuake spielt `<spielverzeichnis>/music/trackNN.ogg` (statt CD-Audio).
  Die Steam-/GOG-Neuauflage liefert den Soundtrack unter `rerelease/<id1|hipnotic|rogue>/music/`
  (44,1 kHz Stereo); `prepare-shooter.ps1` kopiert ihn mit.

Der Spielordner wird nie beschrieben: XRShell bietet System- (`files/system` intern) und
Save-Verzeichnis (`/sdcard/Android/data/xr.shooter/files/saves/`) an.

| Core | Spielstände/Konfiguration |
| --- | --- |
| PrBoom | `saves/<IWAD-Name>/` (z. B. `saves/DOOM2/prbmsav0.dsg`, `prboom.cfg`) |
| ECWolf | `saves/` (Speichern/Laden nur über libretro: VR-Menü „Spielstand“) |
| TyrQuake | `saves/id1/` |
| alle | VR-Menü-Spielstände (Savestates): `saves/<Ordnername>.state<N>` |

## Welche Original-Dateien (nur Dateinamen)

| Spiel | Quelle | Dateien |
| --- | --- | --- |
| The Ultimate Doom | GOG / Steam | `DOOM.WAD` (Steam: `base/DOOM.WAD`; 2024-Neuauflage: `rerelease/DOOM.WAD`) |
| Doom II | GOG / Steam | `DOOM2.WAD` (Steam: `base/`, Neuauflage: `rerelease/`) |
| Final Doom | GOG / Steam | `TNT.WAD`, `PLUTONIA.WAD` (Steam: `base/`) |
| Freedoom | freedoom.github.io (frei) | `freedoom1.wad`, `freedoom2.wad` |
| Wolfenstein 3D | GOG / Steam | `*.WL6` (AUDIOHED, AUDIOT, GAMEMAPS, MAPHEAD, VGADICT, VGAGRAPH, VGAHEAD, VSWAP) |
| Spear of Destiny | GOG / Steam | `*.SOD` (gleiche Namen) |
| Quake | GOG / Steam | `id1/PAK0.PAK`, `id1/PAK1.PAK` (Steam: Original-`Id1\`; die Neuauflage `rerelease/id1/` wird **nicht** erkannt — die Startregel sucht nur `<Ordner>/id1/pak0.pak`; ob TyrQuake das 2021er-Paket überhaupt lädt, ist ungeprüft) |

Nur die Datendateien kopieren, keine Installer, keine EXE nötig.

## Bedienung (Pad-Modus)

In allen Shooter-Cores ist der Controller ein Gamepad mit **Analogsticks** (Totzone 0,15 mit
Neuskalierung in XRShell; die Totzonen der Cores stehen dafür auf 0). Kein Laser und kein
Mauszeiger im Spiel; der Strahl erscheint nur bei offenem VR-Menü.

Fest für alle Apps: **linke Menütaste = VR-Menü** (Spiel pausiert; Beenden mit Bestätigung,
Strahl/Zielpunkt ausblendbar), **linker Griff = Fenster greifen**, linker Griff + linker Stick =
Breite/Abstand. Der linke Stick (und sein Klick) gehört dem Spiel nur ohne linken Griff.

| Quest | Doom (PrBoom) | Wolfenstein (ECWolf) | Quake (TyrQuake) | DOSBox-Pure `pad` |
| --- | --- | --- | --- | --- |
| Linker Stick | laufen / seitwärts (Menü: hoch/runter) | laufen / seitwärts; vor/zurück auch Steuerkreuz (Menü) | laufen / seitwärts; auch Steuerkreuz (Menü) | laufen / seitwärts (DOS.YML: Pfeile, Komma/Punkt) |
| Rechter Stick | drehen | drehen | umsehen (drehen + hoch/runter) | drehen (DOS.YML: Pfeile links/rechts) |
| Trigger rechts | Feuer (R2), Menü: bestätigen | Feuer (A), Menü: bestätigen | Feuer (R2) | R: feuern |
| Griff rechts | Rennen halten (L2) | Lauf-Taste (X): mit Dauerlauf an langsamer per Steuerkreuz, analog schneller | Springen (L2) | L2: rennen |
| A | Benutzen/Tür (Y), Menü: bestätigen | Benutzen/Tür (B), **Menü: zurück** | Menü: bestätigen (A; im Spiel seitwärts rechts) | Y: benutzen / Menü OK |
| B | nächste Waffe (A) | nächste Waffe (R2) | nächste Waffe (R) | B: Waffenrad (halten) |
| X | vorige Waffe (X), Menü: zurück | vorige Waffe (L2) | vorige Waffe (L) | X: Karte |
| Y | Menü (Start) | Pause/Menü (Start) | Menü (Start), im Menü: zurück | Start: Menü (Esc) |
| Trigger links | Karte (Select) | Karte (Select) | abtauchen (B), Menü: zurück | L: „Ja“ bzw. springen (je Spiel) |
| Rechten Stick drücken | 180°-Drehung (R3) | Statusleiste (Y) | Punktestand (Select) | R3: Schnellspeichern (Doom) |
| Linken Stick drücken | Dauerlauf an/aus (L3) | – | – | L3: DOSBox-Pure-Menü/Tastatur |

Belegungstabellen im Code: `src/pad_analog.h` (`kDoomLayout`, `kWolfLayout`, `kQuakeLayout`,
`kDosPadLayout`).

### Eingabegerät (retro_set_controller_port_device)

XRShell setzt das Gerät an Port 0 nach `retro_load_game`, wie RetroArch:

- **PrBoom**: „Doom Gamepad Modern“ = `RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)`. Ohne den
  Aufruf bleibt das Gerät 0 und PrBoom liest **keine** Eingaben.
- **TyrQuake**: „Modern“ = `RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)` (setzt dabei die
  Quake-Tastenbindungen per Konsolenbefehl — daher erst nach dem Laden).
- **ECWolf**: `RETRO_DEVICE_JOYPAD` (ECWolf kennt nur das RetroPad).
- **DOSBox-Pure** (`xrshell_input = pad`): `RETRO_DEVICE_JOYPAD` = „Use Gamepad Mapper“. Der
  Mapper nimmt eine eigene `PADMAP.DBP` auf C:, sonst `C:\DOS.YML` (`profiles/<spiel>.yml`,
  „Content Provided Mapping“, Spalte „DOSBox-Pure `pad`“ oben = Doom), sonst das **Auto-Mapping** des Spiels
  (keyb2joypad-Datenbank, erkannt an Dateiname + Größe, z. B. `DOOM.EXE`/`DOOM.WAD`,
  `DUKE.RTS`), sonst „Generic Keyboard Bindings“ (Steuerkreuz/linker Stick = Pfeiltasten,
  rechter Stick = Home/End/PgUp/PgDn, B = Strg, A = Alt, X = Leertaste, Y = Shift,
  Start = Enter, Select = Esc). Das Auto-Mapping für ein Spiel direkt wählen wäre
  `RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 99)` — der Mapper-Modus nimmt es ohnehin, wenn es
  existiert, und fällt sonst sinnvoll zurück. Ohne Aufruf ist der Port bei DOSBox-Pure
  „Disabled“ (deshalb reagierte der Pad-Modus dort bisher nicht). Voraussetzung:
  `dosbox_pure_auto_mapping` nicht auf `false`.

### `xrshell_input` (xrshell.opt)

`xrshell_input = pad|mouse` wertet XRShell selbst aus (geht nie an den Core). `pad` schaltet
DOSBox-Pure in den Pad-Modus (RetroPad inkl. Analog an Port 0, kein Laser/Mauszeiger im Spiel,
Laser nur im VR-Menü). `mouse` ist der Standard für DOSBox-Pure. Shooter-Cores
haben einen festen Modus (Log-Hinweis, wenn die Option dort steht). Im Pad-Modus wertet XRShell
`xrshell.keys` **nicht** aus — die Belegung macht der Gamepad-Mapper von DOSBox-Pure.

### Steuerkreuz aus den Sticks

| Core | Sticks als Steuerkreuz | Warum |
| --- | --- | --- |
| PrBoom | linker Stick hoch/runter, ab 0,9 | Menüs nur per Steuerkreuz (Smoke-Test 2026-10-04: analog bewegte den Menü-Cursor nicht); im Spiel ist Steuerkreuz hoch = „vor“ → bei Vollausschlag Tempo-Schub |
| ECWolf | linker Stick vor/zurück | Menüs nur per Steuerkreuz; links/rechts wäre im Spiel „drehen“ statt „seitwärts“ |
| TyrQuake | linker Stick, alle Richtungen | Menüs nur per Steuerkreuz; im Spiel dieselbe Richtung wie analog |

Bei ECWolf und TyrQuake drückt der Stick das Steuerkreuz erst ab **0,9** Ausschlag (Loslassen unter
0,75, `kDpadPressHigh`), sonst gilt 0,5/0,35. Grund (Code-Review 2026-10-03): TyrQuake belegt das
Steuerkreuz im Spiel mit `+forward/+back/+moveleft/+moveright` und addiert den Analogwert — bei 0,5
sprang das Tempo von ~80 auf ~280. ECWolf überschreibt den Analogwert mit dem Steuerkreuz. Mit 0,9
bleibt fast der ganze Weg analog; Vollausschlag ≈ Rennen. Menüs: Stick ganz durchdrücken.
| DOSBox-Pure `pad` | keine | Mapper belegt Steuerkreuz und Sticks getrennt (DOOM: Steuerkreuz = Bildgröße) |

Trigger: XRShell meldet die Trigger digital (`RETRO_DEVICE_INDEX_ANALOG_BUTTON` = 0 oder
32767). Von den drei Shooter-Cores liest keiner analoge Trigger; DOSBox-Pure kann es, nutzt im
Auto-Mapping aber nur digitale Tasten.

## Kern-Optionen (Standard in XRShell, per `xrshell.opt` überschreibbar)

| Core | Option | Wert | Grund |
| --- | --- | --- | --- |
| PrBoom | `prboom-analog_deadzone` | `0` | Totzone macht XRShell |
| | `prboom-resolution` | `960x600` | 3× 320×200, passt in die 1024×768-Textur |
| | `prboom-color_format` | `24bits (truecolor)` | kein Banding der 16-Bit-Lichtrampe (XRShell nimmt XRGB8888) |
| | `prboom-render_threads` | `2` | Wände/Böden auf 2 Threads füllen (Quest 3: 6 Kerne, Compositor braucht welche) |
| | `prboom-sound_samplerate` | `48000` | = AAudio-Rate der Quest, kein Resampling („auto“ fragt eine Frontend-Rate ab, die XRShell nicht meldet) |
| | `prboom-rumble` | `enabled` | Vibration rechter Controller: Schuss/Treffer stark, Aufsammeln schwach (libretro-Rumble, XRShell ab 2026-10-03) |
| | `prboom-mouse_on` | `disabled` | keine Maus im Pad-Modus |
| ECWolf | `ecwolf-analog-deadzone` | `0%` | Totzone macht XRShell |
| | `ecwolf-resolution` | `960x600` | 3× 320×200 |
| | `ecwolf-fps` | `70` | = feste Tic-Rate (TICRATE 70); 72 brächte keine flüssigere Bewegung |
| | `ecwolf-alwaysrun` | `enabled` | Griff rechts = gehen |
| TyrQuake | `tyrquake_analog_deadzone` | `0` | Totzone macht XRShell |
| | `tyrquake_resolution` | `640x480` | |
| | `tyrquake_framerate` | `72` | Quest-Bildrate; > 72 laut Core fehleranfällig |
| | `tyrquake_sound_samplerate` | `48000` | = AAudio-Rate der Quest, kein Resampling (Option ist ein „Hinweis“; `GET_TARGET_SAMPLE_RATE` meldet ebenfalls 48000) |
| | `tyrquake_rumble` | `enabled` | Vibration rechts |

Breitbild in PrBoom (Doom-Menü „Aspect Ratio“): verbreitert das Bild über die 4:3-Breite hinaus.
Mit 960×600 wären 16:9 schon 1280 Pixel → größer als die 1024×768-Textur, XRShell verwirft solche
Frames (Log `passt nicht`, Bild steht). Standard bleibt 4:3 (`render_aspect 0`). Wer Breitbild will:
`prboom-resolution = 640x400` in die `xrshell.opt` des Spielordners (16:9 = 853 Pixel, passt).

Bildrate: XRShell taktet den Core mit seinem gemeldeten fps-Wert (feste Fristen, `src/frame_pacer.h`
— bis 0.3.2 summierte sich die Aufwachverzögerung, 71,6 statt 72 fps) und zeigt pro Display-Frame
das jeweils neueste Bild. Die passende Quest-Rate fordert XRShell selbst an
(`XR_FB_display_refresh_rate`, `src/refresh_rate.h`): 72 fps → 72 Hz, auch erneut nach Brille ab/auf
(neue Session) und nach einem Ratenwechsel von außen (Log `Bildwiederholrate geändert`). Cores, die
`GET_TARGET_REFRESH_RATE` fragen, bekommen die aktuelle Anzeige-Rate. TyrQuake 72, ECWolf 70. **PrBoom**: die Bildrate ist keine Core-Option, sondern `uncapped_framerate` in der
`prboom.cfg` (Standard Index 3 = 60 fps → ruckelt auf 72 Hz). `prepare-shooter.ps1` legt deshalb
`profiles/doom.prboom.cfg` als `<IWAD>.prboom.cfg` (z. B. `DOOM2.prboom.cfg`) neben die WAD;
PrBoom lädt sie als Grundeinstellung (`-baseconfig`, Quelle `libretro/libretro.c`), danach gilt
die eigene `saves/<IWAD>/prboom.cfg`:

| Schlüssel | Wert | Wirkung |
| --- | --- | --- |
| `uncapped_framerate` | `5` | 72 fps (0=35, 1=40, 2=50, 3=60, 4=70, 5=72) — ein Bild pro Quest-Frame |
| `key_menu_enter` | `0x79` (`y`) | A/Trigger/L3 sind im Menü „Bestätigen“ **und** in Ja/Nein-Abfragen „Ja“ |
| `key_fire` | `0x79` (`y`) | Feuer (R2) wie bisher; beim Benennen eines Spielstands bestätigt A |

Hintergrund Ja-Taste: PrBoom liest im Gamepad-Modus keine Tastatur, und Abfragen nehmen nur
`y`/`n`/Leertaste/Esc — Gamepad-Tasten lieferten im Menü `Enter`/`Backspace` (ignoriert) bzw. Esc
(= Nein). Mit `key_menu_enter = y` antwortet „Bestätigen“ mit Ja; Start (Esc) bleibt Nein. Die
Menüs vergleichen gegen die Variable und funktionieren weiter; `y` ist sonst nirgends belegt.

## Start und Ladeanzeige (0.3.6)

Gemeinsamer XRShell-Code, Standard aus dem Skill xr-loading-screen (L1–L8):

| Wann | Was man sieht |
|---|---|
| Antippen in der Bibliothek der Quest | Quest-Startbild `xr.shooter 0.3.6 · Start dauert etwa 3 s` (statt drei Punkten) |
| erster XR-Frame | Tafel an der Stelle des Spielfensters: ohne Spiel „Spiel im Menü auswählen“ + Systemcheck, davor die Spieleliste |
| Spiel gewählt / „Weiterspielen“ | Schritte Systemcheck → Spieldaten prüfen → Core laden → Spiel laden → Erstes Bild mit Dauer, Balken mit Lichtpunkt, „noch etwa N s“ bzw. „Erster Start – dauert etwa N s“ |
| erstes Bild mit Inhalt (PrBoom-Titel, Quake-Konsole) | Tafel weg, Spiel läuft; Eingaben ans Spiel erst ab hier, gehaltene Tasten erst loslassen |
| Fehler (keine Startdatei, Core fehlt, retro_load_game scheitert) | rote Fehlerseite: Klartext, letzte Core-Zeile, `APP_ID=xr.shooter SYSTEM=<doom\|wolf3d\|quake> tools/push-game.sh …`, Logpfad; B = trotzdem weiter, Menütaste = VR-Menü |
| 10 s ohne Fortschritt / über 30 s | gelber Hinweis „Core antwortet seit N s nicht“ (Log je 10 s), danach Fehlerseite „Start hängt?“ |

Ladezeiten pro Core in `files/xr-load-times.json` (gleitendes Mittel); ohne Verlauf Werte aus dem
Core-Smoke-Test (PrBoom ≈ 55 ms, TyrQuake ≈ 120 ms für `retro_load_game`). Der Core lädt in einem
eigenen Thread — auch ein hängendes `retro_load_game` friert die Brillensicht nicht mehr ein.
Logzeilen: `Ladeanzeige sichtbar nach`, `Ladeschritt …: N ms`, `Spiel sichtbar nach`.

## Headset-Befunde

| Version | Spiel | Befund | Folge |
| --- | --- | --- | --- |
| 0.3.0 | Doom II (PrBoom) | läuft; Leistungszeile 71,6 fps bei **90 Hz** Anzeige (jedes vierte Bild doppelt) | 0.3.1: Quest-Rate passend zur Core-Bildrate (72 Hz) |
| 0.3.1 | Quake (TyrQuake) | läuft; Leistungszeile 71,6 fps bei 72 Hz (71,9 Hz gemessen) — Core-Takt driftet | 0.3.3: feste Fristen im Core-Takt (`frame_pacer.h`) |

## Core-Smoke-Test (ohne Brille)

`tests/run-core-smoke.sh` (Quest per USB, keine App wird gestartet): baut `tests/device/core_smoke.cpp`,
kopiert jeden Spielordner nach `/data/local/tmp/coresmoke/` (Original wird nur gelesen), lädt den
echten Core wie XRShell (gleiche Umgebungsantworten, Startdatei-Regel, `xrshell.opt`,
Standardoptionen aus `src/core_defaults.h`, Port-Gerät nach `retro_load_game`, Quest-Tasten →
RetroPad über `padmap`) und spielt ein Eingabe-Skript (`tests/device/core_smoke/*.txt`).
Bericht je Spiel in `tests/out/core-smoke/<app>-<system>-<Ordner>/`: `bericht.txt`/`.json`
(retro_run-Zeiten gegen 72 Hz, Ton-Abweichung, Rumble, AV-Wechsel, Savestate-Rundlauf,
Optionsgültigkeit gegen die Liste des Cores, Schreibzugriffe im Spielordner), `core.log`, PNGs.
Ersetzt keine Headset-Abnahme.

## Offene Prüfpunkte fürs Headset

1. PrBoom: Doom II startet aus `games/doom/<Ordner>/`, Titelbild, Ton (OPL/MIDI-Synthese).
2. Linker Stick: laufen/seitwärts stufenlos; rechter Stick: drehen flüssig, Empfindlichkeit
   (Doom-Option „Mouse sensitivity“ wirkt auf den Stick).
3. Menü-Navigation in allen drei Cores: Doom über linken Stick + A/Trigger, Wolf über linken
   Stick vor/zurück + Trigger, Quake über linken Stick + A.
4. Doom-Ja/Nein-Abfragen („Quit?“, „Nightmare?“, Quicksave überschreiben): A/Trigger = Ja,
   Start = Nein (`<IWAD>.prboom.cfg`, siehe oben). Menüs weiter mit A bedienbar; Spielstand
   benennen und mit A bestätigen. Log `default file with base values` zeigt, dass die Datei geladen wurde.
5. Spielstände landen unter `files/saves/…`, nicht im Spielordner; nach Neustart ladbar.
6. Final Doom (TNT, Plutonia) und die 2024er-IWADs (`rerelease/`) in PrBoom.
7. Frame-Pacing: Leistungszeile zeigt **72,0** fps (0.3.3, vorher 71,6) bei 72 Hz Anzeige — Doom
   (Log `Core-AV: … @ 72.00 fps`) und Quake; nach Brille ab/auf wieder 72 Hz (Log
   `Bildwiederholrate 72 Hz für Core …`). Brille ab: Spiel pausiert, Ton aus (Log `Spiel pausiert (Grund: Fokus/Session)`).
8. DOSBox-Pure mit `xrshell_input = pad` (DOOM.EXE, xr.dosgames): Belegung aus `C:\DOS.YML`
   greift (Log „Content Provided Mapping“), Belegung wie in `docs/DOS-SPIELE.md` „Shooter“;
   ohne DOS.YML greift das Auto-Mapping („Detected Automatic GamePad mappings“).
9. ECWolf/TyrQuake: Start, Menüs, Spielstände (nachrangig). ECWolf: linker Stick vor/zurück
   über 0,5 setzt das Steuerkreuz und überschreibt dort den Analogwert (Tempo 70 statt bis 100) —
   prüfen, ob das Laufgefühl passt, sonst `ecwolf-analog-move-sensitivity` anpassen.

## Offene Punkte (Code)

- Quake II (vitaquake2) braucht `SET_HW_RENDER`.
- Spear-Missionspakete (`.sd2/.sd3`) als eigene Startregeln (Quake-Missionspakete: erledigt über `xrshell.start`).
- Analoge Trigger aus OpenXR (heute bool-Aktionen) — erst nötig, wenn ein Core sie nutzt.
- ~~Belegung pro Spiel änderbar machen~~ — erledigt: VR-Menü → Reiter „Tasten“ (`<Spielordner>.pad`, „für alle“: `games/<system>/_standard.pad`).
- Quake-Neuauflage (`rerelease/id1/`, 2021) wird nicht als Startdatei erkannt; erst prüfen, ob TyrQuake das Paket lädt (Core-Smoke-Test mit `xrshell.start = rerelease/id1/pak0.pak`).
