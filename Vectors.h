#pragma once

#include <cmath>
#include <string>
#include <sstream>

class Vector2 {
public:
    float x, y;

    // Constructors
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}

    // Basic Operations
    Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
    Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
    Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
    Vector2 operator/(float scalar) const { return (scalar != 0) ? Vector2(x / scalar, y / scalar) : Vector2(0, 0); }
    // Define the += operator
    Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }

    // Magnitude and Normalization
    float Magnitude() const { return std::sqrt(x * x + y * y); }
    Vector2 Normalized() const {
        float mag = Magnitude();
        return (mag > 0) ? Vector2(x / mag, y / mag) : Vector2(0, 0);
    }

    // String Conversion
    std::string ToString() const {
        std::ostringstream oss;
        oss << "Vector2(" << x << ", " << y << ")";
        return oss.str();
    }

    // Serialization
    std::string Serialize() const {
        std::ostringstream oss;
        oss << x << "," << y;
        return oss.str();
    }

    static Vector2 Deserialize(const std::string& str) {
        std::istringstream iss(str);
        float x, y;
        char comma;
        if (iss >> x >> comma >> y && comma == ',') {
            return Vector2(x, y);
        }
        return Vector2(); // Return default if invalid
    }
};

#if defined(__USE_OPENGL__)
#ifndef VECTOR3_H_INCLUDED
#define VECTOR3_H_INCLUDED
// Vector3 class for 3D vector operations (positions, normals, directions, etc.)
// Used extensively for OpenGL 3D graphics operations and transformations
class Vector3 {
public:
    float x, y, z;                                                              // Three-dimensional vector components

    // Constructors
    Vector3() : x(0), y(0), z(0) {}                                            // Default constructor - initializes to zero vector
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}                   // Parameterized constructor with all components
    Vector3(float value) : x(value), y(value), z(value) {}                     // Single value constructor - sets all components to same value

    // Conversion constructor from Vector2 (extends to 3D with z=0)
    explicit Vector3(const Vector2& v2) : x(v2.x), y(v2.y), z(0.0f) {}         // Convert Vector2 to Vector3 with default z value

    // Conversion constructor from Vector4 (projects to 3D by ignoring w)
    explicit Vector3(const Vector4& v4) : x(v4.x), y(v4.y), z(v4.z) {}         // Convert Vector4 to Vector3 by dropping w component

    // Basic arithmetic operations for vector mathematics
    Vector3 operator+(const Vector3& other) const {
        return Vector3(x + other.x, y + other.y, z + other.z);                  // Component-wise addition
    }

    Vector3 operator-(const Vector3& other) const {
        return Vector3(x - other.x, y - other.y, z - other.z);                  // Component-wise subtraction
    }

    Vector3 operator*(float scalar) const {
        return Vector3(x * scalar, y * scalar, z * scalar);                     // Scalar multiplication
    }

    Vector3 operator/(float scalar) const {
        return (scalar != 0) ? Vector3(x / scalar, y / scalar, z / scalar) : Vector3(0, 0, 0); // Scalar division with zero check
    }

    // Compound assignment operators for efficient in-place operations
    Vector3& operator+=(const Vector3& other) {
        x += other.x; y += other.y; z += other.z;
        return *this;                                                           // Component-wise addition assignment
    }

    Vector3& operator-=(const Vector3& other) {
        x -= other.x; y -= other.y; z -= other.z;
        return *this;                                                           // Component-wise subtraction assignment
    }

    Vector3& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar;
        return *this;                                                           // Scalar multiplication assignment
    }

    // Vector magnitude and normalization operations
    float Magnitude() const {
        return std::sqrt(x * x + y * y + z * z);                                // Calculate 3D vector magnitude using Euclidean norm
    }

    Vector3 Normalized() const {
        float mag = Magnitude();                                                // Get vector magnitude
        return (mag > 0) ? Vector3(x / mag, y / mag, z / mag) : Vector3(0, 0, 0); // Return normalized vector or zero if magnitude is zero
    }

    // Dot product for vector angle calculations and projections
    float Dot(const Vector3& other) const {
        return x * other.x + y * other.y + z * other.z;                         // Calculate dot product of two 3D vectors
    }

    // Cross product for surface normals and perpendicular vectors
    Vector3 Cross(const Vector3& other) const {
        return Vector3(
            y * other.z - z * other.y,                                          // Cross product X component
            z * other.x - x * other.z,                                          // Cross product Y component
            x * other.y - y * other.x                                           // Cross product Z component
        );
    }

    // Component access methods for array-style indexing
    float& operator[](int index) {
        switch (index) {                                                         // Provide array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    const float& operator[](int index) const {
        switch (index) {                                                         // Provide const array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    // String conversion for debugging and logging purposes
    std::string ToString() const {
        std::ostringstream oss;                                                 // Create string stream for formatting
        oss << "Vector3(" << x << ", " << y << ", " << z << ")";                // Format as readable string
        return oss.str();                                                       // Return formatted string
    }

    // Serialization for data persistence and network transmission
    std::string Serialize() const {
        std::ostringstream oss;                                                 // Create string stream for serialization
        oss << x << "," << y << "," << z;                                       // Format as comma-separated values
        return oss.str();                                                       // Return serialized string
    }

    // Deserialization from string data
    static Vector3 Deserialize(const std::string& str) {
        std::istringstream iss(str);                                            // Create input string stream
        float x, y, z;                                                          // Temporary storage for components
        char comma1, comma2;                                                    // Separator characters

        if (iss >> x >> comma1 >> y >> comma2 >> z &&                          // Parse components and separators
            comma1 == ',' && comma2 == ',') {                                   // Verify correct format
            return Vector3(x, y, z);                                            // Return parsed vector
        }
        return Vector3();                                                       // Return default vector if parsing fails
    }

    // Common predefined vectors for convenience
    static Vector3 Zero() { return Vector3(0.0f, 0.0f, 0.0f); }                // Zero vector
    static Vector3 One() { return Vector3(1.0f, 1.0f, 1.0f); }                 // Unit vector (all components = 1)
    static Vector3 UnitX() { return Vector3(1.0f, 0.0f, 0.0f); }               // Unit vector along X axis
    static Vector3 UnitY() { return Vector3(0.0f, 1.0f, 0.0f); }               // Unit vector along Y axis
    static Vector3 UnitZ() { return Vector3(0.0f, 0.0f, 1.0f); }               // Unit vector along Z axis
    static Vector3 Forward() { return Vector3(0.0f, 0.0f, 1.0f); }             // Forward direction vector
    static Vector3 Back() { return Vector3(0.0f, 0.0f, -1.0f); }               // Backward direction vector
    static Vector3 Up() { return Vector3(0.0f, 1.0f, 0.0f); }                  // Up direction vector
    static Vector3 Down() { return Vector3(0.0f, -1.0f, 0.0f); }               // Down direction vector
    static Vector3 Right() { return Vector3(1.0f, 0.0f, 0.0f); }               // Right direction vector
    static Vector3 Left() { return Vector3(-1.0f, 0.0f, 0.0f); }               // Left direction vector
};
#endif // VECTOR3_H_INCLUDED

#ifndef VECTOR4_H_INCLUDED
#define VECTOR4_H_INCLUDED

// Vector4 class for 4D vector operations (RGBA colors, homogeneous coordinates, etc.)
// Used primarily for OpenGL color operations and 4D transformations
class Vector4 {
public:
    float x, y, z, w;                                                           // Four-dimensional vector components

    // Constructors
    Vector4() : x(0), y(0), z(0), w(0) {}                                       // Default constructor - initializes to zero vector
    Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}     // Parameterized constructor with all components
    Vector4(float value) : x(value), y(value), z(value), w(value) {}            // Single value constructor - sets all components to same value

    // Conversion constructor from Vector2 (extends to 4D with z=0, w=1)
    explicit Vector4(const Vector2& v2) : x(v2.x), y(v2.y), z(0.0f), w(1.0f) {} // Convert Vector2 to Vector4 with default z,w values

    // Basic arithmetic operations for vector mathematics
    Vector4 operator+(const Vector4& other) const {
        return Vector4(x + other.x, y + other.y, z + other.z, w + other.w);     // Component-wise addition
    }

    Vector4 operator-(const Vector4& other) const {
        return Vector4(x - other.x, y - other.y, z - other.z, w - other.w);     // Component-wise subtraction
    }

    Vector4 operator*(float scalar) const {
        return Vector4(x * scalar, y * scalar, z * scalar, w * scalar);          // Scalar multiplication
    }

    Vector4 operator/(float scalar) const {
        return (scalar != 0) ? Vector4(x / scalar, y / scalar, z / scalar, w / scalar) : Vector4(0, 0, 0, 0); // Scalar division with zero check
    }

    // Compound assignment operators for efficient in-place operations
    Vector4& operator+=(const Vector4& other) {
        x += other.x; y += other.y; z += other.z; w += other.w;
        return *this;                                                           // Component-wise addition assignment
    }

    Vector4& operator-=(const Vector4& other) {
        x -= other.x; y -= other.y; z -= other.z; w -= other.w;
        return *this;                                                           // Component-wise subtraction assignment
    }

    Vector4& operator*=(float scalar) {
        x *= scalar; y *= scalar; z *= scalar; w *= scalar;
        return *this;                                                           // Scalar multiplication assignment
    }

    // Vector magnitude and normalization operations
    float Magnitude() const {
        return std::sqrt(x * x + y * y + z * z + w * w);                        // Calculate 4D vector magnitude using Euclidean norm
    }

    Vector4 Normalized() const {
        float mag = Magnitude();                                                // Get vector magnitude
        return (mag > 0) ? Vector4(x / mag, y / mag, z / mag, w / mag) : Vector4(0, 0, 0, 0); // Return normalized vector or zero if magnitude is zero
    }

    // Dot product for vector angle calculations and projections
    float Dot(const Vector4& other) const {
        return x * other.x + y * other.y + z * other.z + w * other.w;           // Calculate dot product of two 4D vectors
    }

    // Component access methods for array-style indexing
    float& operator[](int index) {
        switch (index) {                                                    // Provide array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        case 3: return w;                                                   // Index 3 returns w component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    const float& operator[](int index) const {
        switch (index) {                                                    // Provide const array-style access to components
        case 0: return x;                                                   // Index 0 returns x component
        case 1: return y;                                                   // Index 1 returns y component
        case 2: return z;                                                   // Index 2 returns z component
        case 3: return w;                                                   // Index 3 returns w component
        default: return x;                                                  // Default to x component for invalid indices
        }
    }

    // String conversion for debugging and logging purposes
    std::string ToString() const {
        std::ostringstream oss;                                                 // Create string stream for formatting
        oss << "Vector4(" << x << ", " << y << ", " << z << ", " << w << ")";   // Format as readable string
        return oss.str();                                                       // Return formatted string
    }

    // Serialization for data persistence and network transmission
    std::string Serialize() const {
        std::ostringstream oss;                                                 // Create string stream for serialization
        oss << x << "," << y << "," << z << "," << w;                           // Format as comma-separated values
        return oss.str();                                                       // Return serialized string
    }

    // Deserialization from string data
    static Vector4 Deserialize(const std::string& str) {
        std::istringstream iss(str);                                            // Create input string stream
        float x, y, z, w;                                                       // Temporary storage for components
        char comma1, comma2, comma3;                                            // Separator characters

        if (iss >> x >> comma1 >> y >> comma2 >> z >> comma3 >> w &&           // Parse components and separators
            comma1 == ',' && comma2 == ',' && comma3 == ',') {                  // Verify correct format
            return Vector4(x, y, z, w);                                         // Return parsed vector
        }
        return Vector4();                                                       // Return default vector if parsing fails
    }

    // Common predefined vectors for convenience
    static Vector4 Zero() { return Vector4(0.0f, 0.0f, 0.0f, 0.0f); }          // Zero vector
    static Vector4 One() { return Vector4(1.0f, 1.0f, 1.0f, 1.0f); }           // Unit vector (all components = 1)
    static Vector4 UnitX() { return Vector4(1.0f, 0.0f, 0.0f, 0.0f); }         // Unit vector along X axis
    static Vector4 UnitY() { return Vector4(0.0f, 1.0f, 0.0f, 0.0f); }         // Unit vector along Y axis
    static Vector4 UnitZ() { return Vector4(0.0f, 0.0f, 1.0f, 0.0f); }         // Unit vector along Z axis
    static Vector4 UnitW() { return Vector4(0.0f, 0.0f, 0.0f, 1.0f); }         // Unit vector along W axis
};

#endif // VECTOR4_H_INCLUDED
#endif // __USE_OPENGL__
