# XRShell — M0-Grundgerüst + M1 (erster Spiel-Frame: DOSBox-Pure in VR)

Basis-Layer für Classic-Game-Ports in VR auf Android-Headsets (Meta Quest, Pico,
künftig Android XR). Reine native C++/OpenXR-App, keine Engine, keine Assets.

Konzept-Dokument (Scope, Legal-Modell, Backlog): [`docs/Konzept_XR_Retro_BaseLayer.md`](docs/Konzept_XR_Retro_BaseLayer.md)

## Was M0 kann

- OpenXR-Session (Stereo, STAGE-Raum) mit OpenGL-ES-Rendering
- Virtueller Bildschirm (3,2 × 1,8 m) mit Prozedur-Testtextur — läuft ohne jegliche Assets
- Rechter Controller: Laser + Cursor per Raycast auf die Screen-Ebene
- Bildschirm startet zentriert vor dem Blick; linke Hand verstellt Breite/Abstand
- Beenden über Volume-Up (Quest-Konvention für Sideload-Apps)

## Was M1 kann (Issue #4/#5, Milestone „M1 – Erster Spiel-Frame")

- **Emulation:** DOSBox-Pure libretro-Core (GPL, libretro-Buildbot) läuft
  prozessintern auf eigenem Thread — Fixpoint-Timing auf dem vom Core gemeldeten
  fps-Wert (Condition-Variable, kein Busy-Waiting)
- **Video:** Core-Frames (XRGB8888) per Double-Buffer + `glTexSubImage2D` in der
  1024×768-Textur des Bildschirm-Quads — GLES ausschließlich im Render-Thread
- **Audio:** Core-Audio-Callbacks → SPSC-Ring (16384 Frames) → AAudio-Stream
  (Samplerate aus `retro_system_av_info`); Lautstärke fest 1.0, Schnittstelle da
- **Input:** Trigger = linke Maustaste, B = rechte Maustaste, rechter Thumbstick =
  Analog + DPad; Cursor per Raycast als Mausposition; Tastatur-Overlay (#5)
- **Overlay:** 68 Tasten (QWERTZ, Ziffern, F1–F12, Enter/Esc/Pfeile), halbtransparent
  unterhalb des Screens; Toggle per **linker Menu-Taste**, Tasten per Raycast + Trigger
- **Core-Bezug:** `./gradlew assembleDebug` lädt den Core zur Build-Zeit in
  `assets/cores/`; beim ersten Start extrahiert die App ihn nach `filesDir/cores/`
  (W^X ab Android 31)

### Bedienung (Quest Touch)

| Controller-Element | Wirkung |
| --- | --- |
| Rechter Trigger (auf Screen) | Linke Maustaste im Spiel (halten = ziehen) |
| Rechter Griff oder B-Knopf | Rechte Maustaste |
| Rechter Thumbstick | Pfeiltasten (z. B. Karte scrollen) + Joypad/Analog |
| Linker Griff halten + linker Stick | links/rechts = Breite, vor/zurück = Abstand |
| Linker Griff halten + linken Stick drücken | Fenster zurücksetzen und vor dem Blick zentrieren |
| Linke Menu-Taste | Tastatur-Overlay an/aus |
| Trigger auf Overlay-Taste | Taste halten/ tipppen (Maus-Klicks werden abgefangen) |
| Volume-Up | App beenden |

Komfort (Regeln aus XR-Settlers2.5): Die Leinwand startet zentriert vor der Blickrichtung
(nur Yaw, Mitte knapp unter Augenhöhe, 2 m Abstand, 2,4 m breit). Der Zeiger ist zeitabhängig
geglättet (τ 25 ms), der Zielpunkt friert 85 ms nach einem Klick ein, ein kurzer Haptik-Impuls
bestätigt den Klick. Ein Klick muss auf dem Screen beginnen; nach Fokusverlust (Systemmenü) wirkt
eine gehaltene Taste erst nach dem Loslassen wieder. Grenzen: Abstand 0,7–3,5 m, Breite 1–3,2 m
und 0,55–1,8 × Abstand.

### Spielablage (Bring Your Own Assets — siehe Konzept, Abschnitt 2)

Spieldateien **niemals ins Repo** — aufs Headset nach:

```
/sdcard/Android/data/xr.shell/files/game/
```

z. B. per `adb push SpielOrdner /sdcard/Android/data/xr.shell/files/game/`.
Gestartet wird (alphabetisch) die erste Datei direkt in `game/` mit DOSBox-Pure-Endung
(`.zip/.dosz/.exe/.com/.bat/.iso/.cue/.img/.conf` …), sonst der erste Unterordner als
Laufwerk C: mit DOSBox-Pure-Startmenü (kein eigener Browser — erst M3). Liegt dort
nichts, bleibt bewusst die Fallback-Testtextur (Schachbrett) sichtbar — die Shell
startet immer. Details: `TEST.md`, Abschnitt 3.

### Shooter (xr.shooter)

Eigene App `xr.shooter` (Flavor `fps`) mit PrBoom (Doom/Doom II/Final Doom), ECWolf
(Wolfenstein 3D) und TyrQuake (Quake) als libretro-Cores. Spiele sind Ordner unter
`files/games/doom|wolf3d|quake/<Ordner>/` (eigene Original-Dateien), Auswahl im VR-Menü.
Steuerung als Gamepad mit Analogsticks (linker Stick laufen, rechter drehen, Trigger
feuern). Übertragen mit `APP_ID=xr.shooter SYSTEM=doom tools/push-game.sh <Ordner> <Name>`.
DOSBox-Pure kann per `xrshell_input = pad` in `xrshell.opt` denselben Gamepad-Modus nutzen
(Auto-Mapping des Cores). Details, Belegung und Prüfpunkte: [`docs/SHOOTER.md`](docs/SHOOTER.md).

### Noch nicht drin

- **Datei-Browser / Loader-UI:** erst M3; bis dahin festes `game/`-Verzeichnis
- **Lautstärke-UI** (Schnittstelle `setVolume` ist vorbereitet)
- Headset-getestet: Tippgefühl Overlay, Audio-Latenz, DOSBox-Pure-Spielbarkeit —
  erster On-Device-Lauf steht aus

## Projektstruktur

```
xrshell/
├── CMakeLists.txt              # Android-Toolchain + OpenXR via FetchContent
├── src/
│   ├── main.cpp                # android_main, Event-Loop, Frame-Orchestrierung
│   ├── android_glue.{h,cpp}    # EGL-Context auf dem NativeWindow
│   ├── xr_session.{h,cpp}      # Instance/Session/Swapchains/Actions, Frame-Loop
│   ├── quad_renderer.{h,cpp}   # Bildschirm-Quad, Laser, Cursor (GLES 3)
│   ├── libretro_adapter.{h,cpp}# M1: Core-Loader, Core-Thread, Frame-Double-Buffer, Input
│   ├── core_audio.{h,cpp}        # M1b: AAudio + SPSC-Ring
│   ├── keyboard_overlay.{h,cpp}  # #5: Tastatur-Overlay (Quad-Grid + Pixelfont)
│   └── libretro.h                # libretro-API (MIT, aus libretro-common)
└── android/
    ├── build.gradle            # externalNativeBuild → ../CMakeLists.txt
    │                           # + fetchDosboxPureCore: Core → assets/cores/ (gitignored)
    ├── settings.gradle
    ├── gradle.properties
    └── src/main/AndroidManifest.xml   # vr_only, Headtracking, Quest-Kategorien
```

## Voraussetzungen

Entwicklung läuft auf **macOS und Windows** (Details: `AGENTS.md`, Abschnitt Toolchain).

- JDK 17 (`JAVA_HOME` setzen)
- Android SDK (compileSdk 34, build-tools 34.0.0) + NDK **30.0.16248370** + CMake 3.22.1
  (Android Studio → SDK Manager, oder `cmdline-tools`)
- USB-Kabel, Quest mit aktiviertem Developer Mode
- `adb` (Android Platform Tools, Teil des SDK)

`android/local.properties` mit `sdk.dir=…` bzw. Umgebungsvariable `ANDROID_HOME` setzen.
Liegt das NDK außerhalb des SDK (z. B. Homebrew): `ndk.path=…` in `local.properties`
oder Umgebungsvariable `ANDROID_NDK`.

## Build

Variante A — Android Studio: Projektordner `android/` öffnen, Build → Build APK.

Variante B — Kommandozeile (Gradle-Wrapper liegt im Repo — nie systemweites `gradle` nutzen):

```bash
cd android
./gradlew assembleDebug          # Windows-PowerShell: .\gradlew.bat assembleDebug
# baut alle Apps (Flavors), z. B.:
#   android/build/outputs/apk/shell/debug/XRShell-shell-debug.apk   xr.shell    (DOSBox-Pure)
#   android/build/outputs/apk/dos/debug/XRShell-dos-debug.apk       xr.dosgames (DOSBox-Pure, Bibliothek)
#   android/build/outputs/apk/fps/debug/XRShell-fps-debug.apk       xr.shooter  (PrBoom, ECWolf, TyrQuake)
#   android/build/outputs/apk/adventure/debug/XRShell-adventure-debug.apk  xr.adventure (ScummVM)
#   android/build/outputs/apk/island/debug/XRShell-island-debug.apk xr.island   (DOSBox-Pure)
```

Variante C — nur die native Library (ohne APK, zum Code-Check):

```bash
export ANDROID_NDK=<pfad-zum-ndk>
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android
```

## Auf die Quest laden

```bash
adb install -r android/build/outputs/apk/shell/debug/XRShell-shell-debug.apk
# Dann: Bibliothek → Unbekannte Quellen → xr.shell
```

## Nächste Schritte (M2 → M3)

- [x] Cursor-Hit-Test-API (`RayHit raycastToScreen()`)
- [x] Grip = Bildschirm greifen/verschieben
- [x] M1a: libretro-Core-Loader + DOSBox-Pure → Core-Frames in die Bildschirm-Textur
- [x] M1b: Audio via AAudio (SPSC-Ring, Core-Samplerate)
- [x] M1c: Trigger → libretro-Maus, B → rechte Taste, Stick → Joypad/Analog
- [x] #5 virtuelles Tastatur-Overlay (QWERTZ, 68 Tasten, Menu-Taste togglet)
- [ ] M2: SDL→OpenXR-Shim (Adapter A) — CorsixTH/OpenTTD/ScummVM
- [ ] M3: Asset-Import-Assistent + Datei-Browser statt festem `game/`-Verzeichnis
- [ ] Lautstärke-UI + Audio-Komfort (M4)
- [ ] Culling/Orientierung des Laser-Quads verfeinern (Issue #3, Kosmetik)

## Lizenz

Empfehlung: MIT/Apache für XRShell selbst. Adapter an GPL-Engines bleiben GPL
(siehe Konzept, Abschnitt 2 und 7).

libretro-Cores sind nicht Teil des Repos; `build.gradle` lädt sie beim Build vom
libretro-Buildbot und packt sie ins APK:

- DOSBox-Pure — GPL-2.0
- ScummVM — GPL-3.0-oder-später
- PrBoom, ECWolf, TyrQuake — GPL-2.0-oder-später

Konsolen-Emulation ist bewusst nicht Teil dieses öffentlichen Stands.
