# PUNPack Compression & Encryption Class - Comprehensive Usage Guide

## Overview

PUNPack is a high-performance class providing compression, data integrity verification,
symmetric Blowfish encryption, and bcrypt password hashing — all in a single, thread-safe
API. It is designed for gaming platforms where timing is critical and data security is paramount.

The class supports PHP interoperability: `HashPassword()` produces hashes that PHP's
`password_verify()` can check, and hashes produced by PHP's `password_hash()` can be
verified by `VerifyPassword()`.

## Table of Contents

1. [Basic Setup](#basic-setup)
2. [String Compression](#string-compression)
3. [Wide String Compression](#wide-string-compression)
4. [Structure Packing](#structure-packing)
5. [Memory Buffer Compression](#memory-buffer-compression)
6. [Checksum Calculation](#checksum-calculation)
7. [XOR Encryption / Decryption](#xor-encryption--decryption)
8. [Blowfish Encryption / Decryption](#blowfish-encryption--decryption)
9. [Password Hashing (bcrypt)](#password-hashing-bcrypt)
10. [PHP Interoperability](#php-interoperability)
11. [Performance Analysis](#performance-analysis)
12. [Error Handling](#error-handling)
13. [Integration Examples](#integration-examples)
14. [Best Practices](#best-practices)

---

## Basic Setup

### Include Required Headers

```cpp
#include "Includes.h"
#include "PUNPack.h"
#include "Debug.h"
#include <iostream>
#include <chrono>
#include <random>
#include <cstring>

extern Debug debug;
```

### Example Structure for Testing

```cpp
struct GamePlayerData {
    char     playerName[32];                    // Player name
    uint32_t playerId;                          // Unique player ID
    float    positionX, positionY, positionZ;   // 3D position
    uint16_t health;                            // Current health
    uint16_t maxHealth;                         // Maximum health
    uint8_t  level;                             // Player level
    uint8_t  experience;                        // Experience points
    bool     isAlive;                           // Alive status
    bool     hasWeapon;                         // Weapon equipped
    uint64_t sessionTime;                       // Session time (ms)

    GamePlayerData() : playerId(0), positionX(0), positionY(0), positionZ(0),
                       health(100), maxHealth(100), level(1), experience(0),
                       isAlive(true), hasWeapon(false), sessionTime(0) {
        memset(playerName, 0, sizeof(playerName));
        strcpy_s(playerName, sizeof(playerName), "DefaultPlayer");
    }

    GamePlayerData(const char* name, uint32_t id, float x, float y, float z) :
        playerId(id), positionX(x), positionY(y), positionZ(z),
        health(100), maxHealth(100), level(1), experience(0),
        isAlive(true), hasWeapon(false), sessionTime(0) {
        memset(playerName, 0, sizeof(playerName));
        strcpy_s(playerName, sizeof(playerName), name);
    }
};
```

### Basic Initialization

```cpp
class PUNPackExampleUsage {
private:
    PUNPack m_compressor;
    bool    m_isInitialized;

public:
    PUNPackExampleUsage() : m_isInitialized(false) {}

    bool Initialize() {
        if (!m_compressor.Initialize()) {
            #if defined(_DEBUG_PUNPACK_)
                debug.logLevelMessage(LogLevel::LOG_ERROR,
                    L"[PUNPackExample] Failed to initialize PUNPack");
            #endif
            return false;
        }
        m_isInitialized = true;
        return true;
    }

    void Cleanup() {
        if (m_isInitialized) {
            m_compressor.Cleanup();
            m_isInitialized = false;
        }
    }
};
```

---

## String Compression

```cpp
void ExampleStringCompression() {
    std::vector<std::string> testStrings = {
        "Hello, World! This is a test string for compression.",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",  // Excellent for RLE
        "The quick brown fox jumps over the lazy dog.",
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit."
    };

    std::vector<CompressionType> types = {
        CompressionType::RLE,
        CompressionType::LZ77,
        CompressionType::HUFFMAN,
        CompressionType::HYBRID
    };

    for (const auto& str : testStrings) {
        for (CompressionType type : types) {
            // Compress with encryption enabled
            PackResult packed = m_compressor.PackString(str, type, true);

            if (packed.IsValid()) {
                // Decompress
                UnpackResult unpacked = m_compressor.UnpackString(packed);

                if (unpacked.success) {
                    std::string result(unpacked.data.begin(), unpacked.data.end());
                    bool ok = (result == str);
                    // ok == true confirms round-trip integrity
                }
            }
        }
    }
}
```

---

## Wide String Compression

```cpp
void ExampleWideStringCompression() {
    std::vector<std::wstring> testStrings = {
        L"Hello, World! Unicode test with émojis.",
        L"Русский текст для тестирования сжатия.",
        L"数字测试：１２３４５６７８９０"
    };

    for (const auto& ws : testStrings) {
        PackResult packed = m_compressor.PackString(ws, CompressionType::HYBRID, true);

        if (packed.IsValid()) {
            UnpackResult unpacked = m_compressor.UnpackWString(packed);

            if (unpacked.success) {
                const wchar_t* wptr = reinterpret_cast<const wchar_t*>(unpacked.data.data());
                size_t         wlen = unpacked.data.size() / sizeof(wchar_t);
                std::wstring   result(wptr, wlen);
                // result == ws  confirms round-trip
            }
        }
    }
}
```

---

## Structure Packing

```cpp
void ExampleStructurePacking() {
    GamePlayerData player("Alice", 1001, 100.5f, 200.3f, 50.0f);
    player.health  = 75;
    player.level   = 5;
    player.hasWeapon = true;

    // Pack with LZ77 compression and XOR encryption
    PackResult packed = m_compressor.PackStruct(player, CompressionType::LZ77, true);

    if (packed.IsValid()) {
        GamePlayerData restored;
        UnpackResult unpacked = m_compressor.UnpackStruct(packed, restored);

        if (unpacked.success) {
            bool ok = (strcmp(player.playerName, restored.playerName) == 0) &&
                      (player.playerId == restored.playerId) &&
                      (player.health   == restored.health);
            // ok == true confirms perfect round-trip
        }
    }
}
```

---

## Memory Buffer Compression

```cpp
void ExampleBufferCompression() {
    // Highly repetitive buffer - ideal for RLE
    std::vector<uint8_t> repetitiveData(1024, 0xAA);

    // Auto-detect best algorithm
    CompressionType best = m_compressor.GetOptimalCompressionType(
        repetitiveData.data(), repetitiveData.size());

    PackResult packed = m_compressor.PackBuffer(repetitiveData, best, false);

    if (packed.IsValid()) {
        UnpackResult unpacked = m_compressor.UnpackBuffer(packed);

        if (unpacked.success) {
            bool ok = (memcmp(unpacked.data.data(),
                              repetitiveData.data(),
                              repetitiveData.size()) == 0);
        }
    }

    // Raw pointer overload
    const char* rawData = "Raw memory block for compression.";
    PackResult rawPacked = m_compressor.PackBuffer(
        rawData, strlen(rawData), CompressionType::HUFFMAN, true);
}
```

---

## Checksum Calculation

```cpp
void ExampleChecksumCalculation() {
    std::string testString = "Checksum test data.";

    // Calculate CRC32 checksum
    uint32_t crc = m_compressor.CalculateChecksum(testString);

    // Verify unchanged data
    bool valid = m_compressor.VerifyChecksum(
        testString.c_str(), testString.length(), crc);
    // valid == true

    // Verify tampered data (should fail)
    std::string tampered = testString + "X";
    bool shouldFail = m_compressor.VerifyChecksum(
        tampered.c_str(), tampered.length(), crc);
    // shouldFail == false

    // Wide string checksum
    std::wstring ws = L"Wide string checksum test.";
    uint32_t wideCrc = m_compressor.CalculateChecksum(ws);

    // Vector checksum
    std::vector<uint8_t> buf = {0x01, 0x02, 0xAA, 0xFF};
    uint32_t bufCrc = m_compressor.CalculateChecksum(buf);
}
```

---

## XOR Encryption / Decryption

The original lightweight encryption used by `PackString` / `PackBuffer` when `encrypt = true`.
This XOR cipher operates with a randomly generated key stored in `PackResult::decipherKey`.
The key must be kept with the packet to enable decryption.

```cpp
void ExampleXorEncryption() {
    // Generate a 32-byte random key
    std::vector<uint8_t> key = m_compressor.GenerateDecipherKey(32);

    std::vector<uint8_t> data = { 'H', 'e', 'l', 'l', 'o', '!' };

    // Encrypt in-place
    m_compressor.EncryptData(data, key);

    // Decrypt in-place (XOR is its own inverse)
    m_compressor.DecryptData(data, key);

    // data is now restored to { 'H', 'e', 'l', 'l', 'o', '!' }

    // Pack with automatic XOR encryption (key embedded in PackResult)
    PackResult packed = m_compressor.PackString("Secret payload", CompressionType::LZ77, true);
    // packed.isEncrypted == true
    // packed.decipherKey holds the random key
    // UnpackString() decrypts automatically using the embedded key
}
```

---

## Blowfish Encryption / Decryption

Blowfish is a proven symmetric block cipher (64-bit blocks, up to 448-bit keys). It is
fully **reversible**: the same key that encrypts can decrypt. This is distinct from bcrypt
password hashing which is one-way.

PUNPack uses **CBC mode** with a random 8-byte IV prepended to the ciphertext, and
**PKCS7 padding** to align data to 8-byte blocks.

### Key Facts

| Property        | Value                                    |
|-----------------|------------------------------------------|
| Block size      | 8 bytes (64 bits)                        |
| Key length      | 1–56 bytes (up to 448 bits)              |
| Mode            | CBC with random IV                       |
| Padding         | PKCS7                                    |
| IV storage      | First 8 bytes of ciphertext output       |
| Overhead        | 8 bytes (IV) + up to 7 bytes (padding)   |

### Encrypting a Byte Buffer

```cpp
void ExampleBlowfishBuffer() {
    std::string key = "MySecretKey123";   // Up to 56 bytes

    std::vector<uint8_t> plaintext = { 0x01, 0x02, 0x03, 0x04, 0x05 };

    // Encrypt - returns IV (8 bytes) + ciphertext
    std::vector<uint8_t> ciphertext = m_compressor.BlowfishEncrypt(plaintext, key);

    // Decrypt - strips IV automatically and removes PKCS7 padding
    std::vector<uint8_t> recovered  = m_compressor.BlowfishDecrypt(ciphertext, key);

    bool ok = (recovered == plaintext);   // true
}
```

### Encrypting a String (Convenience Overloads)

```cpp
void ExampleBlowfishString() {
    std::string key       = "GameServerSharedSecret";
    std::string plaintext = "Player inventory data: 100 gold, sword+5, shield";

    // Encrypt to binary
    std::vector<uint8_t> ciphertext = m_compressor.BlowfishEncryptString(plaintext, key);

    // Decrypt back to string
    std::string recovered = m_compressor.BlowfishDecryptString(ciphertext, key);

    bool ok = (recovered == plaintext);   // true
}
```

### Combining Blowfish with Compression

For maximum efficiency, compress first then encrypt:

```cpp
void ExampleCompressThenEncrypt() {
    std::string key  = "CompressAndEncryptKey";
    std::string data = "Large repetitive game data payload ... (repeated many times)";

    // Step 1: Compress with PUNPack (no built-in XOR encryption)
    PackResult compressed = m_compressor.PackString(data, CompressionType::HYBRID, false);

    if (compressed.IsValid()) {
        // Step 2: Blowfish-encrypt the compressed bytes
        std::vector<uint8_t> secure =
            m_compressor.BlowfishEncrypt(compressed.compressedData, key);

        // --- Transfer secure bytes across network / write to disk ---

        // Step 3: Blowfish-decrypt
        std::vector<uint8_t> decryptedBytes =
            m_compressor.BlowfishDecrypt(secure, key);

        // Step 4: Restore compressed bytes into PackResult and decompress
        compressed.compressedData = decryptedBytes;
        UnpackResult result = m_compressor.UnpackString(compressed);

        if (result.success) {
            std::string restored(result.data.begin(), result.data.end());
            // restored == data
        }
    }
}
```

### Blowfish for Network Packet Security

```cpp
void ExampleNetworkPacket() {
    // Shared secret established via key exchange (e.g., Diffie-Hellman)
    std::string sessionKey = "SharedSessionKey_ABCDEF1234";

    // Outgoing: serialise + compress + Blowfish
    GamePlayerData player("Bob", 42, 10.0f, 20.0f, 0.0f);

    PackResult packed = m_compressor.PackStruct(player, CompressionType::LZ77, false);
    std::vector<uint8_t> packet =
        m_compressor.BlowfishEncrypt(packed.compressedData, sessionKey);

    // Incoming: Blowfish-decrypt + decompress
    std::vector<uint8_t> decrypted = m_compressor.BlowfishDecrypt(packet, sessionKey);
    packed.compressedData = decrypted;

    GamePlayerData receivedPlayer;
    UnpackResult result = m_compressor.UnpackStruct(packed, receivedPlayer);
    // receivedPlayer is now identical to player
}
```

---

## Password Hashing (bcrypt)

`HashPassword()` produces a **one-way** bcrypt hash that is **not reversible**. The only
way to check a password against the hash is with `VerifyPassword()`. This mirrors exactly
how PHP's `password_hash()` and `password_verify()` work.

### Key Facts

| Property         | Value                                                  |
|------------------|--------------------------------------------------------|
| Algorithm        | bcrypt (Blowfish-based, EksBlowfishSetup)              |
| Output format    | `$2b$<cost>$<22-char salt><31-char hash>` (60 chars)  |
| Salt             | 16 random bytes, auto-generated per call               |
| Cost default     | 12 (matches PHP default, ~250 ms on modern hardware)   |
| Cost range       | 4–31 (each +1 doubles the work)                        |
| Password limit   | 72 bytes (bcrypt standard limit)                       |
| PHP compatible   | Yes — $2a$, $2b$, $2y$ variants all accepted           |

### Hashing a Password

```cpp
void ExampleHashPassword() {
    // Default cost 12 - good balance of security and speed
    std::string hash = m_compressor.HashPassword("MySecretPassword123!");

    // hash looks like:
    // $2b$12$R9h/cIPz0gi.URNNX3kh2O...  (always 60 chars)

    // Higher cost for storing admin / privileged passwords
    std::string adminHash = m_compressor.HashPassword("AdminPassword!", 14);

    // Lower cost for high-frequency verification scenarios (testing/dev only)
    std::string fastHash  = m_compressor.HashPassword("TestPassword",   4);
}
```

### Verifying a Password

```cpp
void ExampleVerifyPassword() {
    std::string storedHash = m_compressor.HashPassword("UserPassword99");

    // Correct password - returns true
    bool correctMatch = m_compressor.VerifyPassword("UserPassword99", storedHash);

    // Wrong password - returns false
    bool wrongMatch = m_compressor.VerifyPassword("WrongPassword", storedHash);

    // Case-sensitive - returns false
    bool caseMatch = m_compressor.VerifyPassword("userpassword99", storedHash);
}
```

### Cost Factor Selection Guide

```cpp
void ExampleCostSelection() {
    // Cost 10: ~65 ms  — minimum recommended for production
    // Cost 12: ~250 ms — PHP default, good general choice
    // Cost 13: ~500 ms — higher security, still tolerable for login
    // Cost 14: ~1 sec  — admin accounts, one-time operations

    // Benchmark to find the right cost for your hardware:
    for (int cost = 10; cost <= 14; ++cost) {
        auto t0 = std::chrono::high_resolution_clock::now();
        m_compressor.HashPassword("BenchmarkPassword", cost);
        auto t1 = std::chrono::high_resolution_clock::now();
        float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

        #if defined(_DEBUG_PUNPACK_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[PUNPackExample] bcrypt cost %d = %.0f ms", cost, ms);
        #endif
    }
}
```

---

## PHP Interoperability

Hashes produced by either side can be verified by the other.

### C++ generates hash → PHP verifies

```cpp
// C++ side - generate and store hash
std::string hash = m_compressor.HashPassword("UserPassword");
// Store hash in database: "$2b$12$..."
```

```php
<?php
// PHP side - verify the hash stored by C++
$storedHash = '$2b$12$...'; // read from database
$isValid = password_verify('UserPassword', $storedHash);
// $isValid === true
?>
```

### PHP generates hash → C++ verifies

```php
<?php
// PHP side - hash at registration time
$hash = password_hash('UserPassword', PASSWORD_BCRYPT, ['cost' => 12]);
// Store $hash in database
?>
```

```cpp
// C++ side - verify at login time
std::string storedHash = /* read from database */ "$2y$12$...";
bool valid = m_compressor.VerifyPassword("UserPassword", storedHash);
// valid == true
// Note: $2y$ prefix is accepted as well as $2a$ and $2b$
```

### Complete PHP-compatible User Authentication Flow

```cpp
class UserAuthSystem {
private:
    PUNPack m_crypto;

public:
    bool Initialize() { return m_crypto.Initialize(); }
    void Cleanup()    { m_crypto.Cleanup(); }

    // Called at registration - returns hash to store in DB
    std::string RegisterUser(const std::string& plainPassword) {
        // Cost 12 matches PHP default; raise to 13-14 for higher security
        return m_crypto.HashPassword(plainPassword, 12);
    }

    // Called at login - compares plain password against stored hash
    bool LoginUser(const std::string& plainPassword, const std::string& storedHash) {
        return m_crypto.VerifyPassword(plainPassword, storedHash);
    }

    // Upgrade legacy MD5/SHA hash to bcrypt when user next logs in
    std::string UpgradeLegacyHash(const std::string& plainPassword) {
        return m_crypto.HashPassword(plainPassword, 12);
    }
};

void ExampleAuthFlow() {
    UserAuthSystem auth;
    auth.Initialize();

    // Registration
    std::string hash = auth.RegisterUser("SecureP@ssw0rd!");
    // Store hash in database...

    // Login attempt
    bool ok = auth.LoginUser("SecureP@ssw0rd!", hash);    // true
    bool bad = auth.LoginUser("WrongPassword",  hash);    // false

    auth.Cleanup();
}
```

---

## Performance Analysis

```cpp
void ExamplePerformanceAnalysis() {
    m_compressor.ResetStatistics();

    std::string testData = "Performance test payload - ";
    for (int i = 0; i < 12; ++i) testData += testData; // ~100 KB

    // Test all compression types
    for (auto type : { CompressionType::RLE, CompressionType::LZ77,
                       CompressionType::HUFFMAN, CompressionType::HYBRID }) {
        for (int i = 0; i < 5; ++i) {
            PackResult p = m_compressor.PackString(testData, type, false);
            if (p.IsValid())
                m_compressor.UnpackString(p);
        }
    }

    PUNPack::CompressionStats stats = m_compressor.GetStatistics();

    #if defined(_DEBUG_PUNPACK_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[PUNPackExample] Operations: %zu, Avg ratio: %.2f, Avg compress: %.2f ms",
            stats.totalOperations,
            stats.averageCompressionRatio,
            stats.averageCompressionTime);
    #endif
}
```

---

## Error Handling

```cpp
void ExampleErrorHandling() {
    // Empty string - PackString returns an invalid PackResult
    PackResult empty = m_compressor.PackString("", CompressionType::HYBRID, false);
    // empty.IsValid() == false

    // Default-constructed PackResult - UnpackString fails gracefully
    PackResult invalid;
    UnpackResult r = m_compressor.UnpackString(invalid);
    // r.success == false, r.errorMessage contains the reason

    // Tampered ciphertext - checksum mismatch detected
    std::string text = "Important game data.";
    PackResult p = m_compressor.PackString(text, CompressionType::LZ77, true);
    if (!p.compressedData.empty())
        p.compressedData[p.compressedData.size() / 2] ^= 0xFF;   // corrupt one byte
    UnpackResult corrupted = m_compressor.UnpackString(p);
    // corrupted.success == false

    // Blowfish with empty key - returns empty vector
    std::vector<uint8_t> data = { 1, 2, 3 };
    std::vector<uint8_t> badKey = m_compressor.BlowfishEncrypt(data, "");
    // badKey.empty() == true

    // VerifyPassword with wrong hash format - returns false
    bool badFormat = m_compressor.VerifyPassword("password", "not_a_bcrypt_hash");
    // badFormat == false
}
```

---

## Integration Examples

### Game Data Manager

```cpp
class GameDataManager {
private:
    PUNPack m_compressor;
    std::vector<PackResult> m_cache;
    std::string m_blowfishKey;  // Shared session key from key exchange

public:
    bool Initialize(const std::string& sessionKey) {
        m_blowfishKey = sessionKey;
        return m_compressor.Initialize();
    }

    void Cleanup() { m_compressor.Cleanup(); }

    // Save player data: compress + Blowfish-encrypt
    bool SavePlayerData(const GamePlayerData& player) {
        PackResult packed =
            m_compressor.PackStruct(player, CompressionType::LZ77, false);

        if (!packed.IsValid()) return false;

        // Blowfish-encrypt the compressed bytes
        packed.compressedData =
            m_compressor.BlowfishEncrypt(packed.compressedData, m_blowfishKey);
        packed.compressedSize = packed.compressedData.size();

        m_cache.push_back(packed);
        return true;
    }

    // Load player data: Blowfish-decrypt + decompress
    bool LoadPlayerData(size_t index, GamePlayerData& out) {
        if (index >= m_cache.size()) return false;

        PackResult packed = m_cache[index];

        // Blowfish-decrypt first
        packed.compressedData =
            m_compressor.BlowfishDecrypt(packed.compressedData, m_blowfishKey);
        packed.compressedSize = packed.compressedData.size();

        UnpackResult result = m_compressor.UnpackStruct(packed, out);
        return result.success;
    }

    // Verify all cached entries' checksums
    bool VerifyAllIntegrity() {
        for (size_t i = 0; i < m_cache.size(); ++i) {
            PackResult tmp = m_cache[i];
            tmp.compressedData =
                m_compressor.BlowfishDecrypt(tmp.compressedData, m_blowfishKey);
            uint32_t crc = m_compressor.CalculateChecksum(tmp.compressedData);
            if (crc != tmp.compressedChecksum) return false;
        }
        return true;
    }
};
```

### Complete Application Runner

```cpp
void RunPUNPackExamples() {
    PUNPack compressor;
    if (!compressor.Initialize()) return;

    //--- Compression round-trip -------------------------------------------
    std::string text = "This is the test payload for PUNPack.";
    PackResult  p    = compressor.PackString(text, CompressionType::HYBRID, false);
    UnpackResult u   = compressor.UnpackString(p);
    std::string back(u.data.begin(), u.data.end());
    // back == text

    //--- Blowfish symmetric encryption ------------------------------------
    std::string key        = "My56ByteMaxBlowfishKey_ABCDEFGH";
    std::vector<uint8_t> enc = compressor.BlowfishEncryptString(text, key);
    std::string dec        = compressor.BlowfishDecryptString(enc, key);
    // dec == text

    //--- bcrypt password hashing -----------------------------------------
    std::string hash = compressor.HashPassword("MyPassword!", 12);
    bool valid       = compressor.VerifyPassword("MyPassword!", hash);  // true
    bool invalid_pw  = compressor.VerifyPassword("WrongPass",  hash);  // false

    compressor.Cleanup();
}
```

---

## Best Practices

### 1. Choosing Compression Types

| Type    | Best For                                              |
|---------|-------------------------------------------------------|
| RLE     | Data with long runs of identical bytes (bitmaps, etc.)|
| LZ77    | General-purpose: save files, network packets          |
| HUFFMAN | Text-heavy data with predictable character frequency  |
| HYBRID  | Unknown data — auto-selects best of RLE / LZ77        |

```cpp
// Let PUNPack choose for you
CompressionType best = compressor.GetOptimalCompressionType(data, size);
```

### 2. Encryption Selection Guide

| Scenario                               | Recommended Method     |
|----------------------------------------|------------------------|
| Compressing game data in transit       | Blowfish CBC + LZ77    |
| Storing user passwords                 | bcrypt (HashPassword)  |
| Quick in-memory obfuscation            | XOR (built-in encrypt) |
| High-security data at rest             | Blowfish CBC           |
| PHP login system integration           | bcrypt (VerifyPassword)|

### 3. Blowfish Key Guidelines

```cpp
// Good: long, random-looking keys
std::string goodKey = "Kj9#mP2!xQ7&vL4@nR1$";

// Keys are capped at 56 bytes internally; longer keys are silently truncated
// Good keys use the full character set and have high entropy

// For session keys, derive from a key exchange (e.g., Diffie-Hellman)
// Never hard-code production keys in source code
```

### 4. bcrypt Cost Guidelines

```cpp
// Minimum for production: cost 10 (~65 ms on modern hardware)
// PHP default and good general choice: cost 12 (~250 ms)
// High-value accounts (admin): cost 13-14 (~500 ms - 1 s)
// Development / unit tests: cost 4 (fastest, insecure in production)

// Pick cost so that hashing takes ~100-300 ms on your server hardware
```

### 5. Error Handling Pattern

```cpp
// Always check PackResult validity before unpacking
PackResult packed = compressor.PackString(data, type, encrypt);
if (!packed.IsValid()) {
    // Log and handle failure
    return false;
}

// Always check UnpackResult success
UnpackResult unpacked = compressor.UnpackString(packed);
if (!unpacked.success) {
    // unpacked.errorMessage contains the reason
    return false;
}

// Blowfish: empty output means failure
std::vector<uint8_t> enc = compressor.BlowfishEncrypt(data, key);
if (enc.empty()) {
    return false; // bad key or uninitialised
}
```

### 6. Memory / Security

```cpp
// Sensitive plaintext: clear memory after use
std::string password = "UserPassword";
std::string hash = compressor.HashPassword(password);

// Overwrite the plaintext copy in memory
std::fill(password.begin(), password.end(), '\0');
password.clear();
// hash is now safe to store; password is wiped

// For Blowfish keys: keep them in memory only as long as needed
```

### 7. Thread Safety

PUNPack uses internal mutexes for key generation and statistics. Each thread should
use its own `PUNPack` instance for compression and Blowfish operations to avoid
contention on the operation mutex.

```cpp
// Thread-safe: each thread owns its instance
thread_local PUNPack threadCompressor;

void WorkerThread() {
    threadCompressor.Initialize();
    // ... use threadCompressor ...
    threadCompressor.Cleanup();
}
```

### 8. Debug Configuration

```cpp
// Enable in Debug.h (or your project preprocessor defines):
#define _DEBUG_PUNPACK_
// All PUNPack methods will emit diagnostic logLevelMessage / logDebugMessage output
```

---

## Summary

PUNPack provides a complete data security toolkit:

| Feature                  | Method(s)                                      |
|--------------------------|------------------------------------------------|
| Compression              | `PackString`, `PackBuffer`, `PackStruct`        |
| Decompression            | `UnpackString`, `UnpackBuffer`, `UnpackStruct`  |
| Checksum (CRC32)         | `CalculateChecksum`, `VerifyChecksum`           |
| XOR encryption           | `EncryptData` / `DecryptData` (via Pack flags)  |
| Blowfish symmetric       | `BlowfishEncrypt` / `BlowfishDecrypt`           |
| Blowfish string helpers  | `BlowfishEncryptString` / `BlowfishDecryptString` |
| bcrypt password hash     | `HashPassword`                                 |
| bcrypt verify (PHP-compat)| `VerifyPassword`                              |
| Key generation           | `GenerateDecipherKey`                          |
| Performance metrics      | `GetStatistics` / `ResetStatistics`            |
