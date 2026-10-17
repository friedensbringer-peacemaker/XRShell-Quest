// XRShell — Videoaufnahme des Spielbilds. Siehe video_recorder.h.

#include "video_recorder.h"

#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/native_window.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr int32_t kColorFormatSurface = 0x7F000789;   // MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
constexpr int32_t kAudioRate = 48000;
constexpr int32_t kVideoBitrate = 16000000;            // Pixelgrafik braucht Bitrate gegen Artefakte

using PresentationTimeFn = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLnsecsANDROID);

bool makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') {
            const std::string part = path.substr(0, i);
            if (::mkdir(part.c_str(), 0775) != 0 && errno != EEXIST) return false;
        }
    return true;
}

} // namespace

bool VideoRecorder::start(EGLDisplay display, EGLConfig config, EGLContext context,
                          const std::string& videoDir, int width, int height) {
    if (mActive.load()) return true;
    mWidth = width & ~1;
    mHeight = height & ~1;

    // Datei anlegen: Zeitstempel im Namen, Ordner bei Bedarf erzeugen.
    char name[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof(name), "xr-%Y%m%d-%H%M%S.mp4", std::localtime(&now));
    if (!makeDirs(videoDir)) { LOGE("Video: Ordner nicht anlegbar: %s", videoDir.c_str()); return false; }
    mPath = videoDir + "/" + name;
    mFd = ::open(mPath.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0664);
    if (mFd < 0) { LOGE("Video: Datei nicht anlegbar: %s (%s)", mPath.c_str(), strerror(errno)); return false; }

    // H.264 mit Eingabe-Oberfläche.
    mVideo = AMediaCodec_createEncoderByType("video/avc");
    AMediaFormat* vf = AMediaFormat_new();
    AMediaFormat_setString(vf, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_WIDTH, mWidth);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_HEIGHT, mHeight);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatSurface);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_BIT_RATE, kVideoBitrate);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_FRAME_RATE, 60);
    AMediaFormat_setInt32(vf, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
    const bool videoOk = mVideo &&
        AMediaCodec_configure(mVideo, vf, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
        AMediaCodec_createInputSurface(mVideo, &mWindow) == AMEDIA_OK && mWindow;
    AMediaFormat_delete(vf);
    if (!videoOk) {
        LOGE("Video: H.264-Encoder %dx%d nicht verfügbar", mWidth, mHeight);
        stop();
        return false;
    }

    // AAC (optional — ohne Ton wird nur Bild aufgenommen).
    mAudio = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    AMediaFormat* af = AMediaFormat_new();
    AMediaFormat_setString(af, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_SAMPLE_RATE, kAudioRate);
    AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
    AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_BIT_RATE, 192000);
    AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_AAC_PROFILE, 2);   // AAC-LC
    AMediaFormat_setInt32(af, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);
    if (mAudio && AMediaCodec_configure(mAudio, af, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) != AMEDIA_OK) {
        AMediaCodec_delete(mAudio);
        mAudio = nullptr;
    }
    AMediaFormat_delete(af);
    if (!mAudio) LOGE("Video: AAC-Encoder nicht verfügbar — Aufnahme ohne Ton");

    // EGL-Oberfläche auf der Encoder-Eingabe (Config muss „recordable“ sein).
    mDisplay = display;
    mContext = context;
    const EGLint surfaceAttribs[] = { EGL_NONE };
    mSurface = eglCreateWindowSurface(display, config, mWindow, surfaceAttribs);
    if (mSurface == EGL_NO_SURFACE) {
        LOGE("Video: EGL-Oberfläche für den Encoder fehlgeschlagen (0x%x)", eglGetError());
        stop();
        return false;
    }

    mMuxer = AMediaMuxer_new(mFd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!mMuxer || AMediaCodec_start(mVideo) != AMEDIA_OK ||
        (mAudio && AMediaCodec_start(mAudio) != AMEDIA_OK)) {
        LOGE("Video: Encoder/Muxer-Start fehlgeschlagen");
        stop();
        return false;
    }

    mAudioRead.store(0);
    mAudioWrite.store(0);
    mAudioFramesQueued = 0;
    mVideoTrack = mAudioTrack = -1;
    mMuxerStarted = false;
    mPending.clear();
    mStopRequested.store(false);
    mStart = std::chrono::steady_clock::now();
    mActive.store(true);
    mThread = std::thread([this] { drainLoop(); });
    LOGI("Video: Aufnahme läuft %dx%d → %s", mWidth, mHeight, mPath.c_str());
    return true;
}

int VideoRecorder::seconds() const {
    if (!mActive.load()) return 0;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - mStart).count());
}

bool VideoRecorder::beginFrame() {
    if (!mActive.load() || mSurface == EGL_NO_SURFACE) return false;
    mPrevDraw = eglGetCurrentSurface(EGL_DRAW);
    mPrevRead = eglGetCurrentSurface(EGL_READ);
    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);   // Standard-Framebuffer = Encoder-Oberfläche
    glViewport(0, 0, mWidth, mHeight);
    return true;
}

void VideoRecorder::endFrame() {
    static const auto setTime = reinterpret_cast<PresentationTimeFn>(
        eglGetProcAddress("eglPresentationTimeANDROID"));
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - mStart).count();
    if (setTime) setTime(mDisplay, mSurface, static_cast<EGLnsecsANDROID>(ns));
    eglSwapBuffers(mDisplay, mSurface);
    eglMakeCurrent(mDisplay, mPrevDraw, mPrevRead, mContext);
}

void VideoRecorder::pushAudio(const int16_t* samples, int32_t frames) {
    if (!mActive.load() || !mAudio || frames <= 0) return;
    const size_t read = mAudioRead.load(std::memory_order_acquire);
    const size_t write = mAudioWrite.load(std::memory_order_relaxed);
    const size_t free = kAudioCapacity - (write - read);
    const size_t n = std::min(static_cast<size_t>(frames), free);   // voll → verwerfen
    for (size_t i = 0; i < n; ++i) {
        const size_t slot = (write + i) & (kAudioCapacity - 1);
        mAudioRing[slot * 2] = samples[i * 2];
        mAudioRing[slot * 2 + 1] = samples[i * 2 + 1];
    }
    mAudioWrite.store(write + n, std::memory_order_release);
}

void VideoRecorder::feedAudio(bool endOfStream) {
    while (true) {
        // Erst Puffer holen, wenn genug Ton da ist (~21 ms) — sonst entstünden Leerblöcke.
        const size_t read = mAudioRead.load(std::memory_order_relaxed);
        const size_t available = mAudioWrite.load(std::memory_order_acquire) - read;
        if (available < 1024 && !endOfStream) return;
        const ssize_t index = AMediaCodec_dequeueInputBuffer(mAudio, 0);
        if (index < 0) return;
        size_t capacity = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(mAudio, static_cast<size_t>(index), &capacity);
        const size_t frames = std::min(available, capacity / 4);
        auto* out = reinterpret_cast<int16_t*>(buf);
        for (size_t i = 0; i < frames; ++i) {
            const size_t slot = (read + i) & (kAudioCapacity - 1);
            out[i * 2] = mAudioRing[slot * 2];
            out[i * 2 + 1] = mAudioRing[slot * 2 + 1];
        }
        mAudioRead.store(read + frames, std::memory_order_release);
        const uint64_t pts = static_cast<uint64_t>(mAudioFramesQueued * 1000000 / kAudioRate);
        mAudioFramesQueued += static_cast<int64_t>(frames);
        AMediaCodec_queueInputBuffer(mAudio, static_cast<size_t>(index), 0, frames * 4, pts,
                                     endOfStream ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);
        if (endOfStream || frames < capacity / 4) return;
    }
}

void VideoRecorder::writeSample(bool video, const uint8_t* data, const AMediaCodecBufferInfo& info) {
    std::lock_guard<std::mutex> lock(mMuxMutex);
    if (!mMuxerStarted) {
        Pending p{ video, std::vector<uint8_t>(data + info.offset, data + info.offset + info.size), info };
        p.info.offset = 0;
        mPending.push_back(std::move(p));
        return;
    }
    AMediaMuxer_writeSampleData(mMuxer, static_cast<size_t>(video ? mVideoTrack : mAudioTrack), data, &info);
}

// Ausgabe eines Encoders abholen; liefert true, solange es noch Daten gab.
bool VideoRecorder::drain(AMediaCodec* codec, bool video, bool& eos) {
    AMediaCodecBufferInfo info;
    const ssize_t index = AMediaCodec_dequeueOutputBuffer(codec, &info, 2000);
    if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
        std::lock_guard<std::mutex> lock(mMuxMutex);
        (video ? mVideoTrack : mAudioTrack) = AMediaMuxer_addTrack(mMuxer, format);
        AMediaFormat_delete(format);
        const bool ready = mVideoTrack >= 0 && (!mAudio || mAudioTrack >= 0);
        if (ready && !mMuxerStarted && AMediaMuxer_start(mMuxer) == AMEDIA_OK) {
            mMuxerStarted = true;
            for (const Pending& p : mPending)
                AMediaMuxer_writeSampleData(mMuxer, static_cast<size_t>(p.video ? mVideoTrack : mAudioTrack),
                                            p.data.data(), &p.info);
            mPending.clear();
        }
        return true;
    }
    if (index < 0) return false;   // TRY_AGAIN_LATER oder OUTPUT_BUFFERS_CHANGED
    size_t size = 0;
    uint8_t* data = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), &size);
    const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
    if (data && info.size > 0 && !config) writeSample(video, data, info);
    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) eos = true;
    AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
    return true;
}

void VideoRecorder::drainLoop() {
    bool videoEos = false, audioEos = mAudio == nullptr, eosSignalled = false;
    const auto deadline = [] { return std::chrono::steady_clock::now() + std::chrono::seconds(3); };
    auto stopBy = deadline();
    while (!(videoEos && audioEos)) {
        if (mStopRequested.load() && !eosSignalled) {
            AMediaCodec_signalEndOfInputStream(mVideo);
            if (mAudio) feedAudio(true);
            eosSignalled = true;
            stopBy = deadline();
        }
        if (mAudio && !audioEos && !eosSignalled) feedAudio(false);
        bool busy = drain(mVideo, true, videoEos);
        if (mAudio && !audioEos) busy |= drain(mAudio, false, audioEos);
        if (eosSignalled && std::chrono::steady_clock::now() > stopBy) {
            LOGE("Video: Encoder-Ende nicht rechtzeitig — Datei wird trotzdem abgeschlossen");
            break;
        }
        if (!busy) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void VideoRecorder::stop() {
    if (mThread.joinable()) {
        mStopRequested.store(true);
        mThread.join();
    }
    mActive.store(false);
    if (mMuxer) {
        if (mMuxerStarted) AMediaMuxer_stop(mMuxer);
        AMediaMuxer_delete(mMuxer);
        mMuxer = nullptr;
    }
    if (mVideo) { AMediaCodec_stop(mVideo); AMediaCodec_delete(mVideo); mVideo = nullptr; }
    if (mAudio) { AMediaCodec_stop(mAudio); AMediaCodec_delete(mAudio); mAudio = nullptr; }
    if (mSurface != EGL_NO_SURFACE) { eglDestroySurface(mDisplay, mSurface); mSurface = EGL_NO_SURFACE; }
    if (mWindow) { ANativeWindow_release(mWindow); mWindow = nullptr; }
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
        if (mMuxerStarted) LOGI("Video: gespeichert %s", mPath.c_str());
        else { ::unlink(mPath.c_str()); LOGE("Video: ohne Inhalt verworfen"); }
    }
    mMuxerStarted = false;
    mPending.clear();
}
