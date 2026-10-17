// Host-Test: Ladeanzeige-Logik (src/load_status.h) — gleitendes Mittel, xr-load-times.json,
// Fortschritt/Restzeit als Parameter-Sweep, Startsignal (erstes echtes Spielbild, DOS-Grafikmodus),
// Watchdog, Systemcheck-Zeilen, Fehlertexte. Abhängigkeitsfrei:
//   c++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_load_status.cpp -o /tmp/t && /tmp/t

#include "load_status.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 40) std::printf("FEHLER: %s\n", what.c_str());
}
bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

void testFormat() {
    check(loadst::seconds(0.04) == "0,0 s", "seconds 0,0");
    check(loadst::seconds(0.44) == "0,4 s", "seconds 0,4");
    check(loadst::seconds(9.94) == "9,9 s", "seconds 9,9");
    check(loadst::seconds(14.2) == "14 s", "seconds 14");
    check(loadst::seconds(-3.0) == "0,0 s", "seconds negativ");
    check(loadst::coreKey("dosbox_pure_libretro_android.so") == "dosbox_pure", "coreKey dosbox");
    check(loadst::coreKey("/data/x/cores/prboom_libretro_android.so") == "prboom", "coreKey Pfad");
    check(loadst::coreKey("foo.so") == "foo", "coreKey .so");
    check(loadst::coreKey("") == "unbekannt", "coreKey leer");
    for (int s = 0; s < loadst::kStepCount; ++s) {
        check(std::string(loadst::stepName(s)) != "?", "stepName");
        check(std::string(loadst::stepKey(s)) != "?", "stepKey");
    }
}

// α = 0,3; Ausreißer (> 3 × Mittel) halb gewichtet; Konvergenz gegen einen festen Wert.
void testAverage() {
    loadst::Average a;
    loadst::addSample(a, 100.0);
    check(near(a.avgMs, 100.0) && a.n == 1, "erster Wert übernommen");
    loadst::addSample(a, 200.0);
    check(near(a.avgMs, 130.0), "α = 0,3");
    loadst::addSample(a, 1000.0);   // > 3 × 130 → α/2
    check(near(a.avgMs, 130.0 + 0.15 * 870.0), "Ausreißer halb gewichtet");
    loadst::addSample(a, -5.0);
    check(a.n == 3, "negative Werte ignoriert");
    // Sweep: Start 0..5000 ms, Ziel 10..20000 ms — nach 40 Messungen auf 1 % am Ziel.
    for (double start : {0.0, 50.0, 800.0, 5000.0})
        for (double target : {10.0, 120.0, 1500.0, 20000.0}) {
            loadst::Average s;
            loadst::addSample(s, start);
            for (int i = 0; i < 40; ++i) loadst::addSample(s, target);
            check(std::fabs(s.avgMs - target) <= 0.01 * target + 0.5, "Konvergenz " + std::to_string(start) + "→" +
                                                                           std::to_string(target));
        }
}

void testHistory() {
    loadst::History h;
    check(!h.hasHistory("prboom", false), "leer: kein Verlauf");
    check(near(h.expectedMs("prboom", loadst::kLoadGame, false), 55.0), "Standard prboom");
    check(near(h.expectedMs("tyrquake", loadst::kLoadGame, false), 120.0), "Standard tyrquake");
    check(near(h.expectedMs("dosbox_pure", loadst::kFirstFrame, false), 1500.0), "Standard DOS erstes Bild");
    check(h.expectedMs("dosbox_pure", loadst::kCore, true) > h.expectedMs("dosbox_pure", loadst::kCore, false),
          "erster Start: Core entpacken dauert länger");
    h.record("prboom", loadst::kLoadGame, 70.0, false);
    check(h.hasHistory("prboom", false) && !h.hasHistory("prboom", true), "Topf getrennt");
    check(near(h.expectedMs("prboom", loadst::kLoadGame, false), 70.0), "gemessener Wert");
    check(near(h.expectedMs("prboom", loadst::kLoadGame, true), 70.0), "erster Start: Normalwert außer Core");
    check(near(h.expectedMs("prboom", loadst::kCore, true), 300.0), "erster Start: Core-Standard");
    check(near(h.expectedMs("ecwolf", loadst::kLoadGame, false), 90.0), "anderer Core: Standard");

    // Rundreise: Cores × Schritte × Werte
    loadst::History src;
    src.version = 10;
    const char* cores[] = {"dosbox_pure", "prboom", "tyrquake", "ecwolf", "scummvm", "other"};
    int k = 0;
    for (const char* c : cores)
        for (int s = 0; s < loadst::kStepCount; ++s)
            for (int r = 0; r < 3; ++r) {
                ++k;
                src.record(c, s, 10.0 + 37.5 * (k % 41), (k % 4) == 0);
            }
    loadst::History back;
    check(back.parse(src.serialize()), "Rundreise parse");
    check(back.version == 10, "Rundreise version");
    for (const char* c : cores)
        for (int s = 0; s < loadst::kStepCount; ++s)
            for (bool first : {false, true})
                check(std::fabs(back.expectedMs(c, s, first) - src.expectedMs(c, s, first)) < 0.06,
                      std::string("Rundreise ") + c + " " + loadst::stepKey(s));
    check(back.serialize() == src.serialize(), "Rundreise bit-gleich");

    // Kaputtes/fremdes JSON: false, Verlauf bleibt unverändert; Unbekanntes wird überlesen.
    loadst::History keep = back;
    for (const char* bad : {"", "{", "nicht json", "{\"cores\": [1,2]}", "{\"version\": 3,}", "[]"}) {
        loadst::History t = keep;
        check(!t.parse(bad), std::string("kaputt abgelehnt: ") + bad);
        check(t.serialize() == keep.serialize(), "kaputt: unverändert");
    }
    loadst::History extra;
    check(extra.parse("{\"version\": 7, \"x\": [true, null, \"a\\\"b\"], \"cores\": {\"prboom\": {\"steps\": "
                      "{\"load\": {\"avg_ms\": 61.5, \"n\": 4, \"neu\": 1}}, \"other\": {}}}}"),
          "Unbekanntes überlesen");
    check(extra.version == 7 && near(extra.expectedMs("prboom", loadst::kLoadGame, false), 61.5), "Werte gelesen");
    loadst::History empty;
    check(empty.parse(loadst::History().serialize()), "leerer Verlauf rundreisefähig");
}

std::array<bool, loadst::kStepCount> allUsed(bool stop) {
    std::array<bool, loadst::kStepCount> u{};
    u.fill(true);
    u[loadst::kStopPrevious] = stop;
    return u;
}

// Fortschritt läuft nie zurück, erreicht 1 erst am Ende; Restzeit ≥ 0, sinkt bei planmäßigem Lauf.
void testSessionSweep() {
    const double scales[] = {0.2, 1.0, 3.0, 10.0};       // tatsächliche Dauer / erwartete
    const double bases[] = {30.0, 400.0, 2500.0};         // erwartete Dauer je Schritt (ms)
    for (double base : bases)
        for (double scale : scales)
            for (bool stop : {false, true})
                for (bool history : {false, true}) {
                    std::array<double, loadst::kStepCount> e{};
                    for (int i = 0; i < loadst::kStepCount; ++i) e[i] = base * (1 + i % 3);
                    loadst::Session s;
                    const double t0 = 1000.0;
                    s.begin(t0, e, allUsed(stop), history, false);
                    double t = t0, last = -1.0;
                    bool monotone = true, belowOne = true, etaOk = true;
                    for (int step = 0; step < loadst::kStepCount; ++step) {
                        if (step == loadst::kStopPrevious && !stop) continue;
                        s.startStep(step, t);
                        const double dur = e[step] / 1000.0 * scale;
                        for (int k = 0; k <= 8; ++k) {
                            const double now = t + dur * k / 8.0;
                            const double p = s.progress(now);
                            if (p + 1e-12 < last) monotone = false;
                            if (p >= 1.0) belowOne = false;
                            last = p;
                            if (s.remainingS(now) < 0.0) etaOk = false;
                            const std::string eta = s.etaText(now);
                            if (eta.empty()) etaOk = false;
                        }
                        t += dur;
                    }
                    s.finishStep(loadst::kFirstFrame, t);
                    const std::string tag = " (Basis " + std::to_string(base) + ", Faktor " + std::to_string(scale) + ")";
                    check(monotone, "Fortschritt monoton" + tag);
                    check(belowOne, "Fortschritt < 1 vor dem Ende" + tag);
                    check(etaOk, "Restzeit/Text gültig" + tag);
                    check(s.allDone() && near(s.progress(t), 1.0), "Ende = 1" + tag);
                    check(s.etaText(t) == "fertig", "Ende: fertig" + tag);
                    check(s.state(loadst::kStopPrevious) == (stop ? loadst::StepState::Done : loadst::StepState::Skipped),
                          "Spielwechsel-Schritt" + tag);
                }
}

void testSessionTexts() {
    std::array<double, loadst::kStepCount> e{};
    e.fill(0.0);
    e[loadst::kSystemCheck] = 30;
    e[loadst::kGameData] = 30;
    e[loadst::kCore] = 80;
    e[loadst::kLoadGame] = 3000;
    e[loadst::kFirstFrame] = 20000;
    loadst::Session s;
    s.begin(0.0, e, allUsed(false), true, false);
    s.startStep(loadst::kSystemCheck, 0.0);
    check(s.etaText(0.0) == "noch etwa 25 s", "Restzeit auf 5 s gerundet: " + s.etaText(0.0));
    s.startStep(loadst::kGameData, 0.03);
    s.startStep(loadst::kCore, 0.06);
    s.startStep(loadst::kLoadGame, 0.14);
    s.startStep(loadst::kFirstFrame, 3.1);
    check(s.etaText(15.0).find("noch etwa") == 0, "Restzeit läuft: " + s.etaText(15.0));
    check(s.etaText(22.0) == "gleich fertig", "unter 5 s: gleich fertig (" + s.etaText(22.0) + ")");
    // Überlauf: Erstes Bild 20 s erwartet, nach 35 s → „dauert länger als sonst (üblich: 20 s)“.
    int step = -1;
    double usual = 0.0;
    check(s.overdue(36.0, step, usual) && step == loadst::kFirstFrame && near(usual, 20.0), "überfällig erkannt");
    check(s.etaText(36.0) == "Erstes Bild dauert länger als sonst (üblich: 20 s) – läuft noch", "Text überfällig: " + s.etaText(36.0));
    check(!s.overdue(25.0, step, usual), "nicht überfällig bei 1,1 × üblich");
    // Ohne Verlauf: „Erster Start – dauert etwa N s“
    loadst::Session f;
    f.begin(0.0, e, allUsed(false), false, false);
    check(f.etaText(0.0) == "Erster Start – dauert etwa 24 s", "ohne Verlauf: " + f.etaText(0.0));
    f.setHistory(false, true);
    check(f.etaText(0.0).find("Erster Start nach Installation/Update") == 0, "erster Start nach Update");
    // Fehlschlag: Schritt wird rot, Session nicht „fertig“-falsch.
    loadst::Session g;
    g.begin(0.0, e, allUsed(false), true, false);
    g.startStep(loadst::kLoadGame, 0.5);
    g.failStep(loadst::kLoadGame, 0.9);
    check(g.state(loadst::kLoadGame) == loadst::StepState::Failed && near(g.durationS(loadst::kLoadGame, 5.0), 0.4),
          "Fehlschlag mit Dauer");
    check(g.state(loadst::kSystemCheck) == loadst::StepState::Pending, "übersprungene Startschritte bleiben offen");
}

// Startsignal: erster Frame mit Inhalt; DOS: Grafikmodus bzw. 3 s Ruhe; schwarz: fester Zeitpunkt.
void testFrameSignal() {
    using FS = loadst::FrameSignal;
    for (double fps : {35.0, 50.0, 60.0, 70.0, 72.0}) {
        const double dt = 1.0 / fps;
        const std::string tag = " @" + std::to_string(static_cast<int>(fps)) + " Hz";
        // Shooter: 3 schwarze Frames, dann Titelbild.
        FS a;
        a.reset(false, FS::blackFallbackFor(0.3));
        double t = 0.0;
        for (int i = 0; i < 3; ++i, t += dt) check(a.onFrame(t, 960, 600, false) == FS::kWait, "schwarz wartet" + tag);
        check(a.onFrame(t, 960, 600, true) == FS::kContent, "Inhalt = Signal" + tag);
        // DOS-AUTOBOOT wie im Smoke-Test: 95 Frames 640×400 (DOS-Start, schwarz bzw. Text), dann 320×200.
        FS d;
        d.reset(true, FS::blackFallbackFor(1.5));
        t = 0.0;
        FS::Result r = FS::kWait;
        for (int i = 0; i < 95 && r == FS::kWait; ++i, t += dt) r = d.onFrame(t, 640, 400, i > 40);   // Text ab Frame 40
        check(r == FS::kWait || r == FS::kSettled, "DOS-Start: Textmodus allein ist kein Spiel (vor 3 s)" + tag);
        if (r == FS::kWait) check(d.onFrame(t, 320, 200, true) == FS::kModeSwitch, "DOS: Grafikmodus = Signal" + tag);
        // DOS-Programm im Textmodus: Signal nach 3 s Inhalt.
        FS x;
        x.reset(true, 8.0);
        t = 0.0;
        r = FS::kWait;
        double content = -1.0;
        for (int i = 0; i < 2000 && r == FS::kWait; ++i, t += dt) {
            r = x.onFrame(t, 640, 400, i >= 10);
            if (i == 10) content = t;
        }
        check(r == FS::kSettled && t - dt - content >= FS::kDosSettleS - 1e-9 && t - dt - content < FS::kDosSettleS + 2 * dt,
              "DOS-Textmodus: nach 3 s" + tag);
        // Nur schwarze Frames: fester Zeitpunkt nach blackFallback (8 s).
        FS b;
        b.reset(false, FS::blackFallbackFor(0.3));
        t = 0.0;
        r = FS::kWait;
        for (int i = 0; i < 5000 && r == FS::kWait; ++i, t += dt) r = b.onFrame(t, 320, 200, false);
        check(r == FS::kBlackTimeout && std::fabs(t - dt - 8.0) < 2 * dt, "schwarz: fester Zeitpunkt 8 s" + tag);
    }
    // Ohne jeden Frame kein Rückfall (dafür ist der Watchdog da); Rückfall skaliert mit der Erwartung.
    FS none;
    none.reset(false, 8.0);
    check(none.onTick(100.0) == FS::kWait && !none.anyFrame(), "ohne Frame kein Signal");
    check(near(FS::blackFallbackFor(1.0), 8.0) && near(FS::blackFallbackFor(6.0), 12.0), "Rückfall ≥ 8 s, 2 × erwartet");
    for (int r = FS::kWait; r <= FS::kBlackTimeout; ++r)
        check(std::string(FS::describe(static_cast<FS::Result>(r))).size() > 3, "describe");
}

void testWatchdog() {
    loadst::Watchdog w;
    w.reset(100.0);
    check(!w.hint(109.9) && w.hint(110.0), "Hinweis ab 10 s");
    int logs = 0;
    for (double t = 100.0; t <= 135.0; t += 0.25) logs += w.shouldLog(t) ? 1 : 0;
    check(logs == 3, "Logzeile je volle 10 s (10/20/30): " + std::to_string(logs));
    w.progress(135.0);
    check(!w.hint(140.0) && !w.shouldLog(140.0), "Fortschritt setzt zurück");
    for (double total : {0.1, 2.0, 15.0, 15.1, 60.0})
        check(near(loadst::Watchdog::errorAfter(total), std::max(30.0, 2.0 * total)), "Fehlerseite nach max(30 s, 2×)");
}

void testChecks() {
    using L = loadst::Level;
    check(loadst::checkOpenXr(true, "Oculus 83.0").level == L::Ok, "OpenXR ok");
    check(loadst::checkOpenXr(false, "").level == L::Fail, "OpenXR fehlt");
    check(loadst::checkController("", 1.0).level == L::Pending, "Controller: erst nach 3 s bewerten");
    check(loadst::checkController("", 3.0).level == L::Warn, "Controller fehlt nach 3 s");
    check(loadst::checkController("/interaction_profiles/oculus/touch_controller", 0.5).detail == "Touch-Controller",
          "Touch-Controller");
    check(loadst::profileName("/interaction_profiles/meta/touch_controller_plus") == "Touch-Plus-Controller", "Touch Plus");
    check(loadst::profileName("/interaction_profiles/khr/simple_controller") == "Einfacher Controller", "Simple");
    for (long long mb : {-1LL, 0LL, 49LL, 50LL, 299LL, 300LL, 1023LL, 1024LL, 48000LL}) {
        const loadst::CheckLine c = loadst::checkStorage(mb);
        const L want = mb < 0 ? L::Warn : mb < 50 ? L::Fail : mb < 300 ? L::Warn : L::Ok;
        check(c.level == want, "Speicher " + std::to_string(mb) + " MB");
        check(c.detail.find('.') == std::string::npos, "Speicher mit Dezimalkomma");
    }
    check(loadst::checkStorage(1536).detail == "1,5 GB frei", "GB-Anzeige");
    for (int p = -1; p <= 100; ++p)
        for (bool charging : {false, true}) {
            const loadst::CheckLine c = loadst::checkBattery(p, charging);
            check(c.level == (p >= 0 && p < 15 && !charging ? L::Warn : L::Ok), "Akku " + std::to_string(p));
        }
    check(loadst::checkLog("").level == L::Warn && loadst::checkLog("/a/b/session-1.log").detail == "session-1.log", "Log");
    check(loadst::checkPreviousRun(true, "/x/session-9.log").detail.find("session-9.log") != std::string::npos, "Absturz");
    check(loadst::checkPreviousRun(false, "").level == L::Ok, "sauber beendet");
    check(loadst::checkLibrary(0, "Ordner nach …").level == L::Warn && loadst::checkLibrary(1, "").detail == "1 Spiel gefunden",
          "Bibliothek");
    check(loadst::checkGameData(false, false, "X").level == L::Fail && loadst::checkGameData(true, false, "X").level == L::Fail &&
          loadst::checkGameData(true, true, "X").level == L::Ok, "Spieldaten");
    check(loadst::checkCore(true, "").level == L::Pending && loadst::checkCore(false, "prboom").level == L::Fail, "Core");
    check(loadst::blocking({loadst::checkCore(false, "prboom")}) && !loadst::blocking({loadst::checkBattery(5, false)}),
          "nur Pflichtprüfungen blockieren");
    check(loadst::logLine(loadst::checkBattery(80, false)) == "✓ Akku: 80 %", "Logzeile");
}

void testErrors() {
    const std::string shooter = "/storage/emulated/0/Android/data/xr.shooter/files/games/quake/Quake";
    check(loadst::pushCommand("xr.shooter", shooter) == "Am PC: APP_ID=xr.shooter SYSTEM=quake tools/push-game.sh <Spielordner> Quake",
          "push-game Shooter: " + loadst::pushCommand("xr.shooter", shooter));
    check(loadst::pushCommand("xr.dosgames", "/x/files/games/dos/Doom2/") ==
          "Am PC: APP_ID=xr.dosgames tools/push-game.sh <Spielordner> Doom2", "push-game DOS");
    check(loadst::pushCommand("xr.island", "").find("<Name>") != std::string::npos, "ohne Pfad: Platzhalter");
    const loadst::Failure all[] = {loadst::Failure::NoGame, loadst::Failure::StartFileMissing, loadst::Failure::CoreMissing,
                                   loadst::Failure::CoreBroken, loadst::Failure::DataUnreadable, loadst::Failure::LoadFailed,
                                   loadst::Failure::Watchdog};
    for (loadst::Failure f : all)
        for (const char* app : {"xr.shell", "xr.dosgames", "xr.shooter", "xr.adventure", "xr.island"}) {
            const loadst::ErrorText e = loadst::errorText(f, "Detail", app, shooter, "tyrquake", 31.0);
            check(!e.title.empty() && !e.remedy.empty(), std::string("Fehlertext ") + app);
            check(e.detail == "Detail", "Detail übernommen");
        }
    check(loadst::errorText(loadst::Failure::Watchdog, "", "xr.shell", "", "x", 31.0).title.find("31 s") != std::string::npos,
          "Watchdog nennt die Zeit");
}

} // namespace

int main() {
    testFormat();
    testAverage();
    testHistory();
    testSessionSweep();
    testSessionTexts();
    testFrameSignal();
    testWatchdog();
    testChecks();
    testErrors();
    std::printf("test_load_status: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
