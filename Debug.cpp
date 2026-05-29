#include "Includes.h"
#include "Debug.h"
#ifdef _DEBUG
#include "ConsoleWindow.h"
extern ConsoleWindow consoleWindow;
#endif

// -----------------------------------------
// Debug Logging Function
// -----------------------------------------
void Debug::DebugLog(const std::string& message)
{
    std::ostringstream oss;
    oss << "[INFO]: " << message << "\n";
    OutputDebugStringA(oss.str().c_str());
#ifdef _DEBUG
    {
        std::wstring w(message.begin(), message.end());
        consoleWindow.AddLine(L"[INFO]: " + w, ConsoleLineColor::Normal);
    }
#endif
}

void Debug::Insert_Into_Log_File(const std::wstring& filename, const std::wstring& lineMsg)
{
    // Get current date/time
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_s(&tm, &t);

    wchar_t timeBuffer[100];
    wcsftime(timeBuffer, sizeof(timeBuffer) / sizeof(wchar_t), L"[%d-%m-%Y (AEST/AEDT) %H:%M:%S]: ", &tm);

    // Append mode: never truncates the file, so a crash never wipes previous entries.
    // Entries are written oldest-first; scroll to the bottom for the most recent output.
    std::wofstream outFile(filename, std::ios::app);
    if (!outFile.is_open()) return;

    // Write a session-start banner once per process lifetime so each run is easy to locate.
    static bool s_sessionStarted = false;
    if (!s_sessionStarted)
    {
        s_sessionStarted = true;
        wchar_t sessionTime[100];
        wcsftime(sessionTime, sizeof(sessionTime) / sizeof(wchar_t),
                 L"%d-%m-%Y (AEST/AEDT) %H:%M:%S", &tm);
        outFile << L"================================================================================\n"
                << L"=== SESSION START: " << sessionTime << L" ===\n"
                << L"================================================================================\n";
    }

    outFile << timeBuffer << lineMsg << L"\n";
    outFile.close();
}


void Debug::logDebugMessage(LogLevel level, const wchar_t* format, ...)
{
    if (int(level) >= int(currentLogLevel))
    {
        wchar_t buffer[2048];

        va_list args;
        va_start(args, format);
        vswprintf_s(buffer, format, args);
        va_end(args);

        std::wstring message(buffer);
        logLevelMessage(level, message); // logLevelMessage already writes the tagged message to the log file
    }
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
        OutputDebugStringW(woss.str().c_str());

#ifdef _DEBUG
        {
            ConsoleLineColor clr = ConsoleLineColor::Normal;
            if (level == LogLevel::LOG_WARNING)
                clr = ConsoleLineColor::Warning;
            else if (level == LogLevel::LOG_ERROR || level == LogLevel::LOG_CRITICAL)
                clr = ConsoleLineColor::Error;
            consoleWindow.AddLine(taggedMessage, clr);
        }
#endif

        // Write unmodified (but tagged) message to the log file
        #if (!defined(NO_DEBUGFILE_OUTPUT))
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
    #ifdef _DEBUG
        OutputDebugStringA(("[INFO]: " + message).c_str());
        {
            std::wstring w(message.begin(), message.end());
            consoleWindow.AddLine(L"[INFO]: " + w, ConsoleLineColor::Normal);
        }
    #endif
}

void Debug::LogWarning(const std::string& message)
{
    #ifdef _DEBUG
        OutputDebugStringA(("[WARNING]: " + message + "\n").c_str());
        {
            std::wstring w(message.begin(), message.end());
            consoleWindow.AddLine(L"[WARNING]: " + w, ConsoleLineColor::Warning);
        }
    #endif
}

void Debug::LogError(const std::string& message)
{
    #ifdef _DEBUG
        OutputDebugStringA(("[ERROR]: " + message + "\n").c_str());
        {
            std::wstring w(message.begin(), message.end());
            consoleWindow.AddLine(L"[ERROR]: " + w, ConsoleLineColor::Error);
        }
    #endif
}

void Debug::LogFunction(const std::string& functionName, const std::string& message)
{
    std::string fullMessage = "[Function: " + functionName + "] " + message;
    #ifdef _DEBUG
        OutputDebugStringA(fullMessage.c_str());
        {
            std::wstring w(fullMessage.begin(), fullMessage.end());
            consoleWindow.AddLine(w, ConsoleLineColor::Normal);
        }
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