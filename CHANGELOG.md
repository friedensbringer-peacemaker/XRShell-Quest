# Änderungen — XRShell-Apps

Eine Codebasis, mehrere Apps (Flavors). Versionen je App in `version.properties`
(`versionCode` steigt für jede weitergegebene APK). „Gebaut“ heißt: APK erzeugt und APK-Gate
bestanden — **nicht** im Headset geprüft. Geprüftes steht in `STATUS.md`.

| App | Flavor | Version |
|---|---|---|
| `xr.dosgames` | `dos` | 0.3.4 (versionCode 7) |
| `xr.shooter` | `fps` | 0.3.3 (versionCode 6) |
| `xr.adventure` | `adventure` | 0.3.4 (versionCode 8) |
| `xr.shell`, `xr.island` | `shell`, `island` | 0.1.1 (versionCode 2) |

Bis hierher hatten alle Apps versionCode 1 / versionName `0.1.0-m0+<Commit>`.

## Unveröffentlicht

### Öffentlicher Stand — ohne Konsolen-Emulation (rechtliche Bereinigung, 2026-10-17)
- Konsolen-Apps und -Cores entfernt: Flavors, Core-Downloads, Manifest, Menü-Optik, Import aus
  `Download/`, Konsolen-Tastenbelegungen und zugehörige Tests/Doku. Im öffentlichen Stand bleiben
  DOSBox Pure, ScummVM, PrBoom, ECWolf, TyrQuake, der libretro-Host, Profile und prepare-Skripte.
- Unbekannte Pad-Systeme fallen auf eine allgemeine RetroPad-Belegung zurück.
- Nachweis: gebaut (native Bibliothek aller verbleibenden Varianten warnungsfrei, Host-Tests grün);
  nicht installiert, nicht im Headset geprüft.

### xr.adventure 0.3.4 (vc 8) — Menü aufgeräumt (Prüfung gegen xr-vr-menu, Vorschauen `xrshell-menu-adventure-audit-*.bmp`)
- **Bedienungshilfe** unten im Reiter „Spiel“: Trigger Klick · B Ansehen · A Spielmenü / Griff Fenster ·
  X Zoom · Y Tastatur · Stick Szene überspringen (A/Stick nur, wenn das Tastenprofil sie so belegt).
- **Spieleliste:** in Apps mit nur einem System (xr.adventure, xr.dosgames) keine System-Plakette mehr
  („Adventure“ bei jedem Spiel), dafür Kachelfarbe je Titel statt überall Grün.
- **„Umgebung (exp.)“** (Himmel/Horizont ausblenden) nicht mehr in den Ordner-Apps
  xr.adventure, xr.dosgames, xr.shooter.

### xr.adventure 0.3.3 (vc 7)
- **ScummVM-Menü auf Deutsch** (Fortsetzen, Laden, Speichern, Optionen …): `scummvm.ini` bekommt
  `gui_language` nach Gerätesprache (de → de_DE); ohne den Eintrag blieb ScummVM englisch, obwohl
  `translations.dat` schon in `system/scummvm/theme/` liegt (aus ScummVM.zip). Vorhandene ini: Eintrag
  nur nachgetragen, eigene Wahl bleibt. Am PC mit Baphomets Fluch 2 geprüft (Strg+F5 → deutsches Menü).

### xr.adventure 0.3.2 (vc 6) — Headset-Befunde 2026-10-10 (Baphomets Fluch 1 lief, 60/60 fps)
- **Speichern/Laden auffindbar:** Reiter „Spiel“ hat statt des Hinweises den Knopf **„Spielmenü öffnen
  (Speichern/Laden)“**; er sendet nach dem Schließen die Profil-Eingabe `game_menu` (ScummVM-Standard
  Strg+F5 = ScummVM-Menü mit Laden/Speichern/Optionen). Am PC mit beiden Spielen geprüft
  (`core_smoketest` mit `SMOKE_KEYS`/`SMOKE_SHOTS`).
- **Tastenprofile Baphomets Fluch 1/2** (`profiles/broken-sword-1/2.keys`, legt `prepare-adventure.sh`
  als `xrshell.keys` dazu): A = ScummVM-Menü, rechten Stick drücken = Zwischensequenz überspringen (Esc).
- **Rechter Griff greift das Fenster** (vorher Rechtsklick, der auch auf B liegt). Neue Griffart
  „Fenster greifen“ (`xrshell_grip_right = window`), in xr.dosgames im Reiter Steuerung wählbar;
  beide Griffe zusammen skalieren weiterhin.

### xr.adventure 0.3.1 (vc 5)
- **Start-Hänger behoben** (aus xr-dos `077c93a`): 0.3.0 blieb nach „XRShell bereit“ ohne Bild
  stehen (Ereignisschleife wartete nach INIT_WINDOW endlos, eingeschleppt mit `64cdf19`).
- Neue Ladeanzeige mit Schritten/Restzeit/Systemcheck und Startbild (aus xr-dos `648014b`, `3181426`).

### Alle XRShell-Apps — Startbild, Ladeanzeige, Systemcheck, Fehlerseite (xr.dosgames 0.3.7 vc 10, xr.shooter 0.3.6 vc 9; übrige beim nächsten Build)
Nutzerwunsch 2026-10-08 („Startmenü wie bei OpenRA, sehen wie weit das Laden ist“); Standard L1–L8
aus dem Skill xr-loading-screen.
- **L1 Quest-Startbild:** `com.oculus.ossplash` im Manifest, `assets/vr_splash.png` je Flavor zur
  Build-Zeit erzeugt (`build.gradle`, `generateSplash<Flavor>`, Java-AWT): `xr.<name>`, Version aus
  `version.properties`, „Start dauert etwa N s“, keine Spielgrafik.
- **L2/L5 Ladeanzeige an der Bildstelle:** eigene Tafel (`src/load_screen.*`, 1280×800, CPU-gerastert
  wie das VR-Menü) im Spielfenster, ab dem ersten XR-Frame (Log „Ladeanzeige sichtbar nach X s“).
  Wandernder Lichtpunkt im Balken (Shader, 1,4 s je Durchlauf) und „seit N s“.
  **`adapter.start()` (dlopen, retro_init, retro_load_game) läuft jetzt auf einem eigenen Lade-Thread**;
  die XR-Schleife fasst den Adapter währenddessen nur über Etappen-Atomics an (Menü eingeschränkt:
  Fenster, Zentrieren, Schließen, Beenden; Pause/Speichern/Import erst danach). Gilt beim App-Start,
  beim Spielwechsel aus der Bibliothek und bei „Spiel neu starten“.
- **L3/L4 Schritte und Restzeit:** Systemcheck → (Vorheriges Spiel beenden) → Spieldaten prüfen →
  Core laden → Spiel laden → Erstes Bild, erledigte mit Dauer. Messwerte in `files/xr-load-times.json`
  (gleitendes Mittel α 0,3, Ausreißer halb, pro Core, `first_run` nach Installation/Update), Restzeit
  „noch etwa N s“ (5-s-Raster), „dauert länger als sonst (üblich: X) – läuft noch“. Ohne Verlauf
  Standardwerte aus dem Core-Smoke-Test (prboom 55 ms, tyrquake 120 ms, dosbox_pure 100 ms +
  1,5 s bis zum Grafikmodus).
- **Startsignal:** erster Video-Frame mit Inhalt (Raster 64×48, nicht nur Schwarz); DOSBox-Pure:
  erst ein Frame mit Inhalt in anderer Bildgröße als der allererste (Grafikmodus des Spiels nach
  AUTOBOOT), sonst 3 s nach dem ersten Inhalt (Textmodus-Programme); nur schwarze Bilder: fester
  Zeitpunkt nach max(8 s, 2 × erwartet). Log „Spiel sichtbar nach X s“ und „Ladeschritt …: N ms“.
- **L6 Systemcheck** (✓/⚠/✗): OpenXR-Laufzeit, Controller-Profil (nach 3 s), Spieldaten/Startdatei,
  Core im APK, freier Speicher, Akku, Sitzungslog, letzter Start abgestürzt (Marker `files/xr-running.marker`).
- **L7 Fehlerseite statt Hänger:** Start-/Core-/Datenfehler mit Klartext, letzter Core-Zeile, Abhilfe
  (`tools/push-game.sh`-Befehl passend zu App und Spielordner), Logpfad + `tools/pull-logs.sh`,
  App und Version; Watchdog: 10 s ohne Fortschritt → Hinweis + Logzeile je 10 s, nach max(30 s,
  2 × erwartet) Fehlerseite. B = Spielbild trotzdem zeigen, Menütaste = VR-Menü (Beenden).
  Hängt der Lade-Thread beim Beenden, endet der Prozess nach dem Aufräumen hart (`_exit`).
- **L8:** Strahl/Cursor und Eingaben ans Spiel erst ab dem ersten Spielbild, gehaltene Tasten erst
  loslassen; Kurzbedienung (2 Zeilen) und `xr.<name> <Version>` auf der Tafel. Bibliotheks-Apps
  zeigen die Tafel ohne Spiel als Startseite („Spiel im Menü auswählen“) hinter der Spieleliste.
- Tests: `tests/test_load_status.cpp` (Parameter-Sweeps: Mittel, JSON-Rundreise, Fortschritt
  monoton, Startsignal 35–72 Hz, Watchdog, Systemcheck, Fehlertexte), `tests/host/load_preview.cpp`
  (PNGs `tests/out/xrshell-load-*.png`).

Nachweis: gebaut (`assembleDebug`, alle 7 Flavors, siehe Commit), Host-Tests und NDK-clang
`-Wall -Wextra -Werror -fsyntax-only` grün, APK-Gate mit `ossplash` und `assets/vr_splash.png`.
Nicht installiert, nicht im Headset geprüft (Startbild, Ladeanzeige, Threading auf dem Gerät offen).

### Alle XRShell-Apps — Start-Hänger behoben (2026-10-08)
- **Kein Bild nach dem Start, Musik läuft:** Seit 0.3.3 (`64cdf19`, Pause ohne Session) wurde die
  Wartezeit der Ereignisschleife einmal vor der Schleife berechnet; beim Start (`xrReady` noch falsch)
  blieb sie auf „endlos warten“, die XR-Session wurde nie begonnen. Jetzt pro Aufruf berechnet.
  Betroffen: alle Builds ab `64cdf19` (xr.shooter 0.3.3+, xr.dosgames 0.3.3+,
  xr.adventure 0.3.0, xr.island/xr.shell ab diesem Stand). Headset-Befund xr.shooter 0.3.4, Quake.

### Alle Bibliotheks-Apps (xr.adventure 0.3.0 vc 4; xr.dosgames/xr.shooter beim nächsten Build)
- **Start im Menü statt sofort im letzten Spiel** (Nutzerwunsch 2026-10-04): die App öffnet die
  Spieleliste; im Reiter „Spiel“ steht oben **„Weiterspielen: <Titel>“** (gefüllter Knopf), das
  zuletzt gespielte Spiel trägt in der Liste das Abzeichen „zuletzt“ und die Liste steht auf seiner
  Seite. Neue Einstellung **„Beim Start: Menü / Letztes Spiel“** (Reiter „Spiel“, global in
  `xrshell-view.cfg` als `start_mode`, nicht in Profilen) stellt das alte Verhalten wieder her.
  Test `testResume` in `test_menu_preview.cpp` (Vorschau `xrshell-menu-weiterspielen.bmp`).

### xr.dosgames 0.3.6 (vc 9) / xr.shooter 0.3.5 (vc 8)
- Start im Menü mit „Weiterspielen“ (aus adventure-sword `daeeac7`), Reiter „Spiel“ ohne
  Spielstand-Knöpfe bei Cores ohne Savestates (Quake/TyrQuake, ScummVM).

### xr.dosgames 0.3.5 (vc 8) / xr.shooter 0.3.4 (vc 7) — Befunde des Core-Smoke-Tests auf der Quest
- Doom (PrBoom): Menü-Cursor per linkem Stick (Steuerkreuz ab 0,9) — vorher waren Optionen/Laden/
  Speichern/Beenden unerreichbar. „Quit Game“ → A = Ja bestätigt (`36f0ca0`).
- Spiel beendet sich selbst (RETRO_ENVIRONMENT_SHUTDOWN): Bibliothek zeigt die Spielauswahl,
  Einzelspiel-Apps beenden sich (`36f0ca0`).
- DOSBox-Pure: `cycle_limit` 0,85 (vorher 100 % → retro_run p95 16 ms > 13,9 ms Budget); DOS-Pad:
  linken Stick drücken = L3 (DOSBox-Pure-Menü); Spielordner werden von `push-game.sh` beschreibbar
  übertragen (DOSBox-Pure speichert direkt im Ordner).
- Core-Smoke-Test (`tests/run-core-smoke.sh`): 10 Spiele mit echten Daten auf der Quest — Doom/Doom II/
  TNT/Sigil/Nerve/Master Levels 1,3–1,5 ms pro Bild, Quake 2,7 ms, Rumble ok, Ton ohne Unterlauf;
  Quake hat keine Savestates (Größe 0) → nur Speichern im Quake-Menü.

### xr.adventure 0.2.1 (vc 3)
- **Baphomets Fluch 1 und 2** (ScummVM `sword1`/`sword2`): Profile `profiles/broken-sword-1/2.scummvm`
  und `tools/prepare-adventure.sh` — prüft die Pflichtdateien der Originalfassung (Director's Cut/
  Remastered laufen nicht → Abbruch mit Hinweis), benennt `speech.clu`/`music.clu` der zwei CDs in
  `speech1/2`, `music1/2` um, lässt Programm/Installer/GOG-Hülle weg, legt `.scummvm`, `xrshell.title`
  und `xrshell.opt` (Filter „scharf“ für 640×480) an. Host-Test `tests/test_prepare_adventure.sh`.
- **Reiter „Spiel“ ohne Spielstand-Knöpfe, wenn der Core keine kann** (ScummVM: `retro_serialize_size`
  = 0). Vorher meldete jeder Klick nur „Speichern fehlgeschlagen“; jetzt Hinweis „im Spiel speichern“.
  Gilt für alle Apps, geprüft einmal je Spiel nach dem ersten Frame. Vorschau
  `tests/out/xrshell-menu-adventure-spiel.bmp`, Test in `test_menu_preview.cpp`.
- **Core-Smoke-Test auch am PC:** `tests/core_smoketest.cpp` baut mit llvm-mingw für Windows
  (Core-DLL vom Buildbot), liest Kern-Optionen aus `./core.opt`, zeigt mit `SMOKE_LOG=info` Optionen,
  nicht unterstützte Umgebungsbefehle und Dauer je `retro_run`.

### xr.dosgames 0.3.4 (vc 7) — Bildschirmtastatur (Y), gilt auch für xr.island/xr.shell beim nächsten Build
- **Bildschirmtastatur passt zur Tastaturbelegung des Spiels** (`b0588ae`): DOSBox-Pure übersetzt
  nach Tastenposition. Mit deutscher Belegung im Gast (`dosbox_pure_keyboard_layout = gr`, z. B.
  Windows 3.1 bei Holiday Island) waren Y/Z vertauscht und ß/´ als „-“/„=“ beschriftet; mit der
  US-Standardbelegung lieferte „-“ unten ein „/“ und Ue/Oe/Ae/#/+ US-Satzzeichen. Jetzt QWERTZ bei
  `gr`, sonst QWERTY, jeweils richtig beschriftet. „<“ war nie beim Spiel angekommen
  (`RETROK_LESS` statt `RETROK_OEM_102`), „Pfeil runter“ war unbeschriftet. Host-Test
  `test_keyboard_layout` (jede Taste erreicht DOSBox-Pure, alle Zeichen haben eine Glyphe).
- Neues Profil `profiles/ydkj.keys/.opt` (Quiz-Reihe unter Windows 3.1, xr.dosgames).

### Alle Pad-Apps
- **Spielstände getrennt nach System:** `saves/<system>/<Spiel>` statt `saves/<Spiel>`; Ordner
  behalten Punkte im Namen („Doom v1.9“ ≠ „Doom v1.666“). Vorhandene Dateien werden beim ersten Start
  übernommen (nur wenn am neuen Ort noch nichts liegt). „Nicht schreibbar“ nur noch einmal im Log.
- **Batteriespeicher (SRAM) wird gesichert:** `saves/<system>/<Spiel>.srm` für Cores, die ihn nicht
  selbst schreiben; geladen beim Start, geschrieben bei Änderung (alle ~2 s, VR-Menü, Quest-Home/Pause,
  Beenden).
- **Tastenbelegung pro Spiel:** Reiter „Tasten“ — Taste antippen, Quest-Taste drücken (Tausch bei
  Doppelbelegung), Sticks Steuerkreuz/Analog/Aus, „Für alle <System>-Spiele“, „Zurücksetzen“; Dateien
  `<Spiel>.pad` bzw. `games/<system>/_standard.pad`.
- **Menü zweispaltig** (Bibliotheks-Apps): links die Spieleliste mit Suchfeld (Klick →
  Bildschirmtastatur, Liste filtert live; Groß/klein egal, ä = a, ß = ss, Wörter in beliebiger
  Reihenfolge), rechts die Reiter.

### Alle Apps
- **Sitzungslog auf der Brille:** jede Logzeile (Tags `XRShell`, `XRShell-Core`) zusätzlich in
  `Android/data/<APP_ID>/files/logs/session-JJJJMMTT-HHMMSS.log` — Kopf mit App-ID, Version, Gerät,
  Sprache, Startzeit; letzte 10 Sitzungen, 5 MB je Datei, spätestens alle 2 s gespeichert. Abholen
  per `tools/pull-logs.sh [APP_ID]`. Leistungszeile alle 10 s: Spiel-fps Ist/Soll, XR-fps,
  Bildgröße, Filter. (`6cbf8fd`)
- **Versionierung:** `version.properties` je App, Gradle prüft die Werte; Version in der
  VR-Menü-Kopfzeile und im Sitzungslog-Kopf.
- Maus (relativ): ein Bildgrößenwechsel (DOS-Videomodus, ScummVM-Menü) lässt den Zeiger nicht mehr
  um die Größendifferenz springen. (`d5736f1`)
- VR-Menü öffnet ohne Vollbild-Neuaufbau, Stick blättert die Spieleliste, Schrift-Cache, lange
  Titel werden per Halbierung gekürzt; ruckelfreier (Vollbild 4× schneller, Reiterwechsel zeichnet
  nur die rechte Spalte). (`3799aa2`, `2ccc211`, `1bf1161`)
- **Abstand-Fix:** Das Bild startete nach dem Menü-Öffnen „vor der Nase“ — der Abstand kam aus der
  momentanen Kopfposition; jetzt der eingestellte Abstand, Messfehler unter 1 m werden verworfen.
  (`35f4656`)
- Core-Takt folgt `SET_SYSTEM_AV_INFO` (Bildratenwechsel zur Laufzeit); libretro-Rumble → Vibration
  am rechten Controller. (`77f30bd`, `980b5ce`)

### xr.dosgames 0.3.3 / xr.shooter 0.3.3 (vc 6)
- **Core-Takt ohne Drift:** feste Fristen statt „Beginn + Periode“ — 72,0 statt 71,6 fps
  (Headset 0.3.0/0.3.1); nach Pause/Rückstand setzt der Takt neu auf, nie Aufholen in Salven.
  `src/frame_pacer.h`, Host-Test `test_frame_pacer` (153 Prüfungen). (`15e7182`)
- **Pause bei Fokusverlust/Brille ab:** eigener Pausengrund neben dem VR-Menü (Systemmenü, Brille
  ab, keine Session) — Ton stumm, Rumble 0, SRAM gesichert; ohne Session pollt die Hauptschleife mit
  100 ms statt leer zu drehen. (`64cdf19`)
- **Bildwiederholrate** nach Session-Neustart erneut angefordert; Ratenwechsel von außen geloggt
  und neu geprüft. (`d66c3f9`)
- **Zeiger lernen** (xr.dosgames): Speicherkarte nach Absturz-Meldung/SHUTDOWN verworfen,
  Lernen an eine Kartengeneration gebunden (Abbruch mit Meldung bei Programmwechsel), Lernzustand
  bei Spielstart/-stopp zurückgesetzt (`9e24b1b`); beim Schließen des Menüs kein gleichzeitiges
  Fangen, Fangen nach gescheitertem Lernen (`d3ae655`).
- **Bildschirmtastatur** im Pad-Modus ausgeblendet und nicht gezeichnet. (`391dc45`)
- **Spielwechsel:** Cores bleiben nach `retro_deinit` geladen (kein `dlclose`, während der
  DOSBox-Pure-Emulationsthread noch ausläuft). (`7293d5b`)
- **Leistung:** Mausmodus/Log-Schalter pro Core-Frame aus dem Zwischenspeicher
  (`OptionStore::revision`). (`588c4cd`)
- **Ton/Bildrate:** TyrQuake `tyrquake_sound_samplerate = 48000`; `GET_TARGET_SAMPLE_RATE`
  (AAudio-Rate) und `GET_TARGET_REFRESH_RATE` (Anzeige-Rate) beantwortet. (`077c89e`)
- **Rechte/Log:** `.pad` und `settings/*.user.opt` 0664 (Ordner 0775), SRAM-Schreibfehler nur
  einmal pro Ursache im Log. (`2f861aa`)
- **QS:** Core-Smoke-Test für die Quest — echte Cores mit den echten Spieldaten ohne App/Brille
  (`tests/run-core-smoke.sh`, `tests/device/core_smoke.cpp`); Standard-Kernoptionen in
  `src/core_defaults.h` (gemeinsam für App und Test, Host-Test `test_core_defaults`). (`13f2da5`)

Nachweis: **gebaut** (`gradlew assembleDebug`, alle Flavors warnungsfrei), Host-Tests grün,
`BUILD_ONLY=1 tests/run-core-smoke.sh` baut. Der Core-Smoke-Test lief noch nicht auf der Quest
(nicht verbunden). **Nicht installiert, nicht im Headset geprüft.**

### xr.dosgames 0.3.2 / xr.shooter 0.3.2
- Sitzungslog-Ordner für adb lesbar (0775/0664, `1011c91`); DOS-Profile SimCity 2000 800×600,
  Pirates! (1987) Feuerknopf/Tempo, Populous Scroll, Syndicate Wars F8 (`8ac9585`); Spielzeiger
  verschieben im Reiter Zeiger (`9899a0d`). Headset 0.3.1: Quake 71,6 fps bei 71,9 Hz Anzeige.

### xr.dosgames 0.3.1 / xr.shooter 0.3.1
- Quest-Bildwiederholrate passend zur Core-Bildrate (XR_FB_display_refresh_rate): 72 fps
  (PrBoom, TyrQuake) und DOSBox-Pure 70 Hz → 72 Hz statt 90 Hz; sonst Standardrate.
  Headset-Befund 0.3.0: Doom II 71,6 fps bei 90 Hz Anzeige. (`src/refresh_rate.h`)
- Bibliotheksordner `files/games/…` für adb beschreibbar (vorher 0750 → push-game.sh scheiterte).

### xr.dosgames 0.3.0 / xr.shooter 0.3.0
- Rumble → Vibration am rechten Controller (PrBoom, TyrQuake). (`980b5ce`, `6e72ec1`)
- Stick als Analog + Steuerkreuz (TyrQuake, ECWolf): Steuerkreuz erst ab 0,9 Ausschlag; Core-Takt
  folgt Bildratenwechseln (auch DOSBox-Pure 60/70 Hz). (`658e1aa`, `77f30bd`)
- DOS-Shooter: Filter Scharf statt HD; Quake (DOS) mit Musik als CD-Image (`QUAKE.CUE` + OGG);
  Final Doom über `FINAL.EXE` (Steam). (`ad555b5`, `6e72ec1`, `49cf647`)
- xr.shooter: Quake-Missionspakete und Soundtrack, Doom-Zusatz-WADs (Sigil, Sigil II, No Rest for
  the Living, Master Levels) über `prepare-shooter.ps1`. (`6e72ec1`, `49cf647`)

### xr.dosgames 0.2.0 (neu)
- DOS-Spielesammlung mit Spielauswahl über Spielordner (`files/games/dos/<Ordner>/`, Titel aus
  `xrshell.title`), Tastenprofil und Menü-Optik wechseln mit dem Spiel; App-ID vorher `xr.dos`.
  (`5d52fe3`, `f3e024e`)
- **HD-Filter (xBR)** als Standard, Filter und Bildwerte pro Spiel; VR-Menü mit Reitern „Bild“ und
  „DOS“ (Kern-Optionen pro Spiel). DOS-Shooter im Pad-Modus: Filter „Scharf“. (`9783b75`,
  `ed8195a`, `ad555b5`)
- **Maus:** Spielzeiger unter dem Laser; relative Maus mit Faktor pro Achse, „Zeiger fangen“ und
  Abgleich. (`9783b75`, `5daa322`)
- **Karte ziehen** mit dem rechten Griff, Reiter „Steuerung“ (Griff/Ziehart/Richtung/Tempo,
  rechter Stick). (`b526097`)
- DOS-Shooter (Doom-Reihe u. a.) im Pad-Modus vorbereitet, Profile. (`b6b03c1`, `7df2e5c`)

### xr.adventure 0.2.0
- ScummVM-libretro-Core (#28). (`6e7d506`)
- **Spielauswahl** für mehrere Adventures: `files/games/adventure/<Ordner>/` mit `*.scummvm`;
  alte Ordner in `files/game/` erscheinen weiter. `push-game.sh` mit `APP_ID=xr.adventure`.
  (`d5736f1`)
- **Sprache und Untertitel** automatisch: `scummvm.ini` mit Gerätesprache und `subtitles=true`
  wird angelegt, falls sie fehlt (bisher per run-as von Hand). (`d5736f1`)
- **HD-Filter (xBR)** als Standard, Bildwerte pro Spiel gespeichert. (`d5736f1`)
- **Maus:** Zeiger wird beim Start und nach jedem Wechsel Spiel ↔ ScummVM-Menü unter den Laser
  geholt (Analyse in `docs/ADVENTURE.md`). (`d5736f1`)

### xr.shooter 0.2.0 (neu)
- PrBoom (Doom), ECWolf (Wolfenstein 3D), TyrQuake (Quake), Spielauswahl über Ordner, Pad-Modus mit
  Analogsticks. (`5ad1d68`)
- Schärferes Bild (960×600, PrBoom in Echtfarben, 2 Render-Threads, 48 kHz), PrBoom mit 72 fps und
  erreichbarer Ja-Taste, `prepare-shooter.ps1`. (`caddfbd`, `c50ed1b`, `f5eea61`)

### Nachweis
- **Gebaut:** siehe Abschnitt „Nachweis der Builds“ unten (APK, versionName/versionCode, SHA-256,
  Tests, APK-Gate).
- **Installiert:** nein (kein Gerät verbunden, bewusst nichts auf die Quest).
- **Im Headset bestätigt:** noch offen — Prüfpunkte in `docs/ADVENTURE.md` und `TEST.md`
  (Sitzungslog).
- **Nicht testbar ohne Headset:** Sitzungslog-Rechte (adb pull ohne root), ScummVM-Sprache und
  Zeigerverhalten beim Menüwechsel, Versionsanzeige im Menü.

### Nachweis der Builds (2026-10-03, Commit `e3fa641`, Debug-Signatur dieses Rechners)

| App | APK | versionName / versionCode | SHA-256 |
|---|---|---|---|
| `xr.dosgames` | `android/build/outputs/apk/dos/debug/XRShell-dos-debug.apk` | `0.2.0+e3fa641` / 2 | `6ebe47ef57af3183d067881fa14d2186bdd7bac6a0f201e11d852b96a2db96bd` |
| `xr.adventure` | `android/build/outputs/apk/adventure/debug/XRShell-adventure-debug.apk` | `0.2.0+e3fa641` / 2 | `84fb72223d68b06ba311de12978620ed5eb7c782ee6400fc2d4d158e13df1e5f` |
| `xr.shooter` | `android/build/outputs/apk/fps/debug/XRShell-fps-debug.apk` | `0.2.0+e3fa641` / 2 | `c0bf7a34ef22d111bbe1ce43d670b55b1f0bac18b57defb3152bfddc4a513099` |

- Alle 7 Flavors warnungsfrei gebaut (`-Wall -Wextra`).
- Host-Tests (`tests/run-host-tests.sh`) grün: key_profile 534, core_select 1768, pad_mapping 162,
  game_folders 1625, pointer_map 55 939, option_store 4136, mouse_relative 10 898, map_drag 43 154,
  pad_analog 90 268, scumm_setup 316, session_log 7902, menu_preview 63 — je 0 Fehler.
- APK-Gate (`xr-port-qa/scripts/apk-gate.sh`) für alle drei bestanden, ohne Versionswarnung; bleibende
  Empfehlung `resizeableActivity=false` (schon vorher offen).
- Nicht installiert, nicht im Headset geprüft.
