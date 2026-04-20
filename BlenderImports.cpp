#include "Includes.h"
#include "BlenderImports.h"
#include "Debug.h"

#include <regex>
#include <cctype>
#include <algorithm>

extern Debug debug;

// ============================================================
// Internal helpers
// ============================================================

int BlenderImports::CountFlippedAxes(AxisFlipFlags f) noexcept
{
    int n = 0;
    if (f & FLIP_X) ++n;
    if (f & FLIP_Y) ++n;
    if (f & FLIP_Z) ++n;
    return n;
}

XMFLOAT3 BlenderImports::ApplyAxisFlip(XMFLOAT3 v, AxisFlipFlags f) noexcept
{
    if (f & FLIP_X) v.x = -v.x;
    if (f & FLIP_Y) v.y = -v.y;
    if (f & FLIP_Z) v.z = -v.z;
    return v;
}

// ============================================================
// Detection
// ============================================================

bool BlenderImports::IsBlenderFile(const std::string& generator)
{
    std::string lower = generator;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    return lower.find("blender") != std::string::npos;
}

BlenderImports::Version BlenderImports::ParseVersion(const std::string& generator)
{
    Version v;
    if (!IsBlenderFile(generator))
        return v;

    // Match "Blender X.Y.Z" — handles "Blender 4.2.1", "Blender 5.1", "Blender 3.6.0 UPBGE", etc.
    static const std::regex re(R"([Bb]lender\s+(\d+)\.(\d+)(?:\.(\d+))?)");
    std::smatch m;
    if (std::regex_search(generator, m, re))
    {
        v.major = std::stoi(m[1].str());
        v.minor = std::stoi(m[2].str());
        v.patch = (m[3].matched) ? std::stoi(m[3].str()) : 0;
        v.valid = true;
    }
    return v;
}

BlenderImports::ImportConfig BlenderImports::BuildConfig(const std::string& generator,
                                                          const json& doc)
{
    ImportConfig cfg;
    cfg.isBlenderFile = IsBlenderFile(generator);
    cfg.version       = ParseVersion(generator);

    // All GLTF files (regardless of exporter) use right-handed Y-up.
    // DirectX is left-handed Y-up.  The standard fix is to negate Z.
    cfg.flipAxes  = FLIP_Z;
    cfg.fixWinding = NeedsWindingFlip(cfg.flipAxes);

    // Detect GLB embedded images: first image with no "uri" uses a bufferView.
    if (doc.contains("images") && doc["images"].is_array() && !doc["images"].empty())
    {
        const auto& img0 = doc["images"][0];
        cfg.hasEmbeddedImages = img0.contains("bufferView") && !img0.contains("uri");
    }

    if (cfg.isBlenderFile)
    {
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[BlenderImports] Detected Blender %ls - GLTF->DX Z-flip + winding correction active.",
            cfg.version.valid ? cfg.version.ToWString().c_str() : L"(version unknown)");
    }

    return cfg;
}

// ============================================================
// Coordinate conversion — positions / normals / tangents
// ============================================================

XMFLOAT3 BlenderImports::ConvertPosition(XMFLOAT3 p, AxisFlipFlags f) noexcept
{
    return ApplyAxisFlip(p, f);
}

XMFLOAT3 BlenderImports::ConvertNormal(XMFLOAT3 n, AxisFlipFlags f) noexcept
{
    return ApplyAxisFlip(n, f);
}

XMFLOAT3 BlenderImports::ConvertTangent(XMFLOAT3 t, AxisFlipFlags f) noexcept
{
    return ApplyAxisFlip(t, f);
}

XMFLOAT3 BlenderImports::ConvertTranslation(XMFLOAT3 t, AxisFlipFlags f) noexcept
{
    return ApplyAxisFlip(t, f);
}

XMFLOAT3 BlenderImports::ConvertScale(XMFLOAT3 s, AxisFlipFlags f) noexcept
{
    // Scale magnitudes are unsigned — flip flags do not negate scale components.
    // Uniform scale is unaffected by coordinate reflection.
    (void)f;
    return s;
}

// ============================================================
// Quaternion conversion
//
// Mathematical derivation:
//   When we reflect axis A (negate A in world space), a rotation that
//   was expressed as quaternion q = (qx, qy, qz, qw) must be
//   re-expressed so that it produces the same physical rotation in the
//   new (mirrored) frame.  This equals  F * R(q) * F^-1 where F is
//   the reflection matrix.
//
//   Result per flip:
//     FLIP_X  →  negate qy, qz   (components perpendicular to X)
//     FLIP_Y  →  negate qx, qz
//     FLIP_Z  →  negate qx, qy   (the standard GLTF→DX conversion)
//
//   Multiple flips compose by applying each rule in sequence.
// ============================================================
XMFLOAT4 BlenderImports::ConvertQuat(XMFLOAT4 q, AxisFlipFlags f) noexcept
{
    float qx = q.x, qy = q.y, qz = q.z, qw = q.w;

    if (f & FLIP_X) { qy = -qy; qz = -qz; }
    if (f & FLIP_Y) { qx = -qx; qz = -qz; }
    if (f & FLIP_Z) { qx = -qx; qy = -qy; }

    return { qx, qy, qz, qw };
}

// ============================================================
// Full node-matrix conversion  (DX row-major convention)
//   M_lh = F * M_rh * F   where F = diag(fx, fy, fz, 1)
// ============================================================
XMMATRIX BlenderImports::ConvertNodeMatrix(const XMMATRIX& m, AxisFlipFlags f) noexcept
{
    const float fx = (f & FLIP_X) ? -1.0f : 1.0f;
    const float fy = (f & FLIP_Y) ? -1.0f : 1.0f;
    const float fz = (f & FLIP_Z) ? -1.0f : 1.0f;
    const XMMATRIX F = XMMatrixScaling(fx, fy, fz);
    return F * m * F;
}

// ============================================================
// Winding order
// ============================================================

bool BlenderImports::NeedsWindingFlip(AxisFlipFlags f) noexcept
{
    // An odd number of axis reflections reverses the handedness of
    // the triangle winding.
    return (CountFlippedAxes(f) % 2) != 0;
}

void BlenderImports::FixWindingOrder(std::vector<uint32_t>& indices) noexcept
{
    // Swap index 1 and index 2 of every triangle to reverse winding.
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
        std::swap(indices[i + 1], indices[i + 2]);
}

// ============================================================
// Material fixup — full GLTF 2.0 PBR extraction
//
// Blender version coverage:
//   3.0+  KHR_materials_clearcoat, KHR_materials_transmission
//   3.2+  KHR_materials_emissive_strength
//   4.0+  KHR_materials_ior, KHR_materials_transmission improvements
//   4.1+  KHR_materials_specular
//   4.2+  KHR_materials_sheen
//   4.4+  local TRS animation (handled in SceneManager)
//   5.0+  future-compatible (same extension set, additional improvements)
// ============================================================
void BlenderImports::ApplyPBRMaterial(Material& mat, const json& gltfMat,
                                       const ImportConfig& cfg)
{
    // ---- pbrMetallicRoughness block ----
    if (gltfMat.contains("pbrMetallicRoughness"))
    {
        const auto& pbr = gltfMat["pbrMetallicRoughness"];

        // baseColorFactor (linear RGBA, default [1,1,1,1])
        if (pbr.contains("baseColorFactor") &&
            pbr["baseColorFactor"].is_array() &&
            pbr["baseColorFactor"].size() >= 3)
        {
            const auto& bcf = pbr["baseColorFactor"];
            mat.Kd.x = bcf[0].get<float>();
            mat.Kd.y = bcf[1].get<float>();
            mat.Kd.z = bcf[2].get<float>();
            if (bcf.size() >= 4)
                mat.dissolve = bcf[3].get<float>();
        }

        // metallicFactor / roughnessFactor
        if (pbr.contains("metallicFactor"))
            mat.Metallic = pbr["metallicFactor"].get<float>();

        if (pbr.contains("roughnessFactor"))
            mat.Roughness = pbr["roughnessFactor"].get<float>();
    }

    // ---- Derive ambient from diffuse (PBR convention) ----
    // Only override if Ka is still at its default (0.1,0.1,0.1) grey.
    const float kaLen = sqrtf(mat.Ka.x*mat.Ka.x + mat.Ka.y*mat.Ka.y + mat.Ka.z*mat.Ka.z);
    const float kdLen = sqrtf(mat.Kd.x*mat.Kd.x + mat.Kd.y*mat.Kd.y + mat.Kd.z*mat.Kd.z);
    if (kaLen < 0.2f && kdLen > 0.01f)
    {
        mat.Ka.x = mat.Kd.x * 0.15f;
        mat.Ka.y = mat.Kd.y * 0.15f;
        mat.Ka.z = mat.Kd.z * 0.15f;
    }

    // ---- normalTexture.scale ----
    if (gltfMat.contains("normalTexture") &&
        gltfMat["normalTexture"].contains("scale"))
    {
        mat.normalScale = gltfMat["normalTexture"]["scale"].get<float>();
    }

    // ---- emissiveFactor ----
    if (gltfMat.contains("emissiveFactor") &&
        gltfMat["emissiveFactor"].is_array() &&
        gltfMat["emissiveFactor"].size() >= 3)
    {
        const auto& ef = gltfMat["emissiveFactor"];
        mat.emissiveFactor = { ef[0].get<float>(), ef[1].get<float>(), ef[2].get<float>() };
    }

    // ---- alphaCutoff / alphaMode / doubleSided ----
    if (gltfMat.contains("alphaCutoff"))
        mat.AlphaCutoff = gltfMat["alphaCutoff"].get<float>();

    if (gltfMat.contains("alphaMode"))
        mat.alphaMode = gltfMat["alphaMode"].get<std::string>();

    if (gltfMat.contains("doubleSided"))
        mat.doubleSided = gltfMat["doubleSided"].get<bool>();

    // ---- Extensions ----
    if (gltfMat.contains("extensions"))
    {
        const auto& ext = gltfMat["extensions"];

        // KHR_materials_emissive_strength  (Blender 3.2+)
        if (ext.contains("KHR_materials_emissive_strength") &&
            ext["KHR_materials_emissive_strength"].contains("emissiveStrength"))
        {
            mat.emissiveStrength =
                ext["KHR_materials_emissive_strength"]["emissiveStrength"].get<float>();
        }

        // KHR_materials_clearcoat  (Blender 3.0+)
        if (ext.contains("KHR_materials_clearcoat"))
        {
            const auto& cc = ext["KHR_materials_clearcoat"];
            if (cc.contains("clearcoatFactor"))
                mat.clearcoatFactor = cc["clearcoatFactor"].get<float>();
            if (cc.contains("clearcoatRoughnessFactor"))
                mat.clearcoatRoughness = cc["clearcoatRoughnessFactor"].get<float>();
        }

        // KHR_materials_transmission  (Blender 3.0+)
        if (ext.contains("KHR_materials_transmission"))
        {
            const auto& tr = ext["KHR_materials_transmission"];
            if (tr.contains("transmissionFactor"))
                mat.Transmission = tr["transmissionFactor"].get<float>();
        }

        // KHR_materials_ior  (Blender 4.0+)
        if (ext.contains("KHR_materials_ior"))
        {
            const auto& ior = ext["KHR_materials_ior"];
            if (ior.contains("ior"))
                mat.ior = ior["ior"].get<float>();
        }

        // KHR_materials_specular  (Blender 4.1+)
        if (ext.contains("KHR_materials_specular"))
        {
            const auto& sp = ext["KHR_materials_specular"];
            if (sp.contains("specularFactor"))
                mat.specularFactor = sp["specularFactor"].get<float>();
            if (sp.contains("specularColorFactor") &&
                sp["specularColorFactor"].is_array() &&
                sp["specularColorFactor"].size() >= 3)
            {
                const auto& scf = sp["specularColorFactor"];
                mat.specularColorFactor = {
                    scf[0].get<float>(),
                    scf[1].get<float>(),
                    scf[2].get<float>()
                };
                // Blend specular colour into Ks so the PBR shader uses it.
                mat.Ks.x = mat.Ks.x * mat.specularFactor * mat.specularColorFactor.x;
                mat.Ks.y = mat.Ks.y * mat.specularFactor * mat.specularColorFactor.y;
                mat.Ks.z = mat.Ks.z * mat.specularFactor * mat.specularColorFactor.z;
            }
        }

        // KHR_materials_sheen  (Blender 4.2+)
        if (ext.contains("KHR_materials_sheen"))
        {
            const auto& sh = ext["KHR_materials_sheen"];
            if (sh.contains("sheenColorFactor") &&
                sh["sheenColorFactor"].is_array() &&
                sh["sheenColorFactor"].size() >= 3)
            {
                const auto& shc = sh["sheenColorFactor"];
                mat.sheenColorFactor = {
                    shc[0].get<float>(),
                    shc[1].get<float>(),
                    shc[2].get<float>()
                };
            }
            if (sh.contains("sheenRoughnessFactor"))
                mat.sheenRoughness = sh["sheenRoughnessFactor"].get<float>();
        }
    }

    // Version-specific log
    if (cfg.isBlenderFile)
    {
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[BlenderImports] Material '%hs': Kd=(%.2f,%.2f,%.2f) M=%.2f R=%.2f "
            L"alpha=%hs dbl=%d emit=(%.2f,%.2f,%.2f)x%.2f",
            mat.name.c_str(),
            mat.Kd.x, mat.Kd.y, mat.Kd.z,
            mat.Metallic, mat.Roughness,
            mat.alphaMode.c_str(), (int)mat.doubleSided,
            mat.emissiveFactor.x, mat.emissiveFactor.y, mat.emissiveFactor.z,
            mat.emissiveStrength);
    }
}

// ============================================================
// Embedded-image extraction (GLB)
// ============================================================
std::vector<uint8_t> BlenderImports::ExtractEmbeddedImage(int imgIdx,
                                                            const json& doc,
                                                            const std::vector<uint8_t>& binaryChunk)
{
    if (!doc.contains("images") || !doc["images"].is_array())
        return {};

    const auto& images = doc["images"];
    if (imgIdx < 0 || imgIdx >= (int)images.size())
        return {};

    const auto& img = images[imgIdx];

    // External URI — not an embedded image.
    if (img.contains("uri"))
        return {};

    if (!img.contains("bufferView"))
        return {};

    int bvIdx = img["bufferView"].get<int>();
    if (!doc.contains("bufferViews") || !doc["bufferViews"].is_array())
        return {};

    const auto& bufferViews = doc["bufferViews"];
    if (bvIdx < 0 || bvIdx >= (int)bufferViews.size())
        return {};

    const auto& bv       = bufferViews[bvIdx];
    size_t      offset   = (size_t)bv.value("byteOffset", 0);
    size_t      length   = (size_t)bv.value("byteLength", 0);

    if (offset + length > binaryChunk.size() || length == 0)
        return {};

    return std::vector<uint8_t>(
        binaryChunk.begin() + (ptrdiff_t)offset,
        binaryChunk.begin() + (ptrdiff_t)(offset + length));
}
