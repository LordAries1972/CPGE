#pragma once

#include "Includes.h"
#include "Debug.h"

// Platform-specific includes for exception handling
#if defined(_WIN32) || defined(_WIN64)
    // Windows-specific exception handling
#ifdef _DEBUG
#pragma comment(lib, "dbghelp.lib")
#include <dbghelp.h>
#include <psapi.h>
#endif
#include <eh.h>
#include <signal.h>
#elif defined(__linux__)
    // Linux-specific exception handling
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cxxabi.h>
#include <dlfcn.h>
#elif defined(__APPLE__)
    // macOS/iOS-specific exception handling
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(__ANDROID__)
    // Android-specific exception handling
#include <unwind.h>
#include <dlfcn.h>
#include <signal.h>
#include <android/log.h>
#include <cxxabi.h>
#endif

// Maximum number of stack frames to capture during exception
const int MAX_STACK_FRAMES = 64;
// Maximum size for symbol name storage
const int MAX_SYMBOL_NAME_LENGTH = 1024;
// Maximum size for module name storage  
const int MAX_MODULE_NAME_LENGTH = 256;
// Number of last function calls to track
const int LAST_CALLS_BUFFER_SIZE = 5;

// Cross-platform structure to hold detailed stack frame information
struct StackFrameInfo {
    uint64_t address;                                   // Memory address of the frame
    char functionName[MAX_SYMBOL_NAME_LENGTH];          // Function name if available
    char moduleName[MAX_MODULE_NAME_LENGTH];            // Module/library name
    uint32_t lineNumber;                                // Source line number if available
    char fileName[512];                                 // Source file name if available
    uint64_t displacement;                              // Offset from symbol start

    // Constructor to initialize all members
    StackFrameInfo() {
        address = 0;
        memset(functionName, 0, sizeof(functionName));
        memset(moduleName, 0, sizeof(moduleName));
        lineNumber = 0;
        memset(fileName, 0, sizeof(fileName));
        displacement = 0;
    }
};

// Cross-platform structure to hold complete exception information
// Cross-platform structure to hold complete exception information (optimized for stack usage)
struct ExceptionDetails {
    uint32_t exceptionCode;                             // Platform-specific exception code
    uint64_t exceptionAddress;                          // Address where exception occurred
    uint32_t threadId;                                  // Thread ID where exception happened
    char exceptionDescription[512];                     // Human-readable description
    int frameCount;                                     // Number of valid frames captured
    uint64_t timeStamp;                                 // When the exception occurred (Unix timestamp)
    uint32_t processId;                                 // Process ID for context

    // Use pointer to heap-allocated stack frames to reduce stack usage
    std::unique_ptr<StackFrameInfo[]> stackFrames;     // Complete stack trace (heap-allocated)

    // Constructor to initialize all members
    ExceptionDetails() {
        exceptionCode = 0;
        exceptionAddress = 0;
        threadId = 0;
        memset(exceptionDescription, 0, sizeof(exceptionDescription));
        frameCount = 0;
        timeStamp = 0;
        processId = 0;
        stackFrames = nullptr; // Will be allocated when needed
    }

    // Method to allocate stack frames on heap
    void AllocateStackFrames(int maxFrames) {
        if (maxFrames > 0) {
            stackFrames = std::make_unique<StackFrameInfo[]>(maxFrames);
        }
    }
};

// Main exception handler class for comprehensive cross-platform exception management
class ExceptionHandler {
public:
    // Constructor - initializes the exception handling system
    ExceptionHandler();

    // Destructor - cleans up resources and restores previous handlers
    ~ExceptionHandler();

    // Initializes the exception handling system and installs platform-specific handlers
    // Returns true if successful, false if initialization failed
    bool Initialize();

    // Cleans up all resources and uninstalls exception handlers
    void Cleanup();

    // Manually logs an exception with full details and stack trace
    // Used for C++ exceptions or manual exception reporting
    void LogException(const std::exception& ex, const char* context = nullptr);

    // Logs a custom exception with user-defined message and context
    void LogCustomException(const char* message, const char* context = nullptr);

    // Records a function call for the last calls buffer (breadcrumb trail)
    // This helps track the last few function calls before an exception
    void RecordFunctionCall(const char* functionName);

    // Gets the current stack trace without an actual exception
    // Useful for debugging or diagnostic purposes
    bool GetCurrentStackTrace(StackFrameInfo* frames, int maxFrames, int& frameCount);

    // Enables or disables automatic crash dump generation
    void SetCrashDumpEnabled(bool enabled) { m_crashDumpEnabled = enabled; }

    // Gets the singleton instance of the exception handler
    static ExceptionHandler& GetInstance();

#if defined(_WIN32) || defined(_WIN64)
    // Windows-specific callback function for SEH (Structured Exception Handling)
    static LONG WINAPI UnhandledExceptionFilter(PEXCEPTION_POINTERS exceptionInfo);
#else
    // Unix-like systems signal handler
    static void SignalHandler(int signal, siginfo_t* info, void* context);
#endif

private:
    // Flag to track if the system has been properly initialized
    bool m_isInitialized;

    // Flag to track if crash dump generation is enabled
    bool m_crashDumpEnabled;

    // Circular buffer to track the last few function calls
    char m_lastCalls[LAST_CALLS_BUFFER_SIZE][256];

    // Current index in the circular buffer
    int m_lastCallsIndex;

    // Mutex for thread-safe access to the last calls buffer
    mutable std::mutex m_lastCallsMutex;

    // Critical section for thread-safe exception handling
    mutable std::mutex m_exceptionMutex;

#if defined(_WIN32) || defined(_WIN64)
    // Windows-specific members
    HANDLE m_processHandle;                             // Handle to the current process for stack walking
    LPTOP_LEVEL_EXCEPTION_FILTER m_previousFilter;     // Previous exception filter to restore on cleanup

#ifdef _DEBUG
    bool m_symbolsInitialized;                      // Symbol handler initialization flag for debug builds
    DWORD64 m_moduleBase;                           // Base address of the main module for symbol resolution
#endif
#else
    // Unix-like systems members
    struct sigaction m_oldSigSegv;                      // Previous SIGSEGV handler
    struct sigaction m_oldSigAbrt;                      // Previous SIGABRT handler
    struct sigaction m_oldSigFpe;                       // Previous SIGFPE handler
    struct sigaction m_oldSigIll;                       // Previous SIGILL handler
    struct sigaction m_oldSigBus;                       // Previous SIGBUS handler
#endif

    // Internal method to capture detailed stack trace information
    bool CaptureStackTrace(void* context, StackFrameInfo* frames, int maxFrames, int& frameCount);

    // Internal method to resolve symbol information for a given address
    bool ResolveSymbolInfo(uint64_t address, StackFrameInfo& frameInfo);

    // Internal method to get module information for a given address
    bool GetModuleInfo(uint64_t address, char* moduleName, size_t moduleNameSize);

    // Internal method to process and log platform-specific exceptions
#if defined(_WIN32) || defined(_WIN64)
    void ProcessSEHException(PEXCEPTION_POINTERS exceptionInfo);
    void GetExceptionDescription(DWORD exceptionCode, char* description, size_t descriptionSize);
#else
    void ProcessSignalException(int signal, siginfo_t* info, void* context);
    void GetSignalDescription(int signal, char* description, size_t descriptionSize);
#endif

    // Internal method to generate crash dump file
    bool GenerateCrashDump(void* exceptionInfo);

    // Internal method to create hex dump for release builds
    void CreateHexDump(uint64_t address, int size, std::string& hexDump);

    // Internal method to get basic stack information for release builds
    bool GetBasicStackTrace(void* context, uint64_t* addresses, int maxFrames, int& frameCount);

    // Internal method to log the last few function calls for context
    void LogLastFunctionCalls();

    // Internal utility methods for cross-platform compatibility
    uint64_t GetCurrentTimestamp();
    uint32_t GetCurrentThreadId();
    uint32_t GetCurrentProcessId();
    void ConvertWideToNarrow(const wchar_t* wide, char* narrow, size_t narrowSize);
    void ConvertNarrowToWide(const char* narrow, wchar_t* wide, size_t wideSize);

    // Prevent copying of the exception handler (singleton pattern)
    ExceptionHandler(const ExceptionHandler&) = delete;
    ExceptionHandler& operator=(const ExceptionHandler&) = delete;
};

// Global instance declaration
extern ExceptionHandler exceptionHandler;

// Convenience macro for recording function calls in debug builds
#ifdef _DEBUG
#define RECORD_FUNCTION_CALL() exceptionHandler.RecordFunctionCall(__FUNCTION__)
#else
#define RECORD_FUNCTION_CALL() // No-op in release builds
#endif

// Convenience macro for logging exceptions with context
#define LOG_EXCEPTION(ex, context) exceptionHandler.LogException(ex, context)

// Convenience macro for logging custom exceptions
#define LOG_CUSTOM_EXCEPTION(msg, context) exceptionHandler.LogCustomException(msg, context)
