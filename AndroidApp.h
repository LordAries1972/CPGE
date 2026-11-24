// ----------------------------------------------------------------------------------------------
// AndroidApp.h -- shared access to the NativeActivity android_app state.
//
// Android-only: on every other platform this header declares nothing. The
// android_app pointer is set once by android_main() (main.cpp) and stays
// valid until the activity is destroyed; FileIO (APK assets), WinSystemAndroid
// (internal-storage path, window size, display density) and Debug (log file
// location, logcat mirror) all read it through here instead of keeping their
// own copies of the activity state.
// ----------------------------------------------------------------------------------------------
#pragma once

// Includes.h first: it defines PLATFORM_ANDROID (via Platform.h), and the
// guard below tests that macro — including it inside the guard meant the
// guard was always false in a TU that had not already included Includes.h,
// so AndroidApp.cpp compiled to an empty file and the linker could not find
// AndroidApp::SetApp / GetApp / GetAssetManager / GetInternalDataPath /
// GetNativeWindow (Vulkan Android build 6).
#include "Includes.h"

#if defined(PLATFORM_ANDROID)

#include <android_native_app_glue.h>   // struct android_app, AAssetManager
#include <string>

namespace AndroidApp
{

// Called once by android_main() with the glue's android_app pointer.
// Must be called before any Get* accessor is used (baseDir, config and the
// asset manager all depend on it).
void SetApp(struct android_app* app);

// The android_app pointer passed to SetApp, or nullptr before android_main runs.
struct android_app* GetApp();

// The app's AAssetManager (APK assets) — nullptr before SetApp.
AAssetManager* GetAssetManager();

// Writable internal-storage directory (activity->internalDataPath) — empty
// before SetApp. This is where GameConfig.cfg, DebugLog.txt and saves live;
// read-only game data comes from the asset manager instead.
std::string GetInternalDataPath();

// The current ANativeWindow, or nullptr before APP_CMD_INIT_WINDOW (and
// again after APP_CMD_TERM_WINDOW).
ANativeWindow* GetNativeWindow();

} // namespace AndroidApp

#endif // PLATFORM_ANDROID
