// XRShell — #5: virtuelles Tastatur-Overlay für DOS-Spiele.
//
// Eigenimplementierung auf Basis des vorhandenen Quad-Renderings (kein ImGui):
//   · halbtransparente Tastatur-Fläche unterhalb des Screens, folgt dessen Pose
//   · Tasten als Quad-Grid (QWERTZ/QWERTY je Gastbelegung + Ziffern + F1–F12 + Enter/Esc/Pfeile)
//   · Beschriftungen aus einer winzigen eingebetteten 3×5-Pixelfont
//     (Atlas-Textur, zur Init-Zeit CPU-seitig gerendert)
//   · Eingabe: Raycast + Trigger — gedrückte Taste → libretro-Tastaturzustand
//     (dieselbe Atomic-Tabelle wie M1c)
//
// GLES nur im Render-Thread (init/draw auf dem Main-Thread, wie der Renderer).

#pragma once

#include "libretro.h"
#include "quad_renderer.h"   // ScreenState, RayHit

#include <GLES3/gl3.h>

#include <string>
#include <vector>

class KeyboardOverlay {
public:
    bool init();      // GL-Programm, Atlas, Tasten-Layout
    void shutdown();

    bool visible() const { return mVisible; }
    void toggle() { mVisible = !mVisible; }
    void hide() { mVisible = false; }

    // Rendert Overlay (Hintergrund + Tasten). Nur Render-Thread.
    void draw(const float* eyeView, const float* eyeProj, const ScreenState& screen);

    // Index der Taste unter dem Strahl (-1 = kein Treffer). Reine Mathematik.
    int keyAt(const XrPosef& aimPose, const ScreenState& screen) const;
    // libretro-Keycode einer Taste (für die Adapter-Tabelle).
    enum retro_key retroKey(int keyIndex) const;
    // Highlight der gerade gehaltenen Taste (für draw).
    void setPressedKey(int keyIndex) { mPressedKey = keyIndex; }
    // QWERTZ (deutsche Gastbelegung) oder QWERTY; beim Spielstart setzen. Ändert die
    // Tastenindizes — gehaltene Overlay-Taste vorher lösen. Atlas folgt im nächsten draw.
    void setGermanLayout(bool german);

private:
    void computeLayout();                     // Tasten-Rechtecke in Overlay-UV [0..1]
    void buildAtlas();                        // Label-Textur CPU-seitig rasterisieren
    void rebuildVertices();                   // VBO-Inhalt aus Layout + Pressed-State
    static void overlayGeometry(const ScreenState& screen,
                                float& cx, float& cy, float& w, float& h, float& z);

    bool mVisible = false;
    bool mGerman = false;
    bool mAtlasDirty = false;   // Beschriftung neu rastern (Render-Thread, in draw)
    int mPressedKey = -1;

    GLuint mProgram = 0;
    GLuint mVbo = 0;
    GLuint mAtlas = 0;
    GLint mLocMvp = -1, mLocTex = -1, mLocBg = -1;

    struct KeyRect {
        std::string label;
        enum retro_key code;
        float u0, v0, u1, v1;   // Rechteck in Overlay-UV (v = 0 oben)
    };
    std::vector<KeyRect> mKeys;
    std::vector<float>   mVertices;   // 6 Ecken pro Taste: x,y,z,u,v,shade
};
