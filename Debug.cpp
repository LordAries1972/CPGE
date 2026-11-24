#include "Includes.h"
#include "Debug.h"
#include "ConsoleWindow.h"
#include <cstdio>      // std::fwprintf (POSIX EmitMessage path)
#include <cstdlib>     // std::exit / EXIT_FAILURE (POSIX fatal path)
#include <cstdarg>     // va_start / va_copy / va_end (came via windows.h on Windows)

#if defined(PLATFORM_ANDROID)
#include "AndroidApp.h"                 // GetInternalDataPath (log file location, Phase D)
#include <android/log.h>                // __android_log_print (logcat mirror)
#endif
extern ConsoleWindow consoleWindow;

// -----------------------------------------
// ClearLogFile  --  truncate the session log to zero bytes.
// Called once from the Debug constructor before any threads exist, so no
// mutex is required here.  Platform guards select the correct API:
//   Windows  : CreateFileW with CREATE_ALWAYS (guaranteed atomic truncate)
//   POSIX    : std::ofstream with std::ios::trunc
// The operation is a no-op when NO_DEBUGFILE_OUTPUT is defined.
// -----------------------------------------
void Debug::ClearLogFile(const std::wstring& filename)
{
    #if !defined(NO_DEBUGFILE_OUTPUT)
        #if defined(_WIN32) || defined(_WIN64)
            // CREATE_ALWAYS truncates an existing file to zero bytes and succeeds
            // even when the file does not yet exist (it is created instead).
            // GENERIC_WRITE is the minimum right needed for a truncate-only open.
            HANDLE hFile = CreateFileW(
                filename.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
                CloseHandle(hFile);
        #else
            // POSIX fallback: opening with std::ios::trunc clears the file.
            // Narrow-string conversion is safe here because LOG_FILE_NAME is ASCII.
            std::ofstream ofs(std::string(filename.begin(), filename.end()),
                              std::ios::out | std::ios::trunc);
            // Destructor closes the file handle automatically.
        #endif
    #endif
}

// -----------------------------------------
// Debug constructor
// -----------------------------------------
Debug::Debug()
{
    // Truncate the log file so each application session begins with a clean slate.
    // This runs during static initialisation of the global `Debug debug;` object
    // in main.cpp, before WinMain and before any worker thread is started.
    #if !defined(NO_DEBUGFILE_OUTPUT)
        ClearLogFile(LOG_FILE_NAME);
    #endif
}

// -----------------------------------------
// Debug Logging Function
// -----------------------------------------
void Debug::DebugLog(const std::string& message)
{
    std::wstring w(message.begin(), message.end());
    logLevelMessage(LogLevel::LOG_DEBUG, w);
}

#if defined(PLATFORM_ANDROID)
// Android log file + logcat mirror (Phase D).  The Debug global is
// constructed during static initialisation, before android_main() runs, so
// the internal-storage path cannot be resolved at construction time: it is
// resolved per write from AndroidApp (empty until SetApp).  The first
// successful write of a session truncates, preserving the "clean slate per
// session" behaviour ClearLogFile provides on the other platforms (on
// Android the constructor-time truncate targets cwd "/", which is not
// writable, so it is a silent no-op).
static std::string AndroidLogFilePath()
{
    const std::string dir = AndroidApp::GetInternalDataPath();
    if (dir.empty())
        return std::string();
    return dir + "/DebugLog.txt";
}

// Mirror one log line to logcat (visible via `adb logcat -s CPGE`).  The
// tag prefix of the tagged line ("[DEBUG]: ", "[WARNING]: ", ...) selects
// the Android priority, so logcat shows the same severities as DebugLog.txt.
static void AndroidMirrorToLogcat(const std::wstring& taggedLine, const std::wstring& fullOutput)
{
    int prio = ANDROID_LOG_INFO;
    if (taggedLine.rfind(L"[DEBUG]", 0) == 0) prio = ANDROID_LOG_DEBUG;
    else if (taggedLine.rfind(L"[WARNING]", 0) == 0) prio = ANDROID_LOG_WARN;
    else if (taggedLine.rfind(L"[ERROR]", 0) == 0 || taggedLine.rfind(L"[CRITICAL]", 0) == 0 ||
             taggedLine.rfind(L"[TERMINATION]", 0) == 0) prio = ANDROID_LOG_ERROR;
    std::string narrow;
    narrow.reserve(fullOutput.size());
    for (wchar_t wc : fullOutput)
        narrow.push_back(static_cast<char>(wc));
    __android_log_print(prio, "CPGE", "%s", narrow.c_str());
}
#endif // PLATFORM_ANDROID

void Debug::Insert_Into_Log_File(const std::wstring& filename, const std::wstring& lineMsg)
{
    // Mutex prevents interleaved writes when multiple threads log simultaneously.
    // Static initialisation is thread-safe (C++11), so the mutex is always valid
    // by the time the lock_guard executes.
    static std::mutex s_logMutex;
    std::lock_guard<std::mutex> lock(s_logMutex);

    std::time_t t = std::time(nullptr);
    std::tm tm;
#if defined(PLATFORM_WINDOWS)
    localtime_s(&tm, &t);
#else
    // POSIX localtime_r takes its arguments in the opposite order to localtime_s.
    localtime_r(&t, &tm);
#endif

    wchar_t timeBuffer[100];
    wcsftime(timeBuffer, sizeof(timeBuffer) / sizeof(wchar_t), L"[%d-%m-%Y (AEST/AEDT) %H:%M:%S]: ", &tm);

    // Build the full output as a wide string, then convert to UTF-8 in one pass.
    // Writing a single byte block via WriteFile(FILE_APPEND_DATA) is atomic and
    // avoids the codecvt \r\n translation issues of std::wofstream in binary mode.
    std::wstring output;

    static bool s_sessionStarted = false;
    if (!s_sessionStarted)
    {
        s_sessionStarted = true;
        wchar_t sessionTime[100];
        wcsftime(sessionTime, sizeof(sessionTime) / sizeof(wchar_t),
                 L"%d-%m-%Y (AEST/AEDT) %H:%M:%S", &tm);
        output  = L"================================================================================\r\n";
        output += L"=== SESSION START: ";
        output += sessionTime;
        output += L" ===\r\n";
        output += L"================================================================================\r\n";
    }
    output += timeBuffer;
    output += lineMsg;
    output += L"\r\n";

#if defined(PLATFORM_WINDOWS)
    // Convert wide → UTF-8 bytes (all log messages are ASCII, so output is 1 byte/char)
    int byteCount = WideCharToMultiByte(CP_UTF8, 0,
                                         output.c_str(), static_cast<int>(output.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) return;
    std::string narrow(byteCount, '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                         output.c_str(), static_cast<int>(output.size()),
                         narrow.data(), byteCount, nullptr, nullptr);

    // FILE_APPEND_DATA: OS positions the write pointer at EOF before each WriteFile call,
    // giving correct append semantics even across multiple processes/handles.
    HANDLE hFile = CreateFileW(filename.c_str(),
                                FILE_APPEND_DATA,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hFile, narrow.c_str(), static_cast<DWORD>(narrow.size()), &written, nullptr);
    CloseHandle(hFile);
#else
    // POSIX: log messages are ASCII by construction, so a byte-wise narrow
    // conversion is exact; append in one write via std::ofstream (the caller
    // already holds the log mutex, so interleaving cannot occur).
    std::string narrow;
    narrow.reserve(output.size());
    for (wchar_t wc : output)
        narrow.push_back(static_cast<char>(wc));

#if defined(PLATFORM_ANDROID)
    // Android: mirror every line to logcat and write the file to internal
    // storage — the relative DebugLog.txt path resolves to cwd "/", which is
    // not writable on Android, so the plain ofstream below would silently
    // drop every line.  When no activity is attached yet (before SetApp),
    // the file write is skipped; logcat already has the line.
    AndroidMirrorToLogcat(lineMsg, output);
    const std::string androidPath = AndroidLogFilePath();
    if (androidPath.empty())
        return;
    static bool s_androidLogTruncated = false;
    std::ofstream ofs(androidPath,
                      std::ios::out | (s_androidLogTruncated ? std::ios::app : std::ios::trunc) | std::ios::binary);
    s_androidLogTruncated = true;
    if (ofs)
        ofs.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
#else
    std::ofstream ofs(std::string(filename.begin(), filename.end()),
                      std::ios::out | std::ios::app | std::ios::binary);
    if (ofs)
        ofs.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
#endif
#endif
}


// Formats into an exactly-sized buffer so arbitrarily long messages (e.g. GLSL
// info logs) can never trip the CRT "Buffer too small" assert in vswprintf_s.
static std::wstring FormatLogMessage(const wchar_t* format, va_list args)
{
    if (!format)
        return std::wstring();

#if defined(PLATFORM_WINDOWS)
    va_list argsCopy;
    va_copy(argsCopy, args);
    const int needed = _vscwprintf(format, argsCopy);
    va_end(argsCopy);

    if (needed < 0)
        return std::wstring(format);    // bad format string: log it raw rather than crash

    std::wstring out(static_cast<size_t>(needed) + 1, L'\0');
    const int written = vswprintf_s(out.data(), out.size(), format, args);
    out.resize(written > 0 ? static_cast<size_t>(written) : 0);
    return out;
#else
    // POSIX has no _vscwprintf and vswprintf reports truncation only as a
    // negative return, so grow the buffer until the message fits. The loop
    // is bounded (1 MiB cap) so a pathological format cannot loop forever.
    std::size_t capacity = 1024;
    for (;;)
    {
        std::wstring out(capacity, L'\0');
        va_list argsCopy;
        va_copy(argsCopy, args);
        const int written = vswprintf(out.data(), out.size(), format, argsCopy);
        va_end(argsCopy);

        if (written >= 0 && static_cast<std::size_t>(written) < capacity)
        {
            out.resize(static_cast<std::size_t>(written));
            return out;
        }
        if (written < 0 && capacity >= (1u << 20))
            return std::wstring(format);    // bad format string: log it raw rather than crash
        capacity *= 2;
    }
#endif
}

void Debug::logDebugMessage(LogLevel level, const wchar_t* format, ...)
{
    if (int(level) >= int(currentLogLevel))
    {
        va_list args;
        va_start(args, format);
        std::wstring message = FormatLogMessage(format, args);
        va_end(args);

        logLevelMessage(level, message); // logLevelMessage already writes the tagged message to the log file
    }
}

void Debug::logLevelMessage(LogLevel level, const std::wstring& message)
{
    EmitMessage(level, message, true);
}

// -----------------------------------------
// Release-visible diagnostics (see Debug.h).
// Never shows a dialog or quits.  Repeated messages are throttled per unique
// text (so warnings raised every frame, even interleaved with others, cannot
// flood the log): the first occurrence is logged, then one "[repeated N more
// times]" line every DIAG_REPEAT_FLUSH occurrences.  The tracking table is
// cleared when it reaches DIAG_MAX_TRACKED entries to bound memory.
// -----------------------------------------
void Debug::logDiagLevelMessage(LogLevel level, const std::wstring& message)
{
    if (level < currentLogLevel)
        return;

    static constexpr unsigned int DIAG_REPEAT_FLUSH = 1000;
    static constexpr size_t       DIAG_MAX_TRACKED  = 512;
    static std::mutex                                     s_diagMutex;
    static std::unordered_map<std::wstring, unsigned int> s_diagCounts;

    bool bFirst   = false;
    bool bSummary = false;
    {
        std::lock_guard<std::mutex> lock(s_diagMutex);
        auto it = s_diagCounts.find(message);
        if (it == s_diagCounts.end())
        {
            if (s_diagCounts.size() >= DIAG_MAX_TRACKED)
                s_diagCounts.clear();
            s_diagCounts.emplace(message, 0u);      // value = repeats since last line written
            bFirst = true;
        }
        else if (++it->second >= DIAG_REPEAT_FLUSH)
        {
            it->second = 0;
            bSummary   = true;
        }
    }

    // Emit outside the lock: EmitMessage takes the log-file mutex itself.
    if (bFirst)
        EmitMessage(level, message, false);
    else if (bSummary)
        EmitMessage(level, message + L" [repeated " + std::to_wstring(DIAG_REPEAT_FLUSH) + L" more times]", false);
}

void Debug::logDiagMessage(LogLevel level, const wchar_t* format, ...)
{
    if (int(level) >= int(currentLogLevel))
    {
        va_list args;
        va_start(args, format);
        std::wstring message = FormatLogMessage(format, args);
        va_end(args);

        logDiagLevelMessage(level, message);
    }
}

void Debug::EmitMessage(LogLevel level, const std::wstring& message, bool bAllowFatalUI)
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
#if defined(PLATFORM_WINDOWS)
        OutputDebugStringW(woss.str().c_str());
#else
        // No OutputDebugString on POSIX: mirror the tagged line to stderr so
        // it is visible when the engine runs from a terminal.
        std::fwprintf(stderr, L"%ls", woss.str().c_str());
#endif

        // In-game console: every level in Debug builds; WARNING and above in Release builds.
#ifndef _DEBUG
        if (level >= LogLevel::LOG_WARNING)
#endif
        {
            ConsoleLineColor clr = ConsoleLineColor::Normal;
            if (level == LogLevel::LOG_WARNING)
                clr = ConsoleLineColor::Warning;
            else if (level == LogLevel::LOG_ERROR || level == LogLevel::LOG_CRITICAL ||
                     level == LogLevel::LOG_TERMINATION)
                clr = ConsoleLineColor::Error;
            consoleWindow.AddLine(taggedMessage, clr);
        }

        // Write unmodified (but tagged) message to the log file
        #if (!defined(NO_DEBUGFILE_OUTPUT))
            Insert_Into_Log_File(LOG_FILE_NAME, taggedMessage);
        #endif

        if (bAllowFatalUI &&
            (level == LogLevel::LOG_ERROR    ||
             level == LogLevel::LOG_CRITICAL ||
             level == LogLevel::LOG_TERMINATION))
        {
#if defined(PLATFORM_WINDOWS)
            if (level == LogLevel::LOG_TERMINATION)
            {
                // TERMINATION is unrecoverable — show a dialog then force-exit.
                // ExitProcess is intentional: program state is corrupt.
                MessageBoxW(nullptr,
                    (std::wstring(L"A fatal error has occurred and the program must close.\n\n")
                        + message).c_str(),
                    L"Fatal Error — Terminating",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
                ExitProcess(EXIT_FAILURE);
            }
            else if (level == LogLevel::LOG_CRITICAL)
            {
                MessageBoxW(nullptr,
                    (std::wstring(L"A critical error has occurred:\n\n") + message +
                        L"\n\nThe application will now close.").c_str(),
                    L"Critical Error",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
            else if (level == LogLevel::LOG_ERROR)
            {
                MessageBoxW(nullptr,
                    (std::wstring(L"An error has occurred:\n\n") + message +
                        L"\n\nThe application will now close.").c_str(),
                    L"Error",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
#endif
            // Post quit AFTER the dialog is dismissed so the user has seen the message.
            // Linux/Android have no Win32 message queue: TERMINATION exits immediately
            // (as ExitProcess does on Windows), while ERROR/CRITICAL set the shared
            // quit flag so the main loop shuts the engine down cleanly instead.
#if defined(PLATFORM_WINDOWS)
            PostQuitMessage(EXIT_FAILURE);
#else
            if (level == LogLevel::LOG_TERMINATION)
                std::exit(EXIT_FAILURE);
#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
            // Linux/Android: only quit on ERROR/CRITICAL once the main loop
            // is actually running. Errors raised during startup (a missing
            // intro movie, a failed asset load before the first frame) must
            // not kill the engine before it runs; they are logged above.
            else if (g_cpgeMainLoopActive.load())
                g_cpgeQuitRequested.store(true);
#else
            else
                g_cpgeQuitRequested.store(true);
#endif
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
    std::wstring w(message.begin(), message.end());
    logLevelMessage(LogLevel::LOG_INFO, w);
}

void Debug::LogWarning(const std::string& message)
{
    std::wstring w(message.begin(), message.end());
    logLevelMessage(LogLevel::LOG_WARNING, w);
}

void Debug::LogError(const std::string& message)
{
    std::wstring w(message.begin(), message.end());
    logLevelMessage(LogLevel::LOG_ERROR, w);
}

void Debug::LogFunction(const std::string& functionName, const std::string& message)
{
    std::wstring w;
    for (char c : ("[Function: " + functionName + "] " + message)) w += (wchar_t)c;
    logLevelMessage(LogLevel::LOG_INFO, w);
}

void Debug::DebugBreak() {
    // Causes the debugger to break into the code at this point
    #ifdef _DEBUG
        #if defined(PLATFORM_WINDOWS)
            __debugbreak(); // Will break if debugging is enabled
        #elif defined(__GNUC__) || defined(__clang__)
            __builtin_trap();   // POSIX equivalent: trap into the debugger / abort
        #endif
    #endif
}

void Debug::SetLogLevel(LogLevel level) {
	currentLogLevel = level;
}