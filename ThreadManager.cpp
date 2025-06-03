/* -------------------------------------------------------------------------------------
// ThreadManager Class Developer Guide
//
// The ThreadManager class simplifies thread management by providing safe creation,
// control, and monitoring of threads.
//
// Here’s how to use it :-
//
// =========
// 1. Setup
// =========
// Include ThreadManager.h and create an instance :
//
// #include "ThreadManager.h"
//
// ThreadManager threadManager;
//
// ===================
// 2 Creating Threads
//===================
//Use SetThread() to define and start a thread :
//
//threadManager.SetThread(THREAD_RENDERER, []() {
//    while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
//        // Task logic here
//    }
//    });
//
// Threads are named (e.g., "THREAD_RENDERER") and stored internally.
//
// =======================
// 3. Controlling Threads
// =======================
// Start / Pause / Resume / Stop:
//
// threadManager.StartThread(THREAD_RENDERER);  // Starts if not running
// threadManager.PauseThread(THREAD_RENDERER);  // Pauses execution
// threadManager.ResumeThread(THREAD_RENDERER); // Resumes from pause
// threadManager.StopThread(THREAD_RENDERER);   // Graceful stop (sets status)
//
// Terminate: Forcefully detaches a thread :
//
// threadManager.TerminateThread(THREAD_RENDERER); // Use sparingly!
//
// =============================
// 4. Thread Status & Debugging
// =============================
// Check status :
//
// ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
//
// Debug mode(auto - enabled in debug builds) :
//
// bool isDebug = threadManager.IsDebugMode(THREAD_RENDERER);
//
// ===========
// 5) Cleanup
// ===========
// Call Cleanup() to join / detach all threads(automatically called in destructor) :
//
//    threadManager.Cleanup(); // Blocks until threads finish
//
// ==========
// Key Notes
// ==========
// Thread - Safety: All operations are mutex - protected.
//
// Graceful Shutdown : Tasks should check ThreadStatus to respond to pauses / stops.
//
// Avoid Terminate : Prefer StopThread() for safe cleanup.
//
// This class ensures structured thread management with minimal boilerplate.
// Refer to DEBUG / Visual Studio 2022 console logs for runtime feedback.
// ---------------------------------------------------------------------------------------
*/
#include "Includes.h"
#include "ThreadManager.h"
#include "Debug.h"

extern Debug debug;

ThreadManager::ThreadManager() :
    bShutdownRequested(false),
    bHasCleanedUp(false),
    IsDestroying(false)
{
    debug.logLevelMessage(LogLevel::LOG_INFO, L"ThreadManager initialized.");
}

ThreadManager::~ThreadManager() {
    if (!IsDestroying) {
        IsDestroying = true;
        Cleanup();
        debug.logLevelMessage(LogLevel::LOG_INFO, L"ThreadManager destroyed.");
    }
}

std::string ThreadManager::getThreadName(ThreadNameID id) {
    switch (id) 
    {
        case THREAD_LOADER:             return "GE-Loader-Thread";
        case THREAD_RENDERER:           return "GE-Rendering-Thread";
        case THREAD_NETWORK:            return "GE-Network-Thread";
        case THREAD_AI_PROCESSING:      return "GE-AI-Thread";
        case THREAD_FILEIO:             return "GE-FileIO-Processing-Thread";
        default:                        return "Unknown";
    }
}

std::wstring ThreadManager::StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

void ThreadManager::SetThread(const ThreadNameID id, std::function<void()> task, bool debugMode) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Cannot create new thread during shutdown");
        return;
    }

    std::string name = this->getThreadName(id);
    if (threads.find(name) != threads.end()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"Thread with name '" + StringToWString(name) + L"' already exists.");
        return;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Setting up thread: " + StringToWString(name));

    ThreadInfo info = { std::thread::id(), ThreadStatus::NotStarted, debugMode };

    std::thread newThread([this, id, task, name]() {
        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            if (!bShutdownRequested) {
                auto& info = GetThreadInfoUnsafe(id);
                info.status = ThreadStatus::Running;
                info.threadID = std::this_thread::get_id();
            }
        }

        if (!bShutdownRequested) {
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"Thread '" + StringToWString(name) + L"' started.");
            task();
        }

        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            if (!bShutdownRequested) {
                auto& info = GetThreadInfoUnsafe(id);
                info.status = ThreadStatus::Stopped;
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"Thread '" + StringToWString(name) + L"' finished.");
            }
        }
        });

    threads.emplace(name, std::make_pair(std::move(newThread), info));
}

bool ThreadManager::DoesThreadExist(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return false;

    std::string name = this->getThreadName(id);
    return threads.find(name) != threads.end();
}

void ThreadManager::StartThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto it = threads.find(name);
    if (it == threads.end()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"No Thread with name '" + StringToWString(name) + L"' found!");
        return;
    }

    auto& info = it->second.second;
    info.threadID = it->second.first.get_id();
    info.status = ThreadStatus::Running;

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' started.");
}

void ThreadManager::PauseThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto& info = GetThreadInfoUnsafe(id);
    if (info.status != ThreadStatus::Running) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"Thread '" + StringToWString(name) + L"' is not Running!");
        return;
    }

    info.status = ThreadStatus::Paused;
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' paused.");
}

void ThreadManager::ResumeThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto& info = GetThreadInfoUnsafe(id);
    if (info.status != ThreadStatus::Paused) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"Thread '" + StringToWString(name) + L"' is not paused.");
        return;
    }

    info.status = ThreadStatus::Running;
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' resumed.");
    pauseCV.notify_all();
}

void ThreadManager::StopThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto& info = GetThreadInfoUnsafe(id);
    if (info.status == ThreadStatus::Stopped || info.status == ThreadStatus::Terminated) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"Thread '" + StringToWString(name) + L"' is already stopped or terminated.");
        return;
    }

    info.status = ThreadStatus::Stopped;
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' stopped.");
}

void ThreadManager::TerminateThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto it = threads.find(name);
    if (it == threads.end()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"No Thread with name '" + StringToWString(name) + L"' was found.");
        return;
    }

    if (it->second.first.joinable()) {
        it->second.first.detach();
    }

    it->second.second.status = ThreadStatus::Terminated;
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' terminated.");

    threads.erase(it);
}

void ThreadManager::Cleanup()
{
    if (bHasCleanedUp) return;

    std::unique_lock<std::mutex> lock(threadsMutex);
    bShutdownRequested = true;
    threadVars.bIsShuttingDown = true;
    lock.unlock();

    // Notify all potentially waiting threads
    pauseCV.notify_all();

    // Give threads a chance to finish gracefully
    lock.lock();
    for (auto it = threads.begin(); it != threads.end(); ) {
        auto& thread = it->second.first;
        auto& info = it->second.second;

        info.status = ThreadStatus::Stopped;

        if (thread.joinable()) {
            try {
                if (thread.get_id() != std::this_thread::get_id()) {
                    lock.unlock();
                    thread.join();
                    lock.lock();
                }
                else {
                    thread.detach();
                }
            }
            catch (const std::system_error& e) {
                debug.logLevelMessage(LogLevel::LOG_ERROR,
                    L"Error joining thread '" + StringToWString(it->first) +
                    L"': " + StringToWString(e.what()));
            }
        }
        it = threads.erase(it);
    }

    // Clean up any remaining locks
    {
        std::lock_guard<std::mutex> locksGuard(locksMutex);

        // Notify any waiting threads for all locks
        for (auto& cvPair : lockConditions) {
            cvPair.second.notify_all();
        }

        if (!locks.empty()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"Cleaning up " + std::to_wstring(locks.size()) + L" unclaimed locks.");
            locks.clear();
        }

        // Clear condition variables map
        lockConditions.clear();
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"All threads and locks cleaned up.");
    bHasCleanedUp = true;
}

ThreadStatus ThreadManager::GetThreadStatus(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return ThreadStatus::Terminated;
    return GetThreadInfoUnsafe(id).status;
}

std::thread::id ThreadManager::GetThreadID(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return std::thread::id();
    return GetThreadInfoUnsafe(id).threadID;
}

bool ThreadManager::IsDebugMode(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return false;
    return GetThreadInfoUnsafe(id).debugMode;
}

ThreadInfo& ThreadManager::GetThreadInfo(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    return GetThreadInfoUnsafe(id);
}

ThreadInfo& ThreadManager::GetThreadInfoUnsafe(const ThreadNameID id) {
    static ThreadInfo emptyThreadInfo;
    if (bShutdownRequested) return emptyThreadInfo;

    try {
        std::string name = this->getThreadName(id);
        auto it = threads.find(name);
        if (it == threads.end()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"No Thread with name '" + StringToWString(name) + L"' was found.");
            return emptyThreadInfo;
        }
        return it->second.second;
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Exception in GetThreadInfo: " + StringToWString(e.what()));
        return emptyThreadInfo;
    }
}

bool ThreadManager::CreateLock(const std::string& lockName) {
    // Lock the lock's mutex to ensure thread-safety when accessing the locks map
    std::lock_guard<std::mutex> guard(locksMutex);

    // Check if a lock with this name already exists
    if (locks.find(lockName) != locks.end()) {
        // Lock already exists, log a warning and return false
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"Lock '" + StringToWString(lockName) + L"' already exists.");
        return false;
    }

    // Create a new lock by using emplace to construct it in-place
    auto result = locks.emplace(std::piecewise_construct,
        std::forward_as_tuple(lockName),                                                                    // Key
        std::forward_as_tuple());                                                                           // Value (LockInfo constructed in-place)

    // Get a reference to the newly inserted LockInfo
    LockInfo& newLock = result.first->second;

    // Set the owner thread ID and lock state
    newLock.ownerThreadID = std::this_thread::get_id();
    // Set as locked since we're creating it
    newLock.isLocked = true; 

    return true;
}

bool ThreadManager::CheckLock(const std::string& lockName) {
    // Lock the lock's mutex to ensure thread-safety when accessing the locks map
    std::lock_guard<std::mutex> guard(locksMutex);

    // Check if the lock exists and is currently locked
    auto it = locks.find(lockName);
    if (it != locks.end() && it->second.isLocked) {
        // If the lock exists and is locked, return true
        return true;
    }

    // Lock doesn't exist or is not locked
    return false;
}

bool ThreadManager::RemoveLock(const std::string& lockName) {
    // Lock the lock's mutex to ensure thread-safety when accessing the locks map
    std::lock_guard<std::mutex> guard(locksMutex);

    // Find the lock
    auto it = locks.find(lockName);
    if (it == locks.end()) {
        // Lock doesn't exist, log a warning and return false
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Cannot remove lock '" + StringToWString(lockName) + L"' as it doesn't exist.");
        return false;
    }

    // Check if the calling thread is the owner
    if (it->second.ownerThreadID != std::this_thread::get_id()) {
        // Not the owner, log an error and return false
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Thread is not the owner of lock '" + StringToWString(lockName) + L"'.");
        return false;
    }

    // Mark the lock as unlocked before removal (important for waiting threads)
    it->second.isLocked = false;

    // Notify any waiting threads before removing the lock
    auto cvIt = lockConditions.find(lockName);
    if (cvIt != lockConditions.end()) {
        cvIt->second.notify_all();
    }

    // Remove the lock
    locks.erase(it);

    // Clean up the condition variable if it exists
    if (cvIt != lockConditions.end()) {
        lockConditions.erase(cvIt);
    }

    return true;
}

bool ThreadManager::TryLock(const std::string& lockName, int timeoutMillisecs) {
    // Prepare a unique lock for the condition variable wait
    std::unique_lock<std::mutex> guard(locksMutex);

    // Find the lock
    auto it = locks.find(lockName);
    if (it == locks.end()) {
        // Lock doesn't exist, create it using emplace to construct in-place
        auto result = locks.emplace(std::piecewise_construct,
            std::forward_as_tuple(lockName),  // Key
            std::forward_as_tuple());         // Value (LockInfo constructed in-place)

        // Get a reference to the newly inserted LockInfo
        LockInfo& newLock = result.first->second;

        // Set the owner thread ID and lock state
        newLock.ownerThreadID = std::this_thread::get_id();
        newLock.isLocked = true;

        return true;
    }

    // Lock exists, check if it's locked
    LockInfo& lock = it->second;
    if (lock.isLocked) {
        // Already locked by another thread

        // If no timeout specified, return immediately
        if (timeoutMillisecs <= 0) {
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TryLock failed - lock '" + StringToWString(lockName) + L"' is already locked.");
            return false;
        }

        // With timeout, wait for the lock to be released
        debug.logLevelMessage(LogLevel::LOG_DEBUG,
            L"TryLock waiting for lock '" + StringToWString(lockName) +
            L"' with timeout " + std::to_wstring(timeoutMillisecs) + L"ms.");

        // Create a condition variable for this specific lock if it doesn't exist
        auto& lockCV = lockConditions[lockName];

        // Wait for the lock to be released or timeout
        bool lockAcquired = lockCV.wait_for(guard,
            std::chrono::milliseconds(timeoutMillisecs),
            [&lock]() { return !lock.isLocked; });

        // If we timed out and the lock is still locked, return false
        if (!lockAcquired) {
            debug.logLevelMessage(LogLevel::LOG_DEBUG,
                L"TryLock timeout - lock '" + StringToWString(lockName) +
                L"' is still locked after " + std::to_wstring(timeoutMillisecs) + L"ms.");
            return false;
        }

        // Lock was released during our wait, so we can acquire it
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"Lock '" + StringToWString(lockName) + L"' acquired via TryLock after waiting.");
    }

    // Lock exists but is not locked (or was released during our wait), acquire it
    lock.isLocked = true;
    lock.ownerThreadID = std::this_thread::get_id();
    return true;
}
