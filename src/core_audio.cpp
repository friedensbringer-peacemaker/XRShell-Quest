// XRShell — M1b: CoreAudio-Implementierung (AAudio + SPSC-Ring + Umrechnung).

#include "core_audio.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool CoreAudio::start(double inputRate) {
    if (mStream) return true;
    if (inputRate <= 0.0) inputRate = 48000.0;

    mBuffer.assign(kCapacity * 2, 0);
    mReadPos.store(0);
    mWritePos.store(0);
    mFrac = 0.0;
    mOverflows = 0;

    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
        LOGE("AAudio: Builder-Erzeugung fehlgeschlagen");
        return false;
    }
    // Keine Rate vorgeben: native Geräterate (Quest 48 kHz), umgerechnet wird im Callback.
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, &CoreAudio::dataCallback, this);

    if (AAudioStreamBuilder_openStream(builder, &mStream) != AAUDIO_OK || !mStream) {
        LOGE("AAudio: Stream konnte nicht geöffnet werden");
        AAudioStreamBuilder_delete(builder);
        mStream = nullptr;
        return false;
    }
    AAudioStreamBuilder_delete(builder);

    const int32_t outputRate = AAudioStream_getSampleRate(mStream);
    mOutputRate = outputRate > 0 ? outputRate : 48000;
    mStep.store(inputRate / static_cast<double>(mOutputRate));
    mTargetFill = static_cast<size_t>(inputRate * 0.08);   // ~80 ms Vorrat

    if (AAudioStream_requestStart(mStream) != AAUDIO_OK) {
        LOGE("AAudio: Stream-Start fehlgeschlagen");
        AAudioStream_close(mStream);
        mStream = nullptr;
        return false;
    }
    LOGI("AAudio läuft: Core %.0f Hz → Gerät %d Hz (Faktor %.3f), Puffer %d Frames",
         inputRate, outputRate, mStep.load(), AAudioStream_getBufferCapacityInFrames(mStream));
    return true;
}

void CoreAudio::stop() {
    if (!mStream) return;
    AAudioStream_requestStop(mStream);
    AAudioStream_close(mStream);
    mStream = nullptr;
    LOGI("AAudio gestoppt");
}

void CoreAudio::write(const int16_t* samples, size_t frames) {
    if (!mStream || !samples || frames == 0) return;

    const size_t read = mReadPos.load(std::memory_order_acquire);
    const size_t write = mWritePos.load(std::memory_order_relaxed);
    size_t free = kCapacity - (write - read);
    if (free > kCapacity) free = 0;   // defensiv gegen kaputte Counter
    if (frames > free) {
        frames = free;
        ++mOverflows;
        if (mOverflows % 64 == 1)
            LOGI("Audio-Ring voll — Samples verworfen (%u. Überlauf)", mOverflows);
    }
    // SPSC mit Abstand kCapacity: Leser liest [read, write), Schreiber darf
    // bis read + kCapacity schreiben → Volllauf ist legal, kein Kapazitäts-Reserve-Bit nötig.
    for (size_t f = 0; f < frames; ++f) {
        const size_t slot = (write + f) & kMask;
        mBuffer[slot * 2]     = samples[f * 2];
        mBuffer[slot * 2 + 1] = samples[f * 2 + 1];
    }
    mWritePos.store(write + frames, std::memory_order_release);
}

aaudio_data_callback_result_t
CoreAudio::dataCallback(AAudioStream*, void* userData, void* audioData, int32_t numFrames) {
    auto* self = static_cast<CoreAudio*>(userData);
    auto* out = static_cast<int16_t*>(audioData);

    size_t read = self->mReadPos.load(std::memory_order_relaxed);
    const size_t write = self->mWritePos.load(std::memory_order_acquire);
    size_t available = write - read;

    // Latenzbremse: zu viel Vorrat (Core minimal schneller als das Gerät) → vorspringen.
    const size_t target = self->mTargetFill.load(std::memory_order_relaxed);
    if (available > target * 2) {
        read = write - target;
        available = target;
        self->mFrac = 0.0;
    }

    // Ratenregelung: Core und Gerät laufen nie exakt gleich schnell. Statt leerzulaufen
    // (Knackser) wird nach dem geglätteten Füllstand um höchstens ±2 % nachgeregelt —
    // knapp → langsamer lesen, reichlich → schneller. Gleichgewicht liegt nahe am Ziel.
    if (target > 0) {
        const double ratio = (static_cast<double>(available) - static_cast<double>(target)) /
                             static_cast<double>(target);
        self->mFillError += (std::clamp(ratio, -1.0, 1.0) - self->mFillError) * 0.05;
    }
    const double step = self->mStep.load(std::memory_order_relaxed) * (1.0 + kMaxRateAdjust * self->mFillError);
    const float vol = self->mVolume.load(std::memory_order_relaxed);
    const int16_t* buf = self->mBuffer.data();
    auto sample = [&](size_t index, int ch) {
        return static_cast<float>(buf[((read + index) & kMask) * 2 + ch]);
    };

    double pos = self->mFrac;   // Position relativ zu read
    int32_t f = 0;
    for (; f < numFrames; ++f) {
        const double end = pos + step;
        // Für die Interpolation wird ein Frame über das Ende hinaus gebraucht.
        if (static_cast<size_t>(std::ceil(end)) + 1 > available) break;
        float left, right;
        if (step > 1.0) {
            // Herunterrechnen: Mittelwert über die Quell-Frames [pos, end).
            const size_t first = static_cast<size_t>(pos);
            const size_t last = std::max(first + 1, static_cast<size_t>(end));
            float sl = 0.0f, sr = 0.0f;
            for (size_t i = first; i < last; ++i) { sl += sample(i, 0); sr += sample(i, 1); }
            const float n = static_cast<float>(last - first);
            left = sl / n;
            right = sr / n;
        } else {
            // Heraufrechnen: linear zwischen den Nachbarn interpolieren.
            const size_t i = static_cast<size_t>(pos);
            const float t = static_cast<float>(pos - static_cast<double>(i));
            left = sample(i, 0) + (sample(i + 1, 0) - sample(i, 0)) * t;
            right = sample(i, 1) + (sample(i + 1, 1) - sample(i, 1)) * t;
        }
        out[f * 2]     = static_cast<int16_t>(std::clamp(left * vol, -32768.0f, 32767.0f));
        out[f * 2 + 1] = static_cast<int16_t>(std::clamp(right * vol, -32768.0f, 32767.0f));
        pos = end;
    }
    // Verbrauchte ganze Frames freigeben, Nachkommaanteil merken.
    const size_t consumed = static_cast<size_t>(pos);
    self->mFrac = pos - static_cast<double>(consumed);
    self->mReadPos.store(read + consumed, std::memory_order_release);

    // Underrun → Stille am Ende (Core liefert gleich nach).
    for (; f < numFrames; ++f) {
        out[f * 2] = 0;
        out[f * 2 + 1] = 0;
    }
    // Videoaufnahme: fertigen Mix (inkl. Stille bei Pause) weiterreichen.
    if (Tap tap = self->mTap.load(std::memory_order_acquire))
        tap(self->mTapUser.load(std::memory_order_relaxed), out, numFrames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void CoreAudio::setInputRate(double inputRate) {
    if (inputRate <= 0.0 || !mStream) return;
    mStep.store(inputRate / static_cast<double>(mOutputRate));
    mTargetFill = static_cast<size_t>(inputRate * 0.08);
    LOGI("AAudio: Core-Rate jetzt %.0f Hz", inputRate);
}
