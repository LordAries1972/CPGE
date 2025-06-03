// -------------------------------------------------------------------------------------------------------------
// FileIO.h - High-Performance File Input/Output Operations Manager
// 
// Purpose: Provides thread-safe file operations with command queuing and priority processing.
//          Supports multiple platforms (Windows, Linux, MacOS, Android, iOS) with conditional compilation.
//          Integrates with PUNPack for file compression/decompression and ThreadManager for thread safety.
//
// Features:
// - Priority-based command queue processing
// - Cross-platform file operations with conditional compilation
// - Integration with PUNPack compression system
// - Thread-safe operations using ThreadManager and ThreadLockHelper
// - Comprehensive error handling and status reporting
// - Production-ready code with full debugging support
// -------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Debug.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
#include "PUNPack.h"

#include <string>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>
#include <chrono>
#include <fstream>
#include <memory>
#include <functional>

// Platform-specific includes with conditional compilation
#if defined(_WIN64) || defined(_WIN32)
#include <windows.h>                                                // Windows file operations
#include <direct.h>                                                 // Windows directory operations
#include <io.h>                                                     // Windows I/O operations
#elif defined(__linux__)
#include <unistd.h>                                                 // Linux POSIX operations
#include <sys/stat.h>                                               // Linux file statistics
#include <dirent.h>                                                 // Linux directory operations
#include <fcntl.h>                                                  // Linux file control operations
#elif defined(__APPLE__)
#include <unistd.h>                                                 // macOS POSIX operations
#include <sys/stat.h>                                               // macOS file statistics
#include <dirent.h>                                                 // macOS directory operations
#include <fcntl.h>                                                  // macOS file control operations
#elif defined(__ANDROID__)
#include <unistd.h>                                                 // Android POSIX operations
#include <sys/stat.h>                                               // Android file statistics
#include <dirent.h>                                                 // Android directory operations
#include <fcntl.h>                                                  // Android file control operations
#elif defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
#include <unistd.h>                                                 // iOS POSIX operations
#include <sys/stat.h>                                               // iOS file statistics
#include <dirent.h>                                                 // iOS directory operations
#include <fcntl.h>                                                  // iOS file control operations
#endif

// Forward declarations
extern Debug debug;
extern ThreadManager threadManager;

//==============================================================================
// Constants and Configuration
//==============================================================================
const int FILEIO_MAX_QUEUE_SIZE = 1024;                                // Maximum number of queued file operations
const int FILEIO_THREAD_SLEEP_MS = 10;                                 // Thread sleep duration when no tasks are available
const int FILEIO_LOCK_TIMEOUT_MS = 5000;                               // Default lock timeout in milliseconds
const size_t FILEIO_MAX_BUFFER_SIZE = 0x7FFFFFFF;                      // Maximum file buffer size (2GB)
const std::string FILEIO_QUEUE_LOCK = "fileio_queue_lock";             // Lock name for queue operations
const std::string FILEIO_ERROR_LOCK = "fileio_error_lock";             // Lock name for error operations

//==============================================================================
// Enumerations and Types
//==============================================================================

// File operation commands for queue processing
enum class FileIOCommand : uint32_t {
    CMD_NONE = 0,                                                       // No operation
    CMD_DELETE_FILE = 1,                                                // Delete file operation
    CMD_GET_FILE_SIZE = 2,                                              // Get file size operation
    CMD_APPEND_TO_FILE = 3,                                             // Append data to file operation
    CMD_FILE_EXISTS = 4,                                                // Check file existence operation
    CMD_STREAM_WRITE_FILE = 5,                                          // Stream write file operation
    CMD_STREAM_READ_FILE = 6,                                           // Stream read file operation
    CMD_GET_CURRENT_DIRECTORY = 7,                                      // Get current directory operation
    CMD_RENAME_FILE = 8,                                                // Rename file operation
    CMD_DELETE_LINE_IN_FILE = 9,                                        // Delete line in ASCII file operation
    CMD_COPY_FILE_TO = 10,                                              // Copy file operation
    CMD_MOVE_FILE_TO = 11                                               // Move file operation
};

// Task priority levels for queue processing
enum class FileIOPriority : uint8_t {
    PRIORITY_LOW = 0,                                                   // Low priority - background operations
    PRIORITY_NORMAL = 1,                                                // Normal priority - standard operations
    PRIORITY_HIGH = 2,                                                  // High priority - important operations
    PRIORITY_CRITICAL = 3                                               // Critical priority - urgent operations
};

// File type specifications for operations
enum class FileIOType : uint8_t {
    TYPE_ASCII = 0,                                                     // ASCII text file type
    TYPE_BINARY = 1                                                     // Binary file type
};

// File position for append and line deletion operations
enum class FileIOPosition : uint8_t {
    POSITION_FRONT = 0,                                                 // Front/beginning of file
    POSITION_END = 1                                                    // End of file
};

// Error type codes for comprehensive error reporting
enum class FileIOErrorType : uint32_t {
    ERROR_NONE = 0,                                                     // No error occurred
    ERROR_FILE_NOTFOUND = 1,                                            // File does not exist
    ERROR_ACCESSDENIED = 2,                                             // Access permission denied
    ERROR_DISKFULL = 3,                                                 // Insufficient disk space
    ERROR_FILE_LOCKED = 4,                                              // File is locked by another process
    ERROR_INVALID_PARAM = 5,                                            // Invalid function parameter
    ERROR_MEMORY_ALLOCATION = 6,                                        // Memory allocation failed
    ERROR_PUNPACK_FAILED = 7,                                           // PUNPack compression/decompression failed
    ERROR_THREAD_LOCK_FAILED = 8,                                       // Thread lock acquisition failed
    ERROR_PLATFORM_SPECIFIC = 9,                                        // Platform-specific error
    ERROR_UNKNOWN = 999                                                 // Unknown error occurred
};

//==============================================================================
// Data Structures
//==============================================================================

// Error status structure for comprehensive error reporting
struct FileIOErrorStatus {
    int taskID;                                                         // Unique task identifier
    std::string filename;                                               // File name involved in operation
    std::string directory;                                              // Directory path involved in operation
    FileIOCommand taskCommand;                                          // Command that caused the error
    std::string errorTypeText;                                          // Human-readable error description
    FileIOErrorType errorTypeCode;                                      // Numeric error code
    std::chrono::steady_clock::time_point errorTime;                    // Time when error occurred
    std::string platformErrorMessage;                                   // Platform-specific error message
    uint32_t platformErrorCode;                                         // Platform-specific error code

    // Constructor with default initialization
    FileIOErrorStatus() : taskID(0), taskCommand(FileIOCommand::CMD_NONE),
        errorTypeCode(FileIOErrorType::ERROR_NONE), errorTime(std::chrono::steady_clock::now()),
        platformErrorCode(0) {
    }
};

// Task data structure for queue processing
struct FileIOTaskData {
    int taskID;                                                         // Unique task identifier
    FileIOCommand command;                                              // Operation command to execute
    FileIOPriority priority;                                            // Task priority level
    std::string primaryFilename;                                        // Primary file name for operation
    std::string secondaryFilename;                                      // Secondary file name (for copy/move/rename)
    std::string directoryPath;                                          // Directory path for operations
    std::vector<uint8_t> writeBuffer;                                   // Data buffer for write operations
    std::vector<uint8_t> readBuffer;                                    // Data buffer for read operations
    FileIOType fileType;                                                // File type (ASCII/Binary)
    FileIOPosition position;                                            // Position for append/delete operations
    bool shouldPUNPack;                                                 // Whether to use PUNPack compression
    bool isCompleted;                                                   // Task completion status
    bool wasSuccessful;                                                 // Task success status
    std::chrono::steady_clock::time_point createTime;                   // Task creation time
    std::chrono::steady_clock::time_point completeTime;                 // Task completion time
    FileIOErrorStatus errorStatus;                                      // Error information if task failed

    // Constructor with default initialization
    FileIOTaskData() : taskID(0), command(FileIOCommand::CMD_NONE),
        priority(FileIOPriority::PRIORITY_NORMAL), fileType(FileIOType::TYPE_BINARY),
        position(FileIOPosition::POSITION_END), shouldPUNPack(false),
        isCompleted(false), wasSuccessful(false),
        createTime(std::chrono::steady_clock::now()) {
    }
};

// Priority comparison functor for priority queue
struct FileIOTaskComparator {
    bool operator()(const std::shared_ptr<FileIOTaskData>& a, const std::shared_ptr<FileIOTaskData>& b) const {
        // Higher priority values have higher precedence (processed first)
        if (a->priority != b->priority) {
            return static_cast<uint8_t>(a->priority) < static_cast<uint8_t>(b->priority);
        }
        // If priorities are equal, process older tasks first (FIFO within same priority)
        return a->createTime > b->createTime;
    }
};

//==============================================================================
// FileIO Class Declaration
//==============================================================================
class FileIO {
public:
    // Constructor and destructor
    FileIO();                                                           // Initialize FileIO system
    ~FileIO();                                                          // Clean up FileIO resources

    // Initialization and cleanup
    bool Initialize();                                                  // Initialize FileIO subsystem
    void Cleanup();                                                     // Clean up all FileIO resources
    bool IsInitialized() const { return m_isInitialized.load(); }      // Check initialization status

    //==========================================================================
    // Public File Operation Interface
    //==========================================================================

    // File management operations
    bool DeleteFile(const std::string& filename, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool GetFileSize(const std::string& filename, size_t& fileSize, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool FileExists(const std::string& filename, bool& exists, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool RenameFile(const std::string& existingFilename, const std::string& newFilename, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool CopyFileTo(const std::string& filename, const std::string& newFilename, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool MoveFileTo(const std::string& filename, const std::string& filepath, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());

    // File content operations
    bool AppendToFile(const std::string& filename, const std::vector<uint8_t>& data, FileIOType fileType, FileIOPosition position, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool StreamWriteFile(const std::string& filename, const std::vector<uint8_t>& writeBuffer, bool shouldPack, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool StreamReadFile(const std::string& filename, std::vector<uint8_t>& readBuffer, bool shouldUnpack, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());
    bool DeleteLineInFile(const std::string& filename, FileIOPosition lineType, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());

    // Directory operations
    bool GetCurrentDirectory(std::string& currentPath, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL, int& taskID = GetDummyTaskID());

    //==========================================================================
    // Task Queue Management Interface
    //==========================================================================

    // Task injection and management
    bool InjectFileIOTask(FileIOCommand command, const std::vector<uint8_t>& buffers, bool shouldPUNPack, int& taskID, FileIOPriority priority = FileIOPriority::PRIORITY_NORMAL);
    bool IsFileIOTaskCompleted(int taskID, bool& taskSuccess, bool& isReady);
    FileIOErrorStatus GetErrorStatus(int taskID);

    // Queue status and control
    size_t GetQueueSize() const;                                        // Get current queue size
    void ClearQueue();                                                  // Clear all pending tasks
    bool IsQueueEmpty() const;                                          // Check if queue is empty
    bool HasPendingWriteTasks() const;                                  // Check if there are any pending write operations in queue
    size_t GetPendingWriteTaskCount() const;                            // Get count of pending write operations in queue

    //==========================================================================
    // Thread Management Interface
    //==========================================================================

    // Thread control
    bool StartFileIOThread();                                           // Start file processing thread
    void StopFileIOThread();                                            // Stop file processing thread
    bool IsThreadRunning() const { return m_threadRunning.load(); }    // Check if thread is running
    void FileIOTaskingThread();                                         // Main thread processing function

    //==========================================================================
    // Statistics and Monitoring Interface
    //==========================================================================

    // Performance monitoring
    struct FileIOStatistics {
        uint64_t totalTasksProcessed;                                   // Total tasks processed
        uint64_t totalTasksSuccessful;                                  // Total successful tasks
        uint64_t totalTasksFailed;                                      // Total failed tasks
        uint64_t totalBytesRead;                                        // Total bytes read from files
        uint64_t totalBytesWritten;                                     // Total bytes written to files
        float averageTaskProcessingTime;                                // Average task processing time in milliseconds
        std::chrono::steady_clock::time_point sessionStartTime;         // Session start time

        // Constructor with default initialization
        FileIOStatistics() : totalTasksProcessed(0), totalTasksSuccessful(0),
            totalTasksFailed(0), totalBytesRead(0), totalBytesWritten(0),
            averageTaskProcessingTime(0.0f),
            sessionStartTime(std::chrono::steady_clock::now()) {
        }
    };

    FileIOStatistics GetStatistics() const;                             // Get current statistics
    void ResetStatistics();                                             // Reset all statistics

private:
    //==========================================================================
    // Private Member Variables
    //==========================================================================

    // Initialization and state management
    std::atomic<bool> m_isInitialized;                                  // FileIO initialization status
    std::atomic<bool> m_hasCleanedUp;                                   // Cleanup completion status
    std::atomic<bool> m_threadRunning;                                  // Thread execution status

    // Task queue and management
    std::priority_queue<std::shared_ptr<FileIOTaskData>, std::vector<std::shared_ptr<FileIOTaskData>>, FileIOTaskComparator> m_taskQueue;
    std::unordered_map<int, std::shared_ptr<FileIOTaskData>> m_completedTasks; // Completed tasks for status queries
    std::atomic<int> m_nextTaskID;                                      // Next available task ID

    // Error management
    std::unordered_map<int, FileIOErrorStatus> m_errorStatusMap;        // Error status tracking

    // Statistics tracking
    mutable FileIOStatistics m_statistics;                              // Performance statistics

    // PUNPack integration
    std::unique_ptr<PUNPack> m_punpack;                                 // PUNPack instance for compression

    //==========================================================================
    // Private Helper Functions
    //==========================================================================

    // Task management
    int GenerateNextTaskID();                                           // Generate unique task ID
    std::shared_ptr<FileIOTaskData> CreateTaskData(FileIOCommand command, FileIOPriority priority); // Create task data structure
    bool EnqueueTask(std::shared_ptr<FileIOTaskData> taskData);         // Add task to queue
    std::shared_ptr<FileIOTaskData> DequeueTask();                      // Get next task from queue
    void CompleteTask(std::shared_ptr<FileIOTaskData> taskData, bool success); // Mark task as completed

    // File operation implementations
    bool ExecuteDeleteFile(std::shared_ptr<FileIOTaskData> taskData);   // Execute delete file operation
    bool ExecuteGetFileSize(std::shared_ptr<FileIOTaskData> taskData);  // Execute get file size operation
    bool ExecuteAppendToFile(std::shared_ptr<FileIOTaskData> taskData); // Execute append to file operation
    bool ExecuteFileExists(std::shared_ptr<FileIOTaskData> taskData);   // Execute file exists check operation
    bool ExecuteStreamWriteFile(std::shared_ptr<FileIOTaskData> taskData); // Execute stream write file operation
    bool ExecuteStreamReadFile(std::shared_ptr<FileIOTaskData> taskData); // Execute stream read file operation
    bool ExecuteGetCurrentDirectory(std::shared_ptr<FileIOTaskData> taskData); // Execute get current directory operation
    bool ExecuteRenameFile(std::shared_ptr<FileIOTaskData> taskData);   // Execute rename file operation
    bool ExecuteDeleteLineInFile(std::shared_ptr<FileIOTaskData> taskData); // Execute delete line in file operation
    bool ExecuteCopyFileTo(std::shared_ptr<FileIOTaskData> taskData);   // Execute copy file operation
    bool ExecuteMoveFileTo(std::shared_ptr<FileIOTaskData> taskData);   // Execute move file operation

    // Platform-specific implementations
#if defined(_WIN64) || defined(_WIN32)
    bool DeleteFileWindows(const std::string& filename);            // Windows-specific delete file
    size_t GetFileSizeWindows(const std::string& filename);         // Windows-specific get file size
    bool FileExistsWindows(const std::string& filename);            // Windows-specific file exists check
    std::string GetCurrentDirectoryWindows();                       // Windows-specific get current directory
    bool RenameFileWindows(const std::string& oldName, const std::string& newName); // Windows-specific rename file
    bool CopyFileWindows(const std::string& source, const std::string& dest); // Windows-specific copy file
    bool MoveFileWindows(const std::string& source, const std::string& dest); // Windows-specific move file
#endif

#if defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
    bool DeleteFileUnix(const std::string& filename);               // Unix-based delete file
    size_t GetFileSizeUnix(const std::string& filename);            // Unix-based get file size
    bool FileExistsUnix(const std::string& filename);               // Unix-based file exists check
    std::string GetCurrentDirectoryUnix();                          // Unix-based get current directory
    bool RenameFileUnix(const std::string& oldName, const std::string& newName); // Unix-based rename file
    bool CopyFileUnix(const std::string& source, const std::string& dest); // Unix-based copy file
    bool MoveFileUnix(const std::string& source, const std::string& dest); // Unix-based move file
#endif

    // Utility functions
    bool IsASCIIFile(const std::string& filename);                      // Check if file is ASCII text
    void SetTaskError(std::shared_ptr<FileIOTaskData> taskData, FileIOErrorType errorType, const std::string& errorMessage); // Set task error information
    void UpdateStatistics(bool wasSuccessful, size_t bytesProcessed, float processingTime); // Update performance statistics
    std::string GetErrorTypeText(FileIOErrorType errorType);            // Convert error type to text
    uint32_t GetPlatformErrorCode();                                    // Get platform-specific error code
    std::string GetPlatformErrorMessage();                              // Get platform-specific error message
    bool IsWriteOperation(FileIOCommand command) const;                 // Check if command is a write operation

    // Static utility function for dummy task ID references
    static int& GetDummyTaskID() {                                      // Dummy task ID for optional parameters
        static int dummyID = 0;
        return dummyID;
    }
};

