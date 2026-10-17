#include "android_glue.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/log.h>
#include <android/native_window.h>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool egl_init(android_app* app, EglState& egl) {
    if (egl.valid()) return true;
    if (app->window == nullptr) return false;

    egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl.display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl.display, &major, &minor)) {
        LOGE("eglInitialize failed"); return false;
    }
    LOGI("EGL %d.%d", major, minor);

    // EGL_RECORDABLE_ANDROID: die Videoaufnahme rendert in eine Encoder-Oberfläche,
    // die zur Config des Kontexts passen muss. Ohne passende Config weiter ohne.
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,   // GLES 3.0 — von Quest 2/3 unterstützt
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_RECORDABLE_ANDROID, EGL_TRUE,
        EGL_NONE,
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(egl.display, configAttribs, &egl.config, 1, &numConfigs) || numConfigs < 1) {
        configAttribs[14] = EGL_NONE;   // ohne „recordable“ erneut (Index von EGL_RECORDABLE_ANDROID)
        if (!eglChooseConfig(egl.display, configAttribs, &egl.config, 1, &numConfigs) ||
            numConfigs < 1) {
            LOGE("eglChooseConfig failed"); return false;
        }
        LOGI("EGL-Config ohne RECORDABLE — Videoaufnahme evtl. nicht möglich");
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    egl.context = eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, contextAttribs);
    if (egl.context == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return false; }

    egl.surface = eglCreateWindowSurface(egl.display, egl.config, app->window, nullptr);
    if (egl.surface == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed"); return false; }

    if (!eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) {
        LOGE("eglMakeCurrent failed"); return false;
    }
    LOGI("EGL bereit (GLES 3.0)");
    return true;
}

void egl_destroy(EglState& egl) {
    if (egl.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl.surface != EGL_NO_SURFACE) eglDestroySurface(egl.display, egl.surface);
        if (egl.context != EGL_NO_CONTEXT) eglDestroyContext(egl.display, egl.context);
        eglTerminate(egl.display);
    }
    egl = EglState{};
}
