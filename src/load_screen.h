// XRShell — Ladeanzeige an der Stelle des Spielfensters (Skill xr-loading-screen L2–L8).
//
// Tafel 1280×800 Texel, auf der CPU gerastert (System-TTF über TextRaster, Formen geglättet wie im
// VR-Menü) und als Quad lokal am Bildschirm gezeichnet: gleiche Mitte, in die Bildfläche eingepasst
// (16:10). Inhalt kommt als LoadView (reine Daten, aus load_status.h aufbereitet in main.cpp).
//
// Bewegung ohne CPU-Arbeit: der wandernde Lichtpunkt im Fortschrittsbalken entsteht im Shader
// (Uniform uPulse), die Tafel selbst wird nur bei Inhaltswechsel neu gerastert — Laufzeit/Restzeit/
// Balken (obere Zone) höchstens 4×/s und nur dieser Streifen per glTexSubImage2D.
//
// GLES nur im Render-Thread (init/update/draw auf dem Main-Thread). Host-Vorschau:
// tests/host/load_preview.cpp (GL-Stubs, PNGs nach tests/out/).

#pragma once

#include "load_status.h"
#include "text_raster.h"

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

struct LoadView {
    enum Mode { kIdle, kLoading, kError };
    Mode mode = kIdle;
    std::string app, version, game;      // „xr.dosgames“, „0.3.7“, „Doom II“
    std::string headline;                // „Spiel laden …“ / „Spiel im Menü auswählen“
    std::string elapsed;                 // „seit 14 s“ (leer = keine Anzeige)
    float progress = -1.0f;              // 0..1, < 0 = kein Balken
    std::string eta;                     // Zeile unter dem Balken
    bool etaWarn = false;                // „dauert länger als sonst“ gelb
    std::string notice;                  // gelber Hinweis (Watchdog, erster Start, Absturz)
    struct StepLine {
        loadst::StepState state = loadst::StepState::Pending;
        std::string name, time;          // „Core laden“, „0,4 s“
        bool operator==(const StepLine& o) const { return state == o.state && name == o.name && time == o.time; }
    };
    std::vector<StepLine> steps;
    std::vector<std::string> info;       // Leerlauf: linke Spalte „So geht's“
    std::vector<loadst::CheckLine> checks;
    // Fehlerseite
    std::string errorTitle, errorDetail;
    std::vector<std::string> remedy;
    std::string logPath, logHint;
    std::string help1, help2;            // Kurzbedienung (2 Zeilen)

    // Inhalt der oberen Zone (Kopf, Überschrift, Balken, Restzeit, Hinweis) — weiche Änderung.
    bool sameTop(const LoadView& o) const {
        return mode == o.mode && app == o.app && version == o.version && game == o.game && headline == o.headline &&
               elapsed == o.elapsed && progressKey() == o.progressKey() && eta == o.eta && etaWarn == o.etaWarn &&
               notice == o.notice && errorTitle == o.errorTitle && errorDetail == o.errorDetail;
    }
    // Rest (Spalten, Fehlerseite, Fuß) — harte Änderung.
    bool sameBody(const LoadView& o) const {
        return mode == o.mode && steps == o.steps && info == o.info && checks == o.checks &&
               remedy == o.remedy &&
               logPath == o.logPath && logHint == o.logHint && help1 == o.help1 && help2 == o.help2 &&
               app == o.app && version == o.version;
    }
    int progressKey() const { return progress < 0.0f ? -1 : static_cast<int>(progress * 400.0f); }

    // Schrittzeilen, Balken und Restzeit aus einem Ladevorgang (main.cpp und Host-Vorschau).
    void fillFrom(loadst::Session& load, double now) {
        steps.clear();
        for (int i = 0; i < loadst::kStepCount; ++i) {
            const loadst::StepState s = load.state(i);
            if (s == loadst::StepState::Skipped) continue;
            StepLine line;
            line.state = s;
            line.name = loadst::stepName(i);
            if (s == loadst::StepState::Done || s == loadst::StepState::Failed)
                line.time = loadst::seconds(load.durationS(i, now));
            else if (s == loadst::StepState::Active && load.durationS(i, now) >= 2.0)   // ganze s: Tafel 1×/s neu
                line.time = std::to_string(static_cast<int>(load.durationS(i, now))) + " s";
            steps.push_back(line);
        }
        progress = static_cast<float>(load.progress(now));
        eta = load.etaText(now);
        int step = -1;
        double usual = 0.0;
        etaWarn = load.overdue(now, step, usual);
        const int active = load.activeStep();
        headline = active >= 0 ? std::string(loadst::stepName(active)) + " …" : std::string("Fertig");
        elapsed = "seit " + std::to_string(static_cast<int>(load.elapsedS(now))) + " s";
    }
};

class LoadScreen {
public:
    static constexpr int kTexW = 1280, kTexH = 800;
    static constexpr float kAspect = static_cast<float>(kTexW) / kTexH;

    bool init();
    void shutdown();

    // Neuer Inhalt; rastert nur, was sich geändert hat (obere Zone höchstens alle 0,25 s).
    void update(const LoadView& view, double now);
    // Lokal am Bildschirm (localView = Blick · Bildschirm-Modell), in die Bildfläche
    // screenW × screenH eingepasst. timeS treibt den Lichtpunkt im Balken.
    void draw(const float* localView, const float* eyeProj, float screenW, float screenH, double timeS);

    // Lichtpunkt: Lage im Balken (0..1) zur Zeit t — auch für die Host-Vorschau.
    static float pulseAt(double timeS);
    // Balken in Texeln (Host-Vorschau mischt den Lichtpunkt wie der Shader).
    static constexpr int kBarX0 = 56, kBarX1 = 1224, kBarY0 = 222, kBarY1 = 250;

private:
    void redrawAll();
    void redrawTop();
    void upload(int y0, int y1);
    void drawBackground();
    void drawTop();
    void drawBody();
    void drawSteps(int x0, int x1, int y);
    void drawChecks(int x0, int x1, int y, int maxRows, bool onlyProblems);
    void drawInfo(int x0, int x1, int y);
    void drawError(int y);
    void drawFooter();
    void icon(int cx, int cy, loadst::StepState state);
    void checkIcon(int cx, int cy, loadst::Level level);

    // Raster (wie VR-Menü: RGBA8, Byte-Reihenfolge R,G,B,A)
    void text(const std::string& s, int x, int y, float px, uint32_t color, bool bold = false);
    int textWidth(const std::string& s, float px, bool bold = false) const;
    std::string ellipsize(const std::string& s, int maxW, float px, bool bold = false) const;
    std::vector<std::string> wrap(const std::string& s, int maxW, float px, size_t maxLines) const;
    void fillRect(int x0, int y0, int x1, int y1, uint32_t color);
    void roundRect(int x0, int y0, int x1, int y1, int r, uint32_t color);
    void roundRectV(int x0, int y0, int x1, int y1, int r, uint32_t top, uint32_t bottom);
    void circle(int cx, int cy, int r, uint32_t color);
    void ring(int cx, int cy, int r, int width, uint32_t color);
    void line(float x0, float y0, float x1, float y1, float width, uint32_t color);
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, uint32_t color);
    void blend(int x, int y, uint32_t color, uint32_t cov);

    LoadView mView;
    bool mHasView = false;
    double mLastTop = -1.0;
    TextRaster mFont, mFontBold;
    std::vector<uint32_t> mPixels, mBase;   // Tafel, Hintergrund ohne Inhalt (für Teil-Redraw)
    GLuint mProgram = 0, mVbo = 0, mTexture = 0;
    GLint mLocMvp = -1, mLocTex = -1, mLocBar = -1, mLocPulse = -1;
    bool mTextureReady = false;
};
