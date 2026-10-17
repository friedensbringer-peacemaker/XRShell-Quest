# XRShell — M0-Grundgerüst
# Basis-Layer für Classic-Game-Ports in VR (OpenXR + OpenGL ES, Android/Quest)
#
# Build (Android arm64):
#   1. Android SDK + NDK r25+ installieren, ANDROID_NDK Umgebungsvariable setzen
#   2. CMake mit dem Android-Toolchain konfigurieren:
#        cmake -B build-android \
#          -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#          -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
#          -DCMAKE_BUILD_TYPE=Release
#   3. cmake --build build-android
#   4. APK bauen: cd android && ./gradlew assembleDebug (signiertes Debug-APK)

cmake_minimum_required(VERSION 3.22)
project(xrshell CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- OpenXR-Loader (Khronos, wird beim ersten Build heruntergeladen) ----------
include(FetchContent)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.46
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(openxr)

# --- stb_truetype (gemeinfrei/MIT, nur Header) — Menüschrift aus der System-TTF
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# --- Quellen -------------------------------------------------------------------
add_library(xrshell SHARED
    src/main.cpp
    src/xr_session.cpp
    src/quad_renderer.cpp
    src/android_glue.cpp
    src/libretro_adapter.cpp   # M1: libretro-Core-Loader (DOSBox-Pure)
    src/game_folders.cpp       # Bibliothek: Spielordner (xr.dosgames)
    src/core_audio.cpp         # M1b: AAudio + SPSC-Ring für Core-Audio
    src/keyboard_overlay.cpp   # #5: Tastatur-Overlay (Quad-Grid + Pixelfont)
    src/key_profile.cpp        # Tastenprofil pro Spiel (xrshell.keys)
    src/pad_mapping.cpp        # Tastenbelegung der Konsolen-Cores (pro Spiel, .pad)
    src/vr_menu.cpp            # VR-Menü (Passthrough, Wölbung, Filter, Farbe)
    src/load_screen.cpp        # Ladeanzeige an der Bildstelle (Schritte, Restzeit, Systemcheck, Fehlerseite)
    src/text_raster.cpp        # Menüschrift: System-TTF über stb_truetype
    src/stb_truetype_impl.c    # stb_truetype-Implementierung (Fremdcode)
    src/menu_theme.cpp         # Menü-Optik aus Spielgrafik (xrshell.theme)
    src/video_recorder.cpp     # Videoaufnahme (MediaCodec H.264 + AAC → MP4)
    src/stb_image_impl.c       # stb_image (nur BMP) für die Menü-Optik (Fremdcode)
    src/session_log.cpp        # Sitzungslog auf dem Gerät (files/logs/session-*.log)
    src/app_info.cpp           # Version/Gerät/Sprache (Sitzungslog-Kopf, Menü-Kopfzeile)
)

target_include_directories(xrshell PRIVATE src)
target_include_directories(xrshell SYSTEM PRIVATE "${stb_SOURCE_DIR}")
# Fremdcode (stb-Implementierung) — Warnungen nur dort aus.
set_source_files_properties(src/stb_truetype_impl.c src/stb_image_impl.c PROPERTIES COMPILE_OPTIONS "-w")

# native_app_glue aus dem NDK (Pfad: Variable oder Umgebung)
find_library(android-lib android)
find_library(log-lib log)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(aaudio-lib aaudio)
find_library(mediandk-lib mediandk)   # Videoaufnahme

set(NDK_ROOT "${ANDROID_NDK}")
if(NOT NDK_ROOT)
    set(NDK_ROOT "$ENV{ANDROID_NDK}")
endif()
set(NDK_APP_GLUE_DIR "${NDK_ROOT}/sources/android/native_app_glue")
if(EXISTS "${NDK_APP_GLUE_DIR}/android_native_app_glue.c")
    target_sources(xrshell PRIVATE "${NDK_APP_GLUE_DIR}/android_native_app_glue.c")
    target_include_directories(xrshell PRIVATE "${NDK_APP_GLUE_DIR}")
    # NDK-Fremdcode: unbenutzte Parameter in r30 — nicht unser Code, Warnung dort aus.
    set_source_files_properties("${NDK_APP_GLUE_DIR}/android_native_app_glue.c"
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
endif()

# Sitzungslog: jedes __android_log_print der Bibliothek läuft über
# __wrap___android_log_print (session_log.cpp) — logcat wie bisher, eigene Tags
# (XRShell, XRShell-Core) zusätzlich in die Datei. Kein Quelltext muss seine Makros ändern.
target_link_options(xrshell PRIVATE "-Wl,--wrap=__android_log_print")

target_link_libraries(xrshell
    openxr_loader
    ${android-lib}
    ${log-lib}
    ${egl-lib}
    ${gles-lib}
    ${aaudio-lib}
    ${mediandk-lib}
)

# Warnungen sauber halten — das Gerüst soll als Basis für echte Adapter dienen.
# -Wmissing-field-initializers aus: OpenXR-Idiom ist { XR_TYPE_X, ... } ohne
# ausgeschriebene next-Felder — Warnung wäre reiner Rausch.
target_compile_options(xrshell PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# Android-spezifische OpenXR-Typen aktivieren (XrInstanceCreateInfoAndroidKHR,
# XrGraphicsBindingOpenGLESAndroidKHR, XrSwapchainImageOpenGLESKHR, Extensions)
if(DEFINED ANDROID_ABI)
    target_compile_definitions(xrshell PRIVATE
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
    )
endif()
