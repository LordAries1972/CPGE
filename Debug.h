// ----------------------------------------------------------------------------------------------
// This is our Debugging Engine and plays a very important role in any Engine System.
// 
// IF YOU ARE A CONTRIBUTOR TO THIS ENGINE, Then PLEASE DO NOT REMOVE any of the debug references
// or functions within the existing engine.  
// 
// If you are to implement a new system of your own, then please add your own 
// debug defines exclusively and include them strictly in this file for your given debugging purposes.  
// 
// As a Contributor, please always be considerate towards other developers and always explain what 
// you are doing throughout your code by utilising this debugging engine, especially when
// error handling needs to be done!  Thank you!
// ----------------------------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <string>

#if defined(_DEBUG)
#define NO_DEBUGFILE_OUTPUT
#define _DEBUG_EXCEPTIONHANDLER_                                        // Define this line, to show all debug output for the ExceptionHandler class.
//#define _DEBUG_XMPlayer_                                                // Define this line, to show all debug output to runtime console for the XMMODPlayer class.
//#define _DEBUG_CONFIGURATION_
//#define _DEBUG_SOUNDMANAGER_
//#define _DEBUG_SCENEMANAGER_                                            // Define this line, to show all debug output to runtime console for the SceneManager class.
#define _DEBUG_NETWORKMANAGER_                                          // Define this line, to show all debug output to runtime console for the NetworkManager class.
#define _DEBUG_SCENE_TRANSITION_                                        // Debug Info for Scene Transistions.
//#define _DEBUG_TTSMANAGER_                                              // Define this line, to show all debug output for the TTSManager class.
//#define _DEBUG_GUI_                                                     // Define this line, to show all debug output to runtime console for the GUIManager class.
//#define _DEBUG_WINSYSTEM_                                               // Define this line, to show all debug output to runtime console for the SystemUtils class.
#define _DEBUG_MATHPRECALC_                                             // Define this line, to show all debug output for the MathPrecalculation class
//#define _DEBUG_MOVIEPLAYER_                                             // Define this line for MoviePlayer class.

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
//       approach instead until I can figure it out!
// ----------------------------------------------------------------------------------------------
//#define _DEBUG_RENDERER_                                                    // Define this line, to show all debug output to runtime console for the DX11Renderer class.
//#define _DEBUG_DX12RENDERER_                                                // Define this line, to show all debug output to runtime console for the DX12Renderer class.
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
#define _DEBUG_OPENGLRENDERER_                                          // Define this line, to show all debug output for the OpenGLRenderer class.
 
// ----------------------------------------------------------------------------------------------
// Define MACROS for error handling and reporting
// ----------------------------------------------------------------------------------------------
#define THROW_IF_FAILED(hr, msg) \
    if (FAILED(hr)) throw std::runtime_error(std::string("CRITICAL: ") + msg)

const std::wstring LOG_FILE_NAME = L"DebugLog.txt";                                     // Log file name

// Used to debug the pixel shader
struct alignas(16) DebugBuffer {
    int debugMode;
    float _pad[3]; // Align to 16 bytes
};

// This is our debugging levels
enum class LogLevel : int {
    LOG_INFO,
    LOG_DEBUG,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL,
	LOG_TERMINATION,
};

// ----------------------------------------------------------------------------------------------
// Debug class for logging messages to the output console
// ----------------------------------------------------------------------------------------------
class Debug {
public:
    // Default Logging Level.
    static inline LogLevel currentLogLevel = LogLevel::LOG_INFO;

    // Logs a message to the output console
    static void Log(const std::string& message);
    static void logLevelMessage(LogLevel level, const std::wstring& message);
    static void logDebugMessage(LogLevel level, const wchar_t* format, ...);
    static void DebugLog(const std::string& message);
    static void SetLogLevel(LogLevel level);
    static bool LOG_IF_FAILED(HRESULT hr, const LPCWSTR msg);

    // Inserts a dated log message at the top of a given log file
    static void Insert_Into_Log_File(const std::wstring& filename, const std::wstring& lineMsg);

    // Logs a warning message to the output console
    static void LogWarning(const std::string& message);

    // Logs an error message to the output console
    static void LogError(const std::string& message);

    // Logs the function name and message
    static void LogFunction(const std::string& functionName, const std::string& message);

    // Custom debug break for error handling
    static void DebugBreak();

private:

};

// Do this as this is a singleton class.
extern Debug debug;
