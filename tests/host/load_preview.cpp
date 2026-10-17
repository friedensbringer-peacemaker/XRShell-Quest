// Host-Vorschau der Ladeanzeige (ohne Quest, ohne GL-Kontext): rastert die Tafel auf der CPU
// (GL-Aufrufe sind Stubs aus tests/host-stubs/) mit den echten Texten aus load_status.h und
// schreibt PNGs nach tests/out/xrshell-load-*.png:
//   laufend (mit Restzeit), erster-start, dauert-laenger (Watchdog-Hinweis), fehler (Fehlerseite),
//   systemcheck-warnung (Leerlauf der Bibliothek mit ⚠-Zeilen).
// Der Lichtpunkt im Balken entsteht auf der Quest im Shader; hier wird er gleich eingemischt.
// Schrift: Systemschrift des Rechners (Windows Segoe UI) statt Roboto — Breiten weichen leicht ab.
// Gebaut von tests/run-host-tests.sh (braucht einmal einen Gradle-Build für OpenXR-/stb-Header).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define private public   // Tafel-Puffer für die Vorschau erreichbar
#include "load_screen.h"
#undef private

#include "stb_image_write.h"

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

// Tafel auf dunklen Grund gemischt, Lichtpunkt wie im Shader (pulse < 0 = aus).
bool writePng(const std::string& path, const LoadScreen& s, float pulse) {
    const int w = LoadScreen::kTexW, h = LoadScreen::kTexH;
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const float bar0 = static_cast<float>(LoadScreen::kBarX0), bar1 = static_cast<float>(LoadScreen::kBarX1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            const uint32_t c = s.mPixels[i], a = c >> 24;
            const uint32_t bg[3] = {40, 44, 52};
            float out[3];
            for (int k = 0; k < 3; ++k) out[k] = static_cast<float>((c >> (8 * k)) & 0xFF);
            if (pulse >= 0.0f && x >= LoadScreen::kBarX0 && x <= LoadScreen::kBarX1 && y >= LoadScreen::kBarY0 &&
                y <= LoadScreen::kBarY1) {
                const float px = bar0 + (bar1 - bar0) * pulse;
                const float d = (static_cast<float>(x) - px) / (bar1 - bar0) * 16.0f;
                const float g = std::exp(-d * d) * 0.85f;
                const float glow[3] = {255.0f, 237.0f, 204.0f};
                for (int k = 0; k < 3; ++k) out[k] = out[k] + (glow[k] - out[k]) * g;
            }
            for (int k = 0; k < 3; ++k)
                rgb[i * 3 + k] = static_cast<uint8_t>((out[k] * static_cast<float>(a) + bg[k] * (255.0f - a)) / 255.0f);
        }
    return stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
}

std::vector<loadst::CheckLine> okChecks() {
    return {loadst::checkOpenXr(true, "Oculus 83.0"), loadst::checkController("/interaction_profiles/oculus/touch_controller", 5.0),
            loadst::checkGameData(true, true, "Doom2"), loadst::checkCore(true, "dosbox_pure"),
            loadst::checkStorage(48 * 1024), loadst::checkBattery(76, false),
            loadst::checkLog("/storage/emulated/0/Android/data/xr.dosgames/files/logs/session-20261008-101500.log"),
            loadst::checkPreviousRun(false, "")};
}

LoadView baseView(const std::string& app, const std::string& version) {
    LoadView v;
    v.app = app;
    v.version = version;
    v.help1 = "Menütaste links: VR-Menü (Spiele, Beenden) · Griff links: Fenster greifen";
    v.help2 = "Trigger: Klick · B: Rechtsklick · Y: Bildschirmtastatur";
    return v;
}

std::array<double, loadst::kStepCount> expectedFor(const loadst::History& h, const std::string& core, bool first) {
    std::array<double, loadst::kStepCount> e{};
    for (int i = 0; i < loadst::kStepCount; ++i) e[i] = h.expectedMs(core, i, first);
    return e;
}

void render(const std::string& name, const LoadView& view, float pulse, LoadScreen& screen) {
    screen.mHasView = false;
    screen.update(view, 0.0);
    const std::string path = "tests/out/xrshell-load-" + name + ".png";
    check(writePng(path, screen, pulse), "Vorschau schreiben: " + path);
}

void renderPreviews() {
    LoadScreen screen;
    check(screen.init(), "init");
    std::array<bool, loadst::kStepCount> used{};
    used.fill(true);
    used[loadst::kStopPrevious] = false;

    // 1) laufend, mit Verlauf: Spiel laden aktiv, Restzeit.
    {
        loadst::History h;
        for (int i = 0; i < loadst::kStepCount; ++i) h.record("dosbox_pure", i, i == loadst::kFirstFrame ? 6000 : 900, false);
        loadst::Session s;
        s.begin(100.0, expectedFor(h, "dosbox_pure", false), used, true, false);
        s.startStep(loadst::kSystemCheck, 100.0);
        s.startStep(loadst::kGameData, 100.03);
        s.startStep(loadst::kCore, 100.1);
        s.startStep(loadst::kLoadGame, 100.9);
        LoadView v = baseView("xr.dosgames", "0.3.7");
        v.mode = LoadView::kLoading;
        v.game = "Doom II";
        v.fillFrom(s, 101.3);
        v.checks = okChecks();
        render("laufend", v, LoadScreen::pulseAt(0.5), screen);
        check(v.eta.find("noch etwa") == 0, "laufend: Restzeit „noch etwa …“ (" + v.eta + ")");
    }
    // 2) erster Start ohne Verlauf (xr.shooter, Core wird entpackt).
    {
        loadst::History h;
        loadst::Session s;
        s.begin(0.0, expectedFor(h, "tyrquake", true), used, false, true);
        s.startStep(loadst::kSystemCheck, 0.0);
        s.startStep(loadst::kGameData, 0.03);
        s.startStep(loadst::kCore, 0.06);
        LoadView v = baseView("xr.shooter", "0.3.6");
        v.help2 = "Controller = Gamepad · Trigger rechts: Feuer";
        v.mode = LoadView::kLoading;
        v.game = "Quake";
        v.fillFrom(s, 0.3);
        v.notice = "Erster Start nach Installation/Update – Core wird entpackt";
        v.checks = okChecks();
        v.checks[2] = loadst::checkGameData(true, true, "Quake");
        v.checks[3] = loadst::checkCore(true, "tyrquake");
        render("erster-start", v, LoadScreen::pulseAt(0.9), screen);
        check(v.eta.find("Erster Start") == 0, "erster Start: Text (" + v.eta + ")");
    }
    // 3) dauert länger: Spiel laden 14 s statt 0,1 s, Watchdog-Hinweis.
    {
        loadst::History h;
        h.record("dosbox_pure", loadst::kLoadGame, 100, false);
        loadst::Session s;
        s.begin(0.0, expectedFor(h, "dosbox_pure", false), used, true, false);
        s.startStep(loadst::kSystemCheck, 0.0);
        s.startStep(loadst::kGameData, 0.02);
        s.startStep(loadst::kCore, 0.05);
        s.startStep(loadst::kLoadGame, 0.15);
        LoadView v = baseView("xr.dosgames", "0.3.7");
        v.mode = LoadView::kLoading;
        v.game = "Theme Park";
        v.fillFrom(s, 14.2);
        v.notice = "Core antwortet seit 14 s nicht – wird beobachtet";
        v.checks = okChecks();
        render("dauert-laenger", v, LoadScreen::pulseAt(1.1), screen);
        check(v.etaWarn && v.eta.find("länger als sonst") != std::string::npos, "dauert länger: Warnung (" + v.eta + ")");
    }
    // 4) Fehlerseite: retro_load_game fehlgeschlagen (xr.shooter).
    {
        const loadst::ErrorText e = loadst::errorText(loadst::Failure::LoadFailed, "TyrQuake: Couldn't load gfx.wad",
                                                      "xr.shooter", "/storage/emulated/0/Android/data/xr.shooter/files/games/quake/Quake",
                                                      "tyrquake", 0.0);
        LoadView v = baseView("xr.shooter", "0.3.6");
        v.mode = LoadView::kError;
        v.game = "Quake";
        v.headline = "Start fehlgeschlagen";
        v.elapsed = "nach 0,4 s";
        v.errorTitle = e.title;
        v.errorDetail = "Letzte Core-Zeile: " + e.detail;
        v.remedy = e.remedy;
        v.logPath = "Android/data/xr.shooter/files/logs/session-20261008-101500.log";
        v.logHint = "Am PC holen: tools/pull-logs.sh xr.shooter";
        v.help1 = "B: Spielbild trotzdem zeigen · Menütaste links: VR-Menü (anderes Spiel, Beenden)";
        v.help2 = "";
        v.checks = okChecks();
        v.checks[5] = loadst::checkBattery(11, false);
        render("fehler", v, -1.0f, screen);
        check(!e.remedy.empty() && e.remedy[1].find("SYSTEM=quake") != std::string::npos, "Fehler: push-game.sh mit SYSTEM");
    }
    // 5) Leerlauf der Bibliothek mit ⚠-Zeilen im Systemcheck.
    {
        LoadView v = baseView("xr.dosgames", "0.3.7");
        v.mode = LoadView::kIdle;
        v.headline = "Spiel im Menü auswählen";
        v.eta = "Die Spieleliste ist offen – Spiel wählen oder „Weiterspielen“";
        v.info = {"Spiel in der Liste wählen: Strahl + Trigger oder Stick + A",
                  "„Weiterspielen“ setzt das zuletzt gespielte Spiel fort",
                  "Menütaste links öffnet und schließt die Liste"};
        v.checks = okChecks();
        v.checks[1] = loadst::checkController("", 4.0);
        v.checks[2] = loadst::checkLibrary(6, "");
        v.checks[3] = loadst::checkCore(true, "dosbox_pure");
        v.checks[5] = loadst::checkBattery(12, false);
        v.checks[7] = loadst::checkPreviousRun(true, "/x/logs/session-20261008-094012.log");
        v.notice = "Letzter Start ist abgestürzt – Log liegt unter files/logs/";
        render("systemcheck-warnung", v, -1.0f, screen);
    }
}

// Lichtpunkt bewegt sich in jedem Viertel einer Sekunde sichtbar (L5: ≥ 2× pro Sekunde).
void testPulse() {
    for (double t = 0.0; t < 10.0; t += 0.25) {
        const float a = LoadScreen::pulseAt(t), b = LoadScreen::pulseAt(t + 0.25);
        check(a >= 0.0f && a < 1.0f, "Lichtpunkt im Balken");
        check(std::fabs(a - b) > 0.05f, "Lichtpunkt bewegt sich");
    }
}

// Teil-Redraw: nur Laufzeit geändert → obere Zone neu, Rest bleibt bit-gleich.
void testPartialRedraw() {
    LoadScreen screen;
    screen.init();
    LoadView v = baseView("xr.shooter", "0.2.3");
    v.mode = LoadView::kLoading;
    v.progress = 0.3f;
    v.elapsed = "seit 1 s";
    v.checks = okChecks();
    screen.update(v, 0.0);
    const std::vector<uint32_t> before = screen.mPixels;
    v.elapsed = "seit 2 s";
    screen.update(v, 0.1);   // < 0,25 s: noch nichts
    check(screen.mPixels == before, "Teil-Redraw gedrosselt");
    screen.update(v, 0.5);
    bool bodySame = true, topChanged = false;
    for (int y = 0; y < LoadScreen::kTexH; ++y)
        for (int x = 0; x < LoadScreen::kTexW; ++x) {
            const size_t i = static_cast<size_t>(y) * LoadScreen::kTexW + x;
            if (y >= 348 && screen.mPixels[i] != before[i]) bodySame = false;
            if (y < 348 && screen.mPixels[i] != before[i]) topChanged = true;
        }
    check(bodySame, "Teil-Redraw: Spalten unverändert");
    check(topChanged, "Teil-Redraw: Laufzeit neu");
}

} // namespace

int main() {
    testPulse();
    testPartialRedraw();
    renderPreviews();
    std::printf("load_preview: %d Prüfungen, %d Fehler (Bilder: tests/out/xrshell-load-*.png)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
