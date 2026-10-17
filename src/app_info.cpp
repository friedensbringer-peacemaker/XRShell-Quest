// XRShell — App- und Geräteangaben. Siehe app_info.h.

#include "app_info.h"

#include <android/configuration.h>
#include <jni.h>
#include <sys/system_properties.h>

#include <cstdio>
#include <ctime>

namespace {

std::string property(const char* name) {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(name, value);
    return value;
}

std::string jstringToStd(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return out;
}

// PackageManager.getPackageInfo(getPackageName(), 0) → Paket, versionName, versionCode.
void readPackage(ANativeActivity* activity, AppInfo& info) {
    JNIEnv* env = nullptr;
    // Schon angemeldet (z. B. vom OpenXR-Loader)? Dann auch nicht abmelden.
    const bool attached = activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK;
    if (!attached && (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)) return;
    env->PushLocalFrame(16);   // lokale Referenzen am Ende wieder frei (Thread bleibt evtl. angemeldet)
    jobject self = activity->clazz;
    jclass contextClass = env->GetObjectClass(self);
    jmethodID getName = env->GetMethodID(contextClass, "getPackageName", "()Ljava/lang/String;");
    jmethodID getPm = env->GetMethodID(contextClass, "getPackageManager", "()Landroid/content/pm/PackageManager;");
    jstring name = getName ? static_cast<jstring>(env->CallObjectMethod(self, getName)) : nullptr;
    jobject pm = getPm ? env->CallObjectMethod(self, getPm) : nullptr;
    if (!env->ExceptionCheck() && name && pm) {
        info.appId = jstringToStd(env, name);
        jclass pmClass = env->GetObjectClass(pm);
        jmethodID getInfo = env->GetMethodID(pmClass, "getPackageInfo",
                                             "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
        jobject pkg = getInfo ? env->CallObjectMethod(pm, getInfo, name, 0) : nullptr;
        if (!env->ExceptionCheck() && pkg) {
            jclass pkgClass = env->GetObjectClass(pkg);
            jfieldID vName = env->GetFieldID(pkgClass, "versionName", "Ljava/lang/String;");
            jfieldID vCode = env->GetFieldID(pkgClass, "versionCode", "I");
            if (vName) info.versionName = jstringToStd(env, static_cast<jstring>(env->GetObjectField(pkg, vName)));
            if (vCode) info.versionCode = env->GetIntField(pkg, vCode);
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->PopLocalFrame(nullptr);
    if (!attached) activity->vm->DetachCurrentThread();
}

} // namespace

AppInfo readAppInfo(android_app* app) {
    AppInfo info;
    if (!app || !app->activity) return info;
    readPackage(app->activity, info);
    info.device = property("ro.product.manufacturer") + " " + property("ro.product.model");
    info.android = "Android " + property("ro.build.version.release") + ", " + property("ro.build.display.id");
    if (app->config) {
        char lang[2] = {}, country[2] = {};
        AConfiguration_getLanguage(app->config, lang);
        AConfiguration_getCountry(app->config, country);
        if (lang[0]) info.language.assign(lang, 2);
        if (country[0]) info.country.assign(country, 2);
    }
    return info;
}

std::string sessionHeader(const AppInfo& info) {
    const time_t now = time(nullptr);
    tm local {};
    localtime_r(&now, &local);
    char start[32];
    std::strftime(start, sizeof(start), "%Y-%m-%d %H:%M:%S", &local);
    std::string h = "# XRShell-Sitzungslog\n";
    h += "# App: " + (info.appId.empty() ? std::string("?") : info.appId) + "\n";
    h += "# Version: " + (info.versionName.empty() ? std::string("?") : info.versionName) +
         " (versionCode " + std::to_string(info.versionCode) + ")\n";
    h += "# Gerät: " + info.device + ", " + info.android + "\n";
    h += "# Sprache: " + (info.language.empty() ? std::string("?") : info.language) +
         (info.country.empty() ? std::string() : "-" + info.country) + "\n";
    h += std::string("# Start: ") + start + "\n";
    h += "# Zeilen: Uhrzeit +Sekunden seit Start [Stufe] Tag: Text\n";
    return h;
}
