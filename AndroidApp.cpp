// ----------------------------------------------------------------------------------------------
// AndroidApp.cpp -- shared access to the NativeActivity android_app state.
//
// Android-only (compiled by Linux/CMakeLists.txt only when ANDROID). Holds the
// single android_app pointer set by android_main() and hands it out to the
// subsystems that need it (FileIO, WinSystemAndroid, Debug). The pointer is
// owned by the glue/activity, never by this file.
// ----------------------------------------------------------------------------------------------
#include "AndroidApp.h"

#if defined(PLATFORM_ANDROID)

namespace
{
// Set once by android_main(); valid until the activity is destroyed.
struct android_app* s_androidApp = nullptr;
} // namespace

namespace AndroidApp
{

void SetApp(struct android_app* app)
{
    s_androidApp = app;
}

struct android_app* GetApp()
{
    return s_androidApp;
}

AAssetManager* GetAssetManager()
{
    if (!s_androidApp || !s_androidApp->activity)
        return nullptr;
    return s_androidApp->activity->assetManager;
}

std::string GetInternalDataPath()
{
    if (!s_androidApp || !s_androidApp->activity || !s_androidApp->activity->internalDataPath)
        return std::string();
    return std::string(s_androidApp->activity->internalDataPath);
}

ANativeWindow* GetNativeWindow()
{
    if (!s_androidApp)
        return nullptr;
    return s_androidApp->window;
}

} // namespace AndroidApp

#endif // PLATFORM_ANDROID
