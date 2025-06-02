#include "Includes.h"
#include "Debug.h"

// -----------------------------------------
// Debug Logging Function
// -----------------------------------------
void Debug::DebugLog(const std::string& message)
{
    #if defined(_DEBUG)
        std::ostringstream oss;
        oss << "[INFO]: " << message << "\n";
        OutputDebugStringA(oss.str().c_str());
    #endif
}

void Debug::Insert_Into_Log_File(const std::wstring& filename, const std::wstring& lineMsg)
{
    // Get current date/time
    std::wostringstream timestampedMsg;
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &t);

    wchar_t timeBuffer[100];
    wcsftime(timeBuffer, sizeof(timeBuffer) / sizeof(wchar_t), L"[%d-%m-%Y (AEST/AEDT) %H:%M:%S]: ", &tm);
    timestampedMsg << timeBuffer << lineMsg << L"\n";

    std::wstring newEntry = timestampedMsg.str();

    // Read existing contents
    std::wifstream inFile(filename);
    std::wstringstream buffer;
    if (inFile.is_open())
    {
        buffer << inFile.rdbuf();
        inFile.close();
    }

    // Prepend the new line and write it back
    std::wofstream outFile(filename, std::ios::trunc);
    if (outFile.is_open())
    {
        outFile << newEntry << buffer.str();
        outFile.close();
    }
}


void Debug::logDebugMessage(LogLevel level, const wchar_t* format, ...)
{
    #if !defined(NO_DEBUGFILE_OUTPUT) && !defined(_DEBUG)
        if (int(level) >= int(currentLogLevel))
        {
            wchar_t buffer[2048];

            va_list args;
            va_start(args, format);
            vswprintf_s(buffer, format, args);
            va_end(args);

            std::wstring message(buffer);
            logLevelMessage(level, message);

            Insert_Into_Log_File(LOG_FILE_NAME, message);
        }
    #endif
}

void Debug::logLevelMessage(LogLevel level, const std::wstring& message)
{
    if (level >= currentLogLevel)
    {
        std::wstring taggedMessage;
        std::wostringstream woss;

        switch (level)
        {
        case LogLevel::LOG_DEBUG:
            taggedMessage = L"[DEBUG]: " + message;
            break;
        case LogLevel::LOG_INFO:
            taggedMessage = L"[INFO]: " + message;
            break;
        case LogLevel::LOG_WARNING:
            taggedMessage = L"[WARNING]: " + message;
            break;
        case LogLevel::LOG_ERROR:
            taggedMessage = L"[ERROR]: " + message;
            break;
        case LogLevel::LOG_CRITICAL:
            taggedMessage = L"[CRITICAL]: " + message;
            break;
        case LogLevel::LOG_TERMINATION:
            taggedMessage = L"[TERMINATION]: " + message;
            break;
        }

        // Always add newline for console output
        woss << taggedMessage << L"\n";
        
        #if defined(_DEBUG)
            OutputDebugStringW(woss.str().c_str());
        #endif  

        // Write unmodified (but tagged) message to the log file
        #if !defined(NO_DEBUGFILE_OUTPUT) && !defined(_DEBUG)
            Insert_Into_Log_File(LOG_FILE_NAME, taggedMessage);
        #endif

        if (level == LogLevel::LOG_CRITICAL)
        {
            #if !defined(_DEBUG) && !defined(DEBUG)
                throw std::runtime_error("Fatal Critical Error Has Occurred!");
                // Terminate the application.
                PostQuitMessage(0);
    //            DebugBreak();
            #endif
        }
    }
}

bool Debug::LOG_IF_FAILED(HRESULT hr, const LPCWSTR msg)
{
    if (FAILED(hr)) {
        std::wostringstream woss;
        woss << msg << L" (HRESULT: 0x" << std::hex << hr << L")\n";

        // Convert std::wstring to std::string for logging
        std::wstring wstr = woss.str();
        std::string str(wstr.begin(), wstr.end());

        logLevelMessage(LogLevel::LOG_ERROR, wstr);
        return false;
    }

    return true;
}

void Debug::Log(const std::string& message)
{
    // Log message to the standard output
    #ifdef _DEBUG
        OutputDebugStringA(("[INFO]: " + message).c_str()); // Also output to the debug console
    #endif
}

void Debug::LogWarning(const std::string& message)
{
    // Log warning to the standard output
    #ifdef _DEBUG
        OutputDebugStringA(("[WARNING]: " + message + "\n").c_str()); // Also output to the debug console
    #endif
}

void Debug::LogError(const std::string& message) {

    // Log error to the standard output
    #ifdef _DEBUG
        OutputDebugStringA(("[ERROR]: " + message + "\n").c_str()); // Also output to the debug console
    #endif
}

void Debug::LogFunction(const std::string& functionName, const std::string& message) {
    #ifdef _DEBUG
        // Log function-specific messages
        std::string fullMessage = "[Function: " + functionName + "] " + message;
        #ifdef _DEBUG
            OutputDebugStringA(fullMessage.c_str()); // Also output to the debug console
        #endif
    #endif
}

void Debug::DebugBreak() {
    // Causes the debugger to break into the code at this point
    #ifdef _DEBUG
        __debugbreak(); // Will break if debugging is enabled
    #endif
}

void Debug::SetLogLevel(LogLevel level) {
	currentLogLevel = level;
}