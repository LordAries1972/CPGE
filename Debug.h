// ----------------------------------------------------------------------------------------------
// Debug Class Implementation with FileIO Integration and Cross-Platform Support
// Compatible with Windows, Linux, macOS, Android, and iOS platforms
// ----------------------------------------------------------------------------------------------
// This is our Debugging Engine and plays a very important role in any Engine System.
// 
// IF YOU ARE A CONTRIBUTOR TO THIS ENGINE, Then PLEASE DO NOT REMOVE any of the debug references
// or functions within this existing class system.  
// 
// If you are to implement a new system of your own, then please add your own 
// debug defines exclusively and include them strictly in this file for your given debugging purposes.  
// 
// As a Contributor, please always be considerate towards other developers and always explain what 
// you are doing throughout your code by utilising this debugging engine, especially when
// error handling needs to be done!  Thank you!
// 
// On Production ready systems, all the below _DEBUG_<Name>_ #defines will be commented out / undefined
// and all output will strictly be written to file (DebugLog.log)
// ----------------------------------------------------------------------------------------------
#pragma once

// Platform-specific includes with conditional compilation for cross-platform support
#if defined(_WIN64) || defined(_WIN32)
#include <windows.h>                                                // Windows-specific debugging and system functions
#elif defined(__linux__)
#include <unistd.h>                                                 // Linux POSIX operations for debugging
#include <sys/stat.h>                                               // Linux file statistics for log file operations
#include <cstring>                                                  // Linux string operations for debugging
#include <cstdio>                                                   // Linux standard I/O for console output
#include <cstdarg>                                                  // Linux variable argument support
#include <iostream>                                                 // Linux stream operations for console output
#elif defined(__APPLE__)
#include <unistd.h>                                                 // macOS POSIX operations for debugging
#include <sys/stat.h>                                               // macOS file statistics for log file operations
#include <cstring>                                                  // macOS string operations for debugging
#include <cstdio>                                                   // macOS standard I/O for console output
#include <cstdarg>                                                  // macOS variable argument support
#include <iostream>                                                 // macOS stream operations for console output
#include <TargetConditionals.h>                                     // macOS target conditional compilation
#elif defined(__ANDROID__)
#include <unistd.h>                                                 // Android POSIX operations for debugging
#include <sys/stat.h>                                               // Android file statistics for log file operations
#include <cstring>                                                  // Android string operations for debugging
#include <cstdio>                                                   // Android standard I/O for console output
#include <cstdarg>                                                  // Android variable argument support
#include <iostream>                                                 // Android stream operations for console output
#include <android/log.h>                                            // Android-specific logging system
#elif defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
#include <unistd.h>                                                 // iOS POSIX operations for debugging
#include <sys/stat.h>                                               // iOS file statistics for log file operations
#include <cstring>                                                  // iOS string operations for debugging
#include <cstdio>                                                   // iOS standard I/O for console output
#include <cstdarg>                                                  // iOS variable argument support
#include <iostream>                                                 // iOS stream operations for console output
#endif

#include <string>                                                       // Cross-platform string support for all platforms
#include <memory>                                                       // Cross-platform memory management for all platforms
#include <sstream>                                                      // Cross-platform string stream operations for all platforms
#include <ctime>                                                        // Cross-platform time operations for all platforms
#include <chrono>                                                       // Cross-platform high-resolution time operations for all platforms

#if defined(_DEBUG)
//#define NO_DEBUGFILE_OUTPUT
//#define _DEBUG_XMPlayer_                                                // Define this line, to show all debug output to runtime console for the XMMODPlayer class.
//#define _DEBUG_CONFIGURATION_
//#define _DEBUG_SOUNDMANAGER_
#define _DEBUG_PUNPACK_                                                 // Define this line, to show all debug output to runtime console for the PUNPack class. 
#define _DEBUG_NETWORKMANAGER_                                          // Define this line, to show all debug output to runtime console for the NetworkManager class.
#define _DEBUG_GAMEPLAYER_                                              // Define this line, to show all debug output to runtime console for the GamePlayer class
#define _DEBUG_GAMINGAI_                                                // Define this line, to show all debug output to runtime console for the GamingAI class.
#define _DEBUG_FILEIO_                                                  // Define this line, to show all debug output to runtime console for the FileIO class.
//#define _DEBUG_SCENEMANAGER_                                            // Define this line, to show all debug output to runtime console for the SceneManager class.
//#define _DEBUG_SCENE_TRANSITION_                                        // Debug Info for Scene Transistions.
//#define _DEBUG_TTSMANAGER_                                              // Define this line, to show all debug output for the TTSManager class.
//#define _DEBUG_GUI_                                                     // Define this line, to show all debug output to runtime console for the GUIManager class.
//#define _DEBUG_WINSYSTEM_                                               // Define this line, to show all debug output to runtime console for the SystemUtils class.
#define _DEBUG_MATHPRECALC_                                             // Define this line, to show all debug output for the MathPrecalculation class
//#define _DEBUG_MOVIEPLAYER_                                             // Define this line for MoviePlayer class.
#define _DEBUG_DEBUG_                                                   // Define this line, to show all debug output for the Debug class itself.

//#define _DUBUG_JOYSTICK_

//#define _DEBUG_FXMANAGER_
//#define _DEBUG_PARTICLEFX_

// ----------------------------------------------------------------------------------------------
// Defines below this line must at least have _DEBUG_RENDERER_ defined
// for proper functionality

// This example here shows that under the renderer system,
// we want to enable debugging in our given pixel shader.
// =====
// NOTE: Currently atm I cannot for some reason work out why I cannot use RenderDOC
// ===== to determine problems with the pixel shader.  So I have to use this 
//       approach instead until I can figure it out!  I think it may have something
//       to do with DirectX 11, but not sure, more investigating is needed.
// ----------------------------------------------------------------------------------------------
//#define _DEBUG_RENDERER_                                                // Define this line, to show all debug output to runtime console for the DX11Renderer class.
//#define _DEBUG_DX12RENDERER_                                              // Define this line, to show all debug output to runtime console for the DX12Renderer class.
#define _DEBUG_PIXSHADER_

//#define _DEBUG_CAMERA_
//#define _DEBUG_MODEL_
//#define _DEBUG_MODEL_RENDERER_
//#define _DEBUG_LIGHTING_
#define _DEBUG_RENDER_WIREFRAME_
#endif

// ----------------------------------------------------------------------------------------------
// OPENGL Debug flags.
// ----------------------------------------------------------------------------------------------
#define _DEBUG_OPENGLRENDERER_                                            // Define this line, to show all debug output for the OpenGLRenderer class.

// ----------------------------------------------------------------------------------------------
// Define MACROS for error handling and reporting with cross-platform support
// ----------------------------------------------------------------------------------------------
#if defined(_WIN64) || defined(_WIN32)
#define THROW_IF_FAILED(hr, msg) \
        if (FAILED(hr)) throw std::runtime_error(std::string("CRITICAL: ") + msg)
#else
    // Cross-platform macro for non-Windows platforms using generic error handling
#define THROW_IF_FAILED(result, msg) \
        if (result != 0) throw std::runtime_error(std::string("CRITICAL: ") + msg)
#endif

const std::wstring LOG_FILE_NAME = L"DebugLog.txt";                                     // Log file name for all platforms

// Used to debug the pixel shader with cross-platform alignment
struct alignas(16) DebugBuffer {
    int debugMode;                                                      // Debug mode flag for pixel shader debugging
    float _pad[3];                                                      // Align to 16 bytes for GPU buffer compatibility across all platforms
};

// This is our debugging levels with cross-platform compatibility
enum class LogLevel : int {
    LOG_INFO,                                                           // Informational messages for general logging
    LOG_DEBUG,                                                          // Debug messages for detailed diagnostic information
    LOG_WARNING,                                                        // Warning messages for potential issues
    LOG_ERROR,                                                          // Error messages for recoverable failures
    LOG_CRITICAL,                                                       // Critical messages for severe failures
    LOG_TERMINATION,                                                    // Termination messages for application shutdown scenarios
};

// ----------------------------------------------------------------------------------------------
// Debug class for logging messages to the output console and file system
// Now integrated with FileIO class for enhanced file operations and thread safety
// Cross-platform compatible for Windows, Linux, macOS, Android, and iOS
// ----------------------------------------------------------------------------------------------
class Debug {
public:
    // Default Logging Level configuration based on build type for all platforms
#if defined(_DEBUG)
    static inline LogLevel currentLogLevel = LogLevel::LOG_INFO;    // Debug builds show all information and above
#else
    // Only LOG Errors and Critical Errors only on Production Releases for performance
    static inline LogLevel currentLogLevel = LogLevel::LOG_WARNING; // Production builds show warnings and above only
#endif

    // Constructor - Initialize Debug system with FileIO integration for all platforms
    Debug();

    // Destructor - Ensure proper cleanup of Debug resources including FileIO for all platforms
    ~Debug();

    // Initialization and cleanup methods for Debug system lifecycle management across all platforms
    bool Initialize();                                                  // Initialize Debug system and FileIO integration
    void Cleanup();                                                     // Clean up Debug resources and FileIO connections
    bool IsInitialized() const { return m_isInitialized; }             // Check if Debug system is properly initialized

    // Core logging methods for different message types and formatting needs across all platforms
    static void Log(const std::string& message);                       // Log basic string message to console and file
    static void logLevelMessage(LogLevel level, const std::wstring& message); // Log wide string message with specific level
    static void logDebugMessage(LogLevel level, const wchar_t* format, ...); // Log formatted message with parameters
    static void DebugLog(const std::string& message);                  // Legacy debug logging method for compatibility
    static void SetLogLevel(LogLevel level);                           // Set minimum logging level for filtering messages

    // Cross-platform error logging method - replaces Windows-specific LOG_IF_FAILED
#if defined(_WIN64) || defined(_WIN32)
    static bool LOG_IF_FAILED(HRESULT hr, const LPCWSTR msg);      // Log Windows HRESULT failures with context
#else
    static bool LOG_IF_FAILED(int result, const char* msg);        // Log generic error results with context for non-Windows platforms
#endif

    // Specialized logging methods for different severity levels across all platforms
    static void LogWarning(const std::string& message);                // Log warning message to console and file
    static void LogError(const std::string& message);                  // Log error message to console and file
    static void LogFunction(const std::string& functionName, const std::string& message); // Log function-specific messages

    // Development and debugging utilities with cross-platform support
    static void DebugBreak();                                          // Trigger debugger break for development debugging

private:
    // Internal state management for Debug system across all platforms
    bool m_isInitialized;                                              // Track initialization status of Debug system
    bool m_hasCleanedUp;                                               // Track cleanup completion status

    // Internal helper methods for file operations and logging with cross-platform support
    void DeleteLogFileOnStartup();                                    // Delete existing log file for session-based logging
    static void WriteToLogFile(const std::wstring& message);          // Write message to log file using FileIO system
    static void Insert_Into_Log_File(const std::wstring& filename, const std::wstring& lineMsg); // Legacy method maintained for compatibility

    // Cross-platform utility methods for string conversion and console output
    static void OutputToConsole(const std::string& message);          // Cross-platform console output method
    static void OutputToConsole(const std::wstring& message);         // Cross-platform wide string console output method
    static std::string WideStringToString(const std::wstring& wstr);  // Cross-platform wide string to string conversion
    static std::wstring StringToWideString(const std::string& str);   // Cross-platform string to wide string conversion
    static std::string GetFormattedTimestamp();                       // Cross-platform formatted timestamp generation
};

// Global Debug instance declaration for engine-wide access across all platforms
extern Debug debug;