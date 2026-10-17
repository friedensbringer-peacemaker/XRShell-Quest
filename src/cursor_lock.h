// XRShell — Spielzeiger an den Laser koppeln („Zeiger lernen“, xr.dosgames). Abhängigkeitsfrei,
// Host-Test tests/test_cursor_lock.cpp.
//
// Warum: Viele DOS-Spiele lesen nur relative Mausbewegung (Mickeys, INT 33 Fkt. 0Bh) und rechnen
// sie mit eigenem Maßstab, eigener Beschleunigung und eigenem Randanschlag in ihren Zeiger um
// (Worms: mit „direct“ bleibt der Pfeil stehen, relativ driftet er). Faktor + Versatz raten das
// nur von außen. Hier wird die Zeigerposition des Spiels selbst gelesen und nachgeregelt:
//
// Lernen (≈ 1,5–2 s, Laser und Tasten gehen solange nicht ans Spiel): XRShell schiebt die Maus
// nach links oben, dann in kleinen Schritten +x, +y, +x, +y, −x, −y, und sucht im Arbeitsspeicher
// des Cores (RETRO_ENVIRONMENT_SET_MEMORY_MAPS) die 16-Bit-Werte, die genau so mitlaufen: wachsen
// bei +x, bleiben bei ±y stehen, fallen bei −x, gleich große Schritte. Danach werden nur diese
// Kandidaten bis an den Anschlag geschoben, bis sie stillstehen (Bereich, plausibel zur
// Bildgröße). Das ist die X-Position des Spielzeigers (Y entsprechend). 32-Bit-Werte gehen mit (Little Endian: das untere
// Wort läuft gleich mit), jede Byte-Adresse wird geprüft.
//
// Folgen: Ziel = min + Laser-UV · (max − min), Fehler = Ziel − (Position + noch nicht verbuchte
// Bewegung); gesendet wird der Fehler in Maus-Einheiten. „Noch nicht verbucht“: DOS-Spiele holen
// die Mickeys nur in ihrem eigenen Takt ab — bis sich die Position ändert, zählt das Gesendete als
// unterwegs (sonst schießt der Zeiger über). Reagiert die Position länger nicht (anderer
// Bildschirm, Spiel speichert den Zeiger woanders), gilt die Kopplung als verloren → XRShell fällt
// auf die relative Maus zurück, bis neu gelernt wird.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace curlock {

constexpr const char* kOptionKey = "xrshell_cursor_lock";

// Ein Speicherbereich des Cores: logische Adresse start … start + len, Daten bei ptr.
struct Region {
    const uint8_t* ptr = nullptr;
    uint32_t start = 0;
    uint32_t len = 0;
};

inline bool read16(const std::vector<Region>& regs, uint32_t addr, int& out) {
    for (const Region& r : regs) {
        if (!r.ptr || addr < r.start || addr + 2 > r.start + r.len) continue;
        const uint8_t* p = r.ptr + (addr - r.start);
        out = p[0] | (p[1] << 8);
        return true;
    }
    return false;
}

struct Axis {
    uint32_t addr = 0;
    int min = 0, max = 0;          // Bereich des Spielwerts (Randanschlag links/oben … rechts/unten)
    float perDelta = 0.0f;         // Änderung des Spielwerts je gesendeter Maus-Einheit
    int frame = 0;                 // Bildgröße beim Lernen (Pixel), 0 = unbekannt
    bool ok() const { return max > min && perDelta > 0.0f; }
    // Spieleinheiten je Bildpixel: Zeigerwerte zählen fast immer in Pixeln (1) oder im
    // 640er-Raster der Maustreiber (2). Der Bereich ist nur der Anschlag — Worms lässt den
    // Pfeil z. B. nur bis Zeile 167 (darunter die Leiste), Pixel bleiben trotzdem Pixel.
    int unitsPerPixel() const {
        if (frame <= 0) return 0;
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(max + 1) / frame)));
    }
    // Zielwert für Laser-UV 0..1, auf den Anschlag geklemmt.
    float target(float uv) const {
        uv = std::min(std::max(uv, 0.0f), 1.0f);
        const int s = unitsPerPixel();
        const float t = s > 0 ? uv * static_cast<float>(frame * s)
                              : static_cast<float>(min) + uv * static_cast<float>(max - min);
        return std::min(std::max(t, static_cast<float>(min)), static_cast<float>(max));
    }
};

struct Lock {
    Axis x, y;
    bool valid() const { return x.ok() && y.ok(); }
    // „x=1A2B4,0,319,0.500;y=1A2B6,0,199,0.500;f=320,200“ (Adresse hex, f = Bild beim Lernen)
    std::string encode() const {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "x=%X,%d,%d,%.4f;y=%X,%d,%d,%.4f;f=%d,%d", x.addr, x.min, x.max,
                      static_cast<double>(x.perDelta), y.addr, y.min, y.max, static_cast<double>(y.perDelta),
                      x.frame, y.frame);
        return buf;
    }
    static bool decode(const std::string& s, Lock& out) {
        Lock l;
        unsigned ax = 0, ay = 0;
        float px = 0.0f, py = 0.0f;
        if (std::sscanf(s.c_str(), "x=%X,%d,%d,%f;y=%X,%d,%d,%f", &ax, &l.x.min, &l.x.max, &px, &ay, &l.y.min,
                        &l.y.max, &py) != 8)
            return false;
        l.x.addr = ax;
        l.y.addr = ay;
        l.x.perDelta = px;
        l.y.perDelta = py;
        const size_t f = s.find(";f=");   // fehlt in älteren Einträgen → Bereich = ganzes Bild
        if (f != std::string::npos) std::sscanf(s.c_str() + f, ";f=%d,%d", &l.x.frame, &l.y.frame);
        if (l.x.frame < 0 || l.x.frame > 4096 || l.y.frame < 0 || l.y.frame > 4096) l.x.frame = l.y.frame = 0;
        if (!l.valid() || !std::isfinite(px) || !std::isfinite(py)) return false;
        out = l;
        return true;
    }
};

// --- Lernen -----------------------------------------------------------------------------------

class Learner {
public:
    static constexpr int kPush = 6000;     // weit über jeden Bildschirm hinaus (Randanschlag)
    static constexpr int kStep = 10;       // Lernschritt in Maus-Einheiten (klein: gebremste Zeiger messen richtig)
    static constexpr int kWait = 10;       // Polls je Schritt (~0,14 s bei 70 Hz): Spiel holt Mickeys ab
    static constexpr int kCornerPolls = 30;   // Anfang: so lange nach links oben schieben
    static constexpr int kStill = 8;       // Bereich: so viele Polls ohne Änderung = Anschlag erreicht
    static constexpr int kEdgeMax = 160;   // Bereich: höchstens so lange je Richtung schieben (~2,3 s)

    enum class State { Idle, Running, Ok, Failed };

    // frameW/H: Spielbild in Pixeln (Plausibilität des Bereichs).
    void start(int frameW, int frameH) {
        *this = Learner();
        mW = std::max(frameW, 16);
        mH = std::max(frameH, 16);
        mState = State::Running;
    }
    State state() const { return mState; }
    bool running() const { return mState == State::Running; }
    const Lock& lock() const { return mLock; }
    const std::string& message() const { return mMessage; }
    size_t candidatesX() const { return mCandX.size(); }
    size_t candidatesY() const { return mCandY.size(); }

    // Ein Poll: dx/dy = Maus-Einheiten, die jetzt ans Spiel gehen.
    // Ablauf: (1) nach links oben schieben, (2) sechs kleine Schritte ±x/±y und dabei im ganzen
    // Speicher die Werte suchen, die genau so mitlaufen, (3) nur noch diese Kandidaten bis zum
    // Anschlag rechts unten bzw. links oben schieben, bis sie stillstehen (Bereich — auch wenn das
    // Spiel den Zeiger je Abfrage nur ein Stück bewegt, wie die Worms-Waffenleiste).
    void poll(const std::vector<Region>& regs, int& dx, int& dy) {
        dx = dy = 0;
        if (mState != State::Running) return;
        switch (mPhase) {
        case Phase::Corner:
            dx = dy = -kPush;
            if (++mTick >= kCornerPolls) { mPhase = Phase::Settle; mTick = 0; }
            return;
        case Phase::Settle:
            if (++mTick >= kWait) {
                mPrev = snapshot(regs);
                mPhase = Phase::Steps;
                mStep = 0;
                mWait = 0;
            }
            return;
        case Phase::Steps:
            if (mWait > 0) {
                --mWait;
                ++mTick;
                track(regs, mCandX);
                track(regs, mCandY);
                return;
            }
            if (mStep > 0 && !evaluate(regs, mStep - 1)) return;   // fehlgeschlagen
            if (mStep >= kSteps) { mPhase = Phase::EdgeMax; mTick = mStillTicks = 0; remember(regs); return; }
            dx = kMoves[mStep][0];
            dy = kMoves[mStep][1];
            mWait = kWait;
            mTick = 0;
            ++mStep;
            return;
        case Phase::EdgeMax:
        case Phase::EdgeMin: {
            const bool toMax = mPhase == Phase::EdgeMax;
            if (remember(regs)) mStillTicks = 0;
            else ++mStillTicks;
            if ((mStillTicks >= kStill && mTick >= kStill) || mTick >= kEdgeMax) {
                for (Cand& c : mCandX) (toMax ? c.vmax : c.vmin) = c.last;
                for (Cand& c : mCandY) (toMax ? c.vmax : c.vmin) = c.last;
                if (toMax) { mPhase = Phase::EdgeMin; mTick = mStillTicks = 0; return; }
                finish();
                return;
            }
            ++mTick;
            dx = dy = toMax ? kPush : -kPush;
            return;
        }
        }
    }

private:
    enum class Phase { Corner, Settle, Steps, EdgeMax, EdgeMin };
    static constexpr int kSteps = 6;
    static constexpr int kMoves[kSteps][2] = {
        { kStep, 0 }, { 0, kStep }, { kStep, 0 }, { 0, kStep }, { -kStep, 0 }, { 0, -kStep } };

    struct Cand {
        uint32_t addr;
        int last, d1 = 0, sum = 0;
        int changedAt = -1, delay = 0;   // Poll, an dem der Wert im Schritt reagierte; Summe
        int vmin = 0, vmax = 0;
    };

    // Während des Wartens: wann reagiert jeder Kandidat? Kopien zum Neuzeichnen laufen einen
    // Spieltakt nach — die eigentliche Zeigervariable reagiert zuerst.
    void track(const std::vector<Region>& regs, std::vector<Cand>& cands) const {
        if (cands.size() > 4096) return;   // erst nach dem Grobfilter (Rauschen ist dann raus)
        for (Cand& c : cands) {
            int v = 0;
            if (c.changedAt < 0 && read16(regs, c.addr, v) && v != c.last) c.changedAt = mTick;
        }
    }
    // Bereichsphase: aktuelle Werte merken; true, wenn sich einer geändert hat. Werte, die weiter
    // laufen, als ein Zeiger je kommen kann (ungeklemmte Mickey-Zähler, 16-Bit-Überlauf), fliegen
    // raus — sonst stünde die Bereichsphase nie still.
    bool remember(const std::vector<Region>& regs) {
        bool changed = false;
        for (std::vector<Cand>* list : { &mCandX, &mCandY }) {
            const int limit = 16 * (list == &mCandX ? mW : mH);
            std::vector<Cand> keep;
            for (Cand c : *list) {
                int v = 0;
                if (!read16(regs, c.addr, v)) continue;
                if (mTick == 0 && mPhase == Phase::EdgeMax) c.vmin = c.vmax = v;   // Spannweite ab hier
                c.vmin = std::min(c.vmin, v);
                c.vmax = std::max(c.vmax, v);
                if (c.vmax - c.vmin > limit) continue;
                changed |= v != c.last;
                c.last = v;
                keep.push_back(c);
            }
            list->swap(keep);
        }
        return changed;
    }

    static std::vector<uint8_t> snapshot(const std::vector<Region>& regs) {
        size_t total = 0;
        for (const Region& r : regs) total += r.ptr ? r.len : 0;
        std::vector<uint8_t> out;
        out.reserve(total);
        for (const Region& r : regs)
            if (r.ptr) out.insert(out.end(), r.ptr, r.ptr + r.len);
        return out;
    }
    // Adresse ↔ Index im Schnappschuss (gleiche Regionenfolge wie snapshot()).
    static bool addrOf(const std::vector<Region>& regs, size_t index, uint32_t& addr) {
        size_t base = 0;
        for (const Region& r : regs) {
            if (!r.ptr) continue;
            if (index + 2 <= base + r.len) {
                if (index < base) return false;
                addr = r.start + static_cast<uint32_t>(index - base);
                return true;
            }
            base += r.len;
        }
        return false;
    }

    // Erste Bewegung einer Achse: alle 16-Bit-Werte, die gewachsen sind (vorher vs. jetzt).
    static void seed(const std::vector<Region>& regs, const std::vector<uint8_t>& prev,
                     const std::vector<uint8_t>& now, std::vector<Cand>& out) {
        const size_t n = std::min(prev.size(), now.size());
        for (size_t i = 0; i + 1 < n; ++i) {
            const int a = prev[i] | (prev[i + 1] << 8);
            const int b = now[i] | (now[i + 1] << 8);
            if (b <= a || b - a > 4 * kStep) continue;   // gewachsen, aber kein Sprung
            uint32_t addr = 0;
            if (!addrOf(regs, i, addr)) continue;
            Cand c{ addr, b };
            c.d1 = b - a;
            c.sum = c.d1;
            out.push_back(c);
            if (out.size() > 400000) break;   // Rauschen: Bild-/Tonpuffer — Schritte filtern weiter
        }
    }

    // Kandidaten einer Achse nach einem Schritt: expected = +1 / 0 / −1 (Richtung auf dieser Achse).
    static void filter(const std::vector<Region>& regs, std::vector<Cand>& cands, int expected) {
        std::vector<Cand> keep;
        for (Cand c : cands) {
            int v = 0;
            if (!read16(regs, c.addr, v)) continue;
            const int d = v - c.last;
            const int tol = std::max(1, c.d1 / 3);
            bool ok;
            if (expected == 0) ok = d == 0;
            else ok = (expected > 0 ? d : -d) > 0 && std::abs(std::abs(d) - c.d1) <= tol;
            if (!ok) continue;
            c.sum += std::abs(d);
            if (expected != 0) c.delay += c.changedAt >= 0 ? c.changedAt : kWait;
            c.changedAt = -1;
            c.last = v;
            keep.push_back(c);
        }
        cands.swap(keep);
    }

    bool evaluate(const std::vector<Region>& regs, int done) {
        switch (done) {
        case 0: { std::vector<uint8_t> now = snapshot(regs); seed(regs, mPrev, now, mCandX); mPrev.swap(now); return true; }
        case 1: { std::vector<uint8_t> now = snapshot(regs); seed(regs, mPrev, now, mCandY); mPrev.clear();
                  mPrev.shrink_to_fit(); filter(regs, mCandX, 0); return check(); }
        case 2: filter(regs, mCandX, +1); filter(regs, mCandY, 0); return check();
        case 3: filter(regs, mCandX, 0); filter(regs, mCandY, +1); return check();
        case 4: filter(regs, mCandX, -1); filter(regs, mCandY, 0); return check();
        case 5: filter(regs, mCandX, 0); filter(regs, mCandY, -1); return check();
        default: return true;
        }
    }

    bool check() {
        if (!mCandX.empty() && !mCandY.empty()) return true;
        fail(mCandX.empty() && mCandY.empty() ? "kein Zeigerwert gefunden"
             : mCandX.empty() ? "kein X-Wert gefunden" : "kein Y-Wert gefunden");
        return false;
    }
    void fail(const char* why) {
        mState = State::Failed;
        mMessage = why;
        mCandX.clear();
        mCandY.clear();
    }

    bool plausible(const Cand& c, int frame) const {
        const int range = c.vmax - c.vmin;
        return range >= frame / 4 && range <= frame * 16;
    }
    // Bester Kandidat: plausibler Bereich; reagiert am schnellsten (eigentliche Variable vor
    // Kopien), dann gerade Adresse vor ungerader, dann niedrigste Adresse.
    const Cand* best(const std::vector<Cand>& c, int frame) const {
        const Cand* b = nullptr;
        for (const Cand& x : c) {
            if (!plausible(x, frame)) continue;
            if (!b) { b = &x; continue; }
            const bool ea = (x.addr & 1u) == 0, eb = (b->addr & 1u) == 0;
            const bool better = x.delay != b->delay ? x.delay < b->delay : ea != eb ? ea : x.addr < b->addr;
            if (better) b = &x;
        }
        return b;
    }

    void finish() {
        const Cand* cx = best(mCandX, mW);
        const Cand* cy = best(mCandY, mH);
        if (!cx || !cy) {
            fail(!cx && !cy ? "Bereich unplausibel" : !cx ? "X-Bereich unplausibel" : "Y-Bereich unplausibel");
            return;
        }
        mLock.x = Axis{ cx->addr, cx->vmin, cx->vmax, static_cast<float>(cx->sum) / (3.0f * kStep), mW };
        mLock.y = Axis{ cy->addr, cy->vmin, cy->vmax, static_cast<float>(cy->sum) / (3.0f * kStep), mH };
        mState = mLock.valid() ? State::Ok : State::Failed;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu/%zu Kandidaten, X %X (%d…%d), Y %X (%d…%d)", mCandX.size(),
                      mCandY.size(), cx->addr, cx->vmin, cx->vmax, cy->addr, cy->vmin, cy->vmax);
        mMessage = buf;
        mCandX.clear();
        mCandY.clear();
    }

    State mState = State::Idle;
    Phase mPhase = Phase::Corner;
    int mW = 320, mH = 200;
    int mStep = 0, mWait = 0, mTick = 0, mStillTicks = 0;
    std::vector<uint8_t> mPrev;
    std::vector<Cand> mCandX, mCandY;
    Lock mLock;
    std::string mMessage;
};

// --- Folgen -----------------------------------------------------------------------------------

class Follower {
public:
    static constexpr int kPendExpire = 15;   // ~0,2 s ohne Reaktion: Gesendetes gilt als verworfen
    static constexpr int kMaxRetries = 5;    // so oft neu senden, dann pausieren (~1 s)
    static constexpr int kResumeAgree = 3;   // so viele passende Reaktionen → (wieder) gekoppelt
    static constexpr int kProbeEvery = 30;   // pausiert, Laser ruhig: alle ~0,4 s eine Probebewegung
    static constexpr int kProbeWait = 20;    // so lange auf die Reaktion warten
    static constexpr int kProbeMax = 60;     // Probebewegung höchstens (Maus-Einheiten)
    static constexpr int kMaxDelta = 400;    // Maus-Einheiten je Poll
    static constexpr float kGain = 0.6f;     // Anteil des Fehlers je Schritt: stabil, auch wenn der
                                             // gelernte Maßstab bis ~3× zu klein ist (gebremste Zeiger)
    static constexpr size_t kMaxLocks = 4;   // z. B. Menü und Spielrunde (eigenes Programm)

    // Eine Kopplung: sofort aktiv (frisch gelernt).
    void set(const Lock& lock) {
        clear();
        if (!lock.valid()) return;
        mLocks.push_back(lock);
        mWatch.resize(1);
        mCur = 0;
        mActive = true;
    }
    // Gespeicherte Kopplungen eines Spiels: erst beobachten, welche mitläuft (pausiert) — oder
    // active ≥ 0: diese sofort aktiv (gerade gelernt), die übrigen bleiben für den Wechsel.
    void setAll(const std::vector<Lock>& locks, int active = -1) {
        clear();
        for (const Lock& l : locks)
            if (l.valid() && mLocks.size() < kMaxLocks) mLocks.push_back(l);
        mWatch.resize(mLocks.size());
        if (active >= 0 && static_cast<size_t>(active) < mLocks.size()) {
            mCur = static_cast<size_t>(active);
            mActive = true;
        } else {
            mLost = !mLocks.empty();
        }
    }
    const std::vector<Lock>& locks() const { return mLocks; }
    void clear() { *this = Follower(); }
    bool active() const { return mActive; }
    bool lost() const { return mLost; }        // pausiert: relative Maus, observe() sucht weiter
    bool empty() const { return mLocks.empty(); }
    const Lock& lock() const { return mLocks[mCur]; }

    // Pausiert: Die relative Maus fährt; observe() bekommt deren Deltas und prüft für jede
    // Kopplung, ob ihr Wert mitläuft (Bildschirmwechsel vorbei, anderes Programm des Spiels).
    // kResumeAgree passende Änderungen in Folge → diese Kopplung wird aktiv (true).
    // Steht der Laser still (keine Deltas), schickt observe() reihum für eine Kopplung eine
    // Probebewegung Richtung Laser (probeDx/Dy, statt der Deltas zu senden); reagiert deren Wert
    // in die richtige Richtung, wird sie aktiv — ohne dass der Spieler den Laser bewegen muss.
    bool observe(const std::vector<Region>& regs, float u, float v, bool valid, int dx, int dy, int& probeDx,
                 int& probeDy) {
        probeDx = probeDy = 0;
        if (!mLost) return false;
        if (relativeAgree(regs, dx, dy)) return true;
        if (!valid || dx != 0 || dy != 0 || mLocks.empty()) {
            mProbe = -1;
            mProbeTimer = 0;
            return false;
        }
        return probe(regs, u, v, probeDx, probeDy);
    }
    bool observe(const std::vector<Region>& regs, int dx, int dy) {   // ohne Probebewegung
        return mLost && relativeAgree(regs, dx, dy);
    }

    // u/v: Laser im Spielbild 0..1 (Versatz schon eingerechnet), valid = Laser auf dem Bild.
    void poll(const std::vector<Region>& regs, float u, float v, bool valid, int& dx, int& dy) {
        dx = dy = 0;
        if (!mActive) return;
        const Lock& l = mLocks[mCur];
        int cx = 0, cy = 0;
        if (!read16(regs, l.x.addr, cx) || !read16(regs, l.y.addr, cy)) { lose(); return; }
        // Position geändert → Spiel hat abgeholt: Unterwegs-Summe zurücksetzen.
        if (cx != mLastX) { mPendX = 0; mAge = 0; mRetries = 0; }
        if (cy != mLastY) { mPendY = 0; mAge = 0; mRetries = 0; }
        mLastX = cx;
        mLastY = cy;
        if (!valid) { mPendX = mPendY = 0; mAge = 0; return; }
        // Spürbare Bewegung unterwegs (≥ 2 Spieleinheiten), aber keine Reaktion: nach kPendExpire
        // Polls gilt sie als verworfen — Worms setzt beim Bildschirmwechsel den Zeiger selbst (z. B.
        // auf „EXIT“) und löscht die Mickeys; wer weiter wartet, lässt den Pfeil dort stehen. Neu
        // senden; nach kMaxRetries erfolglosen Versuchen pausieren. Kleinere Reste bleiben liegen
        // (Spiel braucht mehrere Mickeys pro Einheit); am Anschlag ist das Ziel geklemmt.
        const bool pending = std::fabs(static_cast<float>(mPendX) * l.x.perDelta) >= 2.0f ||
                             std::fabs(static_cast<float>(mPendY) * l.y.perDelta) >= 2.0f;
        if (!pending) {
            mAge = 0;
        } else if (++mAge > kPendExpire) {
            mPendX = mPendY = mAge = 0;
            if (++mRetries > kMaxRetries) { lose(); return; }
        }
        dx = axisDelta(l.x, u, cx, mPendX);
        dy = axisDelta(l.y, v, cy, mPendY);
        mPendX += dx;
        mPendY += dy;
    }

private:
    bool relativeAgree(const std::vector<Region>& regs, int dx, int dy) {
        for (size_t i = 0; i < mLocks.size(); ++i) {
            Watch& w = mWatch[i];
            const Lock& l = mLocks[i];
            int cx = 0, cy = 0;
            if (!read16(regs, l.x.addr, cx) || !read16(regs, l.y.addr, cy)) { w = Watch(); continue; }
            if (w.obsX < 0) { w.obsX = cx; w.obsY = cy; continue; }
            w.sentX += dx;
            w.sentY += dy;
            judge(w, cx, w.obsX, w.sentX, l.x.perDelta);
            judge(w, cy, w.obsY, w.sentY, l.y.perDelta);
            if (w.agree >= kResumeAgree) {
                activate(i, cx, cy);
                return true;
            }
        }
        return false;
    }

    // Probebewegung (pausiert, Laser ruhig): reihum je Kopplung Richtung Laser, Reaktion prüfen.
    bool probe(const std::vector<Region>& regs, float u, float v, int& outDx, int& outDy) {
        if (mProbe >= 0) {
            const Lock& l = mLocks[static_cast<size_t>(mProbe)];
            int cx = 0, cy = 0;
            if (read16(regs, l.x.addr, cx) && read16(regs, l.y.addr, cy)) {
                const int mx = cx - mProbeX0, my = cy - mProbeY0;
                const bool wrong = (mx != 0 && (mProbeDx == 0 || (mx > 0) != (mProbeDx > 0))) ||
                                   (my != 0 && (mProbeDy == 0 || (my > 0) != (mProbeDy > 0)));
                if ((mx != 0 || my != 0) && !wrong) {
                    activate(static_cast<size_t>(mProbe), cx, cy);
                    return true;
                }
                if (wrong) mProbeTimer = kProbeWait;   // reagiert falsch → nächste Kopplung
            }
            if (++mProbeTimer > kProbeWait) {
                mProbe = -1;
                mProbeTimer = 0;
                mProbeNext = (mProbeNext + 1) % mLocks.size();
            }
            return false;
        }
        if (++mProbeTimer < kProbeEvery) return false;
        mProbeTimer = 0;
        const size_t i = mProbeNext % mLocks.size();
        const Lock& l = mLocks[i];
        int cx = 0, cy = 0;
        if (!read16(regs, l.x.addr, cx) || !read16(regs, l.y.addr, cy)) {
            mProbeNext = (i + 1) % mLocks.size();
            return false;
        }
        const float ex = l.x.target(u) - static_cast<float>(cx), ey = l.y.target(v) - static_cast<float>(cy);
        auto amount = [](float err, float perDelta) {
            if (std::fabs(err) < 2.0f) return 0;
            const int d = static_cast<int>(std::lround(err / perDelta));
            return std::min(std::max(d, -kProbeMax), kProbeMax);
        };
        const int pdx = amount(ex, l.x.perDelta), pdy = amount(ey, l.y.perDelta);
        if (pdx == 0 && pdy == 0) {   // Zeiger dieser Kopplung steht schon am Laser
            mProbeNext = (i + 1) % mLocks.size();
            return false;
        }
        mProbe = static_cast<int>(i);
        mProbeX0 = cx;
        mProbeY0 = cy;
        mProbeDx = outDx = pdx;
        mProbeDy = outDy = pdy;
        return false;
    }

    struct Watch { int obsX = -1, obsY = -1, sentX = 0, sentY = 0, agree = 0; };

    static void judge(Watch& w, int cur, int& last, int& sent, float perDelta) {
        if (cur == last) return;
        const int change = cur - last;
        const bool fits = sent != 0 && (change > 0) == (sent > 0) &&
                          std::abs(change) <= std::abs(sent) * perDelta * 2.0f + 2.0f;
        w.agree = fits ? w.agree + 1 : 0;
        last = cur;
        sent = 0;
    }
    static int axisDelta(const Axis& a, float uv, int cur, int pending) {
        const float t = a.target(uv);
        const float predicted = static_cast<float>(cur) + static_cast<float>(pending) * a.perDelta;
        const float err = t - predicted;
        const float dead = std::max(0.5f, a.perDelta * 0.5f);
        if (std::fabs(err) < dead) return 0;
        int d = static_cast<int>(std::lround(err * kGain / a.perDelta));
        if (d == 0) d = err > 0.0f ? 1 : -1;
        return std::min(std::max(d, -kMaxDelta), kMaxDelta);
    }
    void activate(size_t i, int cx, int cy) {
        mCur = i;
        mActive = true;
        mLost = false;
        mLastX = cx;
        mLastY = cy;
        mPendX = mPendY = mAge = mRetries = 0;
        mProbe = -1;
        mProbeTimer = 0;
        for (Watch& w : mWatch) w = Watch();
    }
    void lose() {
        mActive = false;
        mLost = true;
        mProbe = -1;
        mProbeTimer = 0;
        mProbeNext = mCur;   // zuerst die bisherige Kopplung probieren
        for (Watch& w : mWatch) w = Watch();
    }

    std::vector<Lock> mLocks;
    std::vector<Watch> mWatch;
    size_t mCur = 0;
    bool mActive = false, mLost = false;
    int mLastX = -1, mLastY = -1;
    int mPendX = 0, mPendY = 0, mAge = 0, mRetries = 0;
    int mProbe = -1, mProbeTimer = 0, mProbeX0 = 0, mProbeY0 = 0, mProbeDx = 0, mProbeDy = 0;   // pausiert
    size_t mProbeNext = 0;
};

// Mehrere Kopplungen eines Spiels als ein Optionswert: „<lock>|<lock>…“.
inline std::vector<Lock> decodeList(const std::string& s) {
    std::vector<Lock> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t end = std::min(s.find('|', pos), s.size());
        Lock l;
        if (end > pos && Lock::decode(s.substr(pos, end - pos), l)) out.push_back(l);
        pos = end + 1;
    }
    return out;
}
inline std::string encodeList(const std::vector<Lock>& locks) {
    std::string out;
    for (const Lock& l : locks) out += (out.empty() ? "" : "|") + l.encode();
    return out;
}
// Neu gelernte Kopplung vorn einfügen; gleiche Adressen ersetzen, höchstens kMaxLocks behalten.
inline std::vector<Lock> addLock(std::vector<Lock> locks, const Lock& fresh) {
    locks.erase(std::remove_if(locks.begin(), locks.end(), [&](const Lock& l) {
        return l.x.addr == fresh.x.addr && l.y.addr == fresh.y.addr; }), locks.end());
    locks.insert(locks.begin(), fresh);
    if (locks.size() > Follower::kMaxLocks) locks.resize(Follower::kMaxLocks);
    return locks;
}

} // namespace curlock
