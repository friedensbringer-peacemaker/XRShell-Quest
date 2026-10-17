#include "quad_renderer.h"

#include <android/log.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#define LOG_TAG "XRShell"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

// --- Minimal-Mathematik (Spaltenmajor, kompatibel mit OpenXR-Pose/FoV) ----------
void mat4Mul(float* out, const float* a, const float* b) {
    float r[16];
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r[c * 4 + rr] = a[rr] * b[c * 4] + a[4 + rr] * b[c * 4 + 1] +
                            a[8 + rr] * b[c * 4 + 2] + a[12 + rr] * b[c * 4 + 3];
    for (int i = 0; i < 16; ++i) out[i] = r[i];
}

// Vorwärtsrichtung einer OpenXR-Pose: die Aim-Pose zeigt entlang ihrer -Z-Achse
// (#14). = negierte dritte Spalte der Rotationsmatrix aus dem Quaternion.
void aimForward(const XrQuaternionf& q, float* dir) {
    dir[0] = -2 * (q.x * q.z + q.w * q.y);
    dir[1] = -2 * (q.y * q.z - q.w * q.x);
    dir[2] = -(1 - 2 * (q.x * q.x + q.y * q.y));
}

// --- Shader ----------------------------------------------------------------------
const char* kVertex = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUv;
uniform mat4 uMvp;
uniform vec2 uUvScale;   // #11: genutzter Texturausschnitt (Core-Frame < 1024×768)
uniform vec2 uUvOffset;  // Bildzoom: linke obere Ecke des Ausschnitts (Textur-UV)
out vec2 vUv;
void main() { vUv = aUv * uUvScale + uUvOffset; gl_Position = uMvp * vec4(aPos, 1.0); })";

// mode 0 = Bildschirm (Testmuster, RGBA), 1 = Laser/Cursor (farbig),
// 2 = Bildschirm mit Core-Frame (#10): libretro-XRGB8888 liegt auf arm64 als
//     B,G,R,X im Speicher → Kanäle tauschen, X-Byte (undefiniert) ignorieren.
//     Filter (VR-Menü):
//       0 scharf: „sharp bilinear“ — Retro-Frames (z. B. 256×224) werden stark
//         vergrößert; bilinear verwischt jedes Pixel, Nearest flimmert in VR.
//         Innerhalb eines Pixels konstant, an der Kante über genau einen
//         Bildschirmpunkt interpoliert (fwidth = Texel pro Bildschirmpunkt).
//       1 weich: normales Bilinear
//       2 Scanlines: scharf + dunkle Linie zwischen den Bildzeilen
//       3 CRT: Scanlines + Streifenmaske + Randabdunklung
//       4 HD: Pixel-Art-Upscaler xBR-lv2 (nur im Programm mit XRSHELL_HD, siehe unten)
//       5 HD + Scanlines
//     Abgetastet wird nur innerhalb des Frames (uFrameSize) — sonst zieht der
//     Filter am rechten/unteren Rand Texel neben dem Frame mit (Außenlinien).
//     Danach Sättigung und Kontrast (1.0 = neutral).
const char* kFragment = R"(#version 300 es
precision highp float;
in vec2 vUv;
uniform sampler2D uTex;
uniform int uMode;
uniform vec2 uTexSize;    // Texturgröße in Texeln (1024×768)
uniform vec2 uFrameSize;  // genutzter Frame in Texeln (z. B. 256×224)
uniform int uFilter;
uniform float uSat;
uniform float uContrast;
uniform int uKeyMode;     // experimentell: Umgebung durchsichtig (0 aus)
uniform vec3 uKeyColor;   // Himmelsfarbe
uniform vec2 uKeyRows;    // x = Ende Himmelsband, y = Horizont (Framezeilen)
uniform vec3 uGround[4];  // Bodenfarben (Stufe 3 „nur Strecke“)
uniform int uGroundCount;
uniform vec4 uBorder;     // schwarzer Rand in Texeln: oben, unten, links, rechts
uniform vec3 uColor;      // Strahl/Zielpunkt (VR-Menü, Reiter „Zeiger“)
out vec4 frag;
bool sameColor(vec3 a, vec3 b) { return all(lessThan(abs(a - b), vec3(0.035))); }
vec3 frameAt(vec2 texel) {
    texel = clamp(texel, vec2(0.5), uFrameSize - 0.5);
    return texture(uTex, texel / uTexSize).bgr;
}
#ifdef XRSHELL_HD
// ---------------------------------------------------------------------------------
// HD-Filter: xBR level 2 („xbr-lv2“) nach Hyllian, eigene GLSL-ES-3.0-Umsetzung für
// XRShell (einstufig, beliebiger Maßstab, Kantenbreite = ein Ausgabepixel über fwidth).
// Vorlage: xBR-lv2-Shader aus libretro/glsl-shaders, MIT-Lizenz:
//   Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this
//   software and associated documentation files (the "Software"), to deal in the Software
//   without restriction, including without limitation the rights to use, copy, modify,
//   merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to the following
//   conditions: The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
//   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
//   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
//   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
//   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// Nachbarschaft (E = aktueller Quell-Texel), vec4-Komponenten = vier Drehungen um E:
//        A1 B1 C1
//     A0  A  B  C C4
//     D0  D  E  F F4
//     G0  G  H  I I4
//        G5 H5 I5
// Abgetastet wird nur im Frame (texelFetch, auf den Ausschnitt geklemmt).
const vec3 kXbrY = vec3(14.352, 28.176, 5.472);   // Luma · 48
const vec4 kAo = vec4(1.0, -1.0, -1.0, 1.0), kBo = vec4(1.0, 1.0, -1.0, -1.0), kCo = vec4(1.5, 0.5, -0.5, 0.5);
const vec4 kAx = vec4(1.0, -1.0, -1.0, 1.0), kBx = vec4(0.5, 2.0, -0.5, -2.0), kCx = vec4(1.0, 1.0, -0.5, 0.0);
const vec4 kAy = vec4(1.0, -1.0, -1.0, 1.0), kBy = vec4(2.0, 0.5, -2.0, -0.5), kCy = vec4(2.0, 0.0, -1.0, 0.5);
const vec4 kCi = vec4(0.25);
vec3 xpx(ivec2 p) { return texelFetch(uTex, clamp(p, ivec2(0), ivec2(uFrameSize) - 1), 0).bgr; }
vec4 xdf(vec4 a, vec4 b) { return abs(a - b); }
float xcdf(vec3 a, vec3 b) { vec3 d = abs(a - b); return d.r + d.g + d.b; }
vec4 xeq(vec4 a, vec4 b) { return step(xdf(a, b), vec4(15.0)); }
vec4 xne(vec4 a, vec4 b) { return vec4(1.0) - xeq(a, b); }
vec4 xdiff(vec4 a, vec4 b) { return vec4(notEqual(a, b)); }   // exakt verschieden
vec4 xwd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
    return xdf(a, b) + xdf(a, c) + xdf(d, e) + xdf(d, f) + 4.0 * xdf(g, h);
}
vec3 xbr(vec2 texel, vec2 width) {
    ivec2 p = ivec2(floor(texel));
    vec2 fp = fract(texel);
    vec3 A1 = xpx(p + ivec2(-1, -2)), B1 = xpx(p + ivec2(0, -2)), C1 = xpx(p + ivec2(1, -2));
    vec3 A0 = xpx(p + ivec2(-2, -1)), A = xpx(p + ivec2(-1, -1)), B = xpx(p + ivec2(0, -1));
    vec3 C = xpx(p + ivec2(1, -1)), C4 = xpx(p + ivec2(2, -1));
    vec3 D0 = xpx(p + ivec2(-2, 0)), D = xpx(p + ivec2(-1, 0)), E = xpx(p);
    vec3 F = xpx(p + ivec2(1, 0)), F4 = xpx(p + ivec2(2, 0));
    vec3 G0 = xpx(p + ivec2(-2, 1)), G = xpx(p + ivec2(-1, 1)), H = xpx(p + ivec2(0, 1));
    vec3 I = xpx(p + ivec2(1, 1)), I4 = xpx(p + ivec2(2, 1));
    vec3 G5 = xpx(p + ivec2(-1, 2)), H5 = xpx(p + ivec2(0, 2)), I5 = xpx(p + ivec2(1, 2));

    vec4 b = kXbrY * mat4x3(B, D, H, F);
    vec4 c = kXbrY * mat4x3(C, A, G, I);
    vec4 e = kXbrY * mat4x3(E, E, E, E);
    vec4 d = b.yzwx, f = b.wxyz, g = c.zwxy, h = b.zwxy, i = c.wxyz;
    vec4 i4 = kXbrY * mat4x3(I4, C1, A0, G5);
    vec4 i5 = kXbrY * mat4x3(I5, C4, A1, G0);
    vec4 h5 = kXbrY * mat4x3(H5, F4, B1, D0);
    vec4 f4 = h5.yzwx;

    // Kantenbreite: ein Ausgabepixel (in Quell-Texeln) → glatte, aber scharfe Diagonalen
    // bei jedem Maßstab (Ebene ganzzahlig 3–8×, Netz im Augenbild beliebig).
    float w = clamp(max(width.x, width.y), 0.05, 0.5);
    vec4 delta = vec4(w);
    vec4 deltaL = vec4(0.5, 1.0, 0.5, 1.0) * w;
    vec4 deltaU = deltaL.yxwz;
    vec4 fx = kAo * fp.y + kBo * fp.x;
    vec4 fxL = kAx * fp.y + kBx * fp.x;
    vec4 fxU = kAy * fp.y + kBy * fp.x;

    // Kante nur zwischen exakt verschiedenen Farben (lv0/lv2), Ecken-Regel mit Schwelle (lv1).
    vec4 irlv0 = xdiff(e, f) * xdiff(e, h);
    vec4 irlv1 = irlv0 * clamp(xne(f, b) * xne(f, c) + xne(h, d) * xne(h, g) +
                               xeq(e, i) * (xne(f, f4) * xne(f, i4) + xne(h, h5) * xne(h, i5)) +
                               xeq(e, g) + xeq(e, c), 0.0, 1.0);
    vec4 irlv2L = xdiff(e, g) * xdiff(d, g);
    vec4 irlv2U = xdiff(e, c) * xdiff(b, c);

    vec4 fx45i = clamp((fx + delta - kCo - kCi) / (2.0 * delta), 0.0, 1.0);
    vec4 fx45 = clamp((fx + delta - kCo) / (2.0 * delta), 0.0, 1.0);
    vec4 fx30 = clamp((fxL + deltaL - kCx) / (2.0 * deltaL), 0.0, 1.0);
    vec4 fx60 = clamp((fxU + deltaU - kCy) / (2.0 * deltaU), 0.0, 1.0);

    vec4 wd1 = xwd(e, c, g, i, h5, f4, h, f);
    vec4 wd2 = xwd(h, d, i5, f, i4, b, e, i);
    vec4 edri = step(wd1, wd2) * irlv0;                        // wd1 <= wd2
    vec4 edr = (vec4(1.0) - step(wd2, wd1)) * irlv1;           // wd1 <  wd2
    vec4 edrL = step(2.0 * xdf(f, g), xdf(h, c)) * irlv2L * edr;
    vec4 edrU = step(2.0 * xdf(h, c), xdf(f, g)) * irlv2U * edr;

    vec4 m = max(max(edrL * fx30, edrU * fx60), max(edr * fx45, edri * fx45i));
    vec4 px = step(xdf(e, f), xdf(e, h));                      // näher an F als an H?
    vec3 r1 = mix(E, mix(H, F, px.x), m.x);
    r1 = mix(r1, mix(B, D, px.z), m.z);
    vec3 r2 = mix(E, mix(F, B, px.y), m.y);
    r2 = mix(r2, mix(D, H, px.w), m.w);
    return mix(r1, r2, step(xcdf(E, r1), xcdf(E, r2)));
}
#endif
void main() {
    if (uMode == 0) {
        frag = texture(uTex, vUv);
    } else if (uMode == 2) {
        vec2 texel = vUv * uTexSize;
        vec2 width = max(fwidth(texel), vec2(1e-4));   // Texel pro Bildschirmpunkt
        vec3 c;
#ifdef XRSHELL_HD
        if (uFilter >= 4) c = xbr(texel, width); else
#endif
        if (uFilter == 1) {
            c = frameAt(texel);
        } else {
            vec2 seam = floor(texel + 0.5);
            c = frameAt(seam + clamp((texel - seam) / width, -0.5, 0.5));
        }
        if (uFilter == 2 || uFilter == 3 || uFilter == 5) {
            c *= 0.55 + 0.45 * sin(fract(texel.y) * 3.14159265);
            if (uFilter == 3) {
                int k = int(mod(floor(texel.x * 3.0), 3.0));
                vec3 mask = vec3(0.78);
                mask[k] = 1.12;
                c *= mask;
                vec2 f = clamp(texel / uFrameSize, 0.0, 1.0);
                float edge = 16.0 * f.x * f.y * (1.0 - f.x) * (1.0 - f.y);
                c *= mix(0.7, 1.0, pow(edge, 0.3));
                c *= 1.2;
            } else {
                c *= 1.12;
            }
        }
        float gray = dot(c, vec3(0.299, 0.587, 0.114));
        c = mix(vec3(gray), c, uSat);
        c = (c - 0.5) * uContrast + 0.5;
        // Umgebung ausblenden: vormultipliziert (Farbe · Alpha) für den Compositor.
        float alpha = 1.0;
        if (uKeyMode > 0) {
            vec3 raw = frameAt(floor(texel) + 0.5);
            bool hide = sameColor(raw, uKeyColor);
            // Schwarzer Bildrand (Overscan) + 1 Texel Sicherheitsrand an allen Kanten.
            vec2 lo = vec2(uBorder.z, uBorder.x) + 1.0;
            vec2 hi = uFrameSize - vec2(uBorder.w, uBorder.y) - 1.0;
            if (any(lessThan(texel, lo)) || any(greaterThan(texel, hi))) hide = true;
            if (uKeyMode >= 2 && texel.y > uKeyRows.x && texel.y < uKeyRows.y) hide = true;
            if (uKeyMode == 3 && texel.y >= uKeyRows.y) {
                for (int i = 0; i < 4; ++i)
                    if (i < uGroundCount && sameColor(raw, uGround[i])) hide = true;
            }
            if (hide) alpha = 0.0;
        }
        frag = vec4(clamp(c, 0.0, 1.0) * alpha, alpha);
    } else {
        frag = vec4(uColor, 1.0);
    }
})";

GLuint compileShader(GLenum type, const char* src, const char* what) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Shader-Fehler (%s): %s", what, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Programm aus kVertex + kFragment; hd = mit „#define XRSHELL_HD“ hinter der #version-Zeile.
GLuint buildProgram(bool hd) {
    std::string fragment = kFragment;
    if (hd) fragment.insert(fragment.find('\n') + 1, "#define XRSHELL_HD 1\n");
    const char* what = hd ? "HD" : "Basis";
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertex, what);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragment.c_str(), what);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("Program-Link fehlgeschlagen (%s)", what);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

void QuadRenderer::Locs::query(GLuint program) {
    mvp = glGetUniformLocation(program, "uMvp");
    tex = glGetUniformLocation(program, "uTex");
    mode = glGetUniformLocation(program, "uMode");
    uvScale = glGetUniformLocation(program, "uUvScale");
    uvOffset = glGetUniformLocation(program, "uUvOffset");
    color = glGetUniformLocation(program, "uColor");
    texSize = glGetUniformLocation(program, "uTexSize");
    frameSize = glGetUniformLocation(program, "uFrameSize");
    filter = glGetUniformLocation(program, "uFilter");
    sat = glGetUniformLocation(program, "uSat");
    contrast = glGetUniformLocation(program, "uContrast");
    keyMode = glGetUniformLocation(program, "uKeyMode");
    keyColor = glGetUniformLocation(program, "uKeyColor");
    keyRows = glGetUniformLocation(program, "uKeyRows");
    ground = glGetUniformLocation(program, "uGround");
    groundCount = glGetUniformLocation(program, "uGroundCount");
    border = glGetUniformLocation(program, "uBorder");
}

bool QuadRenderer::init() {
    mProgram[0] = buildProgram(false);
    if (!mProgram[0]) return false;
    mLoc[0].query(mProgram[0]);
    // HD-Filter (xBR) als eigenes Programm: scheitert es, laufen HD-Werte als Scharf weiter.
    mProgram[1] = buildProgram(true);
    if (mProgram[1]) mLoc[1].query(mProgram[1]);
    else LOGE("HD-Filter (xBR) nicht verfügbar – Rückfall auf Scharf");
    glGenBuffers(1, &mScreenVbo);

    // Ein Quad (Pos + UV), Zentrum im Ursprung, Einheitsgröße (Skalierung in MVP)
    const float quad[] = {
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, 0.0f,  1.0f, 1.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,
    };
    glGenBuffers(1, &mVbo);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    // Bildschirm-Textur: 1024×768 RGBA — Ziel der Core-Frames (M1a).
    // Das Testmuster (Schachbrett + Rand) bleibt als Fallback stehen, solange
    // kein Core-Frame per glTexSubImage2D überschrieben wird.
    // Puffer auf dem Heap (#9): 3 MB sprengen den ~1-MB-Stack des android_main-Threads.
    const int W = kTextureW, H = kTextureH;
    std::vector<uint8_t> pixels(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int cell = 64;
            bool white = ((x / cell) + (y / cell)) % 2 == 0;
            const bool border = x < 8 || y < 8 || x >= W - 8 || y >= H - 8;
            uint8_t* p = &pixels[(y * W + x) * 4];
            p[0] = border ? 255 : (white ? 220 : 40);
            p[1] = border ? 140 : (white ? 220 : 40);
            p[2] = border ? 20  : (white ? 220 : 60);
            p[3] = 255;
        }
    }
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    LOGI("Renderer initialisiert");
    return true;
}

void QuadRenderer::shutdown() {
    if (mTexture) glDeleteTextures(1, &mTexture);
    if (mVbo) glDeleteBuffers(1, &mVbo);
    if (mScreenVbo) glDeleteBuffers(1, &mScreenVbo);
    for (GLuint& program : mProgram) {
        if (program) glDeleteProgram(program);
        program = 0;
    }
    mTexture = mVbo = mScreenVbo = 0;
}

void QuadRenderer::updateScreenMesh(const ScreenState& screen) {
    if (screen.width == mMeshW && screen.height == mMeshH && screen.curveRadius == mMeshR)
        return;
    mMeshW = screen.width;
    mMeshH = screen.height;
    mMeshR = screen.curveRadius;
    // Relativ zur Bildmitte (Modellmatrix verschiebt nur). Flach: eine Spalte.
    const int columns = screen.curveRadius > 0.0f ? 64 : 1;
    const ScreenState local = localScreen(screen);
    std::vector<float> v;
    v.reserve(static_cast<size_t>(columns) * 6 * 5);
    auto push = [&](float u, float vv) {
        float p[3];
        screenPoint(local, u, vv, p);
        v.insert(v.end(), { p[0], p[1], p[2], u, vv });
    };
    for (int c = 0; c < columns; ++c) {
        const float u0 = static_cast<float>(c) / columns;
        const float u1 = static_cast<float>(c + 1) / columns;
        push(u0, 1.0f); push(u1, 1.0f); push(u1, 0.0f);
        push(u0, 1.0f); push(u1, 0.0f); push(u0, 0.0f);
    }
    glBindBuffer(GL_ARRAY_BUFFER, mScreenVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(),
                 GL_DYNAMIC_DRAW);
    mScreenVertices = static_cast<GLsizei>(v.size() / 5);
}

void QuadRenderer::bindVertices(GLuint vbo) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
}

// Programm, Textur, Filter, Farbe und Umgebungs-Key setzen (Augenbild und Ebene).
void QuadRenderer::setScreenUniforms() {
    // HD-Filter im eigenen Programm; ohne es (Kompilierfehler) der einfache Ersatzfilter.
    const bool hd = isHdFilter(mFilter) && mProgram[1];
    mCur = hd ? 1 : 0;
    glUseProgram(mProgram[mCur]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glUniform1i(L().tex, 0);
    glUniform2f(L().texSize, static_cast<float>(kTextureW), static_cast<float>(kTextureH));
    glUniform2f(L().frameSize, mUvScaleU * kTextureW, mUvScaleV * kTextureH);
    glUniform1i(L().filter, static_cast<int>(hd ? mFilter : withoutHd(mFilter)));
    glUniform1f(L().sat, mSaturation);
    glUniform1f(L().contrast, mContrast);
    glUniform1i(L().keyMode, mKeyMode);
    auto rgb = [](uint32_t c, float* out) {
        out[0] = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(c & 0xFF) / 255.0f;
    };
    float sky[3], ground[4 * 3];
    rgb(mKey.sky, sky);
    for (int i = 0; i < 4; ++i) rgb(mKey.ground[i], ground + i * 3);
    glUniform3fv(L().keyColor, 1, sky);
    glUniform2f(L().keyRows, mKey.skyEnd, mKey.horizon);
    glUniform3fv(L().ground, 4, ground);
    glUniform1i(L().groundCount, mKey.groundCount);
    glUniform4f(L().border, mKey.border[0], mKey.border[1], mKey.border[2], mKey.border[3]);
}

// Texturausschnitt des Bildschirms: genutzter Frame (#11), bei Bildzoom davon der
// Ausschnitt [mZoomU, mZoomU + 1/zoom] (nur Core-Frames). Ebene und Netz gleich.
void QuadRenderer::setScreenUv() {
    const float zoom = mCoreFrame ? mZoom : 1.0f;
    glUniform2f(L().uvScale, mCoreFrame ? mUvScaleU / zoom : 1.0f,
                mCoreFrame ? mUvScaleV / zoom : 1.0f);
    glUniform2f(L().uvOffset, mCoreFrame ? mZoomU * mUvScaleU : 0.0f,
                mCoreFrame ? mZoomV * mUvScaleV : 0.0f);
}

void QuadRenderer::drawScreenLayer() {
    // Vollbild-Pass in das (bereits gebundene) Ebenen-FBO; Viewport setzt der Aufrufer.
    // Hintergrund Alpha 0: ausgeblendete Umgebung lässt Passthrough durch.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    setScreenUniforms();
    bindVertices(mVbo);
    // Einheits-Quad (±0,5) → Normalized Device Coordinates (±1).
    const float mvp[16] = { 2, 0, 0, 0,  0, 2, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    glUniformMatrix4fv(L().mvp, 1, GL_FALSE, mvp);
    glUniform1i(L().mode, mCoreFrame ? 2 : 0);
    setScreenUv();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glUniform2f(L().uvOffset, 0.0f, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void QuadRenderer::draw(const float* eyeView, const float* eyeProj,
                        const ScreenState& screen,
                        const XrPosef* aimPose, bool aimValid, float laserLength) {
    // Mit Passthrough oder Bildschirm-Ebene: Hintergrund Alpha 0 — der Compositor
    // zeigt dort Kamera bzw. Spielbild (Ebene liegt unter dem Augenbild).
    if (mTransparent || mScreenInLayer) glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    else glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    setScreenUniforms();
    glDisable(GL_CULL_FACE);   // Quads von beiden Seiten sichtbar

    // --- Bildschirm (Netz, flach oder gewölbt) — entfällt, wenn er als Ebene läuft ---
    if (!mScreenInLayer) {
        updateScreenMesh(screen);
        bindVertices(mScreenVbo);
        float model[16];
        screenModelMatrix(screen, model);
        float mv[16], mvp[16];
        mat4Mul(mv, const_cast<float*>(eyeView), model);
        mat4Mul(mvp, const_cast<float*>(eyeProj), mv);
        glUniformMatrix4fv(L().mvp, 1, GL_FALSE, mvp);
        glUniform1i(L().mode, mCoreFrame ? 2 : 0);
        setScreenUv();
        glDrawArrays(GL_TRIANGLES, 0, mScreenVertices);
    }
    bindVertices(mVbo);   // Einheits-Quad für Cursor und Laser
    glUniform2f(L().uvOffset, 0.0f, 0.0f);

    // Aufnahme-Punkt: rotes Quadrat 4 cm groß, oben links 2 cm vor der Bildfläche.
    if (mRecordingDot) {
        const ScreenState local = localScreen(screen);
        float p[3];
        screenPoint(local, 0.03f, 0.05f, p);
        float nx = 0.0f, nz = 1.0f;
        if (screen.curveRadius > 0.0f) {
            const float a = (0.03f - 0.5f) * screen.width / screen.curveRadius;
            nx = -std::sin(a);
            nz = std::cos(a);
        }
        const float size = 0.04f;
        const float dotLocal[16] = { size, 0, 0, 0,  0, size, 0, 0,  0, 0, 1, 0,
                                     p[0] + nx * 0.02f, p[1], p[2] + nz * 0.02f, 1 };
        float screenModel[16], model[16], mv[16], mvp[16];
        screenModelMatrix(screen, screenModel);
        mat4Mul(model, screenModel, dotLocal);
        mat4Mul(mv, eyeView, model);
        mat4Mul(mvp, eyeProj, mv);
        const float red[3] = { 0.92f, 0.08f, 0.08f };
        glUniform3fv(L().color, 1, red);
        glUniform2f(L().uvScale, 1.0f, 1.0f);
        glUniformMatrix4fv(L().mvp, 1, GL_FALSE, mvp);
        glUniform1i(L().mode, 1);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glUniform3fv(L().color, 1, mPointerColor);

    // --- Cursor + Laser (nur wenn Controller getrackt) ---
    if (aimValid && aimPose) {
        const XrPosef& aim = *aimPose;
        const float* o = &aim.position.x;

        // Hit-Test jetzt zentral in raycastToScreen() (Issue #1) — draw() nutzt
        // denselben Code wie die Logik in main.cpp.
        const RayHit hit = raycastToScreen(screen, aim);
        float mv[16], mvp[16];

        // Cursor als kleines Quad auf der Bildfläche, 1 cm davor (lokale Normale
        // zeigt zur Zylinderachse bzw. +Z bei flach), dreht mit dem Bildschirm.
        if (hit.hit && mMarkerSize > 0.0f) {
            const float cs = mMarkerSize;
            const ScreenState local = localScreen(screen);
            float p[3];
            screenPoint(local, hit.u, hit.v, p);
            float nx = 0.0f, nz = 1.0f;
            if (screen.curveRadius > 0.0f) {
                const float a = (hit.u - 0.5f) * screen.width / screen.curveRadius;
                nx = -std::sin(a);
                nz = std::cos(a);
            }
            const float cursorLocal[16] = { cs, 0, 0, 0,  0, cs, 0, 0,  0, 0, 1, 0,
                                            p[0] + nx * 0.01f, p[1], p[2] + nz * 0.01f, 1 };
            float screenModel[16], model[16];
            screenModelMatrix(screen, screenModel);
            mat4Mul(model, screenModel, cursorLocal);
            mat4Mul(mv, eyeView, model);
            mat4Mul(mvp, eyeProj, mv);
            glUniformMatrix4fv(L().mvp, 1, GL_FALSE, mvp);
            glUniform2f(L().uvScale, 1.0f, 1.0f);
            glUniform1i(L().mode, 1);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // Laser (#3): zwei gekreuzte schmale Bänder entlang der Zielrichtung,
        // vom Controller bis zum Treffer — von jeder Seite als Linie sichtbar.
        float dir[3];
        aimForward(aim.orientation, dir);
        const float len = std::fmin(std::fmax(laserLength, 0.05f), mRayMaxLength);
        float side[3] = { -dir[2], 0.0f, dir[0] };            // dir × (0,1,0)
        float sl = std::sqrt(side[0] * side[0] + side[2] * side[2]);
        if (sl < 1e-4f) { side[0] = 1.0f; side[2] = 0.0f; sl = 1.0f; }   // senkrecht
        side[0] /= sl; side[2] /= sl;
        const float up[3] = { side[1] * dir[2] - side[2] * dir[1],
                              side[2] * dir[0] - side[0] * dir[2],
                              side[0] * dir[1] - side[1] * dir[0] };
        const float thickness = mRayThickness;
        glUniform1i(L().mode, 1);
        const float* const acrossList[2] = { side, up };
        const int bands = thickness > 0.0f ? 2 : 0;   // Strahl „Aus“: nur Zielpunkt
        for (int b = 0; b < bands; ++b) {
            const float* across = acrossList[b];
            const float lm[16] = { across[0] * thickness, across[1] * thickness, across[2] * thickness, 0,
                                   dir[0] * len, dir[1] * len, dir[2] * len, 0,
                                   0, 0, 0, 0,
                                   o[0] + dir[0] * len * 0.5f,
                                   o[1] + dir[1] * len * 0.5f,
                                   o[2] + dir[2] * len * 0.5f, 1 };
            mat4Mul(mv, eyeView, lm);
            mat4Mul(mvp, eyeProj, mv);
            glUniformMatrix4fv(L().mvp, 1, GL_FALSE, mvp);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    glEnable(GL_CULL_FACE);
}

// Raycast gegen eine achsenparallele Quad-Ebene (Mittelpunkt in Z = planeZ,
// v = 0 oben — gleiche UV-Konvention wie der Bildschirm). #5 nutzt das für
// das Tastatur-Overlay; raycastToScreen bleibt die Screen-Spezialisierung.
RayHit raycastToQuad(float centerX, float centerY, float planeZ,
                     float width, float height, const XrPosef& aimPose) {
    RayHit result{};

    // Blickrichtung aus der Aim-Orientierung (-Z-Achse der Controller-Pose)
    float dir[3];
    aimForward(aimPose.orientation, dir);
    const float* o = &aimPose.position.x;

    // Strahl ↔ Bildebene in Z = planeZ (Quad steht achsenparallel)
    if (std::fabs(dir[2]) < 1e-5f) return result;   // parallel zur Ebene

    const float t = (planeZ - o[2]) / dir[2];
    if (t <= 0.0f) return result;                   // Ebene hinter dem Controller

    const float hx = o[0] + dir[0] * t;
    const float hy = o[1] + dir[1] * t;
    const float u = (hx - (centerX - width * 0.5f)) / width;
    const float v = 1.0f - (hy - (centerY - height * 0.5f)) / height;

    result.u = u;
    result.v = v;
    result.distance = t;
    result.hit = (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f);
    return result;
}

void mat4Multiply(float* out, const float* a, const float* b) { mat4Mul(out, a, b); }

ScreenState localScreen(const ScreenState& screen) {
    ScreenState local = screen;
    local.posX = local.posY = local.posZ = 0.0f;
    local.yaw = 0.0f;
    return local;
}

void screenModelMatrix(const ScreenState& screen, float* m) {
    // Drehung um Y: lokal (x, z) → Welt (c·x + s·z, −s·x + c·z), dann verschieben.
    const float c = std::cos(screen.yaw), s = std::sin(screen.yaw);
    const float model[16] = { c, 0, -s, 0,
                              0, 1, 0, 0,
                              s, 0, c, 0,
                              screen.posX, screen.posY, screen.posZ, 1 };
    for (int i = 0; i < 16; ++i) m[i] = model[i];
}

XrPosef worldToScreenLocal(const ScreenState& screen, const XrPosef& pose) {
    // Position: verschieben, dann um −yaw drehen. Orientierung: q' = q(−yaw) · q.
    const float c = std::cos(screen.yaw), s = std::sin(screen.yaw);
    const float dx = pose.position.x - screen.posX, dz = pose.position.z - screen.posZ;
    XrPosef local = pose;
    local.position.x = c * dx - s * dz;
    local.position.y = pose.position.y - screen.posY;
    local.position.z = s * dx + c * dz;
    const float hs = std::sin(-screen.yaw * 0.5f), hc = std::cos(-screen.yaw * 0.5f);
    const XrQuaternionf& q = pose.orientation;   // (0, hs, 0, hc) · q
    local.orientation.x = hc * q.x + hs * q.z;
    local.orientation.y = hc * q.y + hs * q.w;
    local.orientation.z = hc * q.z - hs * q.x;
    local.orientation.w = hc * q.w - hs * q.y;
    return local;
}

void screenPoint(const ScreenState& screen, float u, float v, float* out) {
    const float s = (u - 0.5f) * screen.width;
    float x = s, z = 0.0f;
    if (screen.curveRadius > 0.0f) {
        const float a = s / screen.curveRadius;   // Bogenlänge → Winkel
        x = screen.curveRadius * std::sin(a);
        z = screen.curveRadius * (1.0f - std::cos(a));
    }
    const float y = (0.5f - v) * screen.height;
    const float c = std::cos(screen.yaw), sn = std::sin(screen.yaw);
    out[0] = screen.posX + c * x + sn * z;
    out[1] = screen.posY + y;
    out[2] = screen.posZ - sn * x + c * z;
}

RayHit raycastToScreen(const ScreenState& screen, const XrPosef& aimPose) {
    // In Bildschirm-Koordinaten rechnen: Mitte im Ursprung, Bild schaut nach +Z.
    const XrPosef local = worldToScreenLocal(screen, aimPose);
    const float r = screen.curveRadius;
    if (r <= 0.0f) return raycastToQuad(0.0f, 0.0f, 0.0f, screen.width, screen.height, local);

    // Gewölbt: Strahl gegen den senkrechten Zylinder (Achse bei z = r).
    // Betrachter und Controller stehen innerhalb → die fernere Lösung ist die Bildfläche.
    RayHit result{};
    float d[3];
    aimForward(local.orientation, d);
    const float* o = &local.position.x;
    const float oz = o[2] - r;
    const float a = d[0] * d[0] + d[2] * d[2];
    if (a < 1e-8f) return result;               // senkrecht nach oben/unten
    const float b = 2.0f * (o[0] * d[0] + oz * d[2]);
    const float c = o[0] * o[0] + oz * oz - r * r;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return result;
    const float t = (-b + std::sqrt(disc)) / (2.0f * a);
    if (t <= 0.0f) return result;
    const float hx = o[0] + d[0] * t, hy = o[1] + d[1] * t, hz = o[2] + d[2] * t;
    const float angle = std::atan2(hx, -(hz - r));   // 0 = Bildmitte
    result.u = angle * r / screen.width + 0.5f;
    result.v = 0.5f - hy / screen.height;
    result.distance = t;
    result.hit = result.u >= 0.0f && result.u <= 1.0f && result.v >= 0.0f && result.v <= 1.0f;
    return result;
}
