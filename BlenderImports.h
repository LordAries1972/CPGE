#pragma once

// ============================================================
// BlenderImports.h  —  Blender GLTF/GLB import compatibility layer
//
// Handles all coordinate-system, winding-order, material, and
// version-specific differences for Blender 3.x through 5.x
// GLTF 2.0 / GLB exports.
//
// Coordinate system mapping:
//   GLTF spec  : right-handed, Y-up  (+X right, +Y up, -Z forward)
//   DirectX    : left-handed,  Y-up  (+X right, +Y up, +Z forward)  → negate Z
//   OpenGL     : right-handed, Y-up  (+X right, +Y up, -Z forward)  → no flip needed
//   Vulkan     : right-handed, Y-down (+X right, -Y up, +Z forward) → flip Y
//
// Usage:
//   1. Call BuildConfig() once per file after reading asset.generator.
//   2. Pass the returned ImportConfig to every conversion helper.
//   3. Call ApplyPBRMaterial() inside BindGLTFMaterialTexturesToModel.
// ============================================================

#include "Includes.h"
#include <nlohmann/json.hpp>
#include "Models.h"

using json = nlohmann::json;

// ============================================================
class BlenderImports
{
public:
    // ----------------------------------------------------------
    // Blender version parsed from asset.generator string
    // ----------------------------------------------------------
    struct Version
    {
        int  major = 0;
        int  minor = 0;
        int  patch = 0;
        bool valid = false;

        bool AtLeast(int maj, int min = 0, int pat = 0) const noexcept
        {
            if (major != maj) return major > maj;
            if (minor != min) return minor > min;
            return patch >= pat;
        }

        std::wstring ToWString() const
        {
            return std::to_wstring(major) + L"." +
                   std::to_wstring(minor) + L"." +
                   std::to_wstring(patch);
        }
    };

    // ----------------------------------------------------------
    // Axis-flip flags
    //   GLTF_TO_DX  = FLIP_Z   (negate Z for DirectX LH)
    //   GLTF_TO_GL  = FLIP_NONE (OpenGL is also RH Y-up — no flip)
    //   GLTF_TO_VK  = FLIP_Y   (Vulkan Y-axis is flipped relative to GLTF)
    // ----------------------------------------------------------
    enum AxisFlipFlags : uint32_t
    {
        FLIP_NONE = 0u,
        FLIP_X    = 1u << 0,
        FLIP_Y    = 1u << 1,
        FLIP_Z    = 1u << 2,
    };

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    static constexpr AxisFlipFlags GLTF_DEFAULT_FLIP = FLIP_Z;   // GLTF → DirectX LH
#elif defined(__USE_VULKAN__)
    static constexpr AxisFlipFlags GLTF_DEFAULT_FLIP = FLIP_Y;   // GLTF → Vulkan (Y-down)
#else
    static constexpr AxisFlipFlags GLTF_DEFAULT_FLIP = FLIP_NONE; // GLTF → OpenGL (same handedness)
#endif

    // Legacy alias kept for existing call sites
    static constexpr AxisFlipFlags GLTF_TO_DX = FLIP_Z;

    // ----------------------------------------------------------
    // Per-file import configuration
    // ----------------------------------------------------------
    struct ImportConfig
    {
        Version       version;
        AxisFlipFlags flipAxes          = GLTF_DEFAULT_FLIP;
        bool          fixWinding        = true;
        bool          isBlenderFile     = false;
        bool          hasEmbeddedImages = false;
    };

    // ---- Detection -------------------------------------------
    static bool         IsBlenderFile(const std::string& generator);
    static Version      ParseVersion (const std::string& generator);
    static ImportConfig BuildConfig  (const std::string& generator, const json& doc);

    // ---- Coordinate conversion --------------------------------
    // XMFLOAT3 aliases to Vector3 on OpenGL/Vulkan builds (Includes.h).
    static XMFLOAT3 ConvertPosition   (XMFLOAT3 p, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertNormal     (XMFLOAT3 n, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertTangent    (XMFLOAT3 t, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertTranslation(XMFLOAT3 t, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertScale      (XMFLOAT3 s, AxisFlipFlags f) noexcept;

    // Quaternion: negate components depending on axis flip.
    //   FLIP_X → negate qy, qz
    //   FLIP_Y → negate qx, qz
    //   FLIP_Z → negate qx, qy   (DX standard)
    static XMFLOAT4 ConvertQuat(XMFLOAT4 q, AxisFlipFlags f) noexcept;

    // Full 4×4 node matrix conversion.
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    static XMMATRIX ConvertNodeMatrix(const XMMATRIX& m, AxisFlipFlags f) noexcept;
#elif defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
    static Matrix4x4 ConvertNodeMatrix(const Matrix4x4& m, AxisFlipFlags f) noexcept;
#endif

    // ---- Winding order ----------------------------------------
    static bool NeedsWindingFlip(AxisFlipFlags f) noexcept;
    static void FixWindingOrder (std::vector<uint32_t>& indices) noexcept;

    // ---- Material fixup ----------------------------------------
    static void ApplyPBRMaterial(Material& mat, const json& gltfMat,
                                 const ImportConfig& cfg);

    // ---- Embedded-image extraction (GLB) ----------------------
    static std::vector<uint8_t> ExtractEmbeddedImage(int imgIdx,
                                                      const json& doc,
                                                      const std::vector<uint8_t>& binaryChunk);

    // ---- Version capability queries ---------------------------
    static bool HasLocalTRSAnimation (const Version& v) noexcept { return v.AtLeast(4, 4); }
    static bool HasEmissiveStrength   (const Version& v) noexcept { return v.AtLeast(3, 2); }
    static bool HasClearcoat          (const Version& v) noexcept { return v.AtLeast(3, 0); }
    static bool HasTransmission       (const Version& v) noexcept { return v.AtLeast(3, 0); }
    static bool HasIOR                (const Version& v) noexcept { return v.AtLeast(4, 0); }
    static bool HasSpecularExtension  (const Version& v) noexcept { return v.AtLeast(4, 1); }
    static bool HasSheen              (const Version& v) noexcept { return v.AtLeast(4, 2); }
    static bool HasReliableConvOri    (const Version& v) noexcept { return v.AtLeast(4, 0); }

private:
    BlenderImports() = delete;

    static XMFLOAT3 ApplyAxisFlip   (XMFLOAT3 v, AxisFlipFlags f) noexcept;
    static int      CountFlippedAxes(AxisFlipFlags f)              noexcept;
};
