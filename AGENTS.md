# AGENTS.md — Kontext für ausgelagerte Aufgaben (Subagents)

Diese Datei ist die erste Anlaufstelle für jede delegierte Aufgabe an diesem Repo.
Sie ergänzt `docs/Konzept_XR_Retro_BaseLayer.md` (Scope, Legal-Modell, Backlog) um
Toolchain- und Workflow-Fakten, die nur auf dieser Maschine gelten.

## Projekt in einem Satz

XRShell ist der Basis-Layer (M0→M3), mit dem klassische PC-Spiele legal in VR
portiert werden: Plattform liefert OpenXR-Runtime + virtuelle Bildschirme +
Controller-als-Maus; der Spieler liefert ausschließlich eigene Original-Assets;
Engine-Adapter bauen auf Open-Source-Reimplementierungen auf (DOSBox/libretro,
SDL2-Shim für CorsixTH/OpenTTD/ScummVM, später openblack/KeeperFX).

## Harter Rahmen (niemals verletzen)

1. **Keine Spiele-Assets jemals ins Repo** (kein ROM, ISO, GOG-Installer, Disk-Image).
   `.gitignore` blockt bewusst die üblichen Endungen. Adapter validieren nur.
2. **Keine Kopierschutz-Umgehung**, keine Markennamen in App-/Adapter-Namen.
3. **C++-Code bleibt fehler- und warnungsfrei** (`-Wall -Wextra`, ausgenommen
   `-Wmissing-field-initializers` wegen des OpenXR-`{XR_TYPE_X}`-Idioms).
4. **Subagents committen, pushen nur der Parent-Agent** (nach Prüfung).
5. Manifest-VR-Features (`vr_only`, `vr.headtracking` required) nicht lockern.
6. App-Label (`android:label`) eigener Ports immer `xr.<name>` in Kleinbuchstaben
   (z. B. `xr.shell`), damit sie auf der Quest unter „Sonstige Quellen“ zusammenstehen.

## Repo-Layout

```
CMakeLists.txt         # native Lib (arm64), OpenXR-SDK via FetchContent
src/main.cpp           # android_main, Event-Loop, Frame-Orchestrierung
src/android_glue.*     # EGL-Context (GLES 3)
src/xr_session.*       # OpenXR: Instance/Session/Swapchains/Actions/Frame-Loop
src/quad_renderer.*    # Bildschirm-Quad, Laser, Cursor + raycastToScreen() API
android/               # Gradle-Projekt (App-Modul = Root-Projekt!)
docs/                  # Konzept-Dokument (Pflichtlektüre für größere Aufgaben)
```

## Toolchain (zwei Entwicklungsrechner: macOS und Windows)

Gemeinsam: JDK 17, Android SDK (compileSdk 34, build-tools 34.0.0), NDK **30.0.16248370**,
CMake 3.22.1. Wrapper = Gradle 8.11.1, AGP 8.7.3. **NIEMALS `gradle` direkt aufrufen** —
immer den Wrapper (auf dem Mac liegt Brew-Gradle 9.x im System und bricht).

**macOS** (Homebrew, kein sudo):

```bash
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
export ANDROID_NDK=/opt/homebrew/share/android-ndk        # r30, liegt außerhalb des SDK
```

**Windows** (SDK-verwaltetes NDK, Benutzer-Umgebungsvariablen gesetzt;
`<XR-Ordner>` = gemeinsamer Ordner aller XR-Projekte, dieses Repo liegt unter
`<XR-Ordner>/XR-Ports-Platform/XR-Ports-Platform`):

```
JAVA_HOME    = C:\Program Files\Eclipse Adoptium\jdk-17.0.20.101-hotspot
ANDROID_HOME = <XR-Ordner>\_tools\android-sdk   # gemeinsam für alle XR-Projekte, NDK unter ndk\30.0.16248370
```

**Nicht** `%LOCALAPPDATA%\Android\Sdk` benutzen: Claude Desktop läuft als MSIX-Sandbox und leitet
`%LOCALAPPDATA%` nach `…\Packages\Claude_…\LocalCache\Local` um — dort installierte SDKs sind für
Programme außerhalb von Claude unsichtbar. `android/local.properties`: `sdk.dir=<XR-Ordner>/_tools/android-sdk`.
Quest-Logs/Screenshots: gemeinsame Werkzeuge unter `<XR-Ordner>/_tools/quest/` (README dort).

Im PATH liegt auf Windows zusätzlich ein Java 8 — ohne JAVA_HOME bricht Gradle ab.
Pakete kommen über die neue `android`-CLI (`sdkmanager` ist dort nur noch ein Wrapper):
`%ANDROID_HOME%\cmdline-tools\latest\bin\android.exe sdk install "ndk;30.0.16248370"`.
Shell in Claude Code: Git Bash (POSIX) und PowerShell.

- APK: `cd android && ./gradlew assembleDebug` (Windows-PowerShell: `.\gradlew.bat assembleDebug`)
  → `android/build/outputs/apk/<flavor>/debug/XRShell-<flavor>-debug.apk`; Flavors: `shell`
  (`xr.shell`), `island` (`xr.island`), `dos` (`xr.dosgames`), `adventure` (`xr.adventure`) und
  `fps` (`xr.shooter`); App-ID = Anzeigename, `namespace` bleibt `dev.xrshell` (JNI/Java)
- NDK-Auflösung in `build.gradle`: `ndk.path` in local.properties oder `ANDROID_NDK` →
  `android.ndkPath`; sonst SDK-NDK. `ndk.dir` in local.properties ist deprecated — entfernen.
- Native Lib nur: `cmake -B build-android -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release && cmake --build build-android -j8`
  (Windows: SDK-CMake unter `%ANDROID_HOME%\cmake\3.22.1\bin`, Generator Ninja liegt daneben.)
- `android/local.properties` (gitignored) enthält `sdk.dir` (und auf macOS optional `ndk.path`).
- `adb` liegt in `$ANDROID_HOME/platform-tools/` (für Sideload).
- `gh` CLI ist auf beiden Rechnern authentifiziert (Issues/Pushes vom Parent-Agent).
- Commits mit der GitHub-Noreply-Adresse (`user.email` im Repo gesetzt) — GitHub lehnt
  Pushes mit der privaten Adresse ab (E-Mail-Privacy).
- Zeilenenden regelt `.gitattributes` (LF im Repo, `gradlew` LF, `*.bat` CRLF).

## Bekannte technische Schuld (bei passender Gelegenheit mitfixen)

- (#3 Laser-Orientierung: in `d15fb15` behoben, noch ungeprüft.)
- Cursor-Hit wird pro Frame berechnet, aber noch nicht an einen Adapter weitergereicht
  (Schnittstelle `RayHit raycastToScreen()` in `quad_renderer.h` ist bereit).

## OpenXR-Idiome, die hier schon einmal gefallen sind

- Plattform-Typen aktivieren: `XR_USE_PLATFORM_ANDROID` + `XR_USE_GRAPHICS_API_OPENGL_ES`
  (NICHT `XR_PLATFORM_ANDROID`) — siehe CMakeLists.
- Enum heißt `XR_ACTION_TYPE_POSE_INPUT` (nicht POSEF).
- Bindings sind `XrPath`, immer via `xrStringToPath` auflösen (nie String zuweisen).
- `XR_NULL_PATH` für leere Pfade; `ALooper_pollOnce` statt `pollAll`.
- Fehler-Strings: `xrResultToString(XrApp::sInstanceForErrors, …)`.

## Stand und Reihenfolge (zuletzt aktualisiert: 2026-09-27)

- ✅ M0: Shell + erster Build + APK (Commits bis `060fdef`)
- ✅ M0.1 #1 (Hit-Test-API), #2 (Grip = Bildschirm verschieben, Touch-Controller-Profil)
- ✅ M1 #4 (libretro + DOSBox-Pure, Audio, Input) und #5 (Tastatur-Overlay) — Code fertig,
  **noch nie auf dem Headset gelaufen**
- ✅ M1.1-Fixes #9–#15 (Stack, Farbkanäle, Texturausschnitt, Aufrufreihenfolge,
  Spielsuche/Core-Update, Aim-Strahl, View-Matrix) + OpenXR-Start auf der Quest
- ✅ Erster Headset-Test (2026-09-27): Bild (FBO-Fix), Ton, Gamepad, VR-Menü v1,
  Passthrough — Details und Rückroll-Tags in **STATUS.md**
- 🟨 **M1.2 #16** (libretro-Adapter für mehrere Cores) — läuft im Headset; Menü v2 (`d15fb15`)
  noch ungeprüft; offen: Frame-Pacing 50 Hz
- ⬜ Danach: DOSBox-Pfad erneut im Headset prüfen → PoC abgeschlossen → SideQuest-Release
- ⬜ Danach: M2 (#6, SDL2-Shim)
  → M3 (#7, braucht die im Konzept für M0 vorgesehene ImGui-UI)
- 🟨 #3 (Laser-Look-at) in `d15fb15` umgesetzt, Headset-Prüfung offen.
- Danach erst Tier-2/Backlog (#8, Konzept Abschnitt 8); nie zwei Tiers parallel.

## Workflow für ausgelagerte Aufgaben

1. Diese Datei + betroffene Issue-Beschreibung lesen; bei Architekturfragen das Konzept.
2. Auf eigenem Stand committen, Message mit `Closes #N` wenn Issue fertig.
3. Nicht pushen, nicht auf andere Issues/Branches ausweiten.
4. Abschlussbericht: was geändert, wo Artefakte liegen, was überraschend war.
