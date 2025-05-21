#pragma once

#include <cstdint>
#include <string>
#include <sstream>

class MyColor {
public:
    uint8_t r, g, b, a;

    // Constructors
    MyColor() : r(255), g(255), b(255), a(255) {}  // Default to white
    MyColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {
    }

    // Predefined Colors
    static MyColor White() { return MyColor(255, 255, 255, 255); }
    static MyColor Black() { return MyColor(0, 0, 0, 255); }
    static MyColor Red() { return MyColor(255, 0, 0, 255); }
    static MyColor Green() { return MyColor(0, 255, 0, 255); }
    static MyColor Blue() { return MyColor(0, 0, 255, 255); }
    static MyColor Purple() { return MyColor(255, 0, 255, 255); }

    // String Conversion
    std::string ToString() const {
        std::ostringstream oss;
        oss << "Color(" << (int)r << ", " << (int)g << ", " << (int)b << ", " << (int)a << ")";
        return oss.str();
    }

    // Serialization
    std::string Serialize() const {
        std::ostringstream oss;
        oss << (int)r << "," << (int)g << "," << (int)b << "," << (int)a;
        return oss.str();
    }

    static MyColor Deserialize(const std::string& str) {
        std::istringstream iss(str);
        int r, g, b, a;
        char comma1, comma2, comma3;
        if (iss >> r >> comma1 >> g >> comma2 >> b >> comma3 >> a &&
            comma1 == ',' && comma2 == ',' && comma3 == ',') {
            return MyColor(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                static_cast<uint8_t>(b), static_cast<uint8_t>(a));
        }
        return MyColor(); // Return default if invalid
    }
};
