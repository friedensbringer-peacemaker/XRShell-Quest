// XRShell — #5: KeyboardOverlay-Implementierung. Siehe keyboard_overlay.h.

#include "keyboard_overlay.h"
#include "keyboard_layout.h"
#include "pixel_font.h"

#include <android/log.h>

#include <cmath>
#include <cstring>

#define LOG_TAG "XRShell"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {


// --- Shader --------------------------------------------------------------------
const char* kVertex = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUv;
layout(location=2) in float aShade;
uniform mat4 uMvp;
out vec2 vUv;
out float vShade;
void main() { vUv = aUv; vShade = aShade; gl_Position = uMvp * vec4(aPos, 1.0); })";

const char* kFragment = R"(#version 300 es
precision mediump float;
in vec2 vUv;
in float vShade;
uniform sampler2D uTex;
uniform int uBg;
out vec4 frag;
void main() {
    if (uBg == 1) { frag = vec4(0.04, 0.05, 0.08, 0.45); return; }
    vec4 t = texture(uTex, vUv);
    vec3 base = mix(vec3(0.13, 0.14, 0.18), vec3(0.30, 0.42, 0.60), vShade);
    frag = vec4(mix(base, vec3(0.95), t.a), mix(0.62, 0.92, vShade));
})";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Overlay-Shader-Fehler: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void mat4MulLocal(float* out, const float* a, const float* b) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r[c * 4 + rr] = a[rr] * b[c * 4] + a[4 + rr] * b[c * 4 + 1] +
                            a[8 + rr] * b[c * 4 + 2] + a[12 + rr] * b[c * 4 + 3];
    for (int i = 0; i < 16; ++i) out[i] = r[i];
}

// Atlas: 16 × 8 Zellen à 64 px
constexpr int kAtlasW = 1024, kAtlasH = 512, kCell = 64;

} // namespace

// ==============================================================================
// Layout
// ==============================================================================

void KeyboardOverlay::overlayGeometry(const ScreenState& screen,
                                      float& cx, float& cy, float& w, float& h,
                                      float& z) {
    // An der Höhe festgemacht, nicht an der Breite: die Breite folgt seit #11
    // dem Seitenverhältnis des Cores (4:3 → schmaler), die Tasten sollen gleich
    // groß bleiben. 1.28 × 1.8 m = 2.3 m wie beim ursprünglichen 16:9-Screen.
    w = screen.height * 1.28f;
    h = w * 0.36f;
    cx = screen.posX;
    cy = screen.posY - screen.height * 0.5f - h * 0.5f - 0.18f;
    z = screen.posZ + 0.10f;   // leicht Richtung Nutzer vor dem Screen
}

void KeyboardOverlay::computeLayout() {
    // QWERTZ bei deutscher Gastbelegung, sonst QWERTY (keyboard_layout.h).
    const kblayout::Row* rows = kblayout::rows(mGerman);

    mKeys.clear();
    constexpr float kMx = 0.02f, kMy = 0.03f, kGap = 0.010f;
    const int rowCount = kblayout::kRowCount;
    const float rh = (1.0f - 2.0f * kMy - kGap * (rowCount - 1)) / rowCount;

    for (int r = 0; r < rowCount; ++r) {
        const kblayout::KeyDef* row = rows[r].keys;
        const int n = rows[r].count;
        float totalSpan = 0.0f;
        for (int i = 0; i < n; ++i) totalSpan += row[i].span;
        const float usable = 1.0f - 2.0f * kMx - kGap * (n - 1);
        float x = kMx;
        const float v0 = kMy + r * (rh + kGap);
        for (int i = 0; i < n; ++i) {
            const float kw = usable * (row[i].span / totalSpan);
            KeyRect rect;
            rect.label = row[i].label;
            rect.code = row[i].code;
            rect.u0 = x;
            rect.u1 = x + kw;
            rect.v0 = v0;
            rect.v1 = v0 + rh;
            mKeys.push_back(rect);
            x += kw + kGap;
        }
    }
}

// ==============================================================================
// Atlas (Label-Textur)
// ==============================================================================

void KeyboardOverlay::buildAtlas() {
    std::vector<uint8_t> atlas(kAtlasW * kAtlasH * 4, 0);

    for (size_t i = 0; i < mKeys.size(); ++i) {
        const std::string& label = mKeys[i].label;
        const int cellX = static_cast<int>(i % 16) * kCell;
        const int cellY = static_cast<int>(i / 16) * kCell;

        // Glyphe 3×5, Skalierung 6 → 18×30; String zentriert in der Zelle
        constexpr int kScale = 6, kGlyphW = 3 * kScale, kGlyphH = 5 * kScale,
                      kSpacing = 2 * kScale;
        const int textW = static_cast<int>(label.size()) * kGlyphW +
                          (static_cast<int>(label.size()) - 1) * kSpacing;
        int penX = cellX + (kCell - textW) / 2;
        const int penY = cellY + (kCell - kGlyphH) / 2;

        for (char c : label) {
            const uint8_t* glyph = pixelfont::glyphFor(c);
            if (glyph) {
                for (int row = 0; row < 5; ++row) {
                    for (int col = 0; col < 3; ++col) {
                        if (!(glyph[row] & (1 << (2 - col)))) continue;
                        for (int sy = 0; sy < kScale; ++sy) {
                            for (int sx = 0; sx < kScale; ++sx) {
                                const int px = penX + col * kScale + sx;
                                const int py = penY + row * kScale + sy;
                                uint8_t* p = &atlas[(py * kAtlasW + px) * 4];
                                p[0] = p[1] = p[2] = 255;
                                p[3] = 255;
                            }
                        }
                    }
                }
            }
            penX += kGlyphW + kSpacing;
        }
    }

    glGenTextures(1, &mAtlas);
    glBindTexture(GL_TEXTURE_2D, mAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasW, kAtlasH, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ==============================================================================
// GL-Lebenszyklus
// ==============================================================================

void KeyboardOverlay::setGermanLayout(bool german) {
    if (german == mGerman) return;
    mGerman = german;
    mPressedKey = -1;
    computeLayout();
    mAtlasDirty = mProgram != 0;   // vor init(): init() rastert ohnehin
    LOGI("Tastatur-Overlay: %s", german ? "QWERTZ (Gast deutsch)" : "QWERTY (Gast US)");
}

bool KeyboardOverlay::init() {
    computeLayout();
    if (mKeys.empty()) return false;

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertex);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragment);
    if (!vs || !fs) return false;
    mProgram = glCreateProgram();
    glAttachShader(mProgram, vs);
    glAttachShader(mProgram, fs);
    glLinkProgram(mProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &ok);
    if (!ok) { LOGE("Overlay-Program-Link fehlgeschlagen"); return false; }
    mLocMvp = glGetUniformLocation(mProgram, "uMvp");
    mLocTex = glGetUniformLocation(mProgram, "uTex");
    mLocBg = glGetUniformLocation(mProgram, "uBg");

    glGenBuffers(1, &mVbo);
    buildAtlas();
    LOGI("Tastatur-Overlay: %zu Tasten, Atlas %dx%d", mKeys.size(), kAtlasW, kAtlasH);
    return true;
}

void KeyboardOverlay::shutdown() {
    if (mAtlas) glDeleteTextures(1, &mAtlas);
    if (mVbo) glDeleteBuffers(1, &mVbo);
    if (mProgram) glDeleteProgram(mProgram);
    mAtlas = mVbo = mProgram = 0;
    mKeys.clear();
    mVertices.clear();
}

// ==============================================================================
// Rendering
// ==============================================================================

void KeyboardOverlay::rebuildVertices() {
    mVertices.clear();
    mVertices.reserve((mKeys.size() + 1) * 6 * 6);
    // Erste 6 Ecken: Hintergrund-Quad (volle Einheitsfläche, uBg=1 im Shader),
    // minimal hinter den Tasten (z = -0.01)
    const float bg[6][6] = {
        {-1, -1, -0.01f, 0, 0, 0}, {1, -1, -0.01f, 0, 0, 0}, {1, 1, -0.01f, 0, 0, 0},
        {-1, -1, -0.01f, 0, 0, 0}, {1,  1, -0.01f, 0, 0, 0}, {-1, 1, -0.01f, 0, 0, 0},
    };
    for (auto& v : bg) mVertices.insert(mVertices.end(), v, v + 6);
    for (size_t i = 0; i < mKeys.size(); ++i) {
        const KeyRect& k = mKeys[i];
        const float shade = (static_cast<int>(i) == mPressedKey) ? 1.0f : 0.0f;
        // lokal: x = u*2-1, y = 1-v*2 (Einheitsquad [-1,1]²) → Skalierung im MVP
        const float x0 = k.u0 * 2.0f - 1.0f, x1 = k.u1 * 2.0f - 1.0f;
        const float y1 = 1.0f - k.v0 * 2.0f, y0 = 1.0f - k.v1 * 2.0f;   // v0 = oben
        // Atlas-UVs
        const float au0 = (static_cast<float>(i % 16) * kCell) / kAtlasW;
        const float au1 = au0 + static_cast<float>(kCell) / kAtlasW;
        const float av0 = (static_cast<float>(i / 16) * kCell) / kAtlasH;
        const float av1 = av0 + static_cast<float>(kCell) / kAtlasH;

        const float quad[6][6] = {
            {x0, y0, 0, au0, av1, shade},
            {x1, y0, 0, au1, av1, shade},
            {x1, y1, 0, au1, av0, shade},
            {x0, y0, 0, au0, av1, shade},
            {x1, y1, 0, au1, av0, shade},
            {x0, y1, 0, au0, av0, shade},
        };
        for (auto& v : quad) mVertices.insert(mVertices.end(), v, v + 6);
    }
}

void KeyboardOverlay::draw(const float* eyeView, const float* eyeProj,
                           const ScreenState& screen) {
    if (!mVisible) return;
    if (mAtlasDirty) {
        if (mAtlas) glDeleteTextures(1, &mAtlas);
        mAtlas = 0;
        buildAtlas();
        mAtlasDirty = false;
    }

    float cx, cy, w, h, z;
    overlayGeometry(screen, cx, cy, w, h, z);

    glUseProgram(mProgram);
    glEnable(GL_BLEND);
    // Alpha separat: Farbe vormultipliziert, Alpha korrekt für den Passthrough-Layer.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);   // Overlay immer sichtbar, auch über dem Laser

    // Hintergrund-Quad (Einheitsquad im VBO, uBg=1 ignoriert Textur/Atlas-UV)
    rebuildVertices();
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, mVertices.size() * sizeof(float),
                 mVertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(5 * sizeof(float)));

    float model[16] = { w, 0, 0, 0,
                        0, h, 0, 0,
                        0, 0, 1, 0,
                        cx, cy, z, 1 };
    float mv[16], mvp[16];
    mat4MulLocal(mv, const_cast<float*>(eyeView), model);
    mat4MulLocal(mvp, const_cast<float*>(eyeProj), mv);
    glUniformMatrix4fv(mLocMvp, 1, GL_FALSE, mvp);
    glUniform1i(mLocTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mAtlas);

    // Hintergrund (leicht größer darstellbar über Einheitsquad-Skalierung im
    // Shader-Modus uBg=1) zuerst, dann alle Tasten mit Atlas-Textur
    glUniform1i(mLocBg, 1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glUniform1i(mLocBg, 0);
    glDrawArrays(GL_TRIANGLES, 6, static_cast<int>(mKeys.size()) * 6);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

// ==============================================================================
// Raycast-Eingabe
// ==============================================================================

int KeyboardOverlay::keyAt(const XrPosef& aimPose, const ScreenState& screen) const {
    float cx, cy, w, h, z;
    overlayGeometry(screen, cx, cy, w, h, z);
    const RayHit hit = raycastToQuad(cx, cy, z, w, h, aimPose);
    if (!hit.hit) return -1;
    for (size_t i = 0; i < mKeys.size(); ++i) {
        const KeyRect& k = mKeys[i];
        if (hit.u >= k.u0 && hit.u <= k.u1 && hit.v >= k.v0 && hit.v <= k.v1)
            return static_cast<int>(i);
    }
    return -1;
}

enum retro_key KeyboardOverlay::retroKey(int keyIndex) const {
    if (keyIndex < 0 || keyIndex >= static_cast<int>(mKeys.size()))
        return RETROK_UNKNOWN;
    return mKeys[static_cast<size_t>(keyIndex)].code;
}
