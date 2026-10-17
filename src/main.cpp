// XRShell — Einstieg: android_main, Event-Loop, Frame-Orchestrierung.
//
// M0-Verhalten:
//   · virtueller Bildschirm mit Testmuster, 2 m vor dem Nutzer (STAGE)
//   · rechter Controller = Laser + Cursor
//   · Trigger-Klick auf dem Screen unten-links: Bildschirm kurz blinken lassen (Hit-Test)
//   · Volume-Up = Exit (Quest-Konvention für Sideload-Apps ohne Quit-Button)
//
// M1a: LibretroAdapter (DOSBox-Pure-Core auf eigenem Thread) rendert echte
// Spiele-Frames in die Bildschirm-Textur; ohne Core/Spiel bleibt das Testmuster.
//
// Bedienung (Tabelle in TEST.md): Leinwand startet zentriert vor dem Blick;
// Trigger = Linksklick, Griff rechts/B = Rechtsklick, rechter Stick = Pfeiltasten
// + Joypad, Griff links + linker Stick = Breite/Abstand, dazu Stick drücken =
// zurücksetzen. Zeiger geglättet, Klick kurz stabilisiert, Haptik bei Klick.
//
// Fenster greifen: linker Griff halten = Fenster folgt der linken Hand; kommt
// der rechte Griff dazu, skaliert Auseinanderziehen/Zusammenschieben der Hände.
// xr.dosgames: rechter Griff = Karte ziehen (Handweg → Scroll-Tasten, map_drag.h),
// pro Spiel umstellbar auf Rechtsklick/aus (VR-Menü → Steuerung); B bleibt Rechtsklick.
// Linke Menütaste = VR-Menü (Passthrough, Wölbung, Filter, Sättigung, Kontrast,
// Fenster zentrieren, bei DOS die Tastatur); das Spiel pausiert, solange es offen ist.

#include "android_glue.h"
#include "app_info.h"
#include "dos_options.h"
#include "key_profile.h"
#include "keyboard_layout.h"
#include "keyboard_overlay.h"
#include "pad_mapping.h"
#include "libretro_adapter.h"
#include "load_screen.h"
#include "load_status.h"
#include "map_drag.h"
#include "pad_analog.h"
#include "pointer_map.h"
#include "quad_renderer.h"
#include "session_log.h"
#include "video_recorder.h"
#include "vr_menu.h"
#include "xr_session.h"

#include <android/log.h>
#include <android/keycodes.h>
#include <android/input.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <thread>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// --- Bedienung (Lernstand XR-Settlers2.5, Skill xr-quest-port) ---
constexpr float kDefaultDistance = 2.0f;    // Startabstand Kopf → Screen (m)
constexpr float kDefaultWidth    = 2.4f;    // Startbreite (m), 4:3 → 1,8 m hoch
constexpr float kMinDistance = 0.7f, kMaxDistance = 3.5f;
// Gespeicherte Abstände darunter gelten als Messfehler (Kopf beim Speichern nah am Bild,
// z. B. vorgebeugt oder herangelaufen) — beim Start dann Standardabstand statt „vor der Nase“.
constexpr float kMinSavedDistance = 1.0f;
// Das VR-Menü steht nie näher als das (vor dem Kopf liegt es ohnehin vor dem Bild).
constexpr float kMinMenuDistance = 0.9f;
constexpr float kMinWidth = 1.0f, kMaxWidth = 6.0f;
constexpr float kScreenBelowEyes = 0.1f;    // Screen-Mitte leicht unter Augenhöhe
constexpr float kPointerTau      = 0.025f;  // Zeigerglättung, Zeitkonstante (s)
constexpr float kClickFreeze     = 0.085f;  // Zielpunkt nach Klick einfrieren (s)
constexpr float kStickDeadzone   = 0.25f;
constexpr float kArrowPress = 0.5f, kArrowRelease = 0.35f;   // Hysterese Pfeiltasten

// Breite hängt vom Abstand ab (0,4–2,5 × Abstand, max. 6 m), damit der Screen weder
// winzig noch unüberschaubar wird.
float clampWidth(float width, float distance) {
    const float lo = std::fmax(kMinWidth, 0.4f * distance);
    const float hi = std::fmin(kMaxWidth, 2.5f * distance);
    return std::fmin(std::fmax(width, lo), hi);
}

// Deadzone mit Neuskalierung: außerhalb der Zone beginnt der Wert bei 0.
float applyDeadzone(float v) {
    const float a = std::fabs(v);
    if (a < kStickDeadzone) return 0.0f;
    return std::copysign((a - kStickDeadzone) / (1.0f - kStickDeadzone), v);
}

// Taste mit „Re-arm“: Ein Druck, der nicht auf einer gültigen Fläche begann
// (oder durch Fokus-/Tracking-Verlust abgebrochen wurde), wirkt erst wieder
// nach dem Loslassen. So wird ein gehaltener Trigger nie zum Klick woanders.
struct ButtonGate {
    bool armed = true;
    bool active = false;
    bool update(bool pressed, bool canStart, bool canHold) {
        if (!pressed) { armed = true; active = false; return false; }
        if (active) {
            if (!canHold) { active = false; armed = false; }
            return active;
        }
        if (armed && canStart) { active = true; return true; }
        armed = false;
        return false;
    }
};

struct AppState {
    android_app* android = nullptr;
    EglState egl;
    XrApp xr;
    QuadRenderer renderer;
    LibretroAdapter adapter;   // M1: libretro-Core (DOSBox-Pure) → renderer-Textur
    padmap::Mapping padMap = padmap::systemFor("").defaults;   // Pad-Cores: Tastenbelegung
    KeyboardOverlay overlay;   // #5: virtuelle Tastatur unterhalb des Screens
    KeyProfile keyProfile;     // xrshell.keys: freie Controller-Tasten → Spieltasten
    bool leftStickHeld[4] = {};   // links, rechts, hoch, runter (Profil, Hysterese)
    bool xrReady = false;
    bool running = true;
    bool finishRequested = false;   // ANativeActivity_finish schon aufgerufen (kein doppelter Aufruf)

    // Bildschirm-Zustand persistiert über Frames (linke Hand verstellt ihn)
    ScreenState screen;
    float screenAspect = 16.0f / 9.0f;      // Testmuster; danach Core-Seitenverhältnis
    float screenDistance = kDefaultDistance;
    bool  centered = false;                 // Startzentrierung vor der Blickrichtung
    bool  adjusting = false;                // Griff links hält gerade die Verstellung
    int   heldOverlayKey = -1;              // per Trigger gedrückte Overlay-Taste

    // Zeiger und Klicks
    XrTime lastDisplayTime = 0;
    bool   pointerFiltered = false;         // Filter hat einen Startwert
    float  pointerU = 0.5f, pointerV = 0.5f;
    XrTime freezeUntil = 0;                 // Klickstabilisierung bis zu diesem Zeitpunkt
    float  freezeU = 0.5f, freezeV = 0.5f;
    ButtonGate leftButton, rightButton;
    bool   arrowHeld[4] = {};               // links, rechts, hoch, runter
    // Bildzoom (Taste X / Reiter „Zeiger“): Mitte des Ausschnitts in Frame-UV
    float  zoomCenterU = 0.5f, zoomCenterV = 0.5f;
    bool   prevX = false;
    bool   prevY = false;   // Y-Flanke: Bildschirmtastatur (ohne Profilbelegung)
    bool   padStickHeld[2][4] = {};         // #16: Sticks links/rechts als Steuerkreuz

    // VR-Menü + gespeicherte Ansichtseinstellungen (pro App)
    VrMenu menu;
    VideoRecorder recorder;                  // Videoaufnahme aus dem Menü (MP4)
    ViewSettings view;
    std::string settingsPath;
    int defaultFilter = 0;                   // Standardfilter von App/Spiel (ohne Wahl im Menü)
    bool menuWasOpen = false;
    bool waitButtonRelease = false;          // nach dem Schließen: Tasten erst loslassen (re-armen)
    bool menuPrevA = false, menuPrevB = false;   // Flanken für A/B im Menü

    // Bibliothek (XRSHELL_LIBRARY): Spielordner, zuletzt gespieltes Spiel
    std::vector<LibretroAdapter::GameEntry> library;
    bool openLibraryPending = false;         // ohne Spiel: Bibliothek nach dem Zentrieren zeigen
    AppInfo info;                            // App-ID, Version, Gerät, Sprache (Sitzungslog, Menü)
    // Leistungszeile (Log alle 10 s)
    double statSince = 0.0;
    uint64_t statCoreFrames = 0;
    int statFrames = 0;

    // xr.dosgames: Bildwerte pro Spiel (Stand, der zuletzt aus den Optionen kam bzw. dorthin ging)
    dosopt::GameView gameView;
    int filterMigration = -1;                // alter, global gewählter Filter → erstes Spiel
    bool applyWindowPending = false;         // Fenstermaße des Spiels übernehmen, sobald der Kopf bekannt ist
    bool hiresPending = false;               // Menü-Aktion „Hohe Auflösung“: nach dem Schließen drücken
    double hiresUntil = 0.0;                 // so lange gilt die Profil-Eingabe „hires“ als gedrückt
    bool gameMenuPending = false;            // „Spielmenü öffnen“: senden, sobald das Spiel wieder läuft
    double gameMenuUntil = 0.0;              // so lange gilt die Profil-Eingabe „game_menu“ als gedrückt
    bool prevLeftGrip = false;               // Fenster greifen: linker Griff im letzten Frame
    int grabHand = 0;                        // 0 links, 1 rechts (Griff „Fenster“)
    // Maus abgleichen (xr.dosgames, relativ): nach dem Fangen ist der nächste Trigger der Spielzeiger
    bool calibrating = false;
    unsigned calibrateCatch = 0;             // Fang-Zähler beim Start (erst danach zählt der Trigger)
    double calibrateUntil = 0.0;

    // Fenster greifen (linker Griff) und mit beiden Griffen skalieren
    bool  grabbing = false, twoHand = false, prevRightGrip = false;
    XrVector3f grabLast = {};               // letzte linke Handposition
    XrVector3f twoHandMid = {};             // letzte Mitte beider Hände
    float twoHandStartDist = 1.0f, twoHandStartWidth = 1.0f;

    // Karte ziehen (xr.dosgames): rechter Griff halten, Handweg ab dem Greifpunkt → Scroll-Tasten
    mapdrag::Settings controls;              // Griff/Stick-Belegung des Spiels (andere Apps: Rechtsklick)
    mapdrag::Driver drag;
    bool  dragging = false;
    bool  dragArmed = true;                  // nach Abbruch erst Griff loslassen
    XrVector3f dragOrigin = {};              // Greifpunkt (rechte Hand)
    float dragPxPerM[2] = {1.0f, 1.0f};      // Hand folgt: Spielpixel je Meter Handweg (mit Hebel)
    float dragU = 0.5f, dragV = 0.5f;        // Zeiger bleibt beim Ziehen hier stehen
    bool  dragKeys[mapdrag::kDirs] = {};

    // --- Start und Ladeanzeige (Skill xr-loading-screen, load_status.h / load_screen.h) ---
    // Phasen: Idle (kein Spiel, Bibliothek) → Loading (Lade-Thread: adapter.start) → FirstFrame
    // (Core läuft, warten aufs erste echte Spielbild) → Shown. Failed = Fehlerseite.
    // Solange loadThread läuft, fasst der Main-Thread den Adapter nicht an (außer den
    // Etappen-Atomics) — siehe adapterBusy().
    enum class Phase { Idle, Loading, FirstFrame, Failed, Shown };
    Phase phase = Phase::Idle;
    LoadScreen loadScreen;
    bool loadScreenReady = false;
    loadst::History loadHistory;
    std::string loadHistoryPath;             // <files>/xr-load-times.json
    loadst::Session load;
    loadst::Watchdog watchdog;
    loadst::FrameSignal frameSignal;
    std::thread loadThread;
    std::atomic<bool> loadDone{false};
    bool loadOk = false;                     // Ergebnis von adapter.start (gültig nach loadDone)
    std::string loadPath, loadTitle, loadCore;   // Spiel, Anzeigename, Core-Schlüssel („dosbox_pure“)
    int loadSeenStage = 0;                   // zuletzt übernommene Etappe des Adapters
    bool loadFirstRun = false;               // erster Start nach Installation/Update (Core entpackt)
    bool watchdogFired = false;              // Fehlerseite „dauert ungewöhnlich lange“
    bool pageHidden = false;                 // B auf der Fehlerseite: Spielbild trotzdem zeigen
    loadst::Failure failure = loadst::Failure::None;
    std::string failureDetail;
    double failedAfter = 0.0;                // Laufzeit bis zum Fehlschlag (Kopfzeile)
    double loadPausedS = 0.0;                // Menü offen/kein Fokus während „Erstes Bild“ (zählt nicht)
    double lastPoll = 0.0;                   // Zeitpunkt des letzten pollLoad
    double appStart = 0.0;                   // steady_clock bei android_main (s)
    double xrReadyAt = 0.0;                  // OpenXR bereit (Controller-Check nach 3 s)
    double nextCheck = 0.0;                  // nächste Controller-/Akku-Prüfung
    bool panelLogged = false;                // „Ladeanzeige sichtbar nach …“ geloggt
    bool previousCrash = false;              // Marker vom letzten Start lag noch
    std::string previousLog;                 // Log jener Sitzung (Marker-Inhalt)
    std::string markerPath;                  // <files>/xr-running.marker
    enum CheckIdx { kChkXr, kChkController, kChkData, kChkCore, kChkStorage, kChkBattery, kChkLog, kChkPrevious, kChkCount };
    std::vector<loadst::CheckLine> checks = std::vector<loadst::CheckLine>(kChkCount);
    std::string emptyLibraryHint;
};

double steadyNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Lade-Thread läuft (oder ist noch nicht abgeholt): Adapter nur über die Etappen-Atomics lesen.
bool adapterBusy(const AppState& state) { return state.loadThread.joinable(); }

float handDistance(const XrVector3f& a, const XrVector3f& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Waagerechter Abstand Kopf → Bildmitte.
float headDistance(const ScreenState& s, const XrVector3f& head) {
    const float dx = s.posX - head.x, dz = s.posZ - head.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Bild auf Abstand d (begrenzt) vom Kopf schieben, Richtung bleibt. Liefert d.
float setHeadDistance(ScreenState& s, const XrVector3f& head, float d) {
    d = std::fmin(std::fmax(d, kMinDistance), kMaxDistance);
    float dx = s.posX - head.x, dz = s.posZ - head.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-3f) { dx = 0.0f; dz = -1.0f; }   // direkt am Kopf: nach vorn
    else { dx /= len; dz /= len; }
    s.posX = head.x + dx * d;
    s.posZ = head.z + dz * d;
    return d;
}

// Bild zum Kopf drehen (nur um die Hochachse).
void faceHead(ScreenState& s, const XrVector3f& head) {
    s.yaw = std::atan2(head.x - s.posX, head.z - s.posZ);
}

// Quest-Tasten in der Reihenfolge von padmap::Source (Belegung und Zuweisen im Menü).
// Linker Stick-Klick nur ohne linken Griff (mit Griff: Fenster zurücksetzen), rechter
// Griff nur, wenn er nicht gerade das Fenster skaliert.
void padSources(const FrameInput& input, bool adjust, bool rightGripFree, bool out[padmap::kSourceCount]) {
    const bool f = input.focused;
    out[padmap::kA] = f && input.aPressed;
    out[padmap::kB] = f && input.bPressed;
    out[padmap::kX] = f && input.xPressed;
    out[padmap::kY] = f && input.yPressed;
    out[padmap::kRightTrigger] = f && input.triggerPressed;
    out[padmap::kRightGrip] = f && input.gripPressed && rightGripFree;
    out[padmap::kLeftTrigger] = f && input.leftTriggerPressed;
    out[padmap::kRightStickClick] = f && input.stickClickPressed;
    out[padmap::kLeftStickClick] = f && input.leftStickClickPressed && !adjust;
}

// #16: Pad-Modus — Controller als RetroPad auf Port 0. Welche Quest-Taste welche RetroPad-Taste
// drückt und was jeder Stick tut (Steuerkreuz, Analog links/rechts, beides, aus), steht in der
// Belegung (pad_mapping.h: Standard je System bzw. Core, pro Spiel im Reiter „Tasten“).
// Steuerkreuz mit Hysterese, Analog mit Totzone 0,15 und Y nach libretro invertiert
// (pad_analog.h). Der linke Stick und sein Klick gehören dem Spiel nur ohne linken Griff
// (der verstellt weiter das Fenster). Die linke Menütaste gehört dem VR-Menü. Skaliert der
// rechte Griff gerade das Fenster (rightGripFree = false), geht er nicht ans Spiel.
// Ohne Fokus ist alles losgelassen.
void updateGamepad(AppState& state, const FrameInput& input, bool adjust, bool rightGripFree) {
    uint16_t mask = 0;
    padmap::Analog analog;
    if (input.focused) {
        const float lx = adjust ? 0.0f : input.leftStickX, ly = adjust ? 0.0f : input.leftStickY;
        const auto modeL = static_cast<padmap::StickMode>(state.padMap.stick[padmap::kLeftStick]);
        const auto modeR = static_cast<padmap::StickMode>(state.padMap.stick[padmap::kRightStick]);
        // Steuerkreuz-Achsen: Bit 0/1 = links x/y, Bit 2/3 = rechts x/y (padmap::StickDpad).
        const unsigned axes = padmap::stickDpadAxes(modeL) | (padmap::stickDpadAxes(modeR) << 2);
        const unsigned highAxes = (padmap::stickDpadHigh(modeL) ? padmap::stickDpadAxes(modeL) : 0u) |
                                  ((padmap::stickDpadHigh(modeR) ? padmap::stickDpadAxes(modeR) : 0u) << 2);
        mask = padmap::stickDpadMask(state.padStickHeld, lx, ly, input.stickX, input.stickY, axes, highAxes);
        // Analog: jeder Stick an sein Ziel; zielen beide auf denselben, gewinnt der stärkere Ausschlag.
        const padmap::Analog raw = padmap::sticksToAnalog(lx, ly, input.stickX, input.stickY);
        const int16_t values[2][2] = {{raw.lx, raw.ly}, {raw.rx, raw.ry}};
        int16_t* slots[2][2] = {{&analog.lx, &analog.ly}, {&analog.rx, &analog.ry}};
        const int targets[2] = {padmap::stickAnalogTarget(modeL), padmap::stickAnalogTarget(modeR)};
        for (int s = 0; s < 2; ++s) {
            const int t = targets[s];
            if (t < 0) continue;
            if (std::abs(values[s][0]) + std::abs(values[s][1]) >= std::abs(*slots[t][0]) + std::abs(*slots[t][1])) {
                *slots[t][0] = values[s][0];
                *slots[t][1] = values[s][1];
            }
        }
        bool pressed[padmap::kSourceCount];
        padSources(input, adjust, rightGripFree, pressed);
        mask = static_cast<uint16_t>(mask | padmap::buttonMask(state.padMap, pressed));
    } else {
        for (auto& stick : state.padStickHeld)
            for (bool& held : stick) held = false;
    }
    state.adapter.setPadButtons(0, mask);
    state.adapter.setPadAnalog(0, analog);
}

// Belegung anwenden: Systeme mit analogDevice bekommen ein Analog-Pad, sobald ein Stick analog wirkt (Shooter haben
// ein festes Gerät aus der Core-Tabelle).
void applyPadDevice(AppState& state) {
    if (!padmap::systemFor(state.adapter.systemFolder()).analogDevice) return;
    bool analog = false;
    for (uint8_t mode : state.padMap.stick)
        analog |= padmap::stickAnalogTarget(static_cast<padmap::StickMode>(mode)) >= 0;
    state.adapter.setPadDevice(analog ? RETRO_DEVICE_ANALOG : RETRO_DEVICE_JOYPAD);
}

// Nach jedem Spielstart: Belegung des Spiels (sonst Systemstandard, sonst eingebaut) laden
// und dem Menü geben; Maus-Cores (DOS, ScummVM) haben keinen Reiter „Tasten“.
void loadPadMapping(AppState& state) {
    if (!state.adapter.padMode()) {
        state.menu.setPad(false, "", state.padMap);
        return;
    }
    const std::string& folder = state.adapter.systemFolder();
    state.padMap = padmap::loadFor(state.adapter.gamePath(), state.android->activity->externalDataPath, folder);
    state.menu.setPad(true, folder, state.padMap);
    applyPadDevice(state);
}

// Menü geht auf: alle ans Spiel gegebenen Eingaben lösen. Die Klick-Gates
// werden entschärft — ein beim Schließen noch gehaltener Trigger wirkt im
// Spiel erst nach dem Loslassen (kein ungewollter Klick).
void releaseGameInputs(AppState& state, XrTime now) {
    state.adapter.setMouseButtons(false, false);
    state.leftButton = ButtonGate{false, false};
    state.rightButton = ButtonGate{false, false};
    const retro_key arrowKey[4] = { RETROK_LEFT, RETROK_RIGHT, RETROK_UP, RETROK_DOWN };
    for (int i = 0; i < 4; ++i) {
        if (state.arrowHeld[i]) state.adapter.setKey(arrowKey[i], false);
        state.arrowHeld[i] = false;
        state.leftStickHeld[i] = false;
    }
    if (state.heldOverlayKey >= 0) {
        state.adapter.setKey(state.overlay.retroKey(state.heldOverlayKey), false);
        state.heldOverlayKey = -1;
    }
    state.overlay.setPressedKey(-1);
    // Karte ziehen: sofort lösen; weiter erst nach erneutem Griff.
    state.drag.reset();
    state.dragging = false;
    state.dragArmed = false;
    for (int i = 0; i < mapdrag::kDirs; ++i) {
        state.dragKeys[i] = false;
        state.keyProfile.releaseNow(static_cast<KeyProfile::Input>(KeyProfile::kScrollLeft + i), state.adapter);
    }
    if (!state.keyProfile.empty()) {
        const bool none[KeyProfile::kInputCount] = {};
        state.keyProfile.update(none, static_cast<double>(now) * 1e-9, state.adapter);
    }
    for (auto& stick : state.padStickHeld)
        for (bool& held : stick) held = false;
    state.adapter.setPadButtons(0, 0);
    state.adapter.setPadAnalog(0, padmap::Analog{});
}

#ifdef XRSHELL_LIBRARY
constexpr bool kLibraryMode = true;    // xr.dosgames, xr.shooter, xr.adventure: Spieleauswahl statt Autostart
#else
constexpr bool kLibraryMode = false;
#endif
#ifdef XRSHELL_LIBRARY_DOS
constexpr bool kDosLibrary = true;     // xr.dosgames: Spielordner unter games/dos/
#else
constexpr bool kDosLibrary = false;
#endif
#ifdef XRSHELL_LIBRARY_SHOOTER
constexpr bool kShooterLibrary = true; // xr.shooter: Spielordner unter games/doom|wolf3d|quake/
#else
constexpr bool kShooterLibrary = false;
#endif
#ifdef XRSHELL_LIBRARY_ADVENTURE
constexpr bool kAdventureLibrary = true;  // xr.adventure: Spielordner unter games/adventure/ (*.scummvm)
#else
constexpr bool kAdventureLibrary = false;
#endif
constexpr bool kFolderLibrary = kDosLibrary || kShooterLibrary || kAdventureLibrary;   // Ordner-Spiele
// xr.dosgames: Reiter „DOS“, Reiter „Ansicht“ heißt „Bild“, Bild- und Kern-Optionen pro Spielordner
// (<files>/settings/<Ordner>.user.opt). Andere Apps (auch xr.shell/xr.island mit DOSBox-Pure)
// bleiben unverändert — dort hängen Win-3.1-Maus und Theme-Park-Werte an festen Profilen.
constexpr bool kDosPage = kDosLibrary;
// Bildwerte (Filter, Sättigung, Kontrast, Wölbung, Fenstermaße) pro Spielordner gespeichert:
// xr.dosgames und xr.adventure (Standardfilter HD/xBR für 320×200-Spiele, im Menü änderbar).
// Nur xr.dosgames hat zusätzlich die Reiter DOS/Steuerung und die Mausfaktoren.
constexpr bool kGameViewPerGame = kDosPage || kAdventureLibrary;

// Standardfilter, solange im Menü nichts gewählt wurde: `xrshell_filter = …` in der
// xrshell.opt des Spiels, sonst App-Standard (xr.dosgames: HD, alle anderen: Scharf).
void updateDefaultFilter(AppState& state) {
    int filter = static_cast<int>(kDosLibrary ? ScreenFilter::Hd : ScreenFilter::Sharp);
    const char* option = state.adapter.profileOption("xrshell_filter");
    if (option && !parseScreenFilter(option, filter))
        LOGE("xrshell_filter = %s unbekannt (scharf, weich, scanlines, crt, hd, hd-scanlines)", option);
    state.defaultFilter = filter;
    LOGI("Standardfilter %d (%s)%s", filter, option ? "Spielordner" : "App",
         state.view.filterChosen ? ", im Menü gewählter Filter hat Vorrang" : "");
}

// xr.dosgames/xr.adventure: Bildwerte des Spiels (Nutzerwert vor xrshell.opt vor App-Standard HD).
// Fenstermaße nur, wenn das Spiel eigene hat — sonst bleibt das Fenster, wo es ist.
void applyGameView(AppState& state) {
    auto lookup = [&](const char* key, std::string& out) { return state.adapter.activeOption(key, out); };
    dosopt::GameView v = dosopt::gameViewFrom(lookup, static_cast<int>(ScreenFilter::Hd));
    state.defaultFilter = v.filter;
    state.view.filter = v.filter;
    state.view.filterChosen = false;   // die Wahl steht in den Optionen des Spiels
    state.view.saturation = v.saturation;
    state.view.contrast = v.contrast;
    state.view.curve = v.curve;
    state.view.pointerOffsetX = v.offsetX;
    state.view.pointerOffsetY = v.offsetY;
    if (v.width > 0.0f) {
        state.view.width = v.width;
        state.view.distance = v.distance;
        state.applyWindowPending = true;
    } else {
        v.width = state.view.width;
        v.distance = state.view.distance;
    }
    state.gameView = v;
    LOGI("Bild des Spiels: Filter %d, Sättigung %d %%, Kontrast %d %%, Wölbung %d%s", v.filter, v.saturation,
         v.contrast, v.curve, state.applyWindowPending ? ", eigene Fenstermaße" : "");
}

// xr.dosgames: Mausfaktoren des Spiels (relativer Modus) an den Adapter.
float mouseGain(const AppState& state, const char* key) {
    std::string v;
    return state.adapter.activeOption(key, v) ? mouserel::parseGain(v) : 1.0f;
}
void applyMouseGain(AppState& state) {
    state.adapter.setMouseGain(mouseGain(state, mouserel::kGainXKey), mouseGain(state, mouserel::kGainYKey));
    // „Zeiger lernen“: gespeicherte Kopplung des Spiels (leer = aus, siehe cursor_lock.h).
    std::string lock;
    state.adapter.setCursorLock(state.adapter.activeOption(curlock::kOptionKey, lock) ? lock : std::string());
}

// xr.dosgames: Belegung rechter Griff/Stick des Spiels (Nutzerwert vor xrshell.opt vor App-Standard
// „Karte ziehen“). Andere Apps behalten Griff = Rechtsklick, Stick = Pfeiltasten.
void applyControls(AppState& state) {
    if (!kDosPage) {
        state.controls = mapdrag::Settings{};
        // xr.adventure: Rechtsklick liegt auf B — der rechte Griff greift das Fenster wie der linke.
        if (kAdventureLibrary) state.controls.grip = mapdrag::kGripWindow;
        return;
    }
    auto lookup = [&](const char* key, std::string& out) { return state.adapter.activeOption(key, out); };
    state.controls = mapdrag::settingsFrom(lookup, mapdrag::kGripDrag);
    LOGI("Steuerung des Spiels: rechter Griff %s, %s, %s, Tempo %d, %d px, rechter Stick %s",
         mapdrag::gripKey(state.controls.grip), state.controls.model == mapdrag::kFollow ? "Hand folgt" : "Joystick",
         state.controls.direction == mapdrag::kNatural ? "natürlich" : "wie Stick", state.controls.speed,
         state.controls.stepPx, state.controls.stickArrows ? "Pfeiltasten" : "aus");
}

// xr.dosgames: im Menü geänderte Bildwerte als Nutzerwerte des Spiels ablegen. Fenstermaße nur bei
// bewusster Änderung (Menü, Greifen, „passend“), nicht beim bloßen Öffnen des Menüs.
void syncGameView(AppState& state, bool windowChanged) {
    if (!kGameViewPerGame || adapterBusy(state) || !state.adapter.gameRunning()) return;
    dosopt::GameView now = state.gameView;
    now.filter = state.view.filter;
    now.saturation = state.view.saturation;
    now.contrast = state.view.contrast;
    now.curve = state.view.curve;
    now.offsetX = state.view.pointerOffsetX;
    now.offsetY = state.view.pointerOffsetY;
    if (windowChanged) {
        now.width = state.view.width;
        now.distance = state.view.distance;
    }
    for (const auto& kv : dosopt::gameViewChanges(state.gameView, now)) {
        state.adapter.setUserOption(kv.first, kv.second, true);
        LOGI("Bild des Spiels: %s = %s", kv.first.c_str(), kv.second.c_str());
    }
    state.defaultFilter = now.filter;
    state.view.filterChosen = false;
    state.gameView = now;
}

// xr.dosgames: Inhalt des Reiters „DOS“ aus den Kern-Optionen des laufenden Spiels.
DosPage buildDosPage(const AppState& state) {
    DosPage page;
    page.gameRunning = state.adapter.gameRunning();
    if (!page.gameRunning) return page;
    const bool announced = state.adapter.coreOptionsAnnounced();
    std::string mouse;
    for (int i = 0; i < dosopt::kCount; ++i) {
        const auto id = static_cast<dosopt::Id>(i);
        const dosopt::Spec& spec = dosopt::spec(id);
        optstore::Definition def;
        const bool defined = state.adapter.coreOptionDefinition(spec.key, def);
        std::string v;
        if (!state.adapter.effectiveOption(spec.key, v)) v = defined ? def.defaultValue : spec.defaultValue;
        DosPage::Option& o = page.options[i];
        o.present = defined || !announced;   // nur, was der Core kennt (Rückfall: geprüfte Liste)
        o.value = dosopt::label(id, v);
        o.pending = state.adapter.optionPending(spec.key);
        page.restartPending |= o.present && o.pending;
        if (id == dosopt::kMouse) mouse = v;
    }
    // Mausgeschwindigkeit des Cores (eine Zahl für beide Achsen) ersetzen die XRShell-Faktoren
    // pro Achse (Y-Verhältnis oft 2:1); per xrshell.opt bleibt sie nutzbar.
    page.options[dosopt::kMouseSpeed].present = false;
    page.relative = mouse == "virtual" || mouse == "true";
    page.gainX = mouserel::gainLabel(mouseGain(state, mouserel::kGainXKey));
    page.gainY = mouserel::gainLabel(mouseGain(state, mouserel::kGainYKey));
    page.cursor = state.adapter.cursorLockState();
    page.hiresKey = state.keyProfile.bindingText(KeyProfile::kHires);
    page.userValues = state.adapter.hasUserOptions();
    return page;
}

// Kopfzeile (xr.dosgames): Spielauflösung und Seitenverhältnis, z. B. „320×200 · 4:3“.
std::string geometryInfo(const AppState& state) {
    const int w = state.renderer.frameWidth(), h = state.renderer.frameHeight();
    if (!state.adapter.gameRunning() || w <= 0 || h <= 0) return "";
    const float a = state.screenAspect;
    char aspect[16];
    if (std::fabs(a - 4.0f / 3.0f) < 0.01f) std::snprintf(aspect, sizeof(aspect), "4:3");
    else if (std::fabs(a - 1.6f) < 0.01f) std::snprintf(aspect, sizeof(aspect), "16:10");
    else std::snprintf(aspect, sizeof(aspect), "%.2f:1", static_cast<double>(a));
    char out[48];
    std::snprintf(out, sizeof(out), "%d×%d · %s", w, h, aspect);
    return out;
}

// Zuletzt gespieltes Spiel (Pfad) im App-Speicher — Start setzt dort fort.
std::string lastGameFile(const AppState& state) {
    const std::string& base = state.settingsPath;
    return base.substr(0, base.rfind('/') + 1) + "xrshell-last.txt";
}

std::string readLastGame(const AppState& state) {
    std::string last;
    if (FILE* f = std::fopen(lastGameFile(state).c_str(), "r")) {
        char buf[1024] = {};
        if (std::fgets(buf, sizeof(buf), f)) last = buf;
        std::fclose(f);
    }
    return last;
}

// xr.adventure: Bedienungshilfe im Reiter „Spiel“ (Tasten aus dem Profil, sonst Grundbelegung).
std::vector<std::string> adventureHelp(const AppState& state) {
    const auto bound = [&](KeyProfile::Input in) { return state.keyProfile.bound(in); };
    std::string first = "Trigger Klick \xC2\xB7 B Ansehen";
    if (bound(KeyProfile::kA) && state.keyProfile.bindingText(KeyProfile::kA) ==
                                     state.keyProfile.bindingText(KeyProfile::kGameMenu))
        first += " \xC2\xB7 A Spielmenü";
    std::string second = "Griff Fenster \xC2\xB7 X Zoom \xC2\xB7 Y Tastatur";
    if (bound(KeyProfile::kRightStickClick) && state.keyProfile.bindingText(KeyProfile::kRightStickClick) == "ESC")
        second += " \xC2\xB7 Stick Szene überspringen";
    return { first, second };
}

// Bibliothek neu einlesen und dem Menü geben (Einträge „System<TAB>Titel“, laufendes und
// zuletzt gespieltes Spiel für „läuft“/„zuletzt“ und den Knopf „Weiterspielen“).
void refreshLibrary(AppState& state) {
    state.library = state.adapter.scanLibrary();
    const std::string last = readLastGame(state);
    std::vector<std::string> labels;
    int running = -1, lastIndex = -1;
    for (size_t i = 0; i < state.library.size(); ++i) {
        labels.push_back(state.library[i].system + "	" + state.library[i].title);
        if (state.library[i].path == state.adapter.gamePath()) running = static_cast<int>(i);
        if (!last.empty() && state.library[i].path == last) lastIndex = static_cast<int>(i);
    }
    state.menu.setLibrary(labels, running, kLibraryMode, lastIndex);
}

// Bildschirmtastatur passend zur Gastbelegung: DOSBox-Pure übersetzt nach Tastenposition,
// eine deutsche Belegung im Gast (keyboard_layout = gr) braucht daher QWERTZ-Beschriftung.
void applyKeyboardLayout(AppState& state) {
    if (state.heldOverlayKey >= 0) {   // Tastenindizes ändern sich
        state.adapter.setKey(state.overlay.retroKey(state.heldOverlayKey), false);
        state.heldOverlayKey = -1;
    }
    state.overlay.setGermanLayout(
        kblayout::isGerman(state.adapter.profileOption("dosbox_pure_keyboard_layout")));
}

// Nach erfolgreichem adapter.start() (Main-Thread, Lade-Thread ist abgeholt): alles, was zum Spiel
// gehört — Tastenbelegung, Bildwerte, Tastenprofil und Menü-Optik (xrshell.keys/.theme im
// Spielordner bzw. neben der Spieldatei); Bibliothek: als „zuletzt gespielt“ merken.
void finishStart(AppState& state, const std::string& path) {
    loadPadMapping(state);
    if (state.adapter.padMode() && state.overlay.visible()) {
        // Pad-Modus (Shooter, DOS mit Gamepad): keine Bildschirmtastatur — Y gehört dem
        // Spiel, Eingaben gehen nicht an das Overlay. Eine vom DOS-Spiel offene Tastatur schließen.
        if (state.heldOverlayKey >= 0) state.adapter.setKey(state.overlay.retroKey(state.heldOverlayKey), false);
        state.heldOverlayKey = -1;
        state.overlay.setPressedKey(-1);
        state.overlay.hide();
        LOGI("Tastatur-Overlay: aus (Pad-Modus)");
    }
    if (kGameViewPerGame) {
        // Früher global im Menü gewählter Filter (filter_chosen) → einmalig als Wahl des ersten
        // gestarteten Spiels (des zuletzt gespielten), danach gilt er pro Spiel.
        std::string own;
        if (state.filterMigration >= 0 && !state.adapter.userOption(dosopt::kFilterKey, own)) {
            state.adapter.setUserOption(dosopt::kFilterKey, screenFilterKey(state.filterMigration), true);
            LOGI("Filterwahl %d übernommen als Wahl für %s", state.filterMigration, path.c_str());
        }
        state.filterMigration = -1;
        applyGameView(state);
    } else {
        updateDefaultFilter(state);
    }
    if (kDosPage) {
        applyMouseGain(state);
        applyControls(state);
        state.calibrating = false;
        state.adapter.setPointerFrozen(false);
    } else {
        applyControls(state);   // xr.adventure: rechter Griff = Fenster
    }
    state.keyProfile.load(path);
    // xr.adventure: ScummVM-Menü (Speichern/Laden/Optionen) liegt bei jedem Spiel auf Strg+F5.
    if (state.adapter.scummCore()) state.keyProfile.setDefault(KeyProfile::kGameMenu, "LCTRL+F5");
    applyKeyboardLayout(state);
    MenuTheme theme;
    theme.load(path);
    state.menu.setTheme(theme);
    if (kLibraryMode) {
        if (FILE* f = std::fopen(lastGameFile(state).c_str(), "w")) {
            std::fputs(path.c_str(), f);
            std::fclose(f);
        }
        refreshLibrary(state);
    }
}

// --- Ladeanzeige: Systemcheck, Lade-Thread, Startsignal, Watchdog -----------------------------

// Absturz-Marker: liegt beim Start noch einer, endete die letzte Sitzung nicht sauber (L6).
void writeMarker(const AppState& state) {
    if (state.markerPath.empty()) return;
    if (FILE* f = std::fopen(state.markerPath.c_str(), "w")) {
        std::fputs(slog::currentPath().c_str(), f);
        std::fclose(f);
    }
}
void removeMarker(const AppState& state) {
    if (!state.markerPath.empty()) ::unlink(state.markerPath.c_str());
}

long long freeMegabytes(const char* path) {
    struct statvfs st {};
    if (!path || ::statvfs(path, &st) != 0) return -1;
    return static_cast<long long>(st.f_bavail) * static_cast<long long>(st.f_frsize) / (1024 * 1024);
}

// Akku in % (−1 = nicht lesbar) und ob er lädt.
int batteryPercent(bool& charging) {
    charging = false;
    int percent = -1;
    if (FILE* f = std::fopen("/sys/class/power_supply/battery/capacity", "r")) {
        if (std::fscanf(f, "%d", &percent) != 1) percent = -1;
        std::fclose(f);
    }
    if (FILE* f = std::fopen("/sys/class/power_supply/battery/status", "r")) {
        char status[32] = {};
        if (std::fgets(status, sizeof(status), f)) charging = std::strncmp(status, "Charging", 8) == 0 ||
                                                              std::strncmp(status, "Full", 4) == 0;
        std::fclose(f);
    }
    return percent;
}

// Speicher, Akku und Controller (alle 1 s, Controller erst 3 s nach dem OpenXR-Start bewerten).
void refreshChecks(AppState& state, double now, bool force = false) {
    if (!force && now < state.nextCheck) return;
    state.nextCheck = now + 1.0;
    const loadst::CheckLine controller =
        loadst::checkController(state.xr.interactionProfile(), state.xrReadyAt > 0.0 ? now - state.xrReadyAt : 0.0);
    if (controller != state.checks[AppState::kChkController]) {
        if (controller.level != loadst::Level::Pending) LOGI("Systemcheck %s", loadst::logLine(controller).c_str());
        state.checks[AppState::kChkController] = controller;
    }
    if (force) {
        bool charging = false;
        const int battery = batteryPercent(charging);
        state.checks[AppState::kChkBattery] = loadst::checkBattery(battery, charging);
        state.checks[AppState::kChkStorage] = loadst::checkStorage(freeMegabytes(state.android->activity->externalDataPath));
    }
}

// Systemcheck beim App-Start (< 300 ms): OpenXR, Log, letzter Start, Speicher, Akku, Bibliothek.
void initChecks(AppState& state, double now) {
    state.checks[AppState::kChkXr] = loadst::checkOpenXr(true, state.xr.runtimeName());
    state.checks[AppState::kChkController] = loadst::checkController("", 0.0);
    state.checks[AppState::kChkData] = kLibraryMode ? loadst::checkLibrary(state.library.size(), state.emptyLibraryHint)
                                                    : loadst::CheckLine{loadst::Level::Pending, "Spieldaten", "wird geprüft …"};
    state.checks[AppState::kChkCore] = loadst::checkCore(false, "");
    state.checks[AppState::kChkLog] = loadst::checkLog(slog::currentPath());
    state.checks[AppState::kChkPrevious] = loadst::checkPreviousRun(state.previousCrash, state.previousLog);
    refreshChecks(state, now, true);
    for (const loadst::CheckLine& c : state.checks)
        if (c.level != loadst::Level::Pending) LOGI("Systemcheck %s", loadst::logLine(c).c_str());
}

// Anzeigename eines Spiels: Bibliothekstitel, sonst Datei-/Ordnername ohne Endung.
std::string gameTitle(const AppState& state, const std::string& path) {
    for (const auto& game : state.library)
        if (game.path == path) return game.title;
    std::string name = path;
    while (!name.empty() && name.back() == '/') name.pop_back();
    name = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
    const size_t dot = name.rfind('.');
    return dot != std::string::npos && dot > 0 ? name.substr(0, dot) : name;
}

void saveLoadHistory(AppState& state) {
    if (state.loadHistoryPath.empty()) return;
    state.loadHistory.version = state.info.versionCode;
    const std::string tmp = state.loadHistoryPath + ".tmp";
    const std::string text = state.loadHistory.serialize();
    FILE* f = std::fopen(tmp.c_str(), "wb");
    bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (f && std::fclose(f) != 0) ok = false;
    if (ok && ::rename(tmp.c_str(), state.loadHistoryPath.c_str()) == 0) return;
    ::unlink(tmp.c_str());
    LOGE("Ladezeiten nicht speicherbar: %s", state.loadHistoryPath.c_str());
}

void logSteps(const AppState& state, double now) {
    for (int i = 0; i < loadst::kStepCount; ++i) {
        const loadst::StepState s = state.load.state(i);
        if (s == loadst::StepState::Done || s == loadst::StepState::Failed)
            LOGI("Ladeschritt %s: %.0f ms%s", loadst::stepName(i), state.load.durationS(i, now) * 1000.0,
                 s == loadst::StepState::Failed ? " (fehlgeschlagen)" : "");
    }
}

// Start gescheitert (Pflichtprüfung, adapter.start, Watchdog) → Fehlerseite.
void failLoad(AppState& state, loadst::Failure failure, const std::string& detail, double now) {
    const int active = state.load.activeStep();
    if (active >= 0) state.load.failStep(active, now);
    state.load.end();
    state.phase = AppState::Phase::Failed;
    state.failure = failure;
    state.failureDetail = detail;
    state.failedAfter = state.load.elapsedS(now);
    state.pageHidden = false;
    logSteps(state, now);
    LOGE("Fehlerseite: %s – %s (%s)", state.loadTitle.c_str(), detail.c_str(), loadst::seconds(state.failedAfter).c_str());
    slog::flush();
}

// Laden beginnen: Systemcheck des Spiels, dann adapter.start() auf dem Lade-Thread. Die
// XR-Schleife zeigt währenddessen die Ladeanzeige (L2) — auch wenn retro_load_game blockiert.
// Gilt für App-Start, Spielwechsel aus der Bibliothek und „Spiel neu starten“.
bool beginLoad(AppState& state, const std::string& path, double now) {
    if (adapterBusy(state)) {
        LOGI("Laden abgelehnt – es lädt noch: %s", state.loadTitle.c_str());
        return false;
    }
    releaseGameInputs(state, 0);   // nichts bleibt im alten Spiel gedrückt
    state.renderer.setCoreFrame(0, 0);   // altes Bild nicht stehen lassen
    state.loadPath = path;
    state.loadTitle = path.empty() ? std::string() : gameTitle(state, path);
    const std::string so = path.empty() ? std::string() : LibretroAdapter::coreSoFor(path);
    state.loadCore = so.empty() ? std::string("unbekannt") : loadst::coreKey(so);
    state.loadSeenStage = LibretroAdapter::kStageIdle;
    state.watchdogFired = false;
    state.pageHidden = false;
    state.failure = loadst::Failure::None;
    state.failureDetail.clear();
    // Erster Start nach Installation/Update: der Core wird (vermutlich) entpackt; bestätigt bzw.
    // korrigiert sich, sobald die Etappe „Core“ fertig ist (adapter.coreExtracted()).
    const bool versionChanged = state.loadHistory.version != state.info.versionCode;
    state.loadFirstRun = versionChanged || !state.loadHistory.hasHistory(state.loadCore, false);
    std::array<double, loadst::kStepCount> expected{};
    std::array<bool, loadst::kStepCount> used{};
    for (int i = 0; i < loadst::kStepCount; ++i) {
        expected[i] = state.loadHistory.expectedMs(state.loadCore, i, state.loadFirstRun);
        used[i] = true;
    }
    used[loadst::kStopPrevious] = state.adapter.coreLoaded();
    state.load.begin(now, expected, used, state.loadHistory.hasHistory(state.loadCore, state.loadFirstRun),
                     state.loadFirstRun);
    state.load.startStep(loadst::kSystemCheck, now);
    state.phase = AppState::Phase::Loading;
    state.watchdog.reset(now);
    state.loadPausedS = 0.0;
    state.lastPoll = now;
    LOGI("Laden: %s (%s), Core %s, erwartet %s%s", state.loadTitle.c_str(), path.c_str(), state.loadCore.c_str(),
         loadst::seconds(state.load.totalExpectedS()).c_str(), state.loadFirstRun ? ", erster Start" : "");

    // Systemcheck des Spiels: Daten vorhanden/lesbar, Core im APK, Speicher, Akku.
    struct stat st {};
    const bool exists = !path.empty() && ::stat(path.c_str(), &st) == 0;
    const bool readable = exists && ::access(path.c_str(), R_OK) == 0;
    state.checks[AppState::kChkData] = path.empty()
        ? loadst::CheckLine{loadst::Level::Fail, "Spieldaten", "kein Spiel in files/game/"}
        : loadst::checkGameData(exists, readable, state.loadTitle);
    state.checks[AppState::kChkCore] = loadst::checkCore(!so.empty() && state.adapter.coreAvailable(so), state.loadCore);
    refreshChecks(state, now, true);
    LOGI("Systemcheck %s", loadst::logLine(state.checks[AppState::kChkData]).c_str());
    LOGI("Systemcheck %s", loadst::logLine(state.checks[AppState::kChkCore]).c_str());
    const double checked = steadyNow();
    state.load.finishStep(loadst::kSystemCheck, checked);
    if (path.empty()) {
        failLoad(state, loadst::Failure::NoGame, "Kein Spiel in files/game/", checked);
        return false;
    }
    if (!exists || !readable) {
        failLoad(state, exists ? loadst::Failure::DataUnreadable : loadst::Failure::NoGame,
                 (exists ? "Nicht lesbar: " : "Fehlt: ") + path, checked);
        return false;
    }
    if (state.checks[AppState::kChkCore].level == loadst::Level::Fail) {
        failLoad(state, loadst::Failure::CoreMissing, so + " fehlt im APK", checked);
        return false;
    }

    state.loadDone.store(false);
    state.loadOk = false;
    state.adapter.resetLoadStage();   // keine Etappen des vorigen Starts übernehmen
    state.loadThread = std::thread([&state, path] {
        state.loadOk = state.adapter.start(path);   // dlopen, retro_init, retro_load_game, Core-Thread
        state.loadDone.store(true);
    });
    return true;
}

// Etappen des Lade-Threads in Schritte übernehmen (Zeitpunkte vom Adapter, nicht vom Frame).
void syncStages(AppState& state) {
    static const int kStepOf[LibretroAdapter::kStageCount] = {-1, loadst::kStopPrevious, loadst::kGameData,
                                                               loadst::kCore, loadst::kLoadGame, loadst::kFirstFrame};
    const int stage = state.adapter.loadStage();
    for (int s = state.loadSeenStage + 1; s <= stage && s < LibretroAdapter::kStageCount; ++s) {
        const double t = state.adapter.loadStageTime(s);
        // Etappe kam in diesem Start nicht vor (kein Spielwechsel) bzw. stammt von einem früheren
        if (t <= 0.0 || t < state.load.beginTime() || kStepOf[s] < 0) continue;
        state.load.startStep(kStepOf[s], t);
        state.watchdog.progress(steadyNow());
        if (kStepOf[s] == loadst::kLoadGame && state.adapter.coreExtracted() != state.loadFirstRun) {
            // Core-Etappe vorbei: jetzt ist klar, ob entpackt wurde — erwartete Zeiten nachziehen.
            state.loadFirstRun = state.adapter.coreExtracted();
            for (int i = loadst::kLoadGame; i < loadst::kStepCount; ++i)
                state.load.setExpected(i, state.loadHistory.expectedMs(state.loadCore, i, state.loadFirstRun));
            state.load.setHistory(state.loadHistory.hasHistory(state.loadCore, state.loadFirstRun), state.loadFirstRun);
        }
    }
    if (stage > state.loadSeenStage) state.loadSeenStage = stage;
}

// Startsignal (L8): Ladeanzeige weg, Messwerte speichern, gehaltene Tasten erst loslassen lassen.
void gameVisible(AppState& state, double now, XrTime displayTime, const char* reason) {
    state.load.finishStep(loadst::kFirstFrame, now);
    state.load.end();
    logSteps(state, now);
    LOGI("Spiel sichtbar nach %s (%s; seit App-Start %s)%s", loadst::seconds(state.load.elapsedS(now)).c_str(), reason,
         loadst::seconds(now - state.appStart).c_str(), state.watchdogFired ? ", nach Watchdog" : "");
    // Messwerte: nur echte Starts (kein B-Durchklicken), Ausreißer gewichtet die History selbst.
    if (std::strcmp(reason, "B gedrückt") != 0) {
        for (int i = 0; i < loadst::kStepCount; ++i)
            if (state.load.state(i) == loadst::StepState::Done)
                state.loadHistory.record(state.loadCore, i, state.load.durationS(i, now) * 1000.0, state.loadFirstRun);
        saveLoadHistory(state);
    }
    state.phase = AppState::Phase::Shown;
    state.pageHidden = false;
    state.watchdogFired = false;
    releaseGameInputs(state, displayTime);
    state.waitButtonRelease = true;   // Trigger/A/B, die beim Laden gehalten wurden, erst loslassen
    state.pointerFiltered = false;
    slog::flush();
}

// Pro Frame: Lade-Thread abholen, Etappen übernehmen, Startsignal, Watchdog.
// paused: VR-Menü offen oder kein Fokus — der Core hält an, das zählt nicht gegen den Watchdog.
void pollLoad(AppState& state, double now, bool newCoreFrame, XrTime displayTime, bool paused) {
    using Phase = AppState::Phase;
    if (state.phase == Phase::Loading) {
        syncStages(state);
        if (state.loadThread.joinable() && state.loadDone.load()) {
            state.loadThread.join();
            syncStages(state);
            if (state.loadOk) {
                finishStart(state, state.loadPath);
                state.phase = Phase::FirstFrame;
                const bool dos = state.loadCore == "dosbox_pure";
                state.frameSignal.reset(dos, loadst::FrameSignal::blackFallbackFor(
                                                 state.load.expectedMs(loadst::kFirstFrame) / 1000.0));
                state.watchdog.progress(now);
                LOGI("Core läuft – warte auf das erste Spielbild%s", dos ? " (DOS: Grafikmodus oder 3 s Ruhe)" : "");
            } else {
                std::string detail = state.adapter.startFailureDetail();
                const std::string coreLine = LibretroAdapter::lastCoreMessage();
                if (!coreLine.empty() && detail.find(coreLine) == std::string::npos) detail += " – Core: " + coreLine;
                failLoad(state, state.adapter.startFailure(), detail, now);
                if (kLibraryMode) refreshLibrary(state);   // kein Spiel läuft mehr
            }
        }
    }
    if (state.phase == Phase::FirstFrame) {
        loadst::FrameSignal::Result r = loadst::FrameSignal::kWait;
        if (newCoreFrame) {
            state.watchdog.progress(now);
            r = state.frameSignal.onFrame(now, state.adapter.frameWidth(), state.adapter.frameHeight(),
                                          state.adapter.lastFrameHasContent());
        } else {
            r = state.frameSignal.onTick(now);
        }
        if (r != loadst::FrameSignal::kWait) gameVisible(state, now, displayTime, loadst::FrameSignal::describe(r));
        else if (state.pageHidden) gameVisible(state, now, displayTime, "B gedrückt");
    }
    const double dt = std::max(0.0, now - state.lastPoll);
    state.lastPoll = now;
    if (state.phase == Phase::FirstFrame && paused) {
        state.loadPausedS += dt;           // angehaltener Core liefert zu Recht kein Bild
        state.watchdog.progress(now);
    }
    if (state.phase == Phase::Loading || state.phase == Phase::FirstFrame) {
        if (state.watchdog.shouldLog(now)) {
            const int step = state.load.activeStep();
            LOGE("Watchdog: %s seit %s ohne Fortschritt (Schritt läuft %s)", step >= 0 ? loadst::stepName(step) : "?",
                 loadst::seconds(state.watchdog.stalledS(now)).c_str(),
                 loadst::seconds(step >= 0 ? state.load.durationS(step, now) : 0.0).c_str());
            slog::flush();
        }
        const double limit = loadst::Watchdog::errorAfter(state.load.totalExpectedS());
        if (!state.watchdogFired && state.load.elapsedS(now) - state.loadPausedS > limit) {
            state.watchdogFired = true;
            LOGE("Watchdog: Start dauert über %s (erwartet %s) – Fehlerseite, B zeigt das Bild",
                 loadst::seconds(limit).c_str(), loadst::seconds(state.load.totalExpectedS()).c_str());
            slog::flush();
        }
    }
}

bool panelVisible(const AppState& state) {
    return state.loadScreenReady && state.phase != AppState::Phase::Shown && !state.pageHidden;
}

// Pad-Apps (Shooter) vs. Maus-Apps (DOS, Adventures) — Kurzbedienung auf der Anzeige.
bool padApp(const AppState& state) {
    if (!adapterBusy(state) && state.adapter.gameRunning()) return state.adapter.padMode();
    return kShooterLibrary;
}

std::string shortLogPath() {
    std::string path = slog::currentPath();
    const std::string prefix = "/storage/emulated/0/";
    if (path.compare(0, prefix.size(), prefix) == 0) path = path.substr(prefix.size());
    return path;
}

// Inhalt der Ladeanzeige aus Phase, Ladevorgang und Systemcheck.
LoadView buildLoadView(AppState& state, double now) {
    using Phase = AppState::Phase;
    LoadView v;
    v.app = state.info.appId.empty() ? std::string("xr.shell") : state.info.appId;
    v.version = state.info.shortVersion();
    v.checks = state.checks;
    v.help1 = std::string("Menütaste links: VR-Menü (") + (kLibraryMode ? "Spiele, " : "") + "Beenden) · Griff links: Fenster greifen";
    v.help2 = padApp(state) ? "Controller = Gamepad · Belegung: VR-Menü → Tasten"
                            : "Trigger: Klick · B: Rechtsklick · Y: Bildschirmtastatur";
    const bool error = state.phase == Phase::Failed || state.watchdogFired;
    if (error) {
        const loadst::Failure f = state.phase == Phase::Failed ? state.failure : loadst::Failure::Watchdog;
        const loadst::ErrorText e = loadst::errorText(f, state.failureDetail, v.app, state.loadPath, state.loadCore,
                                                      state.load.elapsedS(now));
        v.mode = LoadView::kError;
        v.game = state.loadTitle;
        v.headline = f == loadst::Failure::Watchdog ? "Start hängt?" : "Start fehlgeschlagen";
        v.elapsed = state.phase == Phase::Failed ? "nach " + loadst::seconds(state.failedAfter)
                                                 : "seit " + std::to_string(static_cast<int>(state.load.elapsedS(now))) + " s";
        v.errorTitle = e.title;
        v.errorDetail = f == loadst::Failure::Watchdog
            ? "Schritt: " + std::string(state.load.activeStep() >= 0 ? loadst::stepName(state.load.activeStep()) : "?") +
              " · erwartet " + loadst::seconds(state.load.totalExpectedS())
            : e.detail;
        v.remedy = e.remedy;
        v.logPath = shortLogPath();
        v.logHint = "Am PC holen: tools/pull-logs.sh " + v.app;
        v.help1 = std::string("B: Spielbild trotzdem zeigen · Menütaste links: VR-Menü (") +
                  (kLibraryMode ? "anderes Spiel, " : "") + "Beenden)";
        v.help2.clear();
        return v;
    }
    if (state.phase == Phase::Loading || state.phase == Phase::FirstFrame) {
        v.mode = LoadView::kLoading;
        v.game = state.loadTitle;
        v.fillFrom(state.load, now);
        if (state.watchdog.hint(now)) {
            v.notice = (state.phase == Phase::Loading ? "Core antwortet seit " : "Core liefert seit ") +
                       std::to_string(static_cast<int>(state.watchdog.stalledS(now))) +
                       (state.phase == Phase::Loading ? " s nicht – wird beobachtet" : " s kein Bild – wird beobachtet");
        } else if (state.loadFirstRun) {
            v.notice = state.adapter.coreExtracted() ? "Erster Start nach Installation/Update – Core wurde entpackt"
                                                     : "Erster Start nach Installation/Update – dauert etwas länger";
        }   // ein Absturz beim letzten Start steht im Systemcheck („Letzter Start“)
        return v;
    }
    // Leerlauf (Bibliothek ohne laufendes Spiel)
    v.mode = LoadView::kIdle;
    if (state.library.empty()) {
        v.headline = "Noch keine Spiele";
        v.eta = state.emptyLibraryHint;
        v.info = {loadst::pushCommand(v.app, ""), "Danach VR-Menü öffnen – die Liste liest den Ordner neu ein"};
    } else {
        v.headline = state.menu.visible() ? "Spiel im Menü auswählen" : "Menütaste links: Spieleliste öffnen";
        v.eta = std::to_string(state.library.size()) + (state.library.size() == 1 ? " Spiel" : " Spiele") +
                " in der Liste · Fortschritt erscheint hier beim Laden";
        v.info = {"Spiel in der Liste wählen: Strahl + Trigger oder Stick + A",
                  "„Weiterspielen“ (Reiter Spiel) setzt das zuletzt gespielte Spiel fort",
                  "Beim Start: VR-Menü → Spiel → „Beim Start“"};
    }
    if (state.previousCrash) v.notice = "Letzter Start ist abgestürzt – Log liegt unter files/logs/";
    return v;
}

// --- Videoaufnahme -------------------------------------------------------------------
// Ton-Abgriff im AAudio-Callback → Ring der Aufnahme (echtzeitfest).
void recorderAudioTap(void* user, const int16_t* samples, int32_t frames) {
    static_cast<VideoRecorder*>(user)->pushAudio(samples, frames);
}

// Aufnahme starten: Spielbild ganzzahlig vergrößert (max. 1920×1080), Ziel zuerst
// Movies/<Paket>/ (Galerie/Dateien der Quest), sonst der App-Ordner. Liefert Status.
std::string startRecording(AppState& state) {
    const int fw = std::max(1, state.renderer.frameWidth());
    const int fh = std::max(1, state.renderer.frameHeight());
    const int k = std::max(1, std::min(1920 / fw, 1080 / fh));
    const std::string external = state.android->activity->externalDataPath;   // …/Android/data/<paket>/files
    const size_t pkgEnd = external.rfind("/files");
    const size_t pkgStart = external.rfind('/', pkgEnd - 1);
    const std::string package = pkgEnd != std::string::npos && pkgStart != std::string::npos
                                    ? external.substr(pkgStart + 1, pkgEnd - pkgStart - 1) : "xrshell";
    const std::string dirs[2] = { "/storage/emulated/0/Movies/" + package, external + "/videos" };
    for (const std::string& dir : dirs) {
        if (state.recorder.start(state.egl.display, state.egl.config, state.egl.context, dir,
                                 fw * k, fh * k)) {
            state.adapter.setAudioTap(recorderAudioTap, &state.recorder);
            return "Aufnahme läuft";
        }
    }
    return "Aufnahme nicht möglich";
}

std::string stopRecording(AppState& state) {
    if (!state.recorder.active()) return "";
    state.adapter.setAudioTap(nullptr, nullptr);   // erst Abgriff lösen, dann abschließen
    state.recorder.stop();
    const std::string& path = state.recorder.path();
    return "Video gespeichert: " + path.substr(path.rfind('/') + 1);
}

// Ansichtsprofil N im App-Speicher (VR-Menü, Reiter „Spiel“).
std::string profilePath(const AppState& state, int slot) {
    const std::string& base = state.settingsPath;   // …/xrshell-view.cfg
    return base.substr(0, base.rfind('/') + 1) + "xrshell-profile" + std::to_string(slot) + ".cfg";
}

// Screen vor die (neu zentrierte) Blickrichtung setzen — mit gespeicherter
// Breite/Abstand, sonst Standard. useSaved = false setzt auf Standard zurück.
void placeScreen(AppState& state, float eyeHeight, bool useSaved = true) {
    const bool saved = useSaved && state.view.width > 0.0f && state.view.distance >= kMinSavedDistance;
    state.screenDistance = saved ? std::fmin(std::fmax(state.view.distance, kMinDistance), kMaxDistance)
                                 : kDefaultDistance;
    state.screen.width = clampWidth(saved ? state.view.width : kDefaultWidth, state.screenDistance);
    state.screen.height = state.screen.width / state.screenAspect;
    state.screen.posX = 0.0f;
    state.screen.posY = eyeHeight - kScreenBelowEyes;
    state.screen.posZ = -state.screenDistance;
    state.screen.yaw = 0.0f;   // Inhaltsraum ist auf den Blick zentriert
    state.pointerFiltered = false;
}

void handleAppCmd(android_app* app, int32_t cmd) {
    auto* state = static_cast<AppState*>(app->userData);
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (!state->running) break;   // Nachlauf nach dem Beenden (siehe Ende von android_main): nichts neu aufbauen
        if (egl_init(app, state->egl)) {
            if (!state->xrReady && state->xr.init(app, state->egl) &&
                state->renderer.init()) {
                // #5: Tastatur-Overlay (GL-Objekte) — Ausfall blockiert nicht.
                if (!state->overlay.init())
                    LOGE("Tastatur-Overlay konnte nicht initialisiert werden");
                if (!state->menu.init())
                    LOGE("VR-Menü konnte nicht initialisiert werden");
                state->settingsPath =
                    std::string(app->activity->internalDataPath) + "/xrshell-view.cfg";
                if (state->view.load(state->settingsPath))
                    LOGI("Ansicht geladen: Passthrough %d%%, Wölbung %d, Filter %d",
                         state->view.passthrough, state->view.curve, state->view.filter);
                state->adapter.setLanguage(state->info.language, state->info.country);   // scummvm.ini
                if (kGameViewPerGame && state->view.filterChosen) {
                    // xr.dosgames/xr.adventure: Filter gilt jetzt pro Spiel — alte globale Wahl einmal übernehmen.
                    state->filterMigration = state->view.filter;
                    state->view.filterChosen = false;
                }
                // Versionsnummer in der Menü-Kopfzeile (ohne +Commit; der steht im Sitzungslog).
                const std::string version =
                    state->info.versionName.empty() ? std::string() : " · " + state->info.shortVersion();
                // Ladeanzeige (L2): eigene Tafel an der Stelle des Spielfensters, ab dem ersten XR-Frame.
                state->loadScreenReady = state->loadScreen.init();
                if (!state->loadScreenReady) LOGE("Ladeanzeige konnte nicht initialisiert werden");
                state->loadHistoryPath = std::string(app->activity->internalDataPath) + "/xr-load-times.json";
                {
                    std::string text;
                    if (FILE* f = std::fopen(state->loadHistoryPath.c_str(), "rb")) {
                        char buf[4096];
                        size_t n;
                        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
                        std::fclose(f);
                        if (state->loadHistory.parse(text))
                            LOGI("Ladezeiten geladen (%zu Cores, versionCode %lld)", state->loadHistory.cores.size(),
                                 state->loadHistory.version);
                        else
                            LOGE("Ladezeiten unlesbar, Standardwerte: %s", state->loadHistoryPath.c_str());
                    }
                }
                state->xrReadyAt = steadyNow();
                state->adapter.attach(app);
                if (kLibraryMode) {
                    // Bibliotheks-Apps: Standard ist das Menü mit Spieleliste und „Weiterspielen“;
                    // „Beim Start: Letztes Spiel“ (Reiter Spiel) setzt das letzte Spiel sofort fort.
                    // Kopfzeile mit App-Namen; Liste + Suche links (zweispaltig).
                    if (kDosLibrary) state->menu.setTitle("xr.dosgames", "DOS-Spiele" + version);
                    else if (kShooterLibrary) state->menu.setTitle("xr.shooter", "Shooter" + version);
                    else state->menu.setTitle("xr.adventure", "Adventures" + version);
                    // xr.dosgames/xr.adventure: Menüwerte pro Spielordner
                    state->adapter.enableUserOptions(kGameViewPerGame);
                    if (kDosLibrary) state->emptyLibraryHint = "Ordner nach files/games/dos/ kopieren";
                    else if (kShooterLibrary) state->emptyLibraryHint = "Ordner nach files/games/doom|wolf3d|quake/";
                    else state->emptyLibraryHint = "Ordner mit *.scummvm nach files/games/adventure/";
                    state->menu.setLibraryEmptyHint("Keine Spiele – " + state->emptyLibraryHint);
                    refreshLibrary(*state);
                    initChecks(*state, steadyNow());
                    const std::string last = readLastGame(*state);
                    const bool resume = state->view.startMode == ViewSettings::kStartResume;
                    bool started = false;
                    bool known = false;
                    for (const auto& game : state->library)
                        if (game.path == last) known = true;
                    if (known) {   // Core des letzten Spiels für den Systemcheck
                        const std::string so = LibretroAdapter::coreSoFor(last);
                        state->checks[AppState::kChkCore] = loadst::checkCore(state->adapter.coreAvailable(so),
                                                                               loadst::coreKey(so));
                    }
                    if (known && resume) started = beginLoad(*state, last, steadyNow());
                    LOGI("Bibliothek: %zu Spiele, Start %s, %s", state->library.size(),
                         resume ? "letztes Spiel" : "Menü",
                         started ? "letztes Spiel wird geladen"
                         : known ? "Auswahl mit „Weiterspielen“ anzeigen" : "Auswahl anzeigen");
                    state->openLibraryPending = !started && state->phase != AppState::Phase::Failed;
                } else {
                    state->menu.setTitle("XR-Menü", state->info.shortVersion());   // xr.shell, xr.island
                    // Einzelspiel-Apps: Spiel aus files/game/ laden (Lade-Thread); ohne Spiel Fehlerseite
                    // mit Übertragungsbefehl, B zeigt das Testmuster.
                    initChecks(*state, steadyNow());
                    beginLoad(*state, state->adapter.findDefaultGame(), steadyNow());
                }
                state->xrReady = true;
                LOGI("XRShell bereit");
            }
        }
        break;
    case APP_CMD_TERM_WINDOW:
        break;   // EGL leben lassen; XR-Session über Events stoppen
    case APP_CMD_PAUSE:
    case APP_CMD_STOP:
    case APP_CMD_SAVE_STATE:
        slog::flush();   // Brille ab / App im Hintergrund: Log sicher auf der Karte
        // Quest-Home/Brille ab: Spielstand im Modul-Speicher sichern — nicht, solange der Lade-Thread
        // den Adapter hat (dann gibt es noch keinen Speicher des neuen Spiels).
        if (!adapterBusy(*state)) state->adapter.flushSaveRam();
        if (cmd != APP_CMD_SAVE_STATE) removeMarker(*state);   // im Hintergrund darf Android beenden
        break;
    case APP_CMD_RESUME:
        writeMarker(*state);   // läuft wieder: ein Absturz ab hier zählt beim nächsten Start
        break;
    case APP_CMD_DESTROY:
        slog::flush();
        state->running = false;
        break;
    default:
        break;
    }
}

int32_t handleInput(android_app* app, AInputEvent* event) {
    auto* state = static_cast<AppState*>(app->userData);
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY &&
        AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN &&
        AKeyEvent_getKeyCode(event) == AKEYCODE_VOLUME_UP) {
        LOGI("Beenden via Volume-Up");
        state->running = false;
        return 1;
    }
    return 0;
}

// Pose/FoV → MVP-Matrizen für den Renderer
void computeEyeMatrices(const FrameView& view, float* viewMat, float* projMat) {
    const float nearZ = 0.05f, farZ = 100.0f;
    // Perspektive aus FOV
    const XrFovf& f = view.fov;
    const float l = nearZ * tanf(f.angleLeft), r = nearZ * tanf(f.angleRight);
    const float b = nearZ * tanf(f.angleDown), t = nearZ * tanf(f.angleUp);
    for (int i = 0; i < 16; ++i) projMat[i] = 0.0f;
    projMat[0] = 2 * nearZ / (r - l);
    projMat[5] = 2 * nearZ / (t - b);
    projMat[8] = (r + l) / (r - l);
    projMat[9] = (t + b) / (t - b);
    projMat[10] = -(farZ + nearZ) / (farZ - nearZ);
    projMat[11] = -1.0f;
    projMat[14] = -2 * farZ * nearZ / (farZ - nearZ);

    // Blickmatrix = Inverse der Augen-Pose (#15): [Rᵀ | −Rᵀ·p].
    // Spaltenmajor: viewMat[c*4 + r] = Rᵀ(r,c) = R(c,r).
    const XrQuaternionf& q = view.pose.orientation;
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float m00 = 1 - 2 * (y * y + z * z), m01 = 2 * (x * y - z * w), m02 = 2 * (x * z + y * w);
    const float m10 = 2 * (x * y + z * w), m11 = 1 - 2 * (x * x + z * z), m12 = 2 * (y * z - x * w);
    const float m20 = 2 * (x * z - y * w), m21 = 2 * (y * z + x * w), m22 = 1 - 2 * (x * x + y * y);
    const float* p = &view.pose.position.x;
    viewMat[0] = m00; viewMat[1] = m01; viewMat[2] = m02; viewMat[3] = 0;
    viewMat[4] = m10; viewMat[5] = m11; viewMat[6] = m12; viewMat[7] = 0;
    viewMat[8] = m20; viewMat[9] = m21; viewMat[10] = m22; viewMat[11] = 0;
    viewMat[12] = -(m00 * p[0] + m10 * p[1] + m20 * p[2]);
    viewMat[13] = -(m01 * p[0] + m11 * p[1] + m21 * p[2]);
    viewMat[14] = -(m02 * p[0] + m12 * p[1] + m22 * p[2]);
    viewMat[15] = 1.0f;
}

} // namespace

void android_main(android_app* app) {
    app->onAppCmd = handleAppCmd;
    app->onInputEvent = handleInput;

    // AppState auf den Heap (#9): der android_main-Thread von native_app_glue
    // hat nur den Bionic-Default-Stack (~1 MB) — Adapter/Renderer-Puffer sind
    // mehrere MB groß.
    auto statePtr = std::make_unique<AppState>();
    AppState& state = *statePtr;
    state.android = app;
    app->userData = &state;

    // Sitzungslog (session_log.h): Kopfzeile mit App-ID, Version, Gerät, Startzeit.
    state.info = readAppInfo(app);
    ::mkdir(app->activity->externalDataPath, 0770);   // nur anlegen, nie chmod (Setgid bleibt)
    const std::string logDir = std::string(app->activity->externalDataPath) + "/logs";
    LOGI("XRShell startet: %s %s (versionCode %lld), %s, %s", state.info.appId.c_str(),
         state.info.versionName.c_str(), state.info.versionCode, state.info.device.c_str(),
         state.info.android.c_str());
    state.appStart = steadyNow();
    if (slog::start(logDir, sessionHeader(state.info)))
        LOGI("Sitzungslog: %s", slog::currentPath().c_str());
    // Absturz-Marker (Systemcheck „Letzter Start“): lag er noch, endete die letzte Sitzung nicht sauber.
    state.markerPath = std::string(app->activity->internalDataPath) + "/xr-running.marker";
    if (FILE* f = std::fopen(state.markerPath.c_str(), "r")) {
        char last[512] = {};
        if (std::fgets(last, sizeof(last), f)) state.previousLog = last;
        std::fclose(f);
        state.previousCrash = true;
        LOGE("Letzter Start endete nicht sauber (Marker), Log: %s", state.previousLog.c_str());
    }
    writeMarker(state);

    while (state.running) {
        int ident;
        int events;
        android_poll_source* source;
        // ALooper_pollOnce statt pollAll (pollAll ist deprecated). Ohne laufende XR-Session
        // (Brille ab, App im Hintergrund) gibt es kein xrWaitFrame, das bremst → mit Timeout
        // warten statt mit 0 im Kreis zu laufen.
        // Die Wartezeit bei JEDEM Aufruf neu bestimmen: APP_CMD_INIT_WINDOW (in dieser Schleife
        // verarbeitet) setzt xrReady — mit einem einmal vorab berechneten -1 blieb die Schleife danach
        // im endlosen Warten auf Android-Ereignisse, die XR-Session wurde nie begonnen (Headset
        // 2026-10-08: Spiel lädt, Musik läuft, kein Bild; Log endet nach „XRShell bereit“).
        while ((ident = ALooper_pollOnce(!state.xrReady ? -1 : state.xr.sessionRunning() ? 0 : 100,
                                         nullptr, &events, reinterpret_cast<void**>(&source))) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) state.running = false;
        }

        slog::flushIfDue();   // gepuffertes Sitzungslog spätestens alle 2 s auf die Karte
        if (!state.xrReady) continue;

        state.xr.pollEvents(state.running);
        if (!state.running) break;

        FrameInput input;
        if (!state.xr.beginFrame(input)) {
            // Keine Session (Brille ab, Systemmenü/Hintergrund): Spiel anhalten, Ton aus. Während des
            // Ladens nicht (Adapter gehört dem Lade-Thread) — der nächste Frame holt es nach.
            if (!adapterBusy(state)) state.adapter.setPauseReason(LibretroAdapter::kPauseFocus, true);
            continue;
        }
        // Ohne Fokus (Systemmenü, Guardian, Brille gerade abgesetzt) hält das Spiel ebenfalls an —
        // eigener Grund neben dem VR-Menü; fortgesetzt wird, sobald der Fokus zurück ist.
        const bool busy = adapterBusy(state);   // Lade-Thread hat den Adapter (siehe AppState)
        if (!busy) state.adapter.setPauseReason(LibretroAdapter::kPauseFocus, !input.focused);

        // Menü-Bedienung per A/B: Flanken (unabhängig davon, ob das Menü offen ist).
        const bool aEdge = input.aPressed && !state.menuPrevA;
        const bool bEdge = input.bPressed && !state.menuPrevB;
        state.menuPrevA = input.aPressed;
        state.menuPrevB = input.bPressed;
        // Pflicht P1: Nach dem Schließen des Menüs zählen noch gehaltene Tasten (Trigger, A, B, …)
        // nicht als Spiel-Eingabe, bis sie einmal losgelassen wurden.
        if (state.waitButtonRelease && !state.menu.visible()) {
            const bool anyHeld = input.triggerPressed || input.aPressed || input.bPressed || input.xPressed ||
                                 input.yPressed || input.gripPressed || input.leftTriggerPressed ||
                                 input.stickClickPressed || input.leftStickClickPressed;
            if (anyHeld) {
                input.triggerPressed = input.aPressed = input.bPressed = input.xPressed = false;
                input.yPressed = input.gripPressed = input.leftTriggerPressed = false;
                input.stickClickPressed = input.leftStickClickPressed = false;
                input.triggerChanged = input.gripChanged = false;
            } else {
                state.waitButtonRelease = false;
            }
        }

        // M1a: neuen Core-Frame (falls vorhanden) in die Renderer-Textur laden.
        // Läuft auf dem Render-Thread — GLES-Aufrufe gehören hierhin.
        // #10/#11: ab dem ersten Core-Frame liest der Shader die Textur als BGRX
        // und zeigt nur den Frame-Ausschnitt; der Screen übernimmt das
        // Seitenverhältnis des Cores (Höhe bleibt, Breite folgt).
        const bool newCoreFrame = !busy && state.adapter.updateTexture(state.renderer.texture());
        if (newCoreFrame) {
            state.renderer.setCoreFrame(state.adapter.frameWidth(),
                                        state.adapter.frameHeight());
            state.screenAspect = state.adapter.aspectRatio();
        }
        // Ladeanzeige: Lade-Thread abholen, Etappen, erstes echtes Spielbild, Watchdog (steady_clock).
        const double wall = steadyNow();
        pollLoad(state, wall, newCoreFrame, input.displayTime, state.menu.visible() || !input.focused);
        if (panelVisible(state)) refreshChecks(state, wall);

        ScreenState& screen = state.screen;
        // Breite ist die Stellgröße (linke Hand), Höhe folgt dem Seitenverhältnis.
        screen.height = screen.width / state.screenAspect;

        // Zeitschritt für zeitabhängige Glättung/Verstellung (nicht pro Frame).
        float dt = 0.0f;
        if (state.lastDisplayTime != 0)
            dt = static_cast<float>(input.displayTime - state.lastDisplayTime) * 1e-9f;
        dt = std::fmin(std::fmax(dt, 0.0f), 0.1f);
        state.lastDisplayTime = input.displayTime;

        // --- Start: Leinwand zentriert vor der Blickrichtung ---
        // Sobald die Kopfpose gültig ist (nicht erst bei FOCUSED), nur Yaw.
        if (!state.centered) {
            float eyeHeight = 0.0f;
            if (state.xr.recenter(eyeHeight)) {
                placeScreen(state, eyeHeight);
                state.centered = true;
            }
        }

        // Kopf (Inhaltsraum): Bezugspunkt fürs Greifen, Drehen und den Abstand.
        const XrVector3f head = input.headValid ? input.headPos
                                                : XrVector3f{ 0.0f, screen.posY, 0.0f };

        // --- Fenster greifen: linker Griff = Fenster folgt der linken Hand ---
        // Hebel: die Handbewegung wird mit „Abstand Kopf→Bild / Abstand Kopf→Hand“
        // verstärkt — kleine Bewegungen schwenken das Bild durch den Raum. Das Bild
        // dreht sich dabei immer zum Kopf. Kommt der rechte Griff NACH dem linken
        // dazu, skaliert der Handabstand (auseinander = größer); wer rechts schon
        // hält (Spieltaste), skaliert dadurch nicht versehentlich.
        const bool leftGrip = input.focused && input.leftGripPressed;
        const bool rightGrip = input.focused && input.gripPressed;
        const bool adjust = leftGrip;   // linker Stick gehört dann dem Fenster
        // Griff „Fenster“ (xr.adventure): auch der rechte Griff greift; links hat Vorrang. Wechselt
        // die greifende Hand, setzt der Griffpunkt neu an (kein Sprung).
        const bool rightWindow = state.controls.grip == mapdrag::kGripWindow;
        const int hand = leftGrip && input.leftHandValid ? 0
                       : rightWindow && rightGrip && input.rightHandValid ? 1 : -1;
        if (hand < 0) {
            state.grabbing = false;
        } else if (!state.grabbing || hand != state.grabHand) {
            state.grabbing = true;
            state.grabHand = hand;
            state.grabLast = hand == 0 ? input.leftHandPos : input.rightHandPos;
        }
        const bool rightGripStarted = rightGrip && !state.prevRightGrip;
        const bool leftGripStarted = leftGrip && !state.prevLeftGrip;
        state.prevRightGrip = rightGrip;
        state.prevLeftGrip = leftGrip;
        const bool twoHandBefore = state.twoHand;
        // Beidhändig skalieren: zweiter Griff kommt dazu, während der erste das Fenster hält.
        if (!state.grabbing || !leftGrip || !rightGrip || !input.leftHandValid || !input.rightHandValid)
            state.twoHand = false;
        else if (rightGripStarted || (rightWindow && leftGripStarted)) state.twoHand = true;
        if (state.grabbing) {
            const XrVector3f& l = state.grabHand == 0 ? input.leftHandPos : input.rightHandPos;
            const XrVector3f screenPos = { screen.posX, screen.posY, screen.posZ };
            const float gain = std::fmin(std::fmax(
                handDistance(head, screenPos) / std::fmax(handDistance(head, l), 0.2f), 1.0f), 5.0f);
            XrVector3f delta = { l.x - state.grabLast.x, l.y - state.grabLast.y, l.z - state.grabLast.z };
            if (state.twoHand) {
                // Beide Hände ausdrücklich (l ist die greifende Hand, kann auch die rechte sein).
                const XrVector3f& hl = input.leftHandPos;
                const XrVector3f& r = input.rightHandPos;
                const XrVector3f mid = { (hl.x + r.x) * 0.5f, (hl.y + r.y) * 0.5f, (hl.z + r.z) * 0.5f };
                const float dist = std::fmax(handDistance(hl, r), 0.05f);
                if (!twoHandBefore) {
                    state.twoHandStartDist = dist;
                    state.twoHandStartWidth = screen.width;
                    state.twoHandMid = mid;
                }
                screen.width = state.twoHandStartWidth * dist / state.twoHandStartDist;
                delta = { mid.x - state.twoHandMid.x, mid.y - state.twoHandMid.y, mid.z - state.twoHandMid.z };
                state.twoHandMid = mid;
            }
            screen.posX += delta.x * gain;
            screen.posY += delta.y * gain;
            screen.posZ += delta.z * gain;
            state.grabLast = l;   // auch beidhändig: beim Loslassen rechts kein Sprung
            state.screenDistance = setHeadDistance(screen, head, headDistance(screen, head));
            faceHead(screen, head);
            screen.width = clampWidth(screen.width, state.screenDistance);
            screen.height = screen.width / state.screenAspect;
        }

        // Linker Stick bei gehaltenem Griff: links/rechts = Breite, vor/zurück =
        // Abstand; Stick drücken = auf Standard zurücksetzen und zentrieren.
        if (adjust) {
            const float sx = applyDeadzone(input.leftStickX);
            const float sy = applyDeadzone(input.leftStickY);
            if (sx != 0.0f || sy != 0.0f) {
                state.screenDistance = setHeadDistance(screen, head,
                                                       state.screenDistance + sy * 1.0f * dt);
                screen.width = clampWidth(screen.width + sx * 0.8f * dt, state.screenDistance);
                screen.height = screen.width / state.screenAspect;
            }
            if (input.leftStickClickChanged && input.leftStickClickPressed) {
                float eyeHeight = 0.0f;
                if (state.xr.recenter(eyeHeight)) {
                    placeScreen(state, eyeHeight, false);
                    LOGI("Fenster zurückgesetzt und zentriert");
                }
            }
        }
        if (state.adjusting && !adjust) {
            LOGI("Fenster: Breite %.2f m, Abstand %.2f m", screen.width, state.screenDistance);
            state.view.width = screen.width;
            state.view.distance = state.screenDistance;
            state.view.save(state.settingsPath);
            syncGameView(state, true);           // xr.dosgames: Fenster gehört zum Spiel
            state.adapter.saveUserOptions();
        }
        // xr.dosgames: eigene Fenstermaße des (neu gestarteten) Spiels übernehmen.
        if (state.applyWindowPending && state.centered) {
            state.applyWindowPending = false;
            if (state.view.distance < kMinSavedDistance) state.view.distance = kDefaultDistance;
            screen.width = clampWidth(state.view.width, state.view.distance);
            state.screenDistance = setHeadDistance(screen, head, state.view.distance);
            faceHead(screen, head);
            screen.height = screen.width / state.screenAspect;
        }
        state.adjusting = adjust;

        // --- Ansicht aus dem VR-Menü anwenden (wirkt nur bei Änderung) ---
        state.xr.setPassthrough(state.view.passthrough);
        state.renderer.setTransparentBackground(state.xr.passthroughActive());
        if (!state.view.filterChosen) state.view.filter = state.defaultFilter;   // Menü zeigt den Standard
        state.renderer.setImageSettings(static_cast<ScreenFilter>(state.view.filter),
                                        state.view.saturation, state.view.contrast);
        screen.curveRadius = state.view.curveRadius(screen.width);
        // Experimentell: Umgebung durchsichtig — Analyse läuft nur, wenn gewählt.
        state.adapter.setEnvironmentAnalysis(state.view.environment > 0);
        state.renderer.setEnvironmentKey(state.view.environment, state.adapter.environmentKey());
        float pointerRgb[3];
        state.view.pointerRgb(pointerRgb);
        // Im offenen Menü bleiben Strahl und Zielpunkt sichtbar, auch wenn sie „Aus“ sind (P3).
        const bool menuShown = state.menu.visible();
        state.renderer.setPointerStyle(pointerRgb, state.view.rayThickness(menuShown),
                                       state.view.rayMaxLength(menuShown), state.view.markerSize(menuShown));
        // Bildzoom: Ausschnitt der Kantenlänge 1/zoom um die gewählte Mitte, am
        // Bildrand geklemmt. Screen-UV u → Frame-UV zoomU0 + u · zoomSpan.
        const float zoomSpan = 1.0f / state.view.zoomFactor();
        const float zoomU0 = std::min(std::max(state.zoomCenterU - zoomSpan * 0.5f, 0.0f), 1.0f - zoomSpan);
        const float zoomV0 = std::min(std::max(state.zoomCenterV - zoomSpan * 0.5f, 0.0f), 1.0f - zoomSpan);
        state.renderer.setZoom(state.view.zoomFactor(), zoomU0, zoomV0);

        const bool aimValid =
            (input.aimFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        // Strahl in Bildschirm-Koordinaten — Overlay und Menü hängen lokal am Bild.
        const XrPosef localAim = worldToScreenLocal(screen, input.aimPose);
        const ScreenState local = localScreen(screen);

        // Hit-Test pro Frame einmal für die Logik (Issue #1) — unabhängig vom Rendering.
        RayHit cursorHit;
        if (aimValid) {
            cursorHit = raycastToScreen(screen, input.aimPose);
            screen.cursorVisible = cursorHit.hit;
            screen.cursorU = cursorHit.u;
            screen.cursorV = cursorHit.v;
        } else {
            screen.cursorVisible = false;
        }

        // Ohne Fokus (Systemmenü, Brille abgesetzt) gelten alle Tasten als los.
        const bool triggerPressed = input.focused && input.triggerPressed;
        const bool pad = !busy && state.adapter.padMode();
        const double now = static_cast<double>(input.displayTime) * 1e-9;
        // Ladeanzeige an der Stelle des Spielfensters (L2): solange sie steht, bekommt das Spiel keine
        // Eingaben, Strahl/Cursor nur fürs Menü (L8: erst mit dem ersten echten Spielbild).
        const bool panel = panelVisible(state);
        const bool gameInput = !busy && (state.phase == AppState::Phase::Shown || state.pageHidden);
        if (!gameInput) screen.cursorVisible = false;

        // Bibliothek ohne fortsetzbares Spiel: Auswahl zeigen, sobald das Bild steht.
        if (state.openLibraryPending && state.centered) {
            state.openLibraryPending = false;
            state.view.width = screen.width;
            state.view.distance = state.screenDistance;
            state.menu.open(std::fmax(headDistance(screen, head), kMinMenuDistance));
            state.menu.showGamesTab();
        }

        // Spiel hat sich selbst beendet (Doom „Quit Game“ → Ja, DOSBox-Pure „Beenden“): Bibliothek
        // zeigen statt eines stehenden Bildes; Einzelspiel-Apps beenden wie über das VR-Menü.
        if (!busy && state.adapter.takeShutdownRequest()) {
            LOGI("Spiel beendet sich selbst");
            state.adapter.saveUserOptions();
            if (kLibraryMode) {
                state.adapter.stopGame();
                refreshLibrary(state);
                state.openLibraryPending = true;
                state.phase = AppState::Phase::Idle;   // Ladeanzeige als Startseite (Spiel wählen)
                state.pageHidden = false;
            } else {
                state.view.save(state.settingsPath);
                ANativeActivity_finish(app->activity);
                state.finishRequested = true;
                state.running = false;
            }
        }

        // --- VR-Menü (linke Menütaste) — Spiel pausiert, Eingaben gehen ans Menü ---
        if (input.focused && input.menuChanged && input.menuPressed) {
            if (state.menu.visible()) {
                state.menu.close();
            } else {
                state.view.width = screen.width;          // Regler zeigen den Ist-Zustand
                // Eingestellter Abstand, nicht die momentane Kopfposition: wer sich beim Öffnen
                // vorbeugt oder herangelaufen ist, speicherte sonst „0,36 m“ (Bild vor der Nase).
                state.view.distance = state.screenDistance;
                state.menu.open(std::fmax(headDistance(screen, head), kMinMenuDistance));
            }
        }
        // Fehlerseite (L7): B = Spielbild trotzdem zeigen (Bibliothek: danach die Spieleliste).
        const bool errorPage = state.phase == AppState::Phase::Failed || state.watchdogFired;
        if (panel && errorPage && !state.menu.visible() && input.focused && bEdge) {
            state.pageHidden = true;
            state.waitButtonRelease = true;   // dieses B geht nicht als Rechtsklick ans Spiel
            LOGI("Fehlerseite: B – Spielbild trotzdem zeigen%s", busy ? " (Lade-Thread läuft noch)" : "");
            if (kLibraryMode && state.phase == AppState::Phase::Failed) state.openLibraryPending = true;
        }
        // Menü auf/zu erst übernehmen, wenn der Lade-Thread fertig ist (Pause/Eingaben gehen an den Adapter).
        if (!busy && state.menu.visible() != state.menuWasOpen) {
            state.menuWasOpen = state.menu.visible();
            state.adapter.setPaused(state.menuWasOpen);
            if (state.menuWasOpen) releaseGameInputs(state, input.displayTime);
            else {
                state.view.save(state.settingsPath);
                state.adapter.saveUserOptions();   // xr.dosgames: Werte pro Spiel (sonst ohne Wirkung)
                // relativ: Spielzeiger unter den Laser (im Abgleich fängt der Abgleich selbst; beim
                // „Zeiger lernen“ bewegt das Lernen den Zeiger — Fangen danach, falls es scheitert)
                if (kDosPage && !state.calibrating && !state.adapter.cursorLearnPending()) state.adapter.catchPointer();
                state.waitButtonRelease = true;
                if (state.hiresPending) {          // „Hohe Auflösung“: jetzt, da das Spiel weiterläuft
                    state.hiresPending = false;
                    state.hiresUntil = now + 0.15;
                }
                if (state.gameMenuPending) {       // „Spielmenü öffnen“: Kombination (Strg+F5) braucht ~0,1 s
                    state.gameMenuPending = false;
                    state.gameMenuUntil = now + 0.3;
                }
            }
            LOGI("VR-Menü %s", state.menuWasOpen ? "offen" : "geschlossen");
        }
        // Leistungszeile alle 10 s: Core-Frames/s gegen Soll-Takt, Headset-Frames/s.
        // Belegt Emulationstempo und Frame-Pacing ohne Headset-Beobachtung (Log).
        ++state.statFrames;
        if (now - state.statSince >= 10.0) {
            const uint64_t core = state.adapter.frameCount();
            // Spiel-fps (Ist/Soll), XR-Rate (Headset-Frames/s), Frame-Größe, Filter (Skill xr-port-qa).
            if (state.statSince > 0.0 && state.adapter.gameRunning())
                LOGI("Leistung: Spiel %.1f/%.1f fps, XR %.1f fps, Bild %dx%d, Filter %s%s",
                     static_cast<double>(core - state.statCoreFrames) / (now - state.statSince),
                     state.adapter.timingFps(),
                     static_cast<double>(state.statFrames) / (now - state.statSince),
                     state.renderer.frameWidth(), state.renderer.frameHeight(),
                     screenFilterKey(state.view.filter), state.menu.visible() ? ", Menü offen" : "");
            state.statSince = now;
            state.statCoreFrames = core;
            state.statFrames = 0;
        }
        // Zeigerverschiebung (Reiter Zeiger; xr.dosgames pro Spiel) — wirkt auf Laser → Spielzeiger.
        state.adapter.setPointerOffset(state.view.pointerOffsetPercentX(), state.view.pointerOffsetPercentY());
        const bool menuOpen = state.menu.visible();
        if (menuOpen) {
            screen.cursorVisible = false;
            // Spiel-Mauszeiger ausblenden kann noch kein Core (DOSBox-Pure zeichnet den Zeiger des
            // DOS-/Windows-Programms ins Bild) → Eintrag grau, Wert wird trotzdem gespeichert.
            if (!busy) {   // während des Ladens: Menü mit dem letzten Stand, ohne den Adapter zu fragen
                state.menu.setOptions(!pad, state.xr.passthroughSupported(), state.adapter.canHideGameCursor());
                state.menu.setStatesAvailable(state.adapter.statesSupported());
            state.menu.setGameMenuAvailable(!state.keyProfile.bindingText(KeyProfile::kGameMenu).empty());
            // Ordner-Apps (DOS, Shooter, Adventures): kein Himmel/Horizont auszublenden.
            state.menu.setEnvironmentAvailable(!kFolderLibrary);
            if (kAdventureLibrary) state.menu.setControlsHelp(adventureHelp(state));
                state.adapter.setGameCursorVisible(state.view.gameCursorVisible());
                state.menu.setRecording(state.recorder.active(), state.recorder.seconds(), state.adapter.gameRunning());
            }
            if (kDosPage && !busy) {
                state.menu.setDosPage(true, buildDosPage(state));
                state.menu.setControlsPage(true, state.adapter.gameRunning(), state.controls);
                state.menu.setInfo(geometryInfo(state));
            }
            const bool click = triggerPressed && input.triggerChanged;
            VrMenu::PadInput menuPad;
            if (input.focused) {
                menuPad.stickX = input.stickX;
                menuPad.stickY = input.stickY;
                menuPad.aEdge = aEdge;
                menuPad.bEdge = bEdge;
                padSources(input, false, true, menuPad.sources);
            }
            VrMenu::Action action = state.menu.update(aimValid ? &localAim : nullptr, click,
                                                      triggerPressed, menuPad, now, state.view);
            if ((click || menuPad.aEdge) && state.menu.hovered()) state.xr.vibrate(0.018f, 0.25f);
            else if (state.menu.focusMoved()) state.xr.vibrate(0.010f, 0.15f);
            if (busy) {
                // Lade-Thread läuft: nur, was den Adapter nicht braucht (Fenster, Zentrieren, Schließen,
                // Beenden). Alles andere erst nach dem Laden.
                switch (action) {
                case VrMenu::Action::None: case VrMenu::Action::Changed: case VrMenu::Action::WindowChanged:
                case VrMenu::Action::Recenter: case VrMenu::Action::Close: case VrMenu::Action::Quit:
                case VrMenu::Action::QuitArmed: case VrMenu::Action::ConfirmArmed:
                    break;
                default:
                    state.menu.setStatus("Bitte warten – Spiel lädt noch", now);
                    action = VrMenu::Action::None;
                    break;
                }
            }
            if (kGameViewPerGame && !busy) syncGameView(state, action == VrMenu::Action::WindowChanged);   // Filter, Sättigung, …
            char status[64];
            switch (action) {
            case VrMenu::Action::WindowChanged:
                screen.width = clampWidth(state.view.width, state.view.distance);
                state.screenDistance = setHeadDistance(screen, head, state.view.distance);
                faceHead(screen, head);
                screen.height = screen.width / state.screenAspect;
                state.menu.reposition(state.screenDistance);   // Tafel bleibt vor dem Bild
                break;
            case VrMenu::Action::FitSize: {
                // xr.dosgames: Breite für 53° (320er-Spiele) bzw. 66° (ab 640 Pixel) beim jetzigen Abstand.
                const int fw = state.renderer.frameWidth() > 0 ? state.renderer.frameWidth() : 320;
                state.view.width = dosopt::fitWidth(fw, state.view.distance);
                screen.width = clampWidth(state.view.width, state.view.distance);
                state.view.width = screen.width;
                state.screenDistance = setHeadDistance(screen, head, state.view.distance);
                faceHead(screen, head);
                screen.height = screen.width / state.screenAspect;
                state.menu.reposition(state.screenDistance);
                syncGameView(state, true);
                char fit[80];
                std::snprintf(fit, sizeof(fit), "Bildgröße %.1f m (%.0f° bei %d Pixel Breite)",
                              static_cast<double>(screen.width),
                              static_cast<double>(dosopt::fitAngleDegrees(fw)), fw);
                for (char* c = fit; *c; ++c) if (*c == '.') *c = ',';
                state.menu.setStatus(fit, now);
                break;
            }
            case VrMenu::Action::Recenter: {
                float eyeHeight = 0.0f;
                if (state.xr.recenter(eyeHeight)) {
                    placeScreen(state, eyeHeight, false);
                    state.view.width = screen.width;
                    state.view.distance = state.screenDistance;
                    state.menu.reposition(state.screenDistance);
                    syncGameView(state, true);
                }
                break;
            }
            case VrMenu::Action::SaveState:
                std::snprintf(status, sizeof(status), state.adapter.saveState(state.menu.stateSlot())
                              ? "Spielstand %d gespeichert" : "Speichern fehlgeschlagen",
                              state.menu.stateSlot());
                state.menu.setStatus(status, now);
                break;
            case VrMenu::Action::LoadState:
                std::snprintf(status, sizeof(status), state.adapter.loadState(state.menu.stateSlot())
                              ? "Spielstand %d geladen" : "Platz %d ist leer",
                              state.menu.stateSlot());
                state.menu.setStatus(status, now);
                break;
            case VrMenu::Action::SaveProfile:
                std::snprintf(status, sizeof(status),
                              state.view.save(profilePath(state, state.menu.profileSlot()))
                              ? "Profil %d gespeichert" : "Profil %d: Fehler",
                              state.menu.profileSlot());
                state.menu.setStatus(status, now);
                break;
            case VrMenu::Action::StartModeChanged:
                state.view.save(state.settingsPath);   // sofort: wirkt beim nächsten App-Start
                state.menu.setStatus(state.view.startMode == ViewSettings::kStartResume
                                         ? "Beim Start: letztes Spiel fortsetzen"
                                         : "Beim Start: Menü mit Spieleliste", now);
                LOGI("Beim Start: %s", state.view.startMode == ViewSettings::kStartResume ? "letztes Spiel" : "Menü");
                break;
            case VrMenu::Action::LoadProfile: {
                ViewSettings loaded;
                if (loaded.load(profilePath(state, state.menu.profileSlot()))) {
                    loaded.startMode = state.view.startMode;   // App-Verhalten, kein Profilwert
                    state.view = loaded;
                    if (state.view.width > 0.0f && state.view.distance > 0.0f) {
                        screen.width = clampWidth(state.view.width, state.view.distance);
                        state.screenDistance = setHeadDistance(screen, head, state.view.distance);
                        faceHead(screen, head);
                        screen.height = screen.width / state.screenAspect;
                        state.menu.reposition(state.screenDistance);
                    }
                    syncGameView(state, true);   // xr.dosgames: geladene Bildwerte gelten für dieses Spiel
                    std::snprintf(status, sizeof(status), "Profil %d geladen", state.menu.profileSlot());
                } else {
                    std::snprintf(status, sizeof(status), "Profil %d ist leer", state.menu.profileSlot());
                }
                state.menu.setStatus(status, now);
                break;
            }
            case VrMenu::Action::ToggleKeyboard:
                state.overlay.toggle();
                LOGI("Tastatur-Overlay: %s", state.overlay.visible() ? "an" : "aus");
                break;
            case VrMenu::Action::StartGame: {
                const int index = state.menu.selectedGame();
                if (index >= 0 && index < static_cast<int>(state.library.size())) {
                    const LibretroAdapter::GameEntry game = state.library[index];
                    if (kFolderLibrary && game.path == state.adapter.gamePath()) {
                        // xr.dosgames/xr.shooter: laufendes Spiel gewählt → weiterspielen statt neu starten.
                        state.menu.close();
                    } else {
                        // Laden im Hintergrund; die Ladeanzeige (bzw. Fehlerseite) steht an der Bildstelle.
                        const bool loading = beginLoad(state, game.path, steadyNow());
                        state.menu.close();
                        LOGI("Bibliothek: %s %s", game.title.c_str(), loading ? "wird geladen" : "– Fehlerseite");
                    }
                }
                break;
            }
            case VrMenu::Action::ToggleRecording:
                state.menu.setStatus(state.recorder.active() ? stopRecording(state) : startRecording(state), now);
                break;
            case VrMenu::Action::PadChanged: {
                // Belegung gilt sofort und wird für dieses Spiel gespeichert.
                state.padMap = state.menu.padMapping();
                applyPadDevice(state);
                if (!padmap::saveFile(padmap::gameFile(state.adapter.gamePath()), state.padMap))
                    state.menu.setStatus("Belegung nicht speicherbar", now);
                break;
            }
            case VrMenu::Action::PadSaveSystem: {
                // Systemstandard schreiben; das Spiel folgt ihm (eigene Datei entfällt).
                const std::string data = state.android->activity->externalDataPath;
                const std::string& folder = state.adapter.systemFolder();
                ::mkdir((data + "/games").c_str(), 0775);
                ::mkdir((data + "/games/" + (folder.empty() ? std::string("other") : folder)).c_str(), 0775);
                state.padMap = state.menu.padMapping();
                const bool ok = padmap::saveFile(padmap::systemFile(data, folder), state.padMap);
                if (ok) std::remove(padmap::gameFile(state.adapter.gamePath()).c_str());
                state.menu.setStatus(ok ? std::string("Standard für alle ") + padmap::systemFor(folder).label +
                                              "-Spiele gespeichert"
                                        : std::string("Standard nicht speicherbar"), now);
                break;
            }
            case VrMenu::Action::PadReset: {
                // Erst die Spielbelegung verwerfen; gab es keine, auch den Systemstandard.
                const std::string data = state.android->activity->externalDataPath;
                const std::string& folder = state.adapter.systemFolder();
                const bool hadGame = std::remove(padmap::gameFile(state.adapter.gamePath()).c_str()) == 0;
                if (!hadGame) std::remove(padmap::systemFile(data, folder).c_str());
                loadPadMapping(state);
                state.menu.setStatus(hadGame ? std::string("Zurück auf ") + padmap::systemFor(folder).label + "-Standard"
                                             : std::string("Eingebaute Belegung wiederhergestellt"), now);
                break;
            }
            case VrMenu::Action::QuitArmed:
            case VrMenu::Action::ConfirmArmed:
                state.xr.vibrate(0.03f, 0.6f);   // zweiter, kräftigerer Puls nach dem Klick-Puls
                break;
            case VrMenu::Action::DosOption: {
                // Reiter DOS: Kern-Option um eine Stufe weiter (Werte aus der Liste des Cores).
                const auto id = static_cast<dosopt::Id>(state.menu.dosOption());
                const dosopt::Spec& spec = dosopt::spec(id);
                optstore::Definition def;
                const bool defined = state.adapter.coreOptionDefinition(spec.key, def);
                std::string cur;
                if (!state.adapter.effectiveOption(spec.key, cur)) cur = defined ? def.defaultValue : spec.defaultValue;
                std::vector<std::string> announced;
                for (const auto& v : def.values) announced.push_back(v.first);
                const std::string next = dosopt::stepValue(dosopt::choices(id, announced, cur), cur,
                                                           state.menu.dosStep());
                state.adapter.setUserOption(spec.key, next, spec.live);
                LOGI("DOS-Option %s = %s (%s)", spec.key, next.c_str(), spec.live ? "sofort" : "nach Neustart");
                if (!spec.live) state.menu.setStatus("Wirkt nach Neustart (DOS → Spiel neu starten)", now);
                break;
            }
            case VrMenu::Action::DosGain: {
                const char* key = state.menu.dosOption() == 0 ? mouserel::kGainXKey : mouserel::kGainYKey;
                const float g = mouserel::stepGain(mouseGain(state, key), state.menu.dosStep());
                state.adapter.setUserOption(key, mouserel::formatGain(g), true);
                applyMouseGain(state);
                break;
            }
            case VrMenu::Action::ControlOption: {
                // Reiter Steuerung: Griff/Zieh-Art/Richtung/Tempo/Stick, sofort und pro Spiel.
                const mapdrag::Settings next = mapdrag::stepRow(
                    state.controls, static_cast<mapdrag::Row>(state.menu.controlRow()), state.menu.dosStep());
                for (const auto& kv : mapdrag::changes(state.controls, next)) {
                    state.adapter.setUserOption(kv.first, kv.second, true);
                    LOGI("Steuerung: %s = %s", kv.first.c_str(), kv.second.c_str());
                }
                state.controls = next;
                break;
            }
            case VrMenu::Action::DosCalibrate:
                // Menü zu → Zeiger wird gefangen; danach Laser auf den Spielzeiger, Trigger.
                state.calibrating = true;
                state.calibrateCatch = state.adapter.catchCount();
                state.calibrateUntil = now + 20.0;
                state.adapter.catchPointer(0.3f, 0.3f);   // fester Punkt, dort einfrieren
                state.adapter.setPointerFrozen(true);
                state.menu.close();
                LOGI("Maus abgleichen: nach dem Fangen Laser auf den Spielzeiger, Trigger");
                break;
            case VrMenu::Action::DosLearnPointer: {
                // Kopplung braucht relative Bewegung (Mickeys) — „direct“ setzt nur die Position.
                std::string mode;
                if (!state.adapter.activeOption("dosbox_pure_mouse_input", mode) ||
                    (mode != "virtual" && mode != "true")) {
                    state.adapter.setUserOption("dosbox_pure_mouse_input", "virtual", true);
                    LOGI("Zeiger lernen: Mausmodus auf Relativ");
                }
                state.calibrating = false;
                state.adapter.setPointerFrozen(false);
                state.adapter.requestCursorLearn();
                state.menu.close();
                LOGI("Zeiger lernen angestoßen");
                break;
            }
            case VrMenu::Action::GameMenu:
                // Wie „Hohe Auflösung“: erst senden, wenn das Spiel wieder läuft.
                state.gameMenuPending = true;
                state.menu.close();
                LOGI("Spielmenü: sende %s", state.keyProfile.bindingText(KeyProfile::kGameMenu).c_str());
                break;
            case VrMenu::Action::DosHires:
                // Taste geht erst, wenn das Spiel wieder läuft (pausiert sieht der Core nichts).
                state.hiresPending = true;
                state.menu.close();
                LOGI("Hohe Auflösung: sende %s", state.keyProfile.bindingText(KeyProfile::kHires).c_str());
                break;
            case VrMenu::Action::RestartGame: {
                const std::string path = state.adapter.gamePath();   // Kopie: start() leert gamePath
                if (!path.empty()) {
                    beginLoad(state, path, steadyNow());   // Ladeanzeige wie beim Spielwechsel
                    state.menu.close();
                    LOGI("Spiel neu gestartet: %s", path.c_str());
                } else {
                    state.menu.setStatus("Neustart fehlgeschlagen", now);
                }
                break;
            }
            case VrMenu::Action::ResetGameOptions: {
                state.adapter.resetUserOptions(dosopt::isLiveKey);
                state.adapter.saveUserOptions();   // Nutzerdatei löschen
                applyGameView(state);
                applyMouseGain(state);
                applyControls(state);
                const DosPage page = buildDosPage(state);
                state.menu.setStatus(page.restartPending ? "Spiel-Standard – Neustart nötig"
                                                         : "Spiel-Standard wiederhergestellt", now);
                break;
            }
            case VrMenu::Action::Quit:
                LOGI("Beenden über das VR-Menü");
                state.view.save(state.settingsPath);
                if (!busy) state.adapter.saveUserOptions();   // beim Laden gehört der Adapter dem Lade-Thread
                ANativeActivity_finish(app->activity);
                state.finishRequested = true;
                state.running = false;
                break;
            case VrMenu::Action::Close:
                state.menu.close();
                break;
            default:
                break;
            }
        }

        // Quest-Bildwiederholrate passend zum Spiel (72 fps → 72 Hz); ohne Spiel Standardrate.
        state.xr.matchRefreshRate(state.adapter.gameRunning() ? state.adapter.timingFps() : 0.0);
        state.adapter.setDisplayRefreshRate(state.xr.displayRefreshRate());

        // #16: Pad-Core — Gamepad statt Maus, kein Laser/Cursor, kein Overlay.
        if (pad) {
            screen.cursorVisible = false;
            if (!menuOpen && gameInput) updateGamepad(state, input, adjust, !state.twoHand);
            // libretro-Rumble → rechter Controller. Kurze Impulse, pro Frame erneuert, solange der
            // Core Rumble meldet; bei offenem Menü (Spiel pausiert) und vor dem ersten Bild still.
            const float rumble = menuOpen || !input.focused || !gameInput ? 0.0f : state.adapter.rumbleAmplitude();
            if (rumble > 0.02f) state.xr.vibrate(0.03f, rumble < 1.0f ? rumble : 1.0f);
        }

        if (!pad && !menuOpen && gameInput) {
            // --- #5: Tastatur-Overlay — Tastendruck per Raycast/Trigger ---
            int overlayKey = -1;
            bool overlayCaptures = false;
            if (state.overlay.visible() && aimValid) {
                overlayKey = state.overlay.keyAt(localAim, local);
                // Ein Ziehen, das auf dem Screen begann, drückt keine Overlay-Tasten.
                overlayCaptures = overlayKey >= 0 && !state.leftButton.active;
            }
            if (overlayCaptures) {
                // Tastenwechsel während Halten: alte Taste loslassen, neue drücken
                if (state.heldOverlayKey != overlayKey) {
                    if (state.heldOverlayKey >= 0)
                        state.adapter.setKey(
                            state.overlay.retroKey(state.heldOverlayKey), false);
                    if (triggerPressed) {
                        state.adapter.setKey(state.overlay.retroKey(overlayKey), true);
                        state.heldOverlayKey = overlayKey;
                        state.xr.vibrate(0.018f, 0.25f);
                    } else {
                        state.heldOverlayKey = -1;
                    }
                }
                state.overlay.setPressedKey(triggerPressed ? overlayKey : -1);
            } else {
                if (state.heldOverlayKey >= 0) {
                    state.adapter.setKey(state.overlay.retroKey(state.heldOverlayKey), false);
                    state.heldOverlayKey = -1;
                }
                state.overlay.setPressedKey(-1);
            }

            // --- M1c: Controller → libretro-Input (nach cursorHit-Berechnung) ---
            // Trigger = links, Griff rechts oder B = rechts. Ein Klick muss auf dem
            // Screen beginnen; gehalten (Ziehen) darf der Strahl ihn verlassen.
            // Overlay fängt ab, solange der Strahl auf einer Taste liegt.
            const bool onScreen = cursorHit.hit && aimValid && !overlayCaptures;
            const bool canHold = input.focused && aimValid;
            // Maus abgleichen (xr.dosgames): der erste Trigger nach dem Fangen zeigt, wo der Spielzeiger
            // wirklich ist → Faktor pro Achse = Faktor · Fangposition / Zeigerposition. Der Klick
            // geht nicht ans Spiel (Taste erst nach dem Loslassen wieder scharf).
            // „Zeiger lernen“ fertig: Kopplung pro Spiel speichern, Rückmeldung per Haptik/Status.
            if (kDosPage) {
                bool ok = false;
                std::string enc, msg;
                if (state.adapter.takeCursorLearnResult(ok, enc, msg)) {
                    if (ok) {
                        state.adapter.setUserOption(curlock::kOptionKey, enc, true);
                        state.adapter.saveUserOptions();
                    } else {
                        state.adapter.catchPointer();   // beim Schließen des Menüs ausgelassen
                    }
                    state.menu.setStatus(ok ? "Zeiger gekoppelt" : "Zeiger lernen fehlgeschlagen: " + msg, now);
                    state.xr.vibrate(0.03f, ok ? 0.6f : 0.3f);
                }
            }
            if (state.calibrating && now > state.calibrateUntil) {
                state.calibrating = false;
                state.adapter.setPointerFrozen(false);
                state.adapter.catchPointer();
                LOGI("Maus abgleichen: abgelaufen");
            } else if (state.calibrating && state.adapter.catchCount() != state.calibrateCatch &&
                       triggerPressed && input.triggerChanged && onScreen) {
                float u0 = 0.0f, v0 = 0.0f;
                state.adapter.lastCatchUv(u0, v0);
                const float au = frameUvFromScreen(cursorHit.u, zoomU0, zoomSpan);
                const float av = frameUvFromScreen(cursorHit.v, zoomV0, zoomSpan);
                const float gx = mouseGain(state, mouserel::kGainXKey), gy = mouseGain(state, mouserel::kGainYKey);
                const float nx = mouserel::calibratedGain(gx, u0, au), ny = mouserel::calibratedGain(gy, v0, av);
                state.adapter.setUserOption(mouserel::kGainXKey, mouserel::formatGain(nx), true);
                state.adapter.setUserOption(mouserel::kGainYKey, mouserel::formatGain(ny), true);
                state.adapter.saveUserOptions();
                applyMouseGain(state);
                state.adapter.setPointerFrozen(false);
                state.adapter.catchPointer();
                state.calibrating = false;
                state.leftButton = ButtonGate{false, false};
                state.xr.vibrate(0.03f, 0.6f);
                LOGI("Maus abgeglichen: gefangen %.2f/%.2f, Zeiger %.2f/%.2f → Faktor %.2f/%.2f (vorher %.2f/%.2f)",
                     static_cast<double>(u0), static_cast<double>(v0), static_cast<double>(au), static_cast<double>(av),
                     static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(gx), static_cast<double>(gy));
            }
            // --- Karte ziehen (rechter Griff, xr.dosgames): Handweg ab dem Greifpunkt → Scroll-Tasten.
            // Beginnt nur ohne gehaltene Maustaste und nicht, wenn der Griff gerade das Fenster
            // skaliert (linker Griff zuerst). Während des Ziehens keine Klicks, Zeiger steht.
            const bool gripHeld = input.focused && input.gripPressed;
            const bool dragMode = state.controls.grip == mapdrag::kGripDrag;
            if (!gripHeld) state.dragArmed = true;
            const bool wasDragging = state.dragging;
            if (state.dragging && (!gripHeld || !dragMode || state.twoHand || !input.rightHandValid)) {
                state.dragging = false;
                if (gripHeld) state.dragArmed = false;   // abgebrochen: erst loslassen
            } else if (!state.dragging && gripHeld && state.dragArmed) {
                if (dragMode && !state.twoHand && input.rightHandValid && !state.calibrating &&
                    !state.leftButton.active && !state.rightButton.active) {
                    state.dragging = true;
                    state.dragOrigin = input.rightHandPos;
                    state.dragU = state.pointerU;
                    state.dragV = state.pointerV;
                    // Hand folgt: Hebel wie beim Fenster greifen (Kopf→Bild / Kopf→Hand), auf Spielpixel.
                    const XrVector3f screenPos = { screen.posX, screen.posY, screen.posZ };
                    const float lever = std::fmin(std::fmax(handDistance(head, screenPos) /
                                                            std::fmax(handDistance(head, input.rightHandPos), 0.2f),
                                                            1.0f), 5.0f);
                    const int fw = std::max(1, state.renderer.frameWidth());
                    const int fh = std::max(1, state.renderer.frameHeight());
                    state.dragPxPerM[0] = lever * static_cast<float>(fw) / std::fmax(screen.width, 0.1f);
                    state.dragPxPerM[1] = lever * static_cast<float>(fh) / std::fmax(screen.height, 0.1f);
                    // Stick-Pfeiltasten jetzt lösen (gleiche Tasten wie das Ziehen).
                    const retro_key arrowKey[4] = { RETROK_LEFT, RETROK_RIGHT, RETROK_UP, RETROK_DOWN };
                    for (int i = 0; i < 4; ++i) {
                        if (state.arrowHeld[i]) state.adapter.setKey(arrowKey[i], false);
                        state.arrowHeld[i] = false;
                    }
                    LOGI("Karte ziehen: gegriffen (%s, Hebel %.1f)",
                         state.controls.model == mapdrag::kFollow ? "Hand folgt" : "Joystick",
                         static_cast<double>(lever));
                }
                state.dragArmed = state.dragging;   // nicht nachträglich mitten im Halten beginnen
            }
            if (state.dragging != wasDragging) {
                state.xr.vibrate(state.dragging ? 0.02f : 0.015f, state.dragging ? 0.35f : 0.2f);
                if (!state.dragging) LOGI("Karte ziehen: losgelassen");
            }
            {
                float offX = 0.0f, offY = 0.0f;
                if (state.dragging) {
                    // Handweg auf die Bildebene: rechts = Bildachse x (Drehung um die Hochachse), oben = y.
                    const XrVector3f& h = input.rightHandPos;
                    const float dx = h.x - state.dragOrigin.x, dy = h.y - state.dragOrigin.y;
                    const float dz = h.z - state.dragOrigin.z;
                    offX = dx * std::cos(screen.yaw) - dz * std::sin(screen.yaw);
                    offY = dy;
                    if (state.controls.model == mapdrag::kFollow) {
                        offX *= state.dragPxPerM[0];
                        offY *= state.dragPxPerM[1];
                    }
                }
                bool keys[mapdrag::kDirs];
                state.drag.update(state.dragging, offX, offY, dt, state.controls, keys);
                bool newPress = false;
                for (int i = 0; i < mapdrag::kDirs; ++i) {
                    newPress |= keys[i] && !state.dragKeys[i];
                    state.dragKeys[i] = keys[i];
                }
                if (newPress && state.dragging) state.xr.vibrate(0.006f, 0.1f);   // leiser Takt je Tastendruck
            }

            const bool wasLeft = state.leftButton.active, wasRight = state.rightButton.active;
            const bool mouseFree = !state.dragging;
            const bool left = state.leftButton.update(triggerPressed, onScreen && mouseFree, canHold && mouseFree);
            const bool gripRight = state.controls.grip == mapdrag::kGripRightClick && input.gripPressed &&
                                   !state.twoHand;
            const bool right = state.rightButton.update(input.focused && (gripRight || input.bPressed),
                                                        onScreen && mouseFree, canHold && mouseFree);

            // Zeiger zeitabhängig glätten (τ 25 ms) statt pro Frame.
            if (onScreen) {
                if (!state.pointerFiltered) {
                    state.pointerU = cursorHit.u;
                    state.pointerV = cursorHit.v;
                    state.pointerFiltered = true;
                } else {
                    const float k = 1.0f - std::exp(-dt / kPointerTau);
                    state.pointerU += (cursorHit.u - state.pointerU) * k;
                    state.pointerV += (cursorHit.v - state.pointerV) * k;
                }
            } else if (!left && !right) {
                state.pointerFiltered = false;   // beim Wiedereintritt nicht nachziehen
            }

            // Klickstabilisierung: Zielpunkt ~85 ms nach dem Druck einfrieren, damit
            // der Druck selbst den Controller nicht verreißt; danach Ziehen erlauben.
            // Haptik bestätigt die Eingabe (nicht den Erfolg im Spiel).
            if ((left && !wasLeft) || (right && !wasRight)) {
                state.freezeUntil = input.displayTime +
                                    static_cast<XrTime>(kClickFreeze * 1e9f);
                state.freezeU = state.pointerU;
                state.freezeV = state.pointerV;
                state.xr.vibrate(0.018f, 0.25f);
            }
            const bool frozen = input.displayTime < state.freezeUntil;
            const float screenU = state.dragging ? state.dragU : frozen ? state.freezeU : state.pointerU;
            const float screenV = state.dragging ? state.dragV : frozen ? state.freezeV : state.pointerV;
            state.adapter.setPointerUV(frameUvFromScreen(screenU, zoomU0, zoomSpan),
                                       frameUvFromScreen(screenV, zoomV0, zoomSpan), state.pointerFiltered);

            // X = Bildzoom 1× → 1,5× → 2× → 3× → 1×, um die Stelle unter dem Strahl
            // (nur ohne X-Belegung im Tastenprofil).
            const bool xDown = input.focused && input.xPressed;
            if (xDown && !state.prevX && !state.keyProfile.bound(KeyProfile::kX)) {
                if (onScreen) {
                    state.zoomCenterU = zoomU0 + cursorHit.u * zoomSpan;
                    state.zoomCenterV = zoomV0 + cursorHit.v * zoomSpan;
                }
                state.view.zoom = (state.view.zoom + 1) % ViewSettings::kZooms;
                if (state.view.zoom == 0) state.zoomCenterU = state.zoomCenterV = 0.5f;
                LOGI("Bildzoom %.1f×", static_cast<double>(state.view.zoomFactor()));
            }
            state.prevX = xDown;

            // Y = Bildschirmtastatur an/aus (Namensfelder, Speichernamen), nur ohne
            // Y-Belegung im Tastenprofil. DOS meldet keine Textfelder, daher kein
            // automatisches Öffnen.
            const bool yDown = input.focused && input.yPressed;
            if (yDown && !state.prevY && !state.keyProfile.bound(KeyProfile::kY)) {
                state.overlay.toggle();
                state.xr.vibrate(0.018f, 0.25f);
                LOGI("Tastatur-Overlay per Y: %s", state.overlay.visible() ? "an" : "aus");
            }
            state.prevY = yDown;
            state.adapter.setMouseButtons(left, right);

            // Rechter Stick: libretro zählt Y nach unten, OpenXR nach oben.
            const float stickX = input.focused ? input.stickX : 0.0f;
            const float stickY = input.focused ? input.stickY : 0.0f;
            state.adapter.setStick(stickX, -stickY);

            // Rechter Stick zusätzlich als Pfeiltasten (DOS-Spiele scrollen damit,
            // z. B. die Parkkarte in Theme Park). Hysterese gegen Flattern. Ruht, solange die Karte
            // gezogen wird bzw. deren Tasten noch unten sind (gleiche Tasten); pro Spiel abschaltbar.
            bool scrollBusy = state.dragging;
            for (int i = 0; i < mapdrag::kDirs; ++i)
                scrollBusy |= state.keyProfile.holding(static_cast<KeyProfile::Input>(KeyProfile::kScrollLeft + i));
            const bool stickArrows = state.controls.stickArrows && !scrollBusy;
            const float arrowAxis[4] = { stickArrows ? -stickX : 0.0f, stickArrows ? stickX : 0.0f,
                                         stickArrows ? stickY : 0.0f, stickArrows ? -stickY : 0.0f };
            const retro_key arrowKey[4] = { RETROK_LEFT, RETROK_RIGHT, RETROK_UP, RETROK_DOWN };
            for (int i = 0; i < 4; ++i) {
                const bool held = state.arrowHeld[i] ? arrowAxis[i] > kArrowRelease
                                                     : arrowAxis[i] > kArrowPress;
                if (held != state.arrowHeld[i]) {
                    state.adapter.setKey(arrowKey[i], held);
                    state.arrowHeld[i] = held;
                }
            }

            // --- Tastenprofil (xrshell.keys): A/X/Y, Trigger links, rechten Stick
            // drücken, linker Stick. Der linke Stick gehört nur ohne linken Griff
            // dem Spiel — mit Griff verstellt er das Fenster. Scroll-Tasten (Karte ziehen)
            // laufen immer hier durch (Standard Pfeiltasten, Mindest-Haltezeit 50 ms).
            {
                const bool f = input.focused;
                const bool stickFree = f && !adjust;
                const float lx = stickFree ? input.leftStickX : 0.0f;
                const float ly = stickFree ? input.leftStickY : 0.0f;
                const float leftAxis[4] = { -lx, lx, ly, -ly };
                for (int i = 0; i < 4; ++i)
                    state.leftStickHeld[i] = state.leftStickHeld[i] ? leftAxis[i] > kArrowRelease
                                                                    : leftAxis[i] > kArrowPress;
                bool pressed[KeyProfile::kInputCount] = {};
                pressed[KeyProfile::kA] = f && input.aPressed;
                pressed[KeyProfile::kX] = f && input.xPressed;
                pressed[KeyProfile::kY] = f && input.yPressed;
                pressed[KeyProfile::kLeftTrigger] = f && input.leftTriggerPressed;
                pressed[KeyProfile::kRightStickClick] = f && input.stickClickPressed;
                pressed[KeyProfile::kLeftStickLeft] = state.leftStickHeld[0];
                pressed[KeyProfile::kLeftStickRight] = state.leftStickHeld[1];
                pressed[KeyProfile::kLeftStickUp] = state.leftStickHeld[2];
                pressed[KeyProfile::kLeftStickDown] = state.leftStickHeld[3];
                // Menü-Aktion „Hohe Auflösung“ (xr.dosgames): Profil-Eingabe „hires“ kurz halten.
                pressed[KeyProfile::kHires] = now < state.hiresUntil;
                pressed[KeyProfile::kGameMenu] = now < state.gameMenuUntil;
                pressed[KeyProfile::kScrollLeft] = state.dragKeys[mapdrag::kLeft];
                pressed[KeyProfile::kScrollRight] = state.dragKeys[mapdrag::kRight];
                pressed[KeyProfile::kScrollUp] = state.dragKeys[mapdrag::kUp];
                pressed[KeyProfile::kScrollDown] = state.dragKeys[mapdrag::kDown];
                state.keyProfile.update(pressed, static_cast<double>(input.displayTime) * 1e-9,
                                        state.adapter);
            }
        } // !pad && !menuOpen

        // Laser, wenn der Controller zeigt: DOS-Maus oder offenes Menü (nicht beim
        // Gamepad). Er endet am Treffer (Menütafel, sonst Bildschirm).
        // Strahl erst mit dem ersten echten Spielbild (L8) — vorher nur fürs Menü.
        const bool showLaser = aimValid && ((!pad && gameInput) || menuOpen);
        float laserLength = 4.0f;
        if (menuOpen && state.menu.hovered()) laserLength = state.menu.hitDistance();
        else if (cursorHit.hit) laserLength = cursorHit.distance;
        // Bildschirm als Compositor-Ebene (schärfer): gefiltertes Spielbild mit
        // ganzzahliger Vergrößerung in die Ebenen-Swapchain, Pose wie die Geometrie
        // von Raycast/Cursor (flach: Bildmitte; gewölbt: Zylinderachse vor der Mitte).
        const bool screenLayer = state.xr.screenLayerAvailable();
        // Ladeanzeige steht an der Bildstelle: Spielbild (Ebene bzw. Netz) solange weglassen.
        state.renderer.setScreenInLayer(screenLayer || panel);
        if (screenLayer && !panel && state.xr.beginScreenLayer()) {
            const int fw = std::max(1, state.renderer.frameWidth());
            const int fh = std::max(1, state.renderer.frameHeight());
            const int scale = std::max(1, std::min(state.xr.screenLayerWidth() / fw,
                                                   state.xr.screenLayerHeight() / fh));
            XrApp::ScreenLayer layer;
            layer.rectW = std::min(fw * scale, state.xr.screenLayerWidth());
            layer.rectH = std::min(fh * scale, state.xr.screenLayerHeight());
            glViewport(0, 0, layer.rectW, layer.rectH);
            state.renderer.drawScreenLayer();
            layer.width = screen.width;
            layer.height = screen.height;
            layer.radius = screen.curveRadius;
            const float s = std::sin(screen.yaw), c = std::cos(screen.yaw);
            layer.pose.orientation = { 0.0f, std::sin(screen.yaw * 0.5f), 0.0f,
                                       std::cos(screen.yaw * 0.5f) };
            layer.pose.position = { screen.posX + s * screen.curveRadius, screen.posY,
                                    screen.posZ + c * screen.curveRadius };
            state.xr.endScreenLayer(layer);
        }
        // Videoaufnahme: jedes neue Spielbild gefiltert in die Encoder-Oberfläche.
        if (state.recorder.active() && newCoreFrame && state.recorder.beginFrame()) {
            state.renderer.drawScreenLayer();
            state.recorder.endFrame();
        }
        // Roter Aufnahme-Punkt am Bildrand (blinkt im Sekundentakt), nicht im Menü.
        state.renderer.setRecordingDot(state.recorder.active() && !menuOpen &&
                                       (state.recorder.seconds() % 2 == 0));

        if (panel) {
            state.loadScreen.update(buildLoadView(state, wall), wall);
            if (!state.panelLogged) {
                state.panelLogged = true;
                LOGI("Ladeanzeige sichtbar nach %s (seit App-Start)", loadst::seconds(wall - state.appStart).c_str());
            }
        }
        float screenModel[16];
        screenModelMatrix(screen, screenModel);
        for (int eye = 0; eye < 2; ++eye) {
            FrameView view = state.xr.acquireView(eye);
            float viewMat[16], projMat[16];
            computeEyeMatrices(view, viewMat, projMat);
            float localView[16];   // Overlay und Menü hängen lokal am Bildschirm
            mat4Multiply(localView, viewMat, screenModel);

            const XrPosef* aim = showLaser ? &input.aimPose : nullptr;
            state.renderer.draw(viewMat, projMat, screen, aim, showLaser, laserLength);
            if (panel) state.loadScreen.draw(localView, projMat, screen.width, screen.height, wall);
            else if (!pad) state.overlay.draw(localView, projMat, local);   // #5 (Pad-Modus: keine Tastatur)
            state.menu.draw(localView, projMat);

            state.xr.releaseView(eye);
        }
        state.xr.endFrame();
    }

    LOGI("XRShell beendet — Shutdown");
    // Lade-Thread: kurz auf sein Ende warten. Hängt retro_load_game, bleibt er stehen — dann den
    // Adapter nicht anfassen und den Prozess am Ende hart beenden (der Thread nutzt AppState).
    bool loaderHung = false;
    if (state.loadThread.joinable()) {
        for (int i = 0; i < 100 && !state.loadDone.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (state.loadDone.load()) {
            state.loadThread.join();
        } else {
            loaderHung = true;
            state.loadThread.detach();
            LOGE("Lade-Thread hängt (%s) – Prozess wird nach dem Aufräumen beendet", state.loadTitle.c_str());
        }
    }
    if (!loaderHung) stopRecording(state);   // Video sauber abschließen (Ton-Abgriff zuerst lösen)
    if (!loaderHung) state.adapter.shutdown();   // M1: Core-Thread stoppen, bevor GLES abgebaut wird
    state.loadScreen.shutdown();
    removeMarker(state);        // sauber beendet
    state.overlay.shutdown();   // #5
    state.menu.shutdown();
    state.view.save(state.settingsPath);
    state.renderer.shutdown();
    state.xr.shutdown();
    egl_destroy(state.egl);
    LOGI("XRShell: Sitzung beendet");
    slog::flush();

    // Bis Android das Ende bestätigt (APP_CMD_DESTROY), Ereignisse weiter abholen. Wer android_main
    // vorher verlässt, lässt Eingaben liegen („Input dispatching timed out“ → ANR nach 5 s) und
    // onDestroy wartet bis zum destroyTimeout (Android tötet den Prozess nach 10 s) — so geschehen bei
    // jedem „Beenden“ im VR-Menü. Endete die Schleife ohne finish (Volume-Up, OpenXR-Instanz
    // verloren), jetzt beenden.
    if (!state.finishRequested && !app->destroyRequested) ANativeActivity_finish(app->activity);
    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(250, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0 && source)
            source->process(app, source);
    }
    LOGI("XRShell: von Android beendet");
    if (loaderHung) {
        slog::flush();
        _exit(0);   // der hängende Lade-Thread darf AppState nicht nach dessen Abbau benutzen
    }
}
