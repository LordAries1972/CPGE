// -------------------------------------------------------------------------------------------------------------
// FileIO.cpp - Implementation of high-performance file input/output operations manager
// Provides comprehensive file operations with thread-safe command queuing and priority processing
// 
// VERY IMPORTANT: DO NOT USE THE Debug Class for any debug output here as Debug class depends on this
// class.  If you do, you will encounter circular problems leading to a stack overflow!
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "FileIO.h"
#include "ThreadLockHelper.h"

// External reference declarations
extern ThreadManager threadManager;

#pragma warning(push)
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

// Constructor - Initialize all member variables to safe defaults
FileIO::FileIO() :
    m_isInitialized(false),                                             // FileIO subsystem not yet initialized
    m_hasCleanedUp(false),                                              // Cleanup not yet performed
    m_threadRunning(false),                                             // Processing thread not running
    m_nextTaskID(1),                                                    // Start task IDs at 1
    m_punpack(nullptr)                                                  // PUNPack instance not yet created
{
    // Initialize statistics with default values
    m_statistics = FileIOStatistics();
}

// Destructor - Ensure proper cleanup of all file I/O resources
FileIO::~FileIO() {
    // Perform cleanup if not already done
    if (!m_hasCleanedUp.load()) {
        Cleanup();
    }
}

// Initialize the FileIO subsystem and prepare for file operations
bool FileIO::Initialize() {
    // Prevent double initialization
    if (m_isInitialized.load()) {
        return true;
    }

    try {
        // Initialize PUNPack compression system
        m_punpack = std::make_unique<PUNPack>();
        if (!m_punpack->Initialize()) {
            return false;
        }

        // Reset all statistics to zero
        ResetStatistics();

        // Clear any existing task queues and error maps
        ClearQueue();
        m_errorStatusMap.clear();
        m_completedTasks.clear();

        // Mark as successfully initialized
        m_isInitialized.store(true);
        m_hasCleanedUp.store(false);

        return true;
    }
    catch (const std::exception& e) {
        return false;
    }
}

// Clean up all FileIO resources and shutdown processing thread
void FileIO::Cleanup() {
    // Prevent double cleanup
    if (m_hasCleanedUp.load()) {
        return;
    }

    // Stop processing thread if running
    if (m_threadRunning.load()) {
        StopFileIOThread();
    }

    // Clear all task queues and maps with thread safety
    ClearQueue();
    m_completedTasks.clear();
    m_errorStatusMap.clear();

    // Cleanup PUNPack system
    if (m_punpack) {
        m_punpack->Cleanup();
        m_punpack.reset();
    }

    // Reset initialization state
    m_isInitialized.store(false);
    m_hasCleanedUp.store(true);
}

// Start the dedicated file processing thread
bool FileIO::StartFileIOThread() {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Don't start if already running
    if (m_threadRunning.load()) {
        return true;
    }

    try {
        // Set thread running flag
        m_threadRunning.store(true);

        // Create and start FileIO thread using ThreadManager
        if (!threadManager.DoesThreadExist(THREAD_FILEIO)) {
            threadManager.SetThread(THREAD_FILEIO, [this]() { FileIOTaskingThread(); });
        }

        threadManager.StartThread(THREAD_FILEIO);

        return true;
    }
    catch (const std::exception& e) {
        m_threadRunning.store(false);
        return false;
    }
}

// Stop the file processing thread gracefully
void FileIO::StopFileIOThread() {
    // Signal thread to stop
    m_threadRunning.store(false);

    // Stop thread through ThreadManager
    if (threadManager.DoesThreadExist(THREAD_FILEIO)) {
        threadManager.StopThread(THREAD_FILEIO);
    }
}

// Delete file operation with cross-platform support
bool FileIO::DeleteFile(const std::string& filename, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate filename parameter
    if (filename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_DELETE_FILE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Get file size operation with cross-platform support
bool FileIO::GetFileSize(const std::string& filename, size_t& fileSize, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate filename parameter
    if (filename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_GET_FILE_SIZE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Check file existence operation with cross-platform support
bool FileIO::FileExists(const std::string& filename, bool& exists, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate filename parameter
    if (filename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_FILE_EXISTS, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Append data to file operation with ASCII/Binary support
bool FileIO::AppendToFile(const std::string& filename, const std::vector<uint8_t>& data, FileIOType fileType, FileIOPosition position, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate parameters
    if (filename.empty() || data.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_APPEND_TO_FILE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskData->writeBuffer = data;
    taskData->fileType = fileType;
    taskData->position = position;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Stream write file operation with optional PUNPack compression
bool FileIO::StreamWriteFile(const std::string& filename, const std::vector<uint8_t>& writeBuffer, bool shouldPack, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate parameters
    if (filename.empty() || writeBuffer.empty()) {
        return false;
    }

    // Check buffer size limit
    if (writeBuffer.size() > FILEIO_MAX_BUFFER_SIZE) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_STREAM_WRITE_FILE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskData->writeBuffer = writeBuffer;
    taskData->shouldPUNPack = shouldPack;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Stream read file operation with optional PUNPack decompression
bool FileIO::StreamReadFile(const std::string& filename, std::vector<uint8_t>& readBuffer, bool shouldUnpack, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate filename parameter
    if (filename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_STREAM_READ_FILE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskData->shouldPUNPack = shouldUnpack;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Get current directory operation with cross-platform support
bool FileIO::GetCurrentDirectory(std::string& currentPath, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_GET_CURRENT_DIRECTORY, priority);
    if (!taskData) {
        return false;
    }

    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Rename file operation with cross-platform support
bool FileIO::RenameFile(const std::string& existingFilename, const std::string& newFilename, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate parameters
    if (existingFilename.empty() || newFilename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_RENAME_FILE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = existingFilename;
    taskData->secondaryFilename = newFilename;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Delete line in ASCII file operation
bool FileIO::DeleteLineInFile(const std::string& filename, FileIOPosition lineType, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate filename parameter
    if (filename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_DELETE_LINE_IN_FILE, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskData->position = lineType;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Copy file operation with cross-platform support
bool FileIO::CopyFileTo(const std::string& filename, const std::string& newFilename, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate parameters
    if (filename.empty() || newFilename.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_COPY_FILE_TO, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskData->secondaryFilename = newFilename;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Move file operation with cross-platform support
bool FileIO::MoveFileTo(const std::string& filename, const std::string& filepath, FileIOPriority priority, int& taskID) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate parameters
    if (filename.empty() || filepath.empty()) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(FileIOCommand::CMD_MOVE_FILE_TO, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->primaryFilename = filename;
    taskData->directoryPath = filepath;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Inject custom FileIO task into processing queue
bool FileIO::InjectFileIOTask(FileIOCommand command, const std::vector<uint8_t>& buffers, bool shouldPUNPack, int& taskID, FileIOPriority priority) {
    // Ensure FileIO is initialized
    if (!m_isInitialized.load()) {
        return false;
    }

    // Validate command parameter
    if (command == FileIOCommand::CMD_NONE) {
        return false;
    }

    // Create task data
    auto taskData = CreateTaskData(command, priority);
    if (!taskData) {
        return false;
    }

    // Set task-specific parameters
    taskData->writeBuffer = buffers;
    taskData->shouldPUNPack = shouldPUNPack;
    taskID = taskData->taskID;

    // Enqueue the task
    return EnqueueTask(taskData);
}

// Check if specific task has completed processing
bool FileIO::IsFileIOTaskCompleted(int taskID, bool& taskSuccess, bool& isReady) {
    // Initialize output parameters
    taskSuccess = false;
    isReady = false;

    // Validate task ID
    if (taskID <= 0) {
        return false;
    }

    // Check completed tasks map with thread safety
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return false;
    }

    // Search for task in completed tasks map
    auto taskIt = m_completedTasks.find(taskID);
    if (taskIt != m_completedTasks.end()) {
        // Task found in completed tasks
        isReady = taskIt->second->isCompleted;
        taskSuccess = taskIt->second->wasSuccessful;

        return true;
    }

    return false;
}

// Get error status for specific task ID
FileIOErrorStatus FileIO::GetErrorStatus(int taskID) {
    FileIOErrorStatus errorStatus;

    // Validate task ID
    if (taskID <= 0) {
        return errorStatus;
    }

    // Check error status map with thread safety
    ThreadLockHelper errorLock(threadManager, FILEIO_ERROR_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!errorLock.IsLocked()) {
        return errorStatus;
    }

    // Search for error status in map
    auto errorIt = m_errorStatusMap.find(taskID);
    if (errorIt != m_errorStatusMap.end()) {
        errorStatus = errorIt->second;

    }
    else {
    }

    return errorStatus;
}

// Get current queue size
size_t FileIO::GetQueueSize() const {
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return 0;
    }

    return m_taskQueue.size();
}

// Clear all pending tasks from queue
void FileIO::ClearQueue() {
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return;
    }

    // Clear the priority queue by creating a new empty one
    std::priority_queue<std::shared_ptr<FileIOTaskData>, std::vector<std::shared_ptr<FileIOTaskData>>, FileIOTaskComparator> emptyQueue;
    m_taskQueue.swap(emptyQueue);
}

// Check if queue is empty
bool FileIO::IsQueueEmpty() const {
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return true; // Return true to be safe
    }

    return m_taskQueue.empty();
}

// Check if there are any pending write tasks in the queue
bool FileIO::HasPendingWriteTasks() const {
    // Acquire queue lock for thread-safe access
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return false;
    }

    // Create a temporary copy of the queue for iteration without modifying original
    std::priority_queue<std::shared_ptr<FileIOTaskData>, std::vector<std::shared_ptr<FileIOTaskData>>, FileIOTaskComparator> tempQueue = m_taskQueue;

    // Iterate through all tasks in the queue to find write operations
    while (!tempQueue.empty()) {
        std::shared_ptr<FileIOTaskData> currentTask = tempQueue.top();
        tempQueue.pop();

        // Check if current task is a write operation
        if (currentTask && IsWriteOperation(currentTask->command)) {
            return true;
        }
    }

    return false;
}

// Get the count of pending write tasks in the queue
size_t FileIO::GetPendingWriteTaskCount() const {
    // Acquire queue lock for thread-safe access
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return 0;
    }

    size_t writeTaskCount = 0;                                          // Counter for write operations found

    // Create a temporary copy of the queue for iteration without modifying original
    std::priority_queue<std::shared_ptr<FileIOTaskData>, std::vector<std::shared_ptr<FileIOTaskData>>, FileIOTaskComparator> tempQueue = m_taskQueue;

    // Iterate through all tasks in the queue to count write operations
    while (!tempQueue.empty()) {
        std::shared_ptr<FileIOTaskData> currentTask = tempQueue.top();
        tempQueue.pop();

        // Check if current task is a write operation and increment counter
        if (currentTask && IsWriteOperation(currentTask->command)) {
            writeTaskCount++;
        }
    }

    return writeTaskCount;
}

// Helper function to determine if a FileIO command is a write operation
bool FileIO::IsWriteOperation(FileIOCommand command) const {
    // Check if the command involves writing data to storage
    switch (command) {
        case FileIOCommand::CMD_STREAM_WRITE_FILE:                      // Stream write operation - writes data to file
            return true;
        case FileIOCommand::CMD_APPEND_TO_FILE:                         // Append operation - writes data to existing file
            return true;
        case FileIOCommand::CMD_COPY_FILE_TO:                           // Copy operation - creates new file with written data
            return true;
        case FileIOCommand::CMD_MOVE_FILE_TO:                           // Move operation - may involve writing to new location
            return true;
        case FileIOCommand::CMD_RENAME_FILE:                            // Rename operation - may involve file system writes
            return true;
        case FileIOCommand::CMD_DELETE_LINE_IN_FILE:                    // Delete line operation - modifies and writes file content
            return true;

            // Read-only operations that do not modify storage
        case FileIOCommand::CMD_STREAM_READ_FILE:                       // Read operation - only reads from file
            return false;
        case FileIOCommand::CMD_GET_FILE_SIZE:                          // Size query - only reads file metadata
            return false;
        case FileIOCommand::CMD_FILE_EXISTS:                            // Existence check - only queries file system
            return false;
        case FileIOCommand::CMD_GET_CURRENT_DIRECTORY:                  // Directory query - only reads system information
            return false;
        case FileIOCommand::CMD_DELETE_FILE:                            // Delete operation - removes file but doesn't write new data
            return false;

            // Unknown or invalid commands
        case FileIOCommand::CMD_NONE:                                   // No operation specified
        default:
            return false;
    }
}

// Get current performance statistics
FileIO::FileIOStatistics FileIO::GetStatistics() const {
    return m_statistics;
}

// Reset all performance statistics
void FileIO::ResetStatistics() {
    m_statistics = FileIOStatistics();
}

// Generate unique task ID
int FileIO::GenerateNextTaskID() {
    return m_nextTaskID.fetch_add(1);
}

// Create new task data structure
std::shared_ptr<FileIOTaskData> FileIO::CreateTaskData(FileIOCommand command, FileIOPriority priority) {
    try {
        auto taskData = std::make_shared<FileIOTaskData>();
        taskData->taskID = GenerateNextTaskID();
        taskData->command = command;
        taskData->priority = priority;
        taskData->createTime = std::chrono::steady_clock::now();

        return taskData;
    }
    catch (const std::exception& e) {
        return nullptr;
    }
}

// Add task to priority queue
bool FileIO::EnqueueTask(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return false;
    }

    // Check queue size limit
    if (m_taskQueue.size() >= FILEIO_MAX_QUEUE_SIZE) {
        return false;
    }

    // Add task to priority queue
    m_taskQueue.push(taskData);

    return true;
}

// Get next task from priority queue
std::shared_ptr<FileIOTaskData> FileIO::DequeueTask() {
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (!queueLock.IsLocked()) {
        return nullptr;
    }

    // Check if queue is empty
    if (m_taskQueue.empty()) {
        return nullptr;
    }

    // Get highest priority task
    auto taskData = m_taskQueue.top();
    m_taskQueue.pop();

    return taskData;
}

// Mark task as completed and store results
void FileIO::CompleteTask(std::shared_ptr<FileIOTaskData> taskData, bool success) {
    if (!taskData) {
        return;
    }

    // Set completion status
    taskData->isCompleted = true;
    taskData->wasSuccessful = success;
    taskData->completeTime = std::chrono::steady_clock::now();

    // Store in completed tasks map with thread safety
    ThreadLockHelper queueLock(threadManager, FILEIO_QUEUE_LOCK, FILEIO_LOCK_TIMEOUT_MS);
    if (queueLock.IsLocked()) {
        m_completedTasks[taskData->taskID] = taskData;
    }

    // Store error status if task failed
    if (!success) {
        ThreadLockHelper errorLock(threadManager, FILEIO_ERROR_LOCK, FILEIO_LOCK_TIMEOUT_MS);
        if (errorLock.IsLocked()) {
            m_errorStatusMap[taskData->taskID] = taskData->errorStatus;
        }
    }
}

// Execute delete file operation with platform-specific implementation
bool FileIO::ExecuteDeleteFile(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // Use platform-specific delete implementation
        #if defined(_WIN64) || defined(_WIN32)
            result = DeleteFileWindows(taskData->primaryFilename);
        #elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            result = DeleteFileUnix(taskData->primaryFilename);
        #else
            SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
            return false;
        #endif

        if (!result) {
            SetTaskError(taskData, FileIOErrorType::ERROR_FILE_NOTFOUND, "Failed to delete file");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during delete operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute get file size operation with platform-specific implementation
bool FileIO::ExecuteGetFileSize(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        size_t fileSize = 0;

        // Use platform-specific file size implementation
        #if defined(_WIN64) || defined(_WIN32)
            fileSize = GetFileSizeWindows(taskData->primaryFilename);
        #elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            fileSize = GetFileSizeUnix(taskData->primaryFilename);
        #else
            SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
            return false;
        #endif

        if (fileSize != SIZE_MAX) {
            // Store file size in read buffer as bytes
            taskData->readBuffer.resize(sizeof(size_t));
            memcpy(taskData->readBuffer.data(), &fileSize, sizeof(size_t));
            result = true;
        }
        else {
            SetTaskError(taskData, FileIOErrorType::ERROR_FILE_NOTFOUND, "Failed to get file size");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during get file size operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute file exists check operation with platform-specific implementation
bool FileIO::ExecuteFileExists(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        bool exists = false;

        // Use platform-specific file exists implementation
        #if defined(_WIN64) || defined(_WIN32)
            exists = FileExistsWindows(taskData->primaryFilename);
        #elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            exists = FileExistsUnix(taskData->primaryFilename);
        #else
            SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
        return false;
        #endif

        // Store existence result in read buffer
        taskData->readBuffer.resize(sizeof(bool));
        memcpy(taskData->readBuffer.data(), &exists, sizeof(bool));
        result = true;
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during file exists check: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute append to file operation with ASCII/Binary support
bool FileIO::ExecuteAppendToFile(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // Determine file open mode based on file type and position
        std::ios::openmode openMode = std::ios::binary;
        if (taskData->fileType == FileIOType::TYPE_ASCII) {
            openMode = std::ios::out;
        }
        else {
            openMode = std::ios::binary;
        }

        if (taskData->position == FileIOPosition::POSITION_END) {
            openMode |= std::ios::app; // Append to end
        }
        else {
            // For front insertion, we need to read existing content first
            std::vector<uint8_t> existingContent;
            std::ifstream existingFile(taskData->primaryFilename, std::ios::binary);
            if (existingFile.is_open()) {
                existingFile.seekg(0, std::ios::end);
                size_t fileSize = existingFile.tellg();
                existingFile.seekg(0, std::ios::beg);

                if (fileSize > 0) {
                    existingContent.resize(fileSize);
                    existingFile.read(reinterpret_cast<char*>(existingContent.data()), fileSize);
                }
                existingFile.close();
            }

            // Write new data followed by existing content
            std::ofstream outFile(taskData->primaryFilename, openMode | std::ios::trunc);
            if (outFile.is_open()) {
                // Write new data first
                outFile.write(reinterpret_cast<const char*>(taskData->writeBuffer.data()), taskData->writeBuffer.size());

                // Write existing content after new data
                if (!existingContent.empty()) {
                    outFile.write(reinterpret_cast<const char*>(existingContent.data()), existingContent.size());
                }

                outFile.close();
                result = true;
            }
            else {
                SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to open file for front insertion");
            }
        }

        if (taskData->position == FileIOPosition::POSITION_END) {
            // Standard append operation
            std::ofstream outFile(taskData->primaryFilename, openMode);
            if (outFile.is_open()) {
                outFile.write(reinterpret_cast<const char*>(taskData->writeBuffer.data()), taskData->writeBuffer.size());
                outFile.close();
                result = true;
            }
            else {
                SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to open file for append");
            }
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during append operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute stream write file operation with optional PUNPack compression
bool FileIO::ExecuteStreamWriteFile(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        std::vector<uint8_t> dataToWrite = taskData->writeBuffer;

        // Compress data if requested
        if (taskData->shouldPUNPack && m_punpack) {
            PackResult packResult = m_punpack->PackBuffer(dataToWrite, CompressionType::HYBRID, true);
            if (packResult.IsValid()) {
                dataToWrite = packResult.compressedData;
            }
            else {
                SetTaskError(taskData, FileIOErrorType::ERROR_PUNPACK_FAILED, "Failed to compress data");
                return false;
            }
        }

        // Write data to file
        std::ofstream outFile(taskData->primaryFilename, std::ios::binary | std::ios::trunc);
        if (outFile.is_open()) {
            outFile.write(reinterpret_cast<const char*>(dataToWrite.data()), dataToWrite.size());
            outFile.close();
            result = true;

        }
        else {
            SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to open file for writing");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during stream write operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute stream read file operation with optional PUNPack decompression
bool FileIO::ExecuteStreamReadFile(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // Read file data
        std::ifstream inFile(taskData->primaryFilename, std::ios::binary);
        if (inFile.is_open()) {
            // Get file size
            inFile.seekg(0, std::ios::end);
            size_t fileSize = inFile.tellg();
            inFile.seekg(0, std::ios::beg);

            if (fileSize > 0) {
                // Read file content
                std::vector<uint8_t> fileData(fileSize);
                inFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);
                inFile.close();

                // Decompress data if requested
                if (taskData->shouldPUNPack && m_punpack) {
                    PackResult packResult;
                    packResult.compressedData = fileData;
                    packResult.compressedSize = fileData.size();
                    // Note: In a real implementation, you would need to store pack metadata
                    // This is a simplified version

                    UnpackResult unpackResult = m_punpack->UnpackBuffer(packResult);
                    if (unpackResult.success) {
                        taskData->readBuffer = unpackResult.data;
                    }
                    else {
                        SetTaskError(taskData, FileIOErrorType::ERROR_PUNPACK_FAILED, "Failed to decompress data: " + unpackResult.errorMessage);
                        return false;
                    }
                }
                else {
                    taskData->readBuffer = fileData;
                }

                result = true;
            }
            else {
                // File is empty
                taskData->readBuffer.clear();
                result = true;
            }
        }
        else {
            SetTaskError(taskData, FileIOErrorType::ERROR_FILE_NOTFOUND, "Failed to open file for reading");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during stream read operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute get current directory operation with platform-specific implementation
bool FileIO::ExecuteGetCurrentDirectory(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        std::string currentPath;

        // Use platform-specific current directory implementation
        #if defined(_WIN64) || defined(_WIN32)
            currentPath = GetCurrentDirectoryWindows();
        #elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            currentPath = GetCurrentDirectoryUnix();
        #else
            SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
            return false;
        #endif

        if (!currentPath.empty()) {
            // Store path in read buffer
            taskData->readBuffer.assign(currentPath.begin(), currentPath.end());
            result = true;
        }
        else {
            SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Failed to get current directory");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during get current directory operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute rename file operation with platform-specific implementation
bool FileIO::ExecuteRenameFile(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // Use platform-specific rename implementation
        #if defined(_WIN64) || defined(_WIN32)
            result = RenameFileWindows(taskData->primaryFilename, taskData->secondaryFilename);
        #elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
            result = RenameFileUnix(taskData->primaryFilename, taskData->secondaryFilename);
        #else
            SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
            return false;
        #endif

        if (!result) {
            SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to rename file");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during rename operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute delete line in file operation for ASCII files
bool FileIO::ExecuteDeleteLineInFile(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // First verify file is ASCII/text format
        if (!IsASCIIFile(taskData->primaryFilename)) {
            SetTaskError(taskData, FileIOErrorType::ERROR_INVALID_PARAM, "File is not ASCII/text format");
            return false;
        }

        // Read all lines from file
        std::ifstream inFile(taskData->primaryFilename);
        if (!inFile.is_open()) {
            SetTaskError(taskData, FileIOErrorType::ERROR_FILE_NOTFOUND, "Failed to open file for reading");
            return false;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(inFile, line)) {
            lines.push_back(line);
        }
        inFile.close();

        if (lines.empty()) {
            // File is empty, nothing to delete
            result = true;
        }
        else {
            // Delete appropriate line based on position
            if (taskData->position == FileIOPosition::POSITION_FRONT && !lines.empty()) {
                lines.erase(lines.begin()); // Remove first line
            }
            else if (taskData->position == FileIOPosition::POSITION_END && !lines.empty()) {
                lines.pop_back(); // Remove last line
            }

            // Write remaining lines back to file
            std::ofstream outFile(taskData->primaryFilename, std::ios::trunc);
            if (outFile.is_open()) {
                for (size_t i = 0; i < lines.size(); ++i) {
                    outFile << lines[i];
                    if (i < lines.size() - 1) {
                        outFile << "\n"; // Add newline except for last line
                    }
                }
                outFile.close();
                result = true;
            }
            else {
                SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to open file for writing");
            }
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during delete line operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute copy file operation with platform-specific implementation
bool FileIO::ExecuteCopyFileTo(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // Use platform-specific copy implementation
#if defined(_WIN64) || defined(_WIN32)
        result = CopyFileWindows(taskData->primaryFilename, taskData->secondaryFilename);
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
        result = CopyFileUnix(taskData->primaryFilename, taskData->secondaryFilename);
#else
        SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
        return false;
#endif

        if (!result) {
            SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to copy file");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during copy operation: " + errorMsg);
        result = false;
    }

    return result;
}

// Execute move file operation with platform-specific implementation
bool FileIO::ExecuteMoveFileTo(std::shared_ptr<FileIOTaskData> taskData) {
    if (!taskData) {
        return false;
    }

    bool result = false;

    try {
        // Use platform-specific move implementation
#if defined(_WIN64) || defined(_WIN32)
        result = MoveFileWindows(taskData->primaryFilename, taskData->directoryPath);
#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
        result = MoveFileUnix(taskData->primaryFilename, taskData->directoryPath);
#else
        SetTaskError(taskData, FileIOErrorType::ERROR_PLATFORM_SPECIFIC, "Platform not supported");
        return false;
#endif

        if (!result) {
            SetTaskError(taskData, FileIOErrorType::ERROR_ACCESSDENIED, "Failed to move file");
        }
    }
    catch (const std::exception& e) {
        std::string errorMsg = e.what();
        SetTaskError(taskData, FileIOErrorType::ERROR_UNKNOWN, "Exception during move operation: " + errorMsg);
        result = false;
    }

    return result;
}

//==============================================================================
// Windows-specific platform implementations
//==============================================================================

#if defined(_WIN64) || defined(_WIN32)

// Windows-specific delete file implementation
bool FileIO::DeleteFileWindows(const std::string& filename) {
    // Convert std::string to wide string for Windows API
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, NULL, 0);
    std::wstring wFilename(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, &wFilename[0], size_needed);

    // Use Windows API to delete file
    BOOL result = ::DeleteFileW(wFilename.c_str());

    if (!result) {
        DWORD error = ::GetLastError();
    }

    return result != FALSE;
}

// Windows-specific get file size implementation
size_t FileIO::GetFileSizeWindows(const std::string& filename) {
    // Convert std::string to wide string for Windows API
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, NULL, 0);
    std::wstring wFilename(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, &wFilename[0], size_needed);

    // Get file attributes to check if file exists and get size
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (::GetFileAttributesExW(wFilename.c_str(), GetFileExInfoStandard, &fileData)) {
        // Combine high and low parts of file size
        LARGE_INTEGER fileSize;
        fileSize.LowPart = fileData.nFileSizeLow;
        fileSize.HighPart = fileData.nFileSizeHigh;
        return static_cast<size_t>(fileSize.QuadPart);
    }

    DWORD error = ::GetLastError();
    return SIZE_MAX; // Return maximum size_t value to indicate error
}

// Windows-specific file exists check implementation
bool FileIO::FileExistsWindows(const std::string& filename) {
    // Convert std::string to wide string for Windows API
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, NULL, 0);
    std::wstring wFilename(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, &wFilename[0], size_needed);

    // Check if file exists using Windows API
    DWORD attributes = ::GetFileAttributesW(wFilename.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

// Windows-specific get current directory implementation
std::string FileIO::GetCurrentDirectoryWindows() {
    // Get required buffer size for current directory
    DWORD bufferSize = ::GetCurrentDirectoryW(0, NULL);
    if (bufferSize == 0) {
        DWORD error = ::GetLastError();
        return "";
    }

    // Allocate buffer and get current directory
    std::wstring wCurrentDir(bufferSize, 0);
    DWORD result = ::GetCurrentDirectoryW(bufferSize, &wCurrentDir[0]);
    if (result == 0 || result >= bufferSize) {
        DWORD error = ::GetLastError();
        return "";
    }

    // Remove null terminator if present
    if (!wCurrentDir.empty() && wCurrentDir.back() == L'\0') {
        wCurrentDir.pop_back();
    }

    // Convert wide string to std::string
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wCurrentDir.c_str(), -1, NULL, 0, NULL, NULL);
    std::string currentDir(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wCurrentDir.c_str(), -1, &currentDir[0], size_needed, NULL, NULL);

    // Remove null terminator if present
    if (!currentDir.empty() && currentDir.back() == '\0') {
        currentDir.pop_back();
    }

    return currentDir;
}

// Windows-specific rename file implementation
bool FileIO::RenameFileWindows(const std::string& oldName, const std::string& newName) {
    // Convert std::string to wide strings for Windows API
    int size_needed_old = MultiByteToWideChar(CP_UTF8, 0, oldName.c_str(), -1, NULL, 0);
    std::wstring wOldName(size_needed_old, 0);
    MultiByteToWideChar(CP_UTF8, 0, oldName.c_str(), -1, &wOldName[0], size_needed_old);

    int size_needed_new = MultiByteToWideChar(CP_UTF8, 0, newName.c_str(), -1, NULL, 0);
    std::wstring wNewName(size_needed_new, 0);
    MultiByteToWideChar(CP_UTF8, 0, newName.c_str(), -1, &wNewName[0], size_needed_new);

    // Use Windows API to move/rename file
    BOOL result = ::MoveFileW(wOldName.c_str(), wNewName.c_str());

    if (!result) {
        DWORD error = ::GetLastError();
    }

    return result != FALSE;
}

// Windows-specific copy file implementation
bool FileIO::CopyFileWindows(const std::string& source, const std::string& dest) {
    // Convert std::string to wide strings for Windows API
    int size_needed_src = MultiByteToWideChar(CP_UTF8, 0, source.c_str(), -1, NULL, 0);
    std::wstring wSource(size_needed_src, 0);
    MultiByteToWideChar(CP_UTF8, 0, source.c_str(), -1, &wSource[0], size_needed_src);

    int size_needed_dst = MultiByteToWideChar(CP_UTF8, 0, dest.c_str(), -1, NULL, 0);
    std::wstring wDest(size_needed_dst, 0);
    MultiByteToWideChar(CP_UTF8, 0, dest.c_str(), -1, &wDest[0], size_needed_dst);

    // Use Windows API to copy file
    BOOL result = ::CopyFileW(wSource.c_str(), wDest.c_str(), FALSE); // FALSE = overwrite if exists

    if (!result) {
        DWORD error = ::GetLastError();
    }

    return result != FALSE;
}

// Windows-specific move file implementation
bool FileIO::MoveFileWindows(const std::string& source, const std::string& dest) {
    // Convert std::string to wide strings for Windows API
    int size_needed_src = MultiByteToWideChar(CP_UTF8, 0, source.c_str(), -1, NULL, 0);
    std::wstring wSource(size_needed_src, 0);
    MultiByteToWideChar(CP_UTF8, 0, source.c_str(), -1, &wSource[0], size_needed_src);

    int size_needed_dst = MultiByteToWideChar(CP_UTF8, 0, dest.c_str(), -1, NULL, 0);
    std::wstring wDest(size_needed_dst, 0);
    MultiByteToWideChar(CP_UTF8, 0, dest.c_str(), -1, &wDest[0], size_needed_dst);

    // Use Windows API to move file with replace existing option
    BOOL result = ::MoveFileExW(wSource.c_str(), wDest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);

    if (!result) {
        DWORD error = ::GetLastError();
    }

    return result != FALSE;
}

#endif // Windows-specific implementations

//==============================================================================
// Unix-based platform implementations (Linux, macOS, Android, iOS)
//==============================================================================

#if defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__) || defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)

// Unix-based delete file implementation
bool FileIO::DeleteFileUnix(const std::string& filename) {
    // Use POSIX unlink function to delete file
    int result = unlink(filename.c_str());

    if (result != 0) {
        int error = errno;
    }

    return result == 0;
}

// Unix-based get file size implementation
size_t FileIO::GetFileSizeUnix(const std::string& filename) {
    // Use stat function to get file information
    struct stat statBuf;
    if (stat(filename.c_str(), &statBuf) == 0) {
        return static_cast<size_t>(statBuf.st_size);
    }

    int error = errno;
    return SIZE_MAX; // Return maximum size_t value to indicate error
}

// Unix-based file exists check implementation
bool FileIO::FileExistsUnix(const std::string& filename) {
    // Use access function to check if file exists and is readable
    return access(filename.c_str(), F_OK) == 0;
}

// Unix-based get current directory implementation
std::string FileIO::GetCurrentDirectoryUnix() {
    // Get current working directory using getcwd
    char* buffer = getcwd(nullptr, 0); // Let getcwd allocate buffer
    if (buffer != nullptr) {
        std::string currentDir(buffer);
        free(buffer); // Free the allocated buffer
        return currentDir;
    }

    int error = errno;
    return "";
}

// Unix-based rename file implementation
bool FileIO::RenameFileUnix(const std::string& oldName, const std::string& newName) {
    // Use POSIX rename function
    int result = rename(oldName.c_str(), newName.c_str());

    if (result != 0) {
        int error = errno;
    }

    return result == 0;
}

// Unix-based copy file implementation
bool FileIO::CopyFileUnix(const std::string& source, const std::string& dest) {
    // Open source file for reading
    int sourceFile = open(source.c_str(), O_RDONLY);
    if (sourceFile == -1) {
        int error = errno;
        return false;
    }

    // Get source file permissions
    struct stat statBuf;
    if (fstat(sourceFile, &statBuf) != 0) {
        int error = errno;
        close(sourceFile);
        return false;
    }

    // Create destination file with same permissions
    int destFile = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, statBuf.st_mode);
    if (destFile == -1) {
        int error = errno;
        close(sourceFile);
        return false;
    }

    // Copy file contents
    const size_t bufferSize = 4096;
    char buffer[bufferSize];
    ssize_t bytesRead;
    bool success = true;

    while ((bytesRead = read(sourceFile, buffer, bufferSize)) > 0) {
        ssize_t bytesWritten = write(destFile, buffer, bytesRead);
        if (bytesWritten != bytesRead) {
            int error = errno;
            success = false;
            break;
        }
    }

    if (bytesRead == -1) {
        int error = errno;
        success = false;
    }

    // Close files
    close(sourceFile);
    close(destFile);

    return success;
}

// Unix-based move file implementation
bool FileIO::MoveFileUnix(const std::string& source, const std::string& dest) {
    // Try rename first (fastest if on same filesystem)
    if (rename(source.c_str(), dest.c_str()) == 0) {
        return true;
    }

    // If rename failed, try copy and delete
    if (CopyFileUnix(source, dest)) {
        if (unlink(source.c_str()) == 0) {
            return true;
        }
        else {
            // Copy succeeded but delete failed - clean up destination
            unlink(dest.c_str());
        }
    }

    int error = errno;
    return false;
}

#endif // Unix-based implementations

//==============================================================================
// Utility Functions Implementation
//==============================================================================

// Check if file is ASCII text format
bool FileIO::IsASCIIFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read first 1024 bytes to determine if file is text
    const size_t sampleSize = 1024;
    char buffer[sampleSize];
    file.read(buffer, sampleSize);
    size_t bytesRead = file.gcount();
    file.close();

    if (bytesRead == 0) {
        return true; // Empty file is considered ASCII
    }

    // Check for null bytes and non-printable characters
    for (size_t i = 0; i < bytesRead; ++i) {
        unsigned char c = static_cast<unsigned char>(buffer[i]);

        // Allow common text characters: printable ASCII, tab, newline, carriage return
        if (c == 0 || (c < 32 && c != 9 && c != 10 && c != 13) || c > 126) {
            // Found binary character
            return false;
        }
    }

    return true; // All characters are text-compatible
}

// Set error information for failed task
void FileIO::SetTaskError(std::shared_ptr<FileIOTaskData> taskData, FileIOErrorType errorType, const std::string& errorMessage) {
    if (!taskData) {
        return;
    }

    // Set error information in task data
    taskData->errorStatus.taskID = taskData->taskID;
    taskData->errorStatus.filename = taskData->primaryFilename;
    taskData->errorStatus.directory = taskData->directoryPath;
    taskData->errorStatus.taskCommand = taskData->command;
    taskData->errorStatus.errorTypeCode = errorType;
    taskData->errorStatus.errorTypeText = GetErrorTypeText(errorType);
    taskData->errorStatus.errorTime = std::chrono::steady_clock::now();
    taskData->errorStatus.platformErrorCode = GetPlatformErrorCode();
    taskData->errorStatus.platformErrorMessage = GetPlatformErrorMessage();

    // Add custom error message
    if (!errorMessage.empty()) {
        taskData->errorStatus.errorTypeText += ": " + errorMessage;
    }
}

// Update performance statistics
void FileIO::UpdateStatistics(bool wasSuccessful, size_t bytesProcessed, float processingTime) {
    // Update atomic counters
    m_statistics.totalTasksProcessed++;
    if (wasSuccessful) {
        m_statistics.totalTasksSuccessful++;
        m_statistics.totalBytesRead += bytesProcessed; // Simplified - in real implementation, separate read/write
    }
    else {
        m_statistics.totalTasksFailed++;
    }

    // Update average processing time (simple moving average)
    if (m_statistics.totalTasksProcessed == 1) {
        m_statistics.averageTaskProcessingTime = processingTime;
    }
    else {
        m_statistics.averageTaskProcessingTime =
            (m_statistics.averageTaskProcessingTime * 0.9f) + (processingTime * 0.1f);
    }
}

// Convert error type enumeration to human-readable text
std::string FileIO::GetErrorTypeText(FileIOErrorType errorType) {
    switch (errorType) {
        case FileIOErrorType::ERROR_NONE:
            return "No error";
        case FileIOErrorType::ERROR_FILE_NOTFOUND:
            return "File not found";
        case FileIOErrorType::ERROR_ACCESSDENIED:
            return "Access denied";
        case FileIOErrorType::ERROR_DISKFULL:
            return "Insufficient disk space";
        case FileIOErrorType::ERROR_FILE_LOCKED:
            return "File is locked";
        case FileIOErrorType::ERROR_INVALID_PARAM:
            return "Invalid parameter";
        case FileIOErrorType::ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case FileIOErrorType::ERROR_PUNPACK_FAILED:
            return "Compression/decompression failed";
        case FileIOErrorType::ERROR_THREAD_LOCK_FAILED:
            return "Thread lock acquisition failed";
        case FileIOErrorType::ERROR_PLATFORM_SPECIFIC:
            return "Platform-specific error";
        case FileIOErrorType::ERROR_UNKNOWN:
        default:
            return "Unknown error";
    }
}

// Get platform-specific error code (also needs correction)
uint32_t FileIO::GetPlatformErrorCode() {
    #if defined(_WIN64) || defined(_WIN32)
        return static_cast<uint32_t>(::GetLastError());
    #else
        return static_cast<uint32_t>(errno);
    #endif
}

// Get platform-specific error message (also needs correction)
std::string FileIO::GetPlatformErrorMessage() {
    #if defined(_WIN64) || defined(_WIN32)
        DWORD errorCode = ::GetLastError();
        if (errorCode == 0) {
            return "No error";
        }

        LPWSTR messageBuffer = nullptr;
        DWORD size = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPWSTR)&messageBuffer, 0, NULL);

        if (size > 0 && messageBuffer) {
            // Convert wide string to std::string
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, messageBuffer, -1, NULL, 0, NULL, NULL);
            std::string message(size_needed, 0);
            WideCharToMultiByte(CP_UTF8, 0, messageBuffer, -1, &message[0], size_needed, NULL, NULL);

            // Remove null terminator if present
            if (!message.empty() && message.back() == '\0') {
                message.pop_back();
            }

            ::LocalFree(messageBuffer);
            return message;
        }

        return "Unknown Windows error";
    #else
        int errorCode = errno;
        if (errorCode == 0) {
            return "No error";
        }

        return std::string(strerror(errorCode));
    #endif
}

// Main file processing thread function
void FileIO::FileIOTaskingThread() {
    // Main processing loop
    while (m_threadRunning.load() && threadManager.GetThreadStatus(THREAD_FILEIO) == ThreadStatus::Running &&
        !threadManager.threadVars.bIsShuttingDown.load()) {
        try {
            // Get next task from queue
            std::shared_ptr<FileIOTaskData> currentTask = DequeueTask();

            if (currentTask) {
                // Record task start time for performance monitoring
                auto taskStartTime = std::chrono::high_resolution_clock::now();

                // Execute the appropriate operation based on command type
                bool taskSuccess = false;
                switch (currentTask->command) {
                case FileIOCommand::CMD_DELETE_FILE:
                    taskSuccess = ExecuteDeleteFile(currentTask);
                    break;
                case FileIOCommand::CMD_GET_FILE_SIZE:
                    taskSuccess = ExecuteGetFileSize(currentTask);
                    break;
                case FileIOCommand::CMD_APPEND_TO_FILE:
                    taskSuccess = ExecuteAppendToFile(currentTask);
                    break;
                case FileIOCommand::CMD_FILE_EXISTS:
                    taskSuccess = ExecuteFileExists(currentTask);
                    break;
                case FileIOCommand::CMD_STREAM_WRITE_FILE:
                    taskSuccess = ExecuteStreamWriteFile(currentTask);
                    break;
                case FileIOCommand::CMD_STREAM_READ_FILE:
                    taskSuccess = ExecuteStreamReadFile(currentTask);
                    break;
                case FileIOCommand::CMD_GET_CURRENT_DIRECTORY:
                    taskSuccess = ExecuteGetCurrentDirectory(currentTask);
                    break;
                case FileIOCommand::CMD_RENAME_FILE:
                    taskSuccess = ExecuteRenameFile(currentTask);
                    break;
                case FileIOCommand::CMD_DELETE_LINE_IN_FILE:
                    taskSuccess = ExecuteDeleteLineInFile(currentTask);
                    break;
                case FileIOCommand::CMD_COPY_FILE_TO:
                    taskSuccess = ExecuteCopyFileTo(currentTask);
                    break;
                case FileIOCommand::CMD_MOVE_FILE_TO:
                    taskSuccess = ExecuteMoveFileTo(currentTask);
                    break;
                default:
                    SetTaskError(currentTask, FileIOErrorType::ERROR_INVALID_PARAM, "Unknown command type");
                    taskSuccess = false;
                    break;
                }

                // Calculate task processing time
                auto taskEndTime = std::chrono::high_resolution_clock::now();
                float processingTime = std::chrono::duration<float, std::milli>(taskEndTime - taskStartTime).count();

                // Complete the task and update statistics
                CompleteTask(currentTask, taskSuccess);
                UpdateStatistics(taskSuccess, currentTask->writeBuffer.size() + currentTask->readBuffer.size(), processingTime);
            }
            else {
                // No tasks available - sleep to prevent CPU spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(FILEIO_THREAD_SLEEP_MS));
            }
        }
        catch (const std::exception& e) {
            // Brief pause before continuing to prevent rapid exception loops
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

#pragma warning(pop)
