// XRShell — VR-Menü, Implementierung. Siehe vr_menu.h.

#include "vr_menu.h"

#include "pixel_font.h"
#include "pointer_map.h"

#include <android/log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#define LOG_TAG "XRShell"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

// --- Tafel: Raster in vr_menu.h (VrMenu::kTitleH …), hier Schrift und Farben ----------
constexpr float kTextPx = 36.0f, kTitlePx = 44.0f, kSmallPx = 30.0f;
constexpr float kPanelWidth = 1.2f;            // Meter bei 1024 px Texturbreite

constexpr uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return r | (g << 8) | (b << 16) | (a << 24);
}
// Dunkles Glas mit leichtem Verlauf (oben heller), Kopfzeile abgesetzt.
constexpr uint32_t kPanelTop = rgba(27, 30, 42, 240), kPanelBottom = rgba(12, 13, 19, 240);
constexpr uint32_t kHeaderTop = rgba(40, 44, 60, 250), kHeaderBottom = rgba(27, 30, 42, 250);
constexpr uint32_t kField = rgba(9, 10, 15, 255);         // Suchfeld innen
constexpr uint32_t kFieldEdge = rgba(70, 76, 96, 255);
constexpr uint32_t kKey = rgba(46, 51, 66, 255);          // Tasten der Suchtastatur
constexpr uint32_t kKeySpecial = rgba(34, 38, 50, 255);
constexpr uint32_t kKeyEdge = rgba(255, 255, 255, 40);
constexpr uint32_t kAccent = rgba(255, 146, 38, 255);     // Orange
constexpr uint32_t kAccentSoft = rgba(255, 146, 38, 150);
constexpr uint32_t kRowTop = rgba(38, 42, 56, 240), kRowBottom = rgba(29, 32, 43, 240);       // Zeilenkarte
constexpr uint32_t kHoverTop = rgba(62, 82, 122, 252), kHoverBottom = rgba(44, 58, 90, 252);   // Fokus
constexpr uint32_t kTrack = rgba(10, 12, 17, 230);        // Kapseln, Segment-Schalter
constexpr uint32_t kKnob = rgba(62, 68, 86, 255);
constexpr uint32_t kStatusBg = rgba(46, 120, 76, 245);    // grüne Statusmeldung
constexpr uint32_t kTileViolet = rgba(112, 86, 196, 255);
constexpr uint32_t kTileRed = rgba(196, 64, 64, 255);
constexpr uint32_t kTileGreen = rgba(72, 150, 92, 255);
constexpr uint32_t kTileBlue = rgba(40, 96, 180, 255);
constexpr uint32_t kBadgeDos = rgba(64, 112, 180, 255);
constexpr uint32_t kText = rgba(240, 240, 244, 255);
constexpr uint32_t kTextDim = rgba(130, 134, 146, 255);
constexpr uint32_t kTextDark = rgba(17, 19, 26, 255);
constexpr uint32_t kWarn = rgba(255, 104, 88, 255);

const char* const kCurveNames[] = {"Keine", "Leicht", "Mittel", "Stark"};
const char* const kFilterNames[] = {"Scharf", "Weich", "Scanlines", "CRT", "HD", "HD-Scanlines"};
static_assert(sizeof(kFilterNames) / sizeof(kFilterNames[0]) == kScreenFilterCount, "Filternamen");
const char* const kEnvNames[] = {"Aus", "Himmel", "+ Horizont", "Nur Strecke"};
constexpr float kCurveDegrees[] = {0.0f, 30.0f, 60.0f, 90.0f};
// Reiter „Zeiger“ (Vorbild: Strahl/Zielpunkt-Optionen der Siedler-Portierung)
const char* const kRayColorNames[] = {"Orange", "Weiß", "Cyan", "Grün", "Magenta", "Gelb"};
constexpr float kRayColorRgb[][3] = {{1.0f, 0.55f, 0.1f}, {1.0f, 1.0f, 1.0f}, {0.2f, 0.85f, 1.0f},
                                     {0.3f, 1.0f, 0.4f}, {1.0f, 0.3f, 0.8f}, {1.0f, 0.9f, 0.2f}};
const char* const kRayStyleNames[] = {"Normal", "Dünn", "Dick", "Kurz", "Aus"};
const char* const kMarkerNames[] = {"Normal", "Groß", "Klein", "Aus"};
const char* const kZoomNames[] = {"1×", "1,5×", "2×", "3×"};
const char* const kGameCursorNames[] = {"An", "Aus"};
// Reiter „DOS“ (Reihenfolge = dosopt::Id)
const char* const kDosLabels[] = {"Mausmodus", "Mausgeschwindigkeit", "CPU-Tempo", "Seitenverhältnis",
                                  "Grafikkarte"};
static_assert(sizeof(kDosLabels) / sizeof(kDosLabels[0]) == dosopt::kCount, "DOS-Zeilen");
constexpr double kRepeatDelay = 0.45, kRepeatInterval = 0.12;   // Dauerdruck ‹ ›
constexpr double kStickDelay = 0.40, kStickInterval = 0.16;     // Stick halten
constexpr float kStickOn = 0.6f, kStickOff = 0.3f;
constexpr float kZoomFactors[] = {1.0f, 1.5f, 2.0f, 3.0f};

const char* kVertex = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUv;
uniform mat4 uMvp;
out vec2 vUv;
void main() { vUv = aUv; gl_Position = uMvp * vec4(aPos, 1.0); })";

const char* kFragment = R"(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTex;
out vec4 frag;
void main() { frag = texture(uTex, vUv); })";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("Menü-Shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

int clampStep(int value, int step, int lo, int hi) {
    return std::min(std::max(value + step, lo), hi);
}
float clampStepF(float value, float step, float lo, float hi) {
    // Auf das Raster einrasten, damit 2,4 + 0,2 nicht zu 2,6000001 wird.
    const float v = std::round((value + step) / std::fabs(step)) * std::fabs(step);
    return std::min(std::max(v, lo), hi);
}
int wrap(int value, int count) { return (value % count + count) % count; }

uint32_t withAlpha(uint32_t c, uint32_t a) { return (c & 0x00FFFFFFu) | (a << 24); }

// Richtung Weiß aufhellen (amount 0..255), Alpha bleibt.
uint32_t lighten(uint32_t c, uint32_t amount) {
    uint32_t out = c & 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        const uint32_t v = (c >> s) & 0xFF;
        out |= (v + (255 - v) * amount / 255) << s;
    }
    return out;
}

uint32_t over(uint32_t color, uint32_t dst, uint32_t cov);

// n Pixel mit voller Deckung mischen; das Ergebnis wird wiederverwendet, solange der Untergrund
// gleich bleibt (Zeilen auf dem senkrechten Verlauf). Eigene Funktion: eine schlanke Schleife.
void blendRun(uint32_t* p, int n, uint32_t color) {
    uint32_t lastDst = p[0], lastOut = over(color, lastDst, 255);
    for (int i = 0; i < n; ++i) {
        const uint32_t dst = p[i];
        if (dst != lastDst) {
            lastDst = dst;
            lastOut = over(color, dst, 255);
        }
        p[i] = lastOut;
    }
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

// color mit Deckung cov (0..255) über dst mischen; Alpha wird mitgeführt. Ganzzahlig —
// dieselbe Rechnung für blend() und den Tafelhintergrund im Teil-Redraw (sonst Streifen).
uint32_t over(uint32_t color, uint32_t dst, uint32_t cov = 255) {
    const uint32_t a = ((color >> 24) & 0xFF) * cov / 255;   // 0..255
    if (a == 0) return dst;
    if (a == 255) return color | 0xFF000000u;
    const uint32_t inv = 255 - a;
    const uint32_t r = ((color & 0xFF) * a + (dst & 0xFF) * inv) / 255;
    const uint32_t g = (((color >> 8) & 0xFF) * a + ((dst >> 8) & 0xFF) * inv) / 255;
    const uint32_t b = (((color >> 16) & 0xFF) * a + ((dst >> 16) & 0xFF) * inv) / 255;
    const uint32_t outA = a + ((dst >> 24) & 0xFF) * inv / 255;
    return r | (g << 8) | (b << 16) | (std::min(outA, 255u) << 24);
}

// „2.4 m“ → „2,4 m“
void meters(char* out, size_t size, float value) {
    std::snprintf(out, size, "%.1f m", static_cast<double>(value));
    for (char* c = out; *c; ++c)
        if (*c == '.') *c = ',';
}

} // namespace

// ==============================================================================
// Einstellungen
// ==============================================================================

bool ViewSettings::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char key[64];
    float value;
    bool chosenKey = false;
    while (std::fscanf(f, " %63[^= ] = %f", key, &value) == 2) {
        const int i = static_cast<int>(std::lround(value));
        if (!std::strcmp(key, "passthrough")) passthrough = std::min(std::max(i, 0), 100);
        else if (!std::strcmp(key, "curve")) curve = wrap(i, 4);
        else if (!std::strcmp(key, "filter")) filter = wrap(i, kScreenFilterCount);
        else if (!std::strcmp(key, "filter_chosen")) { filterChosen = i != 0; chosenKey = true; }
        else if (!std::strcmp(key, "saturation")) saturation = std::min(std::max(i, 0), 200);
        else if (!std::strcmp(key, "contrast")) contrast = std::min(std::max(i, 50), 150);
        else if (!std::strcmp(key, "environment")) environment = wrap(i, 4);
        else if (!std::strcmp(key, "width")) width = value;
        else if (!std::strcmp(key, "distance")) distance = value;
        else if (!std::strcmp(key, "ray_color")) rayColor = wrap(i, kRayColors);
        else if (!std::strcmp(key, "ray_style")) rayStyle = wrap(i, kRayStyles);
        else if (!std::strcmp(key, "marker")) marker = wrap(i, kMarkers);
        else if (!std::strcmp(key, "game_cursor")) gameCursor = wrap(i, kGameCursors);
        else if (!std::strcmp(key, "pointer_offset_x")) pointerOffsetX = std::min(std::max(i, -kPointerOffsetSteps), kPointerOffsetSteps);
        else if (!std::strcmp(key, "pointer_offset_y")) pointerOffsetY = std::min(std::max(i, -kPointerOffsetSteps), kPointerOffsetSteps);
        else if (!std::strcmp(key, "start_mode")) startMode = wrap(i, kStartModes);
    }
    // Ältere Dateien ohne „filter_chosen“: ein anderer Filter als Scharf war eine Wahl im Menü.
    if (!chosenKey) filterChosen = filter != 0;
    // Ältere Dateien ohne Breite/Abstand bzw. kaputte Werte → Standard (0) statt NaN.
    if (!std::isfinite(width) || width < 0.0f || width > 6.0f) width = 0.0f;
    if (!std::isfinite(distance) || distance < 0.0f || distance > 3.5f) distance = 0.0f;
    std::fclose(f);
    return true;
}

bool ViewSettings::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        LOGE("Einstellungen nicht speicherbar: %s", path.c_str());
        return false;
    }
    std::fprintf(f, "passthrough = %d\ncurve = %d\nfilter = %d\nsaturation = %d\n"
                    "contrast = %d\nenvironment = %d\nwidth = %.3f\ndistance = %.3f\n"
                    "ray_color = %d\nray_style = %d\nmarker = %d\ngame_cursor = %d\n"
                    "filter_chosen = %d\npointer_offset_x = %d\npointer_offset_y = %d\n"
                    "start_mode = %d\n",
                 passthrough, curve, filter, saturation, contrast, environment,
                 static_cast<double>(width), static_cast<double>(distance),
                 rayColor, rayStyle, marker, gameCursor, filterChosen ? 1 : 0,
                 pointerOffsetX, pointerOffsetY, startMode);
    std::fclose(f);
    return true;
}

float ViewSettings::curveRadius(float screenWidth) const {
    const float degrees = kCurveDegrees[wrap(curve, 4)];
    if (degrees <= 0.0f) return 0.0f;
    return screenWidth / (degrees * 3.14159265f / 180.0f);   // Bogenlänge = Breite
}

bool ViewSettings::operator!=(const ViewSettings& o) const {
    return passthrough != o.passthrough || curve != o.curve || filter != o.filter ||
           filterChosen != o.filterChosen ||
           saturation != o.saturation || contrast != o.contrast ||
           environment != o.environment || width != o.width || distance != o.distance ||
           rayColor != o.rayColor || rayStyle != o.rayStyle || marker != o.marker ||
           zoom != o.zoom || gameCursor != o.gameCursor ||
           pointerOffsetX != o.pointerOffsetX || pointerOffsetY != o.pointerOffsetY ||
           startMode != o.startMode;
}

float ViewSettings::pointerOffsetPercentX() const { return ptroff::clampPercent(pointerOffsetX * ptroff::kStep); }
float ViewSettings::pointerOffsetPercentY() const { return ptroff::clampPercent(pointerOffsetY * ptroff::kStep); }

void ViewSettings::pointerRgb(float out[3]) const {
    const float* c = kRayColorRgb[wrap(rayColor, kRayColors)];
    for (int i = 0; i < 3; ++i) out[i] = c[i];
}

float ViewSettings::rayThickness(bool menuOpen) const {
    const int style = wrap(rayStyle, kRayStyles);
    if (menuOpen && (style == kRayStyleOff || style == 3)) return 0.004f;   // Menü braucht den Strahl
    switch (style) {
    case 1: return 0.002f;    // Dünn
    case 2: return 0.008f;    // Dick
    case 4: return 0.0f;      // Aus
    default: return 0.004f;   // Normal, Kurz
    }
}

float ViewSettings::rayMaxLength(bool menuOpen) const {
    return !menuOpen && wrap(rayStyle, kRayStyles) == 3 ? 0.15f : 100.0f;   // Kurz: 15 cm am Controller
}

float ViewSettings::markerSize(bool menuOpen) const {
    const int m = wrap(marker, kMarkers);
    if (menuOpen && m == kMarkerOff) return 0.025f;
    switch (m) {
    case 1: return 0.045f;    // Groß
    case 2: return 0.012f;    // Klein
    case 3: return 0.0f;      // Aus — der Mauszeiger des Spiels reicht
    default: return 0.025f;   // Normal
    }
}

float ViewSettings::zoomFactor() const { return kZoomFactors[wrap(zoom, kZooms)]; }

// ==============================================================================
// Menü
// ==============================================================================

bool VrMenu::init() {
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
    if (!ok) { LOGE("Menü-Programm: Link fehlgeschlagen"); return false; }
    mLocMvp = glGetUniformLocation(mProgram, "uMvp");
    mLocTex = glGetUniformLocation(mProgram, "uTex");

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
    // Mipmaps: die Tafel wird kleiner dargestellt als 1024 px — ohne Mipmaps flimmert Text.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mFont.load();
    mFontBold.load(true);
    setTheme(MenuTheme());   // Standardfarben
    layout();
    LOGI("VR-Menü bereit");
    return true;
}

void VrMenu::setTheme(const MenuTheme& theme) {
    mTheme = theme;
    mColText = theme.active ? theme.text : kText;
    mColDim = theme.active ? theme.textDim : kTextDim;
    mColDark = theme.active ? theme.text : kTextDark;   // Schrift auf dem aktiven Reiter
    mColAccent = theme.active ? (theme.accent | 0xFF000000u) : kAccent;
    mDirty = true;
}

void VrMenu::shutdown() {
    if (mTexture) glDeleteTextures(1, &mTexture);
    if (mVbo) glDeleteBuffers(1, &mVbo);
    if (mProgram) glDeleteProgram(mProgram);
    mTexture = mVbo = mProgram = 0;
}

void VrMenu::setOptions(bool mouseCore, bool passthroughAvailable, bool gameCursorAvailable) {
    if (mouseCore != mKeyboardEntry || passthroughAvailable != mPassthroughAvailable ||
        gameCursorAvailable != mGameCursorAvailable) {
        mKeyboardEntry = mouseCore;
        mPassthroughAvailable = passthroughAvailable;
        mGameCursorAvailable = gameCursorAvailable;
        layout();   // Zeilenzahl kann sich ändern
    }
}

void VrMenu::setStatesAvailable(bool available) {
    if (available == mStatesAvailable) return;
    mStatesAvailable = available;
    layout();   // Zeilenzahl ändert sich
}

void VrMenu::setGameMenuAvailable(bool available) {
    if (available == mGameMenuAvailable) return;
    mGameMenuAvailable = available;
    layout();
}

void VrMenu::setEnvironmentAvailable(bool available) {
    if (available == mEnvironmentAvailable) return;
    mEnvironmentAvailable = available;
    layout();
}

void VrMenu::setControlsHelp(const std::vector<std::string>& lines) {
    if (lines == mHelp) return;
    mHelp = lines;
    layout();
}

void VrMenu::setTitle(const std::string& title, const std::string& subtitle) {
    if (title == mTitle && subtitle == mSubtitle) return;
    mTitle = title;
    mSubtitle = subtitle;
    mDirty = true;
}

void VrMenu::setRecording(bool recording, int seconds, bool available) {
    if (recording == mRecording && seconds == mRecordSeconds && available == mRecordAvailable) return;
    mRecording = recording;
    mRecordSeconds = seconds;
    mRecordAvailable = available;
    // Nur die Leiste (letzte Zeile) neu zeichnen — läuft im Sekundentakt.
    if (mVisible && !mDirty && mTexH > 0) {
        const int last = static_cast<int>(rows().size()) - 1;
        redrawHover(mShown, last, last, false);
    } else {
        mDirty = true;
    }
}

void VrMenu::setStatus(const std::string& text, double now) {
    mStatus = text;
    mStatusUntil = now + 3.0;
    // Nur die Kopfzeile neu — ein Vollredraw pro Meldung ließ das Menü kurz stocken.
    if (mVisible && !mDirty && mTexH > 0) redrawTitle(now);
    else mDirty = true;
}

bool VrMenu::isValueRow(Row r) {
    return r <= kZoom || r == kStateSlot || r == kProfileSlot || r == kPage || r == kGameCursor || r == kStartMode ||
           r == kPointerOffsetX || r == kPointerOffsetY ||
           r == kPadStickL || r == kPadStickR ||
           (r >= kDosMouse && r <= kDosSvga) || r == kDosGainX || r == kDosGainY ||
           (r >= kCtlGrip && r <= kCtlStick);
}

void VrMenu::setControlsPage(bool enabled, bool gameRunning, const mapdrag::Settings& controls) {
    if (enabled != mControlsPage || gameRunning != mControlsGame) {
        mControlsPage = enabled;
        mControlsGame = gameRunning;
        mControls = controls;
        if (!enabled && mTab == kTabControls) mTab = kTabView;
        layout();
        return;
    }
    if (controls == mControls) return;
    mControls = controls;
    if (!mVisible && mTab == kTabControls) mDirty = true;   // beim Öffnen wiederverwendetes Bild
    if (!mVisible || mDirty || mTexH == 0 || mTab != kTabControls) return;
    // Teil-Redraw: nur die Zeilen des Reiters (Griff dimmt die Zieh-Zeilen, Zieh-Art benennt Tempo um).
    const std::vector<Item> list = rows();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].row >= kCtlGrip && list[i].row <= kCtlStick)
            redrawHover(mShown, static_cast<int>(i), static_cast<int>(i), false);
}

void VrMenu::setDosPage(bool enabled, const DosPage& page) {
    if (enabled != mDosPage) {
        mDosPage = enabled;
        mDos = page;
        if (!enabled && mTab == kTabDos) mTab = kTabView;
        layout();
        return;
    }
    if (page == mDos) return;
    const std::vector<Item> before = rows();
    const DosPage old = mDos;
    mDos = page;
    const std::vector<Item> after = rows();
    bool sameRows = before.size() == after.size();
    for (size_t i = 0; sameRows && i < after.size(); ++i) sameRows = before[i].row == after[i].row;
    if (!sameRows) { layout(); return; }   // Zeile kam/ging (z. B. Hohe Auflösung)
    if (!mVisible && (mTab == kTabDos || mTab == kTabPointer)) mDirty = true;   // beim Öffnen wiederverwendetes Bild
    if (!mVisible || mDirty || mTexH == 0 || (mTab != kTabDos && mTab != kTabPointer)) return;
    // Nur die Zeilen neu, deren Inhalt sich geändert hat (Teil-Redraw, kein Ruckeln).
    for (size_t i = 0; i < after.size(); ++i) {
        const Row r = after[i].row;
        bool changed = false;
        if (r >= kDosMouse && r <= kDosSvga) changed = !(old.options[r - kDosMouse] == page.options[r - kDosMouse]);
        else if (r == kDosGainX) changed = old.gainX != page.gainX || old.relative != page.relative;
        else if (r == kDosGainY) changed = old.gainY != page.gainY || old.relative != page.relative;
        else if (r == kDosCalibrate) changed = old.relative != page.relative;
        else if (r == kDosLearn) changed = old.cursor != page.cursor;
        else if (r == kDosHires) changed = old.hiresKey != page.hiresKey;
        else if (r == kDosRestart) changed = old.restartPending != page.restartPending;
        else if (r == kDosReset) changed = old.userValues != page.userValues;
        if (changed) redrawHover(mShown, static_cast<int>(i), static_cast<int>(i), false);
    }
}

void VrMenu::setInfo(const std::string& text) {
    if (text == mInfo) return;
    mInfo = text;
    mInfoDirty = true;   // update() zeichnet nur die Kopfzeile neu
}

int VrMenu::rowIndex(Row r) const {
    const std::vector<Item> list = rows();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].row == r) return static_cast<int>(i);
    return -1;
}

void VrMenu::setLibraryEmptyHint(const std::string& emptyHint) {
    // „Keine Spiele – Ordner nach …“ → Überschrift „Keine Spiele“, Hinweis „Ordner nach …“.
    std::string title = "Noch keine Spiele", hint = emptyHint;
    const std::string dash = " – ";
    const size_t at = emptyHint.find(dash);
    if (at != std::string::npos) {
        title = emptyHint.substr(0, at);
        hint = emptyHint.substr(at + dash.size());
    }
    if (title == mEmptyTitle && hint == mEmptyHint) return;
    mEmptyTitle = title;
    mEmptyHint = hint;
    mDirty = true;
}

// --- Bibliothek und Suche ---------------------------------------------------------

namespace {
// Für die Suche gefaltet: klein, Umlaute/Akzente auf den Grundbuchstaben, ß → ss,
// Apostrophe entfallen („Pilot's“ = „pilots“), sonstige Zeichen werden zu Leerzeichen.
std::string foldForSearch(const std::string& s) {
    std::string out;
    for (const char* p = s.c_str(); *p;) {
        const uint32_t cp = utf8Next(p);
        char c = ' ';
        if (cp >= 'A' && cp <= 'Z') c = static_cast<char>(cp - 'A' + 'a');
        else if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) c = static_cast<char>(cp);
        else if (cp == '\'' || cp == 0x2019 || cp == 0x60 || cp == 0xB4) continue;
        else if (cp == 0xDF) { out += "ss"; continue; }
        else if (cp == 0xC6 || cp == 0xE6) { out += "ae"; continue; }
        else if (cp >= 0xC0 && cp <= 0xFF) {
            const uint32_t l = cp | 0x20;   // Latin-1: Groß- und Kleinbuchstabe liegen 0x20 auseinander
            if (l >= 0xE0 && l <= 0xE5) c = 'a';
            else if (l == 0xE7) c = 'c';
            else if (l >= 0xE8 && l <= 0xEB) c = 'e';
            else if (l >= 0xEC && l <= 0xEF) c = 'i';
            else if (l == 0xF1) c = 'n';
            else if ((l >= 0xF2 && l <= 0xF6) || l == 0xF8) c = 'o';
            else if (l >= 0xF9 && l <= 0xFC) c = 'u';
            else if (l == 0xFD || l == 0xFF) c = 'y';
        }
        if (c == ' ' && (out.empty() || out.back() == ' ')) continue;   // Leerzeichen zusammenfassen
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

size_t utf8Count(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

void utf8PopBack(std::string& s) {
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

constexpr size_t kMaxQuery = 24;   // Zeichen

// Bildschirmtastatur: QWERTZ mit Umlauten; Beschriftung groß, eingefügt wird klein.
struct KeySpec { const char* label; const char* insert; int kind; float span; };
const std::vector<std::vector<KeySpec>>& keySpecs() {
    static const std::vector<std::vector<KeySpec>> specs = {
        {{"1", "1", 0, 1}, {"2", "2", 0, 1}, {"3", "3", 0, 1}, {"4", "4", 0, 1}, {"5", "5", 0, 1},
         {"6", "6", 0, 1}, {"7", "7", 0, 1}, {"8", "8", 0, 1}, {"9", "9", 0, 1}, {"0", "0", 0, 1},
         {"ß", "ß", 0, 1}},
        {{"Q", "q", 0, 1}, {"W", "w", 0, 1}, {"E", "e", 0, 1}, {"R", "r", 0, 1}, {"T", "t", 0, 1},
         {"Z", "z", 0, 1}, {"U", "u", 0, 1}, {"I", "i", 0, 1}, {"O", "o", 0, 1}, {"P", "p", 0, 1},
         {"Ü", "ü", 0, 1}},
        {{"A", "a", 0, 1}, {"S", "s", 0, 1}, {"D", "d", 0, 1}, {"F", "f", 0, 1}, {"G", "g", 0, 1},
         {"H", "h", 0, 1}, {"J", "j", 0, 1}, {"K", "k", 0, 1}, {"L", "l", 0, 1}, {"Ö", "ö", 0, 1},
         {"Ä", "ä", 0, 1}},
        {{"Y", "y", 0, 1}, {"X", "x", 0, 1}, {"C", "c", 0, 1}, {"V", "v", 0, 1}, {"B", "b", 0, 1},
         {"N", "n", 0, 1}, {"M", "m", 0, 1}, {"-", "-", 0, 1}, {".", ".", 0, 1}, {"'", "'", 0, 1},
         {"\xE2\x86\x90", "", 2, 1}},   // ← Rücktaste
        {{"Leerzeichen", " ", 1, 5}, {"Leeren", "", 3, 3}, {"Fertig", "", 4, 3}},
    };
    return specs;
}
} // namespace

namespace {
// Gefalteter Titel + dieselbe Zeichenfolge ohne Leerzeichen („spacepilot“ findet „Space Pilot“).
struct SearchKey { std::string hay, squeezed; };
SearchKey searchKey(const std::string& label) {
    SearchKey k;
    k.hay = foldForSearch(label);
    for (char c : k.hay) if (c != ' ') k.squeezed += c;
    return k;
}
std::vector<std::string> searchWords(const std::string& query) {
    std::vector<std::string> words;
    const std::string q = foldForSearch(query);
    for (size_t start = 0; start < q.size();) {
        size_t end = q.find(' ', start);
        if (end == std::string::npos) end = q.size();
        words.push_back(q.substr(start, end - start));
        start = end + 1;
    }
    return words;
}
bool matchesWords(const std::string& hay, const std::string& squeezed, const std::vector<std::string>& words) {
    for (const std::string& w : words)
        if (hay.find(w) == std::string::npos && squeezed.find(w) == std::string::npos) return false;
    return true;
}
} // namespace

bool VrMenu::matchesSearch(const std::string& label, const std::string& query) {
    const SearchKey k = searchKey(label);
    return matchesWords(k.hay, k.squeezed, searchWords(query));
}

void VrMenu::refilter() {
    // Titel einmal falten (bei jeder neuen Bibliothek), danach pro Tastendruck nur suchen.
    if (mSearchHay.size() != mLibrary.size()) {
        mSearchHay.clear();
        mSearchSqueezed.clear();
        for (const std::string& label : mLibrary) {
            SearchKey k = searchKey(label);
            mSearchHay.push_back(std::move(k.hay));
            mSearchSqueezed.push_back(std::move(k.squeezed));
        }
    }
    mFiltered.clear();
    const std::vector<std::string> words = searchWords(mQuery);
    for (size_t i = 0; i < mLibrary.size(); ++i)
        if (words.empty() || matchesWords(mSearchHay[i], mSearchSqueezed[i], words))
            mFiltered.push_back(static_cast<int>(i));
}

void VrMenu::setLibrary(const std::vector<std::string>& labels, int running, bool enabled, int last) {
    if (last >= static_cast<int>(labels.size())) last = -1;
    if (labels == mLibrary && running == mRunningGame && enabled == mLibraryEnabled && last == mLastGame) return;
    // Weiterspielen-Knopf erscheint/verschwindet → Zeilenzahl im Reiter „Spiel“ ändert sich.
    const bool resumeChanged = (mRunningGame < 0 && mLastGame >= 0) != (running < 0 && last >= 0);
    const bool modeChanged = enabled != mLibraryEnabled || resumeChanged;
    mLastGame = last;
    {
        std::string first;
        bool single = !labels.empty();
        for (const std::string& l : labels) {
            const std::string sys = l.substr(0, l.find('\t'));
            if (&l == &labels.front()) first = sys;
            else if (sys != first) single = false;
        }
        mSingleSystem = single;
    }
    if (labels != mLibrary) { mSearchHay.clear(); mSearchSqueezed.clear(); }   // neu falten
    mLibrary = labels;
    mRunningGame = running;
    mLibraryEnabled = enabled;
    if (!enabled) mSearchActive = false;
    refilter();
    if (modeChanged) layout();   // Tafelbreite und Tastatur
    mPage = std::max(0, std::min(mPage, pageCount() - 1));
    mDirty = true;
}

void VrMenu::showGamesTab() {
    if (!mLibraryEnabled) return;
    mQuery.clear();
    refilter();
    // Seite mit dem laufenden Spiel zeigen, sonst mit dem zuletzt gespielten, sonst die erste.
    // Ohne laufendes Spiel, aber mit einem letzten: Reiter „Spiel“ mit „Weiterspielen“ vorn.
    const int focus = mRunningGame >= 0 ? mRunningGame : mLastGame;
    const auto it = std::find(mFiltered.begin(), mFiltered.end(), focus);
    mPage = it == mFiltered.end() ? 0 : static_cast<int>(it - mFiltered.begin()) / gamesPerPage();
    if (mRunningGame < 0 && mLastGame >= 0) mTab = kTabGame;
    layout();
}

int VrMenu::gamesPerPage() const {
    // Linke Spalte: so viele Zeilen wie die rechte über der Schnellleiste, minus Seite + Leerzeile.
    return std::max(1, static_cast<int>(rows().size()) - 1 - 2);
}

int VrMenu::pageCount() const {
    const int gpp = gamesPerPage();
    return std::max(1, (static_cast<int>(mFiltered.size()) + gpp - 1) / gpp);
}

std::vector<VrMenu::Tab> VrMenu::tabs() const {
    std::vector<Tab> t = { kTabView, kTabPointer };
    if (mPadEnabled) t.push_back(kTabPad);
    if (mDosPage) t.push_back(kTabDos);
    if (mControlsPage) t.push_back(kTabControls);
    t.push_back(kTabGame);
    return t;
}

void VrMenu::setPad(bool enabled, const std::string& systemFolder, const padmap::Mapping& mapping) {
    const bool layoutChange = enabled != mPadEnabled || systemFolder != mPadFolder;
    if (!layoutChange && mapping == mPadMap) return;
    mPadEnabled = enabled;
    mPadFolder = systemFolder;
    mPadMap = mapping;
    mLearnTarget = -1;
    if (!enabled && mTab == kTabPad) mTab = kTabView;
    if (layoutChange) layout();   // Reiterzahl/Zeilen
    else mDirty = true;
}

int VrMenu::zoneCount(const Item& item) const {
    switch (item.row) {
    case kQuickBar: return 3;
    case kPadActions: return 2;
    case kPadPair:
        return std::min(2, static_cast<int>(padmap::systemFor(mPadFolder).buttons.size()) - item.game);
    default: return 0;
    }
}

void VrMenu::zoneSpan(const Item& item, int& x0, int& x1) const {
    x0 = item.row == kQuickBar ? 20 : contentX0() + 20;
    x1 = mTexW - 20;
}

VrMenu::Zone VrMenu::arriveZone(const Item& item, int preferred) const {
    const int n = zoneCount(item);
    if (n == 0) return kZoneRow;
    if (preferred < 0) preferred = item.row == kQuickBar ? 1 : 0;   // Schnellleiste: „Schließen“
    return static_cast<Zone>(kZoneBar0 + std::min(preferred, n - 1));
}

std::vector<VrMenu::Item> VrMenu::rows(Tab tab) const {
    std::vector<Item> r = contentRows(tab);
    // Füllzeilen: Schnellleiste und Tafelhöhe bleiben beim Reiterwechsel stehen.
    size_t most = 0;
    for (Tab t : tabs()) most = std::max(most, contentRows(t).size());
    if (mLibraryEnabled) most = std::max<size_t>(most, 9);   // Spieleliste: 7 Spiele pro Seite
    while (r.size() < most) r.push_back({ kSpacer, -1 });
    r.push_back({ kQuickBar, -1 });   // Aufnahme · Schließen · Beenden in jedem Reiter
    return r;
}

std::vector<VrMenu::Item> VrMenu::contentRows(Tab tab) const {
    std::vector<Item> r;
    auto add = [&](Row row) { r.push_back({ row, -1 }); };
    switch (tab) {
    case kTabView:
        // xr.dosgames: Reiter „Bild“ — alles Optische an einer Stelle, dazu „Größe passend zum Spiel“.
        for (Row row : { kPassthrough, kCurve, kFilter, kSaturation, kContrast, kSize, kDistance })
            add(row);
        if (mEnvironmentAvailable) add(kEnvironment);
        if (mDosPage) add(kFitSize);
        add(kRecenter);
        break;
    case kTabDos:
        if (!mDos.gameRunning) { add(kDosNoGame); break; }
        for (int i = 0; i < dosopt::kCount; ++i) {
            if (mDos.options[i].present) add(static_cast<Row>(kDosMouse + i));
            if (i == dosopt::kMouse && !mDos.gainX.empty()) { add(kDosGainX); add(kDosGainY); }
        }
        if (!mDos.hiresKey.empty()) add(kDosHires);
        if (!mDos.gainX.empty()) add(kDosCalibrate);
        add(kDosRestart);
        add(kDosReset);
        break;
    case kTabControls:
        if (!mControlsGame) { add(kDosNoGame); break; }
        for (Row row : { kCtlGrip, kCtlModel, kCtlDirection, kCtlSpeed, kCtlStick }) add(row);
        break;
    case kTabPointer:
        for (Row row : { kRayColor, kRayStyle, kMarker }) add(row);
        if (mKeyboardEntry) {   // nur Maus-Cores haben einen Spiel-Mauszeiger
            add(kGameCursor);
            add(kPointerOffsetX);   // Spielzeiger liegt neben dem Strahl → nachschieben
            add(kPointerOffsetY);
            // xr.dosgames: Spielzeiger im Speicher finden und an den Laser koppeln.
            if (mDosPage && mDos.gameRunning) add(kDosLearn);
        }
        add(kZoom);
        break;
    case kTabGame:
        if (mLibraryEnabled) {
            if (mRunningGame < 0 && mLastGame >= 0) add(kResume);   // „Weiterspielen: <Titel>“
            add(kStartMode);
        }
        if (mStatesAvailable)
            for (Row row : { kStateSlot, kSaveState, kLoadState }) add(row);
        else
            add(mGameMenuAvailable ? kGameMenu : kNoStates);   // ScummVM: Speichern im Spielmenü
        for (Row row : { kProfileSlot, kSaveProfile, kLoadProfile }) add(row);
        if (mKeyboardEntry) add(kKeyboard);
        for (size_t i = 0; i < mHelp.size(); ++i) r.push_back({ kHelp, static_cast<int>(i) });   // unten: Bedienung
        break;
    case kTabPad: {
        // Konsolentasten paarweise, dann beide Sticks und die Knöpfe; Kurzanleitung nur, wenn
        // die Tafel dadurch nicht höher wird als für die anderen Reiter.
        const int buttons = static_cast<int>(padmap::systemFor(mPadFolder).buttons.size());
        const int pairs = (buttons + 1) / 2;
        size_t others = mLibraryEnabled ? 9 : 0;
        for (Tab t : tabs())
            if (t != kTabPad) others = std::max(others, contentRows(t).size());
        if (static_cast<size_t>(pairs + 4) <= others) add(kPadHint);
        for (int i = 0; i < pairs; ++i) r.push_back({ kPadPair, 2 * i });
        for (Row row : { kPadStickL, kPadStickR, kPadActions }) add(row);
        break;
    }
    }
    return r;
}

std::vector<VrMenu::Item> VrMenu::libRows() const {
    std::vector<Item> r;
    if (!mLibraryEnabled) return r;
    const int gpp = gamesPerPage();
    r.push_back({ kSearch, -1 });
    if (mLibrary.empty()) {
        r.push_back({ kLibraryEmpty, -1 });
        r.push_back({ kLibraryHint, -1 });
    }
    else if (mFiltered.empty()) r.push_back({ kNoMatch, -1 });
    const int first = mPage * gpp;
    const int last = std::min(first + gpp, static_cast<int>(mFiltered.size()));
    for (int i = first; i < last; ++i) r.push_back({ kGame, mFiltered[i] });
    while (static_cast<int>(r.size()) < 1 + gpp) r.push_back({ kSpacer, -1 });
    r.push_back({ kPage, -1 });
    r.push_back({ kSpacer, -1 });   // Spalte bleibt gleich hoch
    return r;
}

void VrMenu::layout() {
    mTexW = mLibraryEnabled ? kLibTexW : 1024;
    mTexH = kTitleH + kTabH + static_cast<int>(rows().size()) * kRowH + kPadBottom;
    // Mit Bibliothek breiter (1,62 m statt 1,8 m bei gleicher Pixeldichte: etwas kleinere Schrift).
    mW = kPanelWidth * static_cast<float>(mTexW) / 1024.0f * (mLibraryEnabled ? 0.9f : 1.0f);
    mH = mW * static_cast<float>(mTexH) / static_cast<float>(mTexW);

    // Suchtastatur: füllt die rechte Spalte unter der Suchanzeige bis zur Schnellleiste.
    mKeys.clear();
    if (mLibraryEnabled) {
        const auto& specs = keySpecs();
        const int gap = 10;
        const float x0 = static_cast<float>(kLibW + 20), x1 = static_cast<float>(mTexW - 20);
        const int y0 = kTitleH + kTabH + 6, y1 = barTop() - 8;
        const int rowCount = static_cast<int>(specs.size());
        const int keyH = (y1 - y0 - gap * (rowCount - 1)) / rowCount;
        const float unit = (x1 - x0 - gap * 10.0f) / 11.0f;
        for (int r = 0; r < rowCount; ++r) {
            float x = x0;
            const int ky = y0 + r * (keyH + gap);
            for (const KeySpec& s : specs[r]) {
                const float w = s.span * unit + (s.span - 1.0f) * gap;
                mKeys.push_back({ s.label, s.insert, static_cast<KeyKind>(s.kind), r,
                                  static_cast<int>(std::lround(x)), ky,
                                  static_cast<int>(std::lround(x + w)), ky + keyH });
                x += w + gap;
            }
        }
    }
    mDirty = true;
}

void VrMenu::reposition(float headDistance) {
    mCz = std::max(0.0f, std::min(0.6f, headDistance - 0.6f));
}

void VrMenu::open(float headDistance) {
    reposition(headDistance);
    const Focus old = hover();
    const bool wasSearching = mSearchActive, wasLearning = mLearnTarget >= 0;
    setHover(Focus());
    mRay = Focus();
    mPadFocus = false;
    mRepeatRow = mRepeatKey = -1;
    mStickDir = 0;
    mQuitArmedUntil = 0.0;   // nie scharf in eine neue Sitzung mitnehmen
    mConfirmUntil = 0.0;
    mSearchActive = false;   // Suchtext bleibt, die Tastatur nicht
    mLearnTarget = -1;
    // Steht die Tafel vom letzten Mal noch, wird sie weiterverwendet: nur Fokus, Suchmodus und
    // Tasten-Zeilen nachziehen (Beenden/Bestätigung/Status zeichnet update selbst zurück) — ein
    // Vollbild beim Drücken der Menütaste kostete auf der Quest sichtbar Frames.
    const bool reuse = mTexH > 0 && !mDirty && mPixels.size() == static_cast<size_t>(mTexW) * mTexH;
    if (!reuse) {
        layout();
    } else {
        if (wasSearching) mRightDirty = true;
        if (wasLearning) mPadRowsDirty = true;
        if (old.any()) redrawFocus(mShown, old, Focus());   // Tasten zeichnet redrawRight
    }
    mVisible = true;
}

void VrMenu::armQuit(double now) {
    mQuitArmedUntil = now + 3.0;
    setStatus("Zum Beenden nochmal klicken", now);
}

void VrMenu::setSearchActive(bool active) {
    if (active == mSearchActive) return;
    mSearchActive = active;
    mRepeatKey = -1;
    if (!active && mHoverKey >= 0) {   // Fokus von der Tastatur zurück aufs Suchfeld
        mHoverKey = -1;
        if (mPadFocus) mHoverLib = 0;
    }
    if (active && mHoverRow >= 0 && mHoverRow < static_cast<int>(rows().size()) - 1) mHoverRow = -1;
    if (active) mHoverTab = -1;
    mRightDirty = true;   // Tastatur bzw. Reiter rechts, Suchfeld links
}

void VrMenu::pressKey(int key) {
    if (key < 0 || key >= static_cast<int>(mKeys.size())) return;
    const Key& k = mKeys[key];
    switch (k.kind) {
    case kKeyChar:
        if (utf8Count(mQuery) < kMaxQuery) mQuery += k.insert;
        break;
    case kKeySpace:
        if (!mQuery.empty() && mQuery.back() != ' ' && utf8Count(mQuery) < kMaxQuery) mQuery += ' ';
        break;
    case kKeyBackspace: utf8PopBack(mQuery); break;
    case kKeyClear: mQuery.clear(); break;
    case kKeyDone: setSearchActive(false); return;
    }
    refilter();
    mPage = 0;
    mLibDirty = true;
}

// Ziel des Strahls auf der Tafel (Texturpixel) bestimmen.
VrMenu::Focus VrMenu::hitTest(float px, float py, const std::vector<Item>& list,
                              const std::vector<Tab>& tabList, const std::vector<Item>& lib) const {
    Focus f;
    const int bar = barTop();
    if (py < kTitleH || py >= bar + kRowH) return f;
    // Zeile mit mehreren Knöpfen: nur die Knöpfe selbst, die Lücken lösen nichts aus.
    auto multiZone = [&](int row) {
        int x0 = 0, x1 = 0;
        zoneSpan(list[row], x0, x1);
        const int n = zoneCount(list[row]);
        const int span = (x1 - x0) / (list[row].row == kPadPair ? 2 : n);
        const int b = std::min(n - 1, std::max(0, static_cast<int>((px - static_cast<float>(x0)) / span)));
        if (px < static_cast<float>(x0 + b * span + 6) || px > static_cast<float>(x0 + (b + 1) * span - 6)) return false;
        f.row = row;
        f.zone = static_cast<Zone>(kZoneBar0 + b);
        return true;
    };
    if (py >= bar) {
        multiZone(static_cast<int>(list.size()) - 1);   // Schnellleiste über die ganze Breite
        return f;
    }
    if (mLibraryEnabled && px < kLibW) {
        const int i = py < kTitleH + kTabH ? 0 : static_cast<int>((py - kTitleH - kTabH) / kRowH) + 1;
        if (i >= static_cast<int>(lib.size())) return f;
        const Row r = lib[i].row;
        if (!isFocusable(r) || (r == kPage && pageCount() <= 1)) return f;
        f.lib = i;
        if (r == kSearch && !mQuery.empty() && px >= kLibW - 12 - 64) f.zone = kZoneMore;   // ×
        if (r == kPage) {
            if (px <= 24 + 80) f.zone = kZoneLess;
            else if (px >= kLibW - 12 - 80) f.zone = kZoneMore;
        }
        return f;
    }
    if (mSearchActive) {
        for (size_t k = 0; k < mKeys.size(); ++k) {
            const Key& key = mKeys[k];
            if (px >= key.x0 - 5 && px <= key.x1 + 5 && py >= key.y0 - 5 && py <= key.y1 + 5) {
                f.key = static_cast<int>(k);
                break;
            }
        }
        return f;
    }
    const int x0 = contentX0();
    if (px < x0) return f;
    if (py < kTitleH + kTabH) {
        const int n = static_cast<int>(tabList.size());
        f.tab = std::max(0, std::min(static_cast<int>((px - x0) * n / (mTexW - x0)), n - 1));
        return f;
    }
    const int row = static_cast<int>((py - kTitleH - kTabH) / kRowH);
    if (row < 0 || row >= static_cast<int>(list.size()) - 1 || !isFocusable(list[row].row)) return f;
    if (zoneCount(list[row]) > 0) {
        multiZone(row);
        return f;
    }
    f.row = row;
    if (isValueRow(list[row].row)) {
        // ‹ › rechtsbündig (bei 1024 px: 560–632 und 888–960), Trefferzonen je ±12 px größer.
        if (px >= mTexW - 464 - 12 && px <= mTexW - 392 + 12) f.zone = kZoneLess;
        else if (px >= mTexW - 136 - 12 && px <= mTexW - 64 + 12) f.zone = kZoneMore;
    }
    return f;
}

// Eine Zeile/Zone auslösen (Trigger-Klick, Dauerdruck, A). Klick auf Beschriftung/Wert = eine Stufe weiter.
VrMenu::Action VrMenu::activate(const std::vector<Item>& list, int row, Zone zone, double now,
                                ViewSettings& settings) {
    if (row < 0 || row >= static_cast<int>(list.size())) return Action::None;
    const int step = zone == kZoneLess ? -1 : 1;
    Action action = Action::None;
    switch (list[row].row) {
    case kPassthrough:
        if (mPassthroughAvailable)
            settings.passthrough = clampStep(settings.passthrough, 10 * step, 0, 100);
        action = Action::Changed;
        break;
    case kCurve: settings.curve = wrap(settings.curve + step, 4); action = Action::Changed; break;
    case kFilter:
        settings.filter = wrap(settings.filter + step, kScreenFilterCount);
        settings.filterChosen = true;   // ab jetzt gilt die Wahl, nicht der Standard des Spiels
        action = Action::Changed;
        break;
    case kSaturation:
        settings.saturation = clampStep(settings.saturation, 10 * step, 0, 200);
        action = Action::Changed;
        break;
    case kContrast:
        settings.contrast = clampStep(settings.contrast, 10 * step, 50, 150);
        action = Action::Changed;
        break;
    case kSize:
        settings.width = clampStepF(settings.width, 0.2f * step, 1.0f, 6.0f);
        action = Action::WindowChanged;
        break;
    case kDistance:
        settings.distance = clampStepF(settings.distance, 0.1f * step, 0.7f, 3.5f);
        action = Action::WindowChanged;
        break;
    case kEnvironment:
        settings.environment = wrap(settings.environment + step, 4);
        action = Action::Changed;
        break;
    case kRayColor: settings.rayColor = wrap(settings.rayColor + step, ViewSettings::kRayColors); action = Action::Changed; break;
    case kRayStyle: settings.rayStyle = wrap(settings.rayStyle + step, ViewSettings::kRayStyles); action = Action::Changed; break;
    case kMarker: settings.marker = wrap(settings.marker + step, ViewSettings::kMarkers); action = Action::Changed; break;
    case kStartMode:
        settings.startMode = wrap(settings.startMode + step, ViewSettings::kStartModes);
        action = Action::StartModeChanged;
        break;
    case kGameMenu: action = Action::GameMenu; break;
    case kResume:
        mSelectedGame = mLastGame;
        action = mLastGame >= 0 ? Action::StartGame : Action::None;
        break;
    case kGameCursor:
        // Auch ohne Core-Unterstützung umschaltbar und gespeichert — wirkt, sobald der Core es kann.
        settings.gameCursor = wrap(settings.gameCursor + step, ViewSettings::kGameCursors);
        action = Action::Changed;
        break;
    case kPointerOffsetX: case kPointerOffsetY: {
        int& v = list[row].row == kPointerOffsetX ? settings.pointerOffsetX : settings.pointerOffsetY;
        v = std::min(std::max(v + step, -ViewSettings::kPointerOffsetSteps), ViewSettings::kPointerOffsetSteps);
        action = Action::Changed;
        break;
    }
    case kZoom: settings.zoom = wrap(settings.zoom + step, ViewSettings::kZooms); action = Action::Changed; break;
    case kRecenter: action = Action::Recenter; break;
    case kStateSlot: mStateSlot = 1 + wrap(mStateSlot - 1 + step, kStateSlots); mDirty = true; break;
    case kSaveState: action = Action::SaveState; break;
    case kLoadState: action = Action::LoadState; break;
    case kProfileSlot: mProfileSlot = 1 + wrap(mProfileSlot - 1 + step, kProfileSlots); mDirty = true; break;
    case kSaveProfile: action = Action::SaveProfile; break;
    case kLoadProfile: action = Action::LoadProfile; break;
    case kKeyboard: action = Action::ToggleKeyboard; break;
    case kDosMouse: case kDosMouseSpeed: case kDosCycles: case kDosAspect: case kDosSvga: {
        const int id = list[row].row - kDosMouse;
        if (mDos.options[id].dim) {
            setStatus(id == dosopt::kMouseSpeed ? "Nur im Mausmodus „Relativ“" : "Wirkt hier nicht", now);
            break;
        }
        mDosOption = id;
        mDosStep = step;
        action = Action::DosOption;
        break;
    }
    case kDosGainX: case kDosGainY: case kDosCalibrate:
        if (!mDos.relative) {
            setStatus("Nur im Mausmodus „Relativ“", now);
            break;
        }
        mDosOption = list[row].row == kDosGainY ? 1 : 0;
        mDosStep = step;
        action = list[row].row == kDosCalibrate ? Action::DosCalibrate : Action::DosGain;
        break;
    case kDosHires: action = Action::DosHires; break;
    case kDosLearn: action = Action::DosLearnPointer; break;
    case kCtlGrip: case kCtlModel: case kCtlDirection: case kCtlSpeed: case kCtlStick: {
        const auto r = static_cast<mapdrag::Row>(list[row].row - kCtlGrip);
        if (mapdrag::rowDim(r, mControls)) {
            setStatus("Nur bei „Rechter Griff: Karte ziehen“", now);
            break;
        }
        mControlRow = r;
        mDosStep = step;
        action = Action::ControlOption;
        break;
    }
    case kFitSize: action = Action::FitSize; break;
    case kDosRestart: case kDosReset: {
        // Wie Beenden: erster Klick schaltet sichtbar scharf, zweiter innerhalb 3 s löst aus.
        const Row r = list[row].row;
        if (r == mConfirmRow && now < mConfirmUntil) {
            mConfirmUntil = 0.0;
            action = r == kDosRestart ? Action::RestartGame : Action::ResetGameOptions;
        } else if (r == kDosReset && !mDos.userValues) {
            setStatus("Schon Spiel-Standard", now);
        } else {
            mConfirmRow = r;
            mConfirmUntil = now + 3.0;
            setStatus(r == kDosRestart ? "Neustart: Ungespeichertes geht verloren" : "Zurücksetzen: nochmal klicken", now);
            action = Action::ConfirmArmed;
        }
        break;
    }
    case kPadPair: {
        // Antippen: Konsolentaste wartet auf die nächste Quest-Taste (Flanke), 5 s lang.
        const auto& buttons = padmap::systemFor(mPadFolder).buttons;
        const int index = list[row].game + (zone >= kZoneBar0 ? zone - kZoneBar0 : 0);
        if (index >= 0 && index < static_cast<int>(buttons.size())) {
            mLearnTarget = mLearnTarget == buttons[index].id ? -1 : buttons[index].id;   // nochmal = abbrechen
            mLearnUntil = now + 5.0;
            mPadRowsDirty = true;
            if (mLearnTarget >= 0)
                setStatus(std::string("Quest-Taste für „") + buttons[index].name + "“ drücken", now);
        }
        break;
    }
    case kPadStickL:
    case kPadStickR: {
        // Analog nur bei Systemen mit Analogsticks (Shooter, DOS-Pad), sonst Steuerkreuz / Aus.
        const int stick = list[row].row == kPadStickL ? padmap::kLeftStick : padmap::kRightStick;
        const std::vector<int> modes = padmap::systemFor(mPadFolder).analog
            ? std::vector<int>{ padmap::kStickDpad, padmap::kStickAnalogLeft, padmap::kStickAnalogLeftDpad,
                                padmap::kStickAnalogLeftDpadY, padmap::kStickAnalogRight, padmap::kStickOff }
            : std::vector<int>{ padmap::kStickDpad, padmap::kStickOff };
        const auto it = std::find(modes.begin(), modes.end(), static_cast<int>(mPadMap.stick[stick]));
        const int at = it == modes.end() ? 0 : static_cast<int>(it - modes.begin());
        mPadMap.stick[stick] = static_cast<uint8_t>(modes[wrap(at + step, static_cast<int>(modes.size()))]);
        mPadRowsDirty = true;
        action = Action::PadChanged;
        break;
    }
    case kPadActions:
        mLearnTarget = -1;
        action = zone == kZoneBar0 ? Action::PadSaveSystem : Action::PadReset;
        break;
    case kQuickBar:
        if (zone == kZoneBar0) {
            if (mRecordAvailable) action = Action::ToggleRecording;
        } else if (zone == kZoneBar1) {
            action = Action::Close;
        } else if (zone == kZoneBar2) {
            if (now < mQuitArmedUntil) {
                action = Action::Quit;
            } else {
                armQuit(now);
                action = Action::QuitArmed;
            }
        }
        break;
    default: break;   // Bibliothekszeilen: activateLib
    }
    return action;
}

// Linke Spalte auslösen: Suchfeld (Tastatur an/aus, × leert), Spiel starten, Seite.
VrMenu::Action VrMenu::activateLib(const std::vector<Item>& lib, int index, Zone zone) {
    if (index < 0 || index >= static_cast<int>(lib.size())) return Action::None;
    switch (lib[index].row) {
    case kSearch:
        if (zone == kZoneMore && !mQuery.empty()) {
            mQuery.clear();
            refilter();
            mPage = 0;
            mLibDirty = true;
        } else {
            setSearchActive(!mSearchActive);
        }
        return Action::None;
    case kGame:
        mSelectedGame = lib[index].game;
        setSearchActive(false);
        return Action::StartGame;
    case kPage:
        if (pageCount() > 1) {
            mPage = wrap(mPage + (zone == kZoneLess ? -1 : 1), pageCount());
            mLibDirty = true;
        }
        return Action::None;
    default: return Action::None;
    }
}

// Erstes Ziel für Stick/A ohne Fokus: das erste Spiel der Liste, sonst die erste Zeile.
VrMenu::Focus VrMenu::initialFocus(const std::vector<Item>& list, const std::vector<Item>& lib) const {
    Focus f;
    for (int i = 0; i < static_cast<int>(lib.size()); ++i)
        if (lib[i].row == kGame) { f.lib = i; return f; }
    if (!lib.empty()) { f.lib = 0; return f; }
    for (int r = 0; r < static_cast<int>(list.size()); ++r)
        if (isFocusable(list[r].row)) {
            f.row = r;
            f.zone = arriveZone(list[r]);
            return f;
        }
    return f;
}

int VrMenu::keyToward(int from, int dir) const {
    if (from < 0 || from >= static_cast<int>(mKeys.size())) return -1;
    const Key& a = mKeys[from];
    if (dir == 3 || dir == 4) {
        const int k = from + (dir == 3 ? -1 : 1);
        return k >= 0 && k < static_cast<int>(mKeys.size()) && mKeys[k].row == a.row ? k : -1;
    }
    const int target = a.row + (dir == 1 ? -1 : 1), cx = (a.x0 + a.x1) / 2;
    int best = -1, bestDist = 1 << 30;
    for (int k = 0; k < static_cast<int>(mKeys.size()); ++k) {
        if (mKeys[k].row != target) continue;
        const int d = std::abs((mKeys[k].x0 + mKeys[k].x1) / 2 - cx);
        if (d < bestDist) { bestDist = d; best = k; }
    }
    return best;
}

// Rechter Stick: hoch/runter wandert durch Reiterleiste, Zeilen und Spieleliste, links/rechts
// wechselt die Spalte, schaltet Reiter, ändert Werte (mit Wiederholung) oder wählt den Knopf
// der Schnellleiste. Im Suchmodus läuft er über die Tasten.
void VrMenu::movePadFocus(const PadInput& pad, double now, const std::vector<Item>& list,
                          const std::vector<Tab>& tabList, ViewSettings& settings, Action& action) {
    const float ax = std::fabs(pad.stickX), ay = std::fabs(pad.stickY);
    int dir = 0;
    if (std::max(ax, ay) >= kStickOn) dir = ay >= ax ? (pad.stickY > 0 ? 1 : 2) : (pad.stickX < 0 ? 3 : 4);
    else if (mStickDir != 0 && std::max(ax, ay) > kStickOff) dir = mStickDir;   // Hysterese
    if (dir == 0) { mStickDir = 0; return; }
    if (dir == mStickDir && now < mStickNext) return;
    mStickNext = now + (dir == mStickDir ? kStickInterval : kStickDelay);
    mStickDir = dir;

    const std::vector<Item> lib = libRows();
    const int barRow = static_cast<int>(list.size()) - 1;
    auto nextRow = [&](int from, int stepDir) {
        for (int r = from; r >= 0 && r < static_cast<int>(list.size()); r += stepDir)
            if (isFocusable(list[r].row) && (!mSearchActive || list[r].row == kQuickBar)) return r;
        return -1;
    };
    auto libOk = [&](int i) {
        return i >= 0 && i < static_cast<int>(lib.size()) && isFocusable(lib[i].row) &&
               !(lib[i].row == kPage && pageCount() <= 1);
    };
    auto nextLib = [&](int from, int stepDir) {
        for (int i = from; i >= 0 && i < static_cast<int>(lib.size()); i += stepDir)
            if (libOk(i)) return i;
        return -1;
    };
    const int tabIndex = static_cast<int>(std::find(tabList.begin(), tabList.end(), mTab) - tabList.begin());
    auto bar = [&](int b) { Focus g; g.row = barRow; g.zone = static_cast<Zone>(kZoneBar0 + b); return g; };
    auto toRight = [&]() {
        Focus g;
        if (mSearchActive && !mKeys.empty()) g.key = 0;
        else g.tab = tabIndex;
        return g;
    };
    auto toLib = [&]() {
        Focus g;
        g.lib = initialFocus(list, lib).lib;
        if (g.lib < 0) g.lib = 0;
        return g;
    };

    mPadFocus = true;
    mFocusMoved = true;
    const Focus f = hover();
    if (!f.any()) { setHover(initialFocus(list, lib)); return; }
    Focus g = f;
    const bool vertical = dir == 1 || dir == 2;
    const int step = dir == 1 || dir == 3 ? -1 : 1;

    if (f.key >= 0) {
        const int k = keyToward(f.key, dir);
        if (k >= 0) { g = Focus(); g.key = k; }
        else if (dir == 2) g = bar(1);
        else if (dir == 3) { g = Focus(); g.lib = 0; }
    } else if (f.lib >= 0) {
        if (vertical) {
            const int n = nextLib(f.lib + step, step);
            const bool fromGame = lib[f.lib].row == kGame;
            if (fromGame && dir == 2 && n >= 0 && lib[n].row != kGame && mPage + 1 < pageCount()) {
                ++mPage;               // unter dem letzten Spiel der Seite: nächste Seite, erstes Spiel
                mLibDirty = true;
                g.lib = 1;
            } else if (fromGame && dir == 1 && n == 0 && mPage > 0) {
                --mPage;               // über dem ersten Spiel: vorige (volle) Seite, letztes Spiel
                mLibDirty = true;
                g.lib = gamesPerPage();
            } else if (n >= 0) {
                g.lib = n;
            } else if (dir == 2) {
                g = bar(0);
            }
            g.zone = kZoneRow;
        } else if (lib[f.lib].row == kPage) {
            action = activateLib(lib, f.lib, step < 0 ? kZoneLess : kZoneMore);
            mFocusMoved = false;
        } else if (dir == 4) {
            g = toRight();
        }
    } else if (f.tab >= 0) {
        if (dir == 2) {
            const int r = nextRow(0, 1);
            if (r >= 0) { g = Focus(); g.row = r; g.zone = arriveZone(list[r]); }
        } else if (!vertical) {
            const int t = tabIndex + step;
            if (t < 0 && mLibraryEnabled) {
                g = toLib();
            } else {
                const int c = std::min(std::max(t, 0), static_cast<int>(tabList.size()) - 1);
                if (tabList[c] != mTab) { mTab = tabList[c]; mRightDirty = true; }   // Höhe bleibt gleich
                g.tab = c;
            }
        }
    } else if (f.row >= 0 && zoneCount(list[f.row]) > 0 && list[f.row].row != kQuickBar) {
        // Zwei Knöpfe nebeneinander (Tasten-Reiter): links/rechts wechselt, ganz links → Liste.
        const int b = std::max(0, static_cast<int>(f.zone) - kZoneBar0);
        if (!vertical) {
            if (b + step < 0 && mLibraryEnabled) g = toLib();
            else g.zone = arriveZone(list[f.row], std::max(0, b + step));
        } else {
            const int r = nextRow(f.row + step, step);
            if (r >= 0) { g.row = r; g.zone = arriveZone(list[r], list[r].row == kQuickBar ? -1 : b); }
            else if (dir == 1) { g = Focus(); g.tab = tabIndex; }
        }
    } else if (f.row >= 0 && list[f.row].row == kQuickBar) {
        const int b = static_cast<int>(f.zone) - kZoneBar0;
        if (!vertical) {
            g.zone = static_cast<Zone>(kZoneBar0 + std::min(std::max(b + step, 0), 2));
        } else if (dir == 1) {
            if (mLibraryEnabled && b == 0) {
                g = Focus();
                g.lib = nextLib(static_cast<int>(lib.size()) - 1, -1);
            } else if (mSearchActive && !mKeys.empty()) {
                g = Focus();
                g.key = static_cast<int>(mKeys.size()) - 1;
            } else {
                const int r = nextRow(f.row - 1, -1);
                if (r >= 0) { g = Focus(); g.row = r; g.zone = arriveZone(list[r], b); }
            }
        }
    } else if (f.row >= 0) {
        if (vertical) {
            const int r = nextRow(f.row + step, step);
            if (r >= 0) { g.row = r; g.zone = arriveZone(list[r]); }
            else if (dir == 1) { g = Focus(); g.tab = tabIndex; }
        } else if (isValueRow(list[f.row].row)) {
            action = activate(list, f.row, step < 0 ? kZoneLess : kZoneMore, now, settings);
            mFocusMoved = false;   // Wertänderung: Klick-Haptik übernimmt der Aufrufer nicht doppelt
        } else if (dir == 3 && mLibraryEnabled) {
            g = toLib();
        }
    }
    setHover(g);
}

VrMenu::Action VrMenu::update(const XrPosef* localAim, bool triggerPressedEdge, bool triggerHeld,
                              const PadInput& pad, double now, ViewSettings& settings) {
    if (!mVisible) return Action::None;
    mFocusMoved = false;
    const std::vector<Tab> tabList = tabs();
    const Focus before = hover();
    const Tab oldTab = mTab;
    const bool oldSearch = mSearchActive;

    // --- Strahl: Ziel auf der Tafel bestimmen ---
    std::vector<Item> list = rows();
    std::vector<Item> lib = libRows();
    Focus ray;
    mHitDistance = 4.0f;
    if (localAim) {
        const RayHit hit = raycastToQuad(0.0f, 0.0f, mCz, mW, mH, *localAim);
        if (hit.hit) {
            mHitDistance = hit.distance;
            ray = hitTest(hit.u * static_cast<float>(mTexW), hit.v * static_cast<float>(mTexH), list, tabList, lib);
        }
    }
    // Der Strahl übernimmt den Fokus, sobald er auf ein anderes Ziel wandert; sonst bleibt
    // der Stick-Fokus stehen (auch wenn der Strahl die Tafel verfehlt).
    const bool rayMoved = ray != mRay;
    mRay = ray;
    if (rayMoved && ray.any()) mPadFocus = false;
    if (!mPadFocus) setHover(ray);

    Action action = Action::None;
    int changedRow = -1;   // Wertzeile, deren Anzeige sich geändert hat (Teil-Redraw)
    auto fire = [&](int row, Zone zone) {
        action = activate(list, row, zone, now, settings);
        if ((action == Action::Changed || action == Action::WindowChanged) && row >= 0) changedRow = row;
    };

    // --- Zuweisen (Reiter Tasten): die nächste neu gedrückte Quest-Taste übernimmt die
    // Konsolentaste; solange wirkt keine Taste aufs Menü. Nach 5 s ohne Taste: abbrechen.
    int sourceEdge = -1;
    for (int s = 0; s < padmap::kSourceCount; ++s) {
        if (pad.sources[s] && !mPrevSources[s] && sourceEdge < 0) sourceEdge = s;
        mPrevSources[s] = pad.sources[s];
    }
    const bool learning = mLearnTarget >= 0;
    if (learning) {
        if (sourceEdge >= 0) {
            padmap::assign(mPadMap, mLearnTarget, static_cast<padmap::Source>(sourceEdge));
            setStatus(std::string(padmap::sourceName(static_cast<padmap::Source>(sourceEdge))) + " zugewiesen", now);
            mLearnTarget = -1;
            mPadRowsDirty = true;
            action = Action::PadChanged;
        } else if (now >= mLearnUntil) {
            mLearnTarget = -1;
            mPadRowsDirty = true;
            setStatus("Zuweisen abgebrochen", now);
        }
    }

    // --- B schließt (im Suchmodus erst die Tastatur), Stick bewegt den Fokus, A löst aus ---
    if (learning) {
        // Eingaben gehören dem Zuweisen.
    } else if (pad.bEdge) {
        if (!mSearchActive) {
            mQuitArmedUntil = 0.0;
            mConfirmUntil = 0.0;
            return Action::Close;
        }
        setSearchActive(false);
    } else {
        movePadFocus(pad, now, list, tabList, settings, action);
        if (action == Action::Changed || action == Action::WindowChanged) changedRow = mHoverRow;
    }
    if (mTab != oldTab) list = rows();
    const bool aClick = !learning && pad.aEdge && hovered();
    if (!learning && pad.aEdge && !hovered()) {   // A ohne Fokus: Fokus setzen
        mPadFocus = true;
        mFocusMoved = true;
        setHover(initialFocus(list, lib));
    }

    // --- Trigger / A: Reiter wechseln, Spalte/Taste/Zeile auslösen ---
    const bool press = !learning && (triggerPressedEdge || aClick);
    if (press && mHoverTab >= 0) {
        if (tabList[mHoverTab] != mTab) {
            mTab = tabList[mHoverTab];
            if (!mPadFocus) mHoverRow = -1;
            mRightDirty = true;   // alle Reiter gleich hoch: nur die rechte Spalte neu
        }
    } else if (press && mHoverLib >= 0) {
        action = activateLib(lib, mHoverLib, mHoverZone);
    } else if (press && mHoverKey >= 0) {
        const int key = mHoverKey;
        pressKey(key);
        if (triggerPressedEdge && mSearchActive && mKeys[key].kind == kKeyBackspace) {
            mRepeatKey = key;   // Rücktaste gehalten: wiederholen
            mRepeatNext = now + kRepeatDelay;
        }
    } else if (press && mHoverRow >= 0) {
        fire(mHoverRow, mHoverZone);
        // Dauerdruck vormerken: nur auf ‹ › von Wertzeilen.
        if (triggerPressedEdge && (mHoverZone == kZoneLess || mHoverZone == kZoneMore) &&
            mHoverRow < static_cast<int>(list.size()) && isValueRow(list[mHoverRow].row)) {
            mRepeatRow = mHoverRow;
            mRepeatZone = mHoverZone;
            mRepeatNext = now + kRepeatDelay;
        }
    } else if (learning) {
        mRepeatRow = mRepeatKey = -1;
    } else if (triggerHeld && mRepeatRow >= 0 && mRepeatRow == mHoverRow && mRepeatZone == mHoverZone &&
               mTab == oldTab) {
        if (now >= mRepeatNext) {
            fire(mRepeatRow, mRepeatZone);
            mRepeatNext = now + kRepeatInterval;
        }
    } else if (triggerHeld && mRepeatKey >= 0 && mRepeatKey == mHoverKey) {
        if (now >= mRepeatNext) {
            pressKey(mRepeatKey);
            mRepeatNext = now + kRepeatInterval;
        }
    } else if (!triggerPressedEdge) {
        mRepeatRow = mRepeatKey = -1;
    }
    // Suchmodus gewechselt: Tastaturfokus gilt nicht mehr bzw. Zeilen sind verdeckt.
    if (!mSearchActive) mHoverKey = -1;
    if (mSearchActive != oldSearch) mRightDirty = true;

    // --- Neu zeichnen: so wenig wie möglich ---
    if (((now < mStatusUntil) != mStatusShown || mInfoDirty) && !mDirty) redrawTitle(now);
    const bool armedNow = now < mQuitArmedUntil;
    const int lastRow = static_cast<int>(rows().size()) - 1;
    if (settings != mShown) {
        if (changedRow >= 0 && !mDirty) {
            mShown = settings;
            redrawHover(settings, changedRow, changedRow, false);
        } else {
            mDirty = true;   // z. B. Profil geladen: alles kann sich geändert haben
        }
    }
    if (mDirty) {
        redraw(settings, now);
    } else {
        if (mLibDirty) redrawLibrary();   // Suche/Seite: nur die linke Spalte (+ Suchanzeige)
        if (mRightDirty) redrawRight(settings);   // Reiterwechsel, Suchtastatur an/aus
        if (mPadRowsDirty) redrawPadRows(settings);
        if (armedNow != mQuitShown) {     // Beenden scharf / zurückgefallen: nur die Leiste
            mQuitShown = armedNow;
            redrawHover(settings, lastRow, lastRow, false);
        }
        const bool confirmNow = now < mConfirmUntil;
        if (confirmNow != mConfirmShown) {   // Neustart/Zurücksetzen scharf: nur diese Zeile
            mConfirmShown = confirmNow;
            const int r = rowIndex(mConfirmRow);
            if (r >= 0) redrawHover(settings, r, r, false);
        }
        if (hover() != before) redrawFocus(settings, before, hover());
    }
    return action;
}

void VrMenu::fillRect(int x0, int y0, int x1, int y1, uint32_t color) {
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, mTexW); y1 = std::min(y1, mTexH);
    for (int y = y0; y < y1; ++y)
        std::fill(mPixels.begin() + y * mTexW + x0, mPixels.begin() + y * mTexW + x1, color);
}

// Bitmap-Schrift des Themes: ganzzahlig auf etwa px vergrößert (scharfe Pixel).
// Zeichen ohne Zelle kommen aus der Systemschrift.
int VrMenu::textWidth(const char* utf8, float px, bool bold) const {
    if (!mTheme.hasFont()) return sysTextWidth(utf8, px, bold);
    const int scale = std::max(1, static_cast<int>(std::lround(px / mTheme.fontCellH)));
    int w = 0;
    for (const char* s = utf8; *s;) {
        const char* start = s;
        uint32_t cp = utf8Next(s);
        if (mTheme.fontUpper) cp = latin1Upper(cp);
        if (const MenuTheme::Glyph* g = mTheme.glyph(cp)) w += (g->w + 1) * scale;
        else if (cp == ' ') w += mTheme.fontCellW * scale / 2;
        else w += sysTextWidth(std::string(start, s).c_str(), px, bold);
    }
    return w;
}

void VrMenu::text(const char* utf8, int x, int y, float px, uint32_t color, bool bold) {
    if (!mTheme.hasFont()) { sysText(utf8, x, y, px, color, bold); return; }
    const int scale = std::max(1, static_cast<int>(std::lround(px / mTheme.fontCellH)));
    const int top = y + (static_cast<int>(px) - mTheme.fontCellH * scale) / 2;
    const uint32_t mul = color == mColDim ? 128 : 256;   // gedimmt: halbe Helligkeit
    const ThemeImage& img = mTheme.font;
    for (const char* s = utf8; *s;) {
        const char* start = s;
        uint32_t cp = utf8Next(s);
        if (mTheme.fontUpper) cp = latin1Upper(cp);
        const MenuTheme::Glyph* g = mTheme.glyph(cp);
        if (!g) {
            if (cp == ' ') { x += mTheme.fontCellW * scale / 2; continue; }
            const std::string one(start, s);
            sysText(one.c_str(), x, y, px, color, bold);
            x += sysTextWidth(one.c_str(), px, bold);
            continue;
        }
        for (int py = 0; py < mTheme.fontCellH * scale; ++py) {
            const int ty = top + py;
            if (ty < 0 || ty >= mTexH) continue;
            const uint32_t* row = &img.px[static_cast<size_t>(g->y + py / scale) * img.w + g->x];
            for (int px2 = 0; px2 < g->w * scale; ++px2) {
                const uint32_t c = row[px2 / scale];
                const int tx = x + px2;
                if ((c >> 24) == 0 || tx < 0 || tx >= mTexW) continue;
                const uint32_t r = (c & 0xFF) * mul >> 8, gr = ((c >> 8) & 0xFF) * mul >> 8,
                               b = ((c >> 16) & 0xFF) * mul >> 8;
                mPixels[static_cast<size_t>(ty) * mTexW + tx] = r | (gr << 8) | (b << 16) | 0xFF000000u;
            }
        }
        x += (g->w + 1) * scale;
    }
}

int VrMenu::sysTextWidth(const char* utf8, float px, bool bold) const {
    const TextRaster& font = bold && mFontBold.ok() ? mFontBold : mFont;
    if (font.ok()) return font.width(utf8, px);
    const int scale = std::max(1, static_cast<int>(px / 6.0f));
    return static_cast<int>(std::strlen(utf8)) * 4 * scale - scale;
}

void VrMenu::sysText(const char* utf8, int x, int y, float px, uint32_t color, bool bold) {
    const TextRaster& font = bold && mFontBold.ok() ? mFontBold : mFont;
    if (font.ok()) {
        font.draw(mPixels.data(), mTexW, mTexH, utf8, x, y, px, color);
        return;
    }
    // Rückfall: 3×5-Pixelschrift (nur Großbuchstaben, keine Umlaute).
    const int scale = std::max(1, static_cast<int>(px / 6.0f));
    for (const char* c = utf8; *c; ++c, x += 4 * scale) {
        const uint8_t* glyph = pixelfont::glyphFor(static_cast<char>(
            std::toupper(static_cast<unsigned char>(*c))));
        if (!glyph) continue;
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (glyph[row] & (1 << (2 - col)))
                    fillRect(x + col * scale, y + row * scale, x + (col + 1) * scale,
                             y + (row + 1) * scale, color);
    }
}

// Pixel mit Deckung cov (0..1) über den Puffer mischen (Alpha wird mitgeführt).
// Ganzzahlig: läuft für jedes Pixel der Tafel, muss billig sein.
void VrMenu::blend(int x, int y, uint32_t color, float cov) {
    if (x < 0 || y < 0 || x >= mTexW || y >= mTexH || cov <= 0.0f) return;
    uint32_t& dst = mPixels[static_cast<size_t>(y) * mTexW + x];
    dst = over(color, dst, static_cast<uint32_t>(std::min(cov, 1.0f) * 255.0f + 0.5f));
}

// Abgerundetes Rechteck mit geglätteter Kante (Radius r in Pixeln). Nur in den
// vier Eckquadraten wird ein Abstand berechnet, der Rest hat volle Deckung.
void VrMenu::roundRect(int x0, int y0, int x1, int y1, int r, uint32_t color) {
    roundRectV(x0, y0, x1, y1, r, color, color);
}

// Wie roundRect, Farbe senkrecht von top (y0) nach bottom (y1) verlaufend.
void VrMenu::roundRectV(int x0, int y0, int x1, int y1, int r, uint32_t top, uint32_t bottom) {
    r = std::max(0, std::min(r, std::min(x1 - x0, y1 - y0) / 2));
    const int ya = std::max(y0, 0), yb = std::min(y1, mTexH);
    const int xa = std::max(x0, 0), xb = std::min(x1, mTexW);
    const uint8_t* mask = r > 0 ? cornerMask(r) : nullptr;   // einmal je Form nachschlagen
    for (int y = ya; y < yb; ++y) {
        const uint32_t color = top == bottom ? top
            : lerpColor(top, bottom, static_cast<float>(y - y0) / static_cast<float>(std::max(y1 - y0 - 1, 1)));
        const bool cornerRow = y < y0 + r || y >= y1 - r;
        uint32_t* row = &mPixels[static_cast<size_t>(y) * mTexW];
        // Volle Deckung: der Untergrund ist innerhalb einer Zeile meist gleich (Verlauf läuft
        // senkrecht) — letztes Mischergebnis wiederverwenden statt jedes Pixel neu zu rechnen.
        // Gleiche Rechnung (over), also bit-gleich mit dem Pixel-für-Pixel-Weg.
        // Eckbereiche (nur in Eckzeilen) geglättet, dazwischen volle Deckung in einer engen Schleife.
        const int midA = cornerRow ? std::max(xa, x0 + r) : xa;
        const int midB = cornerRow ? std::min(xb, x1 - r) : xb;
        const int ay = y < y0 + r ? y0 + r - y : y - (y1 - 1 - r);   // 1..r in Eckzeilen
        auto corner = [&](int x) {
            const int ax = x < x0 + r ? x0 + r - x : x - (x1 - 1 - r);
            const uint32_t cov = mask[(ay - 1) * r + (ax - 1)];
            if (cov) row[x] = over(color, row[x], cov);
        };
        for (int x = xa; x < midA; ++x) corner(x);
        if (midA < midB) blendRun(row + midA, midB - midA, color);
        for (int x = std::max(midB, midA); x < xb; ++x) corner(x);
    }
}

// Kantenglättung einer Ecke mit Radius r: Deckung (0..255) je Abstand |dx|, |dy| = 1..r zum
// Eckmittelpunkt — einmal je Radius gerechnet statt einer Wurzel pro Eckpixel.
const uint8_t* VrMenu::cornerMask(int r) const {
    auto it = mCornerMasks.find(r);
    if (it != mCornerMasks.end()) return it->second.data();
    std::vector<uint8_t> m(static_cast<size_t>(r) * r);
    for (int dy = 1; dy <= r; ++dy)
        for (int dx = 1; dx <= r; ++dx) {
            const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            const float cov = std::min(std::max(static_cast<float>(r) + 0.5f - d, 0.0f), 1.0f);
            m[static_cast<size_t>(dy - 1) * r + (dx - 1)] = cov > 0.0f ? static_cast<uint8_t>(cov * 255.0f + 0.5f) : 0;
        }
    return mCornerMasks.emplace(r, std::move(m)).first->second.data();
}

// Ausgefüllter Kreis, geglättet.
void VrMenu::circle(int cx, int cy, int r, uint32_t color) {
    roundRect(cx - r, cy - r, cx + r, cy + r, r, color);
}

// --- Menü-Optik aus Spielgrafik (xrshell.theme) --------------------------------

// Bild auf ein Rechteck strecken, nächster Nachbar (Pixelgrafik bleibt scharf).
void VrMenu::image(const ThemeImage& img, int x0, int y0, int x1, int y1) {
    if (!img.ok() || x1 <= x0 || y1 <= y0) return;
    for (int y = std::max(y0, 0); y < std::min(y1, mTexH); ++y) {
        const int sy = (y - y0) * img.h / (y1 - y0);
        for (int x = std::max(x0, 0); x < std::min(x1, mTexW); ++x) {
            const uint32_t p = img.px[static_cast<size_t>(sy) * img.w + (x - x0) * img.w / (x1 - x0)];
            blend(x, y, p, 1.0f);
        }
    }
}

// Dreiteilig: linker/rechter Rand im Seitenverhältnis der Höhe, Mitte gestreckt —
// Knöpfe behalten scharfe Kanten, egal wie breit die Zeile ist.
void VrMenu::image3(const ThemeImage& img, int x0, int y0, int x1, int y1) {
    if (!img.ok() || x1 <= x0 || y1 <= y0) return;
    const int capSrc = std::max(1, std::min(img.w / 3, 8));
    const int capDst = std::min((x1 - x0) / 3, capSrc * (y1 - y0) / img.h);
    auto part = [&](int sx0, int sx1, int dx0, int dx1) {
        for (int y = std::max(y0, 0); y < std::min(y1, mTexH); ++y) {
            const int sy = (y - y0) * img.h / (y1 - y0);
            for (int x = std::max(dx0, 0); x < std::min(dx1, mTexW); ++x) {
                const int sx = sx0 + (x - dx0) * (sx1 - sx0) / std::max(dx1 - dx0, 1);
                blend(x, y, img.px[static_cast<size_t>(sy) * img.w + sx], 1.0f);
            }
        }
    };
    part(0, capSrc, x0, x0 + capDst);
    part(capSrc, img.w - capSrc, x0 + capDst, x1 - capDst);
    part(img.w - capSrc, img.w, x1 - capDst, x1);
}

// Bereich mit dem Tafelhintergrund füllen: Tafelbild über die ganze Tafel
// gestreckt gedacht, damit neu gezeichnete Zeilen nahtlos passen.
// Tafelfarbe in Zeile y, wie sie redraw() erzeugt: Verlauf über dem Akzentrand.
uint32_t VrMenu::panelColor(int y) const {
    const float t = static_cast<float>(y - 3) / static_cast<float>(std::max(mTexH - 7, 1));
    return over(lerpColor(kPanelTop, kPanelBottom, t), over(kAccentSoft, 0u));   // Rand liegt auf Transparenz
}

void VrMenu::panelArea(int x0, int y0, int x1, int y1) {
    if (!mTheme.active) {
        for (int y = std::max(y0, 0); y < std::min(y1, mTexH); ++y)
            fillRect(x0, y, x1, y + 1, panelColor(y));
        return;
    }
    const ThemeImage& img = mTheme.panel;
    for (int y = std::max(y0, 0); y < std::min(y1, mTexH); ++y) {
        const int sy = y * img.h / mTexH;
        for (int x = std::max(x0, 0); x < std::min(x1, mTexW); ++x)
            mPixels[static_cast<size_t>(y) * mTexW + x] =
                img.px[static_cast<size_t>(sy) * img.w + x * img.w / mTexW] | 0xFF000000u;
    }
}

void VrMenu::tint(int x0, int y0, int x1, int y1, uint32_t color) {
    for (int y = std::max(y0, 0); y < std::min(y1, mTexH); ++y)
        for (int x = std::max(x0, 0); x < std::min(x1, mTexW); ++x)
            if (mPixels[static_cast<size_t>(y) * mTexW + x] >> 24) blend(x, y, color, 1.0f);
}

// Zeilenkarte: leichter Verlauf mit Lichtkante oben; unter dem Fokus blau aufgehellt,
// mit feinem Akzent-Leuchtrand und Akzentbalken links.
void VrMenu::hoverCard(int x0, int y0, int x1, int y1, bool hover) {
    if (hover) {
        roundRect(x0, y0, x1, y1, 16, withAlpha(mColAccent, 150));
        roundRectV(x0 + 2, y0 + 2, x1 - 2, y1 - 2, 14, kHoverTop, kHoverBottom);
        roundRect(x0 + 8, y0 + 12, x0 + 14, y1 - 12, 3, mColAccent);
    } else {
        roundRectV(x0, y0, x1, y1, 16, kRowTop, kRowBottom);
    }
    roundRect(x0 + 16, y0 + 1, x1 - 16, y0 + 2, 0, rgba(255, 255, 255, hover ? 46 : 22));   // Lichtkante
}

// Knopf: Rand in tone, innen leicht getönt; gefüllt (Fokus/aktiv) als Verlauf in tone.
void VrMenu::softButton(int x0, int y0, int x1, int y1, int r, uint32_t tone, bool filled) {
    if (filled) {
        roundRectV(x0, y0, x1, y1, r, lighten(tone, 40), tone);
        return;
    }
    roundRect(x0, y0, x1, y1, r, tone);
    roundRect(x0 + 3, y0 + 3, x1 - 3, y1 - 3, r - 3, panelColor((y0 + y1) / 2));
    roundRectV(x0 + 3, y0 + 3, x1 - 3, y1 - 3, r - 3, withAlpha(tone, 52), withAlpha(tone, 22));
}

// Schriftgröße, bei der text in maxW passt (von px abwärts bis 22 px).
float VrMenu::fitPx(const char* utf8, int maxW, float px, bool bold) const {
    while (px > 22.0f && textWidth(utf8, px, bold) > maxW) px -= 2.0f;
    return px;
}

// UTF-8-sicher kürzen und „…“ anhängen, bis der Text in maxW passt.
std::string VrMenu::ellipsize(std::string s, int maxW, float px, bool bold) const {
    if (textWidth(s.c_str(), px, bold) <= maxW) return s;
    // Längstes passendes Präfix (ganze UTF-8-Zeichen) per Halbierung statt Zeichen für Zeichen.
    std::vector<size_t> cuts;   // Bytegrenzen nach jedem Zeichen
    for (size_t i = 1; i <= s.size(); ++i)
        if (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) cuts.push_back(i);
    int lo = 0, hi = static_cast<int>(cuts.size()) - 1;   // Anzahl Zeichen in [lo, hi]
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (textWidth((s.substr(0, cuts[mid - 1]) + "…").c_str(), px, bold) <= maxW) lo = mid;
        else hi = mid - 1;
    }
    return (lo > 0 ? s.substr(0, cuts[lo - 1]) : std::string()) + "…";
}

// Reiter als Segment-Schalter (Hintergrund der Leiste liegt schon im Puffer).
void VrMenu::drawTabs() {
    const std::vector<Tab> tabList = tabs();
    const int tabCount = static_cast<int>(tabList.size());
    const bool themed = mTheme.active && mTheme.button.ok();
    const int cx0 = contentX0();
    if (!themed) roundRect(cx0 + 24, kTitleH + 8, mTexW - 24, kTitleH + kTabH - 8, 26, kTrack);
    for (int t = 0; t < tabCount; ++t) {
        const char* name = tabList[t] == kTabView ? (mDosPage ? "Bild" : "Ansicht")
                         : tabList[t] == kTabPointer ? "Zeiger" : tabList[t] == kTabPad ? "Tasten"
                         : tabList[t] == kTabDos ? "DOS" : tabList[t] == kTabControls ? "Steuerung" : "Spiel";
        const int x0 = cx0 + 30 + t * (mTexW - cx0 - 60) / tabCount;
        const int x1 = cx0 + 30 + (t + 1) * (mTexW - cx0 - 60) / tabCount;
        // Viele Reiter (xr.dosgames): zu breite Namen kleiner setzen statt überlaufen lassen.
        const float px = fitPx(name, x1 - x0 - 14, kTextPx, true);
        const bool active = tabList[t] == mTab, hover = t == mHoverTab;
        if (themed) {
            // Spielknopf je Reiter; aktiv in Akzentfarbe getönt, unter dem Strahl aufgehellt.
            const int y0 = kTitleH + 14, y1 = kTitleH + kTabH - 10;
            image3(mTheme.button, x0 + 3, y0, x1 - 3, y1);
            if (active) tint(x0 + 3, y0, x1 - 3, y1, mTheme.accent);
            else if (hover) tint(x0 + 3, y0, x1 - 3, y1, mTheme.hover);
        } else if (active) {
            roundRectV(x0, kTitleH + 14, x1, kTitleH + kTabH - 14, 22, lighten(mColAccent, 50), mColAccent);
            if (hover) roundRect(x0 + 2, kTitleH + kTabH - 20, x1 - 2, kTitleH + kTabH - 16, 2, kTextDark);
        } else if (hover) {
            roundRectV(x0, kTitleH + 14, x1, kTitleH + kTabH - 14, 22, kHoverTop, kHoverBottom);
        }
        text(name, (x0 + x1 - textWidth(name, px, true)) / 2,
             kTitleH + (kTabH - static_cast<int>(px)) / 2, px,
             active ? mColDark : mColText, true);
    }
}

// Suchmodus: rechte Reiterleiste zeigt die Anfrage groß (der Blick liegt auf der Tastatur).
void VrMenu::drawSearchBand() {
    const int x0 = contentX0() + 24, x1 = mTexW - 24;
    const int y0 = kTitleH + 8, y1 = kTitleH + kTabH - 8;
    roundRect(x0 - 4, y0 - 4, x1 + 4, y1 + 4, 32, withAlpha(mColAccent, 70));   // Leuchten
    roundRect(x0, y0, x1, y1, 28, mColAccent);
    roundRectV(x0 + 3, y0 + 3, x1 - 3, y1 - 3, 25, kField, lighten(kField, 10));
    char hits[32];
    std::snprintf(hits, sizeof(hits), "%d Treffer", static_cast<int>(mFiltered.size()));
    const int hw = textWidth(hits, kSmallPx);
    text(hits, x1 - 28 - hw, (y0 + y1 - static_cast<int>(kSmallPx)) / 2, kSmallPx, mColDim);
    const int tx = x0 + 28, ty = (y0 + y1 - static_cast<int>(kTextPx)) / 2;
    const char* label = "Suche:";
    text(label, tx, ty, kTextPx, mColDim);
    const int qx = tx + textWidth(label, kTextPx) + 14;
    const std::string q = ellipsize(mQuery, x1 - 60 - hw - qx, kTextPx, true);
    text(q.c_str(), qx, ty, kTextPx, mColText, true);
    const int cx = qx + textWidth(q.c_str(), kTextPx, true) + 4;
    fillRect(cx, y0 + 14, cx + 3, y1 - 14, mColAccent);   // Schreibmarke
}

// Taste der Suchtastatur: dunkel, unter dem Fokus in Akzentfarbe; „Fertig“ als Akzent-Umriss.
void VrMenu::drawKey(int k) {
    const Key& key = mKeys[k];
    const bool hover = k == mHoverKey;
    const bool done = key.kind == kKeyDone;
    const bool special = key.kind == kKeyBackspace || key.kind == kKeyClear || key.kind == kKeySpace;
    // Tastenkappe: Schatten unten, Verlauf, Lichtkante; Fokus hebt die Taste in Akzentfarbe an.
    const int y0 = key.y0, y1 = key.y1 - 5;
    roundRect(key.x0, y0 + 5, key.x1, key.y1, 14, rgba(0, 0, 0, 150));
    if (hover) {
        roundRect(key.x0 - 3, y0 - 3, key.x1 + 3, y1 + 3, 17, withAlpha(mColAccent, 90));   // Leuchten
        roundRectV(key.x0, y0, key.x1, y1, 14, lighten(mColAccent, 60), mColAccent);
    } else if (done) {
        softButton(key.x0, y0, key.x1, y1, 14, mColAccent, false);
    } else {
        const uint32_t base = special ? kKeySpecial : kKey;
        roundRectV(key.x0, y0, key.x1, y1, 14, lighten(base, 18), base);
        roundRect(key.x0 + 14, y0 + 1, key.x1 - 14, y0 + 2, 0, kKeyEdge);   // Lichtkante oben
    }
    const float px = key.kind == kKeyChar || key.kind == kKeyBackspace ? kTitlePx - 4.0f : kTextPx - 4.0f;
    const uint32_t ink = hover ? mColDark : done ? lighten(mColAccent, 40) : mColText;
    const int w = textWidth(key.label, px, true);
    text(key.label, (key.x0 + key.x1 - w) / 2, (y0 + y1 - static_cast<int>(px)) / 2, px, ink, true);
}

// Eine Zeile der rechten Spalte zeichnen (Fläche vorher geleert).
void VrMenu::drawRow(size_t i, const Item& item, const ViewSettings& settings) {
    char value[48];
    const Row row = item.row;
    const int top = rowTop(static_cast<int>(i));
    const bool hover = static_cast<int>(i) == mHoverRow;
    const int ty = top + (kRowH - static_cast<int>(kTextPx)) / 2;
    if (row == kSpacer) return;   // Füllzeile: Tafelhintergrund bleibt
    const int cx0 = contentX0(), cx1 = mTexW;
    if (row == kPadHint || row == kPadPair || row == kPadActions) {
        drawPadRow(i, item);
        return;
    }
    if (row == kHelp) {
        // „Taste Wirkung · Taste Wirkung“: Teile bis zum ersten Leerzeichen fett/hell, Rest gedimmt.
        const std::string& line = mHelp[static_cast<size_t>(item.game)];
        const float px = kSmallPx;
        int x = cx0 + 44;
        const int y = top + (kRowH - static_cast<int>(px)) / 2;
        size_t pos = 0;
        while (pos < line.size()) {
            size_t sep = line.find(" \xC2\xB7 ", pos);   // „ · “
            const std::string part = line.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
            const size_t sp = part.find(' ');
            const std::string key = part.substr(0, sp), what = sp == std::string::npos ? "" : part.substr(sp);
            text(key.c_str(), x, y, px, mColText, true);
            x += textWidth(key.c_str(), px, true);
            text(what.c_str(), x, y, px, mColDim);
            x += textWidth(what.c_str(), px);
            if (sep == std::string::npos) break;
            text(" \xC2\xB7 ", x, y, px, mColDim);
            x += textWidth(" \xC2\xB7 ", px);
            pos = sep + 4;   // Länge von „ · “ in UTF-8
        }
        return;
    }
    if (row == kDosNoGame || row == kNoStates) {
        const char* hint = row == kNoStates ? "Spielstände: im Spiel speichern (Spielmenü)"
                                            : "Erst ein Spiel starten (Liste links)";
        text(hint, (cx0 + cx1 - textWidth(hint, kSmallPx)) / 2, top + (kRowH - static_cast<int>(kSmallPx)) / 2,
             kSmallPx, mColDim);
        return;
    }
    const bool isAction = !isValueRow(row) && row != kQuickBar;
    const bool themed = mTheme.active && mTheme.button.ok();
    if (!isAction && row != kQuickBar) {
        if (themed) {
            image3(mTheme.button, cx0 + 20, top + 5, cx1 - 20, top + kRowH - 5);
            if (hover) tint(cx0 + 20, top + 5, cx1 - 20, top + kRowH - 5, mTheme.hover);
        } else {
            hoverCard(cx0 + 20, top + 5, cx1 - 20, top + kRowH - 5, hover);
        }
    }

    if (row == kQuickBar) {
        // Drei Knöpfe nebeneinander: Aufnahme (rot, mit Laufzeit) · Schließen · Beenden.
        char rec[32];
        if (mRecording)
            std::snprintf(rec, sizeof(rec), "\xE2\x96\xA0 %d:%02d", mRecordSeconds / 60, mRecordSeconds % 60);
        else
            std::snprintf(rec, sizeof(rec), "\xE2\x97\x8F Video");
        // Beenden scharf (erster Klick): rot gefüllt mit Rückfrage, fällt nach 3 s zurück.
        const char* captions[3] = { rec, "Schließen", mQuitShown ? "Sicher?" : "Beenden" };
        const int span = (mTexW - 40) / 3;
        for (int b = 0; b < 3; ++b) {
            const int x0 = 20 + b * span + 6, x1 = 20 + (b + 1) * span - 6;
            const bool zoneHover = hover && mHoverZone == static_cast<Zone>(kZoneBar0 + b);
            const bool dim = b == 0 && !mRecordAvailable;
            const uint32_t tone = b == 0 ? (dim ? mColDim : kWarn) : b == 1 ? mColDim : kWarn;
            const int tw = textWidth(captions[b], kTextPx, true);
            if (themed) {
                image3(mTheme.button, x0, top + 7, x1, top + kRowH - 7);
                if (zoneHover) tint(x0, top + 7, x1, top + kRowH - 7, mTheme.hover);
                const bool armed = b == 2 && mQuitShown;
                if ((b == 0 && mRecording) || armed) tint(x0, top + 7, x1, top + kRowH - 7, armed ? rgba(224, 48, 32, 176) : 0x60202AFFu);
                text(captions[b], (x0 + x1 - tw) / 2, ty, kTextPx, armed ? rgba(255, 255, 255, 255) : b == 1 ? mColText : (dim ? mColDim : kWarn), true);
                continue;
            }
            const bool filled = zoneHover || (b == 0 && mRecording) || (b == 2 && mQuitShown);
            softButton(x0, top + 7, x1, top + kRowH - 7, 24, tone, filled);
            const uint32_t ink = filled ? mColDark : (b == 1 ? mColText : lighten(tone, 30));
            if (b == 0) {
                // ● / ■ fehlen in der Systemschrift → Symbol als Form vor den Text.
                const char* label = captions[0] + 4;   // UTF-8-Symbol (3 Byte) + Leerzeichen
                const int lw = textWidth(label, kTextPx, true), icon = 16, gap = 10;
                const int ix = (x0 + x1 - lw - icon - gap) / 2, cy = top + kRowH / 2;
                if (mRecording) fillRect(ix, cy - icon / 2, ix + icon, cy + icon / 2, ink);
                else circle(ix + icon / 2, cy, icon / 2, ink);
                text(label, ix + icon + gap, ty, kTextPx, ink, true);
                continue;
            }
            text(captions[b], (x0 + x1 - tw) / 2, ty, kTextPx, ink, true);
        }
        return;
    }

    const char* label = nullptr;
    bool dim = false, accentValue = false;
    value[0] = '\0';
    switch (row) {
    case kPassthrough:
        label = "Passthrough";
        if (mPassthroughAvailable) std::snprintf(value, sizeof(value), "%d %%", settings.passthrough);
        else std::snprintf(value, sizeof(value), "nicht verfügbar");
        dim = !mPassthroughAvailable;
        break;
    case kCurve: label = "Wölbung"; std::snprintf(value, sizeof(value), "%s", kCurveNames[wrap(settings.curve, 4)]); break;
    case kFilter: label = "Filter"; std::snprintf(value, sizeof(value), "%s", kFilterNames[wrap(settings.filter, kScreenFilterCount)]); break;
    case kSaturation: label = "Sättigung"; std::snprintf(value, sizeof(value), "%d %%", settings.saturation); break;
    case kContrast: label = "Kontrast"; std::snprintf(value, sizeof(value), "%d %%", settings.contrast); break;
    case kSize: label = "Bildgröße"; meters(value, sizeof(value), settings.width); break;
    case kDistance: label = "Abstand"; meters(value, sizeof(value), settings.distance); break;
    case kEnvironment: label = "Umgebung (exp.)"; std::snprintf(value, sizeof(value), "%s", kEnvNames[wrap(settings.environment, 4)]); break;
    case kRayColor: label = "Farbe"; std::snprintf(value, sizeof(value), "%s", kRayColorNames[wrap(settings.rayColor, ViewSettings::kRayColors)]); break;
    case kRayStyle: label = "Strahl"; std::snprintf(value, sizeof(value), "%s", kRayStyleNames[wrap(settings.rayStyle, ViewSettings::kRayStyles)]); break;
    case kMarker: label = "Zielpunkt"; std::snprintf(value, sizeof(value), "%s", kMarkerNames[wrap(settings.marker, ViewSettings::kMarkers)]); break;
    case kGameCursor:
        label = "Spiel-Mauszeiger";
        std::snprintf(value, sizeof(value), "%s",
                      kGameCursorNames[wrap(settings.gameCursor, ViewSettings::kGameCursors)]);
        dim = !mGameCursorAvailable;   // Core kann ihn (noch) nicht ausblenden
        break;
    case kPointerOffsetX: case kPointerOffsetY:
        // ‹ = Spielzeiger nach links/oben, › = nach rechts/unten (wirkt nach dem Schließen).
        label = row == kPointerOffsetX ? "Zeiger waagerecht" : "Zeiger senkrecht";
        std::snprintf(value, sizeof(value), "%s",
                      ptroff::label(row == kPointerOffsetX ? settings.pointerOffsetPercentX()
                                                           : settings.pointerOffsetPercentY()).c_str());
        break;
    case kZoom: label = "Bildzoom (X)"; std::snprintf(value, sizeof(value), "%s", kZoomNames[wrap(settings.zoom, ViewSettings::kZooms)]); break;
    case kPadStickL:
    case kPadStickR: {
        const int stick = row == kPadStickL ? padmap::kLeftStick : padmap::kRightStick;
        label = stick == padmap::kLeftStick ? "Linker Stick" : "Rechter Stick";
        std::snprintf(value, sizeof(value), "%s",
                      padmap::stickModeName(static_cast<padmap::StickMode>(mPadMap.stick[stick])));
        break;
    }
    case kStateSlot: label = "Spielstand"; std::snprintf(value, sizeof(value), "Platz %d", mStateSlot); break;
    case kDosMouse: case kDosMouseSpeed: case kDosCycles: case kDosAspect: case kDosSvga: {
        const DosPage::Option& o = mDos.options[row - kDosMouse];
        label = kDosLabels[row - kDosMouse];
        std::snprintf(value, sizeof(value), "%s", o.value.c_str());
        dim = o.dim;
        accentValue = o.pending;   // wirkt erst nach „Spiel neu starten“
        break;
    }
    case kDosGainX: case kDosGainY:
        label = row == kDosGainX ? "Maus waagerecht" : "Maus senkrecht";
        std::snprintf(value, sizeof(value), "%s", (row == kDosGainX ? mDos.gainX : mDos.gainY).c_str());
        dim = !mDos.relative;
        break;
    case kCtlGrip: case kCtlModel: case kCtlDirection: case kCtlSpeed: case kCtlStick: {
        const auto r = static_cast<mapdrag::Row>(row - kCtlGrip);
        label = mapdrag::rowLabel(r, mControls);
        std::snprintf(value, sizeof(value), "%s", mapdrag::valueLabel(r, mControls).c_str());
        dim = mapdrag::rowDim(r, mControls);
        break;
    }
    case kProfileSlot: label = "Profil"; std::snprintf(value, sizeof(value), "Profil %d", mProfileSlot); break;
    case kStartMode:
        label = "Beim Start";
        std::snprintf(value, sizeof(value), "%s",
                      wrap(settings.startMode, ViewSettings::kStartModes) == ViewSettings::kStartResume
                          ? "Letztes Spiel" : "Menü");
        break;
    default: break;
    }
    if (label) {
        // ‹ › rechtsbündig im Raster (bei 1024 px: 560–632 und 888–960, Wertmitte 760).
        const int lessX0 = cx1 - 464, lessX1 = cx1 - 392, moreX0 = cx1 - 136, moreX1 = cx1 - 64;
        const int valueCenter = cx1 - 264;
        text(label, cx0 + 44, ty, kTextPx, dim ? mColDim : mColText);
        // Wert in einer Kapsel zwischen zwei runden Pfeil-Knöpfen.
        if (!themed) roundRect(lessX0 + 20, top + 12, moreX1 - 20, top + kRowH - 12, 20, kTrack);
        for (int z = 0; z < 2; ++z) {
            const int cx = z == 0 ? (lessX0 + lessX1) / 2 : (moreX0 + moreX1) / 2;
            const bool zoneHover = hover && mHoverZone == (z == 0 ? kZoneLess : kZoneMore);
            // Spielgrafik: Pfeilknöpfe des Spiels (gedrückt unter dem Strahl).
            const ThemeImage& arrowImg = z == 0
                ? (zoneHover && mTheme.arrowLeftPressed.ok() ? mTheme.arrowLeftPressed : mTheme.arrowLeft)
                : (zoneHover && mTheme.arrowRightPressed.ok() ? mTheme.arrowRightPressed : mTheme.arrowRight);
            if (themed && arrowImg.ok()) {
                image(arrowImg, cx - 22, top + kRowH / 2 - 22, cx + 22, top + kRowH / 2 + 22);
                continue;
            }
            knob(cx, top + kRowH / 2, z == 0, zoneHover);
        }
        text(value, valueCenter - textWidth(value, kTextPx) / 2, ty, kTextPx,
             dim ? mColDim : accentValue ? mColAccent : mColText);
    } else {
        const std::string hires = "Hohe Auflösung umschalten (" + mDos.hiresKey + ")";
        std::string resume = "Weiterspielen";
        if (row == kResume && mLastGame >= 0) {
            const std::string& entry = mLibrary[mLastGame];
            resume += ": " + entry.substr(entry.find('\t') == std::string::npos ? 0 : entry.find('\t') + 1);
        }
        const bool armed = isConfirmRow(row) && row == mConfirmRow && mConfirmShown;
        const char* caption = armed ? "Sicher? Nochmal klicken"
                            : row == kResume ? resume.c_str()
                            : row == kGameMenu ? "Spielmenü öffnen (Speichern/Laden)"
                            : row == kRecenter ? "Fenster zentrieren"
                            : row == kSaveState ? "Spielstand speichern"
                            : row == kLoadState ? "Spielstand laden"
                            : row == kSaveProfile ? "Profil speichern"
                            : row == kLoadProfile ? "Profil laden"
                            : row == kDosHires ? hires.c_str()
                            : row == kDosRestart ? (mDos.restartPending ? "Neu starten: Änderung übernehmen"
                                                                        : "Spiel neu starten")
                            : row == kDosReset ? "Spiel-Standard wiederherstellen"
                            : row == kFitSize ? "Größe passend zum Spiel"
                            : row == kDosCalibrate ? "Abgleichen: Laser auf Spielzeiger + Trigger"
                            : row == kDosLearn ? (mDos.cursor == 1 ? "Zeiger gekoppelt – neu lernen"
                                                : mDos.cursor == 2 ? "Kopplung pausiert (sucht) – neu lernen"
                                                : mDos.cursor == 3 ? "Zeiger wird gelernt …"
                                                                   : "Zeiger lernen (Pfeil an den Laser koppeln)")
                                                   : "Tastatur ein/aus";
        // Aktionen als Knöpfe: getönt, beim Zielen gefüllt. Neustart/Zurücksetzen rot (verwerfen
        // ungespeicherten Fortschritt bzw. eigene Werte), scharf rot gefüllt; ohne Wirkung gedimmt.
        const bool danger = isConfirmRow(row);
        const bool off = (row == kDosReset && !mDos.userValues) || (row == kDosCalibrate && !mDos.relative);
        const uint32_t tone = off ? mColDim : danger ? kWarn : mColAccent;
        const bool filled = hover || armed || row == kResume;   // Weiterspielen: Hauptknopf, immer gefüllt
        const int mid = (cx0 + cx1) / 2;
        const float px = fitPx(caption, cx1 - cx0 - 120, kTextPx, true);
        const int bw = std::min(cx1 - cx0 - 80, textWidth(caption, px, true) + 90);
        const int bx0 = mid - bw / 2, bx1 = bx0 + bw;
        const int tx = mid - textWidth(caption, px, true) / 2, cty = top + (kRowH - static_cast<int>(px)) / 2;
        if (themed) {
            image3(mTheme.button, bx0, top + 7, bx1, top + kRowH - 7);
            if (hover) tint(bx0, top + 7, bx1, top + kRowH - 7, mTheme.hover);
            if (armed) tint(bx0, top + 7, bx1, top + kRowH - 7, rgba(224, 48, 32, 176));
            text(caption, tx, cty, px,
                 armed ? rgba(255, 255, 255, 255) : danger && !off ? kWarn : off ? mColDim : mColText, true);
            return;
        }
        softButton(bx0, top + 7, bx1, top + kRowH - 7, 24, tone, filled);
        text(caption, tx, cty, px, filled ? mColDark : lighten(tone, 30), true);
    }
}

// Runder Pfeilknopf ‹ / › mit Verlauf; unter dem Fokus in Akzentfarbe.
void VrMenu::knob(int cx, int cy, bool left, bool hover) {
    const uint32_t base = hover ? mColAccent : kKnob;
    roundRectV(cx - 24, cy - 24, cx + 24, cy + 24, 24, lighten(base, hover ? 50 : 22), base);
    const char* arrow = left ? "\xE2\x80\xB9" : "\xE2\x80\xBA";   // ‹ ›
    text(arrow, cx - textWidth(arrow, kTextPx, true) / 2, cy - static_cast<int>(kTextPx) / 2 - 2, kTextPx,
         hover ? mColDark : mColText, true);
}

// Eine Zeile der linken Spalte (Bibliothek) zeichnen (Fläche vorher geleert).
void VrMenu::drawLib(size_t i, const Item& item) {
    const int top = libTop(static_cast<int>(i));
    const bool hover = static_cast<int>(i) == mHoverLib;
    const int x0 = 24, x1 = kLibW - 12;
    const bool themed = mTheme.active && mTheme.button.ok();
    switch (item.row) {
    case kSearch: {
        // Suchfeld: Kapsel mit Lupe, Text bzw. Platzhalter, × zum Leeren; aktiv mit Akzentrand.
        const int y0 = kTitleH + 8, y1 = kTitleH + kTabH - 8, cy = (y0 + y1) / 2;
        const bool clearHover = hover && mHoverZone == kZoneMore;
        if (themed) {
            image3(mTheme.button, x0, y0, x1, y1);
            if (hover || mSearchActive) tint(x0, y0, x1, y1, mTheme.hover);
        } else {
            const bool lit = mSearchActive || (hover && !clearHover);
            if (lit) roundRect(x0 - 4, y0 - 4, x1 + 4, y1 + 4, 32, withAlpha(mColAccent, 70));   // Leuchten
            roundRect(x0, y0, x1, y1, 28, lit ? mColAccent : kFieldEdge);
            roundRectV(x0 + 3, y0 + 3, x1 - 3, y1 - 3, 25, kField, lighten(kField, 10));
        }
        const uint32_t ink = mSearchActive ? mColAccent : mColDim;
        const int lx = x0 + 32, ly = cy - 4;   // Lupe: Ring + Stiel
        circle(lx, ly, 12, ink);
        circle(lx, ly, 8, themed ? rgba(0, 0, 0, 200) : lerpColor(kField, lighten(kField, 10), 0.4f));
        for (int t = 0; t <= 8; ++t) circle(lx + 9 + t, ly + 9 + t, 2, ink);
        const int tx = x0 + 62, ty = cy - static_cast<int>(kTextPx) / 2;
        const int right = mQuery.empty() ? x1 - 24 : x1 - 64;
        if (mQuery.empty()) {
            text(mSearchActive ? "" : "Spiele suchen …", tx, ty, kTextPx, mColDim);
        } else {
            const std::string q = ellipsize(mQuery, right - tx - 8, kTextPx);
            text(q.c_str(), tx, ty, kTextPx, mColText);
        }
        if (mSearchActive) {   // Schreibmarke
            const int cx = tx + (mQuery.empty() ? 0 : textWidth(ellipsize(mQuery, right - tx - 8, kTextPx).c_str(), kTextPx)) + 3;
            fillRect(cx, y0 + 14, cx + 3, y1 - 14, mColAccent);
        }
        if (!mQuery.empty()) {
            const int bx = x1 - 34;
            roundRectV(bx - 17, cy - 17, bx + 17, cy + 17, 17, lighten(clearHover ? mColAccent : kKnob, 30),
                       clearHover ? mColAccent : kKnob);
            const char* x = "\xC3\x97";   // ×
            text(x, bx - textWidth(x, kTextPx, true) / 2, cy - static_cast<int>(kTextPx) / 2 - 1, kTextPx,
                 clearHover ? mColDark : mColText, true);
        }
        break;
    }
    case kGame: {
        // Kachel in Systemfarbe mit Anfangsbuchstabe, Titel, System rechts; laufendes Spiel in Akzent.
        const std::string& entry = mLibrary[item.game];
        const size_t tab = entry.find('\t');
        const std::string system = tab == std::string::npos ? "" : entry.substr(0, tab);
        const std::string title = tab == std::string::npos ? entry : entry.substr(tab + 1);
        const bool running = item.game == mRunningGame;
        if (themed) {
            image3(mTheme.button, x0, top + 4, x1, top + kRowH - 4);
            if (hover) tint(x0, top + 4, x1, top + kRowH - 4, mTheme.hover);
        } else {
            hoverCard(x0, top + 4, x1, top + kRowH - 4, hover);
        }
        uint32_t badge = system == "DOS" ? kBadgeDos : kTileGreen;
        if (mSingleSystem) {
            // Ein System (xr.adventure, xr.dosgames): Kachelfarbe je Titel statt überall gleich.
            static const uint32_t kTileColors[] = { kTileViolet, kTileRed, kTileGreen, kTileBlue, kBadgeDos,
                                                    rgba(176, 120, 40, 255), rgba(40, 150, 150, 255) };
            uint32_t h = 2166136261u;
            for (unsigned char c : title) h = (h ^ c) * 16777619u;
            badge = kTileColors[h % (sizeof(kTileColors) / sizeof(kTileColors[0]))];
        }
        const int tile = 44, tx0 = x0 + 24, ty0 = top + (kRowH - tile) / 2;
        if (running) roundRect(tx0 - 4, ty0 - 4, tx0 + tile + 4, ty0 + tile + 4, 14, mColAccent);
        else roundRect(tx0, ty0 + 3, tx0 + tile, ty0 + tile + 3, 11, rgba(0, 0, 0, 120));   // Schatten
        roundRectV(tx0, ty0, tx0 + tile, ty0 + tile, 11, lighten(badge, 45), badge);
        std::string initial = "#";
        for (const char* p = title.c_str(); *p;) {   // erster Buchstabe/Ziffer, groß
            const char* start = p;
            const uint32_t cp = utf8Next(p);
            if (std::isalnum(static_cast<int>(cp)) && cp < 0x80) {
                initial = std::string(1, static_cast<char>(std::toupper(static_cast<int>(cp))));
                break;
            }
            if (cp >= 0xC0 && cp <= 0xFF) { initial = std::string(start, p); break; }
        }
        const float ipx = 30.0f;
        const int ix = tx0 + (tile - textWidth(initial.c_str(), ipx, true)) / 2, iy = ty0 + (tile - static_cast<int>(ipx)) / 2;
        text(initial.c_str(), ix + 1, iy + 2, ipx, rgba(0, 0, 0, 255), true);   // Schrift mit leichtem Schatten
        text(initial.c_str(), ix, iy, ipx, rgba(255, 255, 255, 255), true);
        // Rechts: System als Plakette in Systemfarbe, davor „läuft“ beim laufenden Spiel.
        const float spx = kSmallPx - 6.0f;
        const int sysW = textWidth(system.c_str(), spx, true) + 24;
        const int pillY0 = top + kRowH / 2 - 15, pillY1 = top + kRowH / 2 + 15;
        int right = x1 - 18;
        if (!system.empty() && !mSingleSystem) {   // Plakette nur, wenn sie etwas unterscheidet
            roundRect(right - sysW, pillY0, right, pillY1, 15, withAlpha(badge, 90));
            text(system.c_str(), right - sysW + 12, top + (kRowH - static_cast<int>(spx)) / 2, spx, lighten(badge, 75), true);
            right -= sysW + 8;
        }
        if (!running && item.game == mLastGame) {
            const char* last = "zuletzt";
            const int lw = textWidth(last, spx, true) + 24;
            roundRect(right - lw, pillY0, right, pillY1, 15, withAlpha(mColAccent, 70));
            text(last, right - lw + 12, top + (kRowH - static_cast<int>(spx)) / 2, spx, lighten(mColAccent, 40), true);
            right -= lw + 8;
        }
        if (running) {
            const char* run = "läuft";
            const int rw = textWidth(run, spx, true) + 24;
            roundRectV(right - rw, pillY0, right, pillY1, 15, lighten(mColAccent, 40), mColAccent);
            text(run, right - rw + 12, top + (kRowH - static_cast<int>(spx)) / 2, spx, mColDark, true);
            right -= rw + 8;
        }
        const int titleX = tx0 + tile + 18;
        const std::string shown = ellipsize(title, right - 6 - titleX, kTextPx);
        text(shown.c_str(), titleX, top + (kRowH - static_cast<int>(kTextPx)) / 2, kTextPx,
             running ? lighten(mColAccent, 30) : mColText);
        break;
    }
    case kLibraryEmpty:
    case kLibraryHint:
    case kNoMatch: {
        std::string msg = item.row == kLibraryEmpty ? mEmptyTitle
                        : item.row == kLibraryHint ? mEmptyHint
                                                   : "Keine Treffer für „" + mQuery + "“";
        const float px = fitPx(msg.c_str(), x1 - x0 - 24, item.row == kLibraryEmpty ? kTextPx : kSmallPx);
        msg = ellipsize(msg, x1 - x0 - 24, px);
        text(msg.c_str(), (x0 + x1 - textWidth(msg.c_str(), px)) / 2, top + (kRowH - static_cast<int>(px)) / 2,
             px, mColDim);
        break;
    }
    case kPage: {
        char info[48];
        const int count = static_cast<int>(mFiltered.size()), total = static_cast<int>(mLibrary.size());
        const int ty = top + (kRowH - static_cast<int>(kSmallPx)) / 2;
        if (pageCount() <= 1) {   // eine Seite: nur Anzahl
            if (mQuery.empty()) std::snprintf(info, sizeof(info), "%d Spiel%s", total, total == 1 ? "" : "e");
            else std::snprintf(info, sizeof(info), "%d von %d Spielen", count, total);
            if (total > 0) text(info, (x0 + x1 - textWidth(info, kSmallPx)) / 2, ty, kSmallPx, mColDim);
            break;
        }
        if (themed) {
            image3(mTheme.button, x0, top + 5, x1, top + kRowH - 5);
            if (hover) tint(x0, top + 5, x1, top + kRowH - 5, mTheme.hover);
        } else {
            hoverCard(x0, top + 5, x1, top + kRowH - 5, hover);
        }
        for (int z = 0; z < 2; ++z) {
            const int cx = z == 0 ? x0 + 44 : x1 - 44;
            knob(cx, top + kRowH / 2, z == 0, hover && mHoverZone == (z == 0 ? kZoneLess : kZoneMore));
        }
        std::snprintf(info, sizeof(info), "Seite %d / %d  ·  %d Spiel%s", mPage + 1, pageCount(), count,
                      count == 1 ? "" : "e");
        text(info, (x0 + x1 - textWidth(info, kSmallPx)) / 2, ty, kSmallPx, mColText);
        break;
    }
    default: break;   // Füllzeile
    }
}

// --- Teil-Redraw: Bereiche neu zeichnen, Streifen sammeln, einmal hochladen --------

void VrMenu::partRow(const ViewSettings& settings, int r) {
    const std::vector<Item> list = rows();
    if (r < 0 || r >= static_cast<int>(list.size())) return;
    const bool bar = list[r].row == kQuickBar;
    if (mSearchActive && !bar) return;   // verdeckt von der Tastatur
    const int top = rowTop(r);
    panelArea(bar || !mLibraryEnabled ? 16 : kLibW + 4, top, mTexW - 16, top + kRowH);
    drawRow(static_cast<size_t>(r), list[r], settings);
    mPartMin = std::min(mPartMin, top);
    mPartMax = std::max(mPartMax, top + kRowH);
}

void VrMenu::partLib(int i) {
    const std::vector<Item> lib = libRows();
    if (i < 0 || i >= static_cast<int>(lib.size())) return;
    const int top = i == 0 ? kTitleH + 2 : libTop(i), bottom = libTop(i) + (i == 0 ? kTabH : kRowH);
    panelArea(16, top, kLibW - 4, bottom);
    drawLib(static_cast<size_t>(i), lib[i]);
    mPartMin = std::min(mPartMin, top);
    mPartMax = std::max(mPartMax, bottom);
}

void VrMenu::partKey(int k) {
    if (k < 0 || k >= static_cast<int>(mKeys.size()) || !mSearchActive) return;
    const Key& key = mKeys[k];
    panelArea(key.x0 - 4, key.y0 - 4, key.x1 + 4, key.y1 + 4);
    drawKey(k);
    mPartMin = std::min(mPartMin, key.y0 - 4);
    mPartMax = std::max(mPartMax, key.y1 + 4);
}

void VrMenu::partTabs() {
    panelArea(mLibraryEnabled ? kLibW + 4 : 16, kTitleH + 2, mTexW - 16, kTitleH + kTabH);
    if (mSearchActive) drawSearchBand();
    else drawTabs();
    mPartMin = std::min(mPartMin, kTitleH + 2);
    mPartMax = std::max(mPartMax, kTitleH + kTabH);
}

void VrMenu::partUpload() {
    const int y0 = std::max(mPartMin, 0), y1 = std::min(mPartMax, mTexH);
    mPartMin = mTexH;
    mPartMax = 0;
    if (y1 <= y0) return;
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, mTexW, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE,
                    mPixels.data() + static_cast<size_t>(y0) * mTexW);
    glGenerateMipmap(GL_TEXTURE_2D);
}

// Nur Hover geändert: alte und neue Zeile (bzw. Reiterleiste) neu zeichnen und
// nur diesen Streifen hochladen — ein Neuzeichnen der ganzen Tafel ließ das Bild
// bei jedem Zeilenwechsel des Lasers stocken.
void VrMenu::redrawHover(const ViewSettings& settings, int oldRow, int newRow, bool tabsChanged) {
    mPartMin = mTexH;
    mPartMax = 0;
    partRow(settings, oldRow);
    if (newRow != oldRow) partRow(settings, newRow);
    if (tabsChanged) partTabs();
    partUpload();
}

void VrMenu::redrawFocus(const ViewSettings& settings, const Focus& before, const Focus& after) {
    mPartMin = mTexH;
    mPartMax = 0;
    for (const Focus* f : { &before, &after }) {
        if (f->row >= 0) partRow(settings, f->row);
        if (f->lib >= 0) partLib(f->lib);
        if (f->key >= 0) partKey(f->key);
    }
    if (before.tab >= 0 || after.tab >= 0) partTabs();
    partUpload();
}

// Rechte Spalte komplett (Reiterleiste bzw. Suchanzeige, Zeilen bzw. Tastatur) und das Suchfeld
// links — ohne Tafelhintergrund, Kopf, Liste und Schnellleiste. Ein Vollbild kostete beim
// Reiterwechsel ein Mehrfaches und ließ das Menü kurz stocken.
void VrMenu::redrawRight(const ViewSettings& settings) {
    mRightDirty = false;
    mPartMin = mTexH;
    mPartMax = 0;
    const int bar = barTop();
    panelArea(mLibraryEnabled ? kLibW + 4 : 16, kTitleH + 2, mTexW - 16, bar);
    if (mSearchActive) drawSearchBand();
    else drawTabs();
    const std::vector<Item> list = rows();
    if (mSearchActive) {
        for (int k = 0; k < static_cast<int>(mKeys.size()); ++k) drawKey(k);
    } else {
        for (size_t i = 0; i + 1 < list.size(); ++i) drawRow(i, list[i], settings);
    }
    mPartMin = kTitleH + 2;
    mPartMax = bar;
    if (mLibraryEnabled) partLib(0);   // Suchfeld zeigt den Suchmodus
    partUpload();
}

void VrMenu::redrawPadRows(const ViewSettings& settings) {
    mPadRowsDirty = false;
    if (mTab != kTabPad) return;
    mPartMin = mTexH;
    mPartMax = 0;
    const std::vector<Item> list = rows();
    for (int r = 0; r < static_cast<int>(list.size()); ++r)
        if (list[r].row >= kPadHint) partRow(settings, r);
    partUpload();
}

void VrMenu::redrawLibrary() {
    mLibDirty = false;
    mPartMin = mTexH;
    mPartMax = 0;
    const int n = static_cast<int>(libRows().size());
    for (int i = 0; i < n; ++i) partLib(i);
    if (mSearchActive) partTabs();   // Suchanzeige mit Trefferzahl
    partUpload();
}

// Titel links (+ Untertitel), Statusmeldung rechts (kurzzeitig) — auf den Kopfzeilen-Hintergrund.
void VrMenu::drawTitleText(double now) {
    mStatusShown = now < mStatusUntil;
    const int titleY = (kTitleH - static_cast<int>(kTitlePx)) / 2 + 2;
    int titleX = 40;
    if (!mTheme.active) {
        // Logo: Kachel in Akzentfarbe mit „xr“ (Standardoptik; Spieloptik bringt ihren eigenen Kopf mit).
        const int s = 46, ly = (kTitleH - s) / 2 + 1;
        roundRect(36, ly + 3, 36 + s, ly + s + 3, 13, rgba(0, 0, 0, 110));
        roundRectV(36, ly, 36 + s, ly + s, 13, lighten(mColAccent, 60), mColAccent);
        const float lpx = 30.0f;
        text("xr", 36 + (s - textWidth("xr", lpx, true)) / 2, ly + (s - static_cast<int>(lpx)) / 2 - 2, lpx,
             mColDark, true);
        titleX = 36 + s + 16;
    }
    text(mTitle.c_str(), titleX, titleY, kTitlePx, mTitleColor, true);
    if (!mSubtitle.empty()) {
        const int sx = titleX + textWidth(mTitle.c_str(), kTitlePx, true) + 18;
        text(mSubtitle.c_str(), sx, titleY + static_cast<int>(kTitlePx - kSmallPx) - 4, kSmallPx, mColDim);
    }
    mInfoDirty = false;
    if (!mStatusShown && !mInfo.empty()) {
        // Info (z. B. Spielauflösung) rechts, gedimmt — die Statusmeldung hat Vorrang.
        const int w = textWidth(mInfo.c_str(), kSmallPx);
        text(mInfo.c_str(), mTexW - 52 - w, titleY + 6, kSmallPx, mColDim);
    }
    if (mStatusShown) {
        const int w = textWidth(mStatus.c_str(), kSmallPx);
        roundRect(mTexW - 40 - w - 32, titleY - 2, mTexW - 36, titleY + static_cast<int>(kSmallPx) + 14,
                  18, kStatusBg);
        text(mStatus.c_str(), mTexW - 52 - w, titleY + 6, kSmallPx, mColText);
    }
}

void VrMenu::redrawTitle(double now) {
    const size_t n = static_cast<size_t>(mTexW) * kTitleH;
    if (mTitleBg.size() != n || mPixels.size() < n) { mDirty = true; return; }
    std::copy(mTitleBg.begin(), mTitleBg.end(), mPixels.begin());
    drawTitleText(now);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mTexW, kTitleH, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
}

void VrMenu::redraw(const ViewSettings& settings, double now) {
    mPixels.assign(static_cast<size_t>(mTexW) * mTexH, 0u);   // Ecken bleiben durchsichtig

    uint32_t titleColor = mColAccent;
    if (mTheme.active) {
        // Spielgrafik: Tafelbild, Kopfzeile, Zierleiste, dunkler Rahmen wie ein Spielfenster.
        panelArea(0, 0, mTexW, mTexH);
        if (mTheme.title.ok()) image(mTheme.title, 0, 0, mTexW, kTitleH);
        if (mTheme.strip.ok()) image(mTheme.strip, 0, kTitleH - 12, mTexW, kTitleH);
        const uint32_t frame = rgba(24, 24, 24, 255);
        fillRect(0, 0, mTexW, 3, frame);
        fillRect(0, mTexH - 3, mTexW, mTexH, frame);
        fillRect(0, 0, 3, mTexH, frame);
        fillRect(mTexW - 3, 0, mTexW, mTexH, frame);
        titleColor = mTheme.titleText;
    } else {
        // Tafel: dunkles Glas mit Verlauf, feinem Akzentrand, abgesetzter Kopfzeile und
        // Akzent-Haarlinie darunter.
        roundRect(0, 0, mTexW, mTexH, 34, kAccentSoft);
        roundRectV(3, 3, mTexW - 3, mTexH - 3, 31, kPanelTop, kPanelBottom);
        roundRectV(3, 3, mTexW - 3, kTitleH + 31, 31, kHeaderTop, kHeaderBottom);
        panelArea(3, kTitleH, mTexW - 3, kTitleH + 32);   // untere Kante der Kopfzeile gerade
        roundRect(32, kTitleH - 2, mTexW - 32, kTitleH, 1, rgba(255, 146, 38, 120));
        roundRect(36, 4, mTexW - 36, 5, 0, rgba(255, 255, 255, 30));   // Glanzkante oben
    }
    if (mLibraryEnabled)   // feine Trennlinie zwischen Spieleliste und Reitern
        roundRect(kLibW - 1, kTitleH + 16, kLibW + 1, barTop() - 10, 1, rgba(255, 255, 255, 26));

    // Kopfzeile: Hintergrund ohne Text merken (Statusmeldung zeichnet nur diesen Streifen neu).
    mTitleBg.assign(mPixels.begin(), mPixels.begin() + static_cast<size_t>(mTexW) * kTitleH);
    mTitleColor = titleColor;
    mQuitShown = now < mQuitArmedUntil;
    mConfirmShown = now < mConfirmUntil;
    drawTitleText(now);

    if (mSearchActive) drawSearchBand();
    else drawTabs();

    const std::vector<Item> list = rows();
    for (size_t i = 0; i < list.size(); ++i)
        if (!mSearchActive || list[i].row == kQuickBar) drawRow(i, list[i], settings);
    const std::vector<Item> lib = libRows();
    for (size_t i = 0; i < lib.size(); ++i) drawLib(i, lib[i]);
    if (mSearchActive)
        for (int k = 0; k < static_cast<int>(mKeys.size()); ++k) drawKey(k);

    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mTexW, mTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 mPixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    mShown = settings;
    mDirty = false;
    mLibDirty = false;
    mPadRowsDirty = false;
    mRightDirty = false;
}

// Reiter „Tasten“: Kurzanleitung, Paar aus Konsolentasten (Name links, Quest-Tasten rechts;
// wartet die Taste aufs Zuweisen, ist sie gefüllt), Knöpfe „Für alle …“ · „Zurücksetzen“.
void VrMenu::drawPadRow(size_t i, const Item& item) {
    const int top = rowTop(static_cast<int>(i));
    const bool hover = static_cast<int>(i) == mHoverRow;
    const int cx0 = contentX0();
    const padmap::System& sys = padmap::systemFor(mPadFolder);
    if (item.row == kPadHint) {
        const char* hint = "Konsolentaste antippen, dann die Quest-Taste drücken";
        const float px = fitPx(hint, mTexW - cx0 - 60, kSmallPx);
        text(hint, (cx0 + mTexW - textWidth(hint, px)) / 2, top + (kRowH - static_cast<int>(px)) / 2, px, mColDim);
        return;
    }
    int x0 = 0, x1 = 0;
    zoneSpan(item, x0, x1);
    const int n = zoneCount(item);
    const int span = (x1 - x0) / (item.row == kPadPair ? 2 : n);
    const int ty = top + (kRowH - static_cast<int>(kTextPx)) / 2;
    for (int b = 0; b < n; ++b) {
        const int bx0 = x0 + b * span + 6, bx1 = x0 + (b + 1) * span - 6;
        const bool zoneHover = hover && mHoverZone == static_cast<Zone>(kZoneBar0 + b);
        if (item.row == kPadActions) {
            std::string label = sys.label;   // „Wolf 3D“ → „Wolf-3D-Spiele“
            std::replace(label.begin(), label.end(), ' ', '-');
            std::string caption = b == 0 ? "Für alle " + label + "-Spiele" : "Zurücksetzen";
            const uint32_t tone = b == 0 ? mColAccent : lighten(kKnob, 70);
            softButton(bx0, top + 7, bx1, top + kRowH - 7, 24, tone, zoneHover);
            const float px = fitPx(caption.c_str(), bx1 - bx0 - 32, kTextPx, true);
            text(caption.c_str(), (bx0 + bx1 - textWidth(caption.c_str(), px, true)) / 2,
                 top + (kRowH - static_cast<int>(px)) / 2, px, zoneHover ? mColDark : lighten(tone, 30), true);
            continue;
        }
        const padmap::ConsoleButton& button = sys.buttons[item.game + b];
        const bool learning = mLearnTarget == button.id;
        if (learning) {
            roundRect(bx0 - 3, top + 2, bx1 + 3, top + kRowH - 2, 19, withAlpha(mColAccent, 90));
            roundRectV(bx0, top + 5, bx1, top + kRowH - 5, 16, lighten(mColAccent, 50), mColAccent);
        } else {
            hoverCard(bx0, top + 5, bx1, top + kRowH - 5, zoneHover);
        }
        const int nameX = bx0 + 30;
        text(button.name, nameX, ty, kTextPx, learning ? mColDark : mColText, true);
        std::string bound;
        if (learning) {
            bound = "drücken …";
        } else {
            for (padmap::Source s : padmap::sourcesFor(mPadMap, button.id)) {
                if (!bound.empty()) bound += " · ";
                bound += padmap::sourceName(s);
            }
            if (bound.empty()) bound = "—";
        }
        const int right = bx1 - 22, left = nameX + textWidth(button.name, kTextPx, true) + 18;
        const std::string shown = ellipsize(bound, right - left, kSmallPx, true);
        const uint32_t ink = learning ? mColDark : bound == "—" ? mColDim : lighten(mColAccent, 40);
        text(shown.c_str(), right - textWidth(shown.c_str(), kSmallPx, true),
             top + (kRowH - static_cast<int>(kSmallPx)) / 2, kSmallPx, ink, true);
    }
}

void VrMenu::draw(const float* localView, const float* eyeProj) {
    if (!mVisible || mTexH == 0) return;
    glUseProgram(mProgram);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glUniform1i(mLocTex, 0);

    const float model[16] = { mW, 0, 0, 0,  0, mH, 0, 0,  0, 0, 1, 0,  0, 0, mCz, 1 };
    float mv[16], mvp[16];
    mat4Multiply(mv, localView, model);
    mat4Multiply(mvp, eyeProj, mv);
    glUniformMatrix4fv(mLocMvp, 1, GL_FALSE, mvp);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);   // Tafel immer über Bildschirm und Laser
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}
