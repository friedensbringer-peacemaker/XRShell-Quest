#include "xr_session.h"
#include "refresh_rate.h"

#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>   // nach gl3.h: nutzt dessen GL-Typen
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#define LOG_TAG "XRShell"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define XR_CHECK(res, what) check(res, what)

void XrApp::check(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result)) return;
    // Ohne Instanz liefert xrResultToString nichts — Puffer vorbelegen und
    // den numerischen Code immer mitloggen.
    char buffer[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (sInstanceForErrors != XR_NULL_HANDLE)
        xrResultToString(sInstanceForErrors, result, buffer);
    LOGE("OpenXR-Fehler bei %s: %s (%d)", what, buffer, static_cast<int>(result));
}

XrInstance XrApp::sInstanceForErrors = XR_NULL_HANDLE;

bool XrApp::init(android_app* app, const EglState& egl) {
    mApp = app;
    if (!createInstance(app)) return false;
    if (!createSession(egl)) return false;
    if (!createSwapchains()) return false;
    if (!createScreenSwapchain()) LOGI("Compositor-Ebene nicht verfügbar — Bildschirm im Augenbild");
    if (!createActions()) return false;
    LOGI("XRShell initialisiert");
    return true;
}

bool XrApp::createInstance(android_app* app) {
    // Khronos-Loader auf Android: vor jedem anderen OpenXR-Aufruf mit JavaVM
    // und Activity initialisieren, sonst findet er keine Runtime
    // ("xrInitializeLoaderKHR was not successfully called").
    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(
            XR_NULL_HANDLE, "xrInitializeLoaderKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader))) ||
        !initializeLoader) {
        LOGE("xrInitializeLoaderKHR nicht verfügbar");
        return false;
    }
    XrLoaderInitInfoAndroidKHR loaderInfo = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    loaderInfo.applicationVM = app->activity->vm;
    loaderInfo.applicationContext = app->activity->clazz;
    XrResult res = initializeLoader(
        reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo));
    XR_CHECK(res, "xrInitializeLoaderKHR");
    if (XR_FAILED(res)) return false;

    std::vector<const char*> extensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };
    // Passthrough nur anfordern, wenn die Runtime es anbietet (Quest: ja).
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> available(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, available.data());
    for (const auto& ext : available) {
        if (std::strcmp(ext.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0)
            mPassthroughSupported = true;
        if (std::strcmp(ext.extensionName, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME) == 0)
            mPerfSettingsSupported = true;
        if (std::strcmp(ext.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0)
            mRefreshRateSupported = true;
    }
    if (mPassthroughSupported) extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    // Bildschirm als Compositor-Ebene: gewölbt als Zylinder, Schärfung per Layer-Settings.
    for (const auto& ext : available) {
        if (std::strcmp(ext.extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) == 0)
            mCylinderSupported = true;
        if (std::strcmp(ext.extensionName, XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME) == 0)
            mLayerSettingsSupported = true;
    }
    if (mCylinderSupported) extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    if (mLayerSettingsSupported) extensions.push_back(XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME);
    if (mPerfSettingsSupported) extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    if (mRefreshRateSupported) extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);

    XrInstanceCreateInfoAndroidKHR androidInfo = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    androidInfo.applicationVM = app->activity->vm;
    androidInfo.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo info = { XR_TYPE_INSTANCE_CREATE_INFO };
    info.next = &androidInfo;
    info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::strcpy(info.applicationInfo.applicationName, "XRShell");
    std::strcpy(info.applicationInfo.engineName, "XRShell-M0");
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.enabledExtensionNames = extensions.data();

    XR_CHECK(xrCreateInstance(&info, &mInstance), "xrCreateInstance");
    if (mInstance == XR_NULL_HANDLE) return false;
    sInstanceForErrors = mInstance;   // für aussagekräftige Fehlermeldungen
    // Laufzeit-Name und -Version für den Systemcheck der Ladeanzeige (load_status.h).
    XrInstanceProperties instanceProps = { XR_TYPE_INSTANCE_PROPERTIES };
    if (XR_SUCCEEDED(xrGetInstanceProperties(mInstance, &instanceProps))) {
        char runtime[XR_MAX_RUNTIME_NAME_SIZE + 32];
        std::snprintf(runtime, sizeof(runtime), "%s %u.%u", instanceProps.runtimeName,
                      static_cast<unsigned>(XR_VERSION_MAJOR(instanceProps.runtimeVersion)),
                      static_cast<unsigned>(XR_VERSION_MINOR(instanceProps.runtimeVersion)));
        mRuntimeName = runtime;
        LOGI("OpenXR-Laufzeit: %s", runtime);
    }

    XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(mInstance, &sysInfo, &mSystemId), "xrGetSystem");
    if (mSystemId == XR_NULL_SYSTEM_ID) return false;
    initPassthroughFunctions();
    return true;
}

void XrApp::initPassthroughFunctions() {
    if (!mPassthroughSupported) return;
    XrSystemPassthroughPropertiesFB ptProps = { XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB };
    XrSystemProperties props = { XR_TYPE_SYSTEM_PROPERTIES };
    props.next = &ptProps;
    XR_CHECK(xrGetSystemProperties(mInstance, mSystemId, &props), "Systemeigenschaften");
    mPassthroughSupported = ptProps.supportsPassthrough == XR_TRUE;
    auto load = [&](const char* name, auto& fn) {
        PFN_xrVoidFunction f = nullptr;
        xrGetInstanceProcAddr(mInstance, name, &f);
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(f);
        return fn != nullptr;
    };
    mPassthroughSupported = mPassthroughSupported &&
        load("xrCreatePassthroughFB", mCreatePassthrough) &&
        load("xrDestroyPassthroughFB", mDestroyPassthrough) &&
        load("xrPassthroughStartFB", mStartPassthrough) &&
        load("xrPassthroughPauseFB", mPausePassthrough) &&
        load("xrCreatePassthroughLayerFB", mCreatePassthroughLayer) &&
        load("xrDestroyPassthroughLayerFB", mDestroyPassthroughLayer) &&
        load("xrPassthroughLayerResumeFB", mResumePassthroughLayer) &&
        load("xrPassthroughLayerPauseFB", mPausePassthroughLayer) &&
        load("xrPassthroughLayerSetStyleFB", mSetPassthroughStyle);
    LOGI("Passthrough verfügbar: %s", mPassthroughSupported ? "ja" : "nein");
}

void XrApp::setPassthrough(int percent) {
    if (!mPassthroughSupported || mSession == XR_NULL_HANDLE) return;
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    if (percent == 0) {
        if (mPassthroughRunning) {
            mPausePassthroughLayer(mPassthroughLayer);
            mPausePassthrough(mPassthrough);
            mPassthroughRunning = false;
            LOGI("Passthrough aus");
        }
        mPassthroughPercent = 0;
        return;
    }
    if (!mPassthroughRunning) {
        XrResult res = XR_SUCCESS;
        if (mPassthrough == XR_NULL_HANDLE) {
            XrPassthroughCreateInfoFB info = { XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
            res = mCreatePassthrough(mSession, &info, &mPassthrough);
        }
        if (XR_SUCCEEDED(res) && mPassthroughLayer == XR_NULL_HANDLE) {
            XrPassthroughLayerCreateInfoFB info = { XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
            info.passthrough = mPassthrough;
            info.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
            res = mCreatePassthroughLayer(mSession, &info, &mPassthroughLayer);
        }
        if (XR_SUCCEEDED(res)) res = mStartPassthrough(mPassthrough);
        if (XR_SUCCEEDED(res)) res = mResumePassthroughLayer(mPassthroughLayer);
        XR_CHECK(res, "Passthrough-Start");
        if (XR_FAILED(res)) {
            // Nicht jeden Frame neu versuchen — bis zur nächsten Änderung aus.
            mPassthroughSupported = false;
            destroyPassthrough();
            return;
        }
        mPassthroughRunning = true;
        mPassthroughPercent = -1;
        LOGI("Passthrough an");
    }
    if (percent != mPassthroughPercent) {
        XrPassthroughStyleFB style = { XR_TYPE_PASSTHROUGH_STYLE_FB };
        style.textureOpacityFactor = static_cast<float>(percent) / 100.0f;
        style.edgeColor = { 0.0f, 0.0f, 0.0f, 0.0f };
        XR_CHECK(mSetPassthroughStyle(mPassthroughLayer, &style), "Passthrough-Stil");
        mPassthroughPercent = percent;
    }
}

void XrApp::destroyPassthrough() {
    if (mPassthroughLayer != XR_NULL_HANDLE && mDestroyPassthroughLayer)
        mDestroyPassthroughLayer(mPassthroughLayer);
    if (mPassthrough != XR_NULL_HANDLE && mDestroyPassthrough)
        mDestroyPassthrough(mPassthrough);
    mPassthroughLayer = XR_NULL_HANDLE;
    mPassthrough = XR_NULL_HANDLE;
    mPassthroughRunning = false;
}

bool XrApp::createSession(const EglState& egl) {
    // Pflicht vor xrCreateSession (sonst XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING):
    // GLES-Anforderungen der Runtime abfragen und gegen unseren Kontext prüfen.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getGlesRequirements = nullptr;
    XR_CHECK(xrGetInstanceProcAddr(mInstance, "xrGetOpenGLESGraphicsRequirementsKHR",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&getGlesRequirements)),
             "xrGetOpenGLESGraphicsRequirementsKHR (ProcAddr)");
    if (!getGlesRequirements) return false;
    XrGraphicsRequirementsOpenGLESKHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    XrResult res = getGlesRequirements(mInstance, mSystemId, &req);
    XR_CHECK(res, "xrGetOpenGLESGraphicsRequirementsKHR");
    if (XR_FAILED(res)) return false;
    const XrVersion ours = XR_MAKE_VERSION(3, 0, 0);   // EGL-Kontext: GLES 3.0
    LOGI("Runtime-GLES: min %u.%u, max %u.%u",
         XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported),
         XR_VERSION_MAJOR(req.maxApiVersionSupported), XR_VERSION_MINOR(req.maxApiVersionSupported));
    if (ours < req.minApiVersionSupported) {
        LOGE("GLES 3.0 unter Runtime-Minimum");
        return false;
    }

    XrGraphicsBindingOpenGLESAndroidKHR gfx = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    gfx.display = egl.display;
    gfx.config = egl.config;
    gfx.context = egl.context;

    XrSessionCreateInfo info = { XR_TYPE_SESSION_CREATE_INFO };
    info.next = &gfx;
    info.systemId = mSystemId;

    XR_CHECK(xrCreateSession(mInstance, &info, &mSession), "xrCreateSession");
    if (mSession == XR_NULL_HANDLE) return false;

    XrReferenceSpaceCreateInfo stage = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    stage.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;   // Bildschirm bleibt stehen
    stage.poseInReferenceSpace.orientation.w = 1.0f;
    XR_CHECK(xrCreateReferenceSpace(mSession, &stage, &mStageSpace), "Stage-Space");
    // Inhaltsraum startet deckungsgleich mit STAGE; recenter() setzt ihn neu.
    XR_CHECK(xrCreateReferenceSpace(mSession, &stage, &mContentSpace), "Content-Space");

    XrReferenceSpaceCreateInfo view = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    view.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;   // Kopf, für recenter()
    view.poseInReferenceSpace.orientation.w = 1.0f;
    XR_CHECK(xrCreateReferenceSpace(mSession, &view, &mViewSpace), "View-Space");
    return mStageSpace != XR_NULL_HANDLE && mContentSpace != XR_NULL_HANDLE &&
           mViewSpace != XR_NULL_HANDLE;
}

bool XrApp::createSwapchains() {
    // MSAA-Funktionen (GL_EXT_multisampled_render_to_texture), falls vorhanden.
    constexpr GLsizei kMsaaSamples = 4;
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC msaaFramebufferTexture = nullptr;
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC msaaRenderbufferStorage = nullptr;
    const char* glExts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (glExts && std::strstr(glExts, "GL_EXT_multisampled_render_to_texture")) {
        msaaFramebufferTexture = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC>(
            eglGetProcAddress("glFramebufferTexture2DMultisampleEXT"));
        msaaRenderbufferStorage = reinterpret_cast<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC>(
            eglGetProcAddress("glRenderbufferStorageMultisampleEXT"));
    }
    LOGI("MSAA %s", msaaFramebufferTexture && msaaRenderbufferStorage ? "4x (render-to-texture)"
                                                                      : "nicht verfügbar");
    uint32_t viewCount = 0;
    XR_CHECK(xrEnumerateViewConfigurationViews(mInstance, mSystemId,
              XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr), "Views zählen");
    if (viewCount != 2) { LOGE("Erwarte 2 Views, habe %u", viewCount); return false; }

    XrViewConfigurationView views[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW },
                                         { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    XR_CHECK(xrEnumerateViewConfigurationViews(mInstance, mSystemId,
              XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, views), "Views lesen");

    for (int i = 0; i < 2; ++i) {
        // Supersampling: Die Szene ist nur ein Bildschirm-Quad, daher über der
        // empfohlenen Auflösung rendern (Quest 3: 1680×1760 → ~2350×2460) —
        // der virtuelle Bildschirm wird deutlich schärfer. Begrenzt auf max.
        constexpr float kSupersample = 1.4f;
        const auto scaled = [](uint32_t rec, uint32_t max) {
            const uint32_t s = static_cast<uint32_t>(static_cast<float>(rec) * kSupersample);
            return static_cast<int32_t>(max > 0 && s > max ? max : s);
        };
        mEyes[i].width  = scaled(views[i].recommendedImageRectWidth, views[i].maxImageRectWidth);
        mEyes[i].height = scaled(views[i].recommendedImageRectHeight, views[i].maxImageRectHeight);

        XrSwapchainCreateInfo sc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sc.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        sc.format = GL_RGBA8;   // GLES-Format; gut von Quest unterstützt
        sc.width = static_cast<uint32_t>(mEyes[i].width);
        sc.height = static_cast<uint32_t>(mEyes[i].height);
        sc.faceCount = 1;
        sc.arraySize = 1;
        sc.mipCount = 1;
        sc.sampleCount = 1;

        XR_CHECK(xrCreateSwapchain(mSession, &sc, &mEyes[i].handle), "xrCreateSwapchain");

        uint32_t imgCount = 0;
        XR_CHECK(xrEnumerateSwapchainImages(mEyes[i].handle, 0, &imgCount, nullptr), "Images zählen");
        mEyes[i].images.resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
        XR_CHECK(xrEnumerateSwapchainImages(mEyes[i].handle, imgCount, &imgCount,
                  reinterpret_cast<XrSwapchainImageBaseHeader*>(mEyes[i].images.data())),
                  "Images lesen");

        // Die Runtime liefert Texturnamen — glBindFramebuffer auf eine Textur
        // zeichnet ins Leere (schwarzes Bild im Headset). Eigene FBOs anlegen.
        // 4×-MSAA gegen flimmernde Bildschirmkanten: auf der Quest-GPU (Tiler)
        // löst GL_EXT_multisampled_render_to_texture on-chip auf — fast gratis.
        // Ohne die Erweiterung bleibt es beim einfachen FBO.
        EyeSwapchain& eye = mEyes[i];
        const bool msaa = msaaFramebufferTexture && msaaRenderbufferStorage;
        glGenRenderbuffers(1, &eye.depthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, eye.depthBuffer);
        if (msaa)
            msaaRenderbufferStorage(GL_RENDERBUFFER, kMsaaSamples, GL_DEPTH_COMPONENT24,
                                    eye.width, eye.height);
        else
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, eye.width, eye.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        eye.framebuffers.resize(imgCount, 0);
        glGenFramebuffers(static_cast<GLsizei>(imgCount), eye.framebuffers.data());
        for (uint32_t k = 0; k < imgCount; ++k) {
            glBindFramebuffer(GL_FRAMEBUFFER, eye.framebuffers[k]);
            if (msaa)
                msaaFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       eye.images[k].image, 0, kMsaaSamples);
            else
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                       eye.images[k].image, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      eye.depthBuffer);
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                LOGE("Auge %d, Bild %u: Framebuffer unvollständig (0x%x)", i, k, status);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return false;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        LOGI("Auge %d: %dx%d, %u Bilder, FBOs bereit", i, eye.width, eye.height, imgCount);
    }
    return true;
}

bool XrApp::createScreenSwapchain() {
    // Groß genug für ganzzahlige Vergrößerung aller Retro-Auflösungen:
    // 256×224 ×8, 256×240 ×7, 160×144 ×12, 512×448 ×4, DOS 640×480 ×3.
    EyeSwapchain& sc = mScreenSwapchain;
    XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = GL_RGBA8;
    info.width = 2048;
    info.height = 1792;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    info.sampleCount = 1;
    if (XR_FAILED(xrCreateSwapchain(mSession, &info, &sc.handle)) || sc.handle == XR_NULL_HANDLE) {
        sc.handle = XR_NULL_HANDLE;
        return false;
    }
    sc.width = static_cast<int32_t>(info.width);
    sc.height = static_cast<int32_t>(info.height);
    uint32_t count = 0;
    XR_CHECK(xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr), "Ebene: Bilder zählen");
    sc.images.resize(count, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
    XR_CHECK(xrEnumerateSwapchainImages(sc.handle, count, &count,
              reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())), "Ebene: Bilder lesen");
    sc.framebuffers.resize(count, 0);
    glGenFramebuffers(static_cast<GLsizei>(count), sc.framebuffers.data());
    for (uint32_t k = 0; k < count; ++k) {
        glBindFramebuffer(GL_FRAMEBUFFER, sc.framebuffers[k]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[k].image, 0);
        // Ränder des Ausschnitts nicht über den Bildrand hinaus abtasten lassen.
        glBindTexture(GL_TEXTURE_2D, sc.images[k].image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("Compositor-Ebene: %dx%d, %u Bilder, Zylinder %s, Schärfung %s", sc.width, sc.height, count,
         mCylinderSupported ? "ja" : "nein", mLayerSettingsSupported ? "ja" : "nein");
    return true;
}

bool XrApp::beginScreenLayer() {
    if (!screenLayerAvailable() || !mSessionRunning) return false;
    uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(mScreenSwapchain.handle, nullptr, &index))) return false;
    XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;
    XR_CHECK(xrWaitSwapchainImage(mScreenSwapchain.handle, &wait), "Ebene: Wait");
    glBindFramebuffer(GL_FRAMEBUFFER, mScreenSwapchain.framebuffers[index]);
    return true;
}

void XrApp::endScreenLayer(const ScreenLayer& layer) {
    XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XR_CHECK(xrReleaseSwapchainImage(mScreenSwapchain.handle, &release), "Ebene: Release");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mScreenLayer = layer;
    mScreenLayerReady = true;
}

bool XrApp::createActions() {
    XrActionSetCreateInfo setInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
    std::strcpy(setInfo.actionSetName, "xrshell");
    std::strcpy(setInfo.localizedActionSetName, "XRShell");
    setInfo.priority = 0;
    XR_CHECK(xrCreateActionSet(mInstance, &setInfo, &mActionSet), "ActionSet");

    XrActionCreateInfo aim = { XR_TYPE_ACTION_CREATE_INFO };
    aim.actionType = XR_ACTION_TYPE_POSE_INPUT;
    aim.countSubactionPaths = 0;
    std::strcpy(aim.actionName, "aim_pose");
    std::strcpy(aim.localizedActionName, "Zeiger");
    XR_CHECK(xrCreateAction(mActionSet, &aim, &mAimAction), "Aim-Action");

    XrActionCreateInfo sel = { XR_TYPE_ACTION_CREATE_INFO };
    sel.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(sel.actionName, "select");
    std::strcpy(sel.localizedActionName, "Auswählen");
    XR_CHECK(xrCreateAction(mActionSet, &sel, &mSelectAction), "Select-Action");

    XrActionCreateInfo grip = { XR_TYPE_ACTION_CREATE_INFO };
    grip.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(grip.actionName, "grip");
    std::strcpy(grip.localizedActionName, "Greifen");
    XR_CHECK(xrCreateAction(mActionSet, &grip, &mGripAction), "Grip-Action");

    XrActionCreateInfo bBtn = { XR_TYPE_ACTION_CREATE_INFO };
    bBtn.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(bBtn.actionName, "b_button");
    std::strcpy(bBtn.localizedActionName, "B-Taste");
    XR_CHECK(xrCreateAction(mActionSet, &bBtn, &mBAction), "B-Action");

    XrActionCreateInfo stick = { XR_TYPE_ACTION_CREATE_INFO };
    stick.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    std::strcpy(stick.actionName, "thumbstick");
    std::strcpy(stick.localizedActionName, "Thumbstick");
    XR_CHECK(xrCreateAction(mActionSet, &stick, &mStickAction), "Stick-Action");

    XrActionCreateInfo menu = { XR_TYPE_ACTION_CREATE_INFO };
    menu.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(menu.actionName, "menu_button");
    std::strcpy(menu.localizedActionName, "Menu");
    XR_CHECK(xrCreateAction(mActionSet, &menu, &mMenuAction), "Menu-Action");

    // Linke Hand: Griff gibt die Fensterverstellung per linkem Stick frei
    // (nie der Griff der Zeigerhand — der ist Rechtsklick).
    XrActionCreateInfo lGrip = { XR_TYPE_ACTION_CREATE_INFO };
    lGrip.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(lGrip.actionName, "grip_left");
    std::strcpy(lGrip.localizedActionName, "Griff links");
    XR_CHECK(xrCreateAction(mActionSet, &lGrip, &mLeftGripAction), "Grip-links-Action");

    XrActionCreateInfo lStick = { XR_TYPE_ACTION_CREATE_INFO };
    lStick.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    std::strcpy(lStick.actionName, "thumbstick_left");
    std::strcpy(lStick.localizedActionName, "Thumbstick links");
    XR_CHECK(xrCreateAction(mActionSet, &lStick, &mLeftStickAction), "Stick-links-Action");

    XrActionCreateInfo lClick = { XR_TYPE_ACTION_CREATE_INFO };
    lClick.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(lClick.actionName, "thumbstick_left_click");
    std::strcpy(lClick.localizedActionName, "Thumbstick links drücken");
    XR_CHECK(xrCreateAction(mActionSet, &lClick, &mLeftClickAction), "Stickklick-links-Action");

    XrActionCreateInfo haptic = { XR_TYPE_ACTION_CREATE_INFO };
    haptic.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
    std::strcpy(haptic.actionName, "haptic_right");
    std::strcpy(haptic.localizedActionName, "Vibration rechts");
    XR_CHECK(xrCreateAction(mActionSet, &haptic, &mHapticAction), "Haptik-Action");

    // Frei belegbare Tasten fürs Tastenprofil (xrshell.keys) — ohne Profil wirkungslos.
    auto boolAction = [&](XrAction& action, const char* name, const char* label) {
        XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
        info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        std::strcpy(info.actionName, name);
        std::strcpy(info.localizedActionName, label);
        XR_CHECK(xrCreateAction(mActionSet, &info, &action), label);
    };
    boolAction(mAAction, "a_button", "A-Taste");
    boolAction(mXAction, "x_button", "X-Taste");
    boolAction(mYAction, "y_button", "Y-Taste");
    boolAction(mLeftTriggerAction, "trigger_left", "Trigger links");
    boolAction(mStickClickAction, "thumbstick_click", "Thumbstick drücken");

    // Griffposen beider Hände: Fenster greifen (links) und skalieren (beide).
    auto poseAction = [&](XrAction& action, const char* name, const char* label) {
        XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
        info.actionType = XR_ACTION_TYPE_POSE_INPUT;
        std::strcpy(info.actionName, name);
        std::strcpy(info.localizedActionName, label);
        XR_CHECK(xrCreateAction(mActionSet, &info, &action), label);
    };
    poseAction(mLeftHandPoseAction, "hand_pose_left", "Hand links");
    poseAction(mRightHandPoseAction, "hand_pose_right", "Hand rechts");

    // Bindings für zwei Profile: Khronos-Simple-Controller (Baseline) und
    // Oculus Touch (Quest 2/3 — Grip liegt dort auf Squeeze, Stick/Menu real).
    XrPath simpleProfile, touchProfile;
    xrStringToPath(mInstance, "/interaction_profiles/khr/simple_controller", &simpleProfile);
    xrStringToPath(mInstance, "/interaction_profiles/oculus/touch_controller", &touchProfile);

    auto bind = [&](XrAction action, const char* pathStr) {
        XrPath path;
        xrStringToPath(mInstance, pathStr, &path);
        XrActionSuggestedBinding binding = { action, path };
        return binding;
    };
    auto suggest = [&](XrPath profile, std::initializer_list<XrActionSuggestedBinding> list) {
        XrInteractionProfileSuggestedBinding suggested = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        suggested.interactionProfile = profile;
        suggested.suggestedBindings = list.begin();
        suggested.countSuggestedBindings = static_cast<uint32_t>(list.size());
        XR_CHECK(xrSuggestInteractionProfileBindings(mInstance, &suggested), "Bindings");
    };

    // Simple-Controller: kein Stick und kein B vorhanden — Aim/Select/Grip/Menu nur.
    suggest(simpleProfile, {
        bind(mAimAction,    "/user/hand/right/input/aim/pose"),
        bind(mSelectAction, "/user/hand/right/input/select/click"),
        bind(mGripAction,   "/user/hand/right/input/menu/click"),   // Fallback ohne Squeeze
        bind(mMenuAction,   "/user/hand/left/input/menu/click"),
        bind(mHapticAction, "/user/hand/right/output/haptic"),
    });

    suggest(touchProfile, {
        bind(mAimAction,    "/user/hand/right/input/aim/pose"),
        bind(mSelectAction, "/user/hand/right/input/trigger/value"),
        bind(mGripAction,   "/user/hand/right/input/squeeze/value"),
        bind(mBAction,      "/user/hand/right/input/b/click"),
        bind(mStickAction,  "/user/hand/right/input/thumbstick"),
        bind(mMenuAction,   "/user/hand/left/input/menu/click"),
        bind(mLeftGripAction,  "/user/hand/left/input/squeeze/value"),
        bind(mLeftStickAction, "/user/hand/left/input/thumbstick"),
        bind(mLeftClickAction, "/user/hand/left/input/thumbstick/click"),
        bind(mHapticAction,    "/user/hand/right/output/haptic"),
        bind(mAAction,         "/user/hand/right/input/a/click"),
        bind(mXAction,         "/user/hand/left/input/x/click"),
        bind(mYAction,         "/user/hand/left/input/y/click"),
        bind(mLeftTriggerAction, "/user/hand/left/input/trigger/value"),
        bind(mStickClickAction,  "/user/hand/right/input/thumbstick/click"),
        bind(mLeftHandPoseAction,  "/user/hand/left/input/grip/pose"),
        bind(mRightHandPoseAction, "/user/hand/right/input/grip/pose"),
    });

    XrSessionActionSetsAttachInfo attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach.actionSets = &mActionSet;
    attach.countActionSets = 1;
    XR_CHECK(xrAttachSessionActionSets(mSession, &attach), "Attach ActionSets");

    // Aim-Space: Pose-Action → Raumkoordinaten
    XrSpace poseSpace;
    XrActionSpaceCreateInfo spaceInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
    spaceInfo.action = mAimAction;
    spaceInfo.poseInActionSpace.orientation.w = 1.0f;
    XR_CHECK(xrCreateActionSpace(mSession, &spaceInfo, &poseSpace), "Aim-Space");
    mAimSpace = poseSpace;
    spaceInfo.action = mLeftHandPoseAction;
    XR_CHECK(xrCreateActionSpace(mSession, &spaceInfo, &mLeftHandSpace), "Hand-links-Space");
    spaceInfo.action = mRightHandPoseAction;
    XR_CHECK(xrCreateActionSpace(mSession, &spaceInfo, &mRightHandSpace), "Hand-rechts-Space");
    return true;
}

void XrApp::pollEvents(bool& running) {
    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(mInstance, &event) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            onSessionStateChanged(s->state);
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            running = false;
            break;
        case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
            // Rate wechselt auch ohne uns (Systemmenü, Leistungsgrenze, andere App davor):
            // loggen und beim nächsten matchRefreshRate neu prüfen.
            const auto* r = reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&event);
            LOGI("Bildwiederholrate geändert: %.0f → %.0f Hz", static_cast<double>(r->fromDisplayRefreshRate),
                 static_cast<double>(r->toDisplayRefreshRate));
            mRefreshForFps = -1.0;
            mCurrentRefreshRate = r->toDisplayRefreshRate;
            break;
        }
        default:
            break;
        }
        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

// Emulatoren (DOSBox-Pure mit Windows 3.1) hängen am CPU-Takt: Ohne Vorgabe
// lief die Quest 3 auf CPU-Stufe 4 (1382 MHz). „Sustained high“ ist die höchste
// Dauerstufe ohne Überhitzungsdrosselung; die GPU bleibt beim Standard.
void XrApp::applyPerformanceLevel() {
    if (!mPerfSettingsSupported || mSession == XR_NULL_HANDLE) return;
    PFN_xrPerfSettingsSetPerformanceLevelEXT setLevel = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(mInstance, "xrPerfSettingsSetPerformanceLevelEXT",
            reinterpret_cast<PFN_xrVoidFunction*>(&setLevel))) || !setLevel) return;
    const XrResult res = setLevel(mSession, XR_PERF_SETTINGS_DOMAIN_CPU_EXT,
                                  XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
    XR_CHECK(res, "xrPerfSettingsSetPerformanceLevelEXT(CPU)");
    if (XR_SUCCEEDED(res)) LOGI("CPU-Leistungsstufe: sustained high");
}

// Bildwiederholrate passend zur Core-Bildrate (refresh_rate.h): 72 fps → 72 Hz statt der
// Standardrate (Quest 3: 90 Hz), sonst zeigt das Display jedes vierte Spielbild doppelt.
// Pro Frame aufrufbar; wirkt nur, wenn sich die Core-Bildrate ändert. Passt keine Rate,
// gilt wieder die Rate vom Sitzungsstart.
void XrApp::matchRefreshRate(double coreFps) {
    if (!mRefreshRateSupported || !mSessionRunning || mSession == XR_NULL_HANDLE) return;
    if (std::fabs(coreFps - mRefreshForFps) < 0.01) return;
    mRefreshForFps = coreFps;
    if (!mEnumerateRefreshRates) {
        xrGetInstanceProcAddr(mInstance, "xrEnumerateDisplayRefreshRatesFB",
                              reinterpret_cast<PFN_xrVoidFunction*>(&mEnumerateRefreshRates));
        xrGetInstanceProcAddr(mInstance, "xrGetDisplayRefreshRateFB",
                              reinterpret_cast<PFN_xrVoidFunction*>(&mGetRefreshRate));
        xrGetInstanceProcAddr(mInstance, "xrRequestDisplayRefreshRateFB",
                              reinterpret_cast<PFN_xrVoidFunction*>(&mRequestRefreshRate));
        if (!mEnumerateRefreshRates || !mGetRefreshRate || !mRequestRefreshRate) {
            mRefreshRateSupported = false;
            return;
        }
        mGetRefreshRate(mSession, &mStartRefreshRate);
        mCurrentRefreshRate = mStartRefreshRate;
    }
    if (mRefreshRates.empty() && (mFallbackRates.empty() || ++mRefreshProbe % 8 == 0)) {
        // Direkt nach dem Sitzungsstart meldet die Quest-Runtime (Horizon OS 2026) noch keine
        // Raten (Anzahl 0, kein Fehler) — Anfragen nimmt sie trotzdem an. Dann die bekannten
        // Quest-3-Raten nehmen; bei der nächsten Bildraten-Änderung wird erneut gefragt.
        uint32_t count = 0;
        XR_CHECK(mEnumerateRefreshRates(mSession, 0, &count, nullptr), "xrEnumerateDisplayRefreshRatesFB");
        std::vector<float> rates(count, 0.0f);
        if (count) mEnumerateRefreshRates(mSession, count, &count, rates.data());
        rates.resize(count);
        std::string list;
        for (float r : rates) list += (list.empty() ? "" : ", ") + std::to_string(static_cast<int>(r));
        if (!rates.empty()) {
            mRefreshRates = rates;
            LOGI("Bildwiederholraten: %s Hz (Start %.0f Hz)", list.c_str(), static_cast<double>(mStartRefreshRate));
        } else {
            rates = {72.0f, 80.0f, 90.0f, 120.0f};
            if (mFallbackRates.empty())   // nur beim ersten Mal loggen (wird bei Bildratenwechseln neu gefragt)
                LOGI("Bildwiederholraten: Runtime meldet keine — Quest-3-Standard 72/80/90/120 Hz (Start %.0f Hz)",
                     static_cast<double>(mStartRefreshRate));
        }
        if (!mRefreshRates.empty()) rates = mRefreshRates;
        mFallbackRates = rates;
    }
    float target = refresh::pick(coreFps, mRefreshRates.empty() ? mFallbackRates : mRefreshRates);
    if (target == 0.0f) target = mStartRefreshRate;
    if (target <= 0.0f) return;
    float current = 0.0f;
    if (XR_SUCCEEDED(mGetRefreshRate(mSession, &current))) {
        mCurrentRefreshRate = current;
        if (std::fabs(current - target) < 0.5f) return;
    }
    const XrResult res = mRequestRefreshRate(mSession, target);
    XR_CHECK(res, "xrRequestDisplayRefreshRateFB");
    if (XR_SUCCEEDED(res)) {
        mCurrentRefreshRate = target;
        LOGI("Bildwiederholrate %.0f Hz für Core %.2f fps", static_cast<double>(target), coreFps);
    }
}

void XrApp::onSessionStateChanged(XrSessionState state) {
    const XrSessionState old = mSessionState;
    mSessionState = state;
    switch (state) {
    case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo begin = { XR_TYPE_SESSION_BEGIN_INFO };
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        XR_CHECK(xrBeginSession(mSession, &begin), "xrBeginSession");
        mSessionRunning = true;
        // Neue Session (Brille ab/auf, App zurück): Runtime startet wieder mit ihrer Standardrate
        // → passende Rate für die Core-Bildrate erneut anfordern.
        mRefreshForFps = -1.0;
        LOGI("Session läuft");
        applyPerformanceLevel();
        break;
    }
    case XR_SESSION_STATE_STOPPING:
        xrEndSession(mSession);
        mSessionRunning = false;
        LOGI("Session gestoppt");
        break;
    case XR_SESSION_STATE_LOSS_PENDING:
        if (mSessionRunning) { xrEndSession(mSession); mSessionRunning = false; }
        break;
    default:
        break;
    }
    (void)old;
}

bool XrApp::beginFrame(FrameInput& input) {
    input = FrameInput{};
    if (!mSessionRunning) return false;

    XR_CHECK(xrWaitFrame(mSession, nullptr, &mFrameState), "xrWaitFrame");
    XR_CHECK(xrBeginFrame(mSession, nullptr), "xrBeginFrame");
    input.displayTime = mFrameState.predictedDisplayTime;
    input.sessionReady = true;

    // Controller-Zustand lesen
    XrActiveActionSet active = { mActionSet, XR_NULL_PATH };
    XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
    sync.activeActionSets = &active;
    sync.countActiveActionSets = 1;
    XR_CHECK(xrSyncActions(mSession, &sync), "xrSyncActions");

    XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getInfo.action = mSelectAction;
    XrActionStateBoolean selState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &selState), "Select-State");
    input.triggerPressed = selState.currentState == XR_TRUE;
    input.triggerChanged = selState.changedSinceLastSync == XR_TRUE;

    getInfo.action = mGripAction;
    XrActionStateBoolean gripState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &gripState), "Grip-State");
    input.gripPressed = gripState.currentState == XR_TRUE;
    input.gripChanged = gripState.changedSinceLastSync == XR_TRUE;

    getInfo.action = mBAction;
    XrActionStateBoolean bState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &bState), "B-State");
    input.bPressed = bState.currentState == XR_TRUE;

    getInfo.action = mMenuAction;
    XrActionStateBoolean menuState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &menuState), "Menu-State");
    input.menuPressed = menuState.currentState == XR_TRUE;
    input.menuChanged = menuState.changedSinceLastSync == XR_TRUE;

    getInfo.action = mStickAction;
    XrActionStateVector2f stickState = { XR_TYPE_ACTION_STATE_VECTOR2F };
    XR_CHECK(xrGetActionStateVector2f(mSession, &getInfo, &stickState), "Stick-State");
    if (stickState.isActive == XR_TRUE) {
        input.stickX = stickState.currentState.x;
        input.stickY = stickState.currentState.y;
    }

    getInfo.action = mLeftGripAction;
    XrActionStateBoolean lGripState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &lGripState), "Grip-links-State");
    input.leftGripPressed = lGripState.currentState == XR_TRUE;

    getInfo.action = mLeftStickAction;
    XrActionStateVector2f lStickState = { XR_TYPE_ACTION_STATE_VECTOR2F };
    XR_CHECK(xrGetActionStateVector2f(mSession, &getInfo, &lStickState), "Stick-links-State");
    if (lStickState.isActive == XR_TRUE) {
        input.leftStickX = lStickState.currentState.x;
        input.leftStickY = lStickState.currentState.y;
    }

    getInfo.action = mLeftClickAction;
    XrActionStateBoolean lClickState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &lClickState), "Stickklick-links-State");
    input.leftStickClickPressed = lClickState.currentState == XR_TRUE;
    input.leftStickClickChanged = lClickState.changedSinceLastSync == XR_TRUE;

    auto readBool = [&](XrAction action, const char* what) {
        getInfo.action = action;
        XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XR_CHECK(xrGetActionStateBoolean(mSession, &getInfo, &state), what);
        return state.currentState == XR_TRUE;
    };
    input.aPressed = readBool(mAAction, "A-State");
    input.xPressed = readBool(mXAction, "X-State");
    input.yPressed = readBool(mYAction, "Y-State");
    input.leftTriggerPressed = readBool(mLeftTriggerAction, "Trigger-links-State");
    input.stickClickPressed = readBool(mStickClickAction, "Stickklick-State");

    input.focused = mSessionState == XR_SESSION_STATE_FOCUSED;

    // Aim-Pose im Inhaltsraum lokalisieren (= STAGE, zentriert)
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    XR_CHECK(xrLocateSpace(mAimSpace, mContentSpace, mFrameState.predictedDisplayTime, &loc),
              "Aim locate");
    input.aimFlags = loc.locationFlags;
    if (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
        input.aimPose = loc.pose;
    }
    auto locateHand = [&](XrSpace space, bool& valid, XrVector3f& pos) {
        if (space == XR_NULL_HANDLE) return;
        XrSpaceLocation hand = { XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(xrLocateSpace(space, mContentSpace, mFrameState.predictedDisplayTime, &hand)))
            return;
        valid = (hand.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        if (valid) pos = hand.pose.position;
    };
    locateHand(mLeftHandSpace, input.leftHandValid, input.leftHandPos);
    locateHand(mRightHandSpace, input.rightHandValid, input.rightHandPos);
    locateHand(mViewSpace, input.headValid, input.headPos);
    return true;
}

bool XrApp::recenter(float& headHeight) {
    if (!mSessionRunning) return false;
    XrSpaceLocation head = { XR_TYPE_SPACE_LOCATION };
    if (XR_FAILED(xrLocateSpace(mViewSpace, mStageSpace,
                                mFrameState.predictedDisplayTime, &head)))
        return false;
    const XrSpaceLocationFlags need =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((head.locationFlags & need) != need) return false;

    // Blickrichtung (-Z der Kopfpose) auf die Bodenebene projizieren — nur Yaw.
    const XrQuaternionf& q = head.pose.orientation;
    const float fx = -2 * (q.x * q.z + q.w * q.y);
    const float fz = -(1 - 2 * (q.x * q.x + q.y * q.y));
    if (fx * fx + fz * fz < 1e-4f) return false;   // senkrecht nach oben/unten
    // Drehung um Y, die -Z auf (fx, fz) abbildet: -sinθ = fx, -cosθ = fz.
    const float yaw = std::atan2(-fx, -fz);

    XrReferenceSpaceCreateInfo info = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    info.poseInReferenceSpace.orientation = { 0.0f, std::sin(yaw * 0.5f), 0.0f,
                                              std::cos(yaw * 0.5f) };
    info.poseInReferenceSpace.position = { head.pose.position.x, 0.0f,
                                           head.pose.position.z };
    XrSpace space = XR_NULL_HANDLE;
    XR_CHECK(xrCreateReferenceSpace(mSession, &info, &space), "Content-Space (recenter)");
    if (space == XR_NULL_HANDLE) return false;
    if (mContentSpace != XR_NULL_HANDLE) xrDestroySpace(mContentSpace);
    mContentSpace = space;
    headHeight = head.pose.position.y;
    LOGI("Zentriert: Yaw %.0f°, Kopf %.2f/%.2f/%.2f m", yaw * 57.2958f,
         head.pose.position.x, head.pose.position.y, head.pose.position.z);
    return true;
}

void XrApp::vibrate(float seconds, float amplitude) {
    if (!mSessionRunning || mHapticAction == XR_NULL_HANDLE) return;
    XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
    vibration.duration = static_cast<XrDuration>(seconds * 1e9f);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    vibration.amplitude = amplitude;
    XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
    info.action = mHapticAction;
    XR_CHECK(xrApplyHapticFeedback(mSession, &info,
                                   reinterpret_cast<const XrHapticBaseHeader*>(&vibration)),
             "Haptik");
}

FrameView XrApp::acquireView(int eye) {
    FrameView v{};
    v.width = mEyes[eye].width;
    v.height = mEyes[eye].height;

    // Blickpose für dieses Auge
    XrViewLocateInfo locate = { XR_TYPE_VIEW_LOCATE_INFO };
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = mFrameState.predictedDisplayTime;
    locate.space = mContentSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t count = 0;
    XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    if (XR_SUCCEEDED(xrLocateViews(mSession, &locate, &viewState, 2, &count, views)) &&
        count == 2) {
        v.pose = views[eye].pose;
        v.fov = views[eye].fov;
    }

    uint32_t index = 0;
    XR_CHECK(xrAcquireSwapchainImage(mEyes[eye].handle, nullptr, &index), "Acquire");
    XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;
    XR_CHECK(xrWaitSwapchainImage(mEyes[eye].handle, &wait), "Wait");
    v.imageIndex = index;

    // In das FBO des akquirierten Swapchain-Bildes zeichnen
    glBindFramebuffer(GL_FRAMEBUFFER, mEyes[eye].framebuffers[index]);
    glViewport(0, 0, v.width, v.height);
    return v;
}

void XrApp::releaseView(int eye) {
    XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XR_CHECK(xrReleaseSwapchainImage(mEyes[eye].handle, &release), "Release");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void XrApp::endFrame() {
    XrCompositionLayerProjectionView projViews[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW },
                                                      { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
    for (int i = 0; i < 2; ++i) {
        projViews[i].subImage.swapchain = mEyes[i].handle;
        projViews[i].subImage.imageRect.offset = { 0, 0 };
        projViews[i].subImage.imageRect.extent = { mEyes[i].width, mEyes[i].height };
    }
    // Poses/FoVs passend zum gerenderten Inhalt: erneut lokalisieren (gültig bis displayTime)
    // Für M0 reicht es, die Poses aus acquireView zu nutzen — wir lokalisieren erneut:
    if (mSessionRunning) {
        XrViewLocateInfo locate = { XR_TYPE_VIEW_LOCATE_INFO };
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = mFrameState.predictedDisplayTime;
        locate.space = mContentSpace;
        XrViewState vs = { XR_TYPE_VIEW_STATE };
        XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
        uint32_t count = 0;
        if (XR_SUCCEEDED(xrLocateViews(mSession, &locate, &vs, 2, &count, views)) && count == 2) {
            for (int i = 0; i < 2; ++i) {
                projViews[i].pose = views[i].pose;
                projViews[i].fov = views[i].fov;
            }
        }
    }

    XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layer.space = mContentSpace;
    layer.viewCount = 2;
    layer.views = projViews;

    // Passthrough als Unterlage: Kamerasicht zuerst, unsere Szene mit Alpha
    // darüber (Hintergrund dann transparent, Farben vormultipliziert).
    XrCompositionLayerPassthroughFB ptLayer = { XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB };
    const XrCompositionLayerBaseHeader* layers[3] = {};
    uint32_t layerCount = 0;
    if (mPassthroughRunning) {
        ptLayer.layerHandle = mPassthroughLayer;
        ptLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ptLayer);
        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    }

    // Bildschirm-Ebene zwischen Passthrough und Augenbild (Laser/Menü liegen darüber).
    // Alpha vormultipliziert: „Umgebung ausblenden“ lässt Passthrough durchscheinen.
    XrCompositionLayerSettingsFB settings = { XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB };
    settings.layerFlags = XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB;
    XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrCompositionLayerCylinderKHR cylinder = { XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR };
    if (mScreenLayerReady) {
        const ScreenLayer& s = mScreenLayer;
        XrSwapchainSubImage sub = {};
        sub.swapchain = mScreenSwapchain.handle;
        sub.imageRect.offset = { 0, 0 };
        sub.imageRect.extent = { s.rectW, s.rectH };
        const void* next = mLayerSettingsSupported ? &settings : nullptr;
        if (s.radius > 0.0f && mCylinderSupported) {
            cylinder.next = next;
            cylinder.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            cylinder.space = mContentSpace;
            cylinder.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            cylinder.subImage = sub;
            cylinder.pose = s.pose;
            cylinder.radius = s.radius;
            cylinder.centralAngle = s.width / s.radius;
            cylinder.aspectRatio = s.width / s.height;
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylinder);
        } else {
            quad.next = next;
            quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quad.space = mContentSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage = sub;
            quad.pose = s.pose;
            quad.size = { s.width, s.height };
            layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
        }
        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        mScreenLayerReady = false;
    }
    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
    XrFrameEndInfo end = { XR_TYPE_FRAME_END_INFO };
    end.displayTime = mFrameState.predictedDisplayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = layerCount;
    end.layers = layers;
    XR_CHECK(xrEndFrame(mSession, &end), "xrEndFrame");
}

std::string XrApp::interactionProfile() const {
    if (mInstance == XR_NULL_HANDLE || mSession == XR_NULL_HANDLE) return {};
    XrPath hand = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(mInstance, "/user/hand/right", &hand))) return {};
    XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };
    if (XR_FAILED(xrGetCurrentInteractionProfile(mSession, hand, &state)) ||
        state.interactionProfile == XR_NULL_PATH)
        return {};
    char buf[XR_MAX_PATH_LENGTH];
    uint32_t len = 0;
    if (XR_FAILED(xrPathToString(mInstance, state.interactionProfile, sizeof(buf), &len, buf))) return {};
    return std::string(buf);
}

void XrApp::shutdown() {
    destroyPassthrough();   // vor der Session freigeben
    if (mAimSpace != XR_NULL_HANDLE)   xrDestroySpace(mAimSpace);
    if (mLeftHandSpace != XR_NULL_HANDLE)  xrDestroySpace(mLeftHandSpace);
    if (mRightHandSpace != XR_NULL_HANDLE) xrDestroySpace(mRightHandSpace);
    if (mLeftHandPoseAction != XR_NULL_HANDLE)  xrDestroyAction(mLeftHandPoseAction);
    if (mRightHandPoseAction != XR_NULL_HANDLE) xrDestroyAction(mRightHandPoseAction);
    for (XrAction a : {mAAction, mXAction, mYAction, mLeftTriggerAction, mStickClickAction})
        if (a != XR_NULL_HANDLE) xrDestroyAction(a);
    if (mHapticAction != XR_NULL_HANDLE)    xrDestroyAction(mHapticAction);
    if (mLeftClickAction != XR_NULL_HANDLE) xrDestroyAction(mLeftClickAction);
    if (mLeftStickAction != XR_NULL_HANDLE) xrDestroyAction(mLeftStickAction);
    if (mLeftGripAction != XR_NULL_HANDLE)  xrDestroyAction(mLeftGripAction);
    if (mMenuAction != XR_NULL_HANDLE)  xrDestroyAction(mMenuAction);
    if (mStickAction != XR_NULL_HANDLE) xrDestroyAction(mStickAction);
    if (mBAction != XR_NULL_HANDLE)     xrDestroyAction(mBAction);
    if (mGripAction != XR_NULL_HANDLE)  xrDestroyAction(mGripAction);
    if (mSelectAction != XR_NULL_HANDLE) xrDestroyAction(mSelectAction);
    if (mAimAction != XR_NULL_HANDLE)   xrDestroyAction(mAimAction);
    if (mActionSet != XR_NULL_HANDLE)   xrDestroyActionSet(mActionSet);
    for (auto& eye : mEyes) {
        // FBOs vor der Swapchain freigeben (EGL-Kontext lebt noch).
        if (!eye.framebuffers.empty())
            glDeleteFramebuffers(static_cast<GLsizei>(eye.framebuffers.size()),
                                 eye.framebuffers.data());
        if (eye.depthBuffer) glDeleteRenderbuffers(1, &eye.depthBuffer);
        if (eye.handle != XR_NULL_HANDLE) xrDestroySwapchain(eye.handle);
    }
    if (!mScreenSwapchain.framebuffers.empty())
        glDeleteFramebuffers(static_cast<GLsizei>(mScreenSwapchain.framebuffers.size()),
                             mScreenSwapchain.framebuffers.data());
    if (mScreenSwapchain.handle != XR_NULL_HANDLE) xrDestroySwapchain(mScreenSwapchain.handle);
    if (mViewSpace != XR_NULL_HANDLE)    xrDestroySpace(mViewSpace);
    if (mContentSpace != XR_NULL_HANDLE) xrDestroySpace(mContentSpace);
    if (mStageSpace != XR_NULL_HANDLE) xrDestroySpace(mStageSpace);
    if (mSession != XR_NULL_HANDLE)    xrDestroySession(mSession);
    if (mInstance != XR_NULL_HANDLE)   xrDestroyInstance(mInstance);
    *this = XrApp{};
}
