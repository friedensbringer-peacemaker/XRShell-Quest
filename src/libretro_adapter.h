// XRShell — M1: libretro-Core-Loader (DOSBox-Pure als erster Core).
//
// Lädt einen libretro-Core (.so) prozessintern aus dem App-Dateisystem,
// füttert ihn mit einem Spiel aus dem externen App-Verzeichnis und rendert
// dessen Frames in die QuadRenderer-Textur.
//
// Thread-Modell:
//   · Init (dlopen, Symbole, retro_init, retro_load_game) läuft im init()/start()-Aufrufer —
//     in main.cpp auf einem eigenen Lade-Thread, damit die Ladeanzeige weiterläuft, auch wenn
//     retro_load_game blockiert (Regeln für den Main-Thread währenddessen: siehe „Ladeanzeige“).
//   · Core-Thread (std::thread): retro_run-Loop mit Fixpoint-Timing auf dem
//     vom Core gemeldeten fps-Wert (Condition-Variable — kein Busy-Waiting).
//   · Video-Callback (Core-Thread) kopiert den Frame unter Mutex in einen
//     Double-Buffer (2 × 1024×768×RGBA).
//   · Audio-Callbacks (Core-Thread) schreiben in den SPSC-Ring von CoreAudio
//     (AAudio-Consumer auf eigenem Thread).
//   · updateTexture() (Render-Thread): überträgt den neuesten Frame per
//     glTexSubImage2D in QuadRenderer::texture(). GLES-Aufrufe ausschließlich
//     hier — nie aus dem Core-Thread.
//
// Audio (M1b): Core-Audio-Callbacks schreiben in den SPSC-Ring von CoreAudio,
// Ausgabe via AAudio. Input ist bewusst noch statisch (M1c folgt).
// Ohne Core/Spiel bleibt die alte Testtextur des Renderers sichtbar (Fallback).
//
// xr.shooter: Spielordner unter games/doom|wolf3d|quake/ laden PrBoom, ECWolf bzw.
// TyrQuake (Startdatei per findStartFile), Pad-Modus mit Analogsticks.
//
// #16: Der Core richtet sich nach der Spieldatei in game/ — ein Ordner mit
// *.scummvm lädt den ScummVM-Core (#28), alles andere DOSBox-Pure. Frames in RGB565/0RGB1555 werden beim Kopieren nach XRGB8888
// gewandelt, damit Upload und Shader für alle Cores gleich bleiben.

#pragma once

#include "core_audio.h"
#include "cursor_lock.h"
#include "game_folders.h"
#include "libretro.h"
#include "load_status.h"
#include "mouse_relative.h"
#include "option_store.h"
#include "pad_analog.h"
#include "quad_renderer.h"
#include "save_ram.h"

#include <android_native_app_glue.h>

#include <GLES3/gl3.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class LibretroAdapter {
public:
    // Entpackt den Core beim ersten Start aus den Assets nach filesDir/cores/,
    // öffnet ihn (dlopen) und startet den Core-Thread.
    // Liefert false, wenn kein Core verfügbar ist (App läuft dann im Fallback).
    bool init(android_app* app);
    // Stoppt den Core-Thread (Condition-Variable weckt ihn), entlädt den Core.
    void shutdown();

    // xr.adventure: Gerätesprache (ISO 639/3166) für die erste scummvm.ini (scumm_setup.h).
    void setLanguage(const std::string& lang, const std::string& country) {
        mLanguage = lang;
        mCountry = country;
    }

    // --- Bibliothek (xr.dosgames, xr.shooter, xr.adventure) ---
    // Nur Adapter vorbereiten, ohne automatisch ein Spiel zu laden.
    void attach(android_app* app) { mApp = app; }
    // Spiel starten (Core nach Endung); ein laufendes Spiel wird vorher beendet.
    bool start(const std::string& gamePath);
    // Laufendes Spiel beenden und Core entladen (Adapter bleibt nutzbar).
    void stopGame();

    // --- Ladeanzeige (load_status.h, main.cpp) ---
    // start() darf auf einem eigenen Lade-Thread laufen (retro_load_game blockiert sonst die
    // XR-Schleife). Solange er läuft, ruft der Main-Thread NUR die folgenden Lesefunktionen auf
    // (Atomics) — alles andere am Adapter erst nach dem join() des Lade-Threads.
    // Etappen in der Reihenfolge des Starts; Zeitpunkt = steady_clock in Sekunden.
    enum LoadStage { kStageIdle, kStageStop, kStageData, kStageCore, kStageGame, kStageReady, kStageCount };
    int loadStage() const { return mLoadStage.load(); }
    // Main-Thread, vor dem Start des Lade-Threads: Etappen des vorigen Starts vergessen.
    void resetLoadStage() {
        for (auto& t : mStageTime) t.store(0.0);
        mCoreExtracted.store(false);
        mLoadStage.store(kStageIdle);
    }
    double loadStageTime(int stage) const { return stage >= 0 && stage < kStageCount ? mStageTime[stage].load() : 0.0; }
    // Dieser Start hat den Core aus dem APK entpackt (erster Start nach Installation/Update).
    bool coreExtracted() const { return mCoreExtracted.load(); }
    // Nach start() == false (erst nach dem join lesen): Grund und Klartext für die Fehlerseite.
    loadst::Failure startFailure() const { return mStartFailure; }
    const std::string& startFailureDetail() const { return mStartDetail; }
    // Letzte Warn-/Fehlerzeile des Cores (libretro-Log), threadsicher — „letzte Engine-Zeile“.
    static std::string lastCoreMessage();
    // Core-.so, den start() für diesen Pfad wählt (ohne xrshell_input, gleiche .so). Vor dem Laden.
    static std::string coreSoFor(const std::string& gamePath);
    // Liegt der Core im APK (oder schon entpackt)? Braucht attach().
    bool coreAvailable(const std::string& soName) const;
    // Einzelspiel-Apps: Spiel in files/game/ suchen (wie init(), ohne zu starten); leer = keins.
    std::string findDefaultGame() { return mApp ? findGameFile(mApp) : std::string(); }
    // Render-Thread, nach updateTexture(): hatte der zuletzt hochgeladene Frame Inhalt
    // (nicht nur Schwarz)? Startsignal der Ladeanzeige.
    bool lastFrameHasContent() const { return mLastContent; }
    struct GameEntry { std::string path, title, system; };
    // Spielordner unter <externalDataPath>/games/dos|doom|wolf3d|quake|adventure/<Ordner>/,
    // sortiert; nur Systeme, deren Core im APK liegt.
    std::vector<GameEntry> scanLibrary() const;

    // Render-Thread: lädt einen neuen Core-Frame in die Renderer-Textur.
    // Kein GLES-Treffer → kein Upload, Fallback-Testmuster bleibt stehen.
    // Liefert true, wenn ein neuer Frame hochgeladen wurde.
    bool updateTexture(GLuint texture);

    // --- Audio ---
    // Lautstärke 0.0..1.0 (Standard 1.0 — fest verdrahtet bis es eine UI gibt).
    void setVolume(float v) { mAudio.setVolume(v); }
    // Videoaufnahme: Abgriff des fertigen Ton-Mixes (siehe CoreAudio::setTap).
    void setAudioTap(CoreAudio::Tap tap, void* user) { mAudio.setTap(tap, user); }

    // --- Input-Schnittstelle (M1c) ---
    // Alles vom Main-Thread aufrufen; Übergabe an den Core-Thread via Atomics.
    void setPointerUV(float u, float v, bool valid);  // Cursor in Screen-UV [0..1]
    void setMouseButtons(bool left, bool right);      // Trigger = Links, B = Rechts
    void setStick(float x, float y);                  // rechter Thumbstick -1..1
    void setKey(enum retro_key key, bool pressed);       // Tastatur (Overlay/Shortcuts)
    // #16: RetroPad-Bitmaske (1 << RETRO_DEVICE_ID_JOYPAD_*) für Port 0/1.
    // Nur im Pad-Modus wirksam (Pad-Cores, z. B. PrBoom).
    void setPadButtons(unsigned port, uint16_t mask);
    // Analogsticks (fertig umgerechnet, padmap::sticksToAnalog) für Port 0/1 — Pad-Modus.
    void setPadAnalog(unsigned port, const padmap::Analog& analog);
    // libretro-Rumble (Port 0): Stärke 0..1 aus „stark“ und „schwach“ (PrBoom: Schuss bzw.
    // Aufsammeln) — main.cpp gibt sie als Vibration an den rechten Controller.
    float rumbleAmplitude() const;

    // Spiel anhalten: Core-Thread ruft kein retro_run mehr, solange mindestens ein Grund gilt.
    // Gründe unabhängig voneinander (VR-Menü offen; Fokus weg/Brille ab/keine Session). Beim
    // Anhalten: Ton stumm, Rumble 0, SRAM sichern; beim Fortsetzen setzt der Takt neu auf.
    enum PauseReason : unsigned { kPauseMenu = 1u, kPauseFocus = 2u };
    void setPauseReason(PauseReason reason, bool on);
    void setPaused(bool paused) { setPauseReason(kPauseMenu, paused); }   // VR-Menü

    // Spielstand (libretro-Savestate) in Platz 1..n; Dateien unter
    // <externalDataPath>/saves/<Spielname>.state<N>. true = erfolgreich.
    bool saveState(int slot);
    bool loadState(int slot);
    // Kann der laufende Core Spielstände (retro_serialize_size > 0)? ScummVM nicht — dort
    // speichert man im Spiel. Einmal je Spiel nach dem ersten Frame geprüft (try_lock, blockiert
    // den Render-Thread nie); bis dahin true, damit das Menü nichts voreilig ausblendet.
    bool statesSupported();
    bool hasState(int slot) const;

    // Batteriespeicher (SRAM) des Moduls nach <externalDataPath>/saves/<Spielname>.srm, nur bei
    // Änderung. Läuft von selbst alle ~2 s, beim Pausieren und vor dem Entladen; Aufruf von außen
    // z. B. bei APP_CMD_PAUSE. Blockiert höchstens bis zum Ende des laufenden retro_run.
    void flushSaveRam();

    // Experimentell: Frame-Analyse für „Umgebung ausblenden“ (Video-Callback).
    // Liefert die häufigste Farbe im Himmelsband, das Ende des Himmelsbands und
    // die Horizontzeile (Framezeilen). keyValid false = nichts Sicheres gefunden.
    void setEnvironmentAnalysis(bool enabled) { mAnalyze.store(enabled); }
    // Typ EnvironmentKey aus quad_renderer.h (Renderer nutzt ihn direkt).
    EnvironmentKey environmentKey() const;

    bool coreLoaded() const { return mCoreLoaded; }
    bool scummCore() const { return mScummCore; }   // xr.adventure: ScummVM (Spielmenü = Strg+F5)
    // #16: Pad-Core — Controller als Gamepad statt Maus/Laser.
    bool padMode() const { return mPadMode; }
    // Bibliotheksordner des laufenden Cores („doom“, „quake“ …; „dospad“ für
    // DOSBox-Pure im Pad-Modus, leer bei Maus-Cores) — wählt die Tastenbelegung (pad_mapping.h).
    const std::string& systemFolder() const { return mCoreFolder; }
    // Pad-Typ an Port 0 (z. B. RETRO_DEVICE_ANALOG für Analogspiele); nur zwischen zwei Frames.
    void setPadDevice(unsigned device);
    // Spiel-Mauszeiger (VR-Menü, Pflicht P3): kann der laufende Core den Zeiger des Spiels
    // ausblenden? DOSBox-Pure nicht — den Zeiger zeichnet das DOS-/Windows-Programm selbst ins Bild.
    // TODO: Core-Option bzw. eigener Maustreiber-Abgriff, sobald ein Core das anbietet.
    bool canHideGameCursor() const { return false; }
    void setGameCursorVisible(bool visible) { mGameCursorVisible = visible; }
    bool gameCursorVisible() const { return mGameCursorVisible; }
    // Leistungszeile: fertige Core-Frames seit Start und Soll-Takt des Cores.
    uint64_t frameCount() const { return mFrameCounter.load(); }
    double timingFps() const { return mTimingFps.load(); }
    // Aktuelle Bildwiederholrate des Displays (Main-Thread, pro Frame); ≤ 0 = unbekannt → 72.
    void setDisplayRefreshRate(float hz) { mDisplayRefresh.store(hz > 0.0f ? hz : 72.0f); }
    bool gameRunning() const { return mGameRunning.load(); }
    // Core hat sich selbst beendet (RETRO_ENVIRONMENT_SHUTDOWN) — einmal abholen (Main-Thread).
    bool takeShutdownRequest() { return mShutdownRequested.exchange(false); }
    // Geladene Spieldatei bzw. Spielordner (leer ohne Spiel) — Tastenprofil-Suche.
    const std::string& gamePath() const { return mGamePath; }
    // Render-Thread: Größe des zuletzt hochgeladenen Frames (#11).
    int  frameWidth() const { return mUploadedW; }
    int  frameHeight() const { return mUploadedH; }
    // Anzeige-Seitenverhältnis laut Core (DOS meist 4:3, auch bei 320×200).
    float aspectRatio() const { return mAspect.load(); }
    // Aktiver Eintrag aus xrshell.opt (bzw. der Nutzerdatei) des laufenden Spiels (nullptr =
    // nicht gesetzt). Neben den Kern-Optionen liest XRShell dort eigene Schlüssel, z. B.
    // `xrshell_filter = hd`. Threadsicher (OptionStore), Zeiger bleibt gültig.
    const char* profileOption(const char* key) const { return coreOption(key); }
    // Geometrie laut Core (base_width/height, auch vor dem ersten Frame).
    int gameWidth() const { return mGameW.load(); }
    int gameHeight() const { return mGameH.load(); }

    // --- Kern-Optionen aus dem VR-Menü (xr.dosgames), siehe option_store.h ---
    // Nutzerwerte pro Spielordner: <externalDataPath>/settings/<Ordner>.user.opt, nach xrshell.opt
    // geladen. Nur in Apps, die das einschalten (vor start()); sonst bleibt alles wie bisher.
    void enableUserOptions(bool enabled) { mUserOptionsEnabled = enabled; }
    bool userOptionsEnabled() const { return mUserOptionsEnabled; }
    // Wert, den der Core gerade sieht / der nach einem Neustart gilt (Nutzer vor Profil).
    bool activeOption(const std::string& key, std::string& out) const { return mOptions.active(key, out); }
    bool effectiveOption(const std::string& key, std::string& out) const { return mOptions.effective(key, out); }
    bool optionPending(const std::string& key) const { return mOptions.pending(key); }
    bool userOption(const std::string& key, std::string& out) const { return mOptions.user(key, out); }
    // Optionsliste des Cores (SET_VARIABLES / SET_CORE_OPTIONS*): erlaubte Werte, Standard.
    bool coreOptionDefinition(const std::string& key, optstore::Definition& out) const {
        return mOptions.definition(key, out);
    }
    bool coreOptionsAnnounced() const { return mOptions.definitionCount() > 0; }
    // live = sofort an den Core (GET_VARIABLE_UPDATE), sonst ab dem nächsten Start.
    void setUserOption(const std::string& key, const std::string& value, bool live) {
        mOptions.setUser(key, value, live);
    }
    bool hasUserOptions() const { return mOptions.hasUserValues(); }
    void resetUserOptions(OptionStore::LiveFn isLive) { mOptions.resetUser(isLive); }
    // Geänderte Nutzerwerte schreiben (leer → Datei löschen). true = nichts zu tun oder gespeichert.
    bool saveUserOptions();

    // --- Relative Maus (xr.dosgames, DOSBox-Pure „virtual“), siehe mouse_relative.h ---
    // Faktor pro Achse auf die Laser-Deltas; wirkt nur mit Nutzeroptionen + relativem Modus.
    void setMouseGain(float x, float y) { mGainX.store(x); mGainY.store(y); }
    // Zeigerverschiebung pro Spiel in Prozent der Bildbreite/-höhe (ptroff, pointer_map.h).
    void setPointerOffset(float percentX, float percentY) { mOffsetX.store(percentX); mOffsetY.store(percentY); }
    // Spielzeiger unter den Laser holen (links oben anschlagen, dann Laserposition) bzw. auf
    // einen festen Punkt (Frame-UV, Abgleich).
    void catchPointer(float u = -1.0f, float v = -1.0f) {
        mCatchTargetU.store(u);
        mCatchTargetV.store(v);
        mCatchRequest.store(true);
    }
    // Abgleich: Laserbewegung geht nicht ans Spiel, der Spielzeiger bleibt stehen.
    void setPointerFrozen(bool frozen) { mPointerFrozen.store(frozen); }
    // Zählt jedes abgeschlossene Fangen; Laserposition dabei in Frame-UV (Abgleich).
    unsigned catchCount() const { return mCatchCount.load(); }
    void lastCatchUv(float& u, float& v) const { u = mCaughtU.load(); v = mCaughtV.load(); }

    // --- Spielzeiger koppeln („Zeiger lernen“, cursor_lock.h; DOSBox-Pure mit Nutzeroptionen) ---
    // Main-Thread: Lernen anstoßen (≈ 1,2 s, Laser/Tasten gehen solange nicht ans Spiel) bzw. eine
    // gespeicherte Kopplung setzen (Lock::encode, leer = aus).
    void requestCursorLearn() { mLearnRequest.store(true); }
    // Lernen angefordert oder läuft — dann nicht „Zeiger fangen“ (beides bewegt den Spielzeiger).
    bool cursorLearnPending() const { return mLearnRequest.load() || mCursorState.load() == kCursorLearning; }
    void setCursorLock(const std::string& encoded) {
        std::lock_guard<std::mutex> lock(mLockMutex);
        mLockIn = encoded;
        mLockPending.store(true);
    }
    // Ergebnis des Lernens einmal abholen: true, wenn eines vorliegt.
    bool takeCursorLearnResult(bool& ok, std::string& encoded, std::string& message) {
        if (!mLearnDone.exchange(false)) return false;
        std::lock_guard<std::mutex> lock(mLockMutex);
        ok = mLearnOk;
        encoded = mLearnEncoded;
        message = mLearnMessage;
        return true;
    }
    // kCursorLost = pausiert: Spielwert reagiert gerade nicht (Überblendung, Film, anderer
    // Bildschirm) oder gespeicherte Kopplung wartet auf die erste Reaktion — sucht selbst weiter.
    enum CursorState { kCursorOff = 0, kCursorLocked = 1, kCursorLost = 2, kCursorLearning = 3 };
    int cursorLockState() const { return mCursorState.load(); }

private:
    // assets/cores/<soName> → filesDir/cores/<soName>
    bool extractCoreFromAssets(android_app* app, const char* soName);
    std::string findGameFile(android_app* app);   // <externalDataPath>/game/
    void extractScummSystem(android_app* app);    // #28: Systemdateien → mSystemDir/scummvm/
    void writeScummIni();                         // xr.adventure: scummvm.ini anlegen, falls sie fehlt
    bool readGameData(const std::string& path);   // need_fullpath=false → mGameData
    // Kern-Optionen aus xrshell.opt (Spielordner bzw. neben der Spieldatei, sonst
    // game/) und ggf. der Nutzerdatei für RETRO_ENVIRONMENT_GET_VARIABLE — z. B.
    // dosbox_pure_mouse_input = direct.
    void loadCoreOptions(const std::string& gamePath);
    const char* coreOption(const char* key) const { return mOptions.get(key); }   // nullptr = Core-Standard
    std::string userOptionsPath(const std::string& gamePath) const;
    void addCoreOptionDefinitions(unsigned cmd, const void* data);   // SET_VARIABLES / SET_CORE_OPTIONS*
    void pollSnapshot();          // input_poll (Core-Thread): Absolut → Delta
    int16_t queryInputState(unsigned port, unsigned device, unsigned index,
                            unsigned id) const;

    // libretro-Callbacks (C-Statische → Userdaten-Pointer)
    static bool environmentCallback(unsigned cmd, void* data);
    static void videoRefreshCallback(const void* data, unsigned width,
                                     unsigned height, size_t pitch);
    static void audioSampleCallback(int16_t left, int16_t right);
    static size_t audioSampleBatchCallback(const int16_t* data, size_t frames);
    static void inputPollCallback();
    static int16_t inputStateCallback(unsigned port, unsigned device,
                                      unsigned index, unsigned id);

    static constexpr int kTextureW = QuadRenderer::kTextureW;   // Renderer-Textur
    static constexpr int kTextureH = QuadRenderer::kTextureH;

    android_app* mApp = nullptr;

    // --- Ladeanzeige: Etappen (Lade-Thread schreibt, Main-Thread liest) ---
    std::atomic<int> mLoadStage{kStageIdle};
    std::atomic<double> mStageTime[kStageCount] = {};
    std::atomic<bool> mCoreExtracted{false};
    loadst::Failure mStartFailure = loadst::Failure::None;   // gültig nach start() == false
    std::string mStartDetail;
    void setStage(LoadStage stage);
    bool fail(loadst::Failure failure, const std::string& detail);   // setzt Grund, liefert false
    bool mLastContent = false;               // nur Render-Thread (updateTexture)

    // --- Core-Handle + API-Funktionszeiger (nur Core-Thread nutzt sie) ---
    void* mCoreHandle = nullptr;
    bool mCoreLoaded = false;
    std::string mGamePath;
    std::vector<uint8_t> mGameData;          // ROM-Inhalt, gültig bis retro_unload_game
    bool mGameCursorVisible = true;     // Wunsch aus dem VR-Menü (wirkt erst mit Core-Unterstützung)
    bool mPadMode = false;                   // #16: Pad-Core (RetroPad)
    std::string mCoreFolder;                 // Bibliotheksordner des Cores (Tastenbelegung)
    void (*mSetPortDevice)(unsigned, unsigned) = nullptr;
    unsigned mPadDevice = 1;                 // aktuelles Gerät an Port 0 (RETRO_DEVICE_JOYPAD)
    std::string mSystemDir, mSaveDir;        // leer = Core bekommt keine Verzeichnisse
    // xrshell.opt + Nutzerwerte; Main-Thread schreibt (Menü), Core-Thread liest (GET_VARIABLE).
    mutable OptionStore mOptions;
    bool mUserOptionsEnabled = false;
    std::string mUserOptionsPath;            // leer = keine Nutzerdatei
    // Vom Core per SET_PIXEL_FORMAT gewählt; libretro-Standard ohne Aufruf: 0RGB1555.
    std::atomic<int> mPixelFormat{RETRO_PIXEL_FORMAT_0RGB1555};
    std::atomic<float> mDisplayRefresh{72.0f};   // GET_TARGET_REFRESH_RATE

    std::thread mCoreThread;
    std::mutex  mMutex;
    std::condition_variable mCv;
    bool mStopRequested = false;             // unter mMutex
    bool mPaused = false;                    // unter mMutex: mPauseReasons != 0
    unsigned mPauseReasons = 0;              // unter mMutex, PauseReason-Bits (bleiben über Spielwechsel)
    // Hält der Core-Thread während retro_run — Savestates nur dazwischen.
    std::mutex mRunMutex;
    size_t (*mSerializeSize)(void) = nullptr;
    bool (*mSerialize)(void*, size_t) = nullptr;
    bool (*mUnserialize)(const void*, size_t) = nullptr;
    bool mStatesChecked = false;             // nur Render-Thread (statesSupported)
    bool mStatesSupported = true;
    uint64_t mStatesFrameBase = 0;           // Frame-Zähler beim Laden des Spiels
    std::string statePath(int slot) const;
    std::string saveBasePath() const;        // <externalDataPath>/saves/<Spielname>
    // SRAM: Zeiger/Größe vom Core (optional), Tracker nur unter mRunMutex anfassen.
    void* (*mGetMemoryData)(unsigned) = nullptr;
    size_t (*mGetMemorySize)(unsigned) = nullptr;
    saveram::Tracker mSaveRam;
    bool mSaveRamFailing = false;            // letzte SRAM-Sicherung fehlgeschlagen (Log nur beim Wechsel)
    void prepareSaves();                     // Ordner + Übernahme vom alten Ablageort
    void beginSaveRam();                     // nach retro_load_game, vor dem Core-Thread
    void flushSaveRamLocked();               // mRunMutex muss gehalten werden
    void analyzeFrame(const uint32_t* pixels, int width, int height);   // Core-Thread

    std::atomic<bool> mAnalyze{false};
    mutable std::mutex mKeyMutex;            // schützt mKey (Core-Thread schreibt, Main liest)
    EnvironmentKey mKey;

    // --- Core-Timing/Audio (gesetzt aus retro_system_av_info) ---
    std::atomic<double> mTimingFps{60.0};    // Fixpoint-Takt des Run-Loops (Core-Thread folgt Änderungen)
    CoreAudio mAudio;                        // AAudio + SPSC-Ring

    // --- Double-Buffer für Core-Frames ---
    std::atomic<uint64_t> mFrameCounter{0};  // hochgezählt vom Video-Callback
    uint64_t mLastUploaded = 0;              // nur Render-Thread
    int mUploadedW = 0, mUploadedH = 0;      // nur Render-Thread
    uint32_t mFront = 0;                     // unter mMutex: Index des fertigen Frames
    int mFrameW = 0, mFrameH = 0;            // unter mMutex
    // Heap statt Inline-Array (#9): 2 × 3 MB dürfen nie auf einem Stack landen.
    std::vector<uint8_t> mPixels[2] = {
        std::vector<uint8_t>(kTextureW * kTextureH * 4),
        std::vector<uint8_t>(kTextureW * kTextureH * 4)};

    std::atomic<bool> mGameRunning{false};
    std::atomic<int> mGameW{0}, mGameH{0};   // Geometrie aus retro_system_av_info (UV→Pixel)
    std::atomic<float> mAspect{4.0f / 3.0f}; // Anzeige-Seitenverhältnis (#11)
    void setGeometry(const retro_game_geometry& geo);   // Core-Thread/Init

    // --- Input-Zustand: Main-Thread schreibt, Core-Thread liest (atomar) ---
    std::atomic<float> mPointerU{0.5f}, mPointerV{0.5f};
    std::atomic<bool>  mPointerValid{false};
    std::atomic<bool>  mMouseLeft{false}, mMouseRight{false};
    std::atomic<float> mStickX{0.0f}, mStickY{0.0f};
    std::atomic<uint16_t> mPad[2] = {};      // #16: RetroPad-Bitmasken Port 0/1
    std::atomic<int16_t> mPadAnalog[2][4] = {};   // Port 0/1: links X/Y, rechts X/Y
    std::atomic<bool> mShutdownRequested{false};   // RETRO_ENVIRONMENT_SHUTDOWN (Core-Thread → Main)
    std::atomic<uint16_t> mRumble[2] = {};        // Port 0: RETRO_RUMBLE_STRONG / _WEAK (Core-Thread)

    // --- Input-Snapshot: ausschließlich Core-Thread (poll → state) ---
    int mMouseDeltaX = 0, mMouseDeltaY = 0;
    void logPointer(bool valid, float u, float v, int w, int h, bool relative);
    unsigned mPointerLogTick = 0;            // Diagnose xrshell_log_pointer (nur Core-Thread)
    int mPointerLogX = -1, mPointerLogY = -1;
    mouserel::RelativeMouse mRelMouse;       // Deltas, Faktoren, Fangen (nur Core-Thread)
    bool mDosboxCore = false;                // laufender Core ist DOSBox-Pure
    bool mScummCore = false;                 // laufender Core ist ScummVM (Fangen bei Größenwechsel)
    int mPollW = 0, mPollH = 0;              // Bildgröße beim letzten Poll (Größenwechsel erkennen)
    std::string mLanguage, mCountry;         // Gerätesprache (scummvm.ini)
    std::atomic<float> mGainX{1.0f}, mGainY{1.0f};
    std::atomic<float> mOffsetX{0.0f}, mOffsetY{0.0f};   // Prozent, ptroff
    std::atomic<bool> mCatchRequest{false}, mPointerFrozen{false};
    std::atomic<float> mCatchTargetU{-1.0f}, mCatchTargetV{-1.0f};
    std::atomic<unsigned> mCatchCount{0};
    std::atomic<float> mCaughtU{0.0f}, mCaughtV{0.0f};
    // Zeiger koppeln: Speicherbereiche des Cores (SET_MEMORY_MAPS), Lernen/Folgen — Core-Thread.
    std::vector<curlock::Region> mMemRegions;
    // Core-Thread: zwischengespeicherte Optionen für pollSnapshot (OptionStore::revision).
    unsigned mPollOptRevision = ~0u;
    bool mPollRelative = false, mPollLogPointer = false;
    // Generation der Speicherkarte: wechselt bei SET_MEMORY_MAPS, Absturz/Beenden des Programms
    // (SET_MESSAGE „crash“, SHUTDOWN) und Spielstart/-stopp. Lernen liest nur in der Generation,
    // in der es begonnen hat (sonst Abbruch); Folgen liest per Adresse über die jeweils aktuelle
    // Karte (leer nach Absturz → pausiert), nie über Zeiger einer alten.
    uint32_t mMemGeneration = 0, mLearnGeneration = 0;
    void dropMemoryMap(const char* why);   // Core-Thread: Bereiche ungültig (Absturz, Beenden)
    void finishLearn(bool ok, const std::string& message);
    curlock::Learner mLearner;
    curlock::Follower mFollower;
    bool cursorPoll(bool valid, float u, float v, int w, int h);   // true = Deltas gesetzt
    std::atomic<bool> mLearnRequest{false}, mLockPending{false}, mLearnDone{false};
    std::atomic<int> mCursorState{kCursorOff};
    std::mutex mLockMutex;   // mLockIn, mLearn*
    std::string mLockIn, mLearnEncoded, mLearnMessage;
    bool mLearnOk = false;
    // RETRO_DEVICE_POINTER: Laserposition im Frame, -0x7FFF (links/oben) … 0x7FFF.
    // DOSBox-Pure nutzt sie im Modus „direct“ (u. a. VMware-Maus für Windows 3.x).
    int16_t mPointerAbsX = 0, mPointerAbsY = 0;

    // Tastatur: Main-Thread (Overlay) setzt, Core-Thread pollt.
    // RETROK_LAST ≈ 320 Keycodes — ein Atomic pro Taste, kein Mutex nötig.
    std::array<std::atomic_bool, RETROK_LAST> mKeys{};
    // #28: Cores mit SET_KEYBOARD_CALLBACK (ScummVM) bekommen Ereignisse statt
    // Polling. Main-Thread sammelt, Core-Thread stellt im input_poll zu.
    struct KeyEvent { enum retro_key key; bool pressed; };
    std::mutex mKeyEventMutex;
    std::vector<KeyEvent> mKeyEvents;                     // unter mKeyEventMutex
    retro_keyboard_event_t mKeyboardCallback = nullptr;   // nur Core-Thread/Init
    void flushKeyEvents();                                // Core-Thread (input_poll)
};
