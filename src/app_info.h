// XRShell — App- und Geräteangaben für Sitzungslog-Kopf und VR-Menü-Kopfzeile.
//
// Version kommt zur Laufzeit aus dem PackageManager (JNI) — sie stimmt damit immer mit dem
// überein, was `dumpsys package` für die installierte APK zeigt. Gerät aus den System-
// Properties, Sprache aus der AConfiguration.

#pragma once

#include <android_native_app_glue.h>

#include <string>

struct AppInfo {
    std::string appId;          // z. B. xr.dosgames (leer = unbekannt)
    std::string versionName;    // versionName laut Manifest (inkl. +Commit)
    long long versionCode = 0;
    std::string device;         // Hersteller Modell
    std::string android;        // Android-Version + Build-ID
    std::string language;       // ISO 639, z. B. „de“
    std::string country;        // ISO 3166, z. B. „DE“

    // „0.2.0+abc1234“ → „0.2.0“ (für die Menü-Kopfzeile).
    std::string shortVersion() const {
        const size_t plus = versionName.find('+');
        return plus == std::string::npos ? versionName : versionName.substr(0, plus);
    }
};

// Liest alles einmal (JNI-Thread wird an- und wieder abgemeldet).
AppInfo readAppInfo(android_app* app);

// Kopfzeilen fürs Sitzungslog („# App: …“ usw.).
std::string sessionHeader(const AppInfo& info);
