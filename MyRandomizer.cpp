// -------------------------------------------------------------------------------------------------------------
// MyRandomizer.cpp - Implementation of high-performance cross-platform random number generation class
// Provides comprehensive random number functionality with unique tracking and probability-based targeting
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "MyRandomizer.h"

// External reference declarations
extern Debug debug;

#pragma warning(push)
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

//==============================================================================
// Constructor and Destructor Implementation
//==============================================================================

// Constructor - Initialize randomizer with high-quality seeding and default state
MyRandomizer::MyRandomizer() :
    m_isInitialized(false),                                             // Randomizer not yet initialized
    m_hasCleanedUp(false),                                              // Cleanup not yet performed
    m_randomEngine(),                                                   // Default construct random engine
    m_randomDevice(),                                                   // Default construct random device
    m_intDistribution(),                                                // Default construct integer distribution
    m_floatDistribution()                                               // Default construct float distribution
{
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer constructor called - initializing random number generator");
    #endif

    // Initialize with default parameters to ensure safe state
    // Actual initialization will occur in Initialize() method
}

// Destructor - Ensure proper cleanup of all randomizer resources
MyRandomizer::~MyRandomizer() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer destructor called - cleaning up random number generator");
    #endif

    // Perform cleanup if not already done
    if (!m_hasCleanedUp) {
        Cleanup();
    }

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer destructor completed - all resources cleaned up");
    #endif
}

//==============================================================================
// Initialization and Cleanup Methods Implementation
//==============================================================================

// Initialize the randomizer subsystem with high-quality seeding
bool MyRandomizer::Initialize() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer::Initialize() called - starting initialization process");
    #endif

    // Prevent double initialization
    if (m_isInitialized) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"MyRandomizer already initialized - skipping initialization");
        #endif
        return true;
    }

    try {
        // Initialize random engine with high-quality seeding
        InitializeRandomEngine();

        // Clear any existing unique trackers
        ClearAllUniqueTrackers();

        // Mark as successfully initialized
        m_isInitialized = true;
        m_hasCleanedUp = false;

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer initialization completed successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            std::string errorMsg = e.what();
            std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"MyRandomizer initialization failed with exception: " + wErrorMsg);
        #endif
        return false;
    }
}

// Clean up all randomizer resources and reset to safe state
void MyRandomizer::Cleanup() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer::Cleanup() called - starting cleanup process");
    #endif

    // Prevent double cleanup
    if (m_hasCleanedUp) {
        return;
    }

    // Clear all unique number trackers and free memory
    ClearAllUniqueTrackers();

    // Reset initialization state
    m_isInitialized = false;
    m_hasCleanedUp = true;

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer cleanup completed successfully");
    #endif
}

//==============================================================================
// Integer Random Number Generation Methods Implementation
//==============================================================================

// Generate random integer number within specified range (inclusive)
int MyRandomizer::GetRandNum(int startRange, int endRange) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandNum(int) called - Range: %d to %d", startRange, endRange);
    #endif

    // Validate input parameters
    if (!ValidateIntegerRange(startRange, endRange)) {
        LogError("GetRandNum(int)", "Invalid range parameters");
        return 0;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandNum(int)", "Randomizer not initialized");
        return 0;
    }

    try {
        // Create distribution for the specified range
        std::uniform_int_distribution<int> distribution(startRange, endRange);

        // Generate random number using high-quality engine
        int randomNumber = distribution(m_randomEngine);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandNum(int) generated: %d", randomNumber);
        #endif

        return randomNumber;
    }
    catch (const std::exception& e) {
        LogError("GetRandNum(int)", "Exception during random number generation: " + std::string(e.what()));
        return 0;
    }
}

// Select unique random integer number from specified range (inclusive)
int MyRandomizer::SelUniqueRandNum(int startRange, int endRange) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelUniqueRandNum(int) called - Range: %d to %d", startRange, endRange);
    #endif

    // Validate input parameters
    if (!ValidateIntegerRange(startRange, endRange)) {
        LogError("SelUniqueRandNum(int)", "Invalid range parameters");
        return 0;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("SelUniqueRandNum(int)", "Randomizer not initialized");
        return 0;
    }

    try {
        // Get or create unique tracker for this range
        UniqueNumberTracker* tracker = GetOrCreateUniqueTracker(startRange, endRange);
        if (!tracker) {
            LogError("SelUniqueRandNum(int)", "Failed to create unique tracker");
            return 0;
        }

        // Check if tracker needs refresh (all numbers have been used)
        if (tracker->needsRefresh || tracker->availableNumbers.empty()) {
            RefreshUniqueTracker(tracker, startRange, endRange);
            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelUniqueRandNum(int) refreshed tracker - Available: %zu",
                    tracker->availableNumbers.size());
            #endif
        }

        // Generate random index to select from available numbers
        std::uniform_int_distribution<size_t> indexDistribution(0, tracker->availableNumbers.size() - 1);
        size_t randomIndex = indexDistribution(m_randomEngine);

        // Get the selected number and mark as used
        int selectedNumber = tracker->availableNumbers[randomIndex];
        tracker->MarkNumberAsUsed(selectedNumber, startRange);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelUniqueRandNum(int) selected: %d, Remaining: %zu",
                selectedNumber, tracker->availableNumbers.size());
        #endif

        return selectedNumber;
    }
    catch (const std::exception& e) {
        LogError("SelUniqueRandNum(int)", "Exception during unique random number generation: " + std::string(e.what()));
        return 0;
    }
}

// Attempt to get target integer number with specified difficulty
int MyRandomizer::TryTargetNumber(int startRange, int endRange, int targetNumber, float difficulty) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryTargetNumber(int) called - Range: %d to %d, Target: %d, Difficulty: %.3f",
            startRange, endRange, targetNumber, difficulty);
    #endif

    // Validate input parameters
    if (!ValidateIntegerRange(startRange, endRange)) {
        LogError("TryTargetNumber(int)", "Invalid range parameters");
        return 0;
    }

    if (!ValidateTargetNumber(startRange, endRange, targetNumber)) {
        LogError("TryTargetNumber(int)", "Invalid target number");
        return 0;
    }

    if (!ValidateDifficulty(difficulty)) {
        LogError("TryTargetNumber(int)", "Invalid difficulty value");
        return 0;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("TryTargetNumber(int)", "Randomizer not initialized");
        return 0;
    }

    try {
        // Check if attempt should succeed based on difficulty
        if (ShouldAttemptSucceed(difficulty)) {
            // Success - return target number
            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryTargetNumber(int) succeeded - Target: %d", targetNumber);
            #endif
            return targetNumber;
        }
        else {
            // Failure - generate random number from range (excluding target if possible)
            int randomNumber;
            int rangeSize = endRange - startRange + 1;

            if (rangeSize > 1) {
                // Generate random number excluding target
                do {
                    randomNumber = GetRandNum(startRange, endRange);
                } while (randomNumber == targetNumber);
            }
            else {
                // Only one number in range, must return it
                randomNumber = targetNumber;
            }

            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryTargetNumber(int) failed - Generated: %d instead of %d",
                    randomNumber, targetNumber);
            #endif
            return randomNumber;
        }
    }
    catch (const std::exception& e) {
        LogError("TryTargetNumber(int)", "Exception during target number attempt: " + std::string(e.what()));
        return 0;
    }
}

// Multiple attempts to get target integer number with specified difficulty
int MyRandomizer::TryAttemptTargetNum(int startRange, int endRange, int numberOfAttempts, int targetNumber, float difficulty, int& attempted) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryAttemptTargetNum(int) called - Range: %d to %d, Target: %d, Attempts: %d, Difficulty: %.3f",
        startRange, endRange, targetNumber, numberOfAttempts, difficulty);
#endif

    // Initialize attempted counter
    attempted = 0;

    // Validate input parameters
    if (!ValidateIntegerRange(startRange, endRange)) {
        LogError("TryAttemptTargetNum(int)", "Invalid range parameters");
        return 0;
    }

    if (!ValidateTargetNumber(startRange, endRange, targetNumber)) {
        LogError("TryAttemptTargetNum(int)", "Invalid target number");
        return 0;
    }

    if (!ValidateDifficulty(difficulty)) {
        LogError("TryAttemptTargetNum(int)", "Invalid difficulty value");
        return 0;
    }

    if (numberOfAttempts <= 0 || numberOfAttempts > MYRANDOMIZER_MAX_ATTEMPTS) {
        LogError("TryAttemptTargetNum(int)", "Invalid number of attempts");
        return 0;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("TryAttemptTargetNum(int)", "Randomizer not initialized");
        return 0;
    }

    try {
        // Attempt to get target number up to numberOfAttempts times
        for (int i = 0; i < numberOfAttempts; ++i) {
            attempted = i + 1;  // Update attempt counter

            // Try to get target number
            int result = TryTargetNumber(startRange, endRange, targetNumber, difficulty);

            // Check if we hit the target
            if (result == targetNumber) {
                #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryAttemptTargetNum(int) succeeded on attempt %d", attempted);
                #endif
                return targetNumber;  // Success - return target number
            }
        }

        // All attempts failed
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryAttemptTargetNum(int) failed after %d attempts", attempted);
        #endif
        return 0;  // Failure - return 0 as specified
    }
    catch (const std::exception& e) {
        LogError("TryAttemptTargetNum(int)", "Exception during multiple target attempts: " + std::string(e.what()));
        return 0;
    }
}

// Get list of unique random integer numbers from specified range
std::vector<int> MyRandomizer::GetListOfUniqueRandNums(int startRange, int endRange, int numOfNumbers) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetListOfUniqueRandNums(int) called - Range: %d to %d, Count: %d",
            startRange, endRange, numOfNumbers);
    #endif

    std::vector<int> resultList;

    // Validate input parameters
    if (!ValidateIntegerRange(startRange, endRange)) {
        LogError("GetListOfUniqueRandNums(int)", "Invalid range parameters");
        return resultList;  // Return empty list
    }

    if (numOfNumbers <= 0) {
        LogError("GetListOfUniqueRandNums(int)", "Invalid number of numbers requested");
        return resultList;  // Return empty list
    }

    // Calculate total numbers available in range
    int totalNumbers = endRange - startRange + 1;
    if (numOfNumbers > totalNumbers) {
        LogWarning("GetListOfUniqueRandNums(int)", "Requested more numbers than available in range, clamping to range size");
        numOfNumbers = totalNumbers;  // Clamp to maximum available
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetListOfUniqueRandNums(int)", "Randomizer not initialized");
        return resultList;  // Return empty list
    }

    try {
        // Reserve space for efficiency
        resultList.reserve(numOfNumbers);

        // Generate unique numbers using SelUniqueRandNum
        for (int i = 0; i < numOfNumbers; ++i) {
            int uniqueNumber = SelUniqueRandNum(startRange, endRange);
            if (uniqueNumber != 0) {  // Valid number returned
                resultList.push_back(uniqueNumber);
            }
            else {
                LogError("GetListOfUniqueRandNums(int)", "Failed to generate unique number");
                break;  // Stop on error
            }
        }

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetListOfUniqueRandNums(int) generated %zu numbers", resultList.size());
        #endif

        return resultList;
    }
    catch (const std::exception& e) {
        LogError("GetListOfUniqueRandNums(int)", "Exception during unique list generation: " + std::string(e.what()));
        return resultList;  // Return empty list on error
    }
}

//==============================================================================
// Float Random Number Generation Methods Implementation
//==============================================================================

// Generate random float number within specified range (inclusive)
float MyRandomizer::GetRandNum(float startRange, float endRange) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandNum(float) called - Range: %.3f to %.3f", startRange, endRange);
    #endif

    // Validate input parameters
    if (!ValidateFloatRange(startRange, endRange)) {
        LogError("GetRandNum(float)", "Invalid range parameters");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandNum(float)", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Create distribution for the specified range
        std::uniform_real_distribution<float> distribution(startRange, endRange);

        // Generate random number using high-quality engine
        float randomNumber = distribution(m_randomEngine);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandNum(float) generated: %.3f", randomNumber);
        #endif

        return randomNumber;
    }
    catch (const std::exception& e) {
        LogError("GetRandNum(float)", "Exception during random number generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Select unique random float number from specified range (using scaled integer tracking)
float MyRandomizer::SelUniqueRandNum(float startRange, float endRange) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelUniqueRandNum(float) called - Range: %.3f to %.3f", startRange, endRange);
    #endif

    // Validate input parameters
    if (!ValidateFloatRange(startRange, endRange)) {
        LogError("SelUniqueRandNum(float)", "Invalid range parameters");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("SelUniqueRandNum(float)", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Scale float range to integer range for tracking (multiply by 1000 for 3 decimal places)
        int scaledStart = static_cast<int>(startRange * 1000);
        int scaledEnd = static_cast<int>(endRange * 1000);

        // Get or create unique tracker for this scaled range
        UniqueNumberTracker* tracker = GetOrCreateUniqueTracker(startRange, endRange);
        if (!tracker) {
            LogError("SelUniqueRandNum(float)", "Failed to create unique tracker");
            return 0.0f;
        }

        // Check if tracker needs refresh (all numbers have been used)
        if (tracker->needsRefresh || tracker->availableNumbers.empty()) {
            RefreshUniqueTracker(tracker, scaledStart, scaledEnd);
            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelUniqueRandNum(float) refreshed tracker - Available: %zu",
                    tracker->availableNumbers.size());
            #endif
        }

        // Generate random index to select from available numbers
        std::uniform_int_distribution<size_t> indexDistribution(0, tracker->availableNumbers.size() - 1);
        size_t randomIndex = indexDistribution(m_randomEngine);

        // Get the selected scaled number and mark as used
        int selectedScaledNumber = tracker->availableNumbers[randomIndex];
        tracker->MarkNumberAsUsed(selectedScaledNumber, scaledStart);

        // Convert back to float
        float selectedNumber = static_cast<float>(selectedScaledNumber) / 1000.0f;

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelUniqueRandNum(float) selected: %.3f, Remaining: %zu",
                selectedNumber, tracker->availableNumbers.size());
        #endif

        return selectedNumber;
    }
    catch (const std::exception& e) {
        LogError("SelUniqueRandNum(float)", "Exception during unique random number generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Attempt to get target float number with specified difficulty
float MyRandomizer::TryTargetNumber(float startRange, float endRange, float targetNumber, float difficulty) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryTargetNumber(float) called - Range: %.3f to %.3f, Target: %.3f, Difficulty: %.3f",
            startRange, endRange, targetNumber, difficulty);
    #endif

    // Validate input parameters
    if (!ValidateFloatRange(startRange, endRange)) {
        LogError("TryTargetNumber(float)", "Invalid range parameters");
        return 0.0f;
    }

    if (!ValidateTargetNumber(startRange, endRange, targetNumber)) {
        LogError("TryTargetNumber(float)", "Invalid target number");
        return 0.0f;
    }

    if (!ValidateDifficulty(difficulty)) {
        LogError("TryTargetNumber(float)", "Invalid difficulty value");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("TryTargetNumber(float)", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Check if attempt should succeed based on difficulty
        if (ShouldAttemptSucceed(difficulty)) {
            // Success - return target number
            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryTargetNumber(float) succeeded - Target: %.3f", targetNumber);
            #endif
            return targetNumber;
        }
        else {
            // Failure - generate random number from range (try to avoid target if possible)
            float randomNumber;
            float rangeSize = endRange - startRange;

            if (rangeSize > 0.001f) {  // Reasonable range size for floats
                // Generate random number, preferably not the target
                int attempts = 0;
                do {
                    randomNumber = GetRandNum(startRange, endRange);
                    attempts++;
                } while (std::abs(randomNumber - targetNumber) < 0.001f && attempts < 10);  // Allow small tolerance and limit attempts
            }
            else {
                // Very small range, just generate any number in range
                randomNumber = GetRandNum(startRange, endRange);
            }

            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryTargetNumber(float) failed - Generated: %.3f instead of %.3f",
                    randomNumber, targetNumber);
            #endif
            return randomNumber;
        }
    }
    catch (const std::exception& e) {
        LogError("TryTargetNumber(float)", "Exception during target number attempt: " + std::string(e.what()));
        return 0.0f;
    }
}

// Multiple attempts to get target float number with specified difficulty
float MyRandomizer::TryAttemptTargetNum(float startRange, float endRange, int numberOfAttempts, float targetNumber, float difficulty, int& attempted) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryAttemptTargetNum(float) called - Range: %.3f to %.3f, Target: %.3f, Attempts: %d, Difficulty: %.3f",
            startRange, endRange, targetNumber, numberOfAttempts, difficulty);
    #endif

    // Initialize attempted counter
    attempted = 0;

    // Validate input parameters
    if (!ValidateFloatRange(startRange, endRange)) {
        LogError("TryAttemptTargetNum(float)", "Invalid range parameters");
        return 0.0f;
    }

    if (!ValidateTargetNumber(startRange, endRange, targetNumber)) {
        LogError("TryAttemptTargetNum(float)", "Invalid target number");
        return 0.0f;
    }

    if (!ValidateDifficulty(difficulty)) {
        LogError("TryAttemptTargetNum(float)", "Invalid difficulty value");
        return 0.0f;
    }

    if (numberOfAttempts <= 0 || numberOfAttempts > MYRANDOMIZER_MAX_ATTEMPTS) {
        LogError("TryAttemptTargetNum(float)", "Invalid number of attempts");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("TryAttemptTargetNum(float)", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Attempt to get target number up to numberOfAttempts times
        for (int i = 0; i < numberOfAttempts; ++i) {
            attempted = i + 1;  // Update attempt counter

            // Try to get target number
            float result = TryTargetNumber(startRange, endRange, targetNumber, difficulty);

            // Check if we hit the target (with small tolerance for floating point comparison)
            if (std::abs(result - targetNumber) < 0.001f) {
                #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryAttemptTargetNum(float) succeeded on attempt %d", attempted);
                #endif
                return targetNumber;  // Success - return target number
            }
        }

        // All attempts failed
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TryAttemptTargetNum(float) failed after %d attempts", attempted);
        #endif
        return 0.0f;  // Failure - return 0.0f as specified
    }
    catch (const std::exception& e) {
        LogError("TryAttemptTargetNum(float)", "Exception during multiple target attempts: " + std::string(e.what()));
        return 0.0f;
    }
}

// Get list of unique random float numbers from specified range
std::vector<float> MyRandomizer::GetListOfUniqueRandNums(float startRange, float endRange, int numOfNumbers) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetListOfUniqueRandNums(float) called - Range: %.3f to %.3f, Count: %d",
            startRange, endRange, numOfNumbers);
    #endif

    std::vector<float> resultList;

    // Validate input parameters
    if (!ValidateFloatRange(startRange, endRange)) {
        LogError("GetListOfUniqueRandNums(float)", "Invalid range parameters");
        return resultList;  // Return empty list
    }

    if (numOfNumbers <= 0) {
        LogError("GetListOfUniqueRandNums(float)", "Invalid number of numbers requested");
        return resultList;  // Return empty list
    }

    // Calculate total numbers available in scaled range (multiply by 1000 for 3 decimal places)
    int scaledStart = static_cast<int>(startRange * 1000);
    int scaledEnd = static_cast<int>(endRange * 1000);
    int totalNumbers = scaledEnd - scaledStart + 1;

    if (numOfNumbers > totalNumbers) {
        LogWarning("GetListOfUniqueRandNums(float)", "Requested more numbers than available in range, clamping to range size");
        numOfNumbers = totalNumbers;  // Clamp to maximum available
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetListOfUniqueRandNums(float)", "Randomizer not initialized");
        return resultList;  // Return empty list
    }

    try {
        // Reserve space for efficiency
        resultList.reserve(numOfNumbers);

        // Generate unique numbers using SelUniqueRandNum
        for (int i = 0; i < numOfNumbers; ++i) {
            float uniqueNumber = SelUniqueRandNum(startRange, endRange);
            if (uniqueNumber != 0.0f) {  // Valid number returned
                resultList.push_back(uniqueNumber);
            }
            else {
                LogError("GetListOfUniqueRandNums(float)", "Failed to generate unique number");
                break;  // Stop on error
            }
        }

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetListOfUniqueRandNums(float) generated %zu numbers", resultList.size());
        #endif

        return resultList;
    }
    catch (const std::exception& e) {
        LogError("GetListOfUniqueRandNums(float)", "Exception during unique list generation: " + std::string(e.what()));
        return resultList;  // Return empty list on error
    }
}

//==============================================================================
// Specialized Random Number Generation Methods Implementation
//==============================================================================

// Generate random percentage from 0.001 to 1.0 inclusive
float MyRandomizer::GetRandPercentage() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandPercentage() called");
    #endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandPercentage", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Generate random percentage using predefined range
        float percentage = GetRandNum(MYRANDOMIZER_MIN_PERCENTAGE, MYRANDOMIZER_MAX_PERCENTAGE);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandPercentage() generated: %.3f", percentage);
        #endif

        return percentage;
    }
    catch (const std::exception& e) {
        LogError("GetRandPercentage", "Exception during percentage generation: " + std::string(e.what()));
        return 0.0f;
    }
}

//==============================================================================
// Utility and Management Methods Implementation
//==============================================================================

// Clear all unique number trackers and free memory
void MyRandomizer::ClearAllUniqueTrackers() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"ClearAllUniqueTrackers() called - Clearing %zu trackers", m_uniqueTrackers.size());
    #endif

    // Clear all unique trackers from map
    m_uniqueTrackers.clear();

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"All unique trackers cleared successfully");
    #endif
}

// Clear specific unique number tracker for integer range
void MyRandomizer::ClearUniqueTracker(int startRange, int endRange) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"ClearUniqueTracker(int) called - Range: %d to %d", startRange, endRange);
    #endif

    // Create range key for lookup
    RangeKey key(startRange, endRange);

    // Find and remove tracker if it exists
    auto it = m_uniqueTrackers.find(key);
    if (it != m_uniqueTrackers.end()) {
        m_uniqueTrackers.erase(it);
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Integer unique tracker cleared successfully");
        #endif
    }
    else {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Integer unique tracker not found - nothing to clear");
        #endif
    }
}

// Clear specific unique number tracker for float range
void MyRandomizer::ClearUniqueTracker(float startRange, float endRange) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"ClearUniqueTracker(float) called - Range: %.3f to %.3f", startRange, endRange);
    #endif

    // Create range key for lookup
    RangeKey key(startRange, endRange);

    // Find and remove tracker if it exists
    auto it = m_uniqueTrackers.find(key);
    if (it != m_uniqueTrackers.end()) {
        m_uniqueTrackers.erase(it);
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Float unique tracker cleared successfully");
        #endif
    }
    else {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Float unique tracker not found - nothing to clear");
        #endif
    }
}

// Get statistics about current active unique trackers
size_t MyRandomizer::GetActiveTrackerCount() const {
    return m_uniqueTrackers.size();
}

// Check if specific integer range has active unique tracker
bool MyRandomizer::HasActiveTracker(int startRange, int endRange) const {
    RangeKey key(startRange, endRange);
    return m_uniqueTrackers.find(key) != m_uniqueTrackers.end();
}

// Check if specific float range has active unique tracker
bool MyRandomizer::HasActiveTracker(float startRange, float endRange) const {
    RangeKey key(startRange, endRange);
    return m_uniqueTrackers.find(key) != m_uniqueTrackers.end();
}

//==============================================================================
// Private Helper Methods Implementation
//==============================================================================

// Validate integer range parameters
bool MyRandomizer::ValidateIntegerRange(int startRange, int endRange) const {
    // Check if start range is valid (must be >= 1)
    if (startRange < MYRANDOMIZER_MIN_STARTRANGE) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid start range: %d (must be >= %d)", startRange, MYRANDOMIZER_MIN_STARTRANGE);
        #endif
        return false;
    }

    // Check if end range is greater than or equal to start range
    if (endRange < startRange) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid range: end (%d) < start (%d)", endRange, startRange);
        #endif
        return false;
    }

    return true;
}

// Validate float range parameters
bool MyRandomizer::ValidateFloatRange(float startRange, float endRange) const {
    // Check if start range is valid (must be > 0.0f)
    if (startRange <= 0.0f) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid start range: %.3f (must be > 0.0)", startRange);
        #endif
        return false;
    }

    // Check if end range is greater than start range
    if (endRange <= startRange) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid range: end (%.3f) <= start (%.3f)", endRange, startRange);
        #endif
        return false;
    }

    return true;
}

// Validate difficulty parameter
bool MyRandomizer::ValidateDifficulty(float difficulty) const {
    // Check if difficulty is within valid range
    if (difficulty < MYRANDOMIZER_MIN_DIFFICULTY || difficulty > MYRANDOMIZER_MAX_DIFFICULTY) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid difficulty: %.3f (must be between %.3f and %.3f)",
                difficulty, MYRANDOMIZER_MIN_DIFFICULTY, MYRANDOMIZER_MAX_DIFFICULTY);
        #endif
        return false;
    }

    return true;
}

// Validate integer target number parameter
bool MyRandomizer::ValidateTargetNumber(int startRange, int endRange, int targetNumber) const {
    // Check if target number is within range
    if (targetNumber < startRange || targetNumber > endRange) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid target number: %d (must be between %d and %d)",
                targetNumber, startRange, endRange);
        #endif
        return false;
    }

    return true;
}

// Validate float target number parameter
bool MyRandomizer::ValidateTargetNumber(float startRange, float endRange, float targetNumber) const {
    // Check if target number is within range
    if (targetNumber < startRange || targetNumber > endRange) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid target number: %.3f (must be between %.3f and %.3f)",
                targetNumber, startRange, endRange);
        #endif
        return false;
    }

    return true;
}

// Get or create unique tracker for integer range
UniqueNumberTracker* MyRandomizer::GetOrCreateUniqueTracker(int startRange, int endRange) {
    // Create range key for lookup
    RangeKey key(startRange, endRange);

    // Check if tracker already exists
    auto it = m_uniqueTrackers.find(key);
    if (it != m_uniqueTrackers.end()) {
        return it->second.get();  // Return existing tracker
    }

    // Create new tracker
    try {
        auto newTracker = std::make_unique<UniqueNumberTracker>(startRange, endRange);
        UniqueNumberTracker* trackerPtr = newTracker.get();
        m_uniqueTrackers[key] = std::move(newTracker);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Created new integer unique tracker - Range: %d to %d", startRange, endRange);
        #endif

        return trackerPtr;
    }
    catch (const std::exception& e) {
        LogError("GetOrCreateUniqueTracker(int)", "Failed to create unique tracker: " + std::string(e.what()));
        return nullptr;
    }
}

// Get or create unique tracker for float range
UniqueNumberTracker* MyRandomizer::GetOrCreateUniqueTracker(float startRange, float endRange) {
    // Create range key for lookup (scaled to integers)
    RangeKey key(startRange, endRange);

    // Check if tracker already exists
    auto it = m_uniqueTrackers.find(key);
    if (it != m_uniqueTrackers.end()) {
        return it->second.get();  // Return existing tracker
    }

    // Create new tracker with scaled integer range
    try {
        int scaledStart = static_cast<int>(startRange * 1000);
        int scaledEnd = static_cast<int>(endRange * 1000);

        auto newTracker = std::make_unique<UniqueNumberTracker>(scaledStart, scaledEnd);
        UniqueNumberTracker* trackerPtr = newTracker.get();
        m_uniqueTrackers[key] = std::move(newTracker);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Created new float unique tracker - Range: %.3f to %.3f (scaled: %d to %d)",
                startRange, endRange, scaledStart, scaledEnd);
        #endif

        return trackerPtr;
    }
    catch (const std::exception& e) {
        LogError("GetOrCreateUniqueTracker(float)", "Failed to create unique tracker: " + std::string(e.what()));
        return nullptr;
    }
}

// Refresh unique tracker with new available numbers
void MyRandomizer::RefreshUniqueTracker(UniqueNumberTracker* tracker, int startRange, int endRange) {
    if (!tracker) {
        LogError("RefreshUniqueTracker", "Null tracker pointer");
        return;
    }

    try {
        // Refresh the tracker with new range
        tracker->RefreshAvailableNumbers(startRange, endRange);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Refreshed unique tracker - Range: %d to %d, Available: %zu",
                startRange, endRange, tracker->availableNumbers.size());
        #endif
    }
    catch (const std::exception& e) {
        LogError("RefreshUniqueTracker", "Exception during tracker refresh: " + std::string(e.what()));
    }
}

// Initialize random engine with high-quality seeding
void MyRandomizer::InitializeRandomEngine() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Initializing random engine with high-quality seeding");
    #endif

    try {
        // Generate high-quality seed combining multiple entropy sources
        uint64_t seed = GenerateHighQualitySeed();

        // Seed the random engine
        m_randomEngine.seed(seed);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Random engine initialized with seed: %llu", seed);
        #endif
    }
    catch (const std::exception& e) {
        LogError("InitializeRandomEngine", "Exception during random engine initialization: " + std::string(e.what()));

        // Fallback to simpler seeding if high-quality seeding fails
        auto now = std::chrono::high_resolution_clock::now();
        auto seed = static_cast<uint64_t>(now.time_since_epoch().count());
        m_randomEngine.seed(seed);

        LogWarning("InitializeRandomEngine", "Using fallback seeding method");
    }
}

// Generate high-quality seed using multiple entropy sources
uint64_t MyRandomizer::GenerateHighQualitySeed() {
    uint64_t seed = 0;

    try {
        // Try to use hardware random device for primary entropy
        if (m_randomDevice.entropy() > 0.0) {
            // Hardware entropy available - use multiple samples
            seed = m_randomDevice();
            seed ^= static_cast<uint64_t>(m_randomDevice()) << 32;
        }
        else {
            // No hardware entropy - use time-based seed
            auto now = std::chrono::high_resolution_clock::now();
            seed = static_cast<uint64_t>(now.time_since_epoch().count());
        }

        // Add additional entropy from current time with nanosecond precision
        auto timePoint = std::chrono::high_resolution_clock::now();
        auto timeSeed = static_cast<uint64_t>(timePoint.time_since_epoch().count());
        seed ^= timeSeed;

        // Add entropy from memory address (ASLR provides some randomness)
        uintptr_t addressSeed = reinterpret_cast<uintptr_t>(this);
        seed ^= static_cast<uint64_t>(addressSeed);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Generated high-quality seed: %llu", seed);
        #endif

        return seed;
    }
    catch (const std::exception& e) {
        LogError("GenerateHighQualitySeed", "Exception during seed generation: " + std::string(e.what()));

        // Return basic time-based seed as fallback
        auto now = std::chrono::high_resolution_clock::now();
        return static_cast<uint64_t>(now.time_since_epoch().count());
    }
}

// Calculate success probability from difficulty value
float MyRandomizer::CalculateSuccessProbability(float difficulty) const {
    // Invert difficulty to get success probability
    // Lower difficulty (0.001) = higher success probability (0.999)
    // Higher difficulty (0.99) = lower success probability (0.01)
    return 1.0f - difficulty;
}

// Determine if attempt should succeed based on difficulty
bool MyRandomizer::ShouldAttemptSucceed(float difficulty) {
    try {
        // Calculate success probability
        float successProbability = CalculateSuccessProbability(difficulty);

        // Generate random number between 0.0 and 1.0
        std::uniform_real_distribution<float> probabilityDistribution(0.0f, 1.0f);
        float randomValue = probabilityDistribution(m_randomEngine);

        // Check if random value is within success probability
        bool shouldSucceed = randomValue <= successProbability;

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"ShouldAttemptSucceed - Difficulty: %.3f, Success Prob: %.3f, Random: %.3f, Result: %s",
                difficulty, successProbability, randomValue, shouldSucceed ? L"Success" : L"Failure");
        #endif

        return shouldSucceed;
    }
    catch (const std::exception& e) {
        LogError("ShouldAttemptSucceed", "Exception during success calculation: " + std::string(e.what()));
        return false;  // Default to failure on error
    }
}

// Log error messages with consistent formatting
void MyRandomizer::LogError(const std::string& functionName, const std::string& errorMessage) const {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        std::string fullMessage = "[MyRandomizer::" + functionName + "] " + errorMessage;
        std::wstring wFullMessage(fullMessage.begin(), fullMessage.end());
        debug.logLevelMessage(LogLevel::LOG_ERROR, wFullMessage);
    #endif
}

// Log debug messages with consistent formatting
void MyRandomizer::LogDebug(const std::string& functionName, const std::string& debugMessage) const {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        std::string fullMessage = "[MyRandomizer::" + functionName + "] " + debugMessage;
        std::wstring wFullMessage(fullMessage.begin(), fullMessage.end());
        debug.logLevelMessage(LogLevel::LOG_DEBUG, wFullMessage);
    #endif
}

// Log warning messages with consistent formatting
void MyRandomizer::LogWarning(const std::string& functionName, const std::string& warningMessage) const {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        std::string fullMessage = "[MyRandomizer::" + functionName + "] " + warningMessage;
        std::wstring wFullMessage(fullMessage.begin(), fullMessage.end());
        debug.logLevelMessage(LogLevel::LOG_WARNING, wFullMessage);
    #endif
}

//==============================================================================
// Template Method Implementations
//==============================================================================

// Template implementation for getting random element from vector
template<typename T>
T MyRandomizer::GetRandElement(const std::vector<T>& elements) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandElement called with %zu elements", elements.size());
#endif

    // Validate input parameters
    if (elements.empty()) {
        LogError("GetRandElement", "Empty elements vector provided");
        return T();  // Return default-constructed element
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandElement", "Randomizer not initialized");
        return T();  // Return default-constructed element
    }

    try {
        // Generate random index within vector bounds
        size_t randomIndex = static_cast<size_t>(GetRandNum(0, static_cast<int>(elements.size() - 1)));

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandElement selected index %zu", randomIndex);
#endif

        return elements[randomIndex];
    }
    catch (const std::exception& e) {
        LogError("GetRandElement", "Exception during element selection: " + std::string(e.what()));
        return T();  // Return default-constructed element on error
    }
}

// Template implementation for weighted element selection
template<typename T>
T MyRandomizer::GetRandWeightedElement(const std::vector<T>& elements, const std::vector<float>& weights) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandWeightedElement called with %zu elements and %zu weights",
        elements.size(), weights.size());
#endif

    // Validate input parameters
    if (!ValidateElementsAndWeights(elements, weights)) {
        LogError("GetRandWeightedElement", "Invalid elements or weights");
        return T();  // Return default-constructed element
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandWeightedElement", "Randomizer not initialized");
        return T();  // Return default-constructed element
    }

    try {
        // Select weighted index
        int selectedIndex = SelectWeightedIndex(weights);

        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(elements.size())) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandWeightedElement selected index %d", selectedIndex);
#endif
            return elements[selectedIndex];
        }
        else {
            LogError("GetRandWeightedElement", "Invalid selected index");
            return T();  // Return default-constructed element
        }
    }
    catch (const std::exception& e) {
        LogError("GetRandWeightedElement", "Exception during weighted selection: " + std::string(e.what()));
        return T();  // Return default-constructed element on error
    }
}

// Template implementation for elements and weights validation
template<typename T>
bool MyRandomizer::ValidateElementsAndWeights(const std::vector<T>& elements, const std::vector<float>& weights) const {
    // Check if vectors are empty
    if (elements.empty() || weights.empty()) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Empty elements or weights vector");
#endif
        return false;
    }

    // Check if vectors have same size
    if (elements.size() != weights.size()) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Elements and weights size mismatch: %zu vs %zu",
            elements.size(), weights.size());
#endif
        return false;
    }

    // Validate weights using existing method
    return ValidateWeights(weights);
}

//==============================================================================
// Advanced Random Number Generation Methods Implementation
//==============================================================================

// Generate random boolean value with optional bias
bool MyRandomizer::GetRandBool(float trueProbability) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandBool called with probability: %.3f", trueProbability);
#endif

    // Validate probability parameter
    if (trueProbability < 0.0f || trueProbability > 1.0f) {
        LogError("GetRandBool", "Invalid probability value (must be 0.0-1.0)");
        return false;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandBool", "Randomizer not initialized");
        return false;
    }

    try {
        // Use Bernoulli distribution for boolean generation
        std::bernoulli_distribution distribution(trueProbability);
        bool result = distribution(m_randomEngine);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandBool generated: %s", result ? L"true" : L"false");
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandBool", "Exception during boolean generation: " + std::string(e.what()));
        return false;
    }
}

// Generate random number using normal (Gaussian) distribution
float MyRandomizer::GetRandNormal(float mean, float standardDeviation) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandNormal called - Mean: %.3f, StdDev: %.3f", mean, standardDeviation);
#endif

    // Validate parameters
    if (!ValidateNormalDistributionParams(mean, standardDeviation)) {
        LogError("GetRandNormal", "Invalid normal distribution parameters");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandNormal", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Create normal distribution with specified parameters
        std::normal_distribution<float> distribution(mean, standardDeviation);
        float result = distribution(m_randomEngine);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandNormal generated: %.3f", result);
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandNormal", "Exception during normal distribution generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random number using exponential distribution
float MyRandomizer::GetRandExponential(float lambda) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandExponential called with lambda: %.3f", lambda);
#endif

    // Validate parameter
    if (!ValidateExponentialParam(lambda)) {
        LogError("GetRandExponential", "Invalid lambda parameter");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandExponential", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Create exponential distribution with specified lambda
        std::exponential_distribution<float> distribution(lambda);
        float result = distribution(m_randomEngine);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandExponential generated: %.3f", result);
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandExponential", "Exception during exponential distribution generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random dice roll (standard gaming dice)
int MyRandomizer::RollDice(int numberOfDice, int sidesPerDie, int modifier) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"RollDice called - Dice: %d, Sides: %d, Modifier: %d",
        numberOfDice, sidesPerDie, modifier);
#endif

    // Validate parameters
    if (numberOfDice <= 0 || numberOfDice > MYRANDOMIZER_MAX_DICE_COUNT) {
        LogError("RollDice", "Invalid number of dice");
        return 0;
    }

    if (sidesPerDie <= 0 || sidesPerDie > MYRANDOMIZER_MAX_DICE_SIDES) {
        LogError("RollDice", "Invalid number of sides per die");
        return 0;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("RollDice", "Randomizer not initialized");
        return 0;
    }

    try {
        int totalRoll = 0;

        // Roll each die and sum results
        for (int i = 0; i < numberOfDice; ++i) {
            int dieRoll = GetRandNum(1, sidesPerDie);
            totalRoll += dieRoll;

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Die #%d rolled: %d", i + 1, dieRoll);
#endif
        }

        // Apply modifier
        totalRoll += modifier;

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"RollDice total result: %d", totalRoll);
#endif

        return totalRoll;
    }
    catch (const std::exception& e) {
        LogError("RollDice", "Exception during dice roll: " + std::string(e.what()));
        return 0;
    }
}

// Generate random color components
MyColor MyRandomizer::GetRandColor(bool includeAlpha) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandColor called - Include Alpha: %s", includeAlpha ? L"true" : L"false");
#endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandColor", "Randomizer not initialized");
        return MyColor();  // Return default white color
    }

    try {
        // Generate random color components
        uint8_t red = GetRandColorComponent();
        uint8_t green = GetRandColorComponent();
        uint8_t blue = GetRandColorComponent();
        uint8_t alpha = includeAlpha ? GetRandColorComponent() : 255;

        MyColor randomColor(red, green, blue, alpha);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandColor generated: R=%d, G=%d, B=%d, A=%d",
            red, green, blue, alpha);
#endif

        return randomColor;
    }
    catch (const std::exception& e) {
        LogError("GetRandColor", "Exception during color generation: " + std::string(e.what()));
        return MyColor();  // Return default white color on error
    }
}

// Generate random 2D vector within specified bounds
Vector2 MyRandomizer::GetRandVector2(const Vector2& minBounds, const Vector2& maxBounds) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector2 called - Min: (%.3f, %.3f), Max: (%.3f, %.3f)",
        minBounds.x, minBounds.y, maxBounds.x, maxBounds.y);
#endif

    // Validate bounds
    if (!ValidateVector2Bounds(minBounds, maxBounds)) {
        LogError("GetRandVector2", "Invalid bounds parameters");
        return Vector2();  // Return default zero vector
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandVector2", "Randomizer not initialized");
        return Vector2();  // Return default zero vector
    }

    try {
        // Generate random x and y components within bounds
        float randomX = GetRandNum(minBounds.x, maxBounds.x);
        float randomY = GetRandNum(minBounds.y, maxBounds.y);

        Vector2 randomVector(randomX, randomY);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector2 generated: (%.3f, %.3f)", randomX, randomY);
#endif

        return randomVector;
    }
    catch (const std::exception& e) {
        LogError("GetRandVector2", "Exception during Vector2 generation: " + std::string(e.what()));
        return Vector2();  // Return default zero vector on error
    }
}

// Generate random 2D vector within circle radius
Vector2 MyRandomizer::GetRandVector2InCircle(float radius, const Vector2& center) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector2InCircle called - Radius: %.3f, Center: (%.3f, %.3f)",
        radius, center.x, center.y);
#endif

    // Validate radius
    if (radius <= 0.0f) {
        LogError("GetRandVector2InCircle", "Invalid radius (must be > 0)");
        return center;  // Return center point on error
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandVector2InCircle", "Randomizer not initialized");
        return center;  // Return center point on error
    }

    try {
        // Generate random angle and distance for uniform distribution within circle
        float angle = GetRandNum(0.0f, MYRANDOMIZER_TWO_PI);
        float distance = std::sqrt(GetRandNum(0.0f, 1.0f)) * radius;  // Square root for uniform distribution

        // Convert polar coordinates to Cartesian
        float randomX = center.x + distance * std::cos(angle);
        float randomY = center.y + distance * std::sin(angle);

        Vector2 randomVector(randomX, randomY);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector2InCircle generated: (%.3f, %.3f), Angle: %.3f, Distance: %.3f",
            randomX, randomY, angle, distance);
#endif

        return randomVector;
    }
    catch (const std::exception& e) {
        LogError("GetRandVector2InCircle", "Exception during Vector2 circle generation: " + std::string(e.what()));
        return center;  // Return center point on error
    }
}

#if defined(__USE_OPENGL__)
// Generate random 3D vector within specified bounds
Vector3 MyRandomizer::GetRandVector3(const Vector3& minBounds, const Vector3& maxBounds) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector3 called - Min: (%.3f, %.3f, %.3f), Max: (%.3f, %.3f, %.3f)",
        minBounds.x, minBounds.y, minBounds.z, maxBounds.x, maxBounds.y, maxBounds.z);
#endif

    // Validate bounds
    if (!ValidateVector3Bounds(minBounds, maxBounds)) {
        LogError("GetRandVector3", "Invalid bounds parameters");
        return Vector3();  // Return default zero vector
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandVector3", "Randomizer not initialized");
        return Vector3();  // Return default zero vector
    }

    try {
        // Generate random x, y, and z components within bounds
        float randomX = GetRandNum(minBounds.x, maxBounds.x);
        float randomY = GetRandNum(minBounds.y, maxBounds.y);
        float randomZ = GetRandNum(minBounds.z, maxBounds.z);

        Vector3 randomVector(randomX, randomY, randomZ);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector3 generated: (%.3f, %.3f, %.3f)", randomX, randomY, randomZ);
#endif

        return randomVector;
    }
    catch (const std::exception& e) {
        LogError("GetRandVector3", "Exception during Vector3 generation: " + std::string(e.what()));
        return Vector3();  // Return default zero vector on error
    }
}

// Generate random 3D vector within sphere radius
Vector3 MyRandomizer::GetRandVector3InSphere(float radius, const Vector3& center) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector3InSphere called - Radius: %.3f, Center: (%.3f, %.3f, %.3f)",
        radius, center.x, center.y, center.z);
#endif

    // Validate radius
    if (radius <= 0.0f) {
        LogError("GetRandVector3InSphere", "Invalid radius (must be > 0)");
        return center;  // Return center point on error
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandVector3InSphere", "Randomizer not initialized");
        return center;  // Return center point on error
    }

    try {
        // Generate random point within sphere using rejection sampling for uniform distribution
        Vector3 randomVector;
        do {
            randomVector.x = GetRandNum(-1.0f, 1.0f);
            randomVector.y = GetRandNum(-1.0f, 1.0f);
            randomVector.z = GetRandNum(-1.0f, 1.0f);
        } while (randomVector.Magnitude() > 1.0f);  // Reject points outside unit sphere

        // Scale to desired radius and translate to center
        randomVector = randomVector * radius + center;

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandVector3InSphere generated: (%.3f, %.3f, %.3f)",
            randomVector.x, randomVector.y, randomVector.z);
#endif

        return randomVector;
    }
    catch (const std::exception& e) {
        LogError("GetRandVector3InSphere", "Exception during Vector3 sphere generation: " + std::string(e.what()));
        return center;  // Return center point on error
    }
}
#endif

// Generate random string from character set
std::string MyRandomizer::GetRandString(int length, const std::string& characterSet) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandString called - Length: %d, CharSet Size: %zu",
        length, characterSet.size());
#endif

    // Validate parameters
    if (length <= 0 || length > MYRANDOMIZER_MAX_STRING_LENGTH) {
        LogError("GetRandString", "Invalid string length");
        return "";
    }

    if (characterSet.empty()) {
        LogError("GetRandString", "Empty character set provided");
        return "";
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandString", "Randomizer not initialized");
        return "";
    }

    try {
        std::string randomString;
        randomString.reserve(length);  // Reserve space for efficiency

        // Generate random characters from character set
        for (int i = 0; i < length; ++i) {
            size_t randomIndex = static_cast<size_t>(GetRandNum(0, static_cast<int>(characterSet.size() - 1)));
            randomString += characterSet[randomIndex];
        }

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandString generated: %S", randomString.c_str());
#endif

        return randomString;
    }
    catch (const std::exception& e) {
        LogError("GetRandString", "Exception during string generation: " + std::string(e.what()));
        return "";
    }
}

// Generate shuffled sequence of numbers
std::vector<int> MyRandomizer::GetShuffledSequence(int start, int end) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetShuffledSequence called - Start: %d, End: %d", start, end);
#endif

    std::vector<int> sequence;

    // Validate range
    if (!ValidateIntegerRange(start, end)) {
        LogError("GetShuffledSequence", "Invalid range parameters");
        return sequence;  // Return empty vector
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetShuffledSequence", "Randomizer not initialized");
        return sequence;  // Return empty vector
    }

    try {
        // Create sequential numbers
        int rangeSize = end - start + 1;
        sequence.reserve(rangeSize);

        for (int i = start; i <= end; ++i) {
            sequence.push_back(i);
        }

        // Shuffle the sequence using Fisher-Yates algorithm
        for (int i = rangeSize - 1; i > 0; --i) {
            int randomIndex = GetRandNum(0, i);
            std::swap(sequence[i], sequence[randomIndex]);
        }

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetShuffledSequence generated %d numbers", rangeSize);
#endif

        return sequence;
    }
    catch (const std::exception& e) {
        LogError("GetShuffledSequence", "Exception during sequence shuffling: " + std::string(e.what()));
        return sequence;  // Return empty vector on error
    }
}

// Generate random number with triangular distribution
float MyRandomizer::GetRandTriangular(float min, float max, float mode) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandTriangular called - Min: %.3f, Max: %.3f, Mode: %.3f",
        min, max, mode);
#endif

    // Validate parameters
    if (!ValidateTriangularParams(min, max, mode)) {
        LogError("GetRandTriangular", "Invalid triangular distribution parameters");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandTriangular", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Generate random value using triangular distribution
        float u = GetRandNum(0.0f, 1.0f);
        float fc = (mode - min) / (max - min);  // Cumulative probability at mode

        float result;
        if (u < fc) {
            // Lower triangle
            result = min + std::sqrt(u * (max - min) * (mode - min));
        }
        else {
            // Upper triangle
            result = max - std::sqrt((1.0f - u) * (max - min) * (max - mode));
        }

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandTriangular generated: %.3f", result);
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandTriangular", "Exception during triangular distribution generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random number with bias towards center
float MyRandomizer::GetRandBiasedToCenter(float min, float max, float bias) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandBiasedToCenter called - Min: %.3f, Max: %.3f, Bias: %.3f",
        min, max, bias);
#endif

    // Validate range
    if (!ValidateFloatRange(min, max)) {
        LogError("GetRandBiasedToCenter", "Invalid range parameters");
        return 0.0f;
    }

    if (bias <= 0.0f) {
        LogError("GetRandBiasedToCenter", "Invalid bias parameter (must be > 0)");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandBiasedToCenter", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Generate biased random number using power distribution
        float u = GetRandNum(0.0f, 1.0f);

        // Apply bias towards center using symmetric power function
        float biasedU;
        if (u < 0.5f) {
            biasedU = 0.5f * std::pow(2.0f * u, 1.0f / bias);
        }
        else {
            biasedU = 1.0f - 0.5f * std::pow(2.0f * (1.0f - u), 1.0f / bias);
        }

        // Map to desired range
        float result = min + biasedU * (max - min);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandBiasedToCenter generated: %.3f", result);
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandBiasedToCenter", "Exception during center-biased generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random number with bias towards edges
float MyRandomizer::GetRandBiasedToEdges(float min, float max, float bias) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandBiasedToEdges called - Min: %.3f, Max: %.3f, Bias: %.3f",
        min, max, bias);
#endif

    // Validate range
    if (!ValidateFloatRange(min, max)) {
        LogError("GetRandBiasedToEdges", "Invalid range parameters");
        return 0.0f;
    }

    if (bias <= 0.0f) {
        LogError("GetRandBiasedToEdges", "Invalid bias parameter (must be > 0)");
        return 0.0f;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandBiasedToEdges", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        // Generate biased random number using inverted power distribution
        float u = GetRandNum(0.0f, 1.0f);

        // Apply bias towards edges using inverted symmetric power function
        float biasedU;
        if (u < 0.5f) {
            biasedU = 0.5f * (1.0f - std::pow(1.0f - 2.0f * u, 1.0f / bias));
        }
        else {
            biasedU = 0.5f + 0.5f * (1.0f - std::pow(2.0f * (1.0f - u), 1.0f / bias));
        }

        // Map to desired range
        float result = min + biasedU * (max - min);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandBiasedToEdges generated: %.3f", result);
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandBiasedToEdges", "Exception during edge-biased generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random angle in radians
float MyRandomizer::GetRandAngleRadians() {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandAngleRadians called");
#endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandAngleRadians", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        float angle = GetRandNum(0.0f, MYRANDOMIZER_TWO_PI);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandAngleRadians generated: %.3f", angle);
#endif

        return angle;
    }
    catch (const std::exception& e) {
        LogError("GetRandAngleRadians", "Exception during angle generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random angle in degrees
float MyRandomizer::GetRandAngleDegrees() {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandAngleDegrees called");
#endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandAngleDegrees", "Randomizer not initialized");
        return 0.0f;
    }

    try {
        float angle = GetRandNum(0.0f, 360.0f);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandAngleDegrees generated: %.3f", angle);
#endif

        return angle;
    }
    catch (const std::exception& e) {
        LogError("GetRandAngleDegrees", "Exception during angle generation: " + std::string(e.what()));
        return 0.0f;
    }
}

// Generate random rotation (0-360 degrees)
float MyRandomizer::GetRandRotation() {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandRotation called");
#endif

    // This is an alias for GetRandAngleDegrees for semantic clarity
    return GetRandAngleDegrees();
}

// Coin flip simulation with configurable probability
bool MyRandomizer::CoinFlip(float headsProb) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CoinFlip called with heads probability: %.3f", headsProb);
#endif

    // This is an alias for GetRandBool for semantic clarity
    bool result = GetRandBool(headsProb);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CoinFlip result: %s", result ? L"Heads" : L"Tails");
#endif

    return result;
}

// Generate random seed value for other systems
uint64_t MyRandomizer::GetRandSeed() {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandSeed called");
#endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandSeed", "Randomizer not initialized");
        return 0;
    }

    try {
        // Generate high-quality seed using current engine and additional entropy
        uint64_t seed = GenerateHighQualitySeed();

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandSeed generated: %llu", seed);
#endif

        return seed;
    }
    catch (const std::exception& e) {
        LogError("GetRandSeed", "Exception during seed generation: " + std::string(e.what()));
        return 0;
    }
}

// Generate random color component (0-255) - MISSING IMPLEMENTATION
uint8_t MyRandomizer::GetRandColorComponent() {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandColorComponent() called");
#endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandColorComponent", "Randomizer not initialized");
        return 0;
    }

    try {
        // Generate random color component value (0-255)
        int colorValue = GetRandNum(0, 255);
        uint8_t result = static_cast<uint8_t>(colorValue);

#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandColorComponent generated: %d", static_cast<int>(result));
#endif

        return result;
    }
    catch (const std::exception& e) {
        LogError("GetRandColorComponent", "Exception during color component generation: " + std::string(e.what()));
        return 0;
    }
}

// Validate Vector2 bounds parameters - MISSING IMPLEMENTATION
bool MyRandomizer::ValidateVector2Bounds(const Vector2& minBounds, const Vector2& maxBounds) const {
    // Check if maximum bounds are greater than minimum bounds for X coordinate
    if (maxBounds.x <= minBounds.x) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid Vector2 X bounds: max (%.3f) <= min (%.3f)", maxBounds.x, minBounds.x);
#endif
        return false;
    }

    // Check if maximum bounds are greater than minimum bounds for Y coordinate
    if (maxBounds.y <= minBounds.y) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid Vector2 Y bounds: max (%.3f) <= min (%.3f)", maxBounds.y, minBounds.y);
#endif
        return false;
    }

    return true;
}

#if defined(__USE_OPENGL__)
// Validate Vector3 bounds parameters - MISSING IMPLEMENTATION
bool MyRandomizer::ValidateVector3Bounds(const Vector3& minBounds, const Vector3& maxBounds) const {
    // Check if maximum bounds are greater than minimum bounds for X coordinate
    if (maxBounds.x <= minBounds.x) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid Vector3 X bounds: max (%.3f) <= min (%.3f)", maxBounds.x, minBounds.x);
#endif
        return false;
    }

    // Check if maximum bounds are greater than minimum bounds for Y coordinate
    if (maxBounds.y <= minBounds.y) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid Vector3 Y bounds: max (%.3f) <= min (%.3f)", maxBounds.y, minBounds.y);
#endif
        return false;
    }

    // Check if maximum bounds are greater than minimum bounds for Z coordinate
    if (maxBounds.z <= minBounds.z) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid Vector3 Z bounds: max (%.3f) <= min (%.3f)", maxBounds.z, minBounds.z);
#endif
        return false;
    }

    return true;
}
#endif

// Validate normal distribution parameters - MISSING IMPLEMENTATION
bool MyRandomizer::ValidateNormalDistributionParams(float mean, float standardDeviation) const {
    // Check if standard deviation is positive and above minimum threshold
    if (standardDeviation <= 0.0f || standardDeviation < MYRANDOMIZER_MIN_STANDARD_DEV) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid standard deviation: %.3f (must be > %.3f)",
            standardDeviation, MYRANDOMIZER_MIN_STANDARD_DEV);
#endif
        return false;
    }

    // Check if mean is finite (not NaN or infinity)
    if (!std::isfinite(mean)) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid mean: %.3f (must be finite)", mean);
#endif
        return false;
    }

    return true;
}

// Validate exponential distribution parameter - MISSING IMPLEMENTATION
bool MyRandomizer::ValidateExponentialParam(float lambda) const {
    // Check if lambda is positive and above minimum threshold
    if (lambda <= 0.0f || lambda < MYRANDOMIZER_MIN_LAMBDA) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid lambda: %.3f (must be > %.3f)",
            lambda, MYRANDOMIZER_MIN_LAMBDA);
#endif
        return false;
    }

    return true;
}

// Validate triangular distribution parameters - MISSING IMPLEMENTATION
bool MyRandomizer::ValidateTriangularParams(float min, float max, float mode) const {
    // Check if range is valid
    if (!ValidateFloatRange(min, max)) {
        return false;  // Error already logged in ValidateFloatRange
    }

    // Check if mode is within range
    if (mode < min || mode > max) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid mode: %.3f (must be between %.3f and %.3f)",
            mode, min, max);
#endif
        return false;
    }

    return true;
}

// Validate weight vector - MISSING IMPLEMENTATION
bool MyRandomizer::ValidateWeights(const std::vector<float>& weights) const {
    // Check if weights vector is empty
    if (weights.empty()) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Empty weights vector");
#endif
        return false;
    }

    float totalWeight = 0.0f;
    bool hasPositiveWeight = false;

    // Check each weight for validity
    for (size_t i = 0; i < weights.size(); ++i) {
        const float& weight = weights[i];

        // Check if weight is finite
        if (!std::isfinite(weight)) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid weight at index %zu: %.3f (must be finite)", i, weight);
#endif
            return false;
        }

        // Check if weight is non-negative
        if (weight < 0.0f) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Negative weight at index %zu: %.3f", i, weight);
#endif
            return false;
        }

        // Accumulate total weight and check for positive weights
        totalWeight += weight;
        if (weight > 0.0f) {
            hasPositiveWeight = true;
        }
    }

    // Check if at least one weight is positive
    if (!hasPositiveWeight) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"All weights are zero - no valid selection possible");
#endif
        return false;
    }

    // Check if total weight is reasonable (not too small)
    if (totalWeight < 0.001f) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Very small total weight: %.6f - may cause precision issues", totalWeight);
#endif
    }

    return true;
}

// Select index based on weights - MISSING IMPLEMENTATION
int MyRandomizer::SelectWeightedIndex(const std::vector<float>& weights) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelectWeightedIndex called with %zu weights", weights.size());
#endif

    // Validate weights
    if (!ValidateWeights(weights)) {
        LogError("SelectWeightedIndex", "Invalid weights vector");
        return -1;
    }

    try {
        // Calculate total weight
        float totalWeight = 0.0f;
        for (const float& weight : weights) {
            totalWeight += weight;
        }

        // Generate random value within total weight range
        float randomValue = GetRandNum(0.0f, totalWeight);

        // Find the selected index using cumulative weight distribution
        float cumulativeWeight = 0.0f;
        for (size_t i = 0; i < weights.size(); ++i) {
            cumulativeWeight += weights[i];
            if (randomValue <= cumulativeWeight) {
#if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SelectWeightedIndex selected index %zu with weight %.3f",
                    i, weights[i]);
#endif
                return static_cast<int>(i);
            }
        }

        // Fallback - should not reach here with valid weights
        LogWarning("SelectWeightedIndex", "Fallback to last index due to floating point precision");
        return static_cast<int>(weights.size() - 1);
    }
    catch (const std::exception& e) {
        LogError("SelectWeightedIndex", "Exception during weighted selection: " + std::string(e.what()));
        return -1;
    }
}

// Calculate spawn delay with urgency factor - MISSING IMPLEMENTATION
float MyRandomizer::CalculateSpawnUrgency(float baseDelay, float urgencyFactor) const {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CalculateSpawnUrgency called - Base: %.3f, Urgency: %.3f",
            baseDelay, urgencyFactor);
    #endif

    // Validate parameters
    if (baseDelay <= 0.0f) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid base delay: %.3f (must be > 0)", baseDelay);
        #endif
        return 1.0f;  // Return minimum delay as fallback
    }

    if (urgencyFactor <= 0.0f) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid urgency factor: %.3f (must be > 0)", urgencyFactor);
        #endif
        return baseDelay;  // Return base delay as fallback
    }

    try {
        // Calculate adjusted delay based on urgency factor
        // Higher urgency factor reduces delay, lower urgency factor increases delay
        float adjustedDelay = baseDelay / urgencyFactor;

        // Ensure minimum delay of 0.1 seconds
        float finalDelay = std::max(adjustedDelay, 0.1f);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CalculateSpawnUrgency result: %.3f", finalDelay);
        #endif

        return finalDelay;
    }
    catch (const std::exception& e) {
        // Fallback to base delay on any error
        return baseDelay;
    }
}

//==============================================================================
// Gaming-Specific Random Generation Methods - MISSING IMPLEMENTATIONS
//==============================================================================

// Generate random stat roll for RPG character creation - MISSING IMPLEMENTATION
int MyRandomizer::GetRandStatRoll(int numberOfDice, int sidesPerDie, bool dropLowest) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandStatRoll called - Dice: %d, Sides: %d, Drop Lowest: %s",
            numberOfDice, sidesPerDie, dropLowest ? L"true" : L"false");
    #endif

    // Validate parameters
    if (numberOfDice <= 0 || numberOfDice > MYRANDOMIZER_MAX_DICE_COUNT) {
        LogError("GetRandStatRoll", "Invalid number of dice");
        return 0;
    }

    if (sidesPerDie <= 0 || sidesPerDie > MYRANDOMIZER_MAX_DICE_SIDES) {
        LogError("GetRandStatRoll", "Invalid number of sides per die");
        return 0;
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandStatRoll", "Randomizer not initialized");
        return 0;
    }

    try {
        std::vector<int> rolls;
        rolls.reserve(numberOfDice);

        // Roll all dice
        for (int i = 0; i < numberOfDice; ++i) {
            int roll = GetRandNum(1, sidesPerDie);
            rolls.push_back(roll);

            #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Die #%d rolled: %d", i + 1, roll);
            #endif
        }

        // Sort rolls if we need to drop the lowest
        if (dropLowest && numberOfDice > 1) {
            std::sort(rolls.begin(), rolls.end());
            rolls.erase(rolls.begin());  // Remove the lowest roll
        }

        // Sum remaining rolls
        int totalRoll = 0;
        for (int roll : rolls) {
            totalRoll += roll;
        }

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandStatRoll total result: %d", totalRoll);
        #endif

        return totalRoll;
    }
    catch (const std::exception& e) {
        LogError("GetRandStatRoll", "Exception during stat roll: " + std::string(e.what()));
        return 0;
    }
}

// Generate random encounter chance - MISSING IMPLEMENTATION
bool MyRandomizer::CheckRandomEncounter(float encounterRate) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CheckRandomEncounter called with rate: %.3f", encounterRate);
    #endif

    // Validate encounter rate (should be between 0.0 and 1.0)
    if (encounterRate < 0.0f || encounterRate > 1.0f) {
        LogError("CheckRandomEncounter", "Invalid encounter rate (must be 0.0-1.0)");
        return false;
    }

    // Use existing GetRandBool method for encounter check
    bool encounterOccurs = GetRandBool(encounterRate);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CheckRandomEncounter result: %s",
            encounterOccurs ? L"Encounter!" : L"No encounter");
    #endif

    return encounterOccurs;
}

// Generate random loot drop based on rarity tables - MISSING IMPLEMENTATION
int MyRandomizer::GetRandLootDrop(const std::vector<float>& rarityThresholds) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandLootDrop called with %zu rarity thresholds",
            rarityThresholds.size());
    #endif

    // Validate rarity thresholds
    if (rarityThresholds.empty()) {
        LogError("GetRandLootDrop", "Empty rarity thresholds vector");
        return -1;  // No loot drop
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandLootDrop", "Randomizer not initialized");
        return -1;
    }

    try {
        // Generate random value for loot drop check
        float dropChance = GetRandPercentage();

        // Check each rarity threshold (assuming sorted from common to rare)
        for (size_t i = 0; i < rarityThresholds.size(); ++i) {
            if (dropChance <= rarityThresholds[i]) {
                #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandLootDrop selected rarity tier %zu with chance %.3f",
                        i, dropChance);
                #endif
                return static_cast<int>(i);
            }
        }

        // No loot drop occurred
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandLootDrop - No loot drop (chance: %.3f)", dropChance);
        #endif
        return -1;
    }
    catch (const std::exception& e) {
        LogError("GetRandLootDrop", "Exception during loot drop calculation: " + std::string(e.what()));
        return -1;
    }
}

// Generate random spawn timing - MISSING IMPLEMENTATION
float MyRandomizer::GetRandSpawnDelay(float minDelay, float maxDelay, float urgencyFactor) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandSpawnDelay called - Min: %.3f, Max: %.3f, Urgency: %.3f",
            minDelay, maxDelay, urgencyFactor);
    #endif

    // Validate delay range
    if (!ValidateFloatRange(minDelay, maxDelay)) {
        LogError("GetRandSpawnDelay", "Invalid delay range");
        return 1.0f;  // Return default delay
    }

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandSpawnDelay", "Randomizer not initialized");
        return 1.0f;
    }

    try {
        // Generate base random delay within range
        float baseDelay = GetRandNum(minDelay, maxDelay);

        // Apply urgency factor to adjust delay
        float finalDelay = CalculateSpawnUrgency(baseDelay, urgencyFactor);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandSpawnDelay result: %.3f", finalDelay);
        #endif

        return finalDelay;
    }
    catch (const std::exception& e) {
        LogError("GetRandSpawnDelay", "Exception during spawn delay calculation: " + std::string(e.what()));
        return 1.0f;
    }
}

// Generate random movement direction (8-directional) - MISSING IMPLEMENTATION
int MyRandomizer::GetRandDirection8() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandDirection8 called");
    #endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandDirection8", "Randomizer not initialized");
        return 0;  // Default to first direction
    }

    try {
        // Generate random direction from 0-7 (8 directions)
        // 0=North, 1=Northeast, 2=East, 3=Southeast, 4=South, 5=Southwest, 6=West, 7=Northwest
        int direction = GetRandNum(0, 7);

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandDirection8 selected: %d", direction);
        #endif

        return direction;
    }
    catch (const std::exception& e) {
        LogError("GetRandDirection8", "Exception during direction generation: " + std::string(e.what()));
        return 0;
    }
}

// Generate random movement direction (4-directional) - MISSING IMPLEMENTATION
int MyRandomizer::GetRandDirection4() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GetRandDirection4 called");
    #endif

    // Ensure randomizer is initialized
    if (!m_isInitialized) {
        LogError("GetRandDirection4", "Randomizer not initialized");
        return 0;  // Default to first direction
    }

    try {
        // Generate random direction from 0-3 (4 directions)
        // 0=North, 1=East, 2=South, 3=West
        int direction = GetRandNum(0, 3);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandDirection4 selected: %d", direction);
    #endif

        return direction;
    }
    catch (const std::exception& e) {
        LogError("GetRandDirection4", "Exception during direction generation: " + std::string(e.what()));
        return 0;
    }
}

// Generate random AI behavior selection - MISSING IMPLEMENTATION
int MyRandomizer::GetRandAIBehavior(const std::vector<float>& behaviorWeights) {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandAIBehavior called with %zu behavior weights",
            behaviorWeights.size());
    #endif

    // Use existing weighted selection method
    int selectedBehavior = SelectWeightedIndex(behaviorWeights);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        if (selectedBehavior >= 0) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetRandAIBehavior selected behavior: %d", selectedBehavior);
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"GetRandAIBehavior failed to select behavior");
        }
    #endif

    return selectedBehavior;
}

#pragma warning(pop)
