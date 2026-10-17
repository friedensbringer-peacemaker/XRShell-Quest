// XRShell — Videoaufnahme des Spielbilds (MP4: H.264 + AAC) aus dem VR-Menü.
//
// Die Systemaufnahme der Quest lässt sich aus einer App nicht steuern, deshalb
// nimmt XRShell selbst auf:
//   · Bild: das gefilterte Spielbild wird (wie die Compositor-Ebene) in eine
//     EGL-Oberfläche des H.264-Encoders gerendert — ganzzahlig vergrößert, GPU-seitig.
//   · Ton: der fertige 48-kHz-Mix aus dem AAudio-Callback (lockfreier Ring) → AAC.
//   · Datei: /sdcard/Movies/<Paket>/xr-<Datum>-<Zeit>.mp4 (in Galerie und Dateien
//     sichtbar), sonst <externalDataPath>/videos/.
// Ein eigener Thread leert beide Encoder in den Muxer; beginFrame/endFrame laufen
// auf dem Render-Thread (EGL-Kontext), pushAudio auf dem Audio-Thread.

#pragma once

#include <EGL/egl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ANativeWindow;

class VideoRecorder {
public:
    ~VideoRecorder() { stop(); }

    // Startet die Aufnahme in videoDir (wird angelegt). width/height gerade.
    bool start(EGLDisplay display, EGLConfig config, EGLContext context,
               const std::string& videoDir, int width, int height);
    // Beendet die Aufnahme und schließt die Datei sauber ab (blockiert kurz).
    void stop();

    bool active() const { return mActive.load(); }
    int seconds() const;                      // Dauer der laufenden Aufnahme
    const std::string& path() const { return mPath; }
    int width() const { return mWidth; }
    int height() const { return mHeight; }

    // Render-Thread: Encoder-Oberfläche aktivieren (Viewport gesetzt), zeichnen,
    // dann endFrame() — stellt die vorherige Oberfläche wieder her.
    bool beginFrame();
    void endFrame();

    // Audio-Thread (AAudio-Callback): fertiger Mix, 48 kHz Stereo PCM-16.
    void pushAudio(const int16_t* samples, int32_t frames);

private:
    void drainLoop();
    bool drain(AMediaCodec* codec, bool video, bool& eos);
    void feedAudio(bool endOfStream);
    void writeSample(bool video, const uint8_t* data, const AMediaCodecBufferInfo& info);

    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLContext mContext = EGL_NO_CONTEXT;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLSurface mPrevDraw = EGL_NO_SURFACE, mPrevRead = EGL_NO_SURFACE;
    ANativeWindow* mWindow = nullptr;
    AMediaCodec* mVideo = nullptr;
    AMediaCodec* mAudio = nullptr;
    AMediaMuxer* mMuxer = nullptr;
    int mFd = -1;
    std::string mPath;
    int mWidth = 0, mHeight = 0;

    std::thread mThread;
    std::atomic<bool> mActive{false}, mStopRequested{false};
    std::chrono::steady_clock::time_point mStart;

    // Muxer erst starten, wenn alle Spuren ihr Format kennen; Daten davor puffern.
    std::mutex mMuxMutex;
    ssize_t mVideoTrack = -1, mAudioTrack = -1;
    bool mMuxerStarted = false;
    struct Pending { bool video; std::vector<uint8_t> data; AMediaCodecBufferInfo info; };
    std::vector<Pending> mPending;

    // Audio-Ring (SPSC): Audio-Thread schreibt, Encoder-Thread liest. 2 s Puffer.
    static constexpr size_t kAudioCapacity = 1 << 17;   // Frames (Potenz von 2)
    std::vector<int16_t> mAudioRing = std::vector<int16_t>(kAudioCapacity * 2);
    std::atomic<size_t> mAudioRead{0}, mAudioWrite{0};
    int64_t mAudioFramesQueued = 0;                     // nur Encoder-Thread
};
