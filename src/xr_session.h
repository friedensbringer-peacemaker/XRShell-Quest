// XRShell — OpenXR-Kern: Instance, Session, Swapchains, Actions, Frame-Loop.
//
// M0-Umfang:
//   · Instance + System (Android-Context übergeben)
//   · Session mit OpenGL-ES-Android-Grafikbindung
//   · Stereo-Swapchains (ein je Auge), STAGE-Referenzraum
//   · Actions: Aim-Pose + Select + Grip (rechte Hand), B + Thumbstick (rechte
//     Hand), Menu + Grip + Thumbstick/Klick (linke Hand), Haptik (rechts)
//   · Frame-Loop liefert pro Auge: View/Pose + Projektion, Controller-Ray + Trigger
//   · Inhaltsraum: STAGE, per recenter() auf Kopfposition + Blick-Yaw verschoben.
//     Alles (Screen, Overlay, Aim, Views, Layer) lebt in diesem Raum, daher
//     bleiben Renderer und Raycast achsenparallel.

#pragma once

#include "android_glue.h"

#include <GLES3/gl3.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <string>
#include <vector>

struct FrameInput {
    bool     sessionReady   = false;
    bool     screenVisible  = false;      // false → nur Leerraum rendern
    XrSpaceLocationFlags aimFlags = 0;
    XrPosef  aimPose        = {};         // Controller-Spitze (STAGE-Raum)
    bool     triggerPressed = false;
    bool     triggerChanged = false;      // Flanke — für Klicks
    bool     gripPressed    = false;      // Grip/Squeeze rechts — Maus-Rechts
    bool     gripChanged    = false;
    bool     bPressed       = false;      // B-Taste (rechte Hand) — Maus-Rechts
    float    stickX = 0.0f, stickY = 0.0f; // rechter Thumbstick -1..1 (+Y = vorn)
    bool     menuPressed    = false;      // linke Menu-Taste — Overlay-Toggle
    bool     menuChanged    = false;
    bool     leftGripPressed = false;     // Griff links — Freigabe Fensterverstellung
    float    leftStickX = 0.0f, leftStickY = 0.0f;   // linker Thumbstick -1..1
    bool     leftStickClickPressed = false;          // linken Stick drücken — Reset
    bool     leftStickClickChanged = false;
    // Frei belegbare Tasten (Tastenprofil xrshell.keys, siehe key_profile.h)
    bool     aPressed = false, xPressed = false, yPressed = false;
    bool     leftTriggerPressed = false;      // Trigger links
    bool     stickClickPressed  = false;      // rechten Stick drücken
    // Griffposen beider Hände (Inhaltsraum) — Fenster greifen/skalieren
    bool     leftHandValid = false, rightHandValid = false;
    XrVector3f leftHandPos = {}, rightHandPos = {};
    bool     headValid = false;          // Kopfposition (Inhaltsraum) — Bildschirm zum Kopf drehen
    XrVector3f headPos = {};
    bool     focused       = false;      // Session FOCUSED (sonst Eingaben lösen)
    XrTime   displayTime    = 0;
};

struct FrameView {
    XrPosef pose        = {};   // Blickpose des Auges (STAGE)
    XrFovf  fov         = {};
    int32_t width       = 0;
    int32_t height      = 0;
    uint32_t imageIndex = 0;
};

class XrApp {
public:
    // Erzeugt Instance/System/Session/Swapchains. EGL muss bereits laufen.
    bool init(android_app* app, const EglState& egl);
    void shutdown();

    // Lebenszyklus (vom NativeActivity aufgerufen)
    void onSessionStateChanged(XrSessionState state);
    void pollEvents(bool& running);

    // Ein kompletter Frame: liefert Views + Eingaben, rendert via Callback je Auge.
    // renderCallback(ui, view): gl-Rendering in den gerade akquirierten Swapchain-Buffer.
    bool beginFrame(FrameInput& input);
    FrameView acquireView(int eye);
    void releaseView(int eye);
    void endFrame();

    bool sessionRunning() const { return mSessionRunning; }

    // Systemcheck der Ladeanzeige: Laufzeit („Oculus 1.0“, leer = unbekannt) und aktives
    // Interaktionsprofil der rechten Hand (Pfad, leer = noch keins/kein Controller).
    const std::string& runtimeName() const { return mRuntimeName; }
    std::string interactionProfile() const;

    // Inhaltsraum auf aktuelle Kopfposition (X/Z) und Blick-Yaw setzen; Pitch
    // bleibt unberücksichtigt. Nur nach beginFrame() aufrufen. false, solange die
    // Kopfpose nicht gültig ist (dann im nächsten Frame erneut versuchen).
    // headHeight: Augenhöhe über dem Boden (STAGE-Y) für die Screen-Höhe.
    bool recenter(float& headHeight);

    // Bildwiederholrate der Quest passend zur Core-Bildrate (XR_FB_display_refresh_rate,
    // refresh_rate.h). Pro Frame aufrufbar, wirkt nur bei Änderung.
    void matchRefreshRate(double coreFps);
    // Zuletzt bekannte Bildwiederholrate des Displays in Hz (0 = unbekannt, z. B. ohne
    // XR_FB_display_refresh_rate) — für GET_TARGET_REFRESH_RATE der Cores.
    float displayRefreshRate() const { return mCurrentRefreshRate; }

    // Kurzer Haptik-Impuls auf der Zeigerhand (rechts).
    void vibrate(float seconds, float amplitude);

    // Passthrough (XR_FB_passthrough) als Hintergrund: 0 = aus, 1..100 =
    // Deckkraft der Kamerasicht in %. Pro Frame aufrufbar, wirkt nur bei
    // Änderung. Ohne Erweiterung oder bei Fehler bleibt es still schwarz.
    void setPassthrough(int percent);
    bool passthroughActive() const { return mPassthroughRunning; }
    bool passthroughSupported() const { return mPassthroughSupported; }

    // --- Bildschirm als Compositor-Ebene (Quad bzw. Zylinder) -----------------
    // Das Spielbild wird in eine eigene Swapchain gerendert und vom Compositor
    // direkt abgetastet (ein Resampling weniger als im Augenbild, dazu
    // Ebenen-Schärfung per XR_FB_composition_layer_settings) → schärfere Schrift.
    struct ScreenLayer {
        XrPosef pose = {};        // Quad: Bildmitte; Zylinder: Achsmitte (Bild in −Z)
        float width = 1.0f, height = 1.0f;   // Meter
        float radius = 0.0f;      // 0 = flach (Quad), sonst Zylinder
        int32_t rectW = 0, rectH = 0;       // genutzter Bildausschnitt in Pixeln
    };
    bool screenLayerAvailable() const { return mScreenSwapchain.handle != XR_NULL_HANDLE; }
    int32_t screenLayerWidth() const { return mScreenSwapchain.width; }
    int32_t screenLayerHeight() const { return mScreenSwapchain.height; }
    // Swapchain-Bild holen und sein FBO binden (danach zeichnen), dann abgeben.
    bool beginScreenLayer();
    void endScreenLayer(const ScreenLayer& layer);

private:
    bool createInstance(android_app* app);
    void initPassthroughFunctions();
    void applyPerformanceLevel();   // XR_EXT_performance_settings: CPU hoch
    void destroyPassthrough();
    bool createSession(const EglState& egl);
    bool createSwapchains();
    bool createActions();

    android_app* mApp = nullptr;
    std::string mRuntimeName;

    XrInstance     mInstance     = XR_NULL_HANDLE;
    XrSystemId     mSystemId     = XR_NULL_SYSTEM_ID;
    XrSession      mSession      = XR_NULL_HANDLE;
    XrSpace        mStageSpace   = XR_NULL_HANDLE;
    XrSpace        mContentSpace = XR_NULL_HANDLE;   // STAGE, zentriert (recenter)
    XrSpace        mViewSpace    = XR_NULL_HANDLE;   // Kopf (VIEW)
    XrSessionState mSessionState = XR_SESSION_STATE_UNKNOWN;
    bool           mSessionRunning = false;

    struct EyeSwapchain {
        XrSwapchain handle = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageOpenGLESKHR> images;
        // Swapchain-Bilder sind GLES-Texturen, keine Framebuffer: je Bild ein
        // eigenes FBO mit der Textur als Farb- und einem Tiefenpuffer pro Auge.
        std::vector<GLuint> framebuffers;
        GLuint depthBuffer = 0;
        int32_t width  = 0;
        int32_t height = 0;
    };
    EyeSwapchain mEyes[2];
    EyeSwapchain mScreenSwapchain;           // Compositor-Ebene des Bildschirms
    bool mScreenLayerReady = false;          // in diesem Frame gerendert → einreichen
    ScreenLayer mScreenLayer;
    bool mCylinderSupported = false;         // XR_KHR_composition_layer_cylinder
    bool mLayerSettingsSupported = false;    // XR_FB_composition_layer_settings
    bool createScreenSwapchain();

    // Actions (rechte Hand; Menu auf linker)
    XrActionSet mActionSet    = XR_NULL_HANDLE;
    XrAction    mAimAction    = XR_NULL_HANDLE;
    XrAction    mSelectAction = XR_NULL_HANDLE;
    XrAction    mGripAction   = XR_NULL_HANDLE;
    XrAction    mBAction      = XR_NULL_HANDLE;   // B → Maus-Rechts
    XrAction    mStickAction  = XR_NULL_HANDLE;   // rechter Thumbstick (Vector2f)
    XrAction    mMenuAction   = XR_NULL_HANDLE;   // linke Menu-Taste
    XrAction    mLeftGripAction  = XR_NULL_HANDLE;   // Griff links
    XrAction    mLeftStickAction = XR_NULL_HANDLE;   // linker Thumbstick
    XrAction    mLeftClickAction = XR_NULL_HANDLE;   // linken Stick drücken
    XrAction    mHapticAction    = XR_NULL_HANDLE;   // Vibration rechts
    XrAction    mAAction = XR_NULL_HANDLE, mXAction = XR_NULL_HANDLE, mYAction = XR_NULL_HANDLE;
    XrAction    mLeftTriggerAction = XR_NULL_HANDLE;   // Trigger links
    XrAction    mStickClickAction  = XR_NULL_HANDLE;   // rechten Stick drücken
    XrSpace    mAimSpace     = XR_NULL_HANDLE;
    XrAction   mLeftHandPoseAction = XR_NULL_HANDLE, mRightHandPoseAction = XR_NULL_HANDLE;
    XrSpace    mLeftHandSpace = XR_NULL_HANDLE, mRightHandSpace = XR_NULL_HANDLE;

    // --- Passthrough (XR_FB_passthrough, Funktionszeiger dynamisch geladen) ---
    bool mPassthroughSupported = false;
    bool mPerfSettingsSupported = false;     // XR_EXT_performance_settings
    bool mRefreshRateSupported = false;      // XR_FB_display_refresh_rate
    double mRefreshForFps = -1.0;            // Core-Bildrate, für die zuletzt gewählt wurde
    std::vector<float> mRefreshRates;        // angebotene Raten
    unsigned mRefreshProbe = 0;              // Runtime-Raten nur bei jedem 8. Wechsel erneut abfragen
    std::vector<float> mFallbackRates;       // Quest-3-Standard, solange die Runtime keine meldet
    float mStartRefreshRate = 0.0f;          // Rate beim ersten Aufruf (Standard der Runtime)
    float mCurrentRefreshRate = 0.0f;        // zuletzt bekannte Anzeige-Rate (0 = unbekannt)
    PFN_xrEnumerateDisplayRefreshRatesFB mEnumerateRefreshRates = nullptr;
    PFN_xrGetDisplayRefreshRateFB        mGetRefreshRate = nullptr;
    PFN_xrRequestDisplayRefreshRateFB    mRequestRefreshRate = nullptr;
    bool mPassthroughRunning = false;
    int  mPassthroughPercent = 0;            // zuletzt angewendet
    XrPassthroughFB      mPassthrough = XR_NULL_HANDLE;
    XrPassthroughLayerFB mPassthroughLayer = XR_NULL_HANDLE;
    PFN_xrCreatePassthroughFB        mCreatePassthrough = nullptr;
    PFN_xrDestroyPassthroughFB       mDestroyPassthrough = nullptr;
    PFN_xrPassthroughStartFB         mStartPassthrough = nullptr;
    PFN_xrPassthroughPauseFB         mPausePassthrough = nullptr;
    PFN_xrCreatePassthroughLayerFB   mCreatePassthroughLayer = nullptr;
    PFN_xrDestroyPassthroughLayerFB  mDestroyPassthroughLayer = nullptr;
    PFN_xrPassthroughLayerResumeFB   mResumePassthroughLayer = nullptr;
    PFN_xrPassthroughLayerPauseFB    mPausePassthroughLayer = nullptr;
    PFN_xrPassthroughLayerSetStyleFB mSetPassthroughStyle = nullptr;

    XrFrameState mFrameState = { XR_TYPE_FRAME_STATE };

    static void check(XrResult result, const char* what);  // loggt Fehler, wirft nicht
    // Hinweis: member-like Nutzung über mInstance-Handle für xrResultToString
    static XrInstance sInstanceForErrors;
};