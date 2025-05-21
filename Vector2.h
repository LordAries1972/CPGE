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
