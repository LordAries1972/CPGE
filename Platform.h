#pragma once

// One OS identity shared by all CPGE base classes. Android also defines
// __linux__, and every Apple target defines __APPLE__: test the narrower
// targets first so their input, timer and filesystem backends stay correct.
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
#elif defined(__ANDROID__)
    #define PLATFORM_ANDROID
#elif defined(__linux__)
    #define PLATFORM_LINUX
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define PLATFORM_IOS
    #else
        #define PLATFORM_MACOS
        #define PLATFORM_APPLE
    #endif
#else
    #error Unsupported CPGE operating system
#endif
