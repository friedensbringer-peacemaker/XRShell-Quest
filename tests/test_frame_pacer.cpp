// Host-Test für src/frame_pacer.h (Takt des Core-Threads) mit simulierter Uhr.
//
//   clang++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/test_frame_pacer.cpp -o t && ./t
//
// Sweep: Bildrate × Aufwachverzögerung (Jitter) × Rechenzeit pro Frame (passt in die Periode) → mittlere Rate exakt
// (≤ 0,05 % Abweichung), keine Salven (kein Abstand < halbe Periode im Dauerlauf). Dazu Pause
// (mit und ohne reset), einzelner langer Frame, fps-Wechsel 70 → 72 und die alte Logik als
// Gegenprobe (driftet mit Jitter).

#include "frame_pacer.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FEHLER: %s\n", what.c_str());
    }
}

// Deterministischer Zufall 0..1.
struct Rng {
    uint32_t s = 12345;
    double next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<double>(s >> 8) / static_cast<double>(1u << 24);
    }
};

// Simulierter Core-Thread: Start eines Frames, Rechenzeit, Frist, Schlafen bis Frist + Jitter.
struct Sim {
    pacing::FramePacer pacer;
    double now = 0.0;
    std::vector<double> starts;
    Rng rng;
    void frame(double period, double cost, double jitterMax, bool oldLogic = false) {
        const double start = now;
        starts.push_back(start);
        now += cost;
        const double deadline = oldLogic ? start + period : pacer.next(now, period);
        if (deadline > now) now = deadline;
        now += jitterMax * rng.next();   // Aufwachverzögerung
    }
    double rate(size_t from, size_t to) const {
        return static_cast<double>(to - from) / (starts[to] - starts[from]);
    }
    int shortGaps(size_t from, size_t to, double period) const {
        int n = 0;
        for (size_t i = from + 1; i <= to && i < starts.size(); ++i)
            if (starts[i] - starts[i - 1] < period * 0.5) ++n;
        return n;
    }
};

std::string fmt(const char* f, double a, double b, double c) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

} // namespace

int main() {
    // 1. Sweep: Rate exakt, keine Salven.
    for (double fps : {35.0, 50.0, 60.0, 70.0, 72.0, 90.0, 120.0}) {
        const double period = 1.0 / fps;
        for (double jitter : {0.0, 0.0001, 0.001, 0.005}) {
            if (jitter >= period * 0.5) continue;   // Jitter über halbe Periode erzeugt zwangsläufig kurze Abstände
            for (double costShare : {0.05, 0.5, 0.9}) {
                // Nur Fälle, in denen ein Frame samt Aufwachverzögerung in die Periode passt
                // (sonst ist der Core schlicht zu langsam — das prüft Abschnitt 4).
                if (period * costShare + jitter >= period * 0.95) continue;
                Sim s;
                const int n = static_cast<int>(fps * 60);   // 1 min
                for (int i = 0; i < n; ++i) s.frame(period, period * costShare * (0.5 + 0.5 * s.rng.next()), jitter);
                const double r = s.rate(10, s.starts.size() - 1);
                check(std::fabs(r / fps - 1.0) < 5e-4, fmt("Rate %.3f bei %.0f fps (Jitter %.4f)", r, fps, jitter));
                check(s.shortGaps(10, s.starts.size() - 1, period) == 0,
                      fmt("keine Salve bei %.0f fps, Jitter %.4f, Anteil %.2f", fps, jitter, costShare));
            }
        }
    }

    // 2. Gegenprobe: alte Logik (Beginn + Periode) driftet mit Jitter 0,25 ms im Mittel unter 72 fps.
    {
        Sim s;
        for (int i = 0; i < 72 * 60; ++i) s.frame(1.0 / 72, 0.002, 0.0005, true);
        const double r = s.rate(10, s.starts.size() - 1);
        check(r < 71.8, fmt("alte Logik driftet: %.3f fps (%.0f %.0f)", r, 0, 0));
        Sim t;
        for (int i = 0; i < 72 * 60; ++i) t.frame(1.0 / 72, 0.002, 0.0005);
        check(std::fabs(t.rate(10, t.starts.size() - 1) - 72.0) < 0.01, "neue Logik: 72,00 fps");
    }

    // 3. Pause (2 s ohne Frames). Mit reset: kein kurzer Abstand; ohne reset: höchstens einer.
    for (bool withReset : {true, false}) {
        Sim s;
        const double period = 1.0 / 72;
        for (int i = 0; i < 720; ++i) s.frame(period, 0.003, 0.0003);
        s.now += 2.0;
        if (withReset) s.pacer.reset();
        const size_t from = s.starts.size() - 1;
        for (int i = 0; i < 720; ++i) s.frame(period, 0.003, 0.0003);
        const int gaps = s.shortGaps(from + 1, s.starts.size() - 1, period);
        check(withReset ? gaps == 0 : gaps <= 1, fmt("Pause (reset %.0f): %.0f kurze Abstände", withReset ? 1 : 0, gaps, 0));
        const double r = s.rate(from + 3, s.starts.size() - 1);
        check(std::fabs(r / 72.0 - 1.0) < 1e-3, fmt("nach Pause %.3f fps (%.0f%.0f)", r, 0, 0));
        // Keine Salve: in jeder Sekunde nach der Pause höchstens 73 Frames.
        int maxPerSecond = 0;
        for (size_t i = from + 1; i < s.starts.size(); ++i) {
            int n = 0;
            for (size_t j = i; j < s.starts.size() && s.starts[j] < s.starts[i] + 1.0; ++j) ++n;
            if (n > maxPerSecond) maxPerSecond = n;
        }
        check(maxPerSecond <= 73, fmt("nach Pause höchstens 73 Frames/s, war %.0f (%.0f%.0f)", maxPerSecond, 0, 0));
    }

    // 4. Einzelner langer Frame (3 Perioden): ein Frame sofort hinterher, dann wieder im Takt.
    {
        Sim s;
        const double period = 1.0 / 72;
        for (int i = 0; i < 300; ++i) s.frame(period, 0.002, 0.0002);
        const size_t from = s.starts.size();
        s.frame(period, period * 3, 0.0002);
        for (int i = 0; i < 300; ++i) s.frame(period, 0.002, 0.0002);
        check(s.shortGaps(from, s.starts.size() - 1, period) <= 1, "langer Frame: höchstens ein Frame sofort");
        check(s.pacer.resyncs() == 1, "langer Frame: genau ein Neuaufsetzen");
        check(std::fabs(s.rate(from + 3, s.starts.size() - 1) / 72.0 - 1.0) < 1e-3, "langer Frame: danach 72 fps");
    }

    // 5. fps-Wechsel 70 → 72 (DOSBox-Pure, PrBoom-Menü): danach exakt 72, keine Salve.
    {
        Sim s;
        for (int i = 0; i < 700; ++i) s.frame(1.0 / 70, 0.003, 0.0004);
        check(std::fabs(s.rate(10, s.starts.size() - 1) - 70.0) < 0.02, "vor dem Wechsel 70 fps");
        const size_t from = s.starts.size() - 1;
        for (int i = 0; i < 720; ++i) s.frame(1.0 / 72, 0.003, 0.0004);
        check(std::fabs(s.rate(from + 2, s.starts.size() - 1) - 72.0) < 0.02, "nach dem Wechsel 72 fps");
        check(s.shortGaps(from, s.starts.size() - 1, 1.0 / 72) == 0, "fps-Wechsel ohne Salve");
        // Umgekehrt 72 → 35 (längere Periode): kein Neuaufsetzen nötig.
        const size_t from2 = s.starts.size() - 1;
        for (int i = 0; i < 350; ++i) s.frame(1.0 / 35, 0.003, 0.0004);
        check(std::fabs(s.rate(from2 + 2, s.starts.size() - 1) - 35.0) < 0.02, "Wechsel auf 35 fps");
    }

    // 6. Ungültige Periode fällt auf 60 fps zurück.
    {
        pacing::FramePacer p;
        const double a = p.next(0.0, 0.0);
        check(std::fabs(a - 1.0 / 60) < 1e-12, "Periode 0 → 1/60");
        const double b = p.next(a, -1.0);
        check(std::fabs(b - 2.0 / 60) < 1e-12, "negative Periode → 1/60");
    }

    std::printf("test_frame_pacer: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
