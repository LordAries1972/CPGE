// ------------------------------------------------------------------------------
// Debug Class Implementation with FileIO Integration and Cross-Platform Support
// Compatible with Windows, Linux, macOS, Android, and iOS platforms
// ------------------------------------------------------------------------------

#include "Includes.h"
#include "Debug.h"
#include "FileIO.h"

extern FileIO fileIO;

// Constructor - Initialize Debug system with FileIO integration and session-based logging for all platforms
Debug::Debug() : m_isInitialized(false), m_hasCleanedUp(false)
{
    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
        // Cross-platform debug output during construction
        OutputToConsole("[DEBUG]: Debug constructor called - initializing debug system with FileIO integration");
    #endif
}

// Destructor - Ensure proper cleanup of Debug resources including FileIO for all platforms
Debug::~Debug()
{
    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
        // Cross-platform debug output during destruction
        OutputToConsole("[DEBUG]: Debug destructor called - cleaning up debug system");
    #endif

    // Perform cleanup if not already done to prevent resource leaks
    if (!m_hasCleanedUp) {
        Cleanup();
    }
}

// Initialize Debug system and integrate with FileIO for enhanced file operations across all platforms
bool Debug::Initialize()
{
    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
        // Cross-platform debug output during initialization
        OutputToConsole("[DEBUG]: Debug::Initialize() called - starting debug system initialization");
    #endif

    // Prevent double initialization to avoid resource conflicts
    if (m_isInitialized) {
        #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
            // Cross-platform warning output for double initialization
            OutputToConsole("[WARNING]: Debug system already initialized - skipping initialization");
        #endif
        return true;
    }

    try {
        // Delete existing log file to ensure session-based logging across all platforms
        DeleteLogFileOnStartup();

        // Mark Debug system as successfully initialized
        m_isInitialized = true;
        m_hasCleanedUp = false;

        #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
            // Cross-platform success message for initialization completion
            OutputToConsole("[INFO]: Debug system initialization completed successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
                // Cross-platform error output for initialization failure
                std::string errorMsg = "[CRITICAL]: Debug initialization failed with exception: " + std::string(e.what());
                OutputToConsole(errorMsg);
        #endif
        return false;
    }
}

// Clean up Debug resources and shutdown FileIO integration for all platforms
void Debug::Cleanup()
{
    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
        // Cross-platform debug output during cleanup
        OutputToConsole("[DEBUG]: Debug::Cleanup() called - starting debug system cleanup");
    #endif

    // Prevent double cleanup to avoid resource access violations
    if (m_hasCleanedUp) {
        return;
    }

    // Mark cleanup as completed
    m_isInitialized = false;
    m_hasCleanedUp = true;

    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
        // Cross-platform success message for cleanup completion
        OutputToConsole("[INFO]: Debug system cleanup completed successfully");
    #endif
}

// Delete existing log file on startup for session-based logging across all platforms
void Debug::DeleteLogFileOnStartup()
{
    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
        // Cross-platform debug output for log file deletion
        OutputToConsole("[DEBUG]: Deleting existing log file for session-based logging");
    #endif

    // Only delete log file if FileIO output is enabled and not in debug mode
    #if !defined(NO_DEBUGFILE_OUTPUT) && !defined(_DEBUG)
        if (fileIO && fileIO->IsInitialized()) {
            // Convert wide string log filename to standard string for FileIO operations using cross-platform method
            std::string logFileName = WideStringToString(LOG_FILE_NAME);

            // Check if log file exists before attempting deletion
            bool fileExists = false;
            int taskID = 0;
            if (fileIO->FileExists(logFileName, fileExists, FileIOPriority::PRIORITY_HIGH, taskID)) {
                // Wait for file existence check to complete
                bool taskSuccess = false;
                bool isReady = false;
                int maxAttempts = 100;                                     // Maximum wait attempts to prevent infinite loops
                int attempts = 0;

                // Poll for task completion with timeout protection
                while (attempts < maxAttempts && !isReady) {
                    if (fileIO->IsFileIOTaskCompleted(taskID, taskSuccess, isReady)) {
                        if (isReady && taskSuccess) {
                            // File existence check completed successfully
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Brief sleep to prevent CPU spinning
                    attempts++;
                }

                // If file exists, delete it for session-based logging
                if (isReady && taskSuccess && fileExists) {
                    int deleteTaskID = 0;
                    fileIO->DeleteFile(logFileName, FileIOPriority::PRIORITY_HIGH, deleteTaskID);

                    #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
                        // Cross-platform info message for log file deletion
                        OutputToConsole("[INFO]: Existing log file deleted for session-based logging");
                    #endif
                }
            }
        }
    #endif
}

// Write message to log file using FileIO system with thread safety and error handling for all platforms
void Debug::WriteToLogFile(const std::wstring& message)
{
    // Only write to file if FileIO output is enabled and not in debug mode
    #if !defined(NO_DEBUGFILE_OUTPUT) && !defined(_DEBUG)
        try {
            // Get current timestamp for log entry using cross-platform method
            std::string timestamp = GetFormattedTimestamp();
            std::wstring wTimestamp = StringToWideString(timestamp);

            // Create timestamped message with cross-platform formatting
            std::wstring timestampedMessage = wTimestamp + L": " + message + L"\n";

            // Convert wide string to UTF-8 for FileIO operations using cross-platform method
            std::string utf8Message = WideStringToString(timestampedMessage);

            // Convert log filename to standard string using cross-platform method
            std::string logFileName = WideStringToString(LOG_FILE_NAME);

            // Prepare data buffer for FileIO write operation
            std::vector<uint8_t> writeBuffer(utf8Message.begin(), utf8Message.end());

            // Append message to log file using FileIO with high priority
            int taskID = 0;
            fileIO->AppendToFile(logFileName, writeBuffer, FileIOType::TYPE_ASCII,
                FileIOPosition::POSITION_END, FileIOPriority::PRIORITY_HIGH, taskID);

            #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
                // Cross-platform debug message for successful file write
                std::string debugMsg = "[DEBUG]: Log message written to file via FileIO - Task ID: " + std::to_string(taskID);
                OutputToConsole(debugMsg);
            #endif
        }
        catch (const std::exception& e) {
            #if defined(_DEBUG_DEBUG_) && defined(_DEBUG)
                // Cross-platform error message for file write failure
                std::string errorMsg = "[ERROR]: Failed to write to log file via FileIO: " + std::string(e.what());
                OutputToConsole(errorMsg);
            #endif
        }
    #endif
}

// Legacy debug logging function maintained for backward compatibility across all platforms
void Debug::DebugLog(const std::string& message)
{
    // Create formatted debug message with INFO level prefix
    std::string formattedMessage = "[INFO]: " + message;

    // Output to debug console for immediate visibility using cross-platform method
    OutputToConsole(formattedMessage);

    // Convert to wide string and write to log file via FileIO
    std::wstring wideMessage = StringToWideString(message);
    WriteToLogFile(L"[INFO]: " + wideMessage);
}

// Legacy file insertion method maintained for compatibility with existing code across all platforms
void Debug::Insert_Into_Log_File(const std::wstring& filename, const std::wstring& lineMsg)
{
    // Delegate to new WriteToLogFile method for FileIO integration
    WriteToLogFile(lineMsg);
}

// Log formatted debug message with variable parameters and specific log level for all platforms
void Debug::logDebugMessage(LogLevel level, const wchar_t* format, ...)
{
    // Check if message level meets current logging threshold
    if (int(level) >= int(currentLogLevel))
    {
        // Format message with variable arguments using cross-platform method
        wchar_t buffer[2048];
        va_list args;
        va_start(args, format);

        #if defined(_WIN64) || defined(_WIN32)
            // Windows-specific wide character formatting
            vswprintf_s(buffer, format, args);
        #else
            // Cross-platform wide character formatting for non-Windows systems
            vswprintf(buffer, sizeof(buffer) / sizeof(wchar_t), format, args);
        #endif

        va_end(args);

        // Create wide string message and delegate to logLevelMessage
        std::wstring message(buffer);
        logLevelMessage(level, message);
    }
}

// Log message with specific level and wide string content for all platforms
void Debug::logLevelMessage(LogLevel level, const std::wstring& message)
{
    // Check if message level meets current logging threshold
    if (level >= currentLogLevel)
    {
        std::wstring taggedMessage;

        // Add appropriate level tag based on message severity
        switch (level)
        {
            case LogLevel::LOG_DEBUG:
                taggedMessage = L"[DEBUG]: " + message;                    // Debug level for detailed diagnostic information
                break;
            case LogLevel::LOG_INFO:
                taggedMessage = L"[INFO]: " + message;                     // Info level for general informational messages
                break;
            case LogLevel::LOG_WARNING:
                taggedMessage = L"[WARNING]: " + message;                  // Warning level for potential issues
                break;
            case LogLevel::LOG_ERROR:
                taggedMessage = L"[ERROR]: " + message;                    // Error level for recoverable failures
                break;
            case LogLevel::LOG_CRITICAL:
                taggedMessage = L"[CRITICAL]: " + message;                 // Critical level for severe failures
                break;
            case LogLevel::LOG_TERMINATION:
                taggedMessage = L"[TERMINATION]: " + message;              // Termination level for shutdown scenarios
                break;
        }

        // Output to console using cross-platform method
        OutputToConsole(taggedMessage);

        // Write to log file via FileIO system
        WriteToLogFile(taggedMessage);

        // Handle critical level with appropriate system response across all platforms
        if (level == LogLevel::LOG_CRITICAL)
        {
            #if !defined(_DEBUG) && !defined(DEBUG)
                // In release builds, throw exception for critical errors
                throw std::runtime_error("Fatal Critical Error Has Occurred!");
            #endif

            #if defined(_DEBUG)
                #if defined(_WIN64) || defined(_WIN32)
                    // Windows-specific quit message for debug builds
                    PostQuitMessage(0);
                    // Optional: Uncomment next line to trigger immediate debugger break
                    // DebugBreak();
                #else
                    // Cross-platform termination for non-Windows debug builds
                    std::terminate();
                #endif
            #endif
        }
    }
}

// Cross-platform error logging method - Windows version for HRESULT
#if defined(_WIN64) || defined(_WIN32)
bool Debug::LOG_IF_FAILED(HRESULT hr, const LPCWSTR msg)
{
    if (FAILED(hr)) {
        // Format error message with HRESULT code in hexadecimal
        std::wostringstream woss;
        woss << msg << L" (HRESULT: 0x" << std::hex << hr << L")";

        // Get formatted wide string message
        std::wstring wstr = woss.str();

        // Log as error level message
        logLevelMessage(LogLevel::LOG_ERROR, wstr);
        return false;
    }

    return true;
}
#else
// Cross-platform error logging method - Non-Windows version for generic error codes
bool Debug::LOG_IF_FAILED(int result, const char* msg)
{
    if (result != 0) {
        // Format error message with generic error code
        std::ostringstream oss;
        oss << msg << " (Error Code: " << result << ")";

        // Convert to wide string and log as error level message
        std::string errorStr = oss.str();
        std::wstring wstr = StringToWideString(errorStr);
        logLevelMessage(LogLevel::LOG_ERROR, wstr);
        return false;
    }

    return true;
}
#endif

// Log basic string message to console and file system across all platforms
void Debug::Log(const std::string& message)
{
    // Output to debug console using cross-platform method
    #ifdef _DEBUG
        std::string formattedMessage = "[INFO]: " + message;
        OutputToConsole(formattedMessage);
    #endif

    // Convert to wide string and write to log file via FileIO
    std::wstring wideMessage = StringToWideString(message);
    WriteToLogFile(L"[INFO]: " + wideMessage);
}

// Log warning message with appropriate level tagging for all platforms
void Debug::LogWarning(const std::string& message)
{
    // Output to debug console using cross-platform method
    #ifdef _DEBUG
        std::string formattedMessage = "[WARNING]: " + message;
        OutputToConsole(formattedMessage);
    #endif

    // Convert to wide string and write to log file via FileIO
    std::wstring wideMessage = StringToWideString(message);
    WriteToLogFile(L"[WARNING]: " + wideMessage);
}

// Log error message with appropriate level tagging for all platforms
void Debug::LogError(const std::string& message) {
    // Output to debug console using cross-platform method
    #ifdef _DEBUG
        std::string formattedMessage = "[ERROR]: " + message;
        OutputToConsole(formattedMessage);
    #endif

    // Convert to wide string and write to log file via FileIO
    std::wstring wideMessage = StringToWideString(message);
    WriteToLogFile(L"[ERROR]: " + wideMessage);
}

// Log function-specific messages with function name context for all platforms
void Debug::LogFunction(const std::string& functionName, const std::string& message) {
    // Create function-specific message format
    std::string fullMessage = "[Function: " + functionName + "] " + message;

    // Output to debug console using cross-platform method
    #ifdef _DEBUG
        OutputToConsole(fullMessage);
    #endif

    // Convert to wide string and write to log file via FileIO
    std::wstring wideMessage = StringToWideString(fullMessage);
    WriteToLogFile(wideMessage);
}

// Trigger debugger break for development debugging across all platforms
void Debug::DebugBreak() {
    // Cross-platform debugger break implementation
    #ifdef _DEBUG
        #if defined(_WIN64) || defined(_WIN32)
            // Windows-specific debugger break
            __debugbreak();                                                    // Will break if debugging is enabled on Windows
        #elif defined(__linux__) || defined(__APPLE__)
            // Linux and macOS debugger break using signal
        #include <signal.h>
            raise(SIGTRAP);                                                    // Will break if debugging is enabled on Unix systems
        #elif defined(__ANDROID__)
            // Android debugger break using abort
            abort();                                                           // Will break if debugging is enabled on Android
        #elif defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            // iOS debugger break using signal
        #include <signal.h>
            raise(SIGTRAP);                                                    // Will break if debugging is enabled on iOS
        #endif
    #endif
}

// Set minimum logging level for message filtering across all platforms
void Debug::SetLogLevel(LogLevel level) {
    currentLogLevel = level;                                           // Update current logging threshold
}

// Cross-platform console output method for standard strings
void Debug::OutputToConsole(const std::string& message)
{
    #if defined(_WIN64) || defined(_WIN32)
        // Windows-specific console output
        OutputDebugStringA(message.c_str());
    #elif defined(__ANDROID__)
        // Android-specific logging system
        __android_log_print(ANDROID_LOG_INFO, "DebugEngine", "%s", message.c_str());
    #else
        // Generic console output for Linux, macOS, and iOS
        std::cout << message << std::endl;
    #endif
}

// Cross-platform console output method for wide strings
void Debug::OutputToConsole(const std::wstring& message)
{
    #if defined(_WIN64) || defined(_WIN32)
        // Windows-specific wide string console output
        OutputDebugStringW(message.c_str());
    #else
        // Convert wide string to standard string for cross-platform output
        std::string narrowMessage = WideStringToString(message);
        OutputToConsole(narrowMessage);
    #endif
}

// Cross-platform wide string to string conversion method
std::string Debug::WideStringToString(const std::wstring& wstr)
{
    #if defined(_WIN64) || defined(_WIN32)
        // Windows-specific wide string conversion using WinAPI
        if (wstr.empty()) return std::string();

        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        std::string result(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size_needed, NULL, NULL);

        // Remove null terminator if present
        if (!result.empty() && result.back() == '\0') {
            result.pop_back();
        }

        return result;
    #else
        // Cross-platform wide string conversion for non-Windows systems
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
        return converter.to_bytes(wstr);
    #endif
}

// Cross-platform string to wide string conversion method
std::wstring Debug::StringToWideString(const std::string& str)
{
    #if defined(_WIN64) || defined(_WIN32)
        // Windows-specific string conversion using WinAPI
        if (str.empty()) return std::wstring();

        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring result(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size_needed);

        // Remove null terminator if present
        if (!result.empty() && result.back() == L'\0') {
            result.pop_back();
        }

        return result;
    #else
        // Cross-platform string conversion for non-Windows systems
        std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
        return converter.from_bytes(str);
    #endif
}

// Cross-platform formatted timestamp generation method
std::string Debug::GetFormattedTimestamp()
{
    // Get current time using cross-platform chrono
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    #if defined(_WIN64) || defined(_WIN32)
        // Windows-specific thread-safe time formatting
        std::tm tm;
        localtime_s(&tm, &t);

        // Format timestamp with timezone information for Windows
        char timeBuffer[100];
        std::strftime(timeBuffer, sizeof(timeBuffer), "[%d-%m-%Y (AEST/AEDT) %H:%M:%S]", &tm);
        return std::string(timeBuffer);
    #else
        // Cross-platform time formatting for non-Windows systems
        std::tm* tm = std::localtime(&t);
        if (tm == nullptr) {
            // Fallback timestamp if localtime fails
            return "[TIMESTAMP_ERROR]";
        }

        // Format timestamp with timezone information for non-Windows systems
        char timeBuffer[100];
        std::strftime(timeBuffer, sizeof(timeBuffer), "[%d-%m-%Y (Local) %H:%M:%S]", tm);
        return std::string(timeBuffer);
    #endif
}