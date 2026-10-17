# XRShell — Stand und Rückroll-Punkte

Zuletzt aktualisiert: 2026-10-04. Gerät: Meta Quest 3. „Geprüft“ heißt: im Headset
beobachtet (Screenshots/Log), nicht nur gebaut.

## Rückroll-Punkte (Git-Tags)

| Tag | Commit | Inhalt | Zustand |
|---|---|---|---|
| `headset-ok-2026-09-27` | `038fb8f` | erste libretro-App läuft, VR-Menü v1 (Passthrough, Wölbung, Filter, Farbe), Einstellungen gespeichert | **im Headset geprüft** |
| `menu-v2-2026-09-27` | `d15fb15` | Menü v2 (Reiter, TTF-Schrift, Größe/Abstand, Spielstände, Profile, Umgebung exp.), Greifen mit Hebel + Drehung, Laser-Fix, App-IDs `xr.*` | gebaut, **noch nicht im Headset geprüft** |

Zurückrollen, z. B. auf den geprüften Stand:

```bash
git checkout headset-ok-2026-09-27     # ansehen / bauen
cd android && ./gradlew assembleDebug  # Achtung: dort noch App-IDs dev.xrshell*
```

## Apps

| App (Label = App-ID) | Inhalt | Spieldaten |
|---|---|---|
| `xr.shell` | DOSBox-Pure | `Android/data/xr.shell/files/game/` |
| `xr.island` | DOSBox-Pure (Windows 3.1) | `Android/data/xr.island/files/game/` |
| `xr.dosgames` | nur DOSBox-Pure, **Spielauswahl** (VR-Menü „Spiele“), Profile pro Spielordner — 0.3.6 (vc 9) gebaut, 0.3.5 (vc 8) installiert 2026-10-04; im Headset gelaufen bis 0.3.2 (Pirates! Gold, Worms United: Zeigerbefunde), **nicht abgenommen** | je Spiel ein Ordner in `Android/data/xr.dosgames/files/games/dos/<Ordner>/`; vorbereitet: Pirates! Gold, Pirates! (1987), Populous, Populous II, SimCity 2000, Syndicate Wars — siehe `docs/DOS-SPIELE.md` |
| `xr.adventure` | nur ScummVM (#28), **Spielauswahl** über Ordner, Standardfilter HD pro Spiel, Sprache/Untertitel automatisch — gebaut 2026-10-03, **nicht im Headset geprüft** | je Spiel ein Ordner mit `*.scummvm` in `Android/data/xr.adventure/files/games/adventure/<Ordner>/` (alte Ordner in `files/game/` gelten weiter) — siehe `docs/ADVENTURE.md` |
| `xr.shooter` | PrBoom (Doom/Doom II/Final Doom), ECWolf (Wolfenstein 3D/SoD), TyrQuake (Quake); Pad-Modus mit Analogsticks, Spielauswahl über Ordner — 0.3.5 (vc 8) gebaut, 0.3.4 (vc 7) installiert 2026-10-04; Doom II (0.3.0) und Quake (0.3.1) im Headset gelaufen, **nicht abgenommen** | je Spiel ein Ordner in `Android/data/xr.shooter/files/games/<doom\|wolf3d\|quake>/<Ordner>/` — siehe `docs/SHOOTER.md` |

Bis `038fb8f` hießen die Apps intern `dev.xrshell*` (gleiche Labels).
Cores und Spieldaten liegen nie im Repo (Build lädt die Cores, `.gitignore` sperrt Spieldaten).
Konsolen-Emulation ist nicht Teil dieses öffentlichen Stands.

## xr.shooter / xr.dosgames — Headset-Befunde, Core-Smoke-Test und Stand 0.3.5/0.3.6 (2026-10-04)

Im Headset beobachtet (Leistungszeile/Log, nicht abgenommen):

- **Doom II, xr.shooter 0.3.0:** läuft; 71,6 fps bei 90 Hz Anzeige → 0.3.1 fordert 72 Hz an.
- **Quake, xr.shooter 0.3.1:** läuft; 71,6 fps bei 72 Hz (71,9 Hz gemessen) → Core-Takt driftete
  (Aufwachverzögerung summierte sich), in 0.3.3 behoben.
- **Pirates! Gold, xr.dosgames:** Zeiger mit `direct` weit neben dem Laser → relative Maus, Abgleich.
- **Worms United, xr.dosgames:** liest nur Mickeys — `direct` bewegt den Pfeil nicht, relativ driftet
  er → „Zeiger lernen“ (Spielzeiger im Speicher an den Laser koppeln, `9a9e74b`); Diagnose-Log
  `xrshell_log_pointer` im Worms-Profil (`8c27a99`). Kopplung im Headset noch nicht bestätigt.

Gebaut in 0.3.3 (Zweig `shooter-qa`, **Quest-Abnahme offen**): Core-Takt mit festen Fristen
(72,0 statt 71,6 fps), Pause bei Fokusverlust/Brille ab (Ton aus, Rumble 0, kein Leerlauf ohne
Session), Bildwiederholrate nach Session-Neustart neu angefordert, Zeiger lernen robust gegen
Absturz/Programmwechsel und ohne gleichzeitiges Fangen, Bildschirmtastatur im Pad-Modus aus, Core
bleibt nach `retro_deinit` geladen (DOSBox-Pure-Thread), Mausmodus pro Core-Frame zwischengespeichert,
TyrQuake 48 kHz und Zielraten an die Cores, Dateirechte `.pad`/`*.user.opt`, SRAM-Fehler einmal
geloggt.

**Core-Smoke-Test auf der Quest (2026-10-04, `tests/run-core-smoke.sh`, ohne App):** 10 Spiele mit den
Steam-Daten des Nutzers laufen (Doom, Doom II, TNT, Sigil, Nerve, Master Levels, Quake, Quake MP1, DOS-
Doom II, DOS-Quake); PrBoom 1,3–1,5 ms/Bild, TyrQuake 2,7 ms, Rumble und Ton ok. Befunde → behoben in
0.3.4/0.3.5 (xr.shooter) bzw. 0.3.5/0.3.6 (xr.dosgames): Doom-Menü per Stick unerreichbar (jetzt
Steuerkreuz ab 0,9; „Quit → Ja“ bestätigt), Spiel-eigenes Beenden → Spielauswahl, DOSBox-Pure
`cycle_limit 0.85` (vorher p95 16 ms), DOS-Pad L3 = DOSBox-Menü, DOS-Spielordner beschreibbar (DOSBox-Pure
speichert direkt im Ordner). Dazu „Start im Menü + Weiterspielen“ (aus adventure-sword).
Prüfpunkte: `TEST.md` 3i (3, 5, 7–11), `docs/SHOOTER.md`, `docs/DOS-SPIELE.md`.

## Geprüft im Headset (Stand `038fb8f`, Menü v2 teilweise)

- OpenXR-Start, Stereo-Bild, Kopf-Tracking; Bild erscheint (FBO-Fix `b58f974`)
- libretro-Core lädt ein eigenes Spiel, Ton über AAudio (Ratenwandlung)
- Gamepad-Steuerung spielbar
- Schärfe: Sharp-Bilinear, 1,4× Supersampling (2352×2464 pro Auge), 4×-MSAA
- VR-Menü v1 auf der linken Menütaste, Spiel pausiert/läuft weiter
- Passthrough 20 % und 80 % (Raum sichtbar), Wölbung stark, Scanlines
- Einstellungen überleben den Neustart (`xrshell-view.cfg`)

## Gebaut, noch nicht geprüft (Stand `d15fb15`)

- Menü mit Reitern „Ansicht“ / „Spiel“, geglättete System-Schrift mit Umlauten
- Bildgröße (1–6 m) und Abstand (0,7–3,5 m) im Menü
- Spielstände (5 Plätze), Ansichtsprofile (3), Spiel beenden
- Umgebung ausblenden (experimentell): Himmel / + Horizont / Nur Strecke, schwarzer Rand weg
- Fenster greifen mit Hebel bis 5×, Bild dreht sich zum Kopf; beidhändig skalieren
- Laser entlang der Zielrichtung bis zum Treffer (#3)
- (2026-10-02) Schnellleiste in jedem Reiter: „● Video“ / „■ m:ss“ · „Schließen“ · „Beenden“ (erster Klick
  rot „Sicher?“, zweiter innerhalb 3 s beendet); Videoaufnahme MP4 (H.264 + AAC) nach
  `Movies/<App-ID>/` (Rückfall `Android/data/<App-ID>/files/videos/`); Audio-Ratenregelung ±2 %
  gegen Knackser bei langsamem Core. Geräte-Tests inkl. Menü-Vorschau (`tests/out/`) grün.
- (2026-10-02) Bibliotheks-Apps: Menü zweispaltig (1536 px, 1,62 m) — links Spieleliste mit Suchfeld
  (Kachel in Systemfarbe, System rechts, laufendes Spiel in Akzent, 7 pro Seite), Klick ins Feld
  blendet rechts eine QWERTZ-Tastatur mit Umlauten ein, Liste filtert live (Groß/klein, ä = a,
  ß = ss, Apostroph egal, Wörter in beliebiger Reihenfolge); B schließt erst die Tastatur.
  Stick: links/rechts wechselt zwischen Liste, Reitern und Tasten. Alle Tafeln: Glas mit Verlauf,
  Akzent-Haarlinie unter dem Titel, Fokuszeile mit Akzentbalken. Menü-Vorschau jetzt auch am PC
  (`tests/run-menu-preview.sh`, ohne Quest) inkl. Pixelvergleich Teil-Redraw = Vollbild.
- (2026-10-03) Tastenbelegung pro Spiel für Pad-Cores: VR-Menü-Reiter „Tasten“, Pad-Taste
  antippen + Quest-Taste drücken (Tausch bei Doppelbelegung), Sticks als Steuerkreuz / Analog / Aus,
  „Für alle <System>-Spiele“ und „Zurücksetzen“. Dateien `<Spiel>.pad` bzw.
  `games/<system>/_standard.pad`.

## Gebaut, noch nicht geprüft (xr.dosgames, xr.shooter, 2026-10-03)

- Core-Wahl nach Endung/Bibliotheksordner (`games/<system>/`), Leistungszeile im Log alle 10 s
- QS: Host-Test `tests/test_core_select.cpp`, Core-Smoke-Test (`tests/core_smoketest.cpp`)

- (2026-10-03) `xr.dosgames`: DOS-Sammlung mit Spielauswahl über Ordner (`xrshell.title` als Name,
  Plakette „DOS“), Tastenprofil und Menü-Optik wechseln mit dem Spiel, laufendes Spiel in der
  Auswahl = weiterspielen; `tools/prepare-dos-games.ps1` baut die Spielordner (AUTOBOOT, CD-Image
  auf C:, SB16), `push-game.sh` legt sie für `APP_ID=xr.dosgames` nach `files/games/dos/`. Gebaut,
  Host-Tests grün — Headset-Prüfliste in `docs/DOS-SPIELE.md`.
- (2026-10-03) `xr.dosgames` nach erstem Headset-Befund („Strahl neben dem Mauszeiger“, „grob“):
  DOS-Profile mit `dosbox_pure_mouse_input = direct` (Zeiger absolut unter dem Laser; Adapter-Standard
  bleibt `virtual`), neuer Filter **HD** (xBR-lv2) und **HD-Scanlines**, Standardfilter pro Spiel
  (`xrshell_filter` in `xrshell.opt`, `xr.dosgames` ohne Angabe HD; Menüwahl hat Vorrang). Gebaut
  (alle Flavors warnungsfrei), Host-Tests grün, Shader per WebGL2 gerendert (`tests/out/shader-*.png`),
  APK-Gate `xr.dosgames` bestanden — **Quest-Abnahme offen**, Prüfpunkte 11–13 in `docs/DOS-SPIELE.md`.
- (2026-10-03) `xr.dosgames`: Einstellungen pro Spiel im VR-Menü — Reiter „Bild“ (bisher „Ansicht“, plus
  „Größe passend zum Spiel“) und neuer Reiter „DOS“ (Mausmodus, Mausgeschwindigkeit, CPU-Tempo,
  Seitenverhältnis live; Grafikkarte nach Neustart; „Hohe Auflösung“ aus `hires = …` im Tastenprofil;
  Spiel neu starten; Spiel-Standard), gespeichert in `files/settings/<Ordner>.user.opt`.
  `GET_VARIABLE_UPDATE` neu im Adapter (alle Apps; ohne Menüänderung immer „nichts geändert“).
  Gebaut (alle Flavors warnungsfrei), Host-Tests + Menü-Vorschau grün (`tests/out/xrdos-menu-*.png`),
  APK-Gate `xr.dosgames` bestanden — **Quest-Abnahme offen**, Prüfpunkte 14–18 in `docs/DOS-SPIELE.md`.
- (2026-10-03) `xr.dosgames` nach Headset-Befund (Pirates! Gold VESA 640×480: Zeiger mit `direct` weit neben
  dem Laser): relative Maus mit Faktor pro Achse, „Zeiger fangen“ (Menü zu / Laser zurück aufs Bild)
  und geführtem Abgleich im Reiter DOS; Profile Pirates! Gold, SimCity 2000, Syndicate Wars auf
  `virtual`. Gebaut, Host-Tests grün — **Quest-Abnahme offen**, Prüfpunkte 19–20.
- (2026-10-03) `xr.dosgames` nach Nutzerwunsch (SimCity 2000: Karte per Griff ziehen): rechter Griff
  halten + Hand bewegen = Scroll-Tasten (Joystick ab Greifpunkt mit Totzone/Takt/Dauerdruck, Diagonale;
  „Hand folgt“ experimentell), Rechtsklick bleibt auf B; neuer Reiter „Steuerung“ (Griff, Zieh-Art,
  Richtung, Tempo, rechter Stick) pro Spiel; Scroll-Tasten pro Spiel (`scroll_*` in `xrshell.keys`).
  Gebaut (alle Flavors warnungsfrei), Host-Tests grün, APK-Gate `xr.dosgames` bestanden —
  **Quest-Abnahme offen**, Prüfpunkte 21–27 in `docs/DOS-SPIELE.md`.
- (2026-10-03) Shooter: neue App `xr.shooter` (PrBoom/ECWolf/TyrQuake, Doom-IWADs auch aus `base/`/`rerelease/`),
  Pad-Modus mit echten Analogsticks (`src/pad_analog.h`) für alle Pad-Cores; DOS-Spiele per `xrshell_input = pad`
  im Pad-Modus mit Belegung aus `C:\DOS.YML` (Doom-Reihe vorbereitet in `tools/prepare-dos-games.ps1`, Profile
  für Heretic, Hexen, Wolf3D, SoD, Duke 3D, Blood, Shadow Warrior, Quake). Alle Flavors warnungsfrei, Host-Tests
  grün, APK-Gate `xr.shooter`/`xr.dosgames` bestanden — **Quest-Abnahme offen**, keine Spieldaten lokal.
  Branch-Hinweis: xr-dos war am 2026-10-03 09:46 versehentlich auf xr-public zurückgesetzt; wiederhergestellt
  auf 35f4656 + Shooter-Commits (Sicherungen: `backup/xr-dos-vor-reset`, `backup/xr-dos-public-fb74613`).

## Gebaut, noch nicht geprüft (2026-10-03)

- Batteriespeicher (SRAM, `RETRO_MEMORY_SAVE_RAM`) eines Cores, der ihn nicht selbst schreibt, wird
  als `saves/<System>/<Spiel>.srm` gesichert und beim Start geladen: alle ~2 s bei Änderung, beim
  Öffnen des VR-Menüs, bei `APP_CMD_PAUSE/STOP` und vor dem Entladen.
- QS: Host-Test `tests/test_save_ram.cpp` (grün)

## Bekannt offen

- Bildrate: PAL 50 fps auf 72/90 Hz ruckelt leicht (Frame-Pacing/Refresh-Wahl)
- Zwei Spieler (Port 1) nicht belegt
- DOSBox-Pfad (xr.shell mit DOS-Spiel) seit dem Menü nicht erneut im Headset geprüft

## Headset-Rückmeldung 2026-09-28 (Menü v2)

- Umgebung „Himmel“ funktioniert: Himmel durchsichtig, die Spielanzeige schwebt frei im Raum
- Störend war der stehenbleibende schwarze Bildrand (Overscan-Zeilen oben, Randspalten)
  → behoben (Randerkennung + 1 Texel Sicherheitsrand), neue Stufe „Nur Strecke“ — ungeprüft
