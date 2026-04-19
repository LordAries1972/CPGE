//-------------------------------------------------------------------------------------------------
// PUNPack.cpp - High-Performance Compression and Decompression Class Implementation
//
// This implementation provides fast compression/decompression with data integrity checking
// and encryption for strings, structures, and memory buffers. Optimized for gaming platforms
// where timing is critical and data integrity is paramount.
//-------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "PUNPack.h"
#include "Debug.h"
#include "MathPrecalculation.h"

#pragma warning(push)
#pragma warning(disable: 4996)  // Suppress deprecated codecvt warnings
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

// External reference for global debug instance
extern Debug debug;

//==============================================================================
// Constructor and Destructor Implementation
//==============================================================================
PUNPack::PUNPack() :
    m_bIsInitialized(false),
    m_bHasCleanedUp(false),
    m_randomGenerator(std::random_device{}()),
    m_byteDistribution(0, 255),
    m_crc32TableInitialized(false),
    m_totalBytesProcessed(0),
    m_totalBytesCompressed(0),
    m_totalOperations(0),
    m_totalCompressionTime(0),
    m_totalDecompressionTime(0),
    m_mathPrecalc(nullptr)
{
#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Constructor called - Initializing compression engine");
#endif

    // Initialize CRC32 table to zero state
    m_crc32Table.fill(0);

    // Get reference to MathPrecalculation singleton for optimization
    m_mathPrecalc = &MathPrecalculation::GetInstance();
}

PUNPack::~PUNPack()
{
    // Ensure cleanup is performed before destruction
    if (!m_bHasCleanedUp.load())
    {
        Cleanup();
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Destructor called - All resources cleaned up");
#endif
}

//==============================================================================
// Initialization and Cleanup Methods
//==============================================================================
bool PUNPack::Initialize()
{
    // Check if already initialized to prevent double initialization
    if (m_bIsInitialized.load())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] Already initialized - skipping");
#endif
        return true;
    }

    // Thread-safe initialization using mutex
    std::lock_guard<std::mutex> lock(m_operationMutex);

    // Double-check pattern to ensure thread safety
    if (m_bIsInitialized.load())
    {
        return true;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Starting initialization of compression engine");
#endif

    try
    {
        // Initialize CRC32 lookup table for fast checksum calculations
        InitializeCRC32Table();

        // Verify MathPrecalculation is available and initialized
        if (m_mathPrecalc && !m_mathPrecalc->IsInitialized())
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] MathPrecalculation not initialized - some optimizations may be unavailable");
#endif
        }

        // Reset all statistics counters
        m_totalBytesProcessed.store(0);
        m_totalBytesCompressed.store(0);
        m_totalOperations.store(0);
        m_totalCompressionTime.store(0);
        m_totalDecompressionTime.store(0);

        // Mark as successfully initialized
        m_bIsInitialized.store(true);
        m_bHasCleanedUp.store(false);

#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Initialization completed successfully");
#endif

        return true;
    }
    catch (const std::exception& e)
    {
        // Convert exception message to wide string for logging
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());

        debug.logLevelMessage(LogLevel::LOG_CRITICAL,
            L"[PUNPack] Initialization failed with exception: " + wErrorMsg);

        return false;
    }
    catch (...)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL,
            L"[PUNPack] Initialization failed with unknown exception");

        return false;
    }
}

void PUNPack::Cleanup()
{
    // Check if already cleaned up
    if (m_bHasCleanedUp.load())
    {
        return;
    }

    // Thread-safe cleanup using mutex
    std::lock_guard<std::mutex> lock(m_operationMutex);

    // Double-check pattern to ensure thread safety
    if (m_bHasCleanedUp.load())
    {
        return;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Starting cleanup of compression engine");
#endif

    // Reset initialization state
    m_bIsInitialized.store(false);

    // Clear CRC32 table
    m_crc32Table.fill(0);
    m_crc32TableInitialized = false;

    // Reset statistics
    m_totalBytesProcessed.store(0);
    m_totalBytesCompressed.store(0);
    m_totalOperations.store(0);
    m_totalCompressionTime.store(0);
    m_totalDecompressionTime.store(0);

    // Clear math precalculation reference
    m_mathPrecalc = nullptr;

    // Mark as cleaned up
    m_bHasCleanedUp.store(true);

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Cleanup completed successfully");
#endif
}

//==============================================================================
// String Packing/Unpacking Implementation
//==============================================================================
PackResult PUNPack::PackString(const std::string& inputString, CompressionType compressionType, bool encrypt)
{
    PackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackString called for string of length: %zu", inputString.length());
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackString called before initialization");
#endif
        return result;
    }

    // Check for empty string
    if (inputString.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] PackString called with empty string");
#endif
        return result;
    }

    try
    {
        // Convert string to byte vector for processing
        std::vector<uint8_t> stringData(inputString.begin(), inputString.end());

        // Calculate checksum of original string data
        result.checksum = CalculateChecksum(stringData);
        result.originalSize = stringData.size();
        result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Perform compression based on specified type
        auto startTime = std::chrono::high_resolution_clock::now();

        switch (compressionType)
        {
        case CompressionType::RLE:
            result.compressedData = CompressRLE(stringData);
            break;
        case CompressionType::LZ77:
            result.compressedData = CompressLZ77(stringData);
            break;
        case CompressionType::HUFFMAN:
            result.compressedData = CompressHuffman(stringData);
            break;
        case CompressionType::HYBRID:
            result.compressedData = CompressHybrid(stringData);
            break;
        default:
            result.compressedData = stringData; // No compression
            break;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float compressionTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Set compression type and calculate compression ratio
        result.compressionType = compressionType;
        result.compressedSize = result.compressedData.size();
        result.compressionRatio = static_cast<float>(result.originalSize) / static_cast<float>(result.compressedSize);

        // Generate decipher key and encrypt if requested
        if (encrypt)
        {
            result.decipherKey = GenerateDecipherKey();
            EncryptData(result.compressedData, result.decipherKey);
            result.isEncrypted = true;
        }

        // Calculate checksum of compressed (and possibly encrypted) data
        result.compressedChecksum = CalculateChecksum(result.compressedData);
        result.totalPacketSize = sizeof(PackResult) + result.compressedData.size();

        // Update compression statistics
        UpdateStatistics(result.originalSize, result.compressedSize, compressionTime, 0.0f);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackString completed - Original: %zu, Compressed: %zu, Ratio: %.2f",
            result.originalSize, result.compressedSize, result.compressionRatio);
#endif

    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackString exception: " + wErrorMsg);
#endif
    }

    return result;
}

PackResult PUNPack::PackString(const std::wstring& inputString, CompressionType compressionType, bool encrypt)
{
    PackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackString (wide) called for string of length: %zu", inputString.length());
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackString (wide) called before initialization");
#endif
        return result;
    }

    // Check for empty string
    if (inputString.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] PackString (wide) called with empty string");
#endif
        return result;
    }

    try
    {
        // Convert wide string to UTF-8 encoded byte vector
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::string utf8String = converter.to_bytes(inputString);
        std::vector<uint8_t> stringData(utf8String.begin(), utf8String.end());

        // Calculate checksum of original UTF-8 encoded data
        result.checksum = CalculateChecksum(stringData);
        result.originalSize = stringData.size();
        result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Perform compression based on specified type
        auto startTime = std::chrono::high_resolution_clock::now();

        switch (compressionType)
        {
        case CompressionType::RLE:
            result.compressedData = CompressRLE(stringData);
            break;
        case CompressionType::LZ77:
            result.compressedData = CompressLZ77(stringData);
            break;
        case CompressionType::HUFFMAN:
            result.compressedData = CompressHuffman(stringData);
            break;
        case CompressionType::HYBRID:
            result.compressedData = CompressHybrid(stringData);
            break;
        default:
            result.compressedData = stringData; // No compression
            break;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float compressionTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Set compression type and calculate compression ratio
        result.compressionType = compressionType;
        result.compressedSize = result.compressedData.size();
        result.compressionRatio = static_cast<float>(result.originalSize) / static_cast<float>(result.compressedSize);

        // Generate decipher key and encrypt if requested
        if (encrypt)
        {
            result.decipherKey = GenerateDecipherKey();
            EncryptData(result.compressedData, result.decipherKey);
            result.isEncrypted = true;
        }

        // Calculate checksum of compressed (and possibly encrypted) data
        result.compressedChecksum = CalculateChecksum(result.compressedData);
        result.totalPacketSize = sizeof(PackResult) + result.compressedData.size();

        // Update compression statistics
        UpdateStatistics(result.originalSize, result.compressedSize, compressionTime, 0.0f);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackString (wide) completed - Original: %zu, Compressed: %zu, Ratio: %.2f",
            result.originalSize, result.compressedSize, result.compressionRatio);
#endif

    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackString (wide) exception: " + wErrorMsg);
#endif
    }

    return result;
}

UnpackResult PUNPack::UnpackString(const PackResult& packedData)
{
    UnpackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackString called");
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load())
    {
        result.errorMessage = "PUNPack not initialized";
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackString called before initialization");
#endif
        return result;
    }

    // Validate pack result
    if (!ValidatePackResult(packedData))
    {
        result.errorMessage = "Invalid pack result data";
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackString received invalid pack result");
#endif
        return result;
    }

    try
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Copy compressed data for processing
        std::vector<uint8_t> workingData = packedData.compressedData;

        // Decrypt if necessary
        if (packedData.isEncrypted && !packedData.decipherKey.empty())
        {
            DecryptData(workingData, packedData.decipherKey);
        }

        // Verify compressed data checksum
        uint32_t verifyChecksum = CalculateChecksum(workingData);
        if (verifyChecksum != packedData.compressedChecksum)
        {
            result.errorMessage = "Compressed data checksum verification failed";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Checksum mismatch - Expected: 0x%08X, Got: 0x%08X",
                packedData.compressedChecksum, verifyChecksum);
#endif
            return result;
        }

        // Decompress based on compression type
        std::vector<uint8_t> decompressedData;

        switch (packedData.compressionType)
        {
        case CompressionType::RLE:
            decompressedData = DecompressRLE(workingData, packedData.originalSize);
            break;
        case CompressionType::LZ77:
            decompressedData = DecompressLZ77(workingData, packedData.originalSize);
            break;
        case CompressionType::HUFFMAN:
            decompressedData = DecompressHuffman(workingData, packedData.originalSize);
            break;
        case CompressionType::HYBRID:
            decompressedData = DecompressHybrid(workingData, packedData.originalSize);
            break;
        default:
            decompressedData = workingData; // No compression
            break;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float decompressionTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Verify decompressed data size
        if (decompressedData.size() != packedData.originalSize)
        {
            result.errorMessage = "Decompressed data size mismatch";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Decompressed size mismatch - Expected: %zu, Got: %zu",
                packedData.originalSize, decompressedData.size());
#endif
            return result;
        }

        // Verify original data checksum
        uint32_t originalChecksum = CalculateChecksum(decompressedData);
        if (originalChecksum != packedData.checksum)
        {
            result.errorMessage = "Original data checksum verification failed";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Original checksum mismatch - Expected: 0x%08X, Got: 0x%08X",
                packedData.checksum, originalChecksum);
#endif
            return result;
        }

        // Set result information
        result.success = true;
        result.data = std::move(decompressedData);
        result.originalSize = packedData.originalSize;
        result.verifiedChecksum = originalChecksum;
        result.usedCompression = packedData.compressionType;
        result.decompressionTime = decompressionTime;

        // Update statistics
        UpdateStatistics(packedData.originalSize, packedData.compressedSize, 0.0f, decompressionTime);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackString completed successfully - Size: %zu, Time: %.2fms",
            result.originalSize, result.decompressionTime);
#endif

    }
    catch (const std::exception& e)
    {
        result.errorMessage = std::string("Exception during unpacking: ") + e.what();
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackString exception: " + wErrorMsg);
#endif
    }

    return result;
}

UnpackResult PUNPack::UnpackWString(const PackResult& packedData)
{
    UnpackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackWString called");
#endif

    // First unpack as regular string to get UTF-8 data
    UnpackResult stringResult = UnpackString(packedData);

    if (!stringResult.success)
    {
        // Forward the error from string unpacking
        result.errorMessage = stringResult.errorMessage;
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackWString failed during string unpacking");
#endif
        return result;
    }

    try
    {
        // Convert UTF-8 data back to wide string
        std::string utf8String(stringResult.data.begin(), stringResult.data.end());
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        std::wstring wideString = converter.from_bytes(utf8String);

        // Convert wide string back to byte representation for result
        const uint8_t* wideBytes = reinterpret_cast<const uint8_t*>(wideString.c_str());
        size_t wideBytesSize = wideString.length() * sizeof(wchar_t);

        result.data.assign(wideBytes, wideBytes + wideBytesSize);
        result.success = true;
        result.originalSize = stringResult.originalSize;
        result.verifiedChecksum = stringResult.verifiedChecksum;
        result.usedCompression = stringResult.usedCompression;
        result.decompressionTime = stringResult.decompressionTime;

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackWString completed successfully - Wide string length: %zu",
            wideString.length());
#endif

    }
    catch (const std::exception& e)
    {
        result.errorMessage = std::string("Exception during wide string conversion: ") + e.what();
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackWString exception: " + wErrorMsg);
#endif
    }

    return result;
}

//==============================================================================
// Memory Buffer Packing/Unpacking Implementation
//==============================================================================
PackResult PUNPack::PackBuffer(const void* buffer, size_t bufferSize, CompressionType compressionType, bool encrypt)
{
    PackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackBuffer called for buffer of size: %zu", bufferSize);
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackBuffer called before initialization");
#endif
        return result;
    }

    // Validate input parameters
    if (buffer == nullptr || bufferSize == 0)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackBuffer called with null buffer or zero size");
#endif
        return result;
    }

    // Check for maximum buffer size limit
    if (bufferSize > PUNPACK_MAX_BUFFER_SIZE)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Buffer size %zu exceeds maximum limit %zu",
            bufferSize, PUNPACK_MAX_BUFFER_SIZE);
#endif
        return result;
    }

    try
    {
        // Convert buffer to byte vector for processing
        std::vector<uint8_t> bufferData = DataToByteVector(buffer, bufferSize);

        // Calculate checksum of original buffer data
        result.checksum = CalculateChecksum(bufferData);
        result.originalSize = bufferSize;
        result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Perform compression based on specified type
        auto startTime = std::chrono::high_resolution_clock::now();

        switch (compressionType)
        {
        case CompressionType::RLE:
            result.compressedData = CompressRLE(bufferData);
            break;
        case CompressionType::LZ77:
            result.compressedData = CompressLZ77(bufferData);
            break;
        case CompressionType::HUFFMAN:
            result.compressedData = CompressHuffman(bufferData);
            break;
        case CompressionType::HYBRID:
            result.compressedData = CompressHybrid(bufferData);
            break;
        default:
            result.compressedData = bufferData; // No compression
            break;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float compressionTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Set compression type and calculate compression ratio
        result.compressionType = compressionType;
        result.compressedSize = result.compressedData.size();
        result.compressionRatio = static_cast<float>(result.originalSize) / static_cast<float>(result.compressedSize);

        // Generate decipher key and encrypt if requested
        if (encrypt)
        {
            result.decipherKey = GenerateDecipherKey();
            EncryptData(result.compressedData, result.decipherKey);
            result.isEncrypted = true;
        }

        // Calculate checksum of compressed (and possibly encrypted) data
        result.compressedChecksum = CalculateChecksum(result.compressedData);
        result.totalPacketSize = sizeof(PackResult) + result.compressedData.size();

        // Update compression statistics
        UpdateStatistics(result.originalSize, result.compressedSize, compressionTime, 0.0f);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackBuffer completed - Original: %zu, Compressed: %zu, Ratio: %.2f",
            result.originalSize, result.compressedSize, result.compressionRatio);
#endif

    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackBuffer exception: " + wErrorMsg);
#endif
    }

    return result;
}

PackResult PUNPack::PackBuffer(const std::vector<uint8_t>& buffer, CompressionType compressionType, bool encrypt)
{
#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackBuffer (vector) called for buffer of size: %zu", buffer.size());
#endif

    // Delegate to pointer-based PackBuffer method
    return PackBuffer(buffer.data(), buffer.size(), compressionType, encrypt);
}

UnpackResult PUNPack::UnpackBuffer(const PackResult& packedData)
{
    UnpackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackBuffer called");
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load())
    {
        result.errorMessage = "PUNPack not initialized";
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackBuffer called before initialization");
#endif
        return result;
    }

    // Validate pack result
    if (!ValidatePackResult(packedData))
    {
        result.errorMessage = "Invalid pack result data";
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackBuffer received invalid pack result");
#endif
        return result;
    }

    try
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Copy compressed data for processing
        std::vector<uint8_t> workingData = packedData.compressedData;

        // Decrypt if necessary
        if (packedData.isEncrypted && !packedData.decipherKey.empty())
        {
            DecryptData(workingData, packedData.decipherKey);
        }

        // Verify compressed data checksum
        uint32_t verifyChecksum = CalculateChecksum(workingData);
        if (verifyChecksum != packedData.compressedChecksum)
        {
            result.errorMessage = "Compressed data checksum verification failed";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Checksum mismatch - Expected: 0x%08X, Got: 0x%08X",
                packedData.compressedChecksum, verifyChecksum);
#endif
            return result;
        }

        // Decompress based on compression type
        std::vector<uint8_t> decompressedData;

        switch (packedData.compressionType)
        {
        case CompressionType::RLE:
            decompressedData = DecompressRLE(workingData, packedData.originalSize);
            break;
        case CompressionType::LZ77:
            decompressedData = DecompressLZ77(workingData, packedData.originalSize);
            break;
        case CompressionType::HUFFMAN:
            decompressedData = DecompressHuffman(workingData, packedData.originalSize);
            break;
        case CompressionType::HYBRID:
            decompressedData = DecompressHybrid(workingData, packedData.originalSize);
            break;
        default:
            decompressedData = workingData; // No compression
            break;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float decompressionTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Verify decompressed data size
        if (decompressedData.size() != packedData.originalSize)
        {
            result.errorMessage = "Decompressed data size mismatch";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Decompressed size mismatch - Expected: %zu, Got: %zu",
                packedData.originalSize, decompressedData.size());
#endif
            return result;
        }

        // Verify original data checksum
        uint32_t originalChecksum = CalculateChecksum(decompressedData);
        if (originalChecksum != packedData.checksum)
        {
            result.errorMessage = "Original data checksum verification failed";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Original checksum mismatch - Expected: 0x%08X, Got: 0x%08X",
                packedData.checksum, originalChecksum);
#endif
            return result;
        }

        // Set result information
        result.success = true;
        result.data = std::move(decompressedData);
        result.originalSize = packedData.originalSize;
        result.verifiedChecksum = originalChecksum;
        result.usedCompression = packedData.compressionType;
        result.decompressionTime = decompressionTime;

        // Update statistics
        UpdateStatistics(packedData.originalSize, packedData.compressedSize, 0.0f, decompressionTime);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackBuffer completed successfully - Size: %zu, Time: %.2fms",
            result.originalSize, result.decompressionTime);
#endif

    }
    catch (const std::exception& e)
    {
        result.errorMessage = std::string("Exception during unpacking: ") + e.what();
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackBuffer exception: " + wErrorMsg);
#endif
    }

    return result;
}

//==============================================================================
// Checksum Calculation Implementation
//==============================================================================
uint32_t PUNPack::CalculateChecksum(const void* data, size_t size) const
{
    // Ensure the class is initialized before attempting checksum calculation
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] CalculateChecksum called before initialization");
#endif
        return 0;
    }

    // Validate input parameters
    if (data == nullptr || size == 0)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] CalculateChecksum called with null data or zero size");
#endif
        return 0;
    }

    // Use fast CRC32 calculation if table is initialized
    if (m_crc32TableInitialized)
    {
        return CalculateCRC32Fast(data, size);
    }
    else
    {
        // Fallback to slower calculation without lookup table
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] CRC32 table not initialized - using slower calculation");
#endif

        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);

        for (size_t i = 0; i < size; ++i)
        {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 1)
                {
                    crc = (crc >> 1) ^ PUNPACK_CHECKSUM_POLYNOMIAL;
                }
                else
                {
                    crc >>= 1;
                }
            }
        }

        return crc ^ 0xFFFFFFFF;
    }
}

uint32_t PUNPack::CalculateChecksum(const std::vector<uint8_t>& data) const
{
#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CalculateChecksum (vector) called for %zu bytes", data.size());
#endif

    // Delegate to pointer-based calculation
    return CalculateChecksum(data.data(), data.size());
}

uint32_t PUNPack::CalculateChecksum(const std::string& data) const
{
#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CalculateChecksum (string) called for string of length: %zu", data.length());
#endif

    // Delegate to pointer-based calculation
    return CalculateChecksum(data.c_str(), data.length());
}

uint32_t PUNPack::CalculateChecksum(const std::wstring& data) const
{
#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CalculateChecksum (wstring) called for string of length: %zu", data.length());
#endif

    // Calculate checksum on the raw byte representation of the wide string
    return CalculateChecksum(data.c_str(), data.length() * sizeof(wchar_t));
}

bool PUNPack::VerifyChecksum(const void* data, size_t size, uint32_t expectedChecksum) const
{
    // Calculate checksum of the provided data
    uint32_t calculatedChecksum = CalculateChecksum(data, size);

    // Compare with expected checksum
    bool isValid = (calculatedChecksum == expectedChecksum);

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] VerifyChecksum - Expected: 0x%08X, Calculated: 0x%08X, Valid: %s",
        expectedChecksum, calculatedChecksum, isValid ? L"true" : L"false");
#endif

    return isValid;
}

//==============================================================================
// Encryption/Decryption Implementation
//==============================================================================
std::vector<uint8_t> PUNPack::GenerateDecipherKey(size_t keySize)
{
    std::vector<uint8_t> key;
    key.reserve(keySize);

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] GenerateDecipherKey called for key size: %zu", keySize);
#endif

    // Thread-safe key generation
    std::lock_guard<std::mutex> lock(m_operationMutex);

    try
    {
        // Generate random bytes for the key using secure random generator
        for (size_t i = 0; i < keySize; ++i)
        {
            key.push_back(static_cast<uint8_t>(m_byteDistribution(m_randomGenerator)));
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] Generated decipher key successfully - Size: %zu", key.size());
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] GenerateDecipherKey exception: " + wErrorMsg);
#endif
        key.clear(); // Return empty key on error
    }

    return key;
}

void PUNPack::EncryptData(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const
{
    // Validate input parameters
    if (data.empty() || key.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] EncryptData called with empty data or key");
#endif
        return;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] EncryptData called - Data size: %zu, Key size: %zu",
        data.size(), key.size());
#endif

    try
    {
        // Perform XOR encryption with key rotation for enhanced security
        size_t keyIndex = 0;
        for (size_t i = 0; i < data.size(); ++i)
        {
            // XOR data byte with current key byte
            data[i] ^= key[keyIndex];

            // Rotate through key bytes
            keyIndex = (keyIndex + 1) % key.size();

            // Additional security: rotate key byte based on position for variation
            if (m_mathPrecalc && m_mathPrecalc->IsInitialized())
            {
                // Use fast rotation from MathPrecalculation if available
                uint8_t rotatedKeyByte = static_cast<uint8_t>(m_mathPrecalc->FastSin(static_cast<float>(i)) * 255.0f);
                data[i] ^= rotatedKeyByte;
            }
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] EncryptData completed successfully");
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] EncryptData exception: " + wErrorMsg);
#endif
    }
}

void PUNPack::DecryptData(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const
{
    // Validate input parameters
    if (data.empty() || key.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] DecryptData called with empty data or key");
#endif
        return;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecryptData called - Data size: %zu, Key size: %zu",
        data.size(), key.size());
#endif

    try
    {
        // Perform XOR decryption (same as encryption due to XOR properties)
        size_t keyIndex = 0;
        for (size_t i = 0; i < data.size(); ++i)
        {
            // Additional security: undo position-based rotation first
            if (m_mathPrecalc && m_mathPrecalc->IsInitialized())
            {
                // Use fast rotation from MathPrecalculation if available
                uint8_t rotatedKeyByte = static_cast<uint8_t>(m_mathPrecalc->FastSin(static_cast<float>(i)) * 255.0f);
                data[i] ^= rotatedKeyByte;
            }

            // XOR data byte with current key byte
            data[i] ^= key[keyIndex];

            // Rotate through key bytes
            keyIndex = (keyIndex + 1) % key.size();
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecryptData completed successfully");
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecryptData exception: " + wErrorMsg);
#endif
    }
}

//==============================================================================
// Statistics and Utility Implementation
//==============================================================================
PUNPack::CompressionStats PUNPack::GetStatistics() const
{
    CompressionStats stats;

    // Thread-safe access to statistics
    std::lock_guard<std::mutex> lock(m_statisticsMutex);

    // Copy atomic values to statistics structure
    stats.totalBytesProcessed = m_totalBytesProcessed.load();
    stats.totalBytesCompressed = m_totalBytesCompressed.load();
    stats.totalOperations = m_totalOperations.load();

    // Calculate averages (avoid division by zero)
    if (stats.totalOperations > 0)
    {
        stats.averageCompressionRatio = static_cast<float>(stats.totalBytesProcessed) / static_cast<float>(stats.totalBytesCompressed);
        stats.averageCompressionTime = static_cast<float>(m_totalCompressionTime.load()) / static_cast<float>(stats.totalOperations * 1000.0f); // Convert microseconds to milliseconds
        stats.averageDecompressionTime = static_cast<float>(m_totalDecompressionTime.load()) / static_cast<float>(stats.totalOperations * 1000.0f);
    }
    else
    {
        stats.averageCompressionRatio = 1.0f;
        stats.averageCompressionTime = 0.0f;
        stats.averageDecompressionTime = 0.0f;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] Statistics - Operations: %zu, Avg Ratio: %.2f, Avg Comp Time: %.2fms",
        stats.totalOperations, stats.averageCompressionRatio, stats.averageCompressionTime);
#endif

    return stats;
}

void PUNPack::ResetStatistics()
{
    // Thread-safe reset of all statistics
    std::lock_guard<std::mutex> lock(m_statisticsMutex);

    m_totalBytesProcessed.store(0);
    m_totalBytesCompressed.store(0);
    m_totalOperations.store(0);
    m_totalCompressionTime.store(0);
    m_totalDecompressionTime.store(0);

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] Statistics reset successfully");
#endif
}

CompressionType PUNPack::GetOptimalCompressionType(const void* data, size_t size) const
{
    // Validate input parameters
    if (data == nullptr || size == 0)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] GetOptimalCompressionType called with invalid parameters");
#endif
        return CompressionType::NONE;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] GetOptimalCompressionType analyzing %zu bytes", size);
#endif

    try
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);

        // If data is too small, don't compress
        if (size < PUNPACK_MIN_COMPRESS_SIZE)
        {
            return CompressionType::NONE;
        }

        // Analyze data characteristics to determine optimal compression
        size_t repeatingBytes = 0;
        size_t uniqueBytes = 0;
        std::unordered_map<uint8_t, size_t> byteFrequency;

        // Count byte frequencies and analyze patterns
        for (size_t i = 0; i < size; ++i)
        {
            byteFrequency[bytes[i]]++;

            // Check for repeating patterns (simple RLE detection)
            if (i > 0 && bytes[i] == bytes[i - 1])
            {
                repeatingBytes++;
            }
        }

        uniqueBytes = byteFrequency.size();

        // Calculate compression heuristics
        float repetitionRatio = static_cast<float>(repeatingBytes) / static_cast<float>(size);
        float diversityRatio = static_cast<float>(uniqueBytes) / 256.0f; // How diverse the byte values are

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] Data analysis - Repetition: %.2f, Diversity: %.2f, Unique bytes: %zu",
            repetitionRatio, diversityRatio, uniqueBytes);
#endif

        // Determine optimal compression based on data characteristics
        if (repetitionRatio > 0.6f)
        {
            // High repetition - RLE is optimal
            return CompressionType::RLE;
        }
        else if (diversityRatio < 0.3f && uniqueBytes < 64)
        {
            // Low diversity - Huffman coding is optimal
            return CompressionType::HUFFMAN;
        }
        else if (size > 1024)
        {
            // Large data with mixed patterns - LZ77 is good for general purpose
            return CompressionType::LZ77;
        }
        else
        {
            // Mixed characteristics - use hybrid approach
            return CompressionType::HYBRID;
        }
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] GetOptimalCompressionType exception: " + wErrorMsg);
#endif
        return CompressionType::NONE;
    }
}

//==============================================================================
// Internal Compression Methods Implementation
//==============================================================================
std::vector<uint8_t> PUNPack::CompressRLE(const std::vector<uint8_t>& input) const
{
    std::vector<uint8_t> compressed;
    compressed.reserve(input.size()); // Reserve space to avoid frequent reallocations

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressRLE processing %zu bytes", input.size());
#endif

    if (input.empty())
    {
        return compressed;
    }

    try
    {
        size_t i = 0;
        while (i < input.size())
        {
            uint8_t currentByte = input[i];
            size_t runLength = 1;

            // Count consecutive identical bytes (run length)
            while (i + runLength < input.size() && input[i + runLength] == currentByte && runLength < 255)
            {
                runLength++;
            }

            // Store run length and byte value
            if (runLength >= 3 || currentByte == 0xFF) // Compress runs of 3+ or special marker bytes
            {
                compressed.push_back(0xFF); // RLE marker
                compressed.push_back(static_cast<uint8_t>(runLength));
                compressed.push_back(currentByte);
            }
            else
            {
                // Store bytes individually if not worth compressing
                for (size_t j = 0; j < runLength; ++j)
                {
                    // Escape 0xFF bytes that aren't RLE markers
                    if (currentByte == 0xFF)
                    {
                        compressed.push_back(0xFF);
                        compressed.push_back(0x00); // Escape sequence for literal 0xFF
                    }
                    compressed.push_back(currentByte);
                }
            }

            i += runLength;
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressRLE completed - Original: %zu, Compressed: %zu",
            input.size(), compressed.size());
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] CompressRLE exception: " + wErrorMsg);
#endif
        return input; // Return original data on error
    }

    return compressed;
}

std::vector<uint8_t> PUNPack::DecompressRLE(const std::vector<uint8_t>& input, size_t originalSize) const
{
    std::vector<uint8_t> decompressed;
    decompressed.reserve(originalSize); // Reserve exact size for efficiency

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressRLE processing %zu bytes to %zu bytes",
        input.size(), originalSize);
#endif

    if (input.empty())
    {
        return decompressed;
    }

    try
    {
        size_t i = 0;
        while (i < input.size() && decompressed.size() < originalSize)
        {
            if (input[i] == 0xFF && i + 1 < input.size())
            {
                if (input[i + 1] == 0x00)
                {
                    // Escaped literal 0xFF
                    decompressed.push_back(0xFF);
                    i += 2;
                }
                else if (i + 2 < input.size())
                {
                    // RLE sequence: marker, length, byte
                    uint8_t runLength = input[i + 1];
                    uint8_t byteValue = input[i + 2];

                    // Expand the run
                    for (uint8_t j = 0; j < runLength && decompressed.size() < originalSize; ++j)
                    {
                        decompressed.push_back(byteValue);
                    }
                    i += 3;
                }
                else
                {
                    // Incomplete sequence - treat as literal
                    decompressed.push_back(input[i]);
                    i++;
                }
            }
            else
            {
                // Literal byte
                decompressed.push_back(input[i]);
                i++;
            }
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressRLE completed - Decompressed: %zu bytes",
            decompressed.size());
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressRLE exception: " + wErrorMsg);
#endif
        return input; // Return original data on error
    }

    return decompressed;
}

std::vector<uint8_t> PUNPack::CompressLZ77(const std::vector<uint8_t>& input) const
{
    std::vector<uint8_t> compressed;
    compressed.reserve(input.size() * 1.1f); // Reserve slightly more space for headers

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressLZ77 processing %zu bytes", input.size());
#endif

    if (input.empty())
    {
        return compressed;
    }

    try
    {
        const size_t windowSize = 4096;   // Look-back window size
        const size_t maxMatchLength = 255; // Maximum match length

        size_t inputPos = 0;

        while (inputPos < input.size())
        {
            size_t bestMatchDistance = 0;
            size_t bestMatchLength = 0;

            // Look for the longest match in the sliding window
            size_t windowStart = (inputPos >= windowSize) ? inputPos - windowSize : 0;

            for (size_t searchPos = windowStart; searchPos < inputPos; ++searchPos)
            {
                size_t matchLength = 0;

                // Count matching bytes
                while (matchLength < maxMatchLength &&
                    inputPos + matchLength < input.size() &&
                    input[searchPos + matchLength] == input[inputPos + matchLength])
                {
                    matchLength++;
                }

                // Update best match if this one is longer
                if (matchLength > bestMatchLength && matchLength >= 3) // Minimum match length of 3
                {
                    bestMatchDistance = inputPos - searchPos;
                    bestMatchLength = matchLength;
                }
            }

            if (bestMatchLength >= 3)
            {
                // Encode match: flag (0x80), distance (2 bytes), length (1 byte)
                compressed.push_back(0x80); // Match flag
                compressed.push_back(static_cast<uint8_t>(bestMatchDistance & 0xFF));
                compressed.push_back(static_cast<uint8_t>((bestMatchDistance >> 8) & 0xFF));
                compressed.push_back(static_cast<uint8_t>(bestMatchLength));
                inputPos += bestMatchLength;
            }
            else
            {
                // Encode literal byte
                uint8_t literal = input[inputPos];
                if (literal == 0x80)
                {
                    // Escape the flag byte
                    compressed.push_back(0x80);
                    compressed.push_back(0x00); // Escape sequence
                }
                compressed.push_back(literal);
                inputPos++;
            }
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressLZ77 completed - Original: %zu, Compressed: %zu",
            input.size(), compressed.size());
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] CompressLZ77 exception: " + wErrorMsg);
#endif
        return input; // Return original data on error
    }

    return compressed;
}

std::vector<uint8_t> PUNPack::DecompressLZ77(const std::vector<uint8_t>& input, size_t originalSize) const
{
    std::vector<uint8_t> decompressed;
    decompressed.reserve(originalSize);

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressLZ77 processing %zu bytes to %zu bytes",
        input.size(), originalSize);
#endif

    if (input.empty())
    {
        return decompressed;
    }

    try
    {
        size_t inputPos = 0;

        while (inputPos < input.size() && decompressed.size() < originalSize)
        {
            if (input[inputPos] == 0x80)
            {
                if (inputPos + 1 < input.size() && input[inputPos + 1] == 0x00)
                {
                    // Escaped literal 0x80
                    decompressed.push_back(0x80);
                    inputPos += 2;
                }
                else if (inputPos + 3 < input.size())
                {
                    // Match sequence: distance (2 bytes), length (1 byte)
                    size_t distance = input[inputPos + 1] | (static_cast<size_t>(input[inputPos + 2]) << 8);
                    size_t length = input[inputPos + 3];

                    // Copy from the sliding window
                    for (size_t i = 0; i < length && decompressed.size() < originalSize; ++i)
                    {
                        if (distance <= decompressed.size())
                        {
                            decompressed.push_back(decompressed[decompressed.size() - distance]);
                        }
                        else
                        {
                            // Invalid distance - corruption detected
#if defined(_DEBUG_PUNPACK_)
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressLZ77 invalid distance detected");
#endif
                            return std::vector<uint8_t>(); // Return empty vector on corruption
                        }
                    }
                    inputPos += 4;
                }
                else
                {
                    // Incomplete sequence
                    decompressed.push_back(input[inputPos]);
                    inputPos++;
                }
            }
            else
            {
                // Literal byte
                decompressed.push_back(input[inputPos]);
                inputPos++;
            }
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressLZ77 completed - Decompressed: %zu bytes",
            decompressed.size());
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressLZ77 exception: " + wErrorMsg);
#endif
        return std::vector<uint8_t>(); // Return empty vector on error
    }

    return decompressed;
}

//==============================================================================
// Huffman Compression Data Structures and Helper Classes
//==============================================================================
struct HuffmanNode {
    uint8_t symbol;                                                     // The byte value (0-255)
    uint32_t frequency;                                                 // Frequency of occurrence
    std::shared_ptr<HuffmanNode> left;                                  // Left child in Huffman tree
    std::shared_ptr<HuffmanNode> right;                                 // Right child in Huffman tree
    bool isLeaf;                                                        // True if this is a leaf node

    // Constructor for leaf nodes
    HuffmanNode(uint8_t sym, uint32_t freq) :
        symbol(sym), frequency(freq), left(nullptr), right(nullptr), isLeaf(true) {
    }

    // Constructor for internal nodes
    HuffmanNode(uint32_t freq, std::shared_ptr<HuffmanNode> l, std::shared_ptr<HuffmanNode> r) :
        symbol(0), frequency(freq), left(l), right(r), isLeaf(false) {
    }
};

// Function-based comparator for priority queue (C++17 compliant)
auto HuffmanNodeComparator = [](const std::shared_ptr<HuffmanNode>& a, const std::shared_ptr<HuffmanNode>& b) -> bool {
    // Higher frequency has lower priority (min-heap)
    if (a->frequency != b->frequency) {
        return a->frequency > b->frequency;
    }
    // For equal frequencies, prefer leaf nodes for deterministic ordering
    if (a->isLeaf != b->isLeaf) {
        return a->isLeaf < b->isLeaf;
    }
    // Final tie-breaker: compare symbols for leaf nodes
    if (a->isLeaf && b->isLeaf) {
        return a->symbol > b->symbol;
    }
    return false; // Equal nodes
};

// Type alias for the priority queue to improve readability
using HuffmanPriorityQueue = std::priority_queue<std::shared_ptr<HuffmanNode>, std::vector<std::shared_ptr<HuffmanNode>>, decltype(HuffmanNodeComparator)> ;

// Huffman code representation for efficient bit manipulation
struct HuffmanCode {
    uint32_t code;                                                      // The bit pattern (right-aligned)
    uint8_t bitLength;                                                  // Number of bits in the code

    HuffmanCode() : code(0), bitLength(0) {}
    HuffmanCode(uint32_t c, uint8_t len) : code(c), bitLength(len) {}
};

// Bit stream writer for efficient bit packing
class BitWriter {
private:
    std::vector<uint8_t>& m_buffer;                                     // Output buffer reference
    uint8_t m_currentByte;                                              // Current byte being built
    uint8_t m_bitCount;                                                 // Number of bits in current byte

public:
    explicit BitWriter(std::vector<uint8_t>& buffer) :
        m_buffer(buffer), m_currentByte(0), m_bitCount(0) {
    }

    // Write bits to the stream
    void WriteBits(uint32_t bits, uint8_t numBits) {
        for (int i = numBits - 1; i >= 0; --i) {
            // Extract bit from position i
            uint8_t bit = (bits >> i) & 1;

            // Add bit to current byte
            m_currentByte = (m_currentByte << 1) | bit;
            m_bitCount++;

            // If byte is complete, flush it to buffer
            if (m_bitCount == 8) {
                m_buffer.push_back(m_currentByte);
                m_currentByte = 0;
                m_bitCount = 0;
            }
        }
    }

    // Flush any remaining bits (pad with zeros if necessary)
    void Flush() {
        if (m_bitCount > 0) {
            // Pad remaining bits with zeros and flush
            m_currentByte <<= (8 - m_bitCount);
            m_buffer.push_back(m_currentByte);
            m_currentByte = 0;
            m_bitCount = 0;
        }
    }

    // Get the number of padding bits that were added
    uint8_t GetPaddingBits() const {
        return m_bitCount == 0 ? 0 : (8 - m_bitCount);
    }
};

// Bit stream reader for efficient bit unpacking
class BitReader {
private:
    const std::vector<uint8_t>& m_buffer;                               // Input buffer reference
    size_t m_byteIndex;                                                 // Current byte index
    uint8_t m_bitIndex;                                                 // Current bit index within byte

public:
    explicit BitReader(const std::vector<uint8_t>& buffer) :
        m_buffer(buffer), m_byteIndex(0), m_bitIndex(0) {
    }

    // Read a single bit from the stream
    bool ReadBit() {
        if (m_byteIndex >= m_buffer.size()) {
            return false; // End of stream
        }

        // Extract bit from current position
        bool bit = (m_buffer[m_byteIndex] >> (7 - m_bitIndex)) & 1;

        // Advance to next bit
        m_bitIndex++;
        if (m_bitIndex == 8) {
            m_bitIndex = 0;
            m_byteIndex++;
        }

        return bit;
    }

    // Check if there are more bits to read
    bool HasMoreBits() const {
        return m_byteIndex < m_buffer.size() || (m_byteIndex == m_buffer.size() && m_bitIndex > 0);
    }

    // Get current position for debugging
    size_t GetPosition() const {
        return m_byteIndex * 8 + m_bitIndex;
    }
};

std::vector<uint8_t> PUNPack::CompressHuffman(const std::vector<uint8_t>& input) const
{
    std::vector<uint8_t> compressed;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] CompressHuffman processing %zu bytes", input.size());
#endif

    // Handle empty input
    if (input.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] CompressHuffman called with empty input");
#endif
        return compressed;
    }

    // Handle single byte input (special case)
    if (input.size() == 1)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] CompressHuffman single byte input - using raw storage");
#endif
        compressed.push_back(0xFF); // Special marker for single byte
        compressed.push_back(input[0]);
        return compressed;
    }

    try
    {
        // Step 1: Calculate frequency of each byte value
        std::array<uint32_t, 256> frequencies{};
        for (uint8_t byte : input)
        {
            frequencies[byte]++;
        }

        // Step 2: Build priority queue of nodes (min-heap by frequency) - FIXED VERSION
        HuffmanPriorityQueue nodeQueue(HuffmanNodeComparator);

        // Create leaf nodes for each byte that appears in input
        for (int i = 0; i < 256; ++i)
        {
            if (frequencies[i] > 0)
            {
                auto node = std::make_shared<HuffmanNode>(static_cast<uint8_t>(i), frequencies[i]);
                nodeQueue.push(node);
            }
        }

        // Handle case where only one unique symbol exists
        if (nodeQueue.size() == 1)
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] CompressHuffman single unique symbol - using RLE fallback");
#endif
            return CompressRLE(input); // Fallback to RLE for single symbol
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressHuffman found %zu unique symbols", nodeQueue.size());
#endif

        // Step 3: Build Huffman tree by combining nodes
        while (nodeQueue.size() > 1)
        {
            // Get two nodes with lowest frequency
            auto right = nodeQueue.top(); nodeQueue.pop();
            auto left = nodeQueue.top(); nodeQueue.pop();

            // Create internal node with combined frequency
            uint32_t combinedFreq = left->frequency + right->frequency;
            auto internalNode = std::make_shared<HuffmanNode>(combinedFreq, left, right);

            nodeQueue.push(internalNode);
        }

        // The remaining node is the root of the Huffman tree
        auto root = nodeQueue.top();

        // Step 4: Generate Huffman codes by traversing the tree
        std::array<HuffmanCode, 256> huffmanCodes{};

        // Use recursive lambda to generate codes (C++17 compliant)
        std::function<void(std::shared_ptr<HuffmanNode>, uint32_t, uint8_t)> generateCodes =
            [&](std::shared_ptr<HuffmanNode> node, uint32_t code, uint8_t depth) -> void {
            if (!node) return;

            if (node->isLeaf)
            {
                // Leaf node - store the code for this symbol
                huffmanCodes[node->symbol] = HuffmanCode(code, depth);

#if defined(_DEBUG_PUNPACK_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] Symbol 0x%02X: code=0x%08X, bits=%d",
                    node->symbol, code, depth);
#endif
            }
            else
            {
                // Internal node - recurse to children
                generateCodes(node->left, code << 1, depth + 1);        // Left = 0
                generateCodes(node->right, (code << 1) | 1, depth + 1); // Right = 1
            }
            };

        generateCodes(root, 0, 0);

        // Step 5: Serialize the Huffman tree for decompression
        compressed.reserve(input.size()); // Reserve space for efficiency

        // Write tree serialization marker
        compressed.push_back(0xFE); // Huffman tree marker

        // Serialize tree structure using pre-order traversal
        std::function<void(std::shared_ptr<HuffmanNode>)> serializeTree =
            [&](std::shared_ptr<HuffmanNode> node) -> void {
            if (!node) return;

            if (node->isLeaf)
            {
                compressed.push_back(0x01); // Leaf marker
                compressed.push_back(node->symbol); // Symbol value
            }
            else
            {
                compressed.push_back(0x00); // Internal node marker
                serializeTree(node->left);
                serializeTree(node->right);
            }
            };

        serializeTree(root);

        // Step 6: Encode the input data using Huffman codes
        BitWriter bitWriter(compressed);

        // Write original data size (4 bytes)
        uint32_t originalSize = static_cast<uint32_t>(input.size());
        for (int i = 3; i >= 0; --i)
        {
            compressed.push_back(static_cast<uint8_t>((originalSize >> (i * 8)) & 0xFF));
        }

        // Encode each byte using its Huffman code
        for (uint8_t byte : input)
        {
            const HuffmanCode& code = huffmanCodes[byte];
            if (code.bitLength > 0)
            {
                bitWriter.WriteBits(code.code, code.bitLength);
            }
        }

        // Flush any remaining bits
        bitWriter.Flush();

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] CompressHuffman completed - Original: %zu, Compressed: %zu, Ratio: %.2f",
            input.size(), compressed.size(), static_cast<float>(input.size()) / static_cast<float>(compressed.size()));
#endif

    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] CompressHuffman exception: " + wErrorMsg);
#endif
        return input; // Return original data on error
    }

    return compressed;
}

std::vector<uint8_t> PUNPack::DecompressHuffman(const std::vector<uint8_t>& input, size_t originalSize) const
{
    std::vector<uint8_t> decompressed;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] DecompressHuffman processing %zu bytes to %zu bytes",
        input.size(), originalSize);
#endif

    // Handle empty input
    if (input.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] DecompressHuffman called with empty input");
#endif
        return decompressed;
    }

    // Handle single byte special case
    if (input.size() == 2 && input[0] == 0xFF)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[PUNPack] DecompressHuffman single byte special case");
#endif
        decompressed.assign(originalSize, input[1]);
        return decompressed;
    }

    try
    {
        size_t readIndex = 0;

        // Check for Huffman tree marker
        if (readIndex >= input.size() || input[readIndex] != 0xFE)
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHuffman invalid tree marker");
#endif
            return DecompressRLE(input, originalSize); // Fallback to RLE
        }
        readIndex++;

        // Step 1: Deserialize the Huffman tree
        std::function<std::shared_ptr<HuffmanNode>()> deserializeTree =
            [&]() -> std::shared_ptr<HuffmanNode> {
            if (readIndex >= input.size())
            {
                return nullptr;
            }

            uint8_t marker = input[readIndex++];

            if (marker == 0x01)
            {
                // Leaf node
                if (readIndex >= input.size())
                {
                    return nullptr;
                }
                uint8_t symbol = input[readIndex++];
                return std::make_shared<HuffmanNode>(symbol, 0); // Frequency not needed for decompression
            }
            else if (marker == 0x00)
            {
                // Internal node
                auto left = deserializeTree();
                auto right = deserializeTree();
                if (!left || !right)
                {
                    return nullptr;
                }
                return std::make_shared<HuffmanNode>(0, left, right);
            }
            else
            {
                // Invalid marker
                return nullptr;
            }
            };

        auto root = deserializeTree();
        if (!root)
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHuffman failed to deserialize tree");
#endif
            return std::vector<uint8_t>();
        }

        // Step 2: Read original data size (4 bytes)
        if (readIndex + 4 > input.size())
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHuffman insufficient data for size header");
#endif
            return std::vector<uint8_t>();
        }

        uint32_t expectedSize = 0;
        for (int i = 0; i < 4; ++i)
        {
            expectedSize = (expectedSize << 8) | input[readIndex++];
        }

        // Verify expected size matches originalSize parameter
        if (expectedSize != originalSize)
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[PUNPack] Size mismatch - Expected: %u, Parameter: %zu",
                expectedSize, originalSize);
#endif
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressHuffman tree deserialized, decoding %u bytes", expectedSize);
#endif

        // Step 3: Decode the compressed data
        decompressed.reserve(expectedSize);

        // Create bit reader for the remaining compressed data
        std::vector<uint8_t> compressedData(input.begin() + readIndex, input.end());
        BitReader bitReader(compressedData);

        // Decode symbols using tree traversal
        while (decompressed.size() < expectedSize && bitReader.HasMoreBits())
        {
            auto currentNode = root;

            // Traverse tree until we reach a leaf
            while (!currentNode->isLeaf && bitReader.HasMoreBits())
            {
                bool bit = bitReader.ReadBit();
                currentNode = bit ? currentNode->right : currentNode->left;

                // Safety check for corrupted data
                if (!currentNode)
                {
#if defined(_DEBUG_PUNPACK_)
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHuffman tree traversal error");
#endif
                    return std::vector<uint8_t>();
                }
            }

            // If we reached a leaf, output the symbol
            if (currentNode->isLeaf)
            {
                decompressed.push_back(currentNode->symbol);
            }
            else
            {
                // Incomplete symbol due to end of data
#if defined(_DEBUG_PUNPACK_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] DecompressHuffman incomplete symbol at end of stream");
#endif
                break;
            }
        }

        // Verify we decoded the expected amount of data
        if (decompressed.size() != expectedSize)
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[PUNPack] DecompressHuffman size mismatch - Expected: %u, Got: %zu",
                expectedSize, decompressed.size());
#endif
        }

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] DecompressHuffman completed successfully - Decoded: %zu bytes",
            decompressed.size());
#endif

    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHuffman exception: " + wErrorMsg);
#endif
        return std::vector<uint8_t>(); // Return empty vector on error
    }

    return decompressed;
}

std::vector<uint8_t> PUNPack::CompressHybrid(const std::vector<uint8_t>& input) const
{
#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressHybrid processing %zu bytes", input.size());
#endif

    try
    {
        // Hybrid approach: try multiple compression methods and choose the best
        std::vector<uint8_t> rleResult = CompressRLE(input);
        std::vector<uint8_t> lz77Result = CompressLZ77(input);

        // Choose the method that gives better compression
        if (rleResult.size() < lz77Result.size() && rleResult.size() < input.size())
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressHybrid chose RLE compression");
#endif

            // Prepend method identifier
            std::vector<uint8_t> result;
            result.push_back(0x01); // RLE identifier
            result.insert(result.end(), rleResult.begin(), rleResult.end());
            return result;
        }
        else if (lz77Result.size() < input.size())
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressHybrid chose LZ77 compression");
#endif

            // Prepend method identifier
            std::vector<uint8_t> result;
            result.push_back(0x02); // LZ77 identifier
            result.insert(result.end(), lz77Result.begin(), lz77Result.end());
            return result;
        }
        else
        {
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CompressHybrid chose no compression");
#endif

            // No compression gives better results
            std::vector<uint8_t> result;
            result.push_back(0x00); // No compression identifier
            result.insert(result.end(), input.begin(), input.end());
            return result;
        }
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] CompressHybrid exception: " + wErrorMsg);
#endif
        return input; // Return original data on error
    }
}

std::vector<uint8_t> PUNPack::DecompressHybrid(const std::vector<uint8_t>& input, size_t originalSize) const
{
#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressHybrid processing %zu bytes to %zu bytes",
        input.size(), originalSize);
#endif

    if (input.empty())
    {
        return std::vector<uint8_t>();
    }

    try
    {
        // Read compression method identifier
        uint8_t method = input[0];
        std::vector<uint8_t> compressedData(input.begin() + 1, input.end());

        switch (method)
        {
        case 0x00: // No compression
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressHybrid using no compression");
#endif
            return compressedData;

        case 0x01: // RLE compression
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressHybrid using RLE decompression");
#endif
            return DecompressRLE(compressedData, originalSize);

        case 0x02: // LZ77 compression
#if defined(_DEBUG_PUNPACK_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DecompressHybrid using LZ77 decompression");
#endif
            return DecompressLZ77(compressedData, originalSize);

        default:
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHybrid unknown method: 0x%02X", method);
#endif
            return std::vector<uint8_t>(); // Return empty vector on unknown method
        }
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DecompressHybrid exception: " + wErrorMsg);
#endif
        return std::vector<uint8_t>(); // Return empty vector on error
    }
}

//==============================================================================
// Internal Utility Methods Implementation
//==============================================================================
std::vector<uint8_t> PUNPack::DataToByteVector(const void* data, size_t size) const
{
    std::vector<uint8_t> result;

    // Validate input parameters
    if (data == nullptr || size == 0)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[PUNPack] DataToByteVector called with null data or zero size");
#endif
        return result;
    }

    try
    {
        // Reserve space for efficiency
        result.reserve(size);

        // Copy data byte by byte
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        result.assign(bytes, bytes + size);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] DataToByteVector converted %zu bytes", size);
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] DataToByteVector exception: " + wErrorMsg);
#endif
        result.clear(); // Return empty vector on error
    }

    return result;
}

bool PUNPack::ValidatePackResult(const PackResult& result) const
{
#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] ValidatePackResult called");
#endif

    // Check magic header
    if (result.magicHeader != PUNPACK_MAGIC_HEADER)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Invalid magic header: 0x%016llX", result.magicHeader);
#endif
        return false;
    }

    // Check version compatibility
    if (result.version != PUNPACK_VERSION)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Version mismatch: 0x%08X", result.version);
#endif
        return false;
    }

    // Check size consistency
    if (result.originalSize == 0 || result.compressedSize == 0)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] Invalid sizes in pack result");
#endif
        return false;
    }

    // Check compressed data availability
    if (result.compressedData.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] No compressed data in pack result");
#endif
        return false;
    }

    // Check compressed data size matches reported size
    if (result.compressedData.size() != result.compressedSize)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Compressed data size mismatch: expected %zu, got %zu",
            result.compressedSize, result.compressedData.size());
#endif
        return false;
    }

    // Check encryption key consistency
    if (result.isEncrypted && result.decipherKey.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] Encrypted data but no decipher key");
#endif
        return false;
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] PackResult validation passed");
#endif

    return true;
}

void PUNPack::UpdateStatistics(size_t originalSize, size_t compressedSize, float compressionTime, float decompressionTime)
{
    // Thread-safe update of statistics
    std::lock_guard<std::mutex> lock(m_statisticsMutex);

    // Update counters atomically
    m_totalBytesProcessed.fetch_add(originalSize);
    m_totalBytesCompressed.fetch_add(compressedSize);
    m_totalOperations.fetch_add(1);

    // Convert times to microseconds and add to totals
    uint64_t compressionMicroseconds = static_cast<uint64_t>(compressionTime * 1000.0f);
    uint64_t decompressionMicroseconds = static_cast<uint64_t>(decompressionTime * 1000.0f);

    m_totalCompressionTime.fetch_add(compressionMicroseconds);
    m_totalDecompressionTime.fetch_add(decompressionMicroseconds);

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[PUNPack] Statistics updated - Operation #%zu",
        m_totalOperations.load());
#endif
}

void PUNPack::InitializeCRC32Table()
{
#if defined(_DEBUG_PUNPACK_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] Initializing CRC32 lookup table");
#endif

    try
    {
        // Generate CRC32 lookup table for fast checksum calculation
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t crc = i;
            for (int j = 0; j < 8; ++j)
            {
                if (crc & 1)
                {
                    crc = (crc >> 1) ^ PUNPACK_CHECKSUM_POLYNOMIAL;
                }
                else
                {
                    crc >>= 1;
                }
            }
            m_crc32Table[i] = crc;
        }

        m_crc32TableInitialized = true;

#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PUNPack] CRC32 lookup table initialized successfully");
#endif
    }
    catch (const std::exception& e)
    {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] CRC32 table initialization exception: " + wErrorMsg);
#endif
        m_crc32TableInitialized = false;
    }
}

uint32_t PUNPack::CalculateCRC32Fast(const void* data, size_t size) const
{
    // Fast CRC32 calculation using pre-computed lookup table
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    // Process data using lookup table for speed optimization
    for (size_t i = 0; i < size; ++i)
    {
        uint8_t tableIndex = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ m_crc32Table[tableIndex];
    }

    return crc ^ 0xFFFFFFFF;
}

#pragma warning(pop)
