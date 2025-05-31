//-------------------------------------------------------------------------------------------------
// MathPrecalculation.cpp - High-Performance Mathematical Precalculation Class Implementation
//
// This implementation provides optimized lookup tables and precalculated values for complex
// mathematical operations to eliminate expensive calculations during real-time loops.
// Designed specifically for gaming platforms where timing is critical.
//-------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "MathPrecalculation.h"
#include "Debug.h"

// External reference for global debug instance
extern Debug debug;

//==============================================================================
// Singleton Pattern Implementation
//==============================================================================
MathPrecalculation& MathPrecalculation::GetInstance()
{
    // Thread-safe singleton using C++11 static initialization guarantee
    static MathPrecalculation instance;
    return instance;
}

//==============================================================================
// Constructor and Destructor
//==============================================================================
MathPrecalculation::MathPrecalculation() :
    m_bIsInitialized(false),
    m_bHasCleanedUp(false),
    m_totalMemoryUsage(0),
    m_lookupCount(0)
{
    // Reserve memory for lookup tables to avoid reallocations during initialization
    m_trigonometricTable.reserve(TRIG_TABLE_SIZE);
    m_sqrtTable.reserve(SQRT_TABLE_SIZE);
    m_interpolationTable.reserve(INTERPOLATION_TABLE_SIZE);
    m_particleDirections.reserve(PARTICLE_ANGLE_DIVISIONS);
    m_transparencyLookup.reserve(1024);

#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Constructor called - Memory reserved for lookup tables");
#endif
}

MathPrecalculation::~MathPrecalculation()
{
    // Ensure cleanup is performed
    if (!m_bHasCleanedUp.load())
    {
        Cleanup();
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Destructor called - All resources cleaned up");
#endif
}

//==============================================================================
// Initialization Methods
//==============================================================================
bool MathPrecalculation::Initialize()
{
    // Check if already initialized to prevent double initialization
    if (m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] Already initialized - skipping");
#endif
        return true;
    }

    // Thread-safe initialization using mutex
    std::lock_guard<std::mutex> lock(m_tablesMutex);

    // Double-check pattern to ensure thread safety
    if (m_bIsInitialized.load())
    {
        return true;
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Starting initialization of lookup tables");
#endif

    try
    {
        // Initialize all lookup tables in order of dependency
        InitializeTrigonometricTables();
        InitializeInverseTrigonometricTables();
        InitializeColorConversionTables();
        InitializeInterpolationTables();
        InitializeParticleData();
        InitializeMatrixCaches();
        InitializeTextOptimizations();

        // Calculate total memory usage for debugging
        m_totalMemoryUsage.store(GetMemoryUsage());

        // Mark as successfully initialized
        m_bIsInitialized.store(true);

#if defined(_DEBUG_MATHPRECALC_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[MathPrecalculation] Initialization completed successfully - Memory usage: %zu bytes",
            m_totalMemoryUsage.load());

        // Dump detailed statistics in debug mode
        DumpTableStatistics();
#endif

        return true;
    }
    catch (const std::exception& e)
    {
        // Convert exception message to wide string for logging
        std::string errorMsg = e.what();
        std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());

        debug.logLevelMessage(LogLevel::LOG_CRITICAL,
            L"[MathPrecalculation] Initialization failed with exception: " + wErrorMsg);

        return false;
    }
    catch (...)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL,
            L"[MathPrecalculation] Initialization failed with unknown exception");

        return false;
    }
}

void MathPrecalculation::InitializeTrigonometricTables()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing trigonometric lookup tables");
#endif

    // Clear and resize the trigonometric table
    m_trigonometricTable.clear();
    m_trigonometricTable.resize(TRIG_TABLE_SIZE);

    // Precalculate trigonometric values for the entire table
    for (int i = 0; i < TRIG_TABLE_SIZE; ++i)
    {
        // Calculate angle for this table entry
        float angle = (static_cast<float>(i) / static_cast<float>(TRIG_TABLE_SIZE)) * 2.0f * XM_PI;

        // Calculate and store all trigonometric values
        TrigonometricData& data = m_trigonometricTable[i];
        data.sine = std::sin(angle);
        data.cosine = std::cos(angle);
        data.tangent = std::tan(angle);

        // Calculate cotangent with safety check for division by zero
        if (std::abs(data.tangent) > 1e-8f)
        {
            data.cotangent = 1.0f / data.tangent;
        }
        else
        {
            // Use a large value for near-zero tangent (cotangent approaches infinity)
            data.cotangent = (data.tangent >= 0.0f) ? 1e8f : -1e8f;
        }
    }

    // Initialize square root lookup table
    m_sqrtTable.clear();
    m_sqrtTable.resize(SQRT_TABLE_SIZE);

    for (int i = 0; i < SQRT_TABLE_SIZE; ++i)
    {
        // Calculate value for this table entry (range 0 to 1000)
        float value = static_cast<float>(i) / SQRT_PRECISION_FACTOR;
        m_sqrtTable[i] = std::sqrt(value);
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Trigonometric tables initialized - Sin/Cos entries: %d, Sqrt entries: %d",
        TRIG_TABLE_SIZE, SQRT_TABLE_SIZE);
#endif
}

void MathPrecalculation::InitializeInverseTrigonometricTables()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing inverse trigonometric lookup tables");
#endif

    // Clear and resize the inverse trigonometric table
    m_inverseTrigonometricTable.clear();
    m_inverseTrigonometricTable.resize(INVERSE_TRIG_TABLE_SIZE);

    // Precalculate inverse trigonometric values for the entire table
    for (int i = 0; i < INVERSE_TRIG_TABLE_SIZE; ++i)
    {
        // Calculate input value for this table entry (domain [-1, 1])
        float inputValue = -1.0f + (static_cast<float>(i) / static_cast<float>(INVERSE_TRIG_TABLE_SIZE - 1)) * 2.0f;

        // Ensure input value is exactly within valid domain to prevent numerical errors
        inputValue = std::clamp(inputValue, -1.0f, 1.0f);

        // Calculate and store all inverse trigonometric values
        InverseTrigonometricData& data = m_inverseTrigonometricTable[i];
        data.inputValue = inputValue;

        // Calculate arcsine (domain [-1, 1], range [-π/2, π/2])
        data.arcSine = std::asin(inputValue);

        // Calculate arccosine (domain [-1, 1], range [0, π])
        data.arcCosine = std::acos(inputValue);

        // Calculate arctangent for extended range (using inputValue * 10 to cover larger domain)
        // This provides arctangent values for inputs from -10 to +10
        float extendedInput = inputValue * 10.0f;
        data.arcTangent = std::atan(extendedInput);
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Inverse trigonometric tables initialized - Entries: %d",
        INVERSE_TRIG_TABLE_SIZE);
#endif
}

void MathPrecalculation::InitializeColorConversionTables()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing color conversion lookup tables");
#endif

    // Initialize YUV to RGB conversion lookup table with proper size calculation
    // Full YUV lookup would be 256^3 * 3 = 48MB, so we use a more efficient approach
    const int yuvTableSize = 64;  // Reduced size for memory efficiency (64^3 * 3 = 768KB)
    const int yuvTableEntries = yuvTableSize * yuvTableSize * yuvTableSize * 3;

    // Clear and resize the YUV to RGB lookup table
    m_yuvToRgbLookup.clear();
    m_yuvToRgbLookup.resize(yuvTableEntries);

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] YUV lookup table allocated - Size: %d entries (%d bytes)",
        yuvTableEntries, yuvTableEntries);
#endif

    // Initialize YUV to RGB conversion lookup table with reduced precision for memory efficiency
    for (int y = 0; y < yuvTableSize; ++y)
    {
        for (int u = 0; u < yuvTableSize; ++u)
        {
            for (int v = 0; v < yuvTableSize; ++v)
            {
                // Scale values to full range [0, 255] for calculation
                int yFull = (y * 255) / (yuvTableSize - 1);
                int uFull = (u * 255) / (yuvTableSize - 1);
                int vFull = (v * 255) / (yuvTableSize - 1);

                // Calculate RGB values using standard YUV to RGB conversion formulas
                // Y'UV to RGB conversion using BT.601 standard
                int r = static_cast<int>(yFull + 1.402f * (vFull - 128));
                int g = static_cast<int>(yFull - 0.344f * (uFull - 128) - 0.714f * (vFull - 128));
                int b = static_cast<int>(yFull + 1.772f * (uFull - 128));

                // Calculate index into the lookup table
                int index = (y * yuvTableSize + u) * yuvTableSize + v;
                int baseIndex = index * 3;

                // Ensure we don't exceed array bounds
                if (baseIndex + 2 < static_cast<int>(m_yuvToRgbLookup.size()))
                {
                    // Store clamped RGB values in the lookup table
                    m_yuvToRgbLookup[baseIndex + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                    m_yuvToRgbLookup[baseIndex + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                    m_yuvToRgbLookup[baseIndex + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                }
            }
        }
    }

    // Initialize color conversion coefficients table
    m_colorConversionTable.clear();
    m_colorConversionTable.resize(COLOR_CONVERSION_TABLE_SIZE);

    // Standard BT.601 coefficients for YUV to RGB conversion
    const float Kr = 0.299f;
    const float Kg = 0.587f;
    const float Kb = 0.114f;

    // Precalculate YUV to RGB conversion coefficients for each color component
    for (int i = 0; i < COLOR_CONVERSION_TABLE_SIZE; ++i)
    {
        ColorConversionData& data = m_colorConversionTable[i];

        // Calculate conversion coefficients for this index
        float normalizedValue = static_cast<float>(i) / 255.0f;

        // YUV to RGB conversion matrix coefficients (BT.601 standard)
        data.yuvToRgbR = XMFLOAT3(1.0f, 0.0f, 1.402f);                         // R = Y + 1.402*V
        data.yuvToRgbG = XMFLOAT3(1.0f, -0.344f, -0.714f);                     // G = Y - 0.344*U - 0.714*V
        data.yuvToRgbB = XMFLOAT3(1.0f, 1.772f, 0.0f);                         // B = Y + 1.772*U

        // Precalculate clamped values for this component index
        for (int j = 0; j < COLOR_CONVERSION_TABLE_SIZE; ++j)
        {
            data.clampedValues[j] = static_cast<uint8_t>(std::clamp(j, 0, 255));
        }
    }

    // Initialize extended clamp table for values beyond normal range [-256, 255]
    for (int i = 0; i < 512; ++i)
    {
        int clampedValue = i - 256;  // Range from -256 to 255
        m_clampTable[i] = static_cast<uint8_t>(std::clamp(clampedValue, 0, 255));
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Color conversion tables initialized - YUV table: %d entries, Conversion data: %d entries, Clamp table: 512 entries",
        yuvTableEntries, COLOR_CONVERSION_TABLE_SIZE);
#endif
}

void MathPrecalculation::InitializeInterpolationTables()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing interpolation coefficient tables");
#endif

    // Clear and resize interpolation table
    m_interpolationTable.clear();
    m_interpolationTable.resize(INTERPOLATION_TABLE_SIZE);

    // Precalculate interpolation coefficients for smooth animations
    for (int i = 0; i < INTERPOLATION_TABLE_SIZE; ++i)
    {
        // Normalize t value to range [0, 1]
        float t = static_cast<float>(i) / static_cast<float>(INTERPOLATION_TABLE_SIZE - 1);

        InterpolationData& data = m_interpolationTable[i];

        // Linear interpolation coefficient (simply t)
        data.linear = t;

        // Smooth step interpolation (3t^2 - 2t^3)
        data.smoothStep = t * t * (3.0f - 2.0f * t);

        // Smoother step interpolation (6t^5 - 15t^4 + 10t^3)
        data.smootherStep = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);

        // Ease-in interpolation (quadratic)
        data.easeIn = t * t;

        // Ease-out interpolation (inverted quadratic)
        data.easeOut = 1.0f - (1.0f - t) * (1.0f - t);

        // Ease-in-out interpolation (cubic)
        if (t < 0.5f)
        {
            data.easeInOut = 2.0f * t * t;
        }
        else
        {
            float temp = -2.0f * t + 2.0f;
            data.easeInOut = 1.0f - temp * temp * 0.5f;
        }
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Interpolation tables initialized - Entries: %d",
        INTERPOLATION_TABLE_SIZE);
#endif
}

void MathPrecalculation::InitializeParticleData()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing particle system precalculations");
#endif

    // Clear and resize particle directions table
    m_particleDirections.clear();
    m_particleDirections.resize(PARTICLE_ANGLE_DIVISIONS);

    // Precalculate particle directions for explosion effects
    for (int i = 0; i < PARTICLE_ANGLE_DIVISIONS; ++i)
    {
        // Calculate angle for this particle direction
        float angleDegrees = static_cast<float>(i);
        float angleRadians = angleDegrees * XM_PI / 180.0f;

        ParticleData& data = m_particleDirections[i];

        // Store angle values
        data.angleDegrees = angleDegrees;
        data.angleRadians = angleRadians;

        // Calculate normalized direction vector
        data.direction.x = std::cos(angleRadians);
        data.direction.y = std::sin(angleRadians);

        // Calculate velocity vector for unit speed
        data.velocity = data.direction;
    }

    // Precalculate common explosion patterns for different particle counts
    std::vector<int> commonParticleCounts = { 8, 16, 24, 32, 48, 64, 100, 128 };

    for (int particleCount : commonParticleCounts)
    {
        std::vector<XMFLOAT2> explosionPattern;
        explosionPattern.reserve(particleCount);

        // Calculate evenly distributed angles for this particle count
        float angleStep = 360.0f / static_cast<float>(particleCount);

        for (int i = 0; i < particleCount; ++i)
        {
            float angle = angleStep * static_cast<float>(i);
            float angleRadians = angle * XM_PI / 180.0f;

            // Calculate direction vector for this particle
            XMFLOAT2 direction;
            direction.x = std::cos(angleRadians);
            direction.y = std::sin(angleRadians);

            explosionPattern.push_back(direction);
        }

        // Store the explosion pattern in the cache
        m_explosionPatterns[particleCount] = std::move(explosionPattern);
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Particle data initialized - Directions: %d, Patterns: %zu",
        PARTICLE_ANGLE_DIVISIONS, m_explosionPatterns.size());
#endif
}

void MathPrecalculation::InitializeMatrixCaches()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing matrix transformation caches");
#endif

    // Precalculate common scale matrices
    std::vector<float> commonScales = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 4.0f, 8.0f };

    for (float scale : commonScales)
    {
        // Create hash key for this scale (multiply by 1000 and cast to int for precision)
        int scaleKey = static_cast<int>(scale * 1000.0f);

        // Create and store the scale matrix
        XMMATRIX scaleMatrix = XMMatrixScaling(scale, scale, scale);
        m_scaleMatrixCache[scaleKey] = scaleMatrix;
    }

    // Precalculate common rotation matrices (every 15 degrees)
    for (int degrees = 0; degrees < 360; degrees += 15)
    {
        float angleRadians = static_cast<float>(degrees) * XM_PI / 180.0f;

        // Create rotation matrices for each axis
        XMMATRIX rotationX = XMMatrixRotationX(angleRadians);
        XMMATRIX rotationY = XMMatrixRotationY(angleRadians);
        XMMATRIX rotationZ = XMMatrixRotationZ(angleRadians);

        // Store rotation matrices with axis-specific keys
        m_rotationMatrixCache[degrees * 1000 + 0] = rotationX;  // X-axis rotation
        m_rotationMatrixCache[degrees * 1000 + 1] = rotationY;  // Y-axis rotation
        m_rotationMatrixCache[degrees * 1000 + 2] = rotationZ;  // Z-axis rotation
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Matrix caches initialized - Scale matrices: %zu, Rotation matrices: %zu",
        m_scaleMatrixCache.size(), m_rotationMatrixCache.size());
#endif
}

void MathPrecalculation::InitializeTextOptimizations()
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Initializing text rendering optimizations");
#endif

    // Precalculate character widths for common ASCII characters
    // This would typically be done with font metrics, but for now we use estimates
    const std::wstring commonChars = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 !@#$%^&*()_+-=[]{}|;':\",./<>?";

    for (wchar_t ch : commonChars)
    {
        float estimatedWidth = 0.0f;

        // Estimate character width based on character type
        if (ch == L' ')
        {
            estimatedWidth = 0.25f;  // Space character
        }
        else if (ch >= L'A' && ch <= L'Z')
        {
            estimatedWidth = 0.7f;   // Uppercase letters
        }
        else if (ch >= L'a' && ch <= L'z')
        {
            // Variable width for lowercase letters
            if (ch == L'i' || ch == L'l')
                estimatedWidth = 0.3f;
            else if (ch == L'm' || ch == L'w')
                estimatedWidth = 0.8f;
            else
                estimatedWidth = 0.6f;
        }
        else if (ch >= L'0' && ch <= L'9')
        {
            estimatedWidth = 0.6f;   // Numbers
        }
        else
        {
            estimatedWidth = 0.5f;   // Other characters
        }

        m_characterWidthCache[ch] = estimatedWidth;
    }

    // Precalculate transparency lookup table for text fade effects
    m_transparencyLookup.clear();
    m_transparencyLookup.resize(1024);

    for (int i = 0; i < 1024; ++i)
    {
        // Normalize value to range [0, 1]
        float normalizedValue = static_cast<float>(i) / 1023.0f;

        // Calculate smooth fade curve (sigmoid-like)
        float transparency = 1.0f / (1.0f + std::exp(-6.0f * (normalizedValue - 0.5f)));

        m_transparencyLookup[i] = transparency;
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[MathPrecalculation] Text optimizations initialized - Character widths: %zu, Transparency entries: %zu",
        m_characterWidthCache.size(), m_transparencyLookup.size());
#endif
}

//==============================================================================
// Trigonometric Lookup Methods
//==============================================================================
float MathPrecalculation::FastSin(float angle) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastSin called before initialization");
#endif
        return std::sin(angle);  // Fallback to standard sine
    }

    // Normalize angle to range [0, 2π]
    float normalizedAngle = NormalizeAngle(angle);

    // Convert angle to table index
    int index = AngleToIndex(normalizedAngle);

    // Ensure index is within bounds
    if (index >= TRIG_TABLE_SIZE)
    {
        index = TRIG_TABLE_SIZE - 1;
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated sine value
    return m_trigonometricTable[index].sine;
}

float MathPrecalculation::FastCos(float angle) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastCos called before initialization");
#endif
        return std::cos(angle);  // Fallback to standard cosine
    }

    // Normalize angle to range [0, 2π]
    float normalizedAngle = NormalizeAngle(angle);

    // Convert angle to table index
    int index = AngleToIndex(normalizedAngle);

    // Ensure index is within bounds
    if (index >= TRIG_TABLE_SIZE)
    {
        index = TRIG_TABLE_SIZE - 1;
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated cosine value
    return m_trigonometricTable[index].cosine;
}

float MathPrecalculation::FastTan(float angle) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastTan called before initialization");
#endif
        return std::tan(angle);  // Fallback to standard tangent
    }

    // Normalize angle to range [0, 2π]
    float normalizedAngle = NormalizeAngle(angle);

    // Convert angle to table index
    int index = AngleToIndex(normalizedAngle);

    // Ensure index is within bounds
    if (index >= TRIG_TABLE_SIZE)
    {
        index = TRIG_TABLE_SIZE - 1;
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated tangent value
    return m_trigonometricTable[index].tangent;
}

float MathPrecalculation::FastCot(float angle) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastCot called before initialization");
#endif
        return 1.0f / std::tan(angle);  // Fallback to standard cotangent
    }

    // Normalize angle to range [0, 2π]
    float normalizedAngle = NormalizeAngle(angle);

    // Convert angle to table index
    int index = AngleToIndex(normalizedAngle);

    // Ensure index is within bounds
    if (index >= TRIG_TABLE_SIZE)
    {
        index = TRIG_TABLE_SIZE - 1;
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated cotangent value
    return m_trigonometricTable[index].cotangent;
}

float MathPrecalculation::FastASin(float value) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastASin called before initialization");
#endif
        return std::asin(std::clamp(value, -1.0f, 1.0f));  // Fallback to standard arcsine with clamping
    }

    // Clamp input value to valid domain [-1, 1] for arcsine
    float clampedValue = std::clamp(value, -1.0f, 1.0f);

    // Convert input value to table index
    int index = static_cast<int>((clampedValue + 1.0f) * INVERSE_TRIG_PRECISION_FACTOR);

    // Ensure index is within bounds
    index = std::clamp(index, 0, INVERSE_TRIG_TABLE_SIZE - 1);

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated arcsine value
    return m_inverseTrigonometricTable[index].arcSine;
}

float MathPrecalculation::FastACos(float value) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastACos called before initialization");
#endif
        return std::acos(std::clamp(value, -1.0f, 1.0f));  // Fallback to standard arccosine with clamping
    }

    // Clamp input value to valid domain [-1, 1] for arccosine
    float clampedValue = std::clamp(value, -1.0f, 1.0f);

    // Convert input value to table index
    int index = static_cast<int>((clampedValue + 1.0f) * INVERSE_TRIG_PRECISION_FACTOR);

    // Ensure index is within bounds
    index = std::clamp(index, 0, INVERSE_TRIG_TABLE_SIZE - 1);

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated arccosine value
    return m_inverseTrigonometricTable[index].arcCosine;
}

float MathPrecalculation::FastATan(float value) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastATan called before initialization");
#endif
        return std::atan(value);  // Fallback to standard arctangent
    }

    // Handle values outside the optimized range [-10, 10]
    if (value < -10.0f)
    {
        return -XM_PIDIV2 + std::atan(1.0f / value);  // Asymptotic approach to -π/2
    }
    if (value > 10.0f)
    {
        return XM_PIDIV2 - std::atan(1.0f / value);   // Asymptotic approach to π/2
    }

    // Normalize input value to lookup table domain [-1, 1]
    float normalizedValue = value / 10.0f;
    normalizedValue = std::clamp(normalizedValue, -1.0f, 1.0f);

    // Convert normalized value to table index
    int index = static_cast<int>((normalizedValue + 1.0f) * INVERSE_TRIG_PRECISION_FACTOR);

    // Ensure index is within bounds
    index = std::clamp(index, 0, INVERSE_TRIG_TABLE_SIZE - 1);

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated arctangent value
    return m_inverseTrigonometricTable[index].arcTangent;
}

float MathPrecalculation::FastATan2(float y, float x) const
{
    // Handle special cases first for optimal performance
    if (x == 0.0f)
    {
        if (y > 0.0f)
        {
            return XM_PIDIV2;  // π/2
        }
        else if (y < 0.0f)
        {
            return -XM_PIDIV2; // -π/2
        }
        else
        {
            return 0.0f;       // Both x and y are zero
        }
    }

    // Handle cases where y is zero
    if (y == 0.0f)
    {
        if (x > 0.0f)
        {
            return 0.0f;       // Positive x-axis
        }
        else
        {
            return XM_PI;      // Negative x-axis
        }
    }

    // Calculate basic arctangent of y/x
    float basicAtan = FastATan(y / x);

    // Adjust for proper quadrant based on signs of x and y
    if (x > 0.0f)
    {
        // First and fourth quadrants
        return basicAtan;
    }
    else if (x < 0.0f)
    {
        if (y >= 0.0f)
        {
            // Second quadrant
            return basicAtan + XM_PI;
        }
        else
        {
            // Third quadrant  
            return basicAtan - XM_PI;
        }
    }

    // This should never be reached due to earlier checks, but included for completeness
    return basicAtan;
}

void MathPrecalculation::FastSinCos(float angle, float& outSin, float& outCos) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastSinCos called before initialization");
#endif
        outSin = std::sin(angle);
        outCos = std::cos(angle);
        return;
    }

    // Normalize angle to range [0, 2π]
    float normalizedAngle = NormalizeAngle(angle);

    // Convert angle to table index
    int index = AngleToIndex(normalizedAngle);

    // Ensure index is within bounds
    if (index >= TRIG_TABLE_SIZE)
    {
        index = TRIG_TABLE_SIZE - 1;
    }

    // Increment lookup counter for statistics (single lookup for both values)
    m_lookupCount.fetch_add(1);

    // Return both precalculated values efficiently
    const TrigonometricData& data = m_trigonometricTable[index];
    outSin = data.sine;
    outCos = data.cosine;
}

float MathPrecalculation::FastSqrt(float value) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastSqrt called before initialization");
#endif
        return std::sqrt(value);  // Fallback to standard square root
    }

    // Handle negative values
    if (value < 0.0f)
    {
        return 0.0f;  // Return 0 for negative values
    }

    // Handle values beyond table range
    if (value >= 1000.0f)
    {
        return std::sqrt(value);  // Fallback to standard square root for large values
    }

    // Convert value to table index
    int index = static_cast<int>(value * SQRT_PRECISION_FACTOR);

    // Ensure index is within bounds
    if (index >= SQRT_TABLE_SIZE)
    {
        index = SQRT_TABLE_SIZE - 1;
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated square root value
    return m_sqrtTable[index];
}



//==============================================================================
// Color Conversion Methods
//==============================================================================
void MathPrecalculation::FastYuvToRgb(uint8_t y, uint8_t u, uint8_t v, uint8_t& outR, uint8_t& outG, uint8_t& outB) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[MathPrecalculation] FastYuvToRgb called before initialization");
#endif
        // Fallback to standard conversion using BT.601 coefficients
        int r = static_cast<int>(y + 1.402f * (v - 128));
        int g = static_cast<int>(y - 0.344f * (u - 128) - 0.714f * (v - 128));
        int b = static_cast<int>(y + 1.772f * (u - 128));

        outR = static_cast<uint8_t>(std::clamp(r, 0, 255));
        outG = static_cast<uint8_t>(std::clamp(g, 0, 255));
        outB = static_cast<uint8_t>(std::clamp(b, 0, 255));
        return;
    }

    // Check if lookup table is properly sized
    if (m_yuvToRgbLookup.empty())
    {
        // Fallback to calculation if lookup table is not available
        int r = static_cast<int>(y + 1.402f * (v - 128));
        int g = static_cast<int>(y - 0.344f * (u - 128) - 0.714f * (v - 128));
        int b = static_cast<int>(y + 1.772f * (u - 128));

        outR = FastClamp(r);
        outG = FastClamp(g);
        outB = FastClamp(b);
        return;
    }

    // Use reduced lookup table size (64x64x64 instead of 256x256x256)
    const int yuvTableSize = 64;

    // Scale input values to reduced table size
    int yIndex = (y * (yuvTableSize - 1)) / 255;
    int uIndex = (u * (yuvTableSize - 1)) / 255;
    int vIndex = (v * (yuvTableSize - 1)) / 255;

    // Ensure indices are within bounds
    yIndex = std::clamp(yIndex, 0, yuvTableSize - 1);
    uIndex = std::clamp(uIndex, 0, yuvTableSize - 1);
    vIndex = std::clamp(vIndex, 0, yuvTableSize - 1);

    // Calculate index into the YUV to RGB lookup table
    size_t index = (static_cast<size_t>(yIndex) * yuvTableSize + static_cast<size_t>(uIndex)) * yuvTableSize + static_cast<size_t>(vIndex);
    size_t baseIndex = index * 3;

    // Ensure index is within bounds of the lookup table
    if (baseIndex + 2 < m_yuvToRgbLookup.size())
    {
        // Retrieve precalculated RGB values from lookup table
        outR = m_yuvToRgbLookup[baseIndex + 0];
        outG = m_yuvToRgbLookup[baseIndex + 1];
        outB = m_yuvToRgbLookup[baseIndex + 2];

        // Increment lookup counter for statistics
        m_lookupCount.fetch_add(1);
    }
    else
    {
        // Fallback calculation if index is out of bounds
        int r = static_cast<int>(y + 1.402f * (v - 128));
        int g = static_cast<int>(y - 0.344f * (u - 128) - 0.714f * (v - 128));
        int b = static_cast<int>(y + 1.772f * (u - 128));

        outR = FastClamp(r);
        outG = FastClamp(g);
        outB = FastClamp(b);

#if defined(_DEBUG_MATHPRECALC_)
        debug.logDebugMessage(LogLevel::LOG_WARNING,
            L"[MathPrecalculation] YUV lookup index out of bounds - using fallback calculation");
#endif
    }
}

XMFLOAT4 MathPrecalculation::FastYuvToRgbFloat(float y, float u, float v) const
{
    // Convert float values to uint8_t for lookup
    uint8_t yInt = static_cast<uint8_t>(std::clamp(y * 255.0f, 0.0f, 255.0f));
    uint8_t uInt = static_cast<uint8_t>(std::clamp(u * 255.0f, 0.0f, 255.0f));
    uint8_t vInt = static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));

    // Perform lookup using integer version
    uint8_t r, g, b;
    FastYuvToRgb(yInt, uInt, vInt, r, g, b);

    // Convert back to float and return as XMFLOAT4
    return XMFLOAT4(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        1.0f  // Full alpha
    );
}

void MathPrecalculation::FastRgbToYuv(uint8_t r, uint8_t g, uint8_t b, uint8_t& outY, uint8_t& outU, uint8_t& outV) const
{
    // Standard RGB to YUV conversion using BT.601 coefficients
    // Y  =  0.299*R + 0.587*G + 0.114*B
    // U  = -0.169*R - 0.331*G + 0.500*B + 128
    // V  =  0.500*R - 0.419*G - 0.081*B + 128

    int y = static_cast<int>(0.299f * r + 0.587f * g + 0.114f * b);
    int u = static_cast<int>(-0.169f * r - 0.331f * g + 0.500f * b + 128);
    int v = static_cast<int>(0.500f * r - 0.419f * g - 0.081f * b + 128);

    // Use fast clamping
    outY = FastClamp(y);
    outU = FastClamp(u);
    outV = FastClamp(v);
}

uint8_t MathPrecalculation::FastGammaCorrect(uint8_t input, float gamma) const
{
    // Normalize input to range [0, 1]
    float normalizedInput = static_cast<float>(input) / 255.0f;

    // Apply gamma correction
    float corrected = std::pow(normalizedInput, 1.0f / gamma);

    // Convert back to uint8_t range and clamp
    int result = static_cast<int>(corrected * 255.0f + 0.5f);
    return FastClamp(result);
}

uint8_t MathPrecalculation::FastClamp(int value) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        return static_cast<uint8_t>(std::clamp(value, 0, 255));
    }

    // Offset value to map range [-256, 255] to [0, 511]
    int offsetValue = value + 256;

    // Ensure index is within bounds
    if (offsetValue < 0)
    {
        return 0;
    }
    else if (offsetValue >= 512)
    {
        return 255;
    }

    // Use lookup table for fast clamping
    return m_clampTable[offsetValue];
}

//==============================================================================
// Interpolation Methods
//==============================================================================
float MathPrecalculation::FastLerp(float start, float end, float t) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        return start + t * (end - start);  // Fallback to standard linear interpolation
    }

    // Clamp t to valid range [0, 1]
    float clampedT = std::clamp(t, 0.0f, 1.0f);

    // Convert t to table index
    int index = static_cast<int>(clampedT * (INTERPOLATION_TABLE_SIZE - 1));

    // Ensure index is within bounds
    if (index >= INTERPOLATION_TABLE_SIZE)
    {
        index = INTERPOLATION_TABLE_SIZE - 1;
    }

    // Get linear interpolation coefficient from lookup table
    float coefficient = m_interpolationTable[index].linear;

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return interpolated value
    return start + coefficient * (end - start);
}

float MathPrecalculation::FastSmoothStep(float start, float end, float t) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        float clampedT = std::clamp(t, 0.0f, 1.0f);
        float smoothT = clampedT * clampedT * (3.0f - 2.0f * clampedT);
        return start + smoothT * (end - start);
    }

    // Clamp t to valid range [0, 1]
    float clampedT = std::clamp(t, 0.0f, 1.0f);

    // Convert t to table index
    int index = static_cast<int>(clampedT * (INTERPOLATION_TABLE_SIZE - 1));

    // Ensure index is within bounds
    if (index >= INTERPOLATION_TABLE_SIZE)
    {
        index = INTERPOLATION_TABLE_SIZE - 1;
    }

    // Get smooth step coefficient from lookup table
    float coefficient = m_interpolationTable[index].smoothStep;

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return interpolated value
    return start + coefficient * (end - start);
}

float MathPrecalculation::FastSmootherStep(float start, float end, float t) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        float clampedT = std::clamp(t, 0.0f, 1.0f);
        float smoothT = clampedT * clampedT * clampedT * (clampedT * (clampedT * 6.0f - 15.0f) + 10.0f);
        return start + smoothT * (end - start);
    }

    // Clamp t to valid range [0, 1]
    float clampedT = std::clamp(t, 0.0f, 1.0f);

    // Convert t to table index
    int index = static_cast<int>(clampedT * (INTERPOLATION_TABLE_SIZE - 1));

    // Ensure index is within bounds
    if (index >= INTERPOLATION_TABLE_SIZE)
    {
        index = INTERPOLATION_TABLE_SIZE - 1;
    }

    // Get smoother step coefficient from lookup table
    float coefficient = m_interpolationTable[index].smootherStep;

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return interpolated value
    return start + coefficient * (end - start);
}

float MathPrecalculation::FastEaseIn(float start, float end, float t) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        float clampedT = std::clamp(t, 0.0f, 1.0f);
        float easeT = clampedT * clampedT;
        return start + easeT * (end - start);
    }

    // Clamp t to valid range [0, 1]
    float clampedT = std::clamp(t, 0.0f, 1.0f);

    // Convert t to table index
    int index = static_cast<int>(clampedT * (INTERPOLATION_TABLE_SIZE - 1));

    // Ensure index is within bounds
    if (index >= INTERPOLATION_TABLE_SIZE)
    {
        index = INTERPOLATION_TABLE_SIZE - 1;
    }

    // Get ease-in coefficient from lookup table
    float coefficient = m_interpolationTable[index].easeIn;

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return interpolated value
    return start + coefficient * (end - start);
}

float MathPrecalculation::FastEaseOut(float start, float end, float t) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        float clampedT = std::clamp(t, 0.0f, 1.0f);
        float easeT = 1.0f - (1.0f - clampedT) * (1.0f - clampedT);
        return start + easeT * (end - start);
    }

    // Clamp t to valid range [0, 1]
    float clampedT = std::clamp(t, 0.0f, 1.0f);

    // Convert t to table index
    int index = static_cast<int>(clampedT * (INTERPOLATION_TABLE_SIZE - 1));

    // Ensure index is within bounds
    if (index >= INTERPOLATION_TABLE_SIZE)
    {
        index = INTERPOLATION_TABLE_SIZE - 1;
    }

    // Get ease-out coefficient from lookup table
    float coefficient = m_interpolationTable[index].easeOut;

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return interpolated value
    return start + coefficient * (end - start);
}

float MathPrecalculation::FastEaseInOut(float start, float end, float t) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        float clampedT = std::clamp(t, 0.0f, 1.0f);
        float easeT;
        if (clampedT < 0.5f)
        {
            easeT = 2.0f * clampedT * clampedT;
        }
        else
        {
            float temp = -2.0f * clampedT + 2.0f;
            easeT = 1.0f - temp * temp * 0.5f;
        }
        return start + easeT * (end - start);
    }

    // Clamp t to valid range [0, 1]
    float clampedT = std::clamp(t, 0.0f, 1.0f);

    // Convert t to table index
    int index = static_cast<int>(clampedT * (INTERPOLATION_TABLE_SIZE - 1));

    // Ensure index is within bounds
    if (index >= INTERPOLATION_TABLE_SIZE)
    {
        index = INTERPOLATION_TABLE_SIZE - 1;
    }

    // Get ease-in-out coefficient from lookup table
    float coefficient = m_interpolationTable[index].easeInOut;

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return interpolated value
    return start + coefficient * (end - start);
}

//==============================================================================
// Particle System Helper Methods
//==============================================================================
XMFLOAT2 MathPrecalculation::GetParticleDirection(int particleIndex, int totalParticles) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        // Fallback calculation
        float angle = (static_cast<float>(particleIndex) / static_cast<float>(totalParticles)) * 2.0f * XM_PI;
        return XMFLOAT2(std::cos(angle), std::sin(angle));
    }

    // Check if we have a precalculated pattern for this particle count
    auto it = m_explosionPatterns.find(totalParticles);
    if (it != m_explosionPatterns.end() && particleIndex < static_cast<int>(it->second.size()))
    {
        // Return precalculated direction from explosion pattern
        m_lookupCount.fetch_add(1);
        return it->second[particleIndex];
    }

    // Calculate direction using angle divisions if no specific pattern exists
    int angleIndex = (particleIndex * PARTICLE_ANGLE_DIVISIONS) / totalParticles;
    angleIndex = std::clamp(angleIndex, 0, PARTICLE_ANGLE_DIVISIONS - 1);

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated direction
    return m_particleDirections[angleIndex].direction;
}

XMFLOAT2 MathPrecalculation::GetParticleVelocity(float angle, float speed) const
{
    // Get direction vector for the specified angle
    float normalizedAngle = NormalizeAngle(angle);
    int angleIndex = static_cast<int>((normalizedAngle / (2.0f * XM_PI)) * PARTICLE_ANGLE_DIVISIONS);
    angleIndex = std::clamp(angleIndex, 0, PARTICLE_ANGLE_DIVISIONS - 1);

    // Get base direction from lookup table
    XMFLOAT2 direction;
    if (m_bIsInitialized.load() && angleIndex < static_cast<int>(m_particleDirections.size()))
    {
        direction = m_particleDirections[angleIndex].direction;
        m_lookupCount.fetch_add(1);
    }
    else
    {
        // Fallback calculation
        direction.x = std::cos(angle);
        direction.y = std::sin(angle);
    }

    // Scale direction by speed to get velocity
    return XMFLOAT2(direction.x * speed, direction.y * speed);
}

float MathPrecalculation::FastDistance(const XMFLOAT2& point1, const XMFLOAT2& point2) const
{
    // Calculate squared distance
    float dx = point2.x - point1.x;
    float dy = point2.y - point1.y;
    float distanceSquared = dx * dx + dy * dy;

    // Use fast square root lookup
    return FastSqrt(distanceSquared);
}

XMFLOAT2 MathPrecalculation::FastNormalize(const XMFLOAT2& vector) const
{
    // Calculate vector magnitude using fast distance calculation
    float magnitude = FastSqrt(vector.x * vector.x + vector.y * vector.y);

    // Handle zero-length vector
    if (magnitude < 1e-8f)
    {
        return XMFLOAT2(0.0f, 0.0f);
    }

    // Normalize by dividing by magnitude
    float invMagnitude = 1.0f / magnitude;
    return XMFLOAT2(vector.x * invMagnitude, vector.y * invMagnitude);
}

//==============================================================================
// Matrix Transformation Methods
//==============================================================================
XMMATRIX MathPrecalculation::GetScaleMatrix(float scaleX, float scaleY, float scaleZ) const
{
    // Check for uniform scaling first (most common case)
    if (scaleX == scaleY && scaleY == scaleZ)
    {
        // Create hash key for uniform scale lookup
        int scaleKey = static_cast<int>(scaleX * 1000.0f);

        // Check if we have this scale matrix cached
        auto it = m_scaleMatrixCache.find(scaleKey);
        if (it != m_scaleMatrixCache.end())
        {
            m_lookupCount.fetch_add(1);
            return it->second;
        }
    }

    // Fallback to standard matrix creation for non-cached scales
    return XMMatrixScaling(scaleX, scaleY, scaleZ);
}

XMMATRIX MathPrecalculation::GetRotationMatrix(float angleX, float angleY, float angleZ) const
{
    // Convert angles from radians to degrees for lookup
    int degreesX = static_cast<int>((angleX * 180.0f / XM_PI) + 0.5f);
    int degreesY = static_cast<int>((angleY * 180.0f / XM_PI) + 0.5f);
    int degreesZ = static_cast<int>((angleZ * 180.0f / XM_PI) + 0.5f);

    // Normalize degrees to [0, 360) range
    degreesX = ((degreesX % 360) + 360) % 360;
    degreesY = ((degreesY % 360) + 360) % 360;
    degreesZ = ((degreesZ % 360) + 360) % 360;

    // Check for single-axis rotations (most common case)
    if (angleX != 0.0f && angleY == 0.0f && angleZ == 0.0f)
    {
        // X-axis rotation only
        if (degreesX % 15 == 0)  // Check if it's a cached angle
        {
            int rotationKey = degreesX * 1000 + 0;  // X-axis key
            auto it = m_rotationMatrixCache.find(rotationKey);
            if (it != m_rotationMatrixCache.end())
            {
                m_lookupCount.fetch_add(1);
                return it->second;
            }
        }
    }
    else if (angleX == 0.0f && angleY != 0.0f && angleZ == 0.0f)
    {
        // Y-axis rotation only
        if (degreesY % 15 == 0)  // Check if it's a cached angle
        {
            int rotationKey = degreesY * 1000 + 1;  // Y-axis key
            auto it = m_rotationMatrixCache.find(rotationKey);
            if (it != m_rotationMatrixCache.end())
            {
                m_lookupCount.fetch_add(1);
                return it->second;
            }
        }
    }
    else if (angleX == 0.0f && angleY == 0.0f && angleZ != 0.0f)
    {
        // Z-axis rotation only
        if (degreesZ % 15 == 0)  // Check if it's a cached angle
        {
            int rotationKey = degreesZ * 1000 + 2;  // Z-axis key
            auto it = m_rotationMatrixCache.find(rotationKey);
            if (it != m_rotationMatrixCache.end())
            {
                m_lookupCount.fetch_add(1);
                return it->second;
            }
        }
    }

    // Fallback to standard matrix creation for non-cached rotations
    return XMMatrixRotationRollPitchYaw(angleX, angleY, angleZ);
}

XMMATRIX MathPrecalculation::FastMatrixMultiply(const XMMATRIX& matrix1, const XMMATRIX& matrix2) const
{
    // For now, use standard DirectX matrix multiplication
    // Future optimization: could implement SIMD-optimized multiplication
    return XMMatrixMultiply(matrix1, matrix2);
}

XMMATRIX MathPrecalculation::GetViewMatrix(const XMFLOAT3& position, const XMFLOAT3& target, const XMFLOAT3& up) const
{
    // Convert XMFLOAT3 to XMVECTOR for DirectX math functions
    XMVECTOR eyePos = XMLoadFloat3(&position);
    XMVECTOR targetPos = XMLoadFloat3(&target);
    XMVECTOR upVector = XMLoadFloat3(&up);

    // Use standard DirectX view matrix calculation
    // Future optimization: could cache common camera positions
    return XMMatrixLookAtLH(eyePos, targetPos, upVector);
}

//==============================================================================
// Text Rendering Optimization Methods
//==============================================================================
float MathPrecalculation::GetCharacterWidthFast(wchar_t character, float fontSize) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        return fontSize * 0.6f;  // Fallback estimate
    }

    // Look up character width in cache
    auto it = m_characterWidthCache.find(character);
    if (it != m_characterWidthCache.end())
    {
        m_lookupCount.fetch_add(1);
        return it->second * fontSize;
    }

    // Fallback for characters not in cache
    return fontSize * 0.6f;  // Default character width estimate
}

float MathPrecalculation::GetTextTransparencyFast(float position, float regionStart, float regionEnd, float fadeDistance) const
{
    // Ensure the system is initialized before attempting lookup
    if (!m_bIsInitialized.load())
    {
        // Fallback calculation
        if (position < regionStart - fadeDistance || position > regionEnd + fadeDistance)
        {
            return 0.0f;  // Completely transparent
        }

        if (position < regionStart)
        {
            float distanceFromStart = regionStart - position;
            return 1.0f - (distanceFromStart / fadeDistance);
        }

        if (position > regionEnd)
        {
            float distanceFromEnd = position - regionEnd;
            return 1.0f - (distanceFromEnd / fadeDistance);
        }

        return 1.0f;  // Fully opaque
    }

    // Calculate normalized position within the transparency range
    float totalRange = (regionEnd + fadeDistance) - (regionStart - fadeDistance);
    if (totalRange <= 0.0f)
    {
        return 1.0f;
    }

    float normalizedPos = (position - (regionStart - fadeDistance)) / totalRange;
    normalizedPos = std::clamp(normalizedPos, 0.0f, 1.0f);

    // Convert to lookup table index
    int index = static_cast<int>(normalizedPos * (m_transparencyLookup.size() - 1));
    index = std::clamp(index, 0, static_cast<int>(m_transparencyLookup.size() - 1));

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Return precalculated transparency value
    return m_transparencyLookup[index];
}

//==============================================================================
// Utility Methods
//==============================================================================
size_t MathPrecalculation::GetMemoryUsage() const
{
    size_t totalMemory = 0;

    // Calculate memory usage for each lookup table
    totalMemory += m_trigonometricTable.size() * sizeof(TrigonometricData);
    totalMemory += m_sqrtTable.size() * sizeof(float);
    totalMemory += m_colorConversionTable.size() * sizeof(ColorConversionData);
    totalMemory += m_yuvToRgbLookup.size() * sizeof(uint8_t);                   // Vector-based YUV lookup
    totalMemory += m_clampTable.size() * sizeof(uint8_t);
    totalMemory += m_interpolationTable.size() * sizeof(InterpolationData);
    totalMemory += m_particleDirections.size() * sizeof(ParticleData);
    totalMemory += m_transparencyLookup.size() * sizeof(float);
    totalMemory += m_inverseTrigonometricTable.size() * sizeof(InverseTrigonometricData);

    // Calculate memory usage for hash maps
    for (const auto& pair : m_explosionPatterns)
    {
        totalMemory += pair.second.size() * sizeof(XMFLOAT2);
    }

    totalMemory += m_scaleMatrixCache.size() * (sizeof(int) + sizeof(XMMATRIX));
    totalMemory += m_rotationMatrixCache.size() * (sizeof(int) + sizeof(XMMATRIX));
    totalMemory += m_characterWidthCache.size() * (sizeof(wchar_t) + sizeof(float));

    return totalMemory;
}

bool MathPrecalculation::ValidateTables() const
{
    // Ensure the system is initialized before validation
    if (!m_bIsInitialized.load())
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Cannot validate tables - system not initialized");
#endif
        return false;
    }

    bool isValid = true;

    // Validate trigonometric table
    if (m_trigonometricTable.size() != TRIG_TABLE_SIZE)
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Trigonometric table size mismatch");
#endif
        isValid = false;
    }

    // Validate square root table
    if (m_sqrtTable.size() != SQRT_TABLE_SIZE)
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Square root table size mismatch");
#endif
        isValid = false;
    }

    // Validate interpolation table
    if (m_interpolationTable.size() != INTERPOLATION_TABLE_SIZE)
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Interpolation table size mismatch");
#endif
        isValid = false;
    }

    // Validate particle directions table
    if (m_particleDirections.size() != PARTICLE_ANGLE_DIVISIONS)
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Particle directions table size mismatch");
#endif
        isValid = false;
    }

    // Sample validation of trigonometric values
    if (!m_trigonometricTable.empty())
    {
        const TrigonometricData& zeroData = m_trigonometricTable[0];
        if (std::abs(zeroData.sine - 0.0f) > 1e-6f || std::abs(zeroData.cosine - 1.0f) > 1e-6f)
        {
#if defined(_DEBUG_MATHPRECALC_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Trigonometric values at angle 0 are incorrect");
#endif
            isValid = false;
        }
    }

    // Validate inverse trigonometric table
    if (m_inverseTrigonometricTable.size() != INVERSE_TRIG_TABLE_SIZE)
    {
#if defined(_DEBUG_MATHPRECALC_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Inverse trigonometric table size mismatch");
#endif
        isValid = false;
    }

    // Sample validation of inverse trigonometric values
    if (!m_inverseTrigonometricTable.empty())
    {
        // Validate arcsine(0) = 0
        int zeroIndex = INVERSE_TRIG_TABLE_SIZE / 2;  // Middle index corresponds to input value 0
        const InverseTrigonometricData& zeroData = m_inverseTrigonometricTable[zeroIndex];
        if (std::abs(zeroData.arcSine - 0.0f) > 1e-6f)
        {
#if defined(_DEBUG_MATHPRECALC_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Inverse trigonometric values at input 0 are incorrect");
#endif
            isValid = false;
        }
    }

#if defined(_DEBUG_MATHPRECALC_)
    if (isValid)
    {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] All lookup tables validated successfully");
    }
    else
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[MathPrecalculation] Lookup table validation failed");
    }
#endif

    return isValid;
}

void MathPrecalculation::DumpTableStatistics() const
{
#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] === Lookup Table Statistics ===");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Trigonometric table entries: %d", TRIG_TABLE_SIZE);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Inverse trigonometric table entries: %d", INVERSE_TRIG_TABLE_SIZE);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Square root table entries: %d", SQRT_TABLE_SIZE);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Color conversion entries: %d", COLOR_CONVERSION_TABLE_SIZE);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Interpolation table entries: %d", INTERPOLATION_TABLE_SIZE);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Particle directions: %d", PARTICLE_ANGLE_DIVISIONS);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Explosion patterns: %zu", m_explosionPatterns.size());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Scale matrix cache: %zu", m_scaleMatrixCache.size());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Rotation matrix cache: %zu", m_rotationMatrixCache.size());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Character width cache: %zu", m_characterWidthCache.size());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Transparency lookup entries: %zu", m_transparencyLookup.size());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Total memory usage: %zu bytes", GetMemoryUsage());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Total lookups performed: %zu", m_lookupCount.load());
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] === End Statistics ===");
#endif
}

void MathPrecalculation::Cleanup()
{
    // Check if already cleaned up
    if (m_bHasCleanedUp.load())
    {
        return;
    }

    // Thread-safe cleanup using mutex
    std::lock_guard<std::mutex> lock(m_tablesMutex);

    // Double-check pattern to ensure thread safety
    if (m_bHasCleanedUp.load())
    {
        return;
    }

#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Starting cleanup of lookup tables");
#endif

    // Clear all lookup tables and free memory
    m_trigonometricTable.clear();
    m_trigonometricTable.shrink_to_fit();

    m_sqrtTable.clear();
    m_sqrtTable.shrink_to_fit();

    m_colorConversionTable.clear();
    m_colorConversionTable.shrink_to_fit();

    m_yuvToRgbLookup.clear();                                                   // Clear vector-based YUV lookup
    m_yuvToRgbLookup.shrink_to_fit();

    m_interpolationTable.clear();
    m_interpolationTable.shrink_to_fit();

    m_particleDirections.clear();
    m_particleDirections.shrink_to_fit();

    m_inverseTrigonometricTable.clear();
    m_inverseTrigonometricTable.shrink_to_fit();

    m_explosionPatterns.clear();
    m_scaleMatrixCache.clear();
    m_rotationMatrixCache.clear();
    m_characterWidthCache.clear();

    m_transparencyLookup.clear();
    m_transparencyLookup.shrink_to_fit();

    // Reset state flags
    m_bIsInitialized.store(false);
    m_bHasCleanedUp.store(true);
    m_totalMemoryUsage.store(0);
    m_lookupCount.store(0);

#if defined(_DEBUG_MATHPRECALC_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[MathPrecalculation] Cleanup completed successfully");
#endif
}

//==============================================================================
// Helper Methods
//==============================================================================
float MathPrecalculation::NormalizeAngle(float angle) const
{
    // Normalize angle to range [0, 2π]
    while (angle < 0.0f)
    {
        angle += 2.0f * XM_PI;
    }

    while (angle >= 2.0f * XM_PI)
    {
        angle -= 2.0f * XM_PI;
    }

    return angle;
}

int MathPrecalculation::AngleToIndex(float angle) const
{
    // Convert normalized angle [0, 2π] to table index [0, TRIG_TABLE_SIZE-1]
    int index = static_cast<int>(angle * TRIG_PRECISION_FACTOR);

    // Ensure index is within bounds
    return std::clamp(index, 0, TRIG_TABLE_SIZE - 1);
}

float MathPrecalculation::InterpolateTableValue(float value, const std::vector<float>& table, float maxValue) const
{
    // Handle edge cases
    if (table.empty())
    {
        return 0.0f;
    }

    if (value <= 0.0f)
    {
        return table[0];
    }

    if (value >= maxValue)
    {
        return table.back();
    }

    // Calculate floating-point index
    float floatIndex = (value / maxValue) * static_cast<float>(table.size() - 1);
    int lowerIndex = static_cast<int>(floatIndex);
    int upperIndex = lowerIndex + 1;

    // Ensure indices are within bounds
    lowerIndex = std::clamp(lowerIndex, 0, static_cast<int>(table.size() - 1));
    upperIndex = std::clamp(upperIndex, 0, static_cast<int>(table.size() - 1));

    // Calculate interpolation factor
    float t = floatIndex - static_cast<float>(lowerIndex);

    // Linear interpolation between table values
    return table[lowerIndex] + t * (table[upperIndex] - table[lowerIndex]);
}

template<typename T>
T MathPrecalculation::ClampValue(T value, T minVal, T maxVal) const
{
    // Template function for clamping values to valid range
    return std::clamp(value, minVal, maxVal);
}

//==============================================================================
// Compression and Checksum Optimization Methods Implementation
//==============================================================================
uint32_t MathPrecalculation::FastRotateLeft(uint32_t value, int positions) const
{
    // Ensure the system is initialized before attempting operation
    if (!m_bIsInitialized.load())
    {
        // Fallback to standard bit rotation
        positions = positions % 32; // Normalize positions to valid range
        return (value << positions) | (value >> (32 - positions));
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Fast bit rotation using optimized approach
    positions = positions % 32; // Normalize positions to valid range [0, 31]
    return (value << positions) | (value >> (32 - positions));
}

uint32_t MathPrecalculation::FastRotateRight(uint32_t value, int positions) const
{
    // Ensure the system is initialized before attempting operation
    if (!m_bIsInitialized.load())
    {
        // Fallback to standard bit rotation
        positions = positions % 32; // Normalize positions to valid range
        return (value >> positions) | (value << (32 - positions));
    }

    // Increment lookup counter for statistics
    m_lookupCount.fetch_add(1);

    // Fast bit rotation using optimized approach
    positions = positions % 32; // Normalize positions to valid range [0, 31]
    return (value >> positions) | (value << (32 - positions));
}

uint32_t MathPrecalculation::FastFNV1aHash(const void* data, size_t size) const
{
    // FNV-1a hash constants for 32-bit
    const uint32_t FNV_OFFSET_BASIS = 0x811C9DC5;
    const uint32_t FNV_PRIME = 0x01000193;

    // Initialize hash with offset basis
    uint32_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    // Process each byte of data
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];           // XOR with byte
        hash *= FNV_PRIME;          // Multiply by FNV prime
    }

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return hash;
}

uint64_t MathPrecalculation::FastFNV1aHash64(const void* data, size_t size) const
{
    // FNV-1a hash constants for 64-bit
    const uint64_t FNV_OFFSET_BASIS = 0xCBF29CE484222325ULL;
    const uint64_t FNV_PRIME = 0x100000001B3ULL;

    // Initialize hash with offset basis
    uint64_t hash = FNV_OFFSET_BASIS;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    // Process each byte of data
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];           // XOR with byte
        hash *= FNV_PRIME;          // Multiply by FNV prime
    }

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return hash;
}

uint32_t MathPrecalculation::FastModPow(uint32_t base, uint32_t exponent, uint32_t modulus) const
{
    // Handle edge cases
    if (modulus == 1) return 0;
    if (exponent == 0) return 1;

    uint32_t result = 1;
    base = base % modulus;

    // Fast modular exponentiation using binary exponentiation
    while (exponent > 0)
    {
        // If exponent is odd, multiply base with result
        if (exponent & 1)
        {
            result = (static_cast<uint64_t>(result) * base) % modulus;
        }

        // Square the base and halve the exponent
        exponent >>= 1;
        base = (static_cast<uint64_t>(base) * base) % modulus;
    }

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return result;
}

void MathPrecalculation::FastByteSwap(uint8_t* data, size_t size) const
{
    // Validate input parameters
    if (data == nullptr || size == 0)
    {
        return;
    }

    // Swap bytes from both ends moving towards center
    uint8_t* left = data;
    uint8_t* right = data + size - 1;

    while (left < right)
    {
        // Swap bytes using XOR to avoid temporary variable
        *left ^= *right;
        *right ^= *left;
        *left ^= *right;

        left++;
        right--;
    }

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }
}

uint32_t MathPrecalculation::FastCountSetBits(uint32_t value) const
{
    // Brian Kernighan's algorithm for counting set bits
    uint32_t count = 0;

    while (value)
    {
        value &= (value - 1); // Clear the lowest set bit
        count++;
    }

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return count;
}

uint32_t MathPrecalculation::FastReverseBits(uint32_t value) const
{
    uint32_t result = 0;

    // Reverse bits using divide and conquer approach
    result = ((value & 0xAAAAAAAA) >> 1) | ((value & 0x55555555) << 1);
    result = ((result & 0xCCCCCCCC) >> 2) | ((result & 0x33333333) << 2);
    result = ((result & 0xF0F0F0F0) >> 4) | ((result & 0x0F0F0F0F) << 4);
    result = ((result & 0xFF00FF00) >> 8) | ((result & 0x00FF00FF) << 8);
    result = (result >> 16) | (result << 16);

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return result;
}

//==============================================================================
// Additional Bit Manipulation Methods for Huffman Compression
//==============================================================================
uint8_t MathPrecalculation::FastCountLeadingZeros(uint32_t value) const
{
    // Count leading zeros using binary search approach
    if (value == 0) return 32;

    uint8_t count = 0;

    // Use binary search to find first set bit
    if (value <= 0x0000FFFF) { count += 16; value <<= 16; }
    if (value <= 0x00FFFFFF) { count += 8;  value <<= 8; }
    if (value <= 0x0FFFFFFF) { count += 4;  value <<= 4; }
    if (value <= 0x3FFFFFFF) { count += 2;  value <<= 2; }
    if (value <= 0x7FFFFFFF) { count += 1; }

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return count;
}

uint8_t MathPrecalculation::FastCountTrailingZeros(uint32_t value) const
{
    // Count trailing zeros using bit manipulation
    if (value == 0) return 32;

    uint8_t count = 0;

    // Use De Bruijn sequence for fast trailing zero count
    static const uint8_t deBruijnTable[32] = {
        0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
        31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
    };

    // Isolate the rightmost set bit and map it using De Bruijn sequence
    uint32_t isolated = value & (~value + 1);
    uint32_t index = (isolated * 0x077CB531U) >> 27;
    count = deBruijnTable[index];

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return count;
}

bool MathPrecalculation::FastIsPowerOfTwo(uint32_t value) const
{
    // A number is power of two if it has exactly one bit set
    bool result = (value != 0) && ((value & (value - 1)) == 0);

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return result;
}

uint32_t MathPrecalculation::FastNextPowerOfTwo(uint32_t value) const
{
    // Handle edge cases
    if (value == 0) return 1;
    if (FastIsPowerOfTwo(value)) return value;

    // Set all bits after the highest set bit
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value++;

    // Increment lookup counter for statistics
    if (m_bIsInitialized.load())
    {
        m_lookupCount.fetch_add(1);
    }

    return value;
}