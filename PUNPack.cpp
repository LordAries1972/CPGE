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
// Blowfish / bcrypt Static Initialisation Constants
// Source: Schneier's original paper (1994) - hexadecimal digits of pi
//==============================================================================
static const uint32_t BF_P_INIT[18] = {
    0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344,
    0xa4093822, 0x299f31d0, 0x082efa98, 0xec4e6c89,
    0x452821e6, 0x38d01377, 0xbe5466cf, 0x34e90c6c,
    0xc0ac29b7, 0xc97c50dd, 0x3f84d5b5, 0xb5470917,
    0x9216d5d9, 0x8979fb1b
};

static const uint32_t BF_S_INIT[4][256] = {
    {   // S[0]
        0xd1310ba6, 0x98dfb5ac, 0x2ffd72db, 0xd01adfb7, 0xb8e1afed, 0x6a267e96, 0xba7c9045, 0xf12c7f99,
        0x24a19947, 0xb3916cf7, 0x0801f2e2, 0x858efc16, 0x636920d8, 0x71574e69, 0xa458fea3, 0xf4933d7e,
        0x0d95748f, 0x728eb658, 0x718bcd58, 0x82154aee, 0x7b54a41d, 0xc25a59b5, 0x9c30d539, 0x2af26013,
        0xc5d1b023, 0x286085f0, 0xca417918, 0xb8db38ef, 0x8e79dcb0, 0x603a180e, 0x6c9e0e8b, 0xb01e8a3e,
        0xd71577c1, 0xbd314b27, 0x78af2fda, 0x55605c60, 0xe65525f3, 0xaa55ab94, 0x57489862, 0x63e81440,
        0x55ca396a, 0x2aab10b6, 0xb4cc5c34, 0x1141e8ce, 0xa15486af, 0x7c72e993, 0xb3ee1411, 0x636fbc2a,
        0x2ba9c55d, 0x741831f6, 0xce5c3e16, 0x9b87931e, 0xafd6ba33, 0x6c24cf5c, 0x7a325381, 0x28958677,
        0x3b8f4898, 0x6b4bb9af, 0xc4bfe81b, 0x66282193, 0x61d809cc, 0xfb21a991, 0x487cac60, 0x5dec8032,
        0xef845d5d, 0xe98575b1, 0xdc262302, 0xeb651b88, 0x23893e81, 0xd396acc5, 0x0f6d6ff3, 0x83f44239,
        0x2e0b4482, 0xa4842004, 0x69c8f04a, 0x9e1f9b5e, 0x21c66842, 0xf6e96c9a, 0x670c9c61, 0xabd388f0,
        0x6a51a0d2, 0xd8542f68, 0x960fa728, 0xab5133a3, 0x6eef0b6c, 0x137a3be4, 0xba3bf050, 0x7efb2a98,
        0xa1f1651d, 0x39af0176, 0x66ca593e, 0x82430e88, 0x8cee8619, 0x456f9fb4, 0x7d84a5c3, 0x3b8b5ebe,
        0xe06f75d8, 0x85c12073, 0x401a449f, 0x56c16aa6, 0x4ed3aa62, 0x363f7706, 0x1bfedf72, 0x429b023d,
        0x37d0d724, 0xd00a1248, 0xdb0fead3, 0x49f1c09b, 0x075372c9, 0x80991b7b, 0x25d479d8, 0xf6e8def7,
        0xe3fe501a, 0xb6794c3b, 0x976ce0bd, 0x04c006ba, 0xc1a94fb6, 0x409f60c4, 0x5e5c9ec2, 0x196a2463,
        0x68fb6faf, 0x3e6c53b5, 0x1339b2eb, 0x3b52ec6f, 0x6dfc511f, 0x9b30952c, 0xcc814544, 0xaf5ebd09,
        0xbee3d004, 0xde334afd, 0x660f2807, 0x192e4bb3, 0xc0cba857, 0x45c8740f, 0xd20b5f39, 0xb9d3fbdb,
        0x5579c0bd, 0x1a60320a, 0xd6a100c6, 0x402c7279, 0x679f25fe, 0xfb1fa3cc, 0x8ea5e9f8, 0xdb3222f8,
        0x3c7516df, 0xfd616b15, 0x2f501ec8, 0xad0552ab, 0x323db5fa, 0xfd238760, 0x53317b48, 0x3e00df82,
        0x9e5c57bb, 0xca6f8ca0, 0x1a87562e, 0xdf1769db, 0xd542a8f6, 0x287effc3, 0xac6732c6, 0x8c4f5573,
        0x695b27b0, 0xbbca58c8, 0xe1ffa35d, 0xb8f011a0, 0x10fa3d98, 0xfd2183b8, 0x4afcb56c, 0x2dd1d35b,
        0x9a53e479, 0xb6f84565, 0xd28e49bc, 0x4bfb9790, 0xe1ddf2da, 0xa4cb7e33, 0x62fb1341, 0xcee4c6e8,
        0xef20cada, 0x36774c01, 0xd07e9efe, 0x2bf11fb4, 0x95dbda4d, 0xae909198, 0xeaad8e71, 0x6b93d5a0,
        0xd08ed1d0, 0xafc725e0, 0x8e3c5b2f, 0x8e7594b7, 0x8ff6e2fb, 0xf2122b64, 0x8888b812, 0x900df01c,
        0x4fad5ea0, 0x688fc31c, 0xd1cff191, 0xb3a8c1ad, 0x2f2f2218, 0xbe0e1777, 0xea752dfe, 0x8b021fa1,
        0xe5a0cc0f, 0xb56f74e8, 0x18acf3d6, 0xce89e299, 0xb4a84fe0, 0xfd13e0b7, 0x7cc43b81, 0xd2ada8d9,
        0x165fa266, 0x80957705, 0x93cc7314, 0x211a1477, 0xe6ad2065, 0x77b5fa86, 0xc75442f5, 0xfb9d35cf,
        0xebcdaf0c, 0x7b3e89a0, 0xd6411bd3, 0xae1e7e49, 0x00250e2d, 0x2071b35e, 0x226800bb, 0x57b8e0af,
        0x2464369b, 0xf009b91e, 0x5563911d, 0x59dfa6aa, 0x78c14389, 0xd95a537f, 0x207d5ba2, 0x02e5b9c5,
        0x83260376, 0x6295cfa9, 0x11c81968, 0x4e734a41, 0xb3472dca, 0x7b14a94a, 0x1b510052, 0x9a532915,
        0xd60f573f, 0xbc9bc6e4, 0x2b60a476, 0x81e67400, 0x08ba6fb5, 0x571be91f, 0xf296ec6b, 0x2a0dd915,
        0xb6636521, 0xe7b9f9b6, 0xff34052e, 0xc5855664, 0x53b02d5d, 0xa99f8fa1, 0x08ba4799, 0x6e85076a
    },
    {   // S[1]
        0x4b7a70e9, 0xb5b32944, 0xdb75092e, 0xc4192623, 0xad6ea6b0, 0x49a7df7d, 0x9cee60b8, 0x8fedb266,
        0xecaa8c71, 0x699a17ff, 0x5664526c, 0xc2b19ee1, 0x193602a5, 0x75094c29, 0xa0591340, 0xe4183a3e,
        0x3f54989a, 0x5b429d65, 0x6b8fe4d6, 0x99f73fd6, 0xa1d29c07, 0xefe830f5, 0x4d2d38e6, 0xf0255dc1,
        0x4cdd2086, 0x8470eb26, 0x6382e9c6, 0x021ecc5e, 0x09686b3f, 0x3ebaefc9, 0x3c971814, 0x6b6a70a1,
        0x687f3584, 0x52a0e286, 0xb79c5305, 0xaa500737, 0x3e07841c, 0x7fdeae5c, 0x8e7d44ec, 0x5716f2b8,
        0xb03ada37, 0xf0500c0d, 0xf01c1f04, 0x0200b3ff, 0xae0cf51a, 0x3cb574b2, 0x25837a58, 0xdc0921bd,
        0xd19113f9, 0x7ca92ff6, 0x94324773, 0x22f54701, 0x3ae5e581, 0x37c2dadc, 0xc8b57634, 0x9af3dda7,
        0xa9446146, 0x0fd0030e, 0xecc8c73e, 0xa4751e41, 0xe238cd99, 0x3bea0e2f, 0x3280bba1, 0x183eb331,
        0x4e548b38, 0x4f6db908, 0x6f420d03, 0xf60a04bf, 0x2cb81290, 0x24977c79, 0x5679b072, 0xbcaf89af,
        0xde9a771f, 0xd9930810, 0xb38bae12, 0xdccf3f2e, 0x5512721f, 0x2e6b7124, 0x501adde6, 0x9f84cd87,
        0x7a584718, 0x7408da17, 0xbc9f9abc, 0xe94b7d8c, 0xec7aec3a, 0xdb851dfa, 0x63094366, 0xc464c3d2,
        0xef1c1847, 0x3215d908, 0xdd433b37, 0x24c2ba16, 0x12a14d43, 0x2a65c451, 0x50940002, 0x133ae4dd,
        0x71dff89e, 0x10314e55, 0x81ac77d6, 0x5f11199b, 0x043556f1, 0xd7a3c76b, 0x3c11183b, 0x5924a509,
        0xf28fe6ed, 0x97f1fbfa, 0x9ebabf2c, 0x1e153c6e, 0x86e34570, 0xeae96fb1, 0x860e5e0a, 0x5a3e2ab3,
        0x771fe71c, 0x4e3d06fa, 0x2965dcb9, 0x99e71d0f, 0x803e89d6, 0x5266c825, 0x2e4cc978, 0x9c10b36a,
        0xc6150eba, 0x94e2ea78, 0xa5fc3c53, 0x1e0a2df4, 0xf2f74ea7, 0x361d2b3d, 0x1939260f, 0x19c27960,
        0x5223a708, 0xf71312b6, 0xebadfe6e, 0xeac31f66, 0xe3bc4595, 0xa67bc883, 0xb17f37d1, 0x018cff28,
        0xc332ddef, 0xbe6c5aa5, 0x65582185, 0x68ab9802, 0xeecea50f, 0xdb2f953b, 0x2aef7dad, 0x5b6e2f84,
        0x1521b628, 0x29076170, 0xecdd4775, 0x619f1510, 0x13cca830, 0xeb61bd96, 0x0334fe1e, 0xaa0363cf,
        0xb5735c90, 0x4c70a239, 0xd59e9e0b, 0xcbaade14, 0xeecc86bc, 0x60622ca7, 0x9cab5cab, 0xb2f3846e,
        0x648b1eaf, 0x19bdf0ca, 0xa02369b9, 0x655abb50, 0x40685a32, 0x3c2ab4b3, 0x319ee9d5, 0xc021b8f7,
        0x9b540b19, 0x875fa099, 0x95f7997e, 0x623d7da8, 0xf837889a, 0x97e32d77, 0x11ed935f, 0x16681281,
        0x0e358829, 0xc7e61fd6, 0x96dedfa1, 0x7858ba99, 0x57f584a5, 0x1b227263, 0x9b83c3ff, 0x1ac24696,
        0xcdb30aeb, 0x532e3054, 0x8fd948e4, 0x6dbc3128, 0x58ebf2ef, 0x34c6ffea, 0xfe28ed61, 0xee7c3c73,
        0x5d4a14d9, 0xe864b7e3, 0x42105d14, 0x203e13e0, 0x45eee2b6, 0xa3aaabea, 0xdb6c4f15, 0xfacb4fd0,
        0xc742f442, 0xef6abbb5, 0x654f3b1d, 0x41cd2105, 0xd81e799e, 0x86854dc7, 0xe44b476a, 0x3d816250,
        0xcf62a1f2, 0x5b8d2646, 0xfc8883a0, 0xc1c7b6a3, 0x7f1524c3, 0x69cb7492, 0x47848a0b, 0x5692b285,
        0x095bbf00, 0xad19489d, 0x1462b174, 0x23820e00, 0x58428d2a, 0x0c55f5ea, 0x1dadf43e, 0x233f7061,
        0x3372f092, 0x8d937e41, 0xd65fecf1, 0x6c223bdb, 0x7cde3759, 0xcbee7460, 0x4085f2a7, 0xce77326e,
        0xa6078084, 0x19f8509e, 0xe8efd855, 0x61d99735, 0xa969a7aa, 0xc50c06c2, 0x5a04abfc, 0x800bcadc,
        0x9e447a2e, 0xc3453484, 0xfdd56705, 0x0e1e9ec9, 0xdb73dbd3, 0x105588cd, 0x675fda79, 0xe3674340,
        0xc5c43465, 0x713e38d8, 0x3d28f89e, 0xf16dff20, 0x153e21e7, 0x8fb03d4a, 0xe6e39f2b, 0xdb83adf7
    },
    {   // S[2]
        0xe93d5a68, 0x948140f7, 0xf64c261c, 0x94692934, 0x411520f7, 0x7602d4f7, 0xbcf46b2e, 0xd4a20068,
        0xd4082471, 0x3320f46a, 0x43b7d4b7, 0x500061af, 0x1e39f62e, 0x97244546, 0x14214f74, 0xbf8b8840,
        0x4d95fc1d, 0x96b591af, 0x70f4ddd3, 0x66a02f45, 0xbfbc09ec, 0x03bd9785, 0x7fac6dd0, 0x31cb8504,
        0x96eb27b3, 0x55fd3941, 0xda2547e6, 0xabca0a9a, 0x28507825, 0x530429f4, 0x0a2c86da, 0xe9b66dfb,
        0x68dc1462, 0xd7486900, 0x680ec0a4, 0x27a18dee, 0x4f3ffea2, 0xe887ad8c, 0xb58ce006, 0x7af4d6b6,
        0xaace1e7c, 0xd3375fec, 0xce78a399, 0x406b2a42, 0x20fe9e35, 0xd9f385b9, 0xee39d7ab, 0x3b124e8b,
        0x1dc9faf7, 0x4b6d1856, 0x26a36631, 0xeae397b2, 0x3a6efa74, 0xdd5b4332, 0x6841e7f7, 0xca7820fb,
        0xfb0af54e, 0xd8feb397, 0x454056ac, 0xba489527, 0x55533a3a, 0x20838d87, 0xfe6ba9b7, 0xd096954b,
        0x55a867bc, 0xa1159a58, 0xcca92963, 0x99e1db33, 0xa62a4a56, 0x3f3125f9, 0x5ef47e1c, 0x9029317c,
        0xfdf8e802, 0x04272f70, 0x80bb155c, 0x05282ce3, 0x95c11548, 0xe4c66d22, 0x48c1133f, 0xc70f86dc,
        0x07f9c9ee, 0x41041f0f, 0x404779a4, 0x5d886e17, 0x325f51eb, 0xd59bc0d1, 0xf2bcc18f, 0x41113564,
        0x257b7834, 0x602a9c60, 0xdff8e8a3, 0x1f636c1b, 0x0e12b4c2, 0x02e1329e, 0xaf664fd1, 0xcad18115,
        0x6b2395e0, 0x333e92e1, 0x3b240b62, 0xeebeb922, 0x85b2a20e, 0xe6ba0d99, 0xde720c8c, 0x2da2f728,
        0xd0127845, 0x95b794fd, 0x647d0862, 0xe7ccf5f0, 0x5449a36f, 0x877d48fa, 0xc39dfd27, 0xf33e8d1e,
        0x0a476341, 0x992eff74, 0x3a6f6eab, 0xf4f8fd37, 0xa812dc60, 0xa1ebddf8, 0x991be14c, 0xdb6e6b0d,
        0xc67b5510, 0x6d672c37, 0x2765d43b, 0xdcd0e804, 0xf1290dc7, 0xcc00ffa3, 0xb5390f92, 0x690fed0b,
        0x667b9ffb, 0xcedb7d9c, 0xa091cf0b, 0xd9155ea3, 0xbb132f88, 0x515bad24, 0x7b9479bf, 0x763bd6eb,
        0x37392eb3, 0xcc115979, 0x8026e297, 0xf42e312d, 0x6842ada7, 0xc66a2b3b, 0x12754ccc, 0x782ef11c,
        0x6a124237, 0xb79251e7, 0x06a1bbe6, 0x4bfb6350, 0x1a6b1018, 0x11caedfa, 0x3d25bdd8, 0xe2e1c3c9,
        0x44421659, 0x0a121386, 0xd90cec6e, 0xd5abea2a, 0x64af674e, 0xda86a85f, 0xbebfe988, 0x64e4c3fe,
        0x9dbc8057, 0xf0f7c086, 0x60787bf8, 0x6003604d, 0xd1fd8346, 0xf6381fb0, 0x7745ae04, 0xd736fccc,
        0x83426b33, 0xf01eab71, 0xb0804187, 0x3c005e5f, 0x77a057be, 0xbde8ae24, 0x55464299, 0xbf582e61,
        0x4e58f48f, 0xf2ddfda2, 0xf474ef38, 0x8789bdc2, 0x5366f9c3, 0xc8b38e74, 0xb475f255, 0x46fcd9b9,
        0x7aeb2661, 0x8b1ddf84, 0x846a0e79, 0x915f95e2, 0x466e598e, 0x20b45770, 0x8cd55591, 0xc902de4c,
        0xb90bace1, 0xbb8205d0, 0x11a86248, 0x7574a99e, 0xb77f19b6, 0xe0a9dc09, 0x662d09a1, 0xc4324633,
        0xe85a1f02, 0x09f0be8c, 0x4a99a025, 0x1d6efe10, 0x1ab93d1d, 0x0ba5a4df, 0xa186f20f, 0x2868f169,
        0xdcb7da83, 0x573906fe, 0xa1e2ce9b, 0x4fcd7f52, 0x50115e01, 0xa70683fa, 0xa002b5c4, 0x0de6d027,
        0x9af88c27, 0x773f8641, 0xc3604c06, 0x61a806b5, 0xf0177a28, 0xc0f586e0, 0x006058aa, 0x30dc7d62,
        0x11e69ed7, 0x2338ea63, 0x53c2dd94, 0xc2c21634, 0xbbcbee56, 0x90bcb6de, 0xebfc7da1, 0xce591d76,
        0x6f05e409, 0x4b7c0188, 0x39720a3d, 0x7c927c24, 0x86e3725f, 0x724d9db9, 0x1ac15bb4, 0xd39eb8fc,
        0xed545578, 0x08fca5b5, 0xd83d7cd3, 0x4dad0fc4, 0x1e50ef5e, 0xb161e6f8, 0xa28514d9, 0x6c51133c,
        0x6fd5c7e7, 0x56e14ec4, 0x362abfce, 0xddc6c837, 0xd79a3234, 0x92638212, 0x670efa8e, 0x406000e0
    },
    {   // S[3]
        0x3a39ce37, 0xd3faf5cf, 0xabc27737, 0x5ac52d1b, 0x5cb0679e, 0x4fa33742, 0xd3822740, 0x99bc9bbe,
        0xd5118e9d, 0xbf0f7315, 0xd62d1c7e, 0xc700c47b, 0xb78c1b6b, 0x21a19045, 0xb26eb1be, 0x6a366eb4,
        0x5748ab2f, 0xbc946e79, 0xc6a376d2, 0x6549c2c8, 0x530ff8ee, 0x468dde7d, 0xd5730a1d, 0x4cd04dc6,
        0x2939bbdb, 0xa9ba4650, 0xac9526e8, 0xbe5ee304, 0xa1fad5f0, 0x6a2d519a, 0x63ef8ce2, 0x9a86ee22,
        0xc089c2b8, 0x43242ef6, 0xa51e03aa, 0x9cf2d0a4, 0x83c061ba, 0x9be96a4d, 0x8fe51550, 0xba645bd6,
        0x2826a2f9, 0xa73a3ae1, 0x4ba99586, 0xef5562e9, 0xc72fefd3, 0xf752f7da, 0x3f046f69, 0x77fa0a59,
        0x80e4a915, 0x87b08601, 0x9b09e6ad, 0x3b3ee593, 0xe990fd5a, 0x9e34d797, 0x2cf0b7d9, 0x022b8b51,
        0x96d5ac3a, 0x017da67d, 0xd1cf3ed6, 0x7c7d2d28, 0x1f9f25cf, 0xadf2b89b, 0x5ad6b472, 0x5a88f54c,
        0xe029ac71, 0xe019a5e6, 0x47b0acfd, 0xed93fa9b, 0xe8d3c48d, 0x283b57cc, 0xf8d56629, 0x79132e28,
        0x785f0191, 0xed756055, 0xf7960e44, 0xe3d35e8c, 0x15056dd4, 0x88f46dba, 0x03a16125, 0x0564f0bd,
        0xc3eb9e15, 0x3c9057a2, 0x97271aec, 0xa93a072a, 0x1b3f6d9b, 0x1e6321f5, 0xf59c66fb, 0x26dcf319,
        0x7533d928, 0xb155fdf5, 0x03563482, 0x8aba3cbb, 0x28517711, 0xc20ad9f8, 0xabcc5167, 0xccad925f,
        0x4de81751, 0x3830dc8e, 0x379d5862, 0x9320f991, 0xea7a90c2, 0xfb3e7bce, 0x5121ce64, 0x774fbe32,
        0xa8b6e37e, 0xc3293d46, 0x48de5369, 0x6413e680, 0xa2ae0810, 0xdd6db224, 0x69852dfd, 0x09072166,
        0xb39a460a, 0x6445c0dd, 0x586cdecf, 0x1c20c8ae, 0x5bbef7dd, 0x1b588d40, 0xccd2017f, 0x6bb4e3bb,
        0xdda26a7e, 0x3a59ff45, 0x3e350a44, 0xbcb4cdd5, 0x72eacea8, 0xfa6484bb, 0x8d6612ae, 0xbf3c6f47,
        0xd29be463, 0x542f5d9e, 0xaec2771b, 0xf64e6370, 0x740e0d8d, 0xe75b1357, 0xf8721671, 0xaf537d5d,
        0x4040cb08, 0x4eb4e2cc, 0x34d2466a, 0x0115af84, 0xe1b00428, 0x95983a1d, 0x06b89fb4, 0xce6ea048,
        0x6f3f3b82, 0x3520ab82, 0x011a1d4b, 0x277227f8, 0x611560b1, 0xe7933fdc, 0xbb3a792b, 0x344525bd,
        0xa08839e1, 0x51ce794b, 0x2f32c9b7, 0xa01fbac9, 0xe01cc87e, 0xbcc7d1f6, 0xcf0111c3, 0xa1e8aac7,
        0x1a908749, 0xd44fbd9a, 0xd0dadecb, 0xd50ada38, 0x0339c32a, 0xc6913667, 0x8df9317c, 0xe0b12b4f,
        0xf79e59b7, 0x43f5bb3a, 0xf2d519ff, 0x27d9459c, 0xbf97222c, 0x15e6fc2a, 0x0f91fc71, 0x9b941525,
        0xfae59361, 0xceb69ceb, 0xc2a86459, 0x12baa8d1, 0xb6c1075e, 0xe3056a0c, 0x10d25065, 0xcb03a442,
        0xe0ec6e0e, 0x1698db3b, 0x4c98a0be, 0x3278e964, 0x9f1f9532, 0xe0d392df, 0xd3a0342b, 0x8971f21e,
        0x1b0a7441, 0x4ba3348c, 0xc5be7120, 0xc37632d8, 0xdf359f8d, 0x9b992f2e, 0xe60b6f47, 0x0fe3f11d,
        0xe54cda54, 0x1edad891, 0xce6279cf, 0xcd3e7e6f, 0x1618b166, 0xfd2c1d05, 0x848fd2c5, 0xf6fb2299,
        0xf523f357, 0xa6327623, 0x93a83531, 0x56cccd02, 0xacf08162, 0x5a75ebb5, 0x6e163697, 0x88d273cc,
        0xde966292, 0x81b949d0, 0x4c50901b, 0x71c65614, 0xe6c6c7bd, 0x327a140a, 0x45e1d006, 0xc3f27b9a,
        0xc9aa53fd, 0x62a80f00, 0xbb25bfe2, 0x35bdd2f6, 0x71126905, 0xb2040222, 0xb6cbcf7c, 0xcd769c2b,
        0x53113ec0, 0x1640e3d3, 0x38abbd60, 0x2547adf0, 0xba38209c, 0xf746ce76, 0x77afa1c5, 0x20756060,
        0x85cbfe4e, 0x8ae88dd8, 0x7aaaf9b0, 0x4cf9aa7e, 0x1948c25c, 0x02fb8a8c, 0x01c36ae4, 0xd6ebe1f9,
        0x90d4f869, 0xa65cdea0, 0x3f09252d, 0xc208e69f, 0xb74e6132, 0xce77e25b, 0x578fdfe3, 0x3ac372e6
    }
};

// bcrypt magic ciphertext (the text "OrpheanBeholderScryDoubt")
static const uint8_t BF_CIPHERTEXT[24] = {
    0x4f, 0x72, 0x70, 0x68, 0x65, 0x61, 0x6e, 0x42,  // OrpheanB
    0x65, 0x68, 0x6f, 0x6c, 0x64, 0x65, 0x72, 0x53,  // eholderS
    0x63, 0x72, 0x79, 0x44, 0x6f, 0x75, 0x62, 0x74   // cryDoubt
};

// bcrypt base64 alphabet: ./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789
static const char BF_BASE64_CHARS[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

//==============================================================================
// Blowfish / bcrypt Internal Helper Implementations
//==============================================================================
void PUNPack::BlowfishInitState(BlowfishState& state) const
{
    // Copy pi-derived P-array and S-box initialisation values
    for (int i = 0; i < 18; ++i)
        state.P[i] = BF_P_INIT[i];
    for (int s = 0; s < 4; ++s)
        for (int j = 0; j < 256; ++j)
            state.S[s][j] = BF_S_INIT[s][j];
}

uint32_t PUNPack::BlowfishF(const BlowfishState& state, uint32_t x) const
{
    // Feistel F-function: two S-box adds and one XOR
    return ((state.S[0][(x >> 24) & 0xFF] + state.S[1][(x >> 16) & 0xFF]) ^
             state.S[2][(x >>  8) & 0xFF]) + state.S[3][x & 0xFF];
}

void PUNPack::BlowfishEncipher(const BlowfishState& state, uint32_t& xL, uint32_t& xR) const
{
    // 16-round Feistel forward pass
    for (int i = 0; i < 16; ++i) {
        xL ^= state.P[i];
        xR ^= BlowfishF(state, xL);
        std::swap(xL, xR);
    }
    std::swap(xL, xR);
    xR ^= state.P[16];
    xL ^= state.P[17];
}

void PUNPack::BlowfishDecipher(const BlowfishState& state, uint32_t& xL, uint32_t& xR) const
{
    // 16-round Feistel reverse pass
    for (int i = 17; i > 1; --i) {
        xL ^= state.P[i];
        xR ^= BlowfishF(state, xL);
        std::swap(xL, xR);
    }
    std::swap(xL, xR);
    xR ^= state.P[1];
    xL ^= state.P[0];
}

void PUNPack::BlowfishExpandKey(BlowfishState& state,
                                const uint8_t* key,  size_t keyLen,
                                const uint8_t* data, size_t dataLen) const
{
    // XOR P-array with key bytes (cycling through the key)
    size_t ki = 0;
    for (int i = 0; i < 18; ++i) {
        uint32_t word = 0;
        for (int k = 0; k < 4; ++k) {
            word = (word << 8) | (keyLen > 0 ? key[ki % keyLen] : 0);
            ++ki;
        }
        state.P[i] ^= word;
    }

    // Successively encrypt a block drawn from data (cycling) to fill P then S-boxes
    uint32_t xL = 0, xR = 0;
    size_t di = 0;

    auto nextBlock = [&]() {
        uint32_t l = 0, r = 0;
        for (int k = 0; k < 4; ++k) { l = (l << 8) | (dataLen > 0 ? data[di % dataLen] : 0); ++di; }
        for (int k = 0; k < 4; ++k) { r = (r << 8) | (dataLen > 0 ? data[di % dataLen] : 0); ++di; }
        xL ^= l;
        xR ^= r;
    };

    for (int i = 0; i < 18; i += 2) {
        nextBlock();
        BlowfishEncipher(state, xL, xR);
        state.P[i]     = xL;
        state.P[i + 1] = xR;
    }
    for (int s = 0; s < 4; ++s) {
        for (int i = 0; i < 256; i += 2) {
            nextBlock();
            BlowfishEncipher(state, xL, xR);
            state.S[s][i]     = xL;
            state.S[s][i + 1] = xR;
        }
    }
}

void PUNPack::EksBlowfishSetup(BlowfishState& state,
                                const uint8_t* password, size_t passLen,
                                const uint8_t* salt, int cost) const
{
    // Reset state to pi-derived constants
    BlowfishInitState(state);

    // Initial key expansion mixing password into P and S-boxes with salt as data
    BlowfishExpandKey(state, password, passLen, salt, PUNPACK_BCRYPT_SALT_BYTES);

    // 2^cost rounds of alternating password / salt expansions
    const uint64_t rounds = static_cast<uint64_t>(1) << cost;
    for (uint64_t i = 0; i < rounds; ++i) {
        BlowfishExpandKey(state, password, passLen, nullptr, 0);
        BlowfishExpandKey(state, salt, PUNPACK_BCRYPT_SALT_BYTES, nullptr, 0);
    }
}

std::string PUNPack::BcryptBase64Encode(const uint8_t* data, size_t len) const
{
    // Encode bytes using bcrypt's modified base64 (little-endian 6-bit groups)
    std::string out;
    size_t i = 0;
    while (i < len) {
        uint32_t w = static_cast<uint32_t>(data[i++]);
        if (i < len) w |= static_cast<uint32_t>(data[i++]) << 8;
        if (i < len) w |= static_cast<uint32_t>(data[i++]) << 16;

        out += BF_BASE64_CHARS[w & 0x3f]; w >>= 6;
        out += BF_BASE64_CHARS[w & 0x3f]; w >>= 6;
        out += BF_BASE64_CHARS[w & 0x3f]; w >>= 6;
        out += BF_BASE64_CHARS[w & 0x3f];
    }
    return out;
}

bool PUNPack::BcryptBase64Decode(const std::string& encoded, uint8_t* data, size_t len) const
{
    // Build reverse-lookup for the bcrypt alphabet
    uint8_t rev[128];
    std::memset(rev, 0xFF, sizeof(rev));
    for (int i = 0; i < 64; ++i)
        rev[(uint8_t)BF_BASE64_CHARS[i]] = static_cast<uint8_t>(i);

    size_t outIdx = 0;
    size_t inIdx  = 0;
    while (outIdx < len && inIdx + 3 < encoded.size()) {
        uint8_t c0 = rev[(uint8_t)encoded[inIdx + 0]];
        uint8_t c1 = rev[(uint8_t)encoded[inIdx + 1]];
        uint8_t c2 = rev[(uint8_t)encoded[inIdx + 2]];
        uint8_t c3 = rev[(uint8_t)encoded[inIdx + 3]];
        if (c0 == 0xFF || c1 == 0xFF) break;

        uint32_t w = c0 | (static_cast<uint32_t>(c1) << 6) |
                     (static_cast<uint32_t>(c2) << 12) | (static_cast<uint32_t>(c3) << 18);

        if (outIdx < len) data[outIdx++] = static_cast<uint8_t>(w & 0xFF);
        if (outIdx < len) data[outIdx++] = static_cast<uint8_t>((w >> 8) & 0xFF);
        if (outIdx < len) data[outIdx++] = static_cast<uint8_t>((w >> 16) & 0xFF);
        inIdx += 4;
    }
    return (outIdx == len);
}

std::vector<uint8_t> PUNPack::GenerateSaltBytes(size_t saltSize)
{
    std::vector<uint8_t> salt;
    salt.reserve(saltSize);
    std::lock_guard<std::mutex> lock(m_operationMutex);
    for (size_t i = 0; i < saltSize; ++i)
        salt.push_back(static_cast<uint8_t>(m_byteDistribution(m_randomGenerator)));
    return salt;
}

//==============================================================================
// Blowfish Public API - Symmetric Encrypt / Decrypt (CBC + PKCS7 padding)
//==============================================================================
std::vector<uint8_t> PUNPack::BlowfishEncrypt(const std::vector<uint8_t>& data, const std::string& key)
{
    std::vector<uint8_t> output;

    if (!m_bIsInitialized.load() || data.empty() || key.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] BlowfishEncrypt: invalid input");
#endif
        return output;
    }

    // Clamp key to Blowfish maximum (56 bytes = 448 bits)
    size_t keyLen = std::min(key.size(), PUNPACK_BLOWFISH_MAX_KEY);

    // Initialise cipher state with supplied key
    BlowfishState state;
    BlowfishInitState(state);
    BlowfishExpandKey(state,
                      reinterpret_cast<const uint8_t*>(key.data()), keyLen,
                      nullptr, 0);

    // Apply PKCS7 padding to next multiple of 8 bytes
    std::vector<uint8_t> padded = data;
    uint8_t padLen = static_cast<uint8_t>(
        PUNPACK_BLOWFISH_BLOCK_SIZE - (padded.size() % PUNPACK_BLOWFISH_BLOCK_SIZE));
    padded.insert(padded.end(), padLen, padLen);

    // Prepend a random 8-byte IV
    std::vector<uint8_t> iv = GenerateSaltBytes(PUNPACK_BLOWFISH_BLOCK_SIZE);
    output.insert(output.end(), iv.begin(), iv.end());

    // CBC mode: XOR with previous ciphertext block then encipher
    uint8_t prev[8];
    std::memcpy(prev, iv.data(), 8);

    for (size_t i = 0; i < padded.size(); i += 8)
    {
        // XOR plaintext with chaining value
        uint8_t block[8];
        for (int k = 0; k < 8; ++k)
            block[k] = padded[i + k] ^ prev[k];

        // Pack to big-endian 32-bit words
        uint32_t xL = (static_cast<uint32_t>(block[0]) << 24) | (static_cast<uint32_t>(block[1]) << 16)
                    | (static_cast<uint32_t>(block[2]) <<  8) |  static_cast<uint32_t>(block[3]);
        uint32_t xR = (static_cast<uint32_t>(block[4]) << 24) | (static_cast<uint32_t>(block[5]) << 16)
                    | (static_cast<uint32_t>(block[6]) <<  8) |  static_cast<uint32_t>(block[7]);

        BlowfishEncipher(state, xL, xR);

        uint8_t enc[8] = {
            static_cast<uint8_t>(xL >> 24), static_cast<uint8_t>(xL >> 16),
            static_cast<uint8_t>(xL >>  8), static_cast<uint8_t>(xL),
            static_cast<uint8_t>(xR >> 24), static_cast<uint8_t>(xR >> 16),
            static_cast<uint8_t>(xR >>  8), static_cast<uint8_t>(xR)
        };
        output.insert(output.end(), enc, enc + 8);
        std::memcpy(prev, enc, 8);
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[PUNPack] BlowfishEncrypt: %zu bytes -> %zu bytes (includes 8-byte IV)",
        data.size(), output.size());
#endif
    return output;
}

std::vector<uint8_t> PUNPack::BlowfishDecrypt(const std::vector<uint8_t>& data, const std::string& key)
{
    std::vector<uint8_t> output;

    // Need at least IV (8 bytes) + one cipher block (8 bytes), and aligned to block size
    if (!m_bIsInitialized.load() || data.size() < 16 || key.empty() ||
        (data.size() % PUNPACK_BLOWFISH_BLOCK_SIZE) != 0)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] BlowfishDecrypt: invalid input");
#endif
        return output;
    }

    size_t keyLen = std::min(key.size(), PUNPACK_BLOWFISH_MAX_KEY);

    BlowfishState state;
    BlowfishInitState(state);
    BlowfishExpandKey(state,
                      reinterpret_cast<const uint8_t*>(key.data()), keyLen,
                      nullptr, 0);

    // Extract the prepended IV
    uint8_t prev[8];
    std::memcpy(prev, data.data(), 8);

    // CBC mode decryption
    for (size_t i = 8; i < data.size(); i += 8)
    {
        uint8_t block[8];
        std::memcpy(block, data.data() + i, 8);

        uint32_t xL = (static_cast<uint32_t>(block[0]) << 24) | (static_cast<uint32_t>(block[1]) << 16)
                    | (static_cast<uint32_t>(block[2]) <<  8) |  static_cast<uint32_t>(block[3]);
        uint32_t xR = (static_cast<uint32_t>(block[4]) << 24) | (static_cast<uint32_t>(block[5]) << 16)
                    | (static_cast<uint32_t>(block[6]) <<  8) |  static_cast<uint32_t>(block[7]);

        BlowfishDecipher(state, xL, xR);

        // XOR with previous ciphertext to recover plaintext
        uint8_t dec[8] = {
            static_cast<uint8_t>((xL >> 24) ^ prev[0]),
            static_cast<uint8_t>((xL >> 16) ^ prev[1]),
            static_cast<uint8_t>((xL >>  8) ^ prev[2]),
            static_cast<uint8_t>( xL        ^ prev[3]),
            static_cast<uint8_t>((xR >> 24) ^ prev[4]),
            static_cast<uint8_t>((xR >> 16) ^ prev[5]),
            static_cast<uint8_t>((xR >>  8) ^ prev[6]),
            static_cast<uint8_t>( xR        ^ prev[7])
        };
        output.insert(output.end(), dec, dec + 8);
        std::memcpy(prev, block, 8);
    }

    // Strip PKCS7 padding
    if (!output.empty())
    {
        uint8_t pad = output.back();
        if (pad > 0 && pad <= 8 && pad <= output.size())
        {
            bool valid = true;
            for (size_t k = output.size() - pad; k < output.size(); ++k)
                if (output[k] != pad) { valid = false; break; }
            if (valid)
                output.resize(output.size() - pad);
        }
    }

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[PUNPack] BlowfishDecrypt: %zu bytes -> %zu bytes", data.size(), output.size());
#endif
    return output;
}

std::vector<uint8_t> PUNPack::BlowfishEncryptString(const std::string& plaintext, const std::string& key)
{
    // Convert string to bytes, then encrypt
    std::vector<uint8_t> bytes(plaintext.begin(), plaintext.end());
    return BlowfishEncrypt(bytes, key);
}

std::string PUNPack::BlowfishDecryptString(const std::vector<uint8_t>& ciphertext, const std::string& key)
{
    // Decrypt then convert bytes back to string
    std::vector<uint8_t> bytes = BlowfishDecrypt(ciphertext, key);
    return std::string(bytes.begin(), bytes.end());
}

//==============================================================================
// Password Hashing - bcrypt (PHP password_hash() / password_verify() compatible)
//==============================================================================
std::string PUNPack::HashPassword(const std::string& password, int cost)
{
    if (!m_bIsInitialized.load() || password.empty())
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] HashPassword: invalid input");
#endif
        return "";
    }

    // Clamp cost to valid bcrypt range
    cost = std::max(PUNPACK_BCRYPT_MIN_COST, std::min(PUNPACK_BCRYPT_MAX_COST, cost));

    // Generate 16 random salt bytes
    std::vector<uint8_t> saltBytes = GenerateSaltBytes(PUNPACK_BCRYPT_SALT_BYTES);

    // Prepare null-terminated password capped at 72 bytes (bcrypt rule)
    std::string pw = password;
    if (pw.size() > 72) pw.resize(72);
    pw.push_back('\0');

    // Run the expensive key schedule
    BlowfishState state;
    EksBlowfishSetup(state,
                     reinterpret_cast<const uint8_t*>(pw.data()), pw.size(),
                     saltBytes.data(), cost);

    // Encrypt the magic ciphertext 64 times
    uint8_t ctext[24];
    std::memcpy(ctext, BF_CIPHERTEXT, 24);

    for (int i = 0; i < 64; ++i)
    {
        for (int b = 0; b < 24; b += 8)
        {
            uint32_t xL = (static_cast<uint32_t>(ctext[b + 0]) << 24) |
                          (static_cast<uint32_t>(ctext[b + 1]) << 16) |
                          (static_cast<uint32_t>(ctext[b + 2]) <<  8) |
                           static_cast<uint32_t>(ctext[b + 3]);
            uint32_t xR = (static_cast<uint32_t>(ctext[b + 4]) << 24) |
                          (static_cast<uint32_t>(ctext[b + 5]) << 16) |
                          (static_cast<uint32_t>(ctext[b + 6]) <<  8) |
                           static_cast<uint32_t>(ctext[b + 7]);
            BlowfishEncipher(state, xL, xR);
            ctext[b + 0] = static_cast<uint8_t>(xL >> 24); ctext[b + 1] = static_cast<uint8_t>(xL >> 16);
            ctext[b + 2] = static_cast<uint8_t>(xL >>  8); ctext[b + 3] = static_cast<uint8_t>(xL);
            ctext[b + 4] = static_cast<uint8_t>(xR >> 24); ctext[b + 5] = static_cast<uint8_t>(xR >> 16);
            ctext[b + 6] = static_cast<uint8_t>(xR >>  8); ctext[b + 7] = static_cast<uint8_t>(xR);
        }
    }

    // Encode: 16-byte salt -> 22 bcrypt-base64 chars, 23-byte hash -> 31 chars
    std::string saltEnc = BcryptBase64Encode(saltBytes.data(), PUNPACK_BCRYPT_SALT_BYTES);
    std::string hashEnc = BcryptBase64Encode(ctext, 23);
    if (saltEnc.size() > 22) saltEnc.resize(22);
    if (hashEnc.size() > 31) hashEnc.resize(31);

    // Compose the $2b$<cost>$<22-char salt><31-char hash> string
    char costBuf[8];
    std::snprintf(costBuf, sizeof(costBuf), "%02d", cost);

    std::string result = "$2b$";
    result += costBuf;
    result += '$';
    result += saltEnc;
    result += hashEnc;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[PUNPack] HashPassword: cost=%d, output length=%zu", cost, result.size());
#endif
    return result;
}

bool PUNPack::VerifyPassword(const std::string& password, const std::string& hashString)
{
    // Minimum valid hash length is 60 chars
    if (!m_bIsInitialized.load() || password.empty() || hashString.size() < 60)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] VerifyPassword: invalid input");
#endif
        return false;
    }

    // Accept $2a$, $2b$, $2y$ prefix variants for full PHP compatibility
    if (hashString[0] != '$' || hashString[1] != '2' ||
        (hashString[2] != 'a' && hashString[2] != 'b' && hashString[2] != 'y') ||
        hashString[3] != '$')
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] VerifyPassword: unsupported hash format");
#endif
        return false;
    }

    // Parse cost factor (characters 4-5, e.g. "12")
    int cost = std::atoi(hashString.c_str() + 4);
    if (cost < PUNPACK_BCRYPT_MIN_COST || cost > PUNPACK_BCRYPT_MAX_COST)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] VerifyPassword: invalid cost factor");
#endif
        return false;
    }

    // Salt is 22 chars starting at position 7 (after "$2b$12$")
    if (hashString.size() < 7 + 22 + 31)
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] VerifyPassword: hash too short");
#endif
        return false;
    }

    std::string saltPart = hashString.substr(7, 22);

    // Decode 16-byte salt from the bcrypt-base64 representation
    uint8_t saltBytes[16] = {};
    if (!BcryptBase64Decode(saltPart, saltBytes, PUNPACK_BCRYPT_SALT_BYTES))
    {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] VerifyPassword: salt decode failed");
#endif
        return false;
    }

    // Prepare password the same way HashPassword does
    std::string pw = password;
    if (pw.size() > 72) pw.resize(72);
    pw.push_back('\0');

    // Re-compute the bcrypt hash with the extracted salt
    BlowfishState state;
    EksBlowfishSetup(state,
                     reinterpret_cast<const uint8_t*>(pw.data()), pw.size(),
                     saltBytes, cost);

    uint8_t ctext[24];
    std::memcpy(ctext, BF_CIPHERTEXT, 24);

    for (int i = 0; i < 64; ++i)
    {
        for (int b = 0; b < 24; b += 8)
        {
            uint32_t xL = (static_cast<uint32_t>(ctext[b + 0]) << 24) |
                          (static_cast<uint32_t>(ctext[b + 1]) << 16) |
                          (static_cast<uint32_t>(ctext[b + 2]) <<  8) |
                           static_cast<uint32_t>(ctext[b + 3]);
            uint32_t xR = (static_cast<uint32_t>(ctext[b + 4]) << 24) |
                          (static_cast<uint32_t>(ctext[b + 5]) << 16) |
                          (static_cast<uint32_t>(ctext[b + 6]) <<  8) |
                           static_cast<uint32_t>(ctext[b + 7]);
            BlowfishEncipher(state, xL, xR);
            ctext[b + 0] = static_cast<uint8_t>(xL >> 24); ctext[b + 1] = static_cast<uint8_t>(xL >> 16);
            ctext[b + 2] = static_cast<uint8_t>(xL >>  8); ctext[b + 3] = static_cast<uint8_t>(xL);
            ctext[b + 4] = static_cast<uint8_t>(xR >> 24); ctext[b + 5] = static_cast<uint8_t>(xR >> 16);
            ctext[b + 6] = static_cast<uint8_t>(xR >>  8); ctext[b + 7] = static_cast<uint8_t>(xR);
        }
    }

    // Encode computed hash and compare against stored portion
    std::string computedHash = BcryptBase64Encode(ctext, 23);
    if (computedHash.size() > 31) computedHash.resize(31);
    std::string storedHash = hashString.substr(7 + 22, 31);

    // Constant-time comparison to resist timing attacks
    bool sameLen = (computedHash.size() == storedHash.size());
    uint8_t diff = 0;
    size_t cmpLen = std::min(computedHash.size(), storedHash.size());
    for (size_t i = 0; i < cmpLen; ++i)
        diff |= static_cast<uint8_t>(computedHash[i] ^ storedHash[i]);

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[PUNPack] VerifyPassword: match=%s", (sameLen && diff == 0) ? L"true" : L"false");
#endif
    return sameLen && (diff == 0);
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
