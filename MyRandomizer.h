// -------------------------------------------------------------------------------------------------------------
// MyRandomizer.h - High-Performance Cross-Platform Random Number Generation Class
// 
// Purpose: Provides comprehensive random number generation with unique number tracking and probability-based
//          target number attempts. Designed for gaming platforms where performance and reliability are critical.
//          Supports multiple platforms (Windows, Linux, MacOS, Android, iOS) with optimal performance.
//
// Features:
// - Cross-platform random number generation with high-quality seeding
// - Integer and floating-point random number generation
// - Unique number selection with automatic list management
// - Probability-based target number attempts with difficulty scaling
// - Multiple attempt target number functionality
// - Bulk unique number generation
// - Advanced statistical distributions (Normal, Exponential, Triangular)
// - Vector and color generation for gaming applications
// - Weighted selection and dice rolling mechanics
// - Gaming-specific random generation methods
// - Production-ready code with comprehensive error handling
// - Optimal performance for gaming applications
// -------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Debug.h"
#include "Vectors.h"
#include "Color.h"

#include <random>
#include <vector>
#include <unordered_set>
#include <map>
#include <chrono>
#include <algorithm>
#include <memory>
#include <string>

// Forward declarations
extern Debug debug;

//==============================================================================
// Constants and Configuration
//==============================================================================
const float MYRANDOMIZER_MIN_PERCENTAGE = 0.001f;                      // Minimum percentage value for GetRandPercentage()
const float MYRANDOMIZER_MAX_PERCENTAGE = 1.0f;                        // Maximum percentage value for GetRandPercentage()
const float MYRANDOMIZER_MIN_DIFFICULTY = 0.001f;                      // Minimum difficulty value for target number attempts
const float MYRANDOMIZER_MAX_DIFFICULTY = 0.99f;                       // Maximum difficulty value for target number attempts
const int MYRANDOMIZER_MIN_STARTRANGE = 1;                             // Minimum valid start range value
const int MYRANDOMIZER_MAX_ATTEMPTS = 1000000;                         // Maximum attempts for target number functionality

// Advanced random generation constants
const int MYRANDOMIZER_MAX_STRING_LENGTH = 256;                         // Maximum string length for random generation
const int MYRANDOMIZER_MAX_DICE_COUNT = 20;                             // Maximum number of dice for rolling
const int MYRANDOMIZER_MAX_DICE_SIDES = 100;                            // Maximum sides per die
const float MYRANDOMIZER_PI = 3.14159265359f;                           // Pi constant for calculations
const float MYRANDOMIZER_TWO_PI = 6.28318530718f;                       // 2*Pi constant for full rotation
const float MYRANDOMIZER_DEGREES_PER_RADIAN = 57.2957795131f;           // Conversion factor degrees per radian
const float MYRANDOMIZER_MIN_STANDARD_DEV = 0.001f;                     // Minimum standard deviation for normal distribution
const float MYRANDOMIZER_MIN_LAMBDA = 0.001f;                           // Minimum lambda for exponential distribution

const std::string myDEFAULT_CHARSET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

//==============================================================================
// Range Key Structure for Unique Number Tracking
//==============================================================================
struct RangeKey {
    int startRange;                                                     // Start of the range
    int endRange;                                                       // End of the range
    bool isFloatRange;                                                  // Whether this is a float range

    // Constructor for integer ranges
    RangeKey(int start, int end) : startRange(start), endRange(end), isFloatRange(false) {}

    // Constructor for float ranges (converted to scaled integers for tracking)
    RangeKey(float start, float end) : startRange(static_cast<int>(start * 1000)),
        endRange(static_cast<int>(end * 1000)),
        isFloatRange(true) {
    }

    // Equality operator for map key comparison
    bool operator==(const RangeKey& other) const {
        return startRange == other.startRange && endRange == other.endRange && isFloatRange == other.isFloatRange;
    }

    // Less than operator for map ordering
    bool operator<(const RangeKey& other) const {
        if (startRange != other.startRange) return startRange < other.startRange;
        if (endRange != other.endRange) return endRange < other.endRange;
        return isFloatRange < other.isFloatRange;
    }
};

//==============================================================================
// Unique Number Tracking Structure
//==============================================================================
struct UniqueNumberTracker {
    std::unordered_set<int> usedNumbers;                               // Set of used numbers for fast lookup
    std::vector<int> availableNumbers;                                 // Vector of available numbers for random selection
    int totalNumbers;                                                  // Total numbers in the range
    bool needsRefresh;                                                 // Flag indicating if the tracker needs to be refreshed

    // Constructor with default initialization
    UniqueNumberTracker() : totalNumbers(0), needsRefresh(true) {}

    // Constructor with range initialization
    UniqueNumberTracker(int start, int end) : totalNumbers(end - start + 1), needsRefresh(true) {
        RefreshAvailableNumbers(start, end);
    }

    // Refresh the available numbers list
    void RefreshAvailableNumbers(int start, int end) {
        usedNumbers.clear();                                           // Clear used numbers set
        availableNumbers.clear();                                      // Clear available numbers vector
        totalNumbers = end - start + 1;                                // Calculate total numbers in range
        availableNumbers.reserve(totalNumbers);                        // Reserve space for efficiency

        // Populate available numbers vector with all numbers in range
        for (int i = start; i <= end; ++i) {
            availableNumbers.push_back(i);
        }
        needsRefresh = false;                                           // Mark as refreshed
    }

    // Mark a number as used and remove from available list
    void MarkNumberAsUsed(int number, int start) {
        usedNumbers.insert(number);                                     // Add to used numbers set

        // Remove from available numbers vector
        auto it = std::find(availableNumbers.begin(), availableNumbers.end(), number);
        if (it != availableNumbers.end()) {
            availableNumbers.erase(it);                                 // Remove the number from available list
        }

        // Check if all numbers have been used
        if (availableNumbers.empty()) {
            needsRefresh = true;                                        // Mark for refresh when all numbers used
        }
    }
};

//==============================================================================
// MyRandomizer Class Declaration
//==============================================================================
class MyRandomizer {
public:
    // Constructor and destructor
    MyRandomizer();                                                     // Initialize randomizer with high-quality seeding
    ~MyRandomizer();                                                    // Clean up randomizer resources

    // Initialization and cleanup
    bool Initialize();                                                  // Initialize randomizer subsystem
    void Cleanup();                                                     // Clean up all randomizer resources
    bool IsInitialized() const { return m_isInitialized; }              // Check initialization status

    //==========================================================================
    // Integer Random Number Generation Methods
    //==========================================================================

    // Generate random number within specified integer range (inclusive)
    int GetRandNum(int startRange, int endRange);

    // Select unique random number from specified integer range (inclusive)
    int SelUniqueRandNum(int startRange, int endRange);

    // Attempt to get target number with specified difficulty (integer version)
    int TryTargetNumber(int startRange, int endRange, int targetNumber, float difficulty);

    // Multiple attempts to get target number (integer version)
    int TryAttemptTargetNum(int startRange, int endRange, int numberOfAttempts, int targetNumber, float difficulty, int& attempted);

    // Get list of unique random numbers (integer version)
    std::vector<int> GetListOfUniqueRandNums(int startRange, int endRange, int numOfNumbers);

    //==========================================================================
    // Float Random Number Generation Methods
    //==========================================================================

    // Generate random number within specified float range (inclusive)
    float GetRandNum(float startRange, float endRange);

    // Select unique random number from specified float range (inclusive)
    float SelUniqueRandNum(float startRange, float endRange);

    // Attempt to get target number with specified difficulty (float version)
    float TryTargetNumber(float startRange, float endRange, float targetNumber, float difficulty);

    // Multiple attempts to get target number (float version)
    float TryAttemptTargetNum(float startRange, float endRange, int numberOfAttempts, float targetNumber, float difficulty, int& attempted);

    // Get list of unique random numbers (float version)
    std::vector<float> GetListOfUniqueRandNums(float startRange, float endRange, int numOfNumbers);

    //==========================================================================
    // Specialized Random Number Generation Methods
    //==========================================================================

    // Generate random percentage from 0.001 to 1.0 inclusive
    float GetRandPercentage();

    //==========================================================================
    // Advanced Random Number Generation Methods
    //==========================================================================

    // Generate random boolean value with optional bias
    bool GetRandBool(float trueProbability = 0.5f);

    // Generate random number using normal (Gaussian) distribution
    float GetRandNormal(float mean, float standardDeviation);

    // Generate random number using exponential distribution
    float GetRandExponential(float lambda);

    // Generate random element from vector/array
    template<typename T>
    T GetRandElement(const std::vector<T>& elements);

    // Generate random weighted selection from elements with weights
    template<typename T>
    T GetRandWeightedElement(const std::vector<T>& elements, const std::vector<float>& weights);

    // Generate random dice roll (standard gaming dice)
    int RollDice(int numberOfDice, int sidesPerDie, int modifier = 0);

    // Generate random color components
    MyColor GetRandColor(bool includeAlpha = true);

    // Generate random 2D vector within specified bounds
    Vector2 GetRandVector2(const Vector2& minBounds, const Vector2& maxBounds);

    // Generate random 2D vector within circle radius
    Vector2 GetRandVector2InCircle(float radius, const Vector2& center = Vector2(0, 0));

    // Generate random 3D vector within specified bounds (if Vector3 available)
#if defined(__USE_OPENGL__)
    Vector3 GetRandVector3(const Vector3& minBounds, const Vector3& maxBounds);

    // Generate random 3D vector within sphere radius
    Vector3 GetRandVector3InSphere(float radius, const Vector3& center = Vector3(0, 0, 0));
#endif

    // Generate random string from character set
    std::string GetRandString(int length, const std::string& characterSet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

    // Generate shuffled sequence of numbers
    std::vector<int> GetShuffledSequence(int start, int end);

    // Generate random number with triangular distribution (for more natural randomness)
    float GetRandTriangular(float min, float max, float mode);

    // Generate random number with bias towards center (bell curve-like)
    float GetRandBiasedToCenter(float min, float max, float bias = 2.0f);

    // Generate random number with bias towards edges
    float GetRandBiasedToEdges(float min, float max, float bias = 2.0f);

    // Generate random angle in radians
    float GetRandAngleRadians();

    // Generate random angle in degrees
    float GetRandAngleDegrees();

    // Generate random rotation (0-360 degrees)
    float GetRandRotation();

    // Coin flip simulation with configurable sides
    bool CoinFlip(float headsProb = 0.5f);

    // Generate random seed value for other systems
    uint64_t GetRandSeed();

    //==========================================================================
    // Gaming-Specific Random Generation Methods
    //==========================================================================

    // Generate random stat roll for RPG character creation
    int GetRandStatRoll(int numberOfDice = 3, int sidesPerDie = 6, bool dropLowest = true);

    // Generate random encounter chance
    bool CheckRandomEncounter(float encounterRate);

    // Generate random loot drop based on rarity tables
    int GetRandLootDrop(const std::vector<float>& rarityThresholds);

    // Generate random spawn timing
    float GetRandSpawnDelay(float minDelay, float maxDelay, float urgencyFactor = 1.0f);

    // Generate random movement direction (8-directional)
    int GetRandDirection8();

    // Generate random movement direction (4-directional)
    int GetRandDirection4();

    // Generate random AI behavior selection
    int GetRandAIBehavior(const std::vector<float>& behaviorWeights);

    //==========================================================================
    // Utility and Management Methods
    //==========================================================================

    // Clear all unique number trackers
    void ClearAllUniqueTrackers();

    // Clear specific unique number tracker for integer range
    void ClearUniqueTracker(int startRange, int endRange);

    // Clear specific unique number tracker for float range
    void ClearUniqueTracker(float startRange, float endRange);

    // Get statistics about current unique trackers
    size_t GetActiveTrackerCount() const;

    // Check if specific range has active tracker
    bool HasActiveTracker(int startRange, int endRange) const;
    bool HasActiveTracker(float startRange, float endRange) const;

private:
    //==========================================================================
    // Private Member Variables
    //==========================================================================

    // Initialization and state management
    bool m_isInitialized;                                               // Randomizer initialization status
    bool m_hasCleanedUp;                                                // Cleanup completion status

    // Random number generation engines
    std::mt19937_64 m_randomEngine;                                     // High-quality 64-bit Mersenne Twister engine
    std::random_device m_randomDevice;                                  // Hardware random device for seeding

    // Unique number tracking
    std::map<RangeKey, std::unique_ptr<UniqueNumberTracker>> m_uniqueTrackers; // Map of unique number trackers by range

    // Distribution generators for different types
    std::uniform_int_distribution<int> m_intDistribution;               // Integer distribution generator
    std::uniform_real_distribution<float> m_floatDistribution;         // Float distribution generator

    // Additional distribution generators for advanced functionality
    std::normal_distribution<float> m_normalDistribution;               // Normal (Gaussian) distribution generator
    std::exponential_distribution<float> m_exponentialDistribution;     // Exponential distribution generator
    std::bernoulli_distribution m_boolDistribution;                     // Boolean distribution generator

    // Gaming-specific constants
    static const std::string myDEFAULT_CHARSET;                         // Default character set for string generation

    //==========================================================================
    // Private Helper Methods
    //==========================================================================

    // Validation methods
    bool ValidateIntegerRange(int startRange, int endRange) const;      // Validate integer range parameters
    bool ValidateFloatRange(float startRange, float endRange) const;    // Validate float range parameters
    bool ValidateDifficulty(float difficulty) const;                    // Validate difficulty parameter
    bool ValidateTargetNumber(int startRange, int endRange, int targetNumber) const; // Validate integer target number
    bool ValidateTargetNumber(float startRange, float endRange, float targetNumber) const; // Validate float target number

    // Advanced distribution parameter validation
    bool ValidateNormalDistributionParams(float mean, float standardDeviation) const; // Validate normal distribution parameters
    bool ValidateExponentialParam(float lambda) const;                  // Validate exponential distribution parameter
    bool ValidateTriangularParams(float min, float max, float mode) const; // Validate triangular distribution parameters
    bool ValidateWeights(const std::vector<float>& weights) const;      // Validate weight vector
    template<typename T>
    bool ValidateElementsAndWeights(const std::vector<T>& elements, const std::vector<float>& weights) const; // Validate elements and weights match

    // Unique number tracking management
    UniqueNumberTracker* GetOrCreateUniqueTracker(int startRange, int endRange); // Get or create integer unique tracker
    UniqueNumberTracker* GetOrCreateUniqueTracker(float startRange, float endRange); // Get or create float unique tracker
    void RefreshUniqueTracker(UniqueNumberTracker* tracker, int startRange, int endRange); // Refresh unique tracker

    // Seeding and initialization
    void InitializeRandomEngine();                                      // Initialize random engine with high-quality seed
    uint64_t GenerateHighQualitySeed();                                 // Generate high-quality seed using multiple sources

    // Color generation helpers
    uint8_t GetRandColorComponent();                                    // Generate random color component (0-255)

    // Vector generation helpers
    bool ValidateVector2Bounds(const Vector2& minBounds, const Vector2& maxBounds) const; // Validate Vector2 bounds

    #if defined(__USE_OPENGL__)
        bool ValidateVector3Bounds(const Vector3& minBounds, const Vector3& maxBounds) const; // Validate Vector3 bounds
    #endif

    // Gaming calculation helpers
    float CalculateSpawnUrgency(float baseDelay, float urgencyFactor) const; // Calculate spawn delay with urgency
    int SelectWeightedIndex(const std::vector<float>& weights);         // Select index based on weights

    // Error handling and logging
    void LogError(const std::string& functionName, const std::string& errorMessage) const; // Log error messages
    void LogDebug(const std::string& functionName, const std::string& debugMessage) const; // Log debug messages
    void LogWarning(const std::string& functionName, const std::string& warningMessage) const; // Log warning messages

    // Difficulty calculation helpers
    float CalculateSuccessProbability(float difficulty) const;          // Calculate success probability from difficulty
    bool ShouldAttemptSucceed(float difficulty);                        // Determine if attempt should succeed based on difficulty
};

