#pragma once

// ============================================================
// BlenderImports.h  —  Blender GLTF/GLB import compatibility layer
//
// Handles all coordinate-system, winding-order, material, and
// version-specific differences for Blender 3.x through 5.x
// GLTF 2.0 / GLB exports into a DirectX left-handed Y-up system.
//
// GLTF spec  : right-handed, Y-up  (+X right, +Y up, -Z forward)
// DirectX    : left-handed,  Y-up  (+X right, +Y up, +Z forward)
// Conversion : negate Z on positions, normals, tangents, translations;
//              adjust quaternions accordingly; reverse triangle winding.
//
// Usage:
//   1. Call BuildConfig() once per file immediately after reading the
//      asset.generator string.
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

        // True if this version is at least (maj.min.pat)
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
    // Axis-flip flags — combine with | as needed
    //   GLTF_TO_DX = FLIP_Z  (the standard conversion)
    // ----------------------------------------------------------
    enum AxisFlipFlags : uint32_t
    {
        FLIP_NONE = 0u,
        FLIP_X    = 1u << 0,
        FLIP_Y    = 1u << 1,
        FLIP_Z    = 1u << 2,
    };

    static constexpr AxisFlipFlags GLTF_TO_DX = FLIP_Z;

    // ----------------------------------------------------------
    // Per-file import configuration — built once per ParseGLTFScene /
    // ParseGLBScene call, then used throughout the loading pipeline.
    // ----------------------------------------------------------
    struct ImportConfig
    {
        Version       version;
        AxisFlipFlags flipAxes          = FLIP_Z;   // default: GLTF→DX
        bool          fixWinding        = true;      // reverse winding when flip count is odd
        bool          isBlenderFile     = false;
        bool          hasEmbeddedImages = false;     // GLB: images live in bufferViews, not URIs
    };

    // ---- Detection -------------------------------------------
    static bool         IsBlenderFile(const std::string& generator);
    static Version      ParseVersion (const std::string& generator);
    static ImportConfig BuildConfig  (const std::string& generator, const json& doc);

    // ---- Coordinate conversion --------------------------------
    // Positions, normals, tangents: apply sign flip to selected axes.
    static XMFLOAT3 ConvertPosition   (XMFLOAT3 p, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertNormal     (XMFLOAT3 n, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertTangent    (XMFLOAT3 t, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertTranslation(XMFLOAT3 t, AxisFlipFlags f) noexcept;
    static XMFLOAT3 ConvertScale      (XMFLOAT3 s, AxisFlipFlags f) noexcept;

    // Quaternion: for each flipped axis negate the OTHER two quaternion components.
    //   FLIP_X → negate qy, qz
    //   FLIP_Y → negate qx, qz
    //   FLIP_Z → negate qx, qy   (GLTF standard)
    static XMFLOAT4 ConvertQuat(XMFLOAT4 q, AxisFlipFlags f) noexcept;

    // Full 4×4 node matrix (DX row-major): applies  F * M * F^-1
    static XMMATRIX ConvertNodeMatrix(const XMMATRIX& m, AxisFlipFlags f) noexcept;

    // ---- Winding order ----------------------------------------
    // Winding must be reversed when an odd number of axes are flipped.
    static bool NeedsWindingFlip(AxisFlipFlags f) noexcept;
    static void FixWindingOrder (std::vector<uint32_t>& indices) noexcept;

    // ---- Material fixup ----------------------------------------
    // Fully extracts ALL GLTF 2.0 PBR properties (Blender 3.x–5.x)
    // and populates the Material struct.  Call this instead of the
    // per-property reads that were scattered through BindGLTFMaterialTexturesToModel.
    static void ApplyPBRMaterial(Material& mat, const json& gltfMat,
                                 const ImportConfig& cfg);

    // ---- Embedded-image extraction (GLB) ----------------------
    // Returns the raw PNG/JPEG bytes for images[imgIdx] when the
    // image uses a bufferView rather than a URI.
    // Returns empty vector if the image has a URI or is out of range.
    static std::vector<uint8_t> ExtractEmbeddedImage(int imgIdx,
                                                      const json& doc,
                                                      const std::vector<uint8_t>& binaryChunk);

    // ---- Version capability queries ---------------------------
    // Did this Blender version support the given extension?
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
