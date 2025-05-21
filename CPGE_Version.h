// ===========================================================================================
// CPGE_Version.h
// ===========================================================================================
// Purpose: 
// This header defines the versioning system for the Cross Platform Game Engine (CPGE) project.
// It provides macros and constants to track the major version, minor version, build number,
// and last build date, maintaining a full versioning history.
//
// ===========================================================================================
#pragma once
#include "Includes.h"

// ===========================================================================================
// CPGE Versioning Metadata
// ===========================================================================================

// Major version of the engine (e.g., 1 for first full release, 2 for second-generation engine)
#define CPGE_VERSION_MAJOR        0

// Minor version of the engine (e.g., 0, 1, 2... minor improvements, feature upgrades)
#define CPGE_VERSION_MINOR        1

// Total number of builds compiled for this project (incremented manually after each successful major operation)
#define CPGE_BUILD_NUMBER         1210

// Last recorded build date in ISO format (DD-MM-YYYY) You Americans, and of course, no dis-respect but, 
// please leave this as its' current format thank you!
#define CPGE_BUILD_DATE           "26-04-2025"

// ===========================================================================================
// CPGE Version String Macro
// ===========================================================================================
// Example Output: "v1.0 Build 1 (2025-04-26)"
#define CPGE_VERSION_STRING "v" + std::to_wstring(CPGE_VERSION_MAJOR) + "." + std::to_wstring(CPGE_VERSION_MINOR) + " Build " + std::to_wstring(CPGE_BUILD_NUMBER) + " (" + CPGE_BUILD_DATE + ")"

// ===========================================================================================
