// Host-Test für src/refresh_rate.h (Quest-Bildwiederholrate passend zur Core-Bildrate).
// Gebaut und ausgeführt von tests/run-host-tests.sh.

#include "refresh_rate.h"

#include <cstdio>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;

void check(bool ok, const char* what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FEHLER: %s\n", what);
    }
}

} // namespace

int main() {
    const std::vector<float> quest3 = {72.0f, 80.0f, 90.0f, 120.0f};
    check(refresh::pick(72.0, quest3) == 72.0f, "PrBoom/TyrQuake 72 fps → 72 Hz");
    check(refresh::pick(70.086, quest3) == 72.0f, "DOSBox-Pure VGA 70,09 fps → 72 Hz");
    check(refresh::pick(70.0, quest3) == 72.0f, "ECWolf 70 fps → 72 Hz");
    check(refresh::pick(35.0, quest3) == 72.0f, "35 fps → 72 Hz (2×)");
    check(refresh::pick(90.0, quest3) == 90.0f, "90 fps → 90 Hz");
    check(refresh::pick(60.0, quest3) == 0.0f, "60 fps → keine Rate bis 90 Hz (120 nicht automatisch)");
    check(refresh::pick(60.0, quest3, 120.0f) == 120.0f, "60 fps mit maxRate 120 → 120 Hz");
    check(refresh::pick(59.94, quest3) == 0.0f, "NTSC 59,94 → keine");
    check(refresh::pick(50.0, quest3) == 0.0f, "PAL 50 → keine");
    check(refresh::pick(40.0, quest3) == 80.0f, "40 fps → 80 Hz (2×)");
    check(refresh::pick(0.0, quest3) == 0.0f, "0 fps → keine");
    check(refresh::pick(-5.0, quest3) == 0.0f, "negativ → keine");
    check(refresh::pick(72.0, {}) == 0.0f, "keine Raten → keine");
    check(refresh::pick(72.0, {60.0f, 72.0f, 120.0f}) == 72.0f, "Quest 2 ähnlich → 72");
    // Sweep: das Ergebnis ist immer 0 oder eine angebotene Rate ≤ 90 innerhalb der Toleranz.
    for (int i = 100; i <= 20000; ++i) {
        const double fps = i / 100.0;
        const float r = refresh::pick(fps, quest3);
        bool ok = r == 0.0f;
        for (float q : quest3) ok = ok || (r == q && q <= refresh::kMaxRate);
        if (r > 0.0f) {
            const double k = std::round(r / fps);
            ok = ok && k >= 1.0 && std::fabs(r - k * fps) / r <= refresh::kTolerance + 1e-6;
        }
        check(ok, "Sweep: Ergebnis gültig");
    }
    std::printf("test_refresh_rate: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
