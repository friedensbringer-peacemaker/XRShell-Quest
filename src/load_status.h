// XRShell — Ladeanzeige (Skill xr-loading-screen, L3–L7): Schritte des libretro-Starts, gemessene
// Ladezeiten mit gleitendem Mittel (files/xr-load-times.json), Fortschritt und Restzeit, Startsignal
// „erstes echtes Spielbild“, Watchdog, Systemcheck-Zeilen und Fehlertexte.
//
// Abhängigkeitsfrei (nur Standardbibliothek) und host-getestet (tests/test_load_status.cpp).
// Kandidat für xr-common (xrc/load_status.h). Gezeichnet wird in load_screen.*, der Ablauf
// (Lade-Thread, Phasen) steht in main.cpp.
//
// Zeiten: Sekunden als double (steady_clock des Aufrufers), Dauern in der Datei in Millisekunden.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace loadst {

// --- Schritte ---------------------------------------------------------------------------------
// Reihenfolge = Anzeige. „Vorheriges Spiel beenden“ gibt es nur beim Spielwechsel.
enum Step { kSystemCheck, kStopPrevious, kGameData, kCore, kLoadGame, kFirstFrame, kStepCount };

inline const char* stepName(int step) {
    static const char* const names[kStepCount] = {"Systemcheck", "Vorheriges Spiel beenden", "Spieldaten prüfen",
                                                  "Core laden", "Spiel laden", "Erstes Bild"};
    return step >= 0 && step < kStepCount ? names[step] : "?";
}
// Schlüssel in xr-load-times.json (stabil, ASCII).
inline const char* stepKey(int step) {
    static const char* const keys[kStepCount] = {"check", "stop", "data", "core", "load", "frame"};
    return step >= 0 && step < kStepCount ? keys[step] : "?";
}

// Warum ein Start scheiterte (Fehlerseite, L7).
enum class Failure { None, NoGame, StartFileMissing, CoreMissing, CoreBroken, DataUnreadable, LoadFailed, Watchdog };

// --- Zahlen und Texte -------------------------------------------------------------------------
// „0,4 s“ unter 10 s, sonst „14 s“ (Dezimalkomma).
inline std::string seconds(double s) {
    if (!(s >= 0.0)) s = 0.0;
    char buf[32];
    if (s < 9.95) std::snprintf(buf, sizeof(buf), "%.1f s", s);
    else std::snprintf(buf, sizeof(buf), "%.0f s", s);
    std::string out = buf;
    for (char& c : out)
        if (c == '.') c = ',';
    return out;
}

// Core-Schlüssel aus dem .so-Namen: „dosbox_pure_libretro_android.so“ → „dosbox_pure“.
inline std::string coreKey(const std::string& soName) {
    std::string key = soName.substr(soName.rfind('/') == std::string::npos ? 0 : soName.rfind('/') + 1);
    const size_t cut = key.find("_libretro");
    if (cut != std::string::npos) key = key.substr(0, cut);
    else if (key.size() > 3 && key.compare(key.size() - 3, 3, ".so") == 0) key = key.substr(0, key.size() - 3);
    return key.empty() ? std::string("unbekannt") : key;
}

// --- Standardwerte ohne Verlauf ---------------------------------------------------------------
// Aus den Core-Smoke-Tests auf der Quest 3 (tests/out/core-smoke/*/bericht.txt, 2026-10):
// retro_load_game prboom 48–60 ms, tyrquake 111–127 ms, dosbox_pure 70–127 ms; DOSBox-Pure schaltet
// beim AUTOBOOT nach 84–95 Frames (≈ 1,2–1,4 s bei 70 Hz) in den Grafikmodus des Spiels.
// Erster Start nach Installation/Update: Core aus dem APK entpacken (einige MB, ScummVM dazu
// Themes/Engine-Daten).
inline double defaultMs(const std::string& core, int step, bool firstRun) {
    switch (step) {
    case kSystemCheck: return 30.0;
    case kStopPrevious: return 200.0;
    case kGameData: return core == "scummvm" ? 60.0 : 30.0;
    case kCore:
        if (!firstRun) return 80.0;
        if (core == "scummvm") return 2500.0;
        if (core == "dosbox_pure") return 700.0;
        return 300.0;
    case kLoadGame:
        if (core == "prboom") return 55.0;
        if (core == "ecwolf") return 90.0;
        if (core == "tyrquake") return 120.0;
        if (core == "dosbox_pure") return 100.0;
        if (core == "scummvm") return 400.0;
        return 40.0;   // übrige Cores
    case kFirstFrame:
        if (core == "dosbox_pure") return 1500.0;   // AUTOBOOT bis Grafikmodus
        if (core == "scummvm") return 1500.0;
        if (core == "tyrquake") return 600.0;
        if (core == "ecwolf") return 500.0;
        return 300.0;
    default: return 0.0;
    }
}

// --- Gleitendes Mittel ------------------------------------------------------------------------
struct Average {
    double avgMs = 0.0;
    int n = 0;
};
constexpr double kAlpha = 0.3;   // Gewicht des neuen Werts
// Neuer Messwert: α = 0,3; Ausreißer (> 3 × Mittel) zählen halb (α/2).
inline void addSample(Average& a, double ms) {
    if (!(ms >= 0.0)) return;
    if (a.n == 0) {
        a.avgMs = ms;
    } else {
        const double alpha = ms > 3.0 * a.avgMs ? kAlpha * 0.5 : kAlpha;
        a.avgMs = a.avgMs + alpha * (ms - a.avgMs);
    }
    ++a.n;
}

// --- Mini-JSON (nur, was xr-load-times.json braucht) -----------------------------------------
namespace json {
struct Value {
    enum Type { kOther, kNumber, kString, kObject } type = kOther;
    double number = 0.0;
    std::string text;
    std::vector<std::pair<std::string, Value>> members;
    const Value* get(const std::string& key) const {
        for (const auto& m : members)
            if (m.first == key) return &m.second;
        return nullptr;
    }
};

class Parser {
public:
    explicit Parser(const std::string& s) : mS(s) {}
    bool parse(Value& out) {
        if (!value(out, 0)) return false;
        skip();
        return mPos == mS.size();
    }

private:
    const std::string& mS;
    size_t mPos = 0;
    void skip() {
        while (mPos < mS.size() && std::isspace(static_cast<unsigned char>(mS[mPos]))) ++mPos;
    }
    bool literal(const char* word) {
        size_t i = 0;
        while (word[i]) {
            if (mPos + i >= mS.size() || mS[mPos + i] != word[i]) return false;
            ++i;
        }
        mPos += i;
        return true;
    }
    bool string(std::string& out) {
        if (mPos >= mS.size() || mS[mPos] != '"') return false;
        ++mPos;
        while (mPos < mS.size() && mS[mPos] != '"') {
            char c = mS[mPos++];
            if (c == '\\') {
                if (mPos >= mS.size()) return false;
                c = mS[mPos++];
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'u') { mPos = std::min(mPos + 4, mS.size()); c = '?'; }
            }
            out += c;
        }
        if (mPos >= mS.size()) return false;
        ++mPos;
        return true;
    }
    bool value(Value& out, int depth) {
        if (depth > 16) return false;
        skip();
        if (mPos >= mS.size()) return false;
        const char c = mS[mPos];
        if (c == '{') {
            out.type = Value::kObject;
            ++mPos;
            skip();
            if (mPos < mS.size() && mS[mPos] == '}') { ++mPos; return true; }
            while (true) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (mPos >= mS.size() || mS[mPos] != ':') return false;
                ++mPos;
                Value v;
                if (!value(v, depth + 1)) return false;
                out.members.emplace_back(std::move(key), std::move(v));
                skip();
                if (mPos < mS.size() && mS[mPos] == ',') { ++mPos; continue; }
                if (mPos < mS.size() && mS[mPos] == '}') { ++mPos; return true; }
                return false;
            }
        }
        if (c == '[') {   // Listen kommen nicht vor — überspringen
            out.type = Value::kOther;
            ++mPos;
            skip();
            if (mPos < mS.size() && mS[mPos] == ']') { ++mPos; return true; }
            while (true) {
                Value ignored;
                if (!value(ignored, depth + 1)) return false;
                skip();
                if (mPos < mS.size() && mS[mPos] == ',') { ++mPos; continue; }
                if (mPos < mS.size() && mS[mPos] == ']') { ++mPos; return true; }
                return false;
            }
        }
        if (c == '"') { out.type = Value::kString; return string(out.text); }
        if (literal("true") || literal("false") || literal("null")) { out.type = Value::kOther; return true; }
        const char* start = mS.c_str() + mPos;
        char* end = nullptr;
        const double d = std::strtod(start, &end);
        if (end == start) return false;
        mPos += static_cast<size_t>(end - start);
        out.type = Value::kNumber;
        out.number = d;
        return std::isfinite(d);
    }
};

inline std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out + "\"";
}
} // namespace json

// --- Verlauf: files/xr-load-times.json --------------------------------------------------------
// {"version": <versionCode>, "cores": {"<core>": {"steps": {"<schritt>": {"avg_ms": …, "n": …}},
//                                                 "first_run": {…}}}}
// Getrennt pro Core; „first_run“ = erster Start nach Installation/Update (Core wird entpackt).
class History {
public:
    struct Profile {
        std::map<std::string, Average> steps, firstRun;
    };
    long long version = 0;   // versionCode, mit dem zuletzt gespeichert wurde
    std::map<std::string, Profile> cores;

    // Gibt es Messwerte für diesen Core (im passenden Topf)?
    bool hasHistory(const std::string& core, bool firstRun) const {
        const auto it = cores.find(core);
        if (it == cores.end()) return false;
        const auto& bucket = firstRun ? it->second.firstRun : it->second.steps;
        for (const auto& kv : bucket)
            if (kv.second.n > 0) return true;
        return false;
    }
    // Erwartete Dauer: Mittel des Topfs, sonst Standardwert. Beim ersten Start ohne eigene
    // first_run-Werte zählen gemessene Normalwerte für die Schritte, die das Entpacken nicht betrifft.
    double expectedMs(const std::string& core, int step, bool firstRun) const {
        const auto it = cores.find(core);
        if (it != cores.end()) {
            const auto& bucket = firstRun ? it->second.firstRun : it->second.steps;
            const auto s = bucket.find(stepKey(step));
            if (s != bucket.end() && s->second.n > 0) return s->second.avgMs;
            if (firstRun && step != kCore) {
                const auto n = it->second.steps.find(stepKey(step));
                if (n != it->second.steps.end() && n->second.n > 0) return n->second.avgMs;
            }
        }
        return defaultMs(core, step, firstRun);
    }
    void record(const std::string& core, int step, double ms, bool firstRun) {
        Profile& p = cores[core];
        addSample((firstRun ? p.firstRun : p.steps)[stepKey(step)], ms);
    }

    std::string serialize() const {
        std::string out = "{\n  \"version\": " + std::to_string(version) + ",\n  \"cores\": {";
        bool firstCore = true;
        for (const auto& c : cores) {
            out += firstCore ? "\n" : ",\n";
            firstCore = false;
            out += "    " + json::quote(c.first) + ": {";
            const std::pair<const char*, const std::map<std::string, Average>*> buckets[2] = {
                {"steps", &c.second.steps}, {"first_run", &c.second.firstRun}};
            bool firstBucket = true;
            for (const auto& b : buckets) {
                out += firstBucket ? "\n" : ",\n";
                firstBucket = false;
                out += std::string("      \"") + b.first + "\": {";
                bool firstStep = true;
                for (const auto& s : *b.second) {
                    char num[64];
                    std::snprintf(num, sizeof(num), "%.1f", s.second.avgMs);
                    out += firstStep ? "" : ", ";
                    firstStep = false;
                    out += json::quote(s.first) + ": {\"avg_ms\": " + num + ", \"n\": " + std::to_string(s.second.n) + "}";
                }
                out += "}";
            }
            out += "\n    }";
        }
        out += cores.empty() ? "}\n}\n" : "\n  }\n}\n";
        return out;
    }

    // Unbekanntes wird überlesen; kaputte Datei → false, Verlauf bleibt leer (Standardwerte gelten).
    bool parse(const std::string& text) {
        json::Value root;
        json::Parser parser(text);
        if (!parser.parse(root) || root.type != json::Value::kObject) return false;
        History h;
        if (const json::Value* v = root.get("version"))
            if (v->type == json::Value::kNumber) h.version = static_cast<long long>(v->number);
        if (const json::Value* list = root.get("cores")) {
            if (list->type != json::Value::kObject) return false;
            for (const auto& c : list->members) {
                if (c.second.type != json::Value::kObject) continue;
                Profile& p = h.cores[c.first];
                const std::pair<const char*, std::map<std::string, Average>*> buckets[2] = {
                    {"steps", &p.steps}, {"first_run", &p.firstRun}};
                for (const auto& b : buckets) {
                    const json::Value* bucket = c.second.get(b.first);
                    if (!bucket || bucket->type != json::Value::kObject) continue;
                    for (const auto& s : bucket->members) {
                        const json::Value* avg = s.second.get("avg_ms");
                        const json::Value* n = s.second.get("n");
                        if (!avg || avg->type != json::Value::kNumber || !(avg->number >= 0.0)) continue;
                        Average a;
                        a.avgMs = std::min(avg->number, 3600.0 * 1000.0);
                        a.n = n && n->type == json::Value::kNumber ? std::max(1, static_cast<int>(n->number)) : 1;
                        (*b.second)[s.first] = a;
                    }
                }
            }
        }
        *this = h;
        return true;
    }
};

// --- Ein Ladevorgang --------------------------------------------------------------------------
enum class StepState { Pending, Active, Done, Failed, Skipped };

class Session {
public:
    // expectedMs: erwartete Dauer je Schritt, used: kommt der Schritt vor (sonst „übersprungen“).
    // hasHistory = es gibt gemessene Werte (sonst „Erster Start – dauert etwa N s“).
    void begin(double now, const std::array<double, kStepCount>& expectedMs, const std::array<bool, kStepCount>& used,
               bool hasHistory, bool firstRun) {
        *this = Session{};
        mBegin = now;
        mActive = true;
        mHasHistory = hasHistory;
        mFirstRun = firstRun;
        for (int i = 0; i < kStepCount; ++i) {
            mSteps[i].expectedMs = std::max(0.0, expectedMs[i]);
            mSteps[i].state = used[i] ? StepState::Pending : StepState::Skipped;
        }
    }
    bool active() const { return mActive; }   // begin() lief, finishAll() noch nicht
    void setExpected(int step, double ms) {
        if (step >= 0 && step < kStepCount) mSteps[step].expectedMs = std::max(0.0, ms);
    }
    void setHistory(bool hasHistory, bool firstRun) { mHasHistory = hasHistory; mFirstRun = firstRun; }
    bool hasHistory() const { return mHasHistory; }
    bool firstRun() const { return mFirstRun; }

    // Schritt beginnt bei t; ein noch aktiver früherer Schritt endet dort (erledigt).
    void startStep(int step, double t) {
        if (step < 0 || step >= kStepCount || mSteps[step].state == StepState::Skipped) return;
        for (int i = 0; i < kStepCount; ++i)
            if (i != step && mSteps[i].state == StepState::Active) finishStep(i, t);
        if (mSteps[step].state != StepState::Pending) return;
        mSteps[step].state = StepState::Active;
        mSteps[step].start = t;
    }
    void finishStep(int step, double t) {
        if (step < 0 || step >= kStepCount) return;
        Run& r = mSteps[step];
        if (r.state == StepState::Pending) { r.state = StepState::Active; r.start = t; }
        if (r.state != StepState::Active) return;
        r.state = StepState::Done;
        r.end = std::max(t, r.start);
    }
    void failStep(int step, double t) {
        if (step < 0 || step >= kStepCount) return;
        Run& r = mSteps[step];
        if (r.state == StepState::Pending) { r.state = StepState::Active; r.start = t; }
        if (r.state != StepState::Active) return;
        r.state = StepState::Failed;
        r.end = std::max(t, r.start);
    }
    // Alle offenen Schritte vor `step` überspringen (z. B. kein Spielwechsel).
    void skipStep(int step) {
        if (step >= 0 && step < kStepCount && mSteps[step].state == StepState::Pending)
            mSteps[step].state = StepState::Skipped;
    }
    void end() { mActive = false; }

    StepState state(int step) const { return mSteps[step].state; }
    double expectedMs(int step) const { return mSteps[step].expectedMs; }
    // Gemessene Dauer (Sekunden) erledigter/gescheiterter Schritte, laufend: bis now.
    double durationS(int step, double now) const {
        const Run& r = mSteps[step];
        if (r.state == StepState::Done || r.state == StepState::Failed) return r.end - r.start;
        if (r.state == StepState::Active) return std::max(0.0, now - r.start);
        return 0.0;
    }
    int activeStep() const {
        for (int i = 0; i < kStepCount; ++i)
            if (mSteps[i].state == StepState::Active) return i;
        return -1;
    }
    bool allDone() const {
        for (const Run& r : mSteps)
            if (r.state == StepState::Pending || r.state == StepState::Active) return false;
        return true;
    }
    double elapsedS(double now) const { return std::max(0.0, now - mBegin); }
    double beginTime() const { return mBegin; }
    double totalExpectedS() const {
        double sum = 0.0;
        for (const Run& r : mSteps)
            if (r.state != StepState::Skipped) sum += r.expectedMs;
        return sum / 1000.0;
    }

    // Anteil 0..1: (erwartete Dauer erledigter Schritte + min(laufend, 0,95 · erwartet)) / Summe.
    // Läuft nie zurück; 1 erst, wenn alle Schritte erledigt sind.
    double progress(double now) {
        if (allDone()) { mShown = 1.0; return mShown; }
        double done = 0.0, total = 0.0;
        for (int i = 0; i < kStepCount; ++i) {
            const Run& r = mSteps[i];
            if (r.state == StepState::Skipped) continue;
            const double e = std::max(r.expectedMs, 1.0) / 1000.0;
            total += e;
            if (r.state == StepState::Done || r.state == StepState::Failed) done += e;
            else if (r.state == StepState::Active) done += std::min(now - r.start, 0.95 * e);
        }
        const double target = total > 0.0 ? std::min(done / total, 0.99) : 0.0;
        mShown = std::max(mShown, target);
        return mShown;
    }
    // Restzeit in Sekunden: erwartete Gesamtdauer · (1 − Fortschritt).
    double remainingS(double now) {
        const double p = progress(now);
        double total = 0.0;
        for (const Run& r : mSteps)
            if (r.state != StepState::Skipped) total += std::max(r.expectedMs, 1.0) / 1000.0;
        return std::max(0.0, total * (1.0 - p));
    }
    // Läuft der aktive Schritt über (über 1,5 × üblich und mindestens 1 s darüber)?
    bool overdue(double now, int& step, double& usualS) const {
        step = activeStep();
        if (step < 0) return false;
        const double running = durationS(step, now);
        usualS = mSteps[step].expectedMs / 1000.0;
        return running > usualS * 1.5 && running - usualS > 1.0;
    }

    // Zeile unter dem Fortschrittsbalken (L4).
    std::string etaText(double now) {
        if (allDone()) return "fertig";
        int step = -1;
        double usual = 0.0;
        if (overdue(now, step, usual))
            return std::string(stepName(step)) + " dauert länger als sonst (üblich: " + seconds(usual) + ") – läuft noch";
        if (!mHasHistory) {
            const int total = std::max(1, static_cast<int>(std::ceil(totalExpectedS())));
            return std::string(mFirstRun ? "Erster Start nach Installation/Update" : "Erster Start") +
                   " – dauert etwa " + std::to_string(total) + " s";
        }
        const double rest = remainingS(now);
        if (rest < 5.0) return "gleich fertig";
        const int rounded = static_cast<int>(std::lround(rest / 5.0)) * 5;
        return "noch etwa " + std::to_string(std::max(5, rounded)) + " s";
    }

private:
    struct Run {
        StepState state = StepState::Pending;
        double expectedMs = 0.0;
        double start = 0.0, end = 0.0;
    };
    std::array<Run, kStepCount> mSteps{};
    double mBegin = 0.0;
    double mShown = 0.0;
    bool mActive = false, mHasHistory = false, mFirstRun = false;
};

// --- Startsignal: erstes echtes Spielbild (L8) ------------------------------------------------
// Signal = erster Video-Frame mit Inhalt (nicht nur schwarz). DOSBox-Pure (waitModeSwitch):
// die ersten Frames zeigen den DOS-Start im Textmodus; das Spiel ist da, sobald ein Frame mit Inhalt
// in einer anderen Bildgröße als der allererste Frame kommt (Grafikmodus) — oder spätestens
// settleS nach dem ersten Inhalt (Textmodus-Programme, Startmenü von DOSBox-Pure). Liefert der Core
// nur schwarze Frames, gilt das Bild nach blackFallbackS ab dem ersten Frame als da (fester Zeitpunkt).
class FrameSignal {
public:
    enum Result { kWait, kContent, kModeSwitch, kSettled, kBlackTimeout };
    static constexpr double kDosSettleS = 3.0;
    // Fester Rückfall bei nur schwarzen Bildern: doppelte erwartete Dauer, mindestens 8 s.
    static double blackFallbackFor(double expectedFirstFrameS) { return std::max(8.0, 2.0 * expectedFirstFrameS); }

    void reset(bool waitModeSwitch, double blackFallbackS, double settleS = kDosSettleS) {
        *this = FrameSignal{};
        mWaitMode = waitModeSwitch;
        mBlackFallback = blackFallbackS;
        mSettle = settleS;
    }
    Result onFrame(double now, int w, int h, bool content) {
        if (mFirstFrame < 0.0) { mFirstFrame = now; mFirstW = w; mFirstH = h; }
        ++mFrames;
        if (!content) return onTick(now);
        if (!mWaitMode) return kContent;
        if (w != mFirstW || h != mFirstH) return kModeSwitch;
        if (mFirstContent < 0.0) mFirstContent = now;
        return onTick(now);
    }
    Result onTick(double now) const {
        if (mWaitMode && mFirstContent >= 0.0 && now - mFirstContent >= mSettle) return kSettled;
        if (mFirstFrame >= 0.0 && mFirstContent < 0.0 && now - mFirstFrame >= mBlackFallback) return kBlackTimeout;
        return kWait;
    }
    bool anyFrame() const { return mFirstFrame >= 0.0; }
    double firstFrameTime() const { return mFirstFrame; }
    int frames() const { return mFrames; }
    static const char* describe(Result r) {
        switch (r) {
        case kContent: return "erstes Bild mit Inhalt";
        case kModeSwitch: return "Grafikmodus des Spiels";
        case kSettled: return "Bild steht (Textmodus)";
        case kBlackTimeout: return "fester Zeitpunkt, nur schwarze Bilder";
        default: return "wartet";
        }
    }

private:
    bool mWaitMode = false;
    double mBlackFallback = 8.0, mSettle = kDosSettleS;
    double mFirstFrame = -1.0, mFirstContent = -1.0;
    int mFirstW = 0, mFirstH = 0, mFrames = 0;
};

// --- Watchdog (L5/L7) -------------------------------------------------------------------------
// Fortschritt = neuer Schritt oder neuer Video-Frame. Ohne Fortschritt ab 10 s Hinweis und je
// volle 10 s eine Logzeile; Fehlerseite nach der doppelten erwarteten Gesamtzeit, mindestens 30 s.
class Watchdog {
public:
    static constexpr double kHintS = 10.0;
    static constexpr double kMinErrorS = 30.0;
    static double errorAfter(double totalExpectedS) { return std::max(kMinErrorS, 2.0 * totalExpectedS); }

    void reset(double now) { mLast = now; mLogged = 0; }
    void progress(double now) { mLast = now; mLogged = 0; }
    double stalledS(double now) const { return std::max(0.0, now - mLast); }
    bool hint(double now) const { return stalledS(now) >= kHintS; }
    // true einmal je volle 10 s Stillstand (10, 20, 30 …) — dann eine Logzeile schreiben.
    bool shouldLog(double now) {
        const int bucket = static_cast<int>(stalledS(now) / kHintS);
        if (bucket <= mLogged) return false;
        mLogged = bucket;
        return true;
    }

private:
    double mLast = 0.0;
    int mLogged = 0;
};

// --- Systemcheck (L6) -------------------------------------------------------------------------
enum class Level { Pending, Ok, Warn, Fail };
struct CheckLine {
    Level level = Level::Pending;
    std::string label;    // „Controller“
    std::string detail;   // „Touch-Controller“ bzw. Abhilfe
    bool operator==(const CheckLine& o) const { return level == o.level && label == o.label && detail == o.detail; }
    bool operator!=(const CheckLine& o) const { return !(*this == o); }
};
inline const char* levelMark(Level l) {
    switch (l) {
    case Level::Ok: return "✓";
    case Level::Warn: return "⚠";
    case Level::Fail: return "✗";
    default: return "…";
    }
}
inline std::string logLine(const CheckLine& c) { return std::string(levelMark(c.level)) + " " + c.label + ": " + c.detail; }

inline CheckLine checkOpenXr(bool ok, const std::string& runtime) {
    if (!ok) return {Level::Fail, "OpenXR", "VR-Laufzeit nicht verfügbar – Quest neu starten"};
    return {Level::Ok, "OpenXR", runtime.empty() ? std::string("Laufzeit bereit") : runtime};
}
// Interaktionsprofil (Pfad) → Anzeigename.
inline std::string profileName(const std::string& path) {
    if (path.find("touch_controller_plus") != std::string::npos) return "Touch-Plus-Controller";
    if (path.find("touch_controller_pro") != std::string::npos) return "Touch-Pro-Controller";
    if (path.find("touch_controller") != std::string::npos) return "Touch-Controller";
    if (path.find("simple_controller") != std::string::npos) return "Einfacher Controller";
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
// Erst nach 3 s bewerten (die Laufzeit meldet das Profil erst nach dem Fokus).
inline CheckLine checkController(const std::string& profilePath, double sinceXrS) {
    if (!profilePath.empty()) return {Level::Ok, "Controller", profileName(profilePath)};
    if (sinceXrS < 3.0) return {Level::Pending, "Controller", "wird geprüft …"};
    return {Level::Warn, "Controller", "Keiner aktiv – Controller in die Hand nehmen"};
}
// Freier Speicher in MB (< 0 = unbekannt): unter 50 MB ✗, unter 300 MB ⚠.
inline CheckLine checkStorage(long long freeMb) {
    if (freeMb < 0) return {Level::Warn, "Speicher", "freier Platz unbekannt"};
    char buf[96];
    if (freeMb >= 1024) std::snprintf(buf, sizeof(buf), "%.1f GB frei", static_cast<double>(freeMb) / 1024.0);
    else std::snprintf(buf, sizeof(buf), "%lld MB frei", freeMb);
    std::string text = buf;
    for (char& c : text)
        if (c == '.') c = ',';
    if (freeMb < 50) return {Level::Fail, "Speicher", "Nur " + text + " – Spielstände/Core-Entpacken scheitern"};
    if (freeMb < 300) return {Level::Warn, "Speicher", "Nur " + text + " – Platz schaffen"};
    return {Level::Ok, "Speicher", text};
}
// Akku in % (< 0 = nicht lesbar): unter 15 % ⚠ (außer beim Laden).
inline CheckLine checkBattery(int percent, bool charging) {
    if (percent < 0) return {Level::Ok, "Akku", "nicht lesbar"};
    const std::string value = std::to_string(percent) + " %" + (charging ? " (lädt)" : "");
    if (percent < 15 && !charging) return {Level::Warn, "Akku", value + " – Ladekabel?"};
    return {Level::Ok, "Akku", value};
}
inline CheckLine checkLog(const std::string& path) {
    if (path.empty()) return {Level::Warn, "Sitzungslog", "Kein Log – Fehlersuche eingeschränkt"};
    const size_t slash = path.rfind('/');
    return {Level::Ok, "Sitzungslog", slash == std::string::npos ? path : path.substr(slash + 1)};
}
// Marker vom letzten Start nicht sauber entfernt → ⚠ mit dem Log jener Sitzung.
inline CheckLine checkPreviousRun(bool crashed, const std::string& lastLog) {
    if (!crashed) return {Level::Ok, "Letzter Start", "sauber beendet"};
    const size_t slash = lastLog.rfind('/');
    const std::string name = slash == std::string::npos ? lastLog : lastLog.substr(slash + 1);
    return {Level::Warn, "Letzter Start", "abgestürzt – Log: " + (name.empty() ? std::string("logs/") : name)};
}
// Spieldaten: Bibliothek ohne laufendes Laden (Anzahl) bzw. das gewählte Spiel.
inline CheckLine checkLibrary(size_t games, const std::string& emptyHint) {
    if (games == 0) return {Level::Warn, "Spieldaten", emptyHint.empty() ? std::string("Keine Spiele gefunden") : emptyHint};
    return {Level::Ok, "Spieldaten", std::to_string(games) + (games == 1 ? " Spiel" : " Spiele") + " gefunden"};
}
inline CheckLine checkGameData(bool exists, bool readable, const std::string& name) {
    if (!exists) return {Level::Fail, "Spieldaten", "fehlt: " + name};
    if (!readable) return {Level::Fail, "Spieldaten", "nicht lesbar: " + name};
    return {Level::Ok, "Spieldaten", name};
}
inline CheckLine checkCore(bool present, const std::string& core) {
    if (core.empty()) return {Level::Pending, "Core", "noch kein Spiel gewählt"};
    if (!present) return {Level::Fail, "Core", core + " fehlt im APK"};
    return {Level::Ok, "Core", core};
}
// Pflichtprüfung gescheitert → Fehlerseite statt Weiterladen.
inline bool blocking(const std::vector<CheckLine>& lines) {
    for (const CheckLine& c : lines)
        if (c.level == Level::Fail && (c.label == "Spieldaten" || c.label == "Core" || c.label == "OpenXR")) return true;
    return false;
}

// --- Fehlerseite (L7) -------------------------------------------------------------------------
struct ErrorText {
    std::string title;                 // Klartext, was schiefging
    std::string detail;                // letzte Core-/Engine-Zeile o. Ä.
    std::vector<std::string> remedy;   // was zu tun ist (Befehle am PC)
};

// Übertragungsbefehl am PC (tools/push-game.sh) passend zur App und zum Spielpfad.
inline std::string pushCommand(const std::string& appId, const std::string& gamePath) {
    std::string path = gamePath;
    while (!path.empty() && path.back() == '/') path.pop_back();
    std::string name = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
    if (name.empty()) name = "<Name>";
    std::string system;
    const size_t games = path.find("/games/");
    if (games != std::string::npos) {
        const size_t s0 = games + 7, s1 = path.find('/', s0);
        if (s1 != std::string::npos) system = path.substr(s0, s1 - s0);
    }
    std::string cmd = "APP_ID=" + appId + " ";
    if (appId == "xr.shooter") cmd += "SYSTEM=" + (system.empty() ? std::string("doom") : system) + " ";
    return "Am PC: " + cmd + "tools/push-game.sh <Spielordner> " + name;
}

inline ErrorText errorText(Failure f, const std::string& detail, const std::string& appId, const std::string& gamePath,
                           const std::string& core, double waitedS) {
    ErrorText e;
    e.detail = detail;
    switch (f) {
    case Failure::NoGame:
        e.title = "Kein Spiel gefunden";
        e.remedy = {pushCommand(appId, gamePath.empty() ? std::string("game/<Name>") : gamePath),
                    "Ziel: Android/data/" + appId + "/files/game/<Name>/"};
        break;
    case Failure::StartFileMissing:
        e.title = "Startdatei fehlt im Spielordner";
        e.remedy = {"Spielordner vollständig neu übertragen:", pushCommand(appId, gamePath)};
        break;
    case Failure::CoreMissing:
        e.title = "Core " + core + " fehlt in der App";
        e.remedy = {"App neu bauen und installieren (android: gradlew assembleDebug)",
                    "Der Core wird beim Bauen geladen (libretro-Buildbot)"};
        break;
    case Failure::CoreBroken:
        e.title = "Core " + core + " lässt sich nicht starten";
        e.remedy = {"App neu installieren; hilft das nicht: Log an die Entwicklung"};
        break;
    case Failure::DataUnreadable:
        e.title = "Spieldaten nicht lesbar";
        e.remedy = {"Rechte reparieren – Spiel neu übertragen:", pushCommand(appId, gamePath)};
        break;
    case Failure::LoadFailed:
        e.title = "Der Core konnte das Spiel nicht laden";
        e.remedy = {"Spieldaten vollständig? Neu übertragen:", pushCommand(appId, gamePath),
                    "Andere Version des Spiels? Siehe Spielprofil (docs/)"};
        break;
    case Failure::Watchdog:
        e.title = "Start dauert ungewöhnlich lange (über " + seconds(waitedS) + ")";
        e.remedy = {"Läuft weiter im Hintergrund – erscheint das Bild, geht es von selbst los",
                    "Sonst: VR-Menü → Beenden und das Log prüfen"};
        break;
    case Failure::None:
        break;
    }
    return e;
}

} // namespace loadst
