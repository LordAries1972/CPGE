// ThreadLockHelper.h - A RAII wrapper for ThreadManager locks

#pragma once

#include "ThreadManager.h"
#include "Debug.h"

extern Debug debug;
extern ThreadManager threadManager;

/* ------------------------------------------------------------------------------------------------------------ */
// This helper class provides RAII-style management of ThreadManager locks
// It will automatically remove the lock when it goes out of scope
//
// Usage example:
/*
void SomeFunction() {
    // Try to acquire the lock with RAII
    ThreadLockHelper lock(threadManager, "my_important_lock", 1000);

    // Check if we got the lock
    if (!lock.IsLocked()) {
        // Handle failure case
        return;
    }

    // Perform thread-safe operations...

    // Lock is automatically released when 'lock' goes out of scope
}
/* ------------------------------------------------------------------------------------------------------------ */
class ThreadLockHelper {
public:
    // Constructor acquires the lock
    ThreadLockHelper(ThreadManager& tm, const std::string& lockName, int timeoutMs = 1000, bool silent = false)
        : m_threadManager(tm), m_lockName(lockName), m_isLocked(false), m_silent(silent) {
        m_isLocked = m_threadManager.TryLock(m_lockName, timeoutMs);
        if (!m_isLocked && !m_silent) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"Could not acquire lock '" + StringToWString(m_lockName) + L"' - timeout reached");
        }
    }

    // Destructor automatically releases the lock
    ~ThreadLockHelper() {
        Release();
    }

    // Check if lock was successfully acquired
    bool IsLocked() const {
        return m_isLocked;
    }

    // Manually release the lock before destruction
    void Release() {
        if (m_isLocked) {
            m_threadManager.RemoveLock(m_lockName);
            m_isLocked = false;
        }
    }

    // Prevent copying
    ThreadLockHelper(const ThreadLockHelper&) = delete;
    ThreadLockHelper& operator=(const ThreadLockHelper&) = delete;

private:
    ThreadManager& m_threadManager;
    std::string m_lockName;
    bool m_isLocked;
    bool m_silent;

    // Helper method to convert std::string to std::wstring for logging
    std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }
};

/* ------------------------------------------------------------------------------------------ */
// For acquiring multiple locks safely:
//
// Usage example:
/*
void SomeFunction() {
    // Create a helper that will manage multiple locks
    MultiThreadLockHelper locks(threadManager);

    // Try to acquire locks in sequence
    if (!locks.TryLock("first_lock") || !locks.TryLock("second_lock")) {
        // If any lock fails, all previous locks are automatically released
        return;
    }

    // Perform operations requiring both locks...

    // All locks are automatically released when 'locks' goes out of scope
}
/* ------------------------------------------------------------------------------------------ */
class MultiThreadLockHelper {
public:
    MultiThreadLockHelper(ThreadManager& tm) : m_threadManager(tm) {}

    ~MultiThreadLockHelper() {
        // Release locks in reverse order (LIFO)
        for (auto it = m_acquiredLocks.rbegin(); it != m_acquiredLocks.rend(); ++it) {
            m_threadManager.RemoveLock(*it);
        }
    }

    // Try to acquire a lock, return success status
    bool TryLock(const std::string& lockName, int timeoutMs = 1000) {
        if (m_threadManager.TryLock(lockName, timeoutMs)) {
            m_acquiredLocks.push_back(lockName);
            return true;
        }

        // Lock failed, log warning
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire lock '" + StringToWString(lockName) + L"' - timeout reached");

        // Release any locks we've already acquired
        for (auto it = m_acquiredLocks.rbegin(); it != m_acquiredLocks.rend(); ++it) {
            m_threadManager.RemoveLock(*it);
        }
        m_acquiredLocks.clear();

        return false;
    }

    // Prevent copying
    MultiThreadLockHelper(const MultiThreadLockHelper&) = delete;
    MultiThreadLockHelper& operator=(const MultiThreadLockHelper&) = delete;

private:
    ThreadManager& m_threadManager;
    std::vector<std::string> m_acquiredLocks;

    // Helper method to convert std::string to std::wstring for logging
    std::wstring StringToWString(const std::string& str) {
        if (str.empty()) return L"";
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }
};
