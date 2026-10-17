// XRShell — Renderer: virtueller Bildschirm (texturierter Quad), Laser-Strahl, Cursor.
//
// M0 rendert absichtlich eine Prozedur-Textur (Schachbrett + Testmuster),
// damit das Gerüst ohne Spiele-Assets baut und läuft.
// M1a ersetzt den Inhalt durch Core-Frames: der LibretroAdapter lädt per
// glTexSubImage2D in dieselbe 1024×768-Textur — das Testmuster bleibt als
// Fallback sichtbar, solange kein Core läuft.

#pragma once

#include <GLES3/gl3.h>
#include <openxr/openxr.h>

#include <cstdint>

#include "screen_filter.h"

// Bildschirm im STAGE-Raum: 3,2 m × 1,8 m, 2 m vor der Startposition.
// pos ist die Bildmitte, yaw die Drehung um die Hochachse (0 = Bild schaut
// nach +Z, zum Startpunkt). Gewölbt (Ultrawide-Stil) liegt das Bild auf einem
// senkrechten Zylinder mit Radius curveRadius, dessen Achse zum Betrachter hin
// vor der Mitte steht — die Ränder kommen näher. 0 = flach.
// „Lokal“ = Bildschirm-Koordinaten: Mitte im Ursprung, yaw 0. Overlay und
// Menü hängen lokal am Bildschirm und drehen/wandern mit.
struct ScreenState {
    float posX = 0.0f, posY = 1.6f, posZ = -2.0f;
    float yaw = 0.0f;
    float width = 3.2f, height = 1.8f;
    float curveRadius = 0.0f;
    bool cursorVisible = false;
    float cursorU = 0.5f, cursorV = 0.5f;   // Cursor in Screen-UV [0..1]
};

// Punkt auf der (evtl. gewölbten) Bildfläche zu UV — gemeinsame Geometrie für
// Netz, Cursor und Raycast, damit Klickpunkt und Bild nie auseinanderlaufen.
void screenPoint(const ScreenState& screen, float u, float v, float* out);

// Lokal → Welt als 4×4-Spaltenmajor-Matrix (Translation · Drehung um Y).
void screenModelMatrix(const ScreenState& screen, float* m);
// Welt-Pose in Bildschirm-Koordinaten (für Raycasts an Overlay/Menü).
XrPosef worldToScreenLocal(const ScreenState& screen, const XrPosef& pose);
// Derselbe Bildschirm in eigenen Koordinaten (Mitte im Ursprung, yaw 0).
ScreenState localScreen(const ScreenState& screen);
// Spaltenmajor-4×4-Produkt out = a · b (out darf a oder b sein).
void mat4Multiply(float* out, const float* a, const float* b);

// Bildfilter für Core-Frames (VR-Menü): ScreenFilter in screen_filter.h.

// Experimentell „Umgebung ausblenden“: Ergebnis der Frame-Analyse (Framezeilen/-spalten).
struct EnvironmentKey {
    bool valid = false;
    uint32_t sky = 0;                 // Himmelsfarbe 0xRRGGBB
    float skyEnd = 0, horizon = 0;    // Ende Himmelsband, Horizontzeile
    uint32_t ground[4] = {};          // häufigste Bodenfarben (Rand, unteres Viertel)
    int groundCount = 0;
    float border[4] = {};             // schwarzer Rand: oben, unten, links, rechts
};

// Ergebnis des Raycast-Hit-Tests (Issue #1): wird pro Frame berechnet und
// an Logik/Adapter weitergereicht — nicht nur visual im Renderer.
struct RayHit {
    bool hit = false;          // Strahl trifft die Screen-Ebene innerhalb [0..1]²
    float u = 0.5f, v = 0.5f;  // Position in Screen-UV
    float distance = 0.0f;     // Abstand Controller → Trefferpunkt (Meter)
};

// Raycast: Controller-Aim gegen die Screen-Ebene (achsparalleler Quad in posZ).
// Reine Mathematik, kein GLES — bewusst außerhalb der Render-Schleife nutzbar.
RayHit raycastToScreen(const ScreenState& screen, const XrPosef& aimPose);

// Verallgemeinerung für beliebige achsenparallele Quads (z. B. das
// Tastatur-Overlay, #5): Mittelpunkt (centerX/centerY) in Z = planeZ,
// v = 0 am oberen Rand — gleiche UV-Konvention wie der Screen.
RayHit raycastToQuad(float centerX, float centerY, float planeZ,
                     float width, float height, const XrPosef& aimPose);

class QuadRenderer {
public:
    bool init();          // GLES-Shader, Geometrie, Testtextur
    void shutdown();

    // Rendert Bildschirm + Cursor + Laser für ein Auge.
    //   eyeView / eyeProj: 4x4-Spaltenmajor-Matrizen aus Pose/FoV
    //   aimPose/aimValid:  Controller für Laser + Cursor-Projektion
    //   laserLength:       Strahllänge bis zum Treffer (Bildschirm/Menü), Meter
    void draw(const float* eyeView, const float* eyeProj,
              const ScreenState& screen,
              const XrPosef* aimPose, bool aimValid, float laserLength = 4.0f);

    GLuint texture() const { return mTexture; }   // M1: Adapter rendert hier hinein

    // #10: Textur enthält Core-Frames (libretro XRGB8888 = B,G,R,X im Speicher)
    // statt des RGBA-Testmusters — der Shader tauscht dann die Farbkanäle.
    // #11: Der Frame belegt nur die linke obere w×h-Ecke der Textur — der
    // Screen-Quad zeigt genau diesen Ausschnitt.
    void setCoreFrame(int width, int height) {
        mCoreFrame = width > 0 && height > 0;
        mUvScaleU = static_cast<float>(width) / kTextureW;
        mUvScaleV = static_cast<float>(height) / kTextureH;
    }

    // Bilddarstellung aus dem VR-Menü: Filter, Sättigung/Kontrast in % (100 = neutral).
    // HD-Filter laufen im zweiten Programm; fehlt es, gilt withoutHd(filter).
    void setImageSettings(ScreenFilter filter, int saturation, int contrast) {
        mFilter = filter;
        mSaturation = static_cast<float>(saturation) / 100.0f;
        mContrast = static_cast<float>(contrast) / 100.0f;
    }
    // Strahl und Zielpunkt (VR-Menü, Reiter „Zeiger“): Farbe RGB 0..1, Strahldicke
    // in m (0 = kein Strahl), größte Strahllänge in m, Zielpunktgröße in m (0 = aus).
    void setPointerStyle(const float rgb[3], float rayThickness, float rayMaxLength,
                         float markerSize) {
        for (int i = 0; i < 3; ++i) mPointerColor[i] = rgb[i];
        mRayThickness = rayThickness;
        mRayMaxLength = rayMaxLength;
        mMarkerSize = markerSize;
    }
    // Bildzoom: zeigt den Frame-Ausschnitt ab (u, v) mit Kantenlänge 1/zoom
    // (Frame-UV 0..1). zoom 1 = ganzes Bild.
    void setZoom(float zoom, float u, float v) { mZoom = zoom; mZoomU = u; mZoomV = v; }

    // Passthrough an: Hintergrund transparent (Alpha 0) statt dunkelgrau.
    void setTransparentBackground(bool transparent) { mTransparent = transparent; }

    // Experimentell: Umgebung des Spielbilds durchsichtig (Passthrough dahinter).
    //   mode 0 aus · 1 Himmel · 2 Himmel + Band bis zum Horizont ·
    //   3 zusätzlich Bodenfarben unterhalb des Horizonts („nur Strecke“).
    //   In allen Stufen verschwindet der schwarze Bildrand.
    void setEnvironmentKey(int mode, const EnvironmentKey& key) {
        mKeyMode = key.valid ? mode : 0;
        mKey = key;
    }

    // Bildschirm als Compositor-Ebene: draw() lässt das Netz weg (Hintergrund
    // transparent), drawScreenLayer() rendert das gefilterte Bild ins Ebenen-FBO.
    void setScreenInLayer(bool inLayer) { mScreenInLayer = inLayer; }
    // Videoaufnahme läuft: roter Punkt oben links am Bildschirm (Aufrufer lässt ihn blinken).
    void setRecordingDot(bool on) { mRecordingDot = on; }
    void drawScreenLayer();
    void setScreenUv();   // uUvScale/uUvOffset für den Bildschirm (inkl. Bildzoom)
    // Genutzter Frame in Texeln (für die ganzzahlige Vergrößerung der Ebene).
    int frameWidth() const { return mCoreFrame ? static_cast<int>(mUvScaleU * kTextureW + 0.5f) : kTextureW; }
    int frameHeight() const { return mCoreFrame ? static_cast<int>(mUvScaleV * kTextureH + 0.5f) : kTextureH; }

    static constexpr int kTextureW = 1024;   // Screen-Textur (Core-Frames ≤ diese Größe)
    static constexpr int kTextureH = 768;

private:
    void updateScreenMesh(const ScreenState& screen);   // Netz bei Größen-/Wölbungswechsel
    void setScreenUniforms();
    static void bindVertices(GLuint vbo);
    bool mScreenInLayer = false;
    bool mRecordingDot = false;

    bool mCoreFrame = false;
    float mUvScaleU = 1.0f, mUvScaleV = 1.0f;
    float mZoom = 1.0f, mZoomU = 0.0f, mZoomV = 0.0f;
    float mPointerColor[3] = {1.0f, 0.55f, 0.1f};   // Orange (bisheriger Standard)
    float mRayThickness = 0.004f, mRayMaxLength = 100.0f, mMarkerSize = 0.025f;
    ScreenFilter mFilter = ScreenFilter::Sharp;
    float mSaturation = 1.0f, mContrast = 1.0f;
    bool mTransparent = false;
    int mKeyMode = 0;
    EnvironmentKey mKey;
    // Uniform-Orte eines Programms; [0] Basis, [1] mit HD-Filter (xBR, 0 = nicht verfügbar).
    struct Locs {
        GLint mvp = -1, tex = -1, mode = -1, uvScale = -1, uvOffset = -1, color = -1;
        GLint texSize = -1, frameSize = -1, filter = -1, sat = -1, contrast = -1;
        GLint keyMode = -1, keyColor = -1, keyRows = -1, ground = -1, groundCount = -1, border = -1;
        void query(GLuint program);
    };
    const Locs& L() const { return mLoc[mCur]; }
    GLuint mProgram[2] = {0, 0};
    Locs mLoc[2];
    int mCur = 0;                            // aktives Programm (setScreenUniforms)
    GLuint mVbo = 0;
    GLuint mScreenVbo = 0;                   // Bildschirm-Netz (Spalten, evtl. gewölbt)
    GLsizei mScreenVertices = 0;
    float mMeshW = -1.0f, mMeshH = -1.0f, mMeshR = -1.0f;
    GLuint mTexture = 0;
};
