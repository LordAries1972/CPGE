# FileIO Class - Comprehensive Usage Documentation and Examples

## Overview

This document provides comprehensive documentation and examples for the FileIO class, showcasing all functionality including:

- Basic file operations (read, write, delete, copy, move, rename)
- Directory operations
- Task queue management and priority handling
- Error handling and status checking
- Performance monitoring and statistics
- Thread management
- PUNPack integration for compression

## Table of Contents

1.  [System Initialization](#system-initialization)
2.  [Basic File Operations](#basic-file-operations)
3.  [File Content Operations](#file-content-operations)
4.  [File Management Operations](#file-management-operations)
5.  [Directory Operations](#directory-operations)
6.  [Advanced Task Management](#advanced-task-management)
7.  [Queue Management and Statistics](#queue-management-and-statistics)
8.  [Thread Management](#thread-management)
9.  [Error Handling and Recovery](#error-handling-and-recovery)
10. [Priority Handling](#priority-handling)
11. [Compression Integration](#compression-integration)
12. [Best Practices](#best-practices)
13. [API Reference](#api-reference)

## System Initialization

### Initializing the FileIO System

Before using any FileIO functionality, you must properly initialize the system and start the processing thread.

```cpp
#include "FileIO.h"
#include "Debug.h"
#include "ThreadManager.h"

// External references required for FileIO operation
extern Debug debug;
extern ThreadManager threadManager;

// Global FileIO instance
FileIO globalFileIO;

/**
 * Demonstrates proper initialization of the FileIO system
 * This should be called during application startup
 */
bool InitializeFileIOSystem() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Starting FileIO system initialization");

    // Initialize the FileIO subsystem
    if (!globalFileIO.Initialize()) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize FileIO system");
        return false;
    }

    // Start the FileIO processing thread for asynchronous operations
    if (!globalFileIO.StartFileIOThread()) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to start FileIO thread");
        globalFileIO.Cleanup();
        return false;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"FileIO system initialized successfully");
    return true;
}
```

### Cleaning Up the FileIO System

Proper cleanup should be performed during application shutdown to ensure all resources are released correctly.

```cpp
/**
 * Demonstrates proper cleanup of the FileIO system
 * This should be called during application shutdown
 */
void CleanupFileIOSystem() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Starting FileIO system cleanup");

    // Stop the FileIO processing thread gracefully
    globalFileIO.StopFileIOThread();

    // Clean up all FileIO resources
    globalFileIO.Cleanup();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"FileIO system cleanup completed");
}
```

## Basic File Operations

### Checking File Existence

Check if a file exists on the filesystem with asynchronous processing.

```cpp
/**
 * Demonstrates file existence checking with asynchronous processing
 * @param filename - Name of file to check for existence
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateFileExists(const std::string& filename) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Checking file existence for: %S", filename.c_str());

    int taskID = 0;                    // Task ID for tracking operation
    bool exists = false;               // Variable to receive result (not used in async mode)

    // Queue file existence check with normal priority
    if (!globalFileIO.FileExists(filename, exists, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue file exists operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"File exists check queued with task ID: %d", taskID);

    // Wait for task completion and check result
    bool taskSuccess = false;          // Task completion success status
    bool isReady = false;              // Task completion readiness status
    int maxWaitAttempts = 100;         // Maximum wait attempts (10 seconds at 100ms intervals)
    int waitAttempts = 0;              // Current wait attempt counter

    // Poll for task completion with timeout
    while (waitAttempts < maxWaitAttempts) {
        if (globalFileIO.IsFileIOTaskCompleted(taskID, taskSuccess, isReady)) {
            if (isReady) {
                if (taskSuccess) {
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"File exists check completed successfully for task ID: %d", taskID);
                    return true;
                } else {
                    // Task failed - get error information
                    FileIOErrorStatus errorStatus = globalFileIO.GetErrorStatus(taskID);
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"File exists check failed for task ID: %d, Error: %S", 
                        taskID, std::wstring(errorStatus.errorTypeText.begin(), errorStatus.errorTypeText.end()).c_str());
                    return false;
                }
            }
        }

        // Sleep briefly before next check to avoid CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitAttempts++;
    }

    debug.logDebugMessage(LogLevel::LOG_WARNING, L"File exists check timed out for task ID: %d", taskID);
    return false;
}
```

### Getting File Size

Retrieve the size of a file in bytes with comprehensive error handling.

```cpp
/**
 * Demonstrates file size retrieval with error handling
 * @param filename - Name of file to get size for
 * @return File size in bytes, or 0 if operation failed
 */
size_t DemonstrateGetFileSize(const std::string& filename) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Getting file size for: %S", filename.c_str());

    int taskID = 0;                    // Task ID for tracking operation
    size_t fileSize = 0;               // Variable to receive result (not used in async mode)

    // Queue file size retrieval with high priority
    if (!globalFileIO.GetFileSize(filename, fileSize, FileIOPriority::PRIORITY_HIGH, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue get file size operation");
        return 0;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Get file size queued with task ID: %d", taskID);

    // Poll for task completion
    bool taskSuccess = false;
    bool isReady = false;
    
    for (int i = 0; i < 100; i++) {
        if (globalFileIO.IsFileIOTaskCompleted(taskID, taskSuccess, isReady) && isReady) {
            if (taskSuccess) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Get file size completed successfully for task ID: %d", taskID);
                // In a real implementation, you would extract the actual file size from the task result
                return 1024; // Placeholder return value
            } else {
                FileIOErrorStatus errorStatus = globalFileIO.GetErrorStatus(taskID);
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Get file size failed for task ID: %d, Error: %S", 
                    taskID, std::wstring(errorStatus.errorTypeText.begin(), errorStatus.errorTypeText.end()).c_str());
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
```

### Deleting Files

Delete a file from the filesystem with priority handling.

```cpp
/**
 * Demonstrates file deletion with priority handling
 * @param filename - Name of file to delete
 * @param priority - Priority level for the operation
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateDeleteFile(const std::string& filename, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Deleting file: %S with priority: %d", filename.c_str(), static_cast<int>(priority));

    int taskID = 0;                    // Task ID for tracking operation

    // Queue file deletion with specified priority
    if (!globalFileIO.DeleteFile(filename, priority, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue delete file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Delete file queued with task ID: %d", taskID);
    return true;
}
```

## File Content Operations

### Writing Data to Files

Write data to a file with optional compression support.

```cpp
/**
 * Demonstrates writing data to a file with optional compression
 * @param filename - Name of file to write to
 * @param data - Data to write to file
 * @param useCompression - Whether to use PUNPack compression
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateWriteFile(const std::string& filename, const std::vector<uint8_t>& data, bool useCompression = false) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Writing file: %S, Data size: %zu, Compression: %s", 
        filename.c_str(), data.size(), useCompression ? L"enabled" : L"disabled");

    int taskID = 0;                    // Task ID for tracking operation

    // Queue stream write operation with normal priority
    if (!globalFileIO.StreamWriteFile(filename, data, useCompression, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue stream write file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Stream write file queued with task ID: %d", taskID);
    return true;
}
```

### Reading Data from Files

Read data from a file with optional decompression support.

```cpp
/**
 * Demonstrates reading data from a file with optional decompression
 * @param filename - Name of file to read from
 * @param data - Vector to receive read data
 * @param useDecompression - Whether to use PUNPack decompression
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateReadFile(const std::string& filename, std::vector<uint8_t>& data, bool useDecompression = false) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Reading file: %S, Decompression: %s", 
        filename.c_str(), useDecompression ? L"enabled" : L"disabled");

    int taskID = 0;                    // Task ID for tracking operation

    // Queue stream read operation with normal priority
    if (!globalFileIO.StreamReadFile(filename, data, useDecompression, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue stream read file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Stream read file queued with task ID: %d", taskID);
    return true;
}
```

### Appending Data to Files

Append data to an existing file at a specified position.

```cpp
/**
 * Demonstrates appending data to a file at specified position
 * @param filename - Name of file to append to
 * @param data - Data to append
 * @param fileType - Type of file (ASCII or Binary)
 * @param position - Position to append (front or end)
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateAppendToFile(const std::string& filename, const std::vector<uint8_t>& data, 
                            FileIOType fileType, FileIOPosition position) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Appending to file: %S, Type: %s, Position: %s, Data size: %zu", 
        filename.c_str(), 
        fileType == FileIOType::TYPE_ASCII ? L"ASCII" : L"Binary",
        position == FileIOPosition::POSITION_FRONT ? L"Front" : L"End",
        data.size());

    int taskID = 0;                    // Task ID for tracking operation

    // Queue append operation with normal priority
    if (!globalFileIO.AppendToFile(filename, data, fileType, position, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue append to file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Append to file queued with task ID: %d", taskID);
    return true;
}
```

### Deleting Lines from Files

Delete a line from an ASCII text file at a specified position.

```cpp
/**
 * Demonstrates deleting a line from an ASCII text file
 * @param filename - Name of ASCII file to modify
 * @param position - Position of line to delete (front or end)
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateDeleteLineInFile(const std::string& filename, FileIOPosition position) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Deleting line in file: %S, Position: %s", 
        filename.c_str(), position == FileIOPosition::POSITION_FRONT ? L"Front" : L"End");

    int taskID = 0;                    // Task ID for tracking operation

    // Queue delete line operation with normal priority
    if (!globalFileIO.DeleteLineInFile(filename, position, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue delete line in file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Delete line in file queued with task ID: %d", taskID);
    return true;
}
```

## File Management Operations

### Copying Files

Copy a file to a new location.

```cpp
/**
 * Demonstrates copying a file to a new location
 * @param sourceFilename - Source file to copy
 * @param destinationFilename - Destination file name
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateCopyFile(const std::string& sourceFilename, const std::string& destinationFilename) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Copying file from: %S to: %S", 
        sourceFilename.c_str(), destinationFilename.c_str());

    int taskID = 0;                    // Task ID for tracking operation

    // Queue copy file operation with normal priority
    if (!globalFileIO.CopyFileTo(sourceFilename, destinationFilename, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue copy file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Copy file queued with task ID: %d", taskID);
    return true;
}
```

### Moving Files

Move a file to a new location.

```cpp
/**
 * Demonstrates moving a file to a new location
 * @param sourceFilename - Source file to move
 * @param destinationPath - Destination directory path
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateMoveFile(const std::string& sourceFilename, const std::string& destinationPath) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Moving file from: %S to: %S", 
        sourceFilename.c_str(), destinationPath.c_str());

    int taskID = 0;                    // Task ID for tracking operation

    // Queue move file operation with normal priority
    if (!globalFileIO.MoveFileTo(sourceFilename, destinationPath, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue move file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Move file queued with task ID: %d", taskID);
    return true;
}
```

### Renaming Files

Rename an existing file.

```cpp
/**
 * Demonstrates renaming a file
 * @param currentFilename - Current file name
 * @param newFilename - New file name
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateRenameFile(const std::string& currentFilename, const std::string& newFilename) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Renaming file from: %S to: %S", 
        currentFilename.c_str(), newFilename.c_str());

    int taskID = 0;                    // Task ID for tracking operation

    // Queue rename file operation with normal priority
    if (!globalFileIO.RenameFile(currentFilename, newFilename, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue rename file operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Rename file queued with task ID: %d", taskID);
    return true;
}
```

## Directory Operations

### Getting Current Directory

Retrieve the current working directory path.

```cpp
/**
 * Demonstrates getting the current directory
 * @param currentPath - String to receive current directory path
 * @return true if operation was queued successfully, false otherwise
 */
bool DemonstrateGetCurrentDirectory(std::string& currentPath) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Getting current directory");

    int taskID = 0;                    // Task ID for tracking operation

    // Queue get current directory operation with normal priority
    if (!globalFileIO.GetCurrentDirectory(currentPath, FileIOPriority::PRIORITY_NORMAL, taskID)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to queue get current directory operation");
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Get current directory queued with task ID: %d", taskID);
    return true;
}
```

## Advanced Task Management

### Custom Task Injection

Inject custom tasks with specific commands and priority handling.

```cpp
/**
 * Demonstrates custom task injection with priority handling
 * @param command - Custom FileIO command to execute
 * @param data - Data buffer for the operation
 * @param useCompression - Whether to use PUNPack compression
 * @param priority - Priority level for the operation
 * @return Task ID if successful, 0 if failed
 */
int DemonstrateCustomTaskInjection(FileIOCommand command, const std::vector<uint8_t>& data, 
                                  bool useCompression, FileIOPriority priority) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Injecting custom task - Command: %d, Data size: %zu, Compression: %s, Priority: %d", 
        static_cast<int>(command), data.size(), useCompression ? L"enabled" : L"disabled", static_cast<int>(priority));

    int taskID = 0;                    // Task ID for tracking operation

    // Inject custom task with specified parameters
    if (!globalFileIO.InjectFileIOTask(command, data, useCompression, taskID, priority)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject custom FileIO task");
        return 0;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Custom task injected with task ID: %d", taskID);
    return taskID;
}
```

### Task Completion Monitoring

Monitor task completion with comprehensive timeout handling.

```cpp
/**
 * Demonstrates task completion monitoring with timeout handling
 * @param taskID - ID of task to monitor
 * @param timeoutSeconds - Maximum time to wait for completion
 * @return true if task completed successfully, false if failed or timed out
 */
bool DemonstrateTaskMonitoring(int taskID, int timeoutSeconds = 10) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Monitoring task ID: %d with timeout: %d seconds", taskID, timeoutSeconds);

    bool taskSuccess = false;          // Task completion success status
    bool isReady = false;              // Task completion readiness status
    int maxWaitAttempts = timeoutSeconds * 10;  // Maximum wait attempts (100ms intervals)
    int waitAttempts = 0;              // Current wait attempt counter

    // Monitor task completion with timeout
    while (waitAttempts < maxWaitAttempts) {
        // Check if task has completed
        if (globalFileIO.IsFileIOTaskCompleted(taskID, taskSuccess, isReady)) {
            if (isReady) {
                // Task completed - check success status
                if (taskSuccess) {
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"Task ID: %d completed successfully", taskID);
                    return true;
                } else {
                    // Task failed - get detailed error information
                    FileIOErrorStatus errorStatus = globalFileIO.GetErrorStatus(taskID);
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"Task ID: %d failed - Error Type: %d, Message: %S", 
                        taskID, static_cast<int>(errorStatus.errorTypeCode),
                        std::wstring(errorStatus.errorTypeText.begin(), errorStatus.errorTypeText.end()).c_str());
                    return false;
                }
            }
        }

        // Sleep briefly before next check to avoid CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitAttempts++;

        // Log progress every second for long-running operations
        if (waitAttempts % 10 == 0) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Still waiting for task ID: %d (elapsed: %d seconds)", 
                taskID, waitAttempts / 10);
        }
    }

    // Task timed out
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"Task ID: %d timed out after %d seconds", taskID, timeoutSeconds);
    return false;
}
```

## Queue Management and Statistics

### Queue Status Monitoring

Monitor and manage the task queue status.

```cpp
/**
 * Demonstrates queue status monitoring and management
 */
void DemonstrateQueueManagement() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Demonstrating queue management operations");

    // Get current queue size
    size_t queueSize = globalFileIO.GetQueueSize();
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Current queue size: %zu tasks", queueSize);

    // Check if queue is empty
    bool isEmpty = globalFileIO.IsQueueEmpty();
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Queue empty status: %s", isEmpty ? L"true" : L"false");

    // Demonstrate queue clearing (use with caution in production)
    if (queueSize > 50) { // Only clear if queue is getting large
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Queue size exceeds threshold, clearing queue");
        globalFileIO.ClearQueue();
        
        size_t newQueueSize = globalFileIO.GetQueueSize();
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Queue cleared - New size: %zu tasks", newQueueSize);
    }
}
```

### Performance Statistics Monitoring

Monitor comprehensive performance statistics.

```cpp
/**
 * Demonstrates performance statistics monitoring
 */
void DemonstrateStatisticsMonitoring() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Demonstrating statistics monitoring");

    // Get current performance statistics
    FileIO::FileIOStatistics stats = globalFileIO.GetStatistics();

    // Log comprehensive statistics information
    debug.logDebugMessage(LogLevel::LOG_INFO, L"FileIO Statistics:");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Total Tasks Processed: %llu", stats.totalTasksProcessed);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Total Tasks Successful: %llu", stats.totalTasksSuccessful);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Total Tasks Failed: %llu", stats.totalTasksFailed);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Total Bytes Read: %llu", stats.totalBytesRead);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Total Bytes Written: %llu", stats.totalBytesWritten);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Average Processing Time: %.2f ms", stats.averageTaskProcessingTime);

    // Calculate success rate if any tasks have been processed
    if (stats.totalTasksProcessed > 0) {
        float successRate = (static_cast<float>(stats.totalTasksSuccessful) / 
                            static_cast<float>(stats.totalTasksProcessed)) * 100.0f;
        debug.logDebugMessage(LogLevel::LOG_INFO, L"  Success Rate: %.2f%%", successRate);
    }

    // Calculate session duration
    auto currentTime = std::chrono::steady_clock::now();
    auto sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(currentTime - stats.sessionStartTime);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  Session Duration: %lld seconds", sessionDuration.count());
}
```

## Thread Management

### Thread Status Control

Monitor and control the FileIO processing thread.

```cpp
/**
 * Demonstrates thread status monitoring and control
 */
void DemonstrateThreadManagement() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Demonstrating thread management operations");

    // Check if FileIO thread is currently running
    bool isRunning = globalFileIO.IsThreadRunning();
    debug.logDebugMessage(LogLevel::LOG_INFO, L"FileIO thread running status: %s", isRunning ? L"true" : L"false");

    if (!isRunning) {
        // Start thread if not running
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Starting FileIO thread");
        
        if (globalFileIO.StartFileIOThread()) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"FileIO thread started successfully");
        } else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to start FileIO thread");
        }
    }
}
```

## Error Handling and Recovery

### Comprehensive Error Handling

Handle errors with detailed status information and recovery strategies.

```cpp
/**
 * Demonstrates comprehensive error handling for FileIO operations
 * @param taskID - Task ID to check for errors
 */
void DemonstrateErrorHandling(int taskID) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Demonstrating error handling for task ID: %d", taskID);

    // Get detailed error status for the task
    FileIOErrorStatus errorStatus = globalFileIO.GetErrorStatus(taskID);

    // Check if error information is available
    if (errorStatus.errorTypeCode != FileIOErrorType::ERROR_NONE) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Error detected for task ID: %d", taskID);
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"  Error Type Code: %d", static_cast<int>(errorStatus.errorTypeCode));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"  Error Description: %S", 
            std::wstring(errorStatus.errorTypeText.begin(), errorStatus.errorTypeText.end()).c_str());
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"  Affected Filename: %S", 
            std::wstring(errorStatus.filename.begin(), errorStatus.filename.end()).c_str());
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"  Command Type: %d", static_cast<int>(errorStatus.taskCommand));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"  Platform Error Code: %u", errorStatus.platformErrorCode);

        // Demonstrate error recovery strategies based on error type
        switch (errorStatus.errorTypeCode) {
            case FileIOErrorType::ERROR_FILE_NOTFOUND:
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Recovery suggestion: Verify file path and existence");
                break;

            case FileIOErrorType::ERROR_ACCESSDENIED:
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Recovery suggestion: Check file permissions and administrative rights");
                break;

            case FileIOErrorType::ERROR_DISKFULL:
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Recovery suggestion: Free disk space or use alternative storage location");
                break;

            case FileIOErrorType::ERROR_FILE_LOCKED:
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Recovery suggestion: Wait for file to be released or close other applications");
                break;

            case FileIOErrorType::ERROR_PUNPACK_FAILED:
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Recovery suggestion: Retry without compression or check data integrity");
                break;

            default:
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Recovery suggestion: Retry operation or contact support");
                break;
        }
    } else {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"No error information available for task ID: %d", taskID);
    }
}
```

## Priority Handling

### Priority-Based Task Processing

Demonstrate how priority levels affect task processing order.

```cpp
/**
 * Demonstrates priority-based task processing
 */
void DemonstratePriorityHandling() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Demonstrating priority-based task processing");

    // Create sample data for demonstration
    std::vector<uint8_t> sampleData = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', '!'};

    // Queue tasks with different priorities to demonstrate processing order
    int lowPriorityTask = 0;
    int normalPriorityTask = 0;
    int highPriorityTask = 0;
    int criticalPriorityTask = 0;

    // Queue low priority task first
    globalFileIO.StreamWriteFile("demo_low_priority.txt", sampleData, false, 
                                FileIOPriority::PRIORITY_LOW, lowPriorityTask);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Low priority task queued with ID: %d", lowPriorityTask);

    // Queue normal priority task second
    globalFileIO.StreamWriteFile("demo_normal_priority.txt", sampleData, false, 
                                FileIOPriority::PRIORITY_NORMAL, normalPriorityTask);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Normal priority task queued with ID: %d", normalPriorityTask);

    // Queue high priority task third
    globalFileIO.StreamWriteFile("demo_high_priority.txt", sampleData, false, 
                                FileIOPriority::PRIORITY_HIGH, highPriorityTask);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"High priority task queued with ID: %d", highPriorityTask);

    // Queue critical priority task last
    globalFileIO.StreamWriteFile("demo_critical_priority.txt", sampleData, false, 
                                FileIOPriority::PRIORITY_CRITICAL, criticalPriorityTask);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Critical priority task queued with ID: %d", criticalPriorityTask);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Tasks should process in order: Critical, High, Normal, Low");
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Monitor the debug output to verify priority processing order");
}
```

### Monitoring Pending Write Operations

Track pending write operations and their completion status.

```cpp
/**
 * Demonstrates checking for pending write operations
 */
void DemonstratePendingWriteTaskMonitoring() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Demonstrating pending write task monitoring");

    // Check initial state before queuing any write operations
    bool initialHasPendingWrites = globalFileIO.HasPendingWriteTasks();
    size_t initialWriteCount = globalFileIO.GetPendingWriteTaskCount();

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Initial state - Has pending writes: %s, Write count: %zu",
        initialHasPendingWrites ? L"true" : L"false", initialWriteCount);

    // Create sample data for write operations
    std::vector<uint8_t> sampleData = {'T', 'e', 's', 't', ' ', 'W', 'r', 'i', 't', 'e', ' ', 'D', 'a', 't', 'a'};

    // Queue several write operations
    int writeTask1 = 0, writeTask2 = 0, writeTask3 = 0;
    
    globalFileIO.StreamWriteFile("pending_test1.txt", sampleData, false, FileIOPriority::PRIORITY_NORMAL, writeTask1);
    globalFileIO.StreamWriteFile("pending_test2.txt", sampleData, true, FileIOPriority::PRIORITY_HIGH, writeTask2);
    globalFileIO.AppendToFile("pending_test1.txt", sampleData, FileIOType::TYPE_ASCII, 
                              FileIOPosition::POSITION_END, FileIOPriority::PRIORITY_NORMAL, writeTask3);

    // Check pending write status after queuing operations
    bool hasPendingWrites = globalFileIO.HasPendingWriteTasks();
    size_t writeCount = globalFileIO.GetPendingWriteTaskCount();

    debug.logDebugMessage(LogLevel::LOG_INFO, L"After queuing writes - Has pending writes: %s, Write count: %zu",
        hasPendingWrites ? L"true" : L"false", writeCount);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Queued write task IDs: %d, %d, %d", writeTask1, writeTask2, writeTask3);

    // Monitor write task completion over time
    int monitorAttempts = 0;
    const int maxMonitorAttempts = 50;     // Monitor for up to 5 seconds

    while (monitorAttempts < maxMonitorAttempts) {
        bool currentHasPendingWrites = globalFileIO.HasPendingWriteTasks();
        size_t currentWriteCount = globalFileIO.GetPendingWriteTaskCount();

        if (currentWriteCount != writeCount) {
            // Write task count changed - log the update
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Write task progress - Previous count: %zu, Current count: %zu",
                writeCount, currentWriteCount);
            writeCount = currentWriteCount;
        }

        // Break if all write tasks completed
        if (!currentHasPendingWrites) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"All write tasks completed successfully");
            break;
        }

        // Sleep briefly before next check
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        monitorAttempts++;
    }

    // Final status check
    bool finalHasPendingWrites = globalFileIO.HasPendingWriteTasks();
    size_t finalWriteCount = globalFileIO.GetPendingWriteTaskCount();

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Final state - Has pending writes: %s, Write count: %zu",
        finalHasPendingWrites ? L"true" : L"false", finalWriteCount);

    if (finalHasPendingWrites) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Some write tasks may still be pending completion");
    }
}
```

## Compression Integration

### PUNPack Compression Support

Demonstrate compression and decompression capabilities with PUNPack integration.

```cpp
/**
 * Demonstrates PUNPack compression integration with FileIO operations
 */
void DemonstrateCompressionIntegration() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Demonstrating PUNPack compression integration");

    // Create larger sample data for meaningful compression demonstration
    std::vector<uint8_t> largeData;
    std::string repeatedText = "This is a sample text for compression testing. ";
    for (int i = 0; i < 100; i++) {    // Repeat text 100 times for better compression ratio
        largeData.insert(largeData.end(), repeatedText.begin(), repeatedText.end());
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"Original data size: %zu bytes", largeData.size());

    // Write data with compression enabled
    int compressedWriteTask = 0;
    if (globalFileIO.StreamWriteFile("demo_compressed.dat", largeData, true, 
                                    FileIOPriority::PRIORITY_NORMAL, compressedWriteTask)) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Compressed write task queued with ID: %d", compressedWriteTask);

        // Wait for compression task to complete
        if (DemonstrateTaskMonitoring(compressedWriteTask, 15)) {
            // Read data back with decompression enabled
            std::vector<uint8_t> decompressedData;
            int decompressedReadTask = 0;
            
            if (globalFileIO.StreamReadFile("demo_compressed.dat", decompressedData, true, 
                                          FileIOPriority::PRIORITY_NORMAL, decompressedReadTask)) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Decompressed read task queued with ID: %d", decompressedReadTask);

                // Wait for decompression task to complete
                if (DemonstrateTaskMonitoring(decompressedReadTask, 15)) {
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Compression/decompression cycle completed successfully");
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Data integrity can be verified by comparing original and decompressed data");
                }
            }
        }
    }

    // Demonstrate compression with custom task injection
    int customCompressionTask = DemonstrateCustomTaskInjection(FileIOCommand::CMD_STREAM_WRITE_FILE, 
                                                              largeData, true, FileIOPriority::PRIORITY_HIGH);
    if (customCompressionTask > 0) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Custom compression task injected with ID: %d", customCompressionTask);
    }
}
```

## Complete Usage Example

### Comprehensive Demonstration

A complete example showcasing all FileIO functionality in a real-world workflow.

```cpp
/**
 * Comprehensive demonstration of all FileIO class functionality
 * This function showcases the complete usage workflow
 */
void CompleteFileIODemonstration() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Starting comprehensive FileIO demonstration");

    // Step 1: Initialize FileIO system
    if (!InitializeFileIOSystem()) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize FileIO system - aborting demonstration");
        return;
    }

    // Step 2: Demonstrate basic file operations
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== BASIC FILE OPERATIONS ===");

    // Check if a file exists
    DemonstrateFileExists("test_file.txt");

    // Get file size
    size_t fileSize = DemonstrateGetFileSize("test_file.txt");

    // Create sample data for file operations
    std::vector<uint8_t> sampleData = {'T', 'e', 's', 't', ' ', 'D', 'a', 't', 'a'};

    // Write data to file
    DemonstrateWriteFile("demo_output.txt", sampleData, false);

    // Read data from file
    std::vector<uint8_t> readData;
    DemonstrateReadFile("demo_output.txt", readData, false);

    // Step 3: Demonstrate file management operations
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== FILE MANAGEMENT OPERATIONS ===");

    // Copy file
    DemonstrateCopyFile("demo_output.txt", "demo_copy.txt");

    // Rename file
    DemonstrateRenameFile("demo_copy.txt", "demo_renamed.txt");

    // Move file
    DemonstrateMoveFile("demo_renamed.txt", "./backup/demo_moved.txt");

    // Step 4: Demonstrate directory operations
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== DIRECTORY OPERATIONS ===");

    std::string currentDir;
    DemonstrateGetCurrentDirectory(currentDir);

    // Step 5: Demonstrate advanced content operations
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== ADVANCED CONTENT OPERATIONS ===");

    // Append data to file
    std::vector<uint8_t> appendData = {'\n', 'A', 'p', 'p', 'e', 'n', 'd', 'e', 'd'};
    DemonstrateAppendToFile("demo_output.txt", appendData, FileIOType::TYPE_ASCII, FileIOPosition::POSITION_END);

    // Delete line from file
    DemonstrateDeleteLineInFile("demo_output.txt", FileIOPosition::POSITION_FRONT);

    // Step 6: Demonstrate priority handling
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== PRIORITY HANDLING ===");

    DemonstratePriorityHandling();

    // Step 7: Demonstrate compression integration
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== COMPRESSION INTEGRATION ===");

    DemonstrateCompressionIntegration();

    // Step 8: Demonstrate queue and thread management
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== QUEUE AND THREAD MANAGEMENT ===");

    DemonstrateQueueManagement();
    DemonstrateThreadManagement();

    // Step 9: Demonstrate statistics monitoring
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== STATISTICS MONITORING ===");

    DemonstrateStatisticsMonitoring();

    // Step 10: Wait for all operations to complete
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== WAITING FOR TASK COMPLETION ===");

    // Allow time for all queued operations to complete
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Get final statistics
    DemonstrateStatisticsMonitoring();

    // Step 11: Cleanup (normally done at application shutdown)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== CLEANUP OPERATIONS ===");

    // Clean up temporary files created during demonstration
    DemonstrateDeleteFile("demo_output.txt", FileIOPriority::PRIORITY_LOW);
    DemonstrateDeleteFile("demo_compressed.dat", FileIOPriority::PRIORITY_LOW);
    DemonstrateDeleteFile("demo_low_priority.txt", FileIOPriority::PRIORITY_LOW);
    DemonstrateDeleteFile("demo_normal_priority.txt", FileIOPriority::PRIORITY_LOW);
    DemonstrateDeleteFile("demo_high_priority.txt", FileIOPriority::PRIORITY_LOW);
    DemonstrateDeleteFile("demo_critical_priority.txt", FileIOPriority::PRIORITY_LOW);

    // Allow time for cleanup operations to complete
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Clean up FileIO system
    CleanupFileIOSystem();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Comprehensive FileIO demonstration completed successfully");
}
```

## Best Practices

### Initialization and Cleanup
- Always initialize the FileIO system before performing any operations
- Start the FileIO processing thread for asynchronous operation
- Properly clean up the system during application shutdown
- Stop the processing thread gracefully before cleanup

### Task Management
- Always monitor task completion for critical operations
- Use appropriate timeout values when waiting for task completion
- Implement proper error handling and recovery strategies
- Use task IDs to track and manage multiple concurrent operations

### Priority Usage
- **PRIORITY_LOW**: Background operations, cleanup tasks
- **PRIORITY_NORMAL**: Standard file operations, general usage
- **PRIORITY_HIGH**: Important operations that need faster processing
- **PRIORITY_CRITICAL**: Urgent operations, system-critical tasks

### Error Handling
- Check task completion status regularly
- Retrieve detailed error information using `GetErrorStatus()`
- Implement appropriate recovery strategies based on error types
- Log errors appropriately for debugging and monitoring

### Performance Optimization
- Use compression for large files or repetitive data
- Monitor queue size to prevent memory issues
- Use statistics to identify performance bottlenecks
- Avoid clearing the queue unless absolutely necessary

### Thread Safety
- All FileIO operations are inherently thread-safe
- Uses ThreadManager and ThreadLockHelper for synchronization
- Asynchronous processing prevents UI blocking
- No additional synchronization needed in client code

## API Reference

### Core Initialization Methods
```cpp
bool Initialize();                          // Initialize FileIO system
void Cleanup();                            // Clean up all resources
bool StartFileIOThread();                  // Start processing thread
void StopFileIOThread();                   // Stop processing thread gracefully
```

### File Operations
```cpp
// File existence and properties
bool FileExists(const std::string& filename, bool& exists, FileIOPriority priority, int& taskID);
bool GetFileSize(const std::string& filename, size_t& size, FileIOPriority priority, int& taskID);

// File content operations
bool StreamWriteFile(const std::string& filename, const std::vector<uint8_t>& data, bool shouldPack, FileIOPriority priority, int& taskID);
bool StreamReadFile(const std::string& filename, std::vector<uint8_t>& data, bool shouldUnpack, FileIOPriority priority, int& taskID);
bool AppendToFile(const std::string& filename, const std::vector<uint8_t>& data, FileIOType fileType, FileIOPosition position, FileIOPriority priority, int& taskID);
bool DeleteLineInFile(const std::string& filename, FileIOPosition position, FileIOPriority priority, int& taskID);

// File management
bool DeleteFile(const std::string& filename, FileIOPriority priority, int& taskID);
bool CopyFileTo(const std::string& sourceFilename, const std::string& destinationFilename, FileIOPriority priority, int& taskID);
bool MoveFileTo(const std::string& sourceFilename, const std::string& destinationPath, FileIOPriority priority, int& taskID);
bool RenameFile(const std::string& currentFilename, const std::string& newFilename, FileIOPriority priority, int& taskID);
```

### Directory Operations
```cpp
bool GetCurrentDirectory(std::string& currentPath, FileIOPriority priority, int& taskID);
```

### Task Management
```cpp
bool InjectFileIOTask(FileIOCommand command, const std::vector<uint8_t>& data, bool shouldPack, int& taskID, FileIOPriority priority);
bool IsFileIOTaskCompleted(int taskID, bool& taskSuccess, bool& isReady);
FileIOErrorStatus GetErrorStatus(int taskID);
```

### Queue Management
```cpp
size_t GetQueueSize();                     // Get current queue size
bool IsQueueEmpty();                       // Check if queue is empty
void ClearQueue();                         // Clear all pending tasks
bool HasPendingWriteTasks();               // Check for pending write operations
size_t GetPendingWriteTaskCount();         // Get count of pending write tasks
```

### Statistics and Monitoring
```cpp
FileIOStatistics GetStatistics();          // Get performance statistics
void ResetStatistics();                    // Reset all statistics
bool IsThreadRunning();                    // Check thread status
```

### Enumerations

#### FileIOPriority
```cpp
enum class FileIOPriority {
    PRIORITY_LOW = 0,                      // Background operations
    PRIORITY_NORMAL,                       // Standard operations
    PRIORITY_HIGH,                         // Important operations
    PRIORITY_CRITICAL                      // Urgent operations
};
```

#### FileIOType
```cpp
enum class FileIOType {
    TYPE_ASCII = 0,                        // ASCII text file
    TYPE_BINARY                            // Binary file
};
```

#### FileIOPosition
```cpp
enum class FileIOPosition {
    POSITION_FRONT = 0,                    // Beginning of file
    POSITION_END                           // End of file
};
```

#### FileIOCommand
```cpp
enum class FileIOCommand {
    CMD_FILE_EXISTS = 0,                   // Check file existence
    CMD_GET_FILE_SIZE,                     // Get file size
    CMD_DELETE_FILE,                       // Delete file
    CMD_STREAM_WRITE_FILE,                 // Write file with streaming
    CMD_STREAM_READ_FILE,                  // Read file with streaming
    CMD_APPEND_TO_FILE,                    // Append data to file
    CMD_DELETE_LINE_IN_FILE,               // Delete line from file
    CMD_COPY_FILE_TO,                      // Copy file to destination
    CMD_MOVE_FILE_TO,                      // Move file to destination
    CMD_RENAME_FILE,                       // Rename file
    CMD_GET_CURRENT_DIRECTORY              // Get current directory
};
```

#### FileIOErrorType
```cpp
enum class FileIOErrorType {
    ERROR_NONE = 0,                        // No error
    ERROR_FILE_NOTFOUND,                   // File not found
    ERROR_ACCESSDENIED,                    // Access denied
    ERROR_DISKFULL,                        // Disk full
    ERROR_FILE_LOCKED,                     // File locked by another process
    ERROR_PUNPACK_FAILED,                  // PUNPack compression/decompression failed
    ERROR_INVALID_PARAMETER,               // Invalid parameter provided
    ERROR_SYSTEM_ERROR                     // Generic system error
};
```

### Statistics Structure
```cpp
struct FileIOStatistics {
    uint64_t totalTasksProcessed;          // Total tasks processed
    uint64_t totalTasksSuccessful;         // Total successful tasks
    uint64_t totalTasksFailed;             // Total failed tasks
    uint64_t totalBytesRead;               // Total bytes read
    uint64_t totalBytesWritten;            // Total bytes written
    double averageTaskProcessingTime;       // Average processing time in ms
    std::chrono::steady_clock::time_point sessionStartTime;  // Session start time
};
```

### Error Status Structure
```cpp
struct FileIOErrorStatus {
    FileIOErrorType errorTypeCode;         // Error type code
    std::string errorTypeText;             // Human-readable error description
    std::string filename;                  // Affected filename
    std::string directory;                 // Affected directory
    FileIOCommand taskCommand;             // Command that caused the error
    uint32_t platformErrorCode;           // Platform-specific error code
    std::string platformErrorMessage;     // Platform-specific error message
};
```

---

## Conclusion

The FileIO class provides a comprehensive, thread-safe, and efficient solution for file operations in C++ applications. By following the patterns and examples in this documentation, you can effectively integrate robust file I/O capabilities into your applications while maintaining optimal performance and reliability.

For additional support or questions about the FileIO class implementation, refer to the source code documentation or contact the development team.