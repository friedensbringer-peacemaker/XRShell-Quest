// Geräte-Test (Quest, arm64): Umrechnung in src/core_audio.cpp (Core-Rate → 48 kHz).
// Ohne echten AAudio-Stream: Ring und Callback werden direkt getrieben.
//
// Fälle: 32 040 Hz (herauf), 48 000 Hz (1:1), 96 000 Hz und 384 000 Hz (herunter,
// sehr hohe Core-Raten) × Erzeuger 1 % schneller / genau / 1 % langsamer.
// Geprüft: Tonhöhe (Nulldurchgänge), Pegel, kein Dauer-Underrun, Latenz ≤ Bremse.

// Standard-Header zuerst: das folgende #define darf nur core_audio.h treffen.
#include <aaudio/AAudio.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#define private public   // Test greift auf Ring/Callback zu (nur hier)
#include "core_audio.h"
#undef private

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int gChecks = 0, gFailures = 0;
void check(bool ok, const char* what, double a = 0, double b = 0) {
    ++gChecks;
    if (!ok && ++gFailures <= 20) std::printf("FEHLER: %s (%.4f / %.4f)\n", what, a, b);
}
}

int main() {
    const double inputs[] = { 32040.0, 48000.0, 96000.0, 384000.0 };
    const double drifts[] = { 1.01, 1.0, 0.99 };
    const double out = 48000.0, tone = 1000.0, seconds = 3.0;

    for (double in : inputs)
    for (double drift : drifts) {
        CoreAudio audio;
        // Zustand wie nach start(), aber ohne Gerät.
        audio.mBuffer.assign(CoreAudio::kCapacity * 2, 0);
        audio.mStream = reinterpret_cast<AAudioStream*>(1);   // nur „läuft“-Kennzeichen
        audio.mOutputRate = static_cast<int32_t>(out);
        audio.mStep.store(in / out);
        audio.mTargetFill.store(static_cast<size_t>(in * 0.08));

        // Erzeuger: Core liefert pro Bild (60 Hz) in·drift/60 Frames Sinus.
        // Verbraucher: Gerät holt alle 4 ms 192 Frames.
        std::vector<int16_t> produced, consumed;
        double phase = 0.0, produceAcc = 0.0;
        const double framesPerTick = in * drift * 0.004;   // Erzeugung je 4-ms-Takt
        int underrunTicks = 0, ticks = 0;
        size_t maxFill = 0;
        for (double t = 0.0; t < seconds; t += 0.004) {
            produceAcc += framesPerTick;
            const size_t n = static_cast<size_t>(produceAcc);
            produceAcc -= static_cast<double>(n);
            produced.resize(n * 2);
            for (size_t i = 0; i < n; ++i) {
                const int16_t sample = static_cast<int16_t>(12000.0 * std::sin(phase));
                produced[i * 2] = sample;
                produced[i * 2 + 1] = sample;
                phase += 2.0 * 3.14159265358979 * tone / in;
            }
            audio.write(produced.data(), n);
            int16_t buf[192 * 2];
            CoreAudio::dataCallback(nullptr, &audio, buf, 192);
            const size_t fill = audio.mWritePos.load() - audio.mReadPos.load();
            if (t > 0.5) {
                maxFill = std::max(maxFill, fill);
                ++ticks;
                if (buf[191 * 2] == 0 && buf[190 * 2] == 0 && buf[100 * 2] == 0) ++underrunTicks;
                consumed.insert(consumed.end(), buf, buf + 192 * 2);
            }
        }
        audio.mStream = nullptr;   // stop() darf kein Gerät schließen

        // Tonhöhe über steigende Nulldurchgänge (linker Kanal), Pegel über Maximum.
        int crossings = 0, peak = 0;
        for (size_t i = 2; i < consumed.size(); i += 2) {
            if (consumed[i - 2] < 0 && consumed[i] >= 0) ++crossings;
            peak = std::max(peak, std::abs(static_cast<int>(consumed[i])));
        }
        const double measuredSeconds = static_cast<double>(consumed.size() / 2) / out;
        const double freq = crossings / measuredSeconds;
        char what[96];
        std::snprintf(what, sizeof(what), "Tonhöhe %.0f Hz @ Drift %.2f", in, drift);
        // Bei zu schnellem Erzeuger springt die Bremse vor → kleine Verzerrung erlaubt.
        check(std::fabs(freq - tone) < tone * 0.03, what, freq, tone);
        std::snprintf(what, sizeof(what), "Pegel %.0f Hz", in);
        check(peak > 12000 * 0.85 && peak <= 12100, what, peak, 12000);
        std::snprintf(what, sizeof(what), "Underruns %.0f Hz @ Drift %.2f", in, drift);
        // Langsamer Erzeuger (0,99) darf gelegentlich leerlaufen, sonst kaum.
        check(underrunTicks <= (drift < 1.0 ? ticks / 20 : 2), what, underrunTicks, ticks);
        std::snprintf(what, sizeof(what), "Latenzbremse %.0f Hz", in);
        check(maxFill <= audio.mTargetFill.load() * 2 + static_cast<size_t>(framesPerTick) + 2, what,
              static_cast<double>(maxFill), static_cast<double>(audio.mTargetFill.load() * 2));
    }
    std::printf("test_audio: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
