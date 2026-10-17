// XRShell — M1b: Core-Audio via AAudio.
//
// Entkopplung Core-Thread ↔ AAudio-Callback über einen SPSC-Ring-Buffer
// (Single-Producer/Single-Consumer, lockfrei via Atomics):
//   · Producer: audio_sample_batch-Callback des Cores (Core-Thread) → write()
//   · Consumer: AAudio-Daten-Callback (AAudio-eigener Thread) → read
// Überlauf: eingehende Frames werden verworfen (logged gedrosselt).
// Underrun: AAudio-Callback füllt mit Stille.
//
// Abtastrate: Der Stream läuft mit der nativen Rate des Geräts (Quest: 48 kHz);
// der Callback rechnet von der Core-Rate um (z. B. 32 040 Hz oder bis
// 384 kHz): herunter per Mittelwert über die Quell-Samples, herauf linear
// interpoliert. Früher wurde die Core-Rate direkt angefordert — Raten, die
// AAudio nicht öffnet, blieben stumm. Latenzbremse: staut sich mehr als
// ~160 ms im Ring, springt der Leser auf ~80 ms zurück (kein Nachhinken).
//
// Lautstärke: fest 1.0, Schnittstelle (setVolume) ist vorgesehen.

#pragma once

#include <aaudio/AAudio.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

class CoreAudio {
public:
    // Öffnet und startet den AAudio-Stream (PCM-16 Stereo, native Geräterate);
    // inputRate = Abtastrate des Cores.
    bool start(double inputRate);
    // Stoppt und schließt den Stream (erst nach Core-Thread-Stop aufrufen).
    void stop();

    // Producer-Seite (Core-Thread): Samples in den Ring kopieren.
    void write(const int16_t* samples, size_t frames);

    // Lautstärke 0.0..1.0 — vorgesehene Schnittstelle, Standard 1.0.
    void setVolume(float v) { mVolume.store(v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)); }

    bool running() const { return mStream != nullptr; }
    // Ausgaberate des Streams (vor start(): 48000 = Quest-Standard) — GET_TARGET_SAMPLE_RATE.
    int32_t outputRate() const { return mOutputRate; }
    // Core meldet zur Laufzeit eine neue Abtastrate (SET_SYSTEM_AV_INFO).
    void setInputRate(double inputRate);
    // Abgriff des fertigen Mixes (48 kHz Stereo) für die Videoaufnahme; nullptr = aus.
    // Wird im AAudio-Callback aufgerufen → muss echtzeitfest sein (kein Lock).
    using Tap = void (*)(void* user, const int16_t* samples, int32_t frames);
    void setTap(Tap tap, void* user) { mTapUser.store(user); mTap.store(tap); }

private:
    static aaudio_data_callback_result_t
    dataCallback(AAudioStream* stream, void* userData, void* audioData,
                 int32_t numFrames);

    // Ring-Größe in Frames (Stereo), Potenz von 2: 65536 ≈ 170 ms @ 384 kHz
    // bzw. 2 s @ 32 kHz (die Latenzbremse hält den Füllstand klein).
    static constexpr size_t kCapacity = 65536;
    static constexpr size_t kMask = kCapacity - 1;

    std::vector<int16_t> mBuffer;              // kCapacity * 2 Samples (Interleaved Stereo)
    std::atomic<size_t> mReadPos{0};           // nur Consumer-Thread
    std::atomic<size_t> mWritePos{0};          // nur Producer-Thread (Core-Thread)
    std::atomic<float>  mVolume{1.0f};
    AAudioStream* mStream = nullptr;
    unsigned mOverflows = 0;                   // Producer-Seite, nur dort zugegriffen
    std::atomic<double> mStep{1.0};            // Quell-Frames pro Ausgabe-Frame
    int32_t mOutputRate = 48000;               // Geräterate des Streams
    double mFillError = 0.0;                   // geglättete Abweichung vom Ziel-Vorrat (Consumer)
    static constexpr double kMaxRateAdjust = 0.02;
    double mFrac = 0.0;                        // Nachkommaanteil der Leseposition (Consumer)
    std::atomic<size_t> mTargetFill{4096};     // ~80 ms Vorrat in Quell-Frames
    std::atomic<Tap> mTap{nullptr};
    std::atomic<void*> mTapUser{nullptr};
};
