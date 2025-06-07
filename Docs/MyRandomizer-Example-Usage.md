# MyRandomizer Example Usage

## Overview

This document provides comprehensive examples of how to use the MyRandomizer class functionality. The MyRandomizer class offers advanced random number generation capabilities including integer/float generation, unique selection, target attempts, and percentage generation specifically designed for gaming applications.

## Table of Contents

- [Basic Setup](#basic-setup)
- [Basic Integer Generation](#basic-integer-generation)
- [Basic Float Generation](#basic-float-generation)
- [Unique Integer Selection](#unique-integer-selection)
- [Unique Float Selection](#unique-float-selection)
- [Target Number Attempts](#target-number-attempts)
- [Percentage Generation](#percentage-generation)
- [Tracker Management](#tracker-management)
- [Real-World Gaming Scenarios](#real-world-gaming-scenarios)
- [Complete Demo Implementation](#complete-demo-implementation)

## Basic Setup

```cpp
// Include required headers
#include "Includes.h"
#include "MyRandomizer.h"
#include "Debug.h"

// External reference declarations
extern Debug debug;

// Global MyRandomizer instance for demonstration
MyRandomizer globalRandomizer;

// Initialize the randomizer before use
bool InitializeRandomizer() {
    if (!globalRandomizer.Initialize()) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize MyRandomizer");
        #endif
        return false;
    }
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer initialized successfully");
    #endif
    
    return true;
}
```

## Basic Integer Generation

### Simple Integer Range Generation

```cpp
void DemoBasicIntegerGeneration() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Basic Integer Random Number Generation Demo ===");
    #endif

    // Generate 10 random integers between 1 and 100
    for (int i = 0; i < 10; ++i) {
        int randomNumber = globalRandomizer.GetRandNum(1, 100);
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Random integer #%d: %d", i + 1, randomNumber);
        #endif
    }

    // Generate random numbers in different ranges
    int smallRange = globalRandomizer.GetRandNum(1, 5);                 // Small range (1-5)
    int mediumRange = globalRandomizer.GetRandNum(50, 150);             // Medium range (50-150)
    int largeRange = globalRandomizer.GetRandNum(1000, 9999);           // Large range (1000-9999)

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Small range (1-5): %d", smallRange);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Medium range (50-150): %d", mediumRange);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Large range (1000-9999): %d", largeRange);
    #endif
}
```

## Basic Float Generation

### Simple Float Range Generation

```cpp
void DemoBasicFloatGeneration() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Basic Float Random Number Generation Demo ===");
    #endif

    // Generate 10 random floats between 0.1 and 10.0
    for (int i = 0; i < 10; ++i) {
        float randomNumber = globalRandomizer.GetRandNum(0.1f, 10.0f);
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Random float #%d: %.3f", i + 1, randomNumber);
        #endif
    }

    // Generate random floats in different ranges
    float preciseRange = globalRandomizer.GetRandNum(0.001f, 0.999f);   // Precise range (0.001-0.999)
    float normalRange = globalRandomizer.GetRandNum(1.0f, 100.0f);      // Normal range (1.0-100.0)
    float largeRange = globalRandomizer.GetRandNum(100.0f, 10000.0f);   // Large range (100.0-10000.0)

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Precise range (0.001-0.999): %.3f", preciseRange);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Normal range (1.0-100.0): %.3f", normalRange);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Large range (100.0-10000.0): %.3f", largeRange);
    #endif
}
```

## Unique Integer Selection

### Single Unique Number Selection

```cpp
void DemoUniqueIntegerSelection() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Unique Integer Number Selection Demo ===");
    #endif

    // Select unique numbers from a small range (1-5) to show cycling behavior
    std::vector<int> uniqueNumbers;
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Selecting unique numbers from range 1-5 (should cycle after 5 selections):");
    #endif

    // Select 8 unique numbers to demonstrate refresh behavior
    for (int i = 0; i < 8; ++i) {
        int uniqueNumber = globalRandomizer.SelUniqueRandNum(1, 5);
        uniqueNumbers.push_back(uniqueNumber);
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Unique selection #%d: %d", i + 1, uniqueNumber);
        #endif
    }

    // Clear the unique tracker for this range
    globalRandomizer.ClearUniqueTracker(1, 5);
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Cleared unique tracker for range 1-5");
    #endif
}
```

### Bulk Unique Number Generation

```cpp
void DemoBulkUniqueGeneration() {
    // Demonstrate bulk unique number generation
    std::vector<int> bulkUniqueNumbers = globalRandomizer.GetListOfUniqueRandNums(10, 20, 5);
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Bulk unique numbers from range 10-20 (5 numbers):");
        for (size_t i = 0; i < bulkUniqueNumbers.size(); ++i) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Bulk unique #%zu: %d", i + 1, bulkUniqueNumbers[i]);
        }
    #endif
}
```

## Unique Float Selection

### Single Unique Float Selection

```cpp
void DemoUniqueFloatSelection() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Unique Float Number Selection Demo ===");
    #endif

    // Select unique float numbers from a small range to show tracking behavior
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Selecting unique float numbers from range 1.0-3.0:");
    #endif

    // Select 5 unique float numbers
    for (int i = 0; i < 5; ++i) {
        float uniqueFloat = globalRandomizer.SelUniqueRandNum(1.0f, 3.0f);
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Unique float #%d: %.3f", i + 1, uniqueFloat);
        #endif
    }
}
```

### Bulk Unique Float Generation

```cpp
void DemoBulkUniqueFloatGeneration() {
    // Demonstrate bulk unique float number generation
    std::vector<float> bulkUniqueFloats = globalRandomizer.GetListOfUniqueRandNums(5.0f, 8.0f, 4);
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Bulk unique floats from range 5.0-8.0 (4 numbers):");
        for (size_t i = 0; i < bulkUniqueFloats.size(); ++i) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Bulk unique float #%zu: %.3f", i + 1, bulkUniqueFloats[i]);
        }
    #endif
}
```

## Target Number Attempts

### Single Target Attempt with Difficulty

```cpp
void DemoTargetNumberAttempts() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Target Number Attempts Demo ===");
    #endif

    // Test different difficulty levels for integer targets
    int targetNumber = 50;
    float difficulties[] = { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f };          // Various difficulty levels
    const char* difficultyNames[] = { "Very Easy", "Easy", "Medium", "Hard", "Very Hard" };

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Testing target number %d with different difficulties:", targetNumber);
    #endif

    // Test each difficulty level with single attempts
    for (int i = 0; i < 5; ++i) {
        int result = globalRandomizer.TryTargetNumber(1, 100, targetNumber, difficulties[i]);
        bool hitTarget = (result == targetNumber);
        
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"%S difficulty (%.1f): Result = %d, Hit Target = %s",
                difficultyNames[i], difficulties[i], result, hitTarget ? L"YES" : L"NO");
        #endif
    }
}
```

### Multiple Target Attempts

```cpp
void DemoMultipleTargetAttempts() {
    int targetNumber = 50;
    
    // Demonstrate multiple attempts for target number
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Testing multiple attempts to hit target number:");
    #endif

    int attemptsMade = 0;
    int multiAttemptResult = globalRandomizer.TryAttemptTargetNum(1, 100, 10, targetNumber, 0.8f, attemptsMade);
    bool multiHitTarget = (multiAttemptResult == targetNumber);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Multiple attempts (max 10, difficulty 0.8): Result = %d, Attempts = %d, Success = %s",
            multiAttemptResult, attemptsMade, multiHitTarget ? L"YES" : L"NO");
    #endif
}
```

### Float Target Attempts

```cpp
void DemoFloatTargetAttempts() {
    // Test with float target numbers
    float floatTarget = 7.5f;
    float floatResult = globalRandomizer.TryTargetNumber(1.0f, 10.0f, floatTarget, 0.4f);
    bool hitFloatTarget = (std::abs(floatResult - floatTarget) < 0.001f);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Float target %.1f (difficulty 0.4): Result = %.3f, Hit Target = %s",
            floatTarget, floatResult, hitFloatTarget ? L"YES" : L"NO");
    #endif
}
```

## Percentage Generation

### Basic Percentage Generation

```cpp
void DemoPercentageGeneration() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Random Percentage Generation Demo ===");
    #endif

    // Generate 10 random percentages
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Generating 10 random percentages (0.001 to 1.0):");
    #endif

    for (int i = 0; i < 10; ++i) {
        float percentage = globalRandomizer.GetRandPercentage();
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Percentage #%d: %.3f (%.1f%%)", i + 1, percentage, percentage * 100.0f);
        #endif
    }
}
```

## Tracker Management

### Basic Tracker Operations

```cpp
void DemoTrackerManagement() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Tracker Management Demo ===");
    #endif

    // Create several unique trackers by using unique selection
    globalRandomizer.SelUniqueRandNum(1, 10);                          // Creates tracker for range 1-10
    globalRandomizer.SelUniqueRandNum(100, 200);                       // Creates tracker for range 100-200
    globalRandomizer.SelUniqueRandNum(5.0f, 15.0f);                    // Creates tracker for float range 5.0-15.0

    // Check tracker statistics
    size_t activeTrackers = globalRandomizer.GetActiveTrackerCount();
    bool hasIntTracker = globalRandomizer.HasActiveTracker(1, 10);
    bool hasFloatTracker = globalRandomizer.HasActiveTracker(5.0f, 15.0f);
    bool hasNonExistentTracker = globalRandomizer.HasActiveTracker(500, 600);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Active trackers count: %zu", activeTrackers);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Has integer tracker (1-10): %s", hasIntTracker ? L"YES" : L"NO");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Has float tracker (5.0-15.0): %s", hasFloatTracker ? L"YES" : L"NO");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Has non-existent tracker (500-600): %s", hasNonExistentTracker ? L"YES" : L"NO");
    #endif
}
```

### Tracker Cleanup Operations

```cpp
void DemoTrackerCleanup() {
    // Clear specific trackers
    globalRandomizer.ClearUniqueTracker(1, 10);                        // Clear integer tracker
    globalRandomizer.ClearUniqueTracker(5.0f, 15.0f);                  // Clear float tracker

    size_t trackersAfterClear = globalRandomizer.GetActiveTrackerCount();
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Active trackers after clearing two: %zu", trackersAfterClear);
    #endif

    // Clear all remaining trackers
    globalRandomizer.ClearAllUniqueTrackers();
    size_t trackersAfterClearAll = globalRandomizer.GetActiveTrackerCount();
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Active trackers after clearing all: %zu", trackersAfterClearAll);
    #endif
}
```

## Real-World Gaming Scenarios

### Damage Calculation System

```cpp
void DemoDamageCalculation() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Damage Calculation Demo ===");
    #endif

    // Random damage calculation
    int baseDamage = 50;
    float damageVariance = globalRandomizer.GetRandNum(0.8f, 1.2f);     // 80% to 120% of base damage
    int finalDamage = static_cast<int>(baseDamage * damageVariance);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Damage calculation: Base = %d, Variance = %.2f, Final = %d",
            baseDamage, damageVariance, finalDamage);
    #endif
}
```

### Critical Hit System

```cpp
void DemoCriticalHitSystem() {
    // Critical hit chance
    float criticalChance = 0.15f;                                       // 15% critical hit chance
    bool isCritical = globalRandomizer.TryTargetNumber(1, 100, 85, criticalChance) == 85; // Target number approach
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Critical hit attempt (15%% chance): %s", isCritical ? L"CRITICAL!" : L"Normal hit");
    #endif
}
```

### Loot Drop System

```cpp
void DemoLootDropSystem() {
    // Loot drop with rarity
    std::vector<int> lootTable = globalRandomizer.GetListOfUniqueRandNums(1, 100, 5); // 5 unique loot items
    
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Loot drops (5 unique items):");
        for (size_t i = 0; i < lootTable.size(); ++i) {
            std::wstring rarity;
            if (lootTable[i] <= 5) rarity = L"Legendary";
            else if (lootTable[i] <= 20) rarity = L"Epic";
            else if (lootTable[i] <= 50) rarity = L"Rare";
            else rarity = L"Common";
            
            debug.logDebugMessage(LogLevel::LOG_INFO, L"  Item #%zu: Roll %d = %s", i + 1, lootTable[i], rarity.c_str());
        }
    #endif
}
```

### Enemy Spawn System

```cpp
void DemoEnemySpawnSystem() {
    // Random enemy spawn position
    Vector2 spawnPosition(
        globalRandomizer.GetRandNum(0.0f, 800.0f),                     // X coordinate (0-800)
        globalRandomizer.GetRandNum(0.0f, 600.0f)                      // Y coordinate (0-600)
    );

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Enemy spawn position: X = %.1f, Y = %.1f", spawnPosition.x, spawnPosition.y);
    #endif
}
```

### Quest Reward System

```cpp
void DemoQuestRewardSystem() {
    // Random quest reward multiplier
    float rewardMultiplier = globalRandomizer.GetRandPercentage();      // Random percentage for bonus
    int baseReward = 1000;
    int bonusReward = static_cast<int>(baseReward * rewardMultiplier);

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Quest reward: Base = %d, Multiplier = %.1f%%, Bonus = %d, Total = %d",
            baseReward, rewardMultiplier * 100.0f, bonusReward, baseReward + bonusReward);
    #endif
}
```

## Complete Demo Implementation

### Main Demo Function

```cpp
// Main function to run all MyRandomizer demonstrations
bool RunMyRandomizerDemo() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Starting MyRandomizer comprehensive demonstration");
    #endif

    // Initialize the randomizer
    if (!globalRandomizer.Initialize()) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize MyRandomizer - demo cancelled");
        #endif
        return false;
    }

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer initialized successfully");
    #endif

    try {
        // Run all demonstration functions
        DemoBasicIntegerGeneration();                                   // Basic integer random number generation
        DemoBasicFloatGeneration();                                     // Basic float random number generation
        DemoUniqueIntegerSelection();                                   // Unique integer number selection
        DemoUniqueFloatSelection();                                     // Unique float number selection
        DemoTargetNumberAttempts();                                     // Target number attempts with difficulty
        DemoPercentageGeneration();                                     // Random percentage generation
        DemoTrackerManagement();                                        // Tracker management and statistics
        DemoGamingScenarios();                                          // Real-world gaming scenarios

        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer demonstration completed successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
            std::string errorMsg = e.what();
            std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Exception during MyRandomizer demo: " + wErrorMsg);
        #endif
        return false;
    }
}
```

### Cleanup Function

```cpp
// Cleanup function to be called when demo is finished
void CleanupMyRandomizerDemo() {
    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Cleaning up MyRandomizer demonstration");
    #endif

    // Clean up the global randomizer instance
    globalRandomizer.Cleanup();

    #if defined(_DEBUG_MYRANDOMIZER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MyRandomizer demonstration cleanup completed");
    #endif
}
```

## Usage Notes

1. **Debug Output**: All examples include comprehensive debug output that can be enabled by defining `_DEBUG_MYRANDOMIZER_` in your Debug.h file.

2. **Thread Safety**: The MyRandomizer class is designed to be thread-safe for use in multithreaded gaming environments.

3. **Memory Management**: Always call `Cleanup()` when finished using the MyRandomizer to properly release resources.

4. **Performance**: The class is optimized for high-performance gaming scenarios with minimal overhead.

5. **Additional Functions**: There are many more functions available in the MyRandomizer class - see MyRandomizer.h in the public scope of the MyRandomizer class definition for a complete list.

## Error Handling

Always check the return value of `Initialize()` before using other functions:

```cpp
if (!globalRandomizer.Initialize()) {
    // Handle initialization failure
    return false;
}

// Safe to use other functions
int randomValue = globalRandomizer.GetRandNum(1, 100);
```

## Integration with Existing Code

The MyRandomizer class is designed to integrate seamlessly with existing game engines and can be used alongside other random number generation systems without conflicts.

For more advanced usage patterns and additional functionality, refer to the complete MyRandomizer.h header file documentation.