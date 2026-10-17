// XRShell — Ladeanzeige, Implementierung. Aufbau und Thread-Regeln: load_screen.h.

#include "load_screen.h"
#include "pixel_font.h"
#include "quad_renderer.h"   // mat4Multiply

#include <android/log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// --- Raster (Texel, Tafel 1280×800) ---------------------------------------------------------
constexpr int kMargin = 56;
constexpr int kTopH = 348;             // obere Zone: Kopf, Überschrift, Balken, Restzeit, Hinweis
constexpr int kColY = 352;             // Spalten (Schritte | Systemcheck)
constexpr int kColSplit = 640;
constexpr int kStepRowH = 46, kCheckRowH = 38;
constexpr int kFooterY1 = 704, kFooterY2 = 740;
constexpr float kAppPx = 54.0f, kGamePx = 34.0f, kHeadPx = 44.0f, kTextPx = 30.0f, kSmallPx = 27.0f;

constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return r | (g << 8) | (b << 16) | (a << 24);
}
// Farben wie das VR-Menü (Glas-Tafel, Akzent Orange).
constexpr uint32_t kPanelTop = rgba(27, 30, 42, 246), kPanelBottom = rgba(12, 13, 19, 246);
constexpr uint32_t kHeaderTop = rgba(40, 44, 60, 252), kHeaderBottom = rgba(27, 30, 42, 252);
constexpr uint32_t kAccent = rgba(255, 146, 38, 255);
constexpr uint32_t kAccentSoft = rgba(255, 146, 38, 150);
constexpr uint32_t kTrack = rgba(8, 9, 13, 255);
constexpr uint32_t kText = rgba(240, 240, 244, 255);
constexpr uint32_t kTextDim = rgba(140, 144, 156, 255);
constexpr uint32_t kOk = rgba(70, 180, 104, 255);
constexpr uint32_t kWarn = rgba(244, 196, 48, 255);
constexpr uint32_t kFail = rgba(232, 72, 64, 255);
constexpr uint32_t kFailSoft = rgba(232, 72, 64, 160);
constexpr uint32_t kGrey = rgba(96, 100, 114, 255);
constexpr uint32_t kWhite = rgba(255, 255, 255, 255);

uint32_t over(uint32_t color, uint32_t dst, uint32_t cov) {
    const uint32_t a = ((color >> 24) & 0xFF) * cov / 255;
    if (a == 0) return dst;
    if (a == 255) return color | 0xFF000000u;
    const uint32_t inv = 255 - a;
    const uint32_t r = ((color & 0xFF) * a + (dst & 0xFF) * inv) / 255;
    const uint32_t g = (((color >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * inv) / 255;
    const uint32_t b = (((color >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * inv) / 255;
    const uint32_t outA = a + ((dst >> 24) & 0xFF) * inv / 255;
    return r | (g << 8) | (b << 16) | (std::min(outA, 255u) << 24);
}

uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    t = std::min(std::max(t, 0.0f), 1.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        const float ca = static_cast<float>((a >> s) & 0xFF), cb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

const char* kVertex = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main() { vUv = aUv; gl_Position = uMvp * vec4(aPos, 1.0); })";

// Lichtpunkt im Fortschrittsbalken (uBar = u0, v0, u1, v1; uPulse < 0 = aus).
const char* kFragment = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
uniform vec4 uBar;
uniform float uPulse;
out vec4 frag;
void main() {
    vec4 c = texture(uTex, vUv);
    if (uPulse >= 0.0 && vUv.x >= uBar.x && vUv.x <= uBar.z && vUv.y >= uBar.y && vUv.y <= uBar.w) {
        float x = mix(uBar.x, uBar.z, uPulse);
        float d = (vUv.x - x) / (uBar.z - uBar.x) * 16.0;
        c.rgb = mix(c.rgb, vec3(1.0, 0.93, 0.8), exp(-d * d) * 0.85);
    }
    frag = c;
})";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Ladeanzeige-Shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// UTF-8: Byte-Länge des letzten Zeichens (für „…“-Kürzen).
size_t lastCharBytes(const std::string& s) {
    size_t n = 1;
    while (n < s.size() && (static_cast<unsigned char>(s[s.size() - n]) & 0xC0) == 0x80) ++n;
    return std::min(n, s.size());
}

} // namespace

// ==============================================================================
// GL
// ==============================================================================

bool LoadScreen::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVertex);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment);
    if (!vs || !fs) return false;
    mProgram = glCreateProgram();
    glAttachShader(mProgram, vs);
    glAttachShader(mProgram, fs);
    glLinkProgram(mProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &ok);
    if (!ok) { LOGE("Ladeanzeige: Link fehlgeschlagen"); return false; }
    mLocMvp = glGetUniformLocation(mProgram, "uMvp");
    mLocTex = glGetUniformLocation(mProgram, "uTex");
    mLocBar = glGetUniformLocation(mProgram, "uBar");
    mLocPulse = glGetUniformLocation(mProgram, "uPulse");

    const float quad[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,   0.5f, -0.5f, 0.0f, 1.0f, 1.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 0.0f,  -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 0.0f,  -0.5f,  0.5f, 0.0f, 0.0f, 0.0f,
    };
    glGenBuffers(1, &mVbo);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);   // Text flimmert sonst
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mPixels.assign(static_cast<size_t>(kTexW) * kTexH, 0);
    mBase = mPixels;
    mFont.load();
    mFontBold.load(true);
    LOGI("Ladeanzeige bereit (%s)", mFont.ok() ? "Systemschrift" : "Pixelschrift");
    return true;
}

void LoadScreen::shutdown() {
    if (mTexture) glDeleteTextures(1, &mTexture);
    if (mVbo) glDeleteBuffers(1, &mVbo);
    if (mProgram) glDeleteProgram(mProgram);
    mTexture = mVbo = mProgram = 0;
    mTextureReady = false;
}

void LoadScreen::update(const LoadView& view, double now) {
    if (!mHasView || !view.sameBody(mView)) {
        mView = view;
        mHasView = true;
        redrawAll();
        mLastTop = now;
        return;
    }
    if (view.sameTop(mView)) return;
    // Weiche Änderung (Laufzeit, Restzeit, Balken): höchstens 4×/s, Überschrift/Hinweis sofort.
    const bool urgent = view.headline != mView.headline || view.notice != mView.notice ||
                        view.errorTitle != mView.errorTitle || view.etaWarn != mView.etaWarn;
    if (!urgent && now - mLastTop < 0.25) return;
    mView = view;
    mLastTop = now;
    redrawTop();
}

void LoadScreen::upload(int y0, int y1) {
    if (!mTexture) return;
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (!mTextureReady || (y0 <= 0 && y1 >= kTexH)) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTexW, kTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
        mTextureReady = true;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, kTexW, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE,
                        mPixels.data() + static_cast<size_t>(y0) * kTexW);
    }
    glGenerateMipmap(GL_TEXTURE_2D);
}

void LoadScreen::redrawAll() {
    drawBackground();
    mBase = mPixels;
    drawTop();
    drawBody();
    upload(0, kTexH);
}

void LoadScreen::redrawTop() {
    std::copy(mBase.begin(), mBase.begin() + static_cast<size_t>(kTopH) * kTexW, mPixels.begin());
    drawTop();
    upload(0, kTopH);
}

float LoadScreen::pulseAt(double timeS) {
    const double period = 1.4;   // eine Wanderung je 1,4 s: sichtbare Bewegung in jedem Frame
    const double f = timeS / period - std::floor(timeS / period);
    return static_cast<float>(f);
}

void LoadScreen::draw(const float* localView, const float* eyeProj, float screenW, float screenH, double timeS) {
    if (!mProgram || !mTextureReady) return;
    // In die Bildfläche eingepasst, gleiche Mitte wie das Spielbild.
    float w = std::min(screenW, screenH * kAspect);
    float h = w / kAspect;
    if (w <= 0.01f) { w = 1.6f; h = 1.0f; }
    glUseProgram(mProgram);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glUniform1i(mLocTex, 0);
    glUniform4f(mLocBar, static_cast<float>(kBarX0) / kTexW, static_cast<float>(kBarY0) / kTexH,
                static_cast<float>(kBarX1) / kTexW, static_cast<float>(kBarY1) / kTexH);
    const bool pulse = mView.mode == LoadView::kLoading && mView.progress >= 0.0f;
    glUniform1f(mLocPulse, pulse ? pulseAt(timeS) : -1.0f);
    const float model[16] = { w, 0, 0, 0,  0, h, 0, 0,  0, 0, 1, 0,  0, 0, 0.004f, 1 };
    float mv[16], mvp[16];
    mat4Multiply(mv, localView, model);
    mat4Multiply(mvp, eyeProj, mv);
    glUniformMatrix4fv(mLocMvp, 1, GL_FALSE, mvp);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);            // der Strahl vor der Tafel bleibt sichtbar
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

// ==============================================================================
// Inhalt
// ==============================================================================

void LoadScreen::drawBackground() {
    std::fill(mPixels.begin(), mPixels.end(), 0u);
    const bool error = mView.mode == LoadView::kError;
    roundRect(0, 0, kTexW, kTexH, 40, error ? kFailSoft : kAccentSoft);   // Rand
    roundRectV(4, 4, kTexW - 4, kTexH - 4, 36, kPanelTop, kPanelBottom);
    roundRectV(4, 4, kTexW - 4, 120, 36, kHeaderTop, kHeaderBottom);
    fillRect(4, 84, kTexW - 4, 120, kHeaderBottom);   // Kopf unten gerade
    fillRect(kMargin, 120, kTexW - kMargin, 122, error ? kFail : kAccent);
}

void LoadScreen::drawTop() {
    const LoadView& v = mView;
    const bool error = v.mode == LoadView::kError;
    // Kopf: App-Name, Spiel, Laufzeit
    text(v.app, kMargin, 34, kAppPx, error ? kFail : kAccent, true);
    int x = kMargin + textWidth(v.app, kAppPx, true) + 28;
    const int elapsedW = v.elapsed.empty() ? 0 : textWidth(v.elapsed, kTextPx);
    if (!v.elapsed.empty()) text(v.elapsed, kTexW - kMargin - elapsedW, 48, kTextPx, kTextDim);
    if (!v.game.empty())
        text(ellipsize(v.game, kTexW - kMargin - elapsedW - 30 - x, kGamePx, true), x, 50, kGamePx, kText, true);

    // Überschrift
    text(ellipsize(v.headline, kTexW - 2 * kMargin, kHeadPx, true), kMargin, 150, kHeadPx,
         error ? kFail : kText, true);

    if (error) {
        text(ellipsize(v.errorTitle, kTexW - 2 * kMargin, kTextPx + 4, true), kMargin, 218, kTextPx + 4, kText, true);
        const std::vector<std::string> lines = wrap(v.errorDetail, kTexW - 2 * kMargin, kSmallPx, 2);
        for (size_t i = 0; i < lines.size(); ++i)
            text(lines[i], kMargin, 262 + static_cast<int>(i) * 34, kSmallPx, kTextDim);
    } else if (v.progress >= 0.0f) {
        // Balken mit Füllung; der Lichtpunkt kommt aus dem Shader.
        roundRect(kBarX0, kBarY0, kBarX1, kBarY1, 14, kTrack);
        const int fill = kBarX0 + static_cast<int>(std::min(std::max(v.progress, 0.0f), 1.0f) * (kBarX1 - kBarX0));
        if (fill > kBarX0 + 6) roundRectV(kBarX0, kBarY0, fill, kBarY1, 14, rgba(255, 176, 84, 255), kAccent);
        text(ellipsize(v.eta, kTexW - 2 * kMargin, kTextPx), kMargin, 262, kTextPx, v.etaWarn ? kWarn : kText);
    } else if (!v.eta.empty()) {
        text(ellipsize(v.eta, kTexW - 2 * kMargin, kTextPx), kMargin, 226, kTextPx, kTextDim);
    }
    if (!v.notice.empty()) {
        const int y = 304;
        triangle(kMargin, y + 30, kMargin + 30, y + 30, kMargin + 15, y + 2, kWarn);
        fillRect(kMargin + 14, y + 11, kMargin + 17, y + 22, rgba(20, 20, 20, 255));
        fillRect(kMargin + 14, y + 25, kMargin + 17, y + 28, rgba(20, 20, 20, 255));
        text(ellipsize(v.notice, kTexW - 2 * kMargin - 44, kSmallPx), kMargin + 44, y + 2, kSmallPx, kWarn);
    }
}

void LoadScreen::drawBody() {
    const LoadView& v = mView;
    if (v.mode == LoadView::kError) {
        drawError(kColY);
    } else {
        if (v.mode == LoadView::kLoading) drawSteps(kMargin, kColSplit - 20, kColY);
        else drawInfo(kMargin, kColSplit - 20, kColY);
        text("Systemcheck", kColSplit + 20, kColY, kSmallPx, kTextDim, true);
        drawChecks(kColSplit + 20, kTexW - kMargin, kColY + 40, 8, false);
    }
    drawFooter();
}

void LoadScreen::drawSteps(int x0, int x1, int y) {
    text("Schritte", x0, y, kSmallPx, kTextDim, true);
    y += 40;
    for (const LoadView::StepLine& s : mView.steps) {
        const bool active = s.state == loadst::StepState::Active;
        if (active) roundRect(x0 - 10, y - 6, x1 + 6, y + kStepRowH - 10, 14, rgba(62, 82, 122, 200));
        icon(x0 + 16, y + 16, s.state);
        const uint32_t ink = s.state == loadst::StepState::Pending ? kTextDim
                             : s.state == loadst::StepState::Failed ? kFail : kText;
        const int timeW = s.time.empty() ? 0 : textWidth(s.time, kSmallPx);
        text(ellipsize(s.name, x1 - x0 - 50 - timeW - 16, kTextPx, active), x0 + 44, y, kTextPx, ink, active);
        if (!s.time.empty()) text(s.time, x1 - timeW, y + 2, kSmallPx, active ? kAccent : kTextDim);
        y += kStepRowH;
    }
}

void LoadScreen::drawInfo(int x0, int x1, int y) {
    text("So geht's", x0, y, kSmallPx, kTextDim, true);
    y += 40;
    for (const std::string& line : mView.info) {
        if (y > kFooterY1 - 50) break;
        circle(x0 + 8, y + 16, 4, kAccent);
        for (const std::string& part : wrap(line, x1 - x0 - 24, kTextPx - 2, 2)) {
            text(part, x0 + 24, y, kTextPx - 2, kText);
            y += 38;
        }
        y += 8;
    }
}

void LoadScreen::drawChecks(int x0, int x1, int y, int maxRows, bool onlyProblems) {
    int rows = 0;
    for (const loadst::CheckLine& c : mView.checks) {
        if (onlyProblems && c.level == loadst::Level::Ok) continue;
        if (rows++ >= maxRows) break;
        checkIcon(x0 + 14, y + 15, c.level);
        const int labelW = textWidth(c.label, kSmallPx, true);
        text(c.label, x0 + 40, y, kSmallPx, kText, true);
        const uint32_t ink = c.level == loadst::Level::Warn ? kWarn : c.level == loadst::Level::Fail ? kFail : kTextDim;
        text(ellipsize(c.detail, x1 - (x0 + 52 + labelW), kSmallPx), x0 + 52 + labelW, y, kSmallPx, ink);
        y += kCheckRowH;
    }
}

void LoadScreen::drawError(int y) {
    const LoadView& v = mView;
    const int x0 = kMargin, x1 = kTexW - kMargin;
    text("Was tun", x0, y, kSmallPx, kTextDim, true);
    y += 40;
    for (const std::string& r : v.remedy) {
        if (y > 500) break;
        circle(x0 + 8, y + 16, 4, kFail);
        for (const std::string& part : wrap(r, x1 - x0 - 24, kTextPx - 2, 2)) {
            text(part, x0 + 24, y, kTextPx - 2, kText);
            y += 36;
        }
        y += 6;
    }
    y = std::max(y + 8, 500);
    if (!v.logPath.empty()) {
        text("Log", x0, y, kSmallPx, kTextDim, true);
        text(ellipsize(v.logPath, x1 - x0 - 70, kSmallPx), x0 + 70, y, kSmallPx, kText);
        y += 36;
    }
    if (!v.logHint.empty()) {
        text(ellipsize(v.logHint, x1 - x0 - 70, kSmallPx), x0 + 70, y, kSmallPx, kTextDim);
        y += 36;
    }
    // Systemcheck: nur, was nicht in Ordnung ist (passt unter die Abhilfe).
    drawChecks(x0, x1, y + 8, std::max(0, (kFooterY1 - 8 - (y + 8)) / kCheckRowH), true);
}

void LoadScreen::drawFooter() {
    const LoadView& v = mView;
    fillRect(kMargin, kFooterY1 - 12, kTexW - kMargin, kFooterY1 - 10, rgba(255, 255, 255, 30));
    const std::string version = v.app + (v.version.empty() ? "" : " " + v.version);
    const int versionW = textWidth(version, kSmallPx);
    text(ellipsize(v.help1, kTexW - 2 * kMargin, kSmallPx), kMargin, kFooterY1, kSmallPx, kTextDim);
    text(ellipsize(v.help2, kTexW - 2 * kMargin - versionW - 30, kSmallPx), kMargin, kFooterY2, kSmallPx, kTextDim);
    text(version, kTexW - kMargin - versionW, kFooterY2, kSmallPx, kText);
}

void LoadScreen::icon(int cx, int cy, loadst::StepState state) {
    switch (state) {
    case loadst::StepState::Done:
        circle(cx, cy, 14, kOk);
        line(cx - 6.5f, cy + 0.5f, cx - 1.5f, cy + 5.5f, 3.4f, kWhite);
        line(cx - 1.5f, cy + 5.5f, cx + 7.0f, cy - 5.0f, 3.4f, kWhite);
        break;
    case loadst::StepState::Active:
        ring(cx, cy, 14, 3, kAccent);
        circle(cx, cy, 7, kAccent);
        break;
    case loadst::StepState::Failed:
        circle(cx, cy, 14, kFail);
        line(cx - 5.5f, cy - 5.5f, cx + 5.5f, cy + 5.5f, 3.4f, kWhite);
        line(cx - 5.5f, cy + 5.5f, cx + 5.5f, cy - 5.5f, 3.4f, kWhite);
        break;
    default:
        ring(cx, cy, 13, 2, kGrey);
        break;
    }
}

void LoadScreen::checkIcon(int cx, int cy, loadst::Level level) {
    switch (level) {
    case loadst::Level::Ok: icon(cx, cy, loadst::StepState::Done); break;
    case loadst::Level::Fail: icon(cx, cy, loadst::StepState::Failed); break;
    case loadst::Level::Warn:
        triangle(cx - 15, cy + 13, cx + 15, cy + 13, cx, cy - 14, kWarn);
        fillRect(cx - 2, cy - 5, cx + 2, cy + 5, rgba(20, 20, 20, 255));
        fillRect(cx - 2, cy + 7, cx + 2, cy + 10, rgba(20, 20, 20, 255));
        break;
    default: icon(cx, cy, loadst::StepState::Pending); break;
    }
}

// ==============================================================================
// Raster
// ==============================================================================

int LoadScreen::textWidth(const std::string& s, float px, bool bold) const {
    const TextRaster& font = bold && mFontBold.ok() ? mFontBold : mFont;
    if (font.ok()) return font.width(s.c_str(), px);
    const int scale = std::max(1, static_cast<int>(px / 6.0f));
    return static_cast<int>(s.size()) * 4 * scale - scale;
}

void LoadScreen::text(const std::string& s, int x, int y, float px, uint32_t color, bool bold) {
    if (s.empty()) return;
    const TextRaster& font = bold && mFontBold.ok() ? mFontBold : mFont;
    if (font.ok()) {
        font.draw(mPixels.data(), kTexW, kTexH, s.c_str(), x, y, px, color);
        return;
    }
    // Rückfall: 3×5-Pixelschrift (nur Großbuchstaben, keine Umlaute).
    const int scale = std::max(1, static_cast<int>(px / 6.0f));
    for (const char* c = s.c_str(); *c; ++c, x += 4 * scale) {
        const uint8_t* glyph = pixelfont::glyphFor(static_cast<char>(std::toupper(static_cast<unsigned char>(*c))));
        if (!glyph) continue;
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (glyph[row] & (1 << (2 - col)))
                    fillRect(x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale, color);
    }
}

std::string LoadScreen::ellipsize(const std::string& s, int maxW, float px, bool bold) const {
    if (maxW <= 0) return std::string();
    if (textWidth(s, px, bold) <= maxW) return s;
    std::string out = s;
    while (!out.empty() && textWidth(out + "…", px, bold) > maxW) out.erase(out.size() - lastCharBytes(out));
    return out + "…";
}

// Zeilenumbruch an Leerzeichen; die letzte erlaubte Zeile wird mit „…“ gekürzt.
std::vector<std::string> LoadScreen::wrap(const std::string& s, int maxW, float px, size_t maxLines) const {
    std::vector<std::string> lines;
    if (s.empty() || maxLines == 0) return lines;
    std::string current;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        const std::string word = s.substr(i, j - i);
        const std::string candidate = current.empty() ? word : current + " " + word;
        if (current.empty() || textWidth(candidate, px) <= maxW) {
            current = candidate;
        } else {
            lines.push_back(current);
            current = word;
            if (lines.size() == maxLines) break;
        }
        i = j + 1;
    }
    if (lines.size() < maxLines) {
        if (!current.empty()) lines.push_back(current);
    } else if (i < s.size() || !current.empty()) {
        lines.back() = ellipsize(lines.back() + " " + current + (i < s.size() ? " " + s.substr(i) : ""), maxW, px);
    }
    for (std::string& line : lines) line = ellipsize(line, maxW, px);
    return lines;
}

void LoadScreen::blend(int x, int y, uint32_t color, uint32_t cov) {
    if (x < 0 || y < 0 || x >= kTexW || y >= kTexH || cov == 0) return;
    uint32_t& dst = mPixels[static_cast<size_t>(y) * kTexW + x];
    dst = over(color, dst, std::min(cov, 255u));
}

void LoadScreen::fillRect(int x0, int y0, int x1, int y1, uint32_t color) {
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, kTexW); y1 = std::min(y1, kTexH);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) blend(x, y, color, 255);
}

void LoadScreen::roundRect(int x0, int y0, int x1, int y1, int r, uint32_t color) {
    roundRectV(x0, y0, x1, y1, r, color, color);
}

// Abgerundetes Rechteck, Farbe senkrecht verlaufend; Abstand nur in den Eckquadraten.
void LoadScreen::roundRectV(int x0, int y0, int x1, int y1, int r, uint32_t top, uint32_t bottom) {
    r = std::max(0, std::min(r, std::min(x1 - x0, y1 - y0) / 2));
    for (int y = std::max(y0, 0); y < std::min(y1, kTexH); ++y) {
        const uint32_t color = top == bottom ? top
            : lerpColor(top, bottom, static_cast<float>(y - y0) / static_cast<float>(std::max(y1 - y0 - 1, 1)));
        const bool cornerRow = y < y0 + r || y >= y1 - r;
        for (int x = std::max(x0, 0); x < std::min(x1, kTexW); ++x) {
            uint32_t cov = 255;
            if (cornerRow && (x < x0 + r || x >= x1 - r)) {
                const float cx = x < x0 + r ? static_cast<float>(x0 + r) - 0.5f : static_cast<float>(x1 - r) - 0.5f;
                const float cy = y < y0 + r ? static_cast<float>(y0 + r) - 0.5f : static_cast<float>(y1 - r) - 0.5f;
                const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float c = std::min(std::max(static_cast<float>(r) + 0.5f - d, 0.0f), 1.0f);
                cov = static_cast<uint32_t>(c * 255.0f + 0.5f);
            }
            blend(x, y, color, cov);
        }
    }
}

void LoadScreen::circle(int cx, int cy, int r, uint32_t color) {
    roundRect(cx - r, cy - r, cx + r, cy + r, r, color);
}

void LoadScreen::ring(int cx, int cy, int r, int width, uint32_t color) {
    for (int y = cy - r - 1; y <= cy + r + 1; ++y)
        for (int x = cx - r - 1; x <= cx + r + 1; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - static_cast<float>(cx);
            const float dy = static_cast<float>(y) + 0.5f - static_cast<float>(cy);
            const float d = std::fabs(std::sqrt(dx * dx + dy * dy) - (static_cast<float>(r) - width * 0.5f));
            const float c = std::min(std::max(width * 0.5f + 0.5f - d, 0.0f), 1.0f);
            blend(x, y, color, static_cast<uint32_t>(c * 255.0f + 0.5f));
        }
}

// Strecke mit runden Enden, geglättet über den Abstand zur Mittellinie.
void LoadScreen::line(float x0, float y0, float x1, float y1, float width, uint32_t color) {
    const float half = width * 0.5f;
    const int xa = static_cast<int>(std::floor(std::min(x0, x1) - half - 1)), xb = static_cast<int>(std::ceil(std::max(x0, x1) + half + 1));
    const int ya = static_cast<int>(std::floor(std::min(y0, y1) - half - 1)), yb = static_cast<int>(std::ceil(std::max(y0, y1) + half + 1));
    const float vx = x1 - x0, vy = y1 - y0;
    const float len2 = std::max(vx * vx + vy * vy, 1e-6f);
    for (int y = ya; y <= yb; ++y)
        for (int x = xa; x <= xb; ++x) {
            const float px = static_cast<float>(x) + 0.5f - x0, py = static_cast<float>(y) + 0.5f - y0;
            const float t = std::min(std::max((px * vx + py * vy) / len2, 0.0f), 1.0f);
            const float dx = px - t * vx, dy = py - t * vy;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float c = std::min(std::max(half + 0.5f - d, 0.0f), 1.0f);
            blend(x, y, color, static_cast<uint32_t>(c * 255.0f + 0.5f));
        }
}

// Gefülltes Dreieck (Warnzeichen), Kanten geglättet über den Abstand zur nächsten Kante.
void LoadScreen::triangle(float ax, float ay, float bx, float by, float cx, float cy, uint32_t color) {
    const float xs[3] = {ax, bx, cx}, ys[3] = {ay, by, cy};
    const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    const float sign = area < 0.0f ? -1.0f : 1.0f;
    const int xa = static_cast<int>(std::floor(std::min({ax, bx, cx}))) - 1, xb = static_cast<int>(std::ceil(std::max({ax, bx, cx}))) + 1;
    const int ya = static_cast<int>(std::floor(std::min({ay, by, cy}))) - 1, yb = static_cast<int>(std::ceil(std::max({ay, by, cy}))) + 1;
    for (int y = ya; y <= yb; ++y)
        for (int x = xa; x <= xb; ++x) {
            const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
            float minD = 1e9f;
            for (int e = 0; e < 3; ++e) {
                const float x0 = xs[e], y0 = ys[e], x1 = xs[(e + 1) % 3], y1 = ys[(e + 1) % 3];
                const float ex = x1 - x0, ey = y1 - y0;
                const float len = std::max(std::sqrt(ex * ex + ey * ey), 1e-6f);
                const float d = sign * ((px - x0) * ey - (py - y0) * ex) / len * -1.0f;   // > 0 innen
                minD = std::min(minD, d);
            }
            const float c = std::min(std::max(minD + 0.5f, 0.0f), 1.0f);
            blend(x, y, color, static_cast<uint32_t>(c * 255.0f + 0.5f));
        }
}
