// XRShell — M0: OpenXR-Session, Stereorendering, Bildschirm-Quad, Controller-Raycast.
// Plattform: Android (Meta Quest / Android XR), OpenGL ES 3.
//
// Architektur:
//   main.cpp          — android_main, Einstieg, Event-Loop
//   android_glue      — EGL-Context auf dem NativeWindow, Lebenszyklus
//   xr_session        — Instance/Session/Swapchains/Actions, Frame-Loop-Logik
//   quad_renderer     — Texturierter Bildschirm + Laser + Cursor (GLES)

#pragma once

#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>

// Minimaler EGL-Zustand, den die OpenXR-Grafikbindung braucht.
struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig  config  = nullptr;
    bool valid() const {
        return display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE &&
               context != EGL_NO_CONTEXT;
    }
};

// Erstellt EGLDisplay/Config/Context/Surface auf dem App-Fenster.
// Rückgabe false, wenn das Fenster noch nicht existiert (erster INIT_WINDOW noch ausstehend).
bool egl_init(android_app* app, EglState& egl);
void egl_destroy(EglState& egl);
