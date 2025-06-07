#include "Includes.h"
#include "ExceptionHandler.h"
#include "Debug.h"

// Suppress Microsoft-specific security warnings for cross-platform compatibility
#if defined(_WIN32) || defined(_WIN64)
#pragma warning(push)
#pragma warning(disable: 4996) // Disable 'strncpy' unsafe function warnings
#endif

// External debug reference
extern Debug debug;

// Cross-platform safe string copy function
static void SafeStringCopy(char* dest, const char* src, size_t destSize) {
    if (!dest || !src || destSize == 0) {
        return;
    }

    #if defined(_WIN32) || defined(_WIN64)
        // Use secure function on Windows
        strcpy_s(dest, destSize, src);
    #else
        // Use standard function on other platforms with manual null termination
        strncpy(dest, src, destSize - 1);
        dest[destSize - 1] = '\0';
    #endif
}

// Cross-platform safe string copy with length limit
static void SafeStringCopyN(char* dest, const char* src, size_t destSize, size_t maxCopy) {
    if (!dest || !src || destSize == 0) {
        return;
    }

    size_t copyLen = (maxCopy < destSize - 1) ? maxCopy : destSize - 1;

    #if defined(_WIN32) || defined(_WIN64)
        // Use secure function on Windows
        strncpy_s(dest, destSize, src, copyLen);
    #else
        // Use standard function on other platforms with manual null termination
        strncpy(dest, src, copyLen);
        dest[copyLen] = '\0';
    #endif
}

// Constructor - initializes member variables to safe defaults
ExceptionHandler::ExceptionHandler() :
    m_isInitialized(false),                     // Not initialized yet
    m_crashDumpEnabled(true),                   // Enable crash dumps by default
    m_lastCallsIndex(0)                         // Start at beginning of circular buffer
{
    // Initialize the last calls buffer to empty strings
    for (int i = 0; i < LAST_CALLS_BUFFER_SIZE; ++i) {
        memset(m_lastCalls[i], 0, sizeof(m_lastCalls[i]));
    }

    #if defined(_WIN32) || defined(_WIN64)
        // Initialize Windows-specific members
        m_processHandle = nullptr;
        m_previousFilter = nullptr;

        #ifdef _DEBUG
            m_symbolsInitialized = false;
            m_moduleBase = 0;
        #endif
    #else
        // Initialize Unix-like system members
        memset(&m_oldSigSegv, 0, sizeof(m_oldSigSegv));
        memset(&m_oldSigAbrt, 0, sizeof(m_oldSigAbrt));
        memset(&m_oldSigFpe, 0, sizeof(m_oldSigFpe));
        memset(&m_oldSigIll, 0, sizeof(m_oldSigIll));
        memset(&m_oldSigBus, 0, sizeof(m_oldSigBus));
    #endif

    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Constructor called - ready for initialization");
    #endif
}

// Destructor - ensures proper cleanup of all resources
ExceptionHandler::~ExceptionHandler() {
    // Always cleanup when the object is destroyed
    Cleanup();

    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Destructor called - cleanup completed");
    #endif
}

// Initializes the complete exception handling system
bool ExceptionHandler::Initialize() {
    // Prevent double initialization
    if (m_isInitialized) {
        #if defined(_DEBUG_EXCEPTIONHANDLER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ExceptionHandler] Already initialized - skipping");
        #endif
        return true;
    }

    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Starting initialization process");
    #endif

    #if defined(_WIN32) || defined(_WIN64)
        // Windows-specific initialization
        m_processHandle = GetCurrentProcess();
        if (!m_processHandle) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to get current process handle. Error: %d", GetLastError());
            return false;
        }

        #ifdef _DEBUG
            // Initialize symbol handler for debug builds only
            if (!SymInitialize(m_processHandle, nullptr, TRUE)) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ExceptionHandler] SymInitialize failed. Error: %d", GetLastError());
                m_symbolsInitialized = false;
            }
            else {
                m_symbolsInitialized = true;

                // Set symbol options for better debugging information
                DWORD symOptions = SymGetOptions();
                symOptions |= SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_INCLUDE_32BIT_MODULES;
                SymSetOptions(symOptions);

                // Get the base address of our main module
                HMODULE hModule = GetModuleHandle(nullptr);
                if (hModule) {
                    m_moduleBase = (DWORD64)hModule;
                    #if defined(_DEBUG_EXCEPTIONHANDLER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Symbol handler initialized. Module base: 0x%llX", m_moduleBase);
                    #endif
                }
            }
        #endif

    // Install our custom unhandled exception filter
    m_previousFilter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    if (!m_previousFilter) {
    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] No previous exception filter was installed");
    #endif
    }
    else {
    #if defined(_DEBUG_EXCEPTIONHANDLER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Previous exception filter saved and replaced");
    #endif
    }
#else
    // Unix-like systems initialization (Linux, macOS, iOS, Android)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = SignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    // Install signal handlers for common crash signals
    if (sigaction(SIGSEGV, &sa, &m_oldSigSegv) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to install SIGSEGV handler");
        return false;
    }

    if (sigaction(SIGABRT, &sa, &m_oldSigAbrt) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to install SIGABRT handler");
        return false;
    }

    if (sigaction(SIGFPE, &sa, &m_oldSigFpe) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to install SIGFPE handler");
        return false;
    }

    if (sigaction(SIGILL, &sa, &m_oldSigIll) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to install SIGILL handler");
        return false;
    }

    if (sigaction(SIGBUS, &sa, &m_oldSigBus) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to install SIGBUS handler");
        return false;
    }

    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Signal handlers installed successfully");
    #endif
#endif

    // Mark as successfully initialized
    m_isInitialized = true;

    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Initialization completed successfully");
    #endif

    return true;
}

// Cleans up all resources and restores previous exception handlers
void ExceptionHandler::Cleanup() {
    // Skip cleanup if not initialized
    if (!m_isInitialized) {
        return;
    }

    #if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Starting cleanup process");
    #endif

#if defined(_WIN32) || defined(_WIN64)
    // Windows-specific cleanup
    if (m_previousFilter) {
        SetUnhandledExceptionFilter(m_previousFilter);
        m_previousFilter = nullptr;
#if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Previous exception filter restored");
#endif
    }
    else {
        SetUnhandledExceptionFilter(nullptr);
#if defined(_DEBUG_EXCEPTIONHANDLER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Exception filter removed");
#endif
    }

#ifdef _DEBUG
    if (m_symbolsInitialized && m_processHandle) {
        if (SymCleanup(m_processHandle)) {
#if defined(_DEBUG_EXCEPTIONHANDLER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Symbol handler cleaned up successfully");
#endif
        }
        else {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ExceptionHandler] SymCleanup failed. Error: %d", GetLastError());
        }
        m_symbolsInitialized = false;
    }
#endif

    m_processHandle = nullptr;
    m_moduleBase = 0;
#else
    // Unix-like systems cleanup
    sigaction(SIGSEGV, &m_oldSigSegv, nullptr);
    sigaction(SIGABRT, &m_oldSigAbrt, nullptr);
    sigaction(SIGFPE, &m_oldSigFpe, nullptr);
    sigaction(SIGILL, &m_oldSigIll, nullptr);
    sigaction(SIGBUS, &m_oldSigBus, nullptr);

#if defined(_DEBUG_EXCEPTIONHANDLER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Signal handlers restored");
#endif
#endif

    // Reset all member variables to safe defaults
    m_isInitialized = false;

    // Clear the last calls buffer
    std::lock_guard<std::mutex> lock(m_lastCallsMutex);
    for (int i = 0; i < LAST_CALLS_BUFFER_SIZE; ++i) {
        memset(m_lastCalls[i], 0, sizeof(m_lastCalls[i]));
    }
    m_lastCallsIndex = 0;

#if defined(_DEBUG_EXCEPTIONHANDLER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Cleanup completed successfully");
#endif
}

// Logs detailed information about a C++ exception
void ExceptionHandler::LogException(const std::exception& ex, const char* context) {
    // Thread-safe exception logging
    std::lock_guard<std::mutex> lock(m_exceptionMutex);

    // Get exception message
    const char* message = ex.what();

    // Log the basic exception information
    if (context) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] C++ Exception in context '%hs': %hs", context, message);
    }
    else {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] C++ Exception: %hs", message);
    }

    // Allocate stack frames on heap to avoid stack overflow
    std::unique_ptr<StackFrameInfo[]> frames(new StackFrameInfo[MAX_STACK_FRAMES]);
    int frameCount = 0;

    if (GetCurrentStackTrace(frames.get(), MAX_STACK_FRAMES, frameCount)) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Stack trace captured (%d frames):", frameCount);

        // Log each stack frame with detailed information
        int framesToLog = std::min(frameCount, 10); // Limit to first 10 frames for readability
        for (int i = 0; i < framesToLog; ++i) {
            if (strlen(frames[i].functionName) > 0) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: %hs [%hs+0x%llX]",
                    i, frames[i].functionName, frames[i].moduleName, frames[i].displacement);
            }
            else {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: 0x%llX [%hs]",
                    i, frames[i].address, frames[i].moduleName);
            }
        }
    }

    // Log the last few function calls for context
    LogLastFunctionCalls();
}

// Logs a custom exception with user-defined message
void ExceptionHandler::LogCustomException(const char* message, const char* context) {
    // Thread-safe custom exception logging
    std::lock_guard<std::mutex> lock(m_exceptionMutex);

    // Log the custom exception message
    if (context) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Custom Exception in context '%hs': %hs", context, message);
    }
    else {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Custom Exception: %hs", message);
    }

    // Allocate stack frames on heap to avoid stack overflow
    std::unique_ptr<StackFrameInfo[]> frames(new StackFrameInfo[MAX_STACK_FRAMES]);
    int frameCount = 0;

    if (GetCurrentStackTrace(frames.get(), MAX_STACK_FRAMES, frameCount)) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Stack trace captured (%d frames):", frameCount);

        // Log each stack frame with detailed information
        int framesToLog = std::min(frameCount, 10); // Limit to first 10 frames for readability
        for (int i = 0; i < framesToLog; ++i) {
            if (strlen(frames[i].functionName) > 0) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: %hs [%hs+0x%llX]",
                    i, frames[i].functionName, frames[i].moduleName, frames[i].displacement);
            }
            else {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: 0x%llX [%hs]",
                    i, frames[i].address, frames[i].moduleName);
            }
        }
    }

    // Log the last few function calls for context
    LogLastFunctionCalls();
}

// Records a function call in the circular buffer for breadcrumb tracking
void ExceptionHandler::RecordFunctionCall(const char* functionName) {
    // Thread-safe function call recording
    std::lock_guard<std::mutex> lock(m_lastCallsMutex);

    // Validate input parameter
    if (!functionName || strlen(functionName) == 0) {
        return;
    }

    // Copy function name to current buffer position (with bounds checking)
    SafeStringCopyN(m_lastCalls[m_lastCallsIndex], functionName, 256, 255);

    // Move to next position in circular buffer
    m_lastCallsIndex = (m_lastCallsIndex + 1) % LAST_CALLS_BUFFER_SIZE;

#if defined(_DEBUG_EXCEPTIONHANDLER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ExceptionHandler] Recorded function call: %hs", functionName);
#endif
}

// Gets the current stack trace without requiring an exception
bool ExceptionHandler::GetCurrentStackTrace(StackFrameInfo* frames, int maxFrames, int& frameCount) {
    // Validate input parameters
    if (!frames || maxFrames <= 0) {
        frameCount = 0;
        return false;
    }

    frameCount = 0;

#if defined(_WIN32) || defined(_WIN64)
    // Windows stack trace capture
    CONTEXT context;
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL;
    RtlCaptureContext(&context);
    return CaptureStackTrace(&context, frames, maxFrames, frameCount);
#elif defined(__linux__) || defined(__APPLE__)
    // Linux/macOS stack trace using backtrace
    void* addresses[MAX_STACK_FRAMES];
    int count = backtrace(addresses, std::min(maxFrames, MAX_STACK_FRAMES));

    frameCount = count;
    for (int i = 0; i < count; ++i) {
        frames[i].address = (uint64_t)addresses[i];
        ResolveSymbolInfo(frames[i].address, frames[i]);
    }

    return count > 0;
#elif defined(__ANDROID__)
    // Android stack trace using unwind
    // Simplified implementation for Android
    frameCount = 0;
    return false; // TODO: Implement Android-specific stack walking
#else
    // Fallback for unsupported platforms
    frameCount = 0;
    return false;
#endif
}

// Gets the singleton instance of the exception handler
ExceptionHandler& ExceptionHandler::GetInstance() {
    // Return the global static instance
    return exceptionHandler;
}

#if defined(_WIN32) || defined(_WIN64)
// Windows-specific callback for SEH (Structured Exception Handling)
LONG WINAPI ExceptionHandler::UnhandledExceptionFilter(PEXCEPTION_POINTERS exceptionInfo) {
    // Get the singleton instance and process the SEH exception
    ExceptionHandler& handler = GetInstance();

    // Process the structured exception with full details (no C++ exceptions here)
    handler.ProcessSEHException(exceptionInfo);

    // Return to continue the exception search (let system handle it)
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
// Unix-like systems signal handler
void ExceptionHandler::SignalHandler(int signal, siginfo_t* info, void* context) {
    // Get the singleton instance and process the signal
    ExceptionHandler& handler = GetInstance();

    // Process the signal with full details (no C++ exceptions here)
    handler.ProcessSignalException(signal, info, context);
}
#endif

// Internal method to capture detailed stack trace information
bool ExceptionHandler::CaptureStackTrace(void* context, StackFrameInfo* frames, int maxFrames, int& frameCount) {
    frameCount = 0;

    // Validate input parameters
    if (!context || !frames || maxFrames <= 0) {
        return false;
    }

#if defined(_WIN32) || defined(_WIN64)
    PCONTEXT pContext = (PCONTEXT)context;

    #ifdef _DEBUG
        // Use DbgHelp for detailed stack walking in debug builds
        if (m_symbolsInitialized && m_processHandle) {
            STACKFRAME64 stackFrame;
            memset(&stackFrame, 0, sizeof(stackFrame));

            // Set up stack frame based on architecture
            #ifdef _WIN64
                    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
                    stackFrame.AddrPC.Offset = pContext->Rip;
                    stackFrame.AddrPC.Mode = AddrModeFlat;
                    stackFrame.AddrFrame.Offset = pContext->Rbp;
                    stackFrame.AddrFrame.Mode = AddrModeFlat;
                    stackFrame.AddrStack.Offset = pContext->Rsp;
                    stackFrame.AddrStack.Mode = AddrModeFlat;
            #else
                    DWORD machineType = IMAGE_FILE_MACHINE_I386;
                    stackFrame.AddrPC.Offset = pContext->Eip;
                    stackFrame.AddrPC.Mode = AddrModeFlat;
                    stackFrame.AddrFrame.Offset = pContext->Ebp;
                    stackFrame.AddrFrame.Mode = AddrModeFlat;
                    stackFrame.AddrStack.Offset = pContext->Esp;
                    stackFrame.AddrStack.Mode = AddrModeFlat;
            #endif

            // Walk the stack and capture frame information
            HANDLE currentThread = GetCurrentThread();
            while (frameCount < maxFrames &&
                StackWalk64(machineType, m_processHandle, currentThread, &stackFrame,
                    pContext, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {

                // Skip frames with zero address (invalid frames)
                if (stackFrame.AddrPC.Offset == 0) {
                    break;
                }

                // Initialize current frame
                StackFrameInfo& currentFrame = frames[frameCount];
                currentFrame.address = stackFrame.AddrPC.Offset;

                // Resolve symbol information for this frame
                ResolveSymbolInfo(currentFrame.address, currentFrame);

                // Get module information for this frame
                GetModuleInfo(currentFrame.address, currentFrame.moduleName, MAX_MODULE_NAME_LENGTH);

                frameCount++;
            }

            return frameCount > 0;
        }
    #endif

    // Fallback to basic stack trace for release builds
    uint64_t addresses[MAX_STACK_FRAMES];
    int basicFrameCount = 0;

    if (GetBasicStackTrace(context, addresses, maxFrames, basicFrameCount)) {
        frameCount = basicFrameCount;

        // Fill in basic information for each frame
        for (int i = 0; i < frameCount; ++i) {
            frames[i].address = addresses[i];
            GetModuleInfo(addresses[i], frames[i].moduleName, MAX_MODULE_NAME_LENGTH);

            // Create hex dump for release builds
            std::string hexDump;
            CreateHexDump(addresses[i], 16, hexDump);
            SafeStringCopyN(frames[i].functionName, hexDump.c_str(), MAX_SYMBOL_NAME_LENGTH, MAX_SYMBOL_NAME_LENGTH - 1);
        }

        return true;
    }
#elif defined(__linux__) || defined(__APPLE__)
    // Unix-like systems stack trace using backtrace
    void* addresses[MAX_STACK_FRAMES];
    int count = backtrace(addresses, std::min(maxFrames, MAX_STACK_FRAMES));

    frameCount = count;
    for (int i = 0; i < count; ++i) {
        frames[i].address = (uint64_t)addresses[i];
        ResolveSymbolInfo(frames[i].address, frames[i]);
        GetModuleInfo(frames[i].address, frames[i].moduleName, MAX_MODULE_NAME_LENGTH);
    }

    return count > 0;
#endif

    return false;
}

// Internal method to resolve symbol information for a given address
bool ExceptionHandler::ResolveSymbolInfo(uint64_t address, StackFrameInfo& frameInfo) {
#if defined(_WIN32) || defined(_WIN64)
    #ifdef _DEBUG
        if (!m_symbolsInitialized || !m_processHandle) {
            // Create hex representation if symbol resolution fails
            snprintf(frameInfo.functionName, MAX_SYMBOL_NAME_LENGTH, "0x%llX", (unsigned long long)address);
            return false;
        }

        // Allocate buffer for symbol information
        char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYMBOL_NAME_LENGTH];
        PSYMBOL_INFO symbol = (PSYMBOL_INFO)symbolBuffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYMBOL_NAME_LENGTH;

        // Try to get symbol information
        DWORD64 displacement = 0;
        if (SymFromAddr(m_processHandle, address, &displacement, symbol)) {
            // Copy symbol name safely
            SafeStringCopyN(frameInfo.functionName, symbol->Name, MAX_SYMBOL_NAME_LENGTH, MAX_SYMBOL_NAME_LENGTH - 1);
            frameInfo.displacement = displacement;

            // Try to get line information
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;

            if (SymGetLineFromAddr64(m_processHandle, address, &lineDisplacement, &line)) {
                frameInfo.lineNumber = line.LineNumber;
                SafeStringCopyN(frameInfo.fileName, line.FileName, sizeof(frameInfo.fileName), sizeof(frameInfo.fileName) - 1);
            }

            return true;
        }
    #endif

    // If symbol resolution fails, create a hex representation
    snprintf(frameInfo.functionName, MAX_SYMBOL_NAME_LENGTH, "0x%llX", (unsigned long long)address);
    return false;

#elif defined(__linux__) || defined(__APPLE__)
    // Unix-like systems symbol resolution using dladdr
    Dl_info dlinfo;
    if (dladdr((void*)address, &dlinfo) && dlinfo.dli_sname) {
        // Demangle C++ symbol names
        int status = 0;
        char* demangled = abi::__cxa_demangle(dlinfo.dli_sname, nullptr, nullptr, &status);

        if (status == 0 && demangled) {
            SafeStringCopyN(frameInfo.functionName, demangled, MAX_SYMBOL_NAME_LENGTH, MAX_SYMBOL_NAME_LENGTH - 1);
            free(demangled);
        }
        else {
            SafeStringCopyN(frameInfo.functionName, dlinfo.dli_sname, MAX_SYMBOL_NAME_LENGTH, MAX_SYMBOL_NAME_LENGTH - 1);
        }

        // Calculate displacement
        frameInfo.displacement = address - (uint64_t)dlinfo.dli_saddr;
        return true;
    }

    // Fallback to hex representation
    snprintf(frameInfo.functionName, MAX_SYMBOL_NAME_LENGTH, "0x%llX", (unsigned long long)address);
    return false;
#else
    // Fallback for unsupported platforms
    snprintf(frameInfo.functionName, MAX_SYMBOL_NAME_LENGTH, "0x%llX", (unsigned long long)address);
    return false;
#endif
}

// Internal method to get module information for a given address
bool ExceptionHandler::GetModuleInfo(uint64_t address, char* moduleName, size_t moduleNameSize) {
    // Validate input parameters
    if (!moduleName || moduleNameSize == 0) {
        return false;
    }

    // Initialize module name to unknown
    SafeStringCopy(moduleName, "Unknown", moduleNameSize);

#if defined(_WIN32) || defined(_WIN64)
    HMODULE hModule = nullptr;

    // Get module handle for the address
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)address, &hModule) && hModule) {

        // Get module file name
        char fullPath[MAX_PATH];
        if (GetModuleFileNameA(hModule, fullPath, MAX_PATH)) {
            // Extract just the filename from the full path
            char* fileName = strrchr(fullPath, '\\');
            if (fileName) {
                SafeStringCopyN(moduleName, fileName + 1, moduleNameSize, moduleNameSize - 1);
            }
            else {
                SafeStringCopyN(moduleName, fullPath, moduleNameSize, moduleNameSize - 1);
            }
            return true;
        }
    }
#elif defined(__linux__) || defined(__APPLE__)
    // Unix-like systems module resolution using dladdr
    Dl_info dlinfo;
    if (dladdr((void*)address, &dlinfo) && dlinfo.dli_fname) {
        // Extract just the filename from the full path
        const char* fileName = strrchr(dlinfo.dli_fname, '/');
        if (fileName) {
            SafeStringCopyN(moduleName, fileName + 1, moduleNameSize, moduleNameSize - 1);
        }
        else {
            SafeStringCopyN(moduleName, dlinfo.dli_fname, moduleNameSize, moduleNameSize - 1);
        }
        return true;
    }
#endif

    return false;
}

#if defined(_WIN32) || defined(_WIN64)
// Internal method to process and log SEH exceptions (no C++ exceptions used)
void ExceptionHandler::ProcessSEHException(PEXCEPTION_POINTERS exceptionInfo) {
    // Thread-safe SEH exception processing
    std::lock_guard<std::mutex> lock(m_exceptionMutex);

    // Extract basic exception information
    ExceptionDetails details;
    details.exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
    details.exceptionAddress = (uint64_t)exceptionInfo->ExceptionRecord->ExceptionAddress;
    details.threadId = GetCurrentThreadId();
    details.processId = GetCurrentProcessId();
    details.timeStamp = GetCurrentTimestamp();

    // Allocate stack frames on heap to avoid stack overflow
    details.AllocateStackFrames(MAX_STACK_FRAMES);

    // Get human-readable exception description
    GetExceptionDescription(details.exceptionCode, details.exceptionDescription,
        sizeof(details.exceptionDescription));

    // Log basic exception information
    debug.logDebugMessage(LogLevel::LOG_CRITICAL,
        L"[ExceptionHandler] SEH Exception 0x%08X (%hs) at address 0x%llX in thread %d",
        details.exceptionCode, details.exceptionDescription, details.exceptionAddress, details.threadId);

    // Capture detailed stack trace
    if (CaptureStackTrace(exceptionInfo->ContextRecord, details.stackFrames.get(),
        MAX_STACK_FRAMES, details.frameCount)) {

        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Stack trace (%d frames):", details.frameCount);

        // Log detailed stack frames (limit to first 15 for readability)
        int framesToLog = std::min(details.frameCount, 15);
        for (int i = 0; i < framesToLog; ++i) {
            const StackFrameInfo& frame = details.stackFrames[i];

            if (strlen(frame.functionName) > 0 && strlen(frame.moduleName) > 0) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: %hs [%hs+0x%llX]",
                    i, frame.functionName, frame.moduleName, frame.displacement);
            }
            else {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: 0x%llX [%hs]",
                    i, frame.address, frame.moduleName);
            }
        }
    }

    // Log the last few function calls for additional context
    LogLastFunctionCalls();

    // Generate crash dump if enabled
    if (m_crashDumpEnabled) {
        if (GenerateCrashDump(exceptionInfo)) {
            #if defined(_DEBUG_EXCEPTIONHANDLER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Crash dump generated successfully");
            #endif
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ExceptionHandler] Failed to generate crash dump");
        }
    }

    // Log additional context information
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Process ID: %d, Thread ID: %d, Timestamp: %llu",
        details.processId, details.threadId, details.timeStamp);
}
#endif

#if defined(_WIN32) || defined(_WIN64)
// Internal method to convert exception code to human-readable description
void ExceptionHandler::GetExceptionDescription(DWORD exceptionCode, char* description, size_t descriptionSize) {
    // Validate input parameters
    if (!description || descriptionSize == 0) {
        return;
    }

    // Convert common exception codes to descriptive text
    switch (exceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        SafeStringCopy(description, "Access Violation - Invalid memory access", descriptionSize);
        break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        SafeStringCopy(description, "Array Bounds Exceeded - Array index out of range", descriptionSize);
        break;
    case EXCEPTION_BREAKPOINT:
        SafeStringCopy(description, "Breakpoint - Debugger breakpoint encountered", descriptionSize);
        break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        SafeStringCopy(description, "Datatype Misalignment - Invalid data alignment", descriptionSize);
        break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        SafeStringCopy(description, "Floating Point - Denormal operand", descriptionSize);
        break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        SafeStringCopy(description, "Floating Point - Division by zero", descriptionSize);
        break;
    case EXCEPTION_FLT_INEXACT_RESULT:
        SafeStringCopy(description, "Floating Point - Inexact result", descriptionSize);
        break;
    case EXCEPTION_FLT_INVALID_OPERATION:
        SafeStringCopy(description, "Floating Point - Invalid operation", descriptionSize);
        break;
    case EXCEPTION_FLT_OVERFLOW:
        SafeStringCopy(description, "Floating Point - Overflow", descriptionSize);
        break;
    case EXCEPTION_FLT_STACK_CHECK:
        SafeStringCopy(description, "Floating Point - Stack check", descriptionSize);
        break;
    case EXCEPTION_FLT_UNDERFLOW:
        SafeStringCopy(description, "Floating Point - Underflow", descriptionSize);
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        SafeStringCopy(description, "Illegal Instruction - Invalid CPU instruction", descriptionSize);
        break;
    case EXCEPTION_IN_PAGE_ERROR:
        SafeStringCopy(description, "In Page Error - Virtual memory page fault", descriptionSize);
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        SafeStringCopy(description, "Integer Division by Zero", descriptionSize);
        break;
    case EXCEPTION_INT_OVERFLOW:
        SafeStringCopy(description, "Integer Overflow", descriptionSize);
        break;
    case EXCEPTION_INVALID_DISPOSITION:
        SafeStringCopy(description, "Invalid Disposition - Exception handler error", descriptionSize);
        break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        SafeStringCopy(description, "Non-continuable Exception - Fatal system error", descriptionSize);
        break;
    case EXCEPTION_PRIV_INSTRUCTION:
        SafeStringCopy(description, "Privileged Instruction - Invalid privilege level", descriptionSize);
        break;
    case EXCEPTION_SINGLE_STEP:
        SafeStringCopy(description, "Single Step - Debugger single step", descriptionSize);
        break;
    case EXCEPTION_STACK_OVERFLOW:
        SafeStringCopy(description, "Stack Overflow - Stack space exhausted", descriptionSize);
        break;
    case 0xC0000374: // STATUS_HEAP_CORRUPTION
        SafeStringCopy(description, "Heap Corruption - Memory heap is corrupted", descriptionSize);
        break;
    default:
        // For unknown exception codes, show the hex value
        snprintf(description, descriptionSize, "Unknown Exception Code (0x%08X)", exceptionCode);
        break;
    }
}
#else
// Internal method to process and log signal exceptions (Unix-like systems)
void ExceptionHandler::ProcessSignalException(int signal, siginfo_t* info, void* context) {
    // Thread-safe signal exception processing
    std::lock_guard<std::mutex> lock(m_exceptionMutex);

    // Extract basic signal information
    ExceptionDetails details;
    details.exceptionCode = signal;
    details.exceptionAddress = (uint64_t)info->si_addr;
    details.threadId = GetCurrentThreadId();
    details.processId = GetCurrentProcessId();
    details.timeStamp = GetCurrentTimestamp();

    // Get human-readable signal description
    GetSignalDescription(signal, details.exceptionDescription,
        sizeof(details.exceptionDescription));

    // Log basic signal information
    debug.logDebugMessage(LogLevel::LOG_CRITICAL,
        L"[ExceptionHandler] Signal %d (%hs) at address 0x%llX in thread %d",
        signal, details.exceptionDescription, details.exceptionAddress, details.threadId);

    // Capture detailed stack trace
    if (CaptureStackTrace(context, details.stackFrames,
        MAX_STACK_FRAMES, details.frameCount)) {

        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Stack trace (%d frames):", details.frameCount);

        // Log detailed stack frames (limit to first 15 for readability)
        int framesToLog = std::min(details.frameCount, 15);
        for (int i = 0; i < framesToLog; ++i) {
            const StackFrameInfo& frame = details.stackFrames[i];

            if (strlen(frame.functionName) > 0 && strlen(frame.moduleName) > 0) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: %hs [%hs+0x%llX]",
                    i, frame.functionName, frame.moduleName, frame.displacement);
            }
            else {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: 0x%llX [%hs]",
                    i, frame.address, frame.moduleName);
            }
        }
    }

    // Log the last few function calls for additional context
    LogLastFunctionCalls();

    // Generate crash dump if enabled
    if (m_crashDumpEnabled) {
        if (GenerateCrashDump(context)) {
#if defined(_DEBUG_EXCEPTIONHANDLER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Crash dump generated successfully");
#endif
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ExceptionHandler] Failed to generate crash dump");
        }
    }

    // Log additional context information
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Process ID: %d, Thread ID: %d, Timestamp: %llu",
        details.processId, details.threadId, details.timeStamp);
}

// Internal method to convert signal to human-readable description
void ExceptionHandler::GetSignalDescription(int signal, char* description, size_t descriptionSize) {
    // Validate input parameters
    if (!description || descriptionSize == 0) {
        return;
    }

    // Convert common signals to descriptive text
    switch (signal) {
    case SIGSEGV:
        SafeStringCopy(description, "Segmentation Fault - Invalid memory access", descriptionSize);
        break;
    case SIGABRT:
        SafeStringCopy(description, "Program Abort - Application terminated abnormally", descriptionSize);
        break;
    case SIGFPE:
        SafeStringCopy(description, "Floating Point Exception - Invalid arithmetic operation", descriptionSize);
        break;
    case SIGILL:
        SafeStringCopy(description, "Illegal Instruction - Invalid CPU instruction", descriptionSize);
        break;
    case SIGBUS:
        SafeStringCopy(description, "Bus Error - Invalid memory alignment or access", descriptionSize);
        break;
    case SIGTERM:
        SafeStringCopy(description, "Termination Request - Process termination requested", descriptionSize);
        break;
    case SIGKILL:
        SafeStringCopy(description, "Kill Signal - Forced process termination", descriptionSize);
        break;
    case SIGINT:
        SafeStringCopy(description, "Interrupt Signal - User interrupt (Ctrl+C)", descriptionSize);
        break;
    default:
        // For unknown signals, show the signal number
        snprintf(description, descriptionSize, "Unknown Signal (%d)", signal);
        break;
    }
}
#endif

// Internal method to generate crash dump file
// Internal method to generate crash dump file
bool ExceptionHandler::GenerateCrashDump(void* exceptionInfo) {
    // Create unique crash dump filename with timestamp
    uint64_t timestamp = GetCurrentTimestamp();

    char dumpFileName[512];
    snprintf(dumpFileName, sizeof(dumpFileName), "CrashDump_%llu.dmp", timestamp);

#if defined(_WIN32) || defined(_WIN64)
    PEXCEPTION_POINTERS pExceptionInfo = (PEXCEPTION_POINTERS)exceptionInfo;

    // Create the crash dump file
    HANDLE hDumpFile = CreateFileA(dumpFileName, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hDumpFile == INVALID_HANDLE_VALUE) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to create dump file: %hs. Error: %d",
            dumpFileName, GetLastError());
        return false;
    }

    // Set up minidump exception information
    MINIDUMP_EXCEPTION_INFORMATION exceptionParam;
    exceptionParam.ThreadId = GetCurrentThreadId();
    exceptionParam.ExceptionPointers = pExceptionInfo;
    exceptionParam.ClientPointers = FALSE;

    // Determine dump type based on build configuration
    MINIDUMP_TYPE dumpType;
#ifdef _DEBUG
    // Full dump with more information for debug builds
    dumpType = (MINIDUMP_TYPE)(MiniDumpWithFullMemory |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo |
        MiniDumpWithProcessThreadData);
#else
    // Smaller dump for release builds
    dumpType = (MINIDUMP_TYPE)(MiniDumpNormal |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo);
#endif

    // Write the minidump to file
    BOOL dumpResult = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
        hDumpFile, dumpType, &exceptionParam, nullptr, nullptr);

    // Close the dump file handle
    CloseHandle(hDumpFile);

    if (dumpResult) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Crash dump saved to: %hs", dumpFileName);
        return true;
    }
    else {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] MiniDumpWriteDump failed. Error: %d", GetLastError());
        // Try to delete the failed dump file
        DeleteFileA(dumpFileName);
        return false;
    }
#else
    // Unix-like systems - create a simple crash report
    FILE* dumpFile = fopen(dumpFileName, "w");
    if (!dumpFile) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ExceptionHandler] Failed to create dump file: %hs", dumpFileName);
        return false;
    }

    fprintf(dumpFile, "Crash Report\n");
    fprintf(dumpFile, "Timestamp: %llu\n", timestamp);
    fprintf(dumpFile, "Process ID: %d\n", GetCurrentProcessId());
    fprintf(dumpFile, "Thread ID: %d\n", GetCurrentThreadId());
    fprintf(dumpFile, "\nStack Trace:\n");

    // Allocate stack frames on heap to avoid stack overflow
    std::unique_ptr<StackFrameInfo[]> frames(new StackFrameInfo[MAX_STACK_FRAMES]);
    int frameCount = 0;

    if (GetCurrentStackTrace(frames.get(), MAX_STACK_FRAMES, frameCount)) {
        for (int i = 0; i < frameCount; ++i) {
            fprintf(dumpFile, "Frame %d: 0x%llX [%s] %s\n",
                i, frames[i].address, frames[i].moduleName, frames[i].functionName);
        }
    }

    fclose(dumpFile);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Crash report saved to: %hs", dumpFileName);
    return true;
#endif
}

// Internal method to create hex dump for release builds
void ExceptionHandler::CreateHexDump(uint64_t address, int size, std::string& hexDump) {
    // Clear the output string
    hexDump.clear();

    // Validate parameters
    if (size <= 0 || size > 256) {
        hexDump = "Invalid size for hex dump";
        return;
    }

    // Reserve space for the hex string
    hexDump.reserve(size * 3 + 50);

    // Add address header
    char addressHeader[32];
    snprintf(addressHeader, sizeof(addressHeader), "Address 0x%llX: ", (unsigned long long)address);
    hexDump += addressHeader;

    // Memory access wrapped in platform-specific protection
    bool memoryAccessible = false;

    #if defined(_WIN32) || defined(_WIN64)
        // Windows memory protection
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) &&
            (mbi.State == MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            memoryAccessible = true;
        }
    #else
        // Unix-like systems - assume accessible for now (more complex implementation needed)
        memoryAccessible = true;
    #endif

    if (memoryAccessible) {
        // Try to read memory at the given address
        const BYTE* memPtr = (const BYTE*)address;

        // Read and format each byte
        for (int i = 0; i < size; ++i) {
            char byteStr[4];
            snprintf(byteStr, sizeof(byteStr), "%02X ", memPtr[i]);
            hexDump += byteStr;
        }
    }
    else {
        // Memory couldn't be accessed safely
        hexDump += "[Memory not accessible]";
    }
}

// Internal method to get basic stack information for release builds
bool ExceptionHandler::GetBasicStackTrace(void* context, uint64_t* addresses, int maxFrames, int& frameCount) {
    frameCount = 0;

    // Validate input parameters
    if (!context || !addresses || maxFrames <= 0) {
        return false;
    }

    #if defined(_WIN32) || defined(_WIN64)
        PCONTEXT pContext = (PCONTEXT)context;

        // Get current stack pointer and instruction pointer
    #ifdef _WIN64
        uint64_t stackPtr = pContext->Rsp;
        uint64_t instructionPtr = pContext->Rip;
    #else
        uint64_t stackPtr = pContext->Esp;
        uint64_t instructionPtr = pContext->Eip;
    #endif

    // Add the current instruction pointer as first frame
    if (frameCount < maxFrames) {
        addresses[frameCount++] = instructionPtr;
    }

    // Try to walk the stack by examining return addresses
    // Use safer memory access checking
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)stackPtr, &mbi, sizeof(mbi)) &&
        (mbi.State == MEM_COMMIT) &&
        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {

        uint64_t* stackFrame = (uint64_t*)stackPtr;

        // Look for potential return addresses on the stack
        for (int i = 0; i < 100 && frameCount < maxFrames; ++i) {
            uint64_t potentialAddress = stackFrame[i];

            // Basic validation - check if it looks like a code address
            if (potentialAddress > 0x400000 && potentialAddress < 0x7FFFFFFF0000) {
                // Try to verify it's actually code by checking if we can get module info
                char moduleName[64];
                if (GetModuleInfo(potentialAddress, moduleName, 64)) {
                    addresses[frameCount++] = potentialAddress;
                }
            }
        }
    }
#else
    // Unix-like systems basic stack trace
    void* addressPtrs[MAX_STACK_FRAMES];
    int count = backtrace(addressPtrs, std::min(maxFrames, MAX_STACK_FRAMES));

    frameCount = count;
    for (int i = 0; i < count; ++i) {
        addresses[i] = (uint64_t)addressPtrs[i];
    }
#endif

    return frameCount > 0;
}

// Internal method to log the last few function calls for context
void ExceptionHandler::LogLastFunctionCalls() {
    // Thread-safe access to last calls buffer
    std::lock_guard<std::mutex> lock(m_lastCallsMutex);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ExceptionHandler] Last function calls (breadcrumb trail):");

    // Log the last calls in chronological order
    bool hasValidCalls = false;
    for (int i = 0; i < LAST_CALLS_BUFFER_SIZE; ++i) {
        // Calculate the actual index (oldest to newest)
        int index = (m_lastCallsIndex + i) % LAST_CALLS_BUFFER_SIZE;

        // Only log non-empty entries
        if (strlen(m_lastCalls[index]) > 0) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"  Call %d: %hs", i + 1, m_lastCalls[index]);
            hasValidCalls = true;
        }
    }

    // If no function calls were recorded, indicate this
    if (!hasValidCalls) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"  No function calls recorded");
    }
}

// Internal utility methods for cross-platform compatibility
uint64_t ExceptionHandler::GetCurrentTimestamp() {
#if defined(_WIN32) || defined(_WIN64)
    // Windows timestamp using GetSystemTime
    SYSTEMTIME sysTime;
    GetSystemTime(&sysTime);

    // Convert to simple timestamp (seconds since 2000)
    return (uint64_t)sysTime.wYear * 10000000000LL +
        (uint64_t)sysTime.wMonth * 100000000LL +
        (uint64_t)sysTime.wDay * 1000000LL +
        (uint64_t)sysTime.wHour * 10000LL +
        (uint64_t)sysTime.wMinute * 100LL +
        (uint64_t)sysTime.wSecond;
#else
    // Unix-like systems timestamp using time()
    return (uint64_t)time(nullptr);
#endif
}

uint32_t ExceptionHandler::GetCurrentThreadId() {
    #if defined(_WIN32) || defined(_WIN64)
        // Windows thread ID
        return ::GetCurrentThreadId();
    #else
        // Unix-like systems thread ID
        return (uint32_t)pthread_self();
    #endif
}

uint32_t ExceptionHandler::GetCurrentProcessId() {
    #if defined(_WIN32) || defined(_WIN64)
        // Windows process ID
        return ::GetCurrentProcessId();
    #else
        // Unix-like systems process ID
        return (uint32_t)getpid();
    #endif
}

// Cross-platform string conversion utilities (if needed for future expansion)
void ExceptionHandler::ConvertWideToNarrow(const wchar_t* wide, char* narrow, size_t narrowSize) {
    // Validate input parameters
    if (!wide || !narrow || narrowSize == 0) {
        return;
    }

    #if defined(_WIN32) || defined(_WIN64)
        // Windows wide to narrow conversion
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, (int)narrowSize, nullptr, nullptr);
    #else
        // Unix-like systems - simple conversion (assuming ASCII subset)
        size_t wideLen = wcslen(wide);
        size_t copyLen = std::min(wideLen, narrowSize - 1);

        for (size_t i = 0; i < copyLen; ++i) {
            narrow[i] = (char)(wide[i] & 0xFF); // Truncate to 8-bit
        }
        narrow[copyLen] = '\0';
    #endif
}

void ExceptionHandler::ConvertNarrowToWide(const char* narrow, wchar_t* wide, size_t wideSize) {
    // Validate input parameters
    if (!narrow || !wide || wideSize == 0) {
        return;
    }

    #if defined(_WIN32) || defined(_WIN64)
        // Windows narrow to wide conversion
        MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, (int)wideSize);
    #else
        // Unix-like systems - simple conversion (assuming ASCII subset)
        size_t narrowLen = strlen(narrow);
        size_t copyLen = std::min(narrowLen, wideSize - 1);

        for (size_t i = 0; i < copyLen; ++i) {
            wide[i] = (wchar_t)(narrow[i] & 0xFF); // Extend to wide char
        }
        wide[copyLen] = L'\0';
    #endif
}

// Restore warning settings
#if defined(_WIN32) || defined(_WIN64)
    #pragma warning(pop)
#endif