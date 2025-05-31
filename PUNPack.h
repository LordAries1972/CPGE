//-------------------------------------------------------------------------------------------------
// PUNPack.h - High-Performance Compression and Decompression Class
// 
// Purpose: Provides fast compression/decompression with data integrity checking and encryption
//          for strings, structures, and memory buffers. Designed for gaming platforms where
//          timing is critical and data integrity is paramount.
//
// Features:
// - String and Wide String compression with UTF-8/UTF-16 support
// - Structure and Class serialization with size validation
// - Memory buffer compression with integrity checking
// - Fast checksum calculation using optimized algorithms
// - Random decipher key generation for basic encryption
// - Thread-safe operations with atomic counters
// - Integration with MathPrecalculation for performance optimization
//-------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Debug.h"
#include "MathPrecalculation.h"

#include <vector>
#include <memory>
#include <atomic>
#include <random>
#include <mutex>

//==============================================================================
// Constants and Configuration
//==============================================================================
const size_t PUNPACK_MAGIC_HEADER = 0x50554E50414B4552ULL;         // "PUNPAKER" in hex
const uint32_t PUNPACK_VERSION = 0x00010000;                       // Version 1.0.0.0
const size_t PUNPACK_MIN_COMPRESS_SIZE = 64;                       // Minimum size to attempt compression
const size_t PUNPACK_MAX_BUFFER_SIZE = 0x7FFFFFFF;                 // Maximum 2GB buffer size
const uint32_t PUNPACK_CHECKSUM_POLYNOMIAL = 0xEDB88320;           // CRC32 polynomial
const size_t PUNPACK_DECIPHER_KEY_SIZE = 32;                       // 256-bit decipher key

//==============================================================================
// Compression Types and Algorithms
//==============================================================================
enum class CompressionType : uint8_t {
    NONE = 0,                                                       // No compression applied
    RLE = 1,                                                        // Run-Length Encoding for simple data
    LZ77 = 2,                                                       // LZ77 algorithm for general purpose
    HUFFMAN = 3,                                                    // Huffman coding for text data
    HYBRID = 4                                                      // Combination of algorithms for optimal compression
};

//==============================================================================
// Pack Result Structure
//==============================================================================
struct PackResult {
    // Header information
    size_t magicHeader;                                             // Magic header for validation
    uint32_t version;                                               // PUNPack version used
    CompressionType compressionType;                                // Compression algorithm used

    // Size information
    size_t originalSize;                                            // Original uncompressed size
    size_t compressedSize;                                          // Compressed data size
    size_t totalPacketSize;                                         // Total packet size including headers

    // Data integrity and security
    uint32_t checksum;                                              // CRC32 checksum of original data
    uint32_t compressedChecksum;                                    // CRC32 checksum of compressed data
    std::vector<uint8_t> decipherKey;                               // Random decipher key for encryption

    // Compressed data
    std::vector<uint8_t> compressedData;                            // The actual compressed data

    // Metadata
    uint64_t timestamp;                                             // Timestamp when packed
    bool isEncrypted;                                               // Whether data is encrypted
    float compressionRatio;                                         // Compression ratio achieved

    // Constructor
    PackResult() :
        magicHeader(PUNPACK_MAGIC_HEADER),
        version(PUNPACK_VERSION),
        compressionType(CompressionType::NONE),
        originalSize(0),
        compressedSize(0),
        totalPacketSize(0),
        checksum(0),
        compressedChecksum(0),
        timestamp(0),
        isEncrypted(false),
        compressionRatio(1.0f)
    {
        // Reserve space for decipher key
        decipherKey.reserve(PUNPACK_DECIPHER_KEY_SIZE);
    }

    // Validation method
    bool IsValid() const {
        return (magicHeader == PUNPACK_MAGIC_HEADER) &&
            (version == PUNPACK_VERSION) &&
            (originalSize > 0) &&
            (compressedSize > 0) &&
            (!compressedData.empty());
    }
};

//==============================================================================
// Decompression Result Structure
//==============================================================================
struct UnpackResult {
    bool success;                                                   // Whether decompression was successful
    std::vector<uint8_t> data;                                      // Decompressed data
    size_t originalSize;                                            // Size of decompressed data
    uint32_t verifiedChecksum;                                      // Verified checksum
    CompressionType usedCompression;                                // Compression type that was used
    float decompressionTime;                                        // Time taken for decompression (milliseconds)
    std::string errorMessage;                                       // Error message if failed

    // Constructor
    UnpackResult() :
        success(false),
        originalSize(0),
        verifiedChecksum(0),
        usedCompression(CompressionType::NONE),
        decompressionTime(0.0f)
    {
    }
};

//==============================================================================
// PUNPack Class Declaration
//==============================================================================
class PUNPack {
public:
    // Constructor and Destructor
    PUNPack();
    ~PUNPack();

    // Initialization and cleanup
    bool Initialize();
    void Cleanup();
    bool IsInitialized() const { return m_bIsInitialized.load(); }

    //==========================================================================
    // String Packing/Unpacking Methods
    //==========================================================================
    // Pack standard string with compression and encryption
    PackResult PackString(const std::string& inputString, CompressionType compressionType = CompressionType::HYBRID, bool encrypt = true);

    // Pack wide string with compression and encryption
    PackResult PackString(const std::wstring& inputString, CompressionType compressionType = CompressionType::HYBRID, bool encrypt = true);

    // Unpack to standard string
    UnpackResult UnpackString(const PackResult& packedData);

    // Unpack to wide string
    UnpackResult UnpackWString(const PackResult& packedData);

    //==========================================================================
    // Structure/Class Packing/Unpacking Methods
    //==========================================================================
    // Pack any structure or class (template method)
    template<typename T>
    PackResult PackStruct(const T& structure, CompressionType compressionType = CompressionType::LZ77, bool encrypt = true);

    // Unpack to structure or class (template method)
    template<typename T>
    UnpackResult UnpackStruct(const PackResult& packedData, T& outputStructure);

    //==========================================================================
    // Memory Buffer Packing/Unpacking Methods
    //==========================================================================
    // Pack memory buffer with size specification
    PackResult PackBuffer(const void* buffer, size_t bufferSize, CompressionType compressionType = CompressionType::LZ77, bool encrypt = true);

    // Pack vector buffer
    PackResult PackBuffer(const std::vector<uint8_t>& buffer, CompressionType compressionType = CompressionType::LZ77, bool encrypt = true);

    // Unpack to memory buffer
    UnpackResult UnpackBuffer(const PackResult& packedData);

    //==========================================================================
    // Checksum Calculation Methods
    //==========================================================================
    // Calculate CRC32 checksum for any buffer
    uint32_t CalculateChecksum(const void* data, size_t size) const;

    // Calculate checksum for vector
    uint32_t CalculateChecksum(const std::vector<uint8_t>& data) const;

    // Calculate checksum for string
    uint32_t CalculateChecksum(const std::string& data) const;

    // Calculate checksum for wide string
    uint32_t CalculateChecksum(const std::wstring& data) const;

    // Verify checksum against expected value
    bool VerifyChecksum(const void* data, size_t size, uint32_t expectedChecksum) const;

    //==========================================================================
    // Encryption/Decryption Methods
    //==========================================================================
    // Generate random decipher key
    std::vector<uint8_t> GenerateDecipherKey(size_t keySize = PUNPACK_DECIPHER_KEY_SIZE);

    // Encrypt data using XOR cipher with key
    void EncryptData(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;

    // Decrypt data using XOR cipher with key
    void DecryptData(std::vector<uint8_t>& data, const std::vector<uint8_t>& key) const;

    //==========================================================================
    // Statistics and Utility Methods
    //==========================================================================
    // Get compression statistics
    struct CompressionStats {
        size_t totalBytesProcessed;
        size_t totalBytesCompressed;
        size_t totalOperations;
        float averageCompressionRatio;
        float averageCompressionTime;
        float averageDecompressionTime;
    };

    CompressionStats GetStatistics() const;
    void ResetStatistics();

    // Get optimal compression type for data
    CompressionType GetOptimalCompressionType(const void* data, size_t size) const;

private:
    //==========================================================================
    // Internal Compression Methods
    //==========================================================================
    // Run-Length Encoding compression
    std::vector<uint8_t> CompressRLE(const std::vector<uint8_t>& input) const;
    std::vector<uint8_t> DecompressRLE(const std::vector<uint8_t>& input, size_t originalSize) const;

    // LZ77 compression algorithm
    std::vector<uint8_t> CompressLZ77(const std::vector<uint8_t>& input) const;
    std::vector<uint8_t> DecompressLZ77(const std::vector<uint8_t>& input, size_t originalSize) const;

    // Huffman coding compression
    std::vector<uint8_t> CompressHuffman(const std::vector<uint8_t>& input) const;
    std::vector<uint8_t> DecompressHuffman(const std::vector<uint8_t>& input, size_t originalSize) const;

    // Hybrid compression (combines multiple algorithms)
    std::vector<uint8_t> CompressHybrid(const std::vector<uint8_t>& input) const;
    std::vector<uint8_t> DecompressHybrid(const std::vector<uint8_t>& input, size_t originalSize) const;

    //==========================================================================
    // Internal Utility Methods
    //==========================================================================
    // Convert data to byte vector
    std::vector<uint8_t> DataToByteVector(const void* data, size_t size) const;

    // Validate pack result integrity
    bool ValidatePackResult(const PackResult& result) const;

    // Update compression statistics
    void UpdateStatistics(size_t originalSize, size_t compressedSize, float compressionTime, float decompressionTime);

    // Fast CRC32 calculation using lookup table
    void InitializeCRC32Table();
    uint32_t CalculateCRC32Fast(const void* data, size_t size) const;

private:
    // Initialization state
    std::atomic<bool> m_bIsInitialized;
    std::atomic<bool> m_bHasCleanedUp;

    // Thread safety
    mutable std::mutex m_operationMutex;
    mutable std::mutex m_statisticsMutex;

    // Random number generation for keys
    std::mt19937_64 m_randomGenerator;
    std::uniform_int_distribution<int> m_byteDistribution;

    // CRC32 lookup table for fast checksum calculation
    std::array<uint32_t, 256> m_crc32Table;
    bool m_crc32TableInitialized;

    // Compression statistics
    mutable std::atomic<size_t> m_totalBytesProcessed;
    mutable std::atomic<size_t> m_totalBytesCompressed;
    mutable std::atomic<size_t> m_totalOperations;
    mutable std::atomic<uint64_t> m_totalCompressionTime;     // In microseconds
    mutable std::atomic<uint64_t> m_totalDecompressionTime;   // In microseconds

    // Performance optimization
    MathPrecalculation* m_mathPrecalc;                        // Reference to math precalculation
};

//==============================================================================
// Template Method Implementations
//==============================================================================
template<typename T>
PackResult PUNPack::PackStruct(const T& structure, CompressionType compressionType, bool encrypt) {
    PackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackStruct called for structure of size: %zu", sizeof(T));
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load()) {
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackStruct called before initialization");
#endif
        return result;
    }

    try {
        // Convert structure to byte vector
        std::vector<uint8_t> structureData = DataToByteVector(&structure, sizeof(T));

        // Calculate checksum of original data
        result.checksum = CalculateChecksum(structureData);
        result.originalSize = sizeof(T);
        result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Perform compression based on specified type
        auto startTime = std::chrono::high_resolution_clock::now();

        switch (compressionType) {
        case CompressionType::RLE:
            result.compressedData = CompressRLE(structureData);
            break;
        case CompressionType::LZ77:
            result.compressedData = CompressLZ77(structureData);
            break;
        case CompressionType::HUFFMAN:
            result.compressedData = CompressHuffman(structureData);
            break;
        case CompressionType::HYBRID:
            result.compressedData = CompressHybrid(structureData);
            break;
        default:
            result.compressedData = structureData; // No compression
            break;
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        float compressionTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

        // Set compression type and size
        result.compressionType = compressionType;
        result.compressedSize = result.compressedData.size();
        result.compressionRatio = static_cast<float>(result.originalSize) / static_cast<float>(result.compressedSize);

        // Generate decipher key and encrypt if requested
        if (encrypt) {
            result.decipherKey = GenerateDecipherKey();
            EncryptData(result.compressedData, result.decipherKey);
            result.isEncrypted = true;
        }

        // Calculate checksum of compressed data
        result.compressedChecksum = CalculateChecksum(result.compressedData);
        result.totalPacketSize = sizeof(PackResult) + result.compressedData.size();

        // Update statistics
        UpdateStatistics(result.originalSize, result.compressedSize, compressionTime, 0.0f);

#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] PackStruct completed - Original: %zu, Compressed: %zu, Ratio: %.2f",
            result.originalSize, result.compressedSize, result.compressionRatio);
#endif

    }
    catch (const std::exception& e) {
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] PackStruct exception: " + wErrorMsg);
#endif
    }

    return result;
}

template<typename T>
UnpackResult PUNPack::UnpackStruct(const PackResult& packedData, T& outputStructure) {
    UnpackResult result;

#if defined(_DEBUG_PUNPACK_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackStruct called for target structure of size: %zu", sizeof(T));
#endif

    // Ensure the class is initialized
    if (!m_bIsInitialized.load()) {
        result.errorMessage = "PUNPack not initialized";
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackStruct called before initialization");
#endif
        return result;
    }

    // Validate pack result
    if (!ValidatePackResult(packedData)) {
        result.errorMessage = "Invalid pack result data";
#if defined(_DEBUG_PUNPACK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackStruct received invalid pack result");
#endif
        return result;
    }

    // Check if expected size matches
    if (packedData.originalSize != sizeof(T)) {
        result.errorMessage = "Structure size mismatch";
#if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Size mismatch - Expected: %zu, Got: %zu",
            sizeof(T), packedData.originalSize);
#endif
        return result;
    }

    try {
        auto startTime = std::chrono::high_resolution_clock::now();

        // Copy compressed data for processing
        std::vector<uint8_t> workingData = packedData.compressedData;

        // Decrypt if necessary
        if (packedData.isEncrypted && !packedData.decipherKey.empty()) {
            DecryptData(workingData, packedData.decipherKey);
        }

        // Verify compressed data checksum
        uint32_t verifyChecksum = CalculateChecksum(workingData);
        if (verifyChecksum != packedData.compressedChecksum) {
            result.errorMessage = "Compressed data checksum verification failed";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Checksum mismatch - Expected: 0x%08X, Got: 0x%08X",
                packedData.compressedChecksum, verifyChecksum);
#endif
            return result;
        }

        // Decompress based on compression type
        std::vector<uint8_t> decompressedData;

        switch (packedData.compressionType) {
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
        if (decompressedData.size() != packedData.originalSize) {
            result.errorMessage = "Decompressed data size mismatch";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Decompressed size mismatch - Expected: %zu, Got: %zu",
                packedData.originalSize, decompressedData.size());
#endif
            return result;
        }

        // Verify original data checksum
        uint32_t originalChecksum = CalculateChecksum(decompressedData);
        if (originalChecksum != packedData.checksum) {
            result.errorMessage = "Original data checksum verification failed";
#if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[PUNPack] Original checksum mismatch - Expected: 0x%08X, Got: 0x%08X",
                packedData.checksum, originalChecksum);
#endif
            return result;
        }

        // Copy decompressed data to output structure
        std::memcpy(&outputStructure, decompressedData.data(), sizeof(T));

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
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[PUNPack] UnpackStruct completed successfully - Size: %zu, Time: %.2fms",
            result.originalSize, result.decompressionTime);
#endif

    }
    catch (const std::exception& e) {
        result.errorMessage = std::string("Exception during unpacking: ") + e.what();
#if defined(_DEBUG_PUNPACK_)
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[PUNPack] UnpackStruct exception: " + wErrorMsg);
#endif
    }

    return result;
}

// External reference for global debug instance
extern Debug debug;
