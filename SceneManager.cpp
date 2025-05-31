// SceneManager.cpp (continued)
#include "Includes.h"
#include "SceneManager.h"
#include "Renderer.h"
#include "DX11Renderer.h"
#include "DX_FXManager.h"
#include "Debug.h"
#include "Lights.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

extern Model models[MAX_MODELS];                                                    // Global Base Model Pool
extern Debug debug;
extern LightsManager lightsManager;
extern ThreadManager threadManager;
extern SystemUtils sysUtils;
extern FXManager fxManager;

// Abstract Renderer Pointer
extern std::shared_ptr<Renderer> renderer;

// --------------------------------------------------------------------------------------------------
// Constructor
// Initializes scene state, default type, and model registry.
// --------------------------------------------------------------------------------------------------
SceneManager::SceneManager()
{
    stSceneType = SCENE_SPLASH;

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Constructor called. Scene type set to SCENE_SPLASH.");
    #endif
}

// --------------------------------------------------------------------------------------------------
// Destructor
// Called at application shutdown to perform full scene resource cleanup.
// --------------------------------------------------------------------------------------------------
SceneManager::~SceneManager()
{
	if (bIsDestroyed) return;
    CleanUp();
	bIsDestroyed = true;
}

// --------------------------------------------------------------------------------------------------
// SceneManager::CleanUp()
// Fully resets and releases all scene-local model rendering resources.
// This does NOT touch the global models[] array, which is managed externally (e.g., main.cpp).
// --------------------------------------------------------------------------------------------------
void SceneManager::CleanUp()
{
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] CleanUp() called to release scene models.");
    #endif

    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (scene_models[i].m_isLoaded)
        {
            scene_models[i].DestroyModel();                 // Fully resets GPU buffers, shaders, textures, and internal state
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[SceneManager] scene_models[%d] Reset().", i);
            #endif
        }
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] CleanUp() completed.");
    #endif
}

bool SceneManager::Initialize(std::shared_ptr<Renderer> renderer)
{
    #if defined(__USE_DIRECTX_11__)
        std::shared_ptr<DX11Renderer> dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
        if (!dx11)
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] DX11Renderer cast failed.");
            #endif
            return false;
        }

        myRenderer = dx11.get();            // Store the renderer pointer for later use
    #endif

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Initialize() called.");
    #endif
    
    sceneFrameCounter = 0;
    return true;
}

// --------------------------------------------------------------------------------------------------
// Scene Switching Calls.
// --------------------------------------------------------------------------------------------------
void SceneManager::InitiateScene()
{
    // Set our current scene to our saved Goto (Next) Scene.
    sceneFrameCounter = 0;                                      // Reset Active Frame Counter.
    stSceneType = stOurGotoScene;
    bSceneSwitching = false;
}

void SceneManager::SetGotoScene(SceneType gotoScene)
{
    stOurGotoScene = gotoScene;
}

SceneType SceneManager::GetGotoScene()
{
    return (stOurGotoScene);
}

// --------------------------------------------------------------------------------------------------
bool SceneManager::ParseGLTFScene(const std::wstring& gltfFile)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] ParseGLTFScene() - Opening GLTF file.");
    #endif

    if (!std::filesystem::exists(gltfFile)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] File not found: %ls", gltfFile.c_str());
        #endif
        return false;
    }

    std::ifstream file(gltfFile);
    if (!file.is_open()) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to open GLTF: %ls", gltfFile.c_str());
        #endif
        return false;
    }

    json doc;
    try {
        file >> doc;
        file.close();
    }
    catch (const std::exception& ex) {
        std::wstring werror(ex.what(), ex.what() + strlen(ex.what()));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] JSON parse error: %ls", werror.c_str());
        return false;
    }

    DetectGLTFExporter(doc);
    bool isSketchfab = (m_lastDetectedExporter == L"Sketchfab");

    // --- Exporter Origin Detection ---
    m_lastDetectedExporter = L"Unknown Exporter";

    if (doc.contains("asset") && doc["asset"].is_object()) {
        const auto& asset = doc["asset"];

        if (asset.contains("generator") && asset["generator"].is_string()) {
            std::string generator = asset["generator"];
            std::string lowerGen = generator;
            std::transform(lowerGen.begin(), lowerGen.end(), lowerGen.begin(), ::tolower);

            if (lowerGen.find("blender") != std::string::npos) {
                m_lastDetectedExporter = L"Blender";  // etc.
            }
            else if (lowerGen.find("sketchfab") != std::string::npos) {
                m_lastDetectedExporter = L"Sketchfab";  // etc.
            }
            else if (lowerGen.find("obj") != std::string::npos || lowerGen.find("fbx") != std::string::npos) {
                m_lastDetectedExporter = L"Converted (OBJ/FBX)";  // etc.
            }
            else {
                m_lastDetectedExporter = std::wstring(generator.begin(), generator.end());
            }
        }
    }

#if defined(_DEBUG_SCENEMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLTF Exporter Detected: %ls", m_lastDetectedExporter.c_str());
#endif

    // Binary .bin load
    if (doc.contains("buffers") && doc["buffers"].is_array() && !doc["buffers"].empty()) {
        const auto& buffers = doc["buffers"];
        std::string uri = buffers[0].value("uri", "");
        std::filesystem::path binPath = std::filesystem::path(gltfFile).parent_path() / uri;

        std::ifstream bin(binPath, std::ios::binary);
        if (bin.is_open()) {
            bin.seekg(0, std::ios::end);
            size_t size = bin.tellg();
            bin.seekg(0);
            gltfBinaryData.resize(size);
            bin.read(reinterpret_cast<char*>(gltfBinaryData.data()), size);
            bin.close();
#if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Loaded GLTF .bin (%d bytes)", (int)size);
#endif
        }
        else {
#if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to open .bin file: %ls", binPath.c_str());
#endif
            return false;
        }
    }

    // Parse GLTF camera, lights, materials
    ParseGLTFCamera(doc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
    ParseGLTFLights(doc);
    ParseMaterialsFromGLTF(doc);

    // Build root node index list
    std::vector<int> rootNodeIndices;
    if (doc.contains("scenes") && doc["scenes"].is_array() && !doc["scenes"].empty()) 
    {
        const auto& scene0 = doc["scenes"][0];
        if (scene0.contains("nodes") && scene0["nodes"].is_array()) 
        {
            for (const auto& n : scene0["nodes"]) 
            {
                if (n.is_number_integer()) {
                    rootNodeIndices.push_back(n.get<int>());
                }
            }
        }
    }

    // Fallback if no scenes/scene[0]/nodes array is valid
    if (rootNodeIndices.empty() && doc.contains("nodes") && doc["nodes"].is_array()) 
    {
        const auto& nodes = doc["nodes"];
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
            rootNodeIndices.push_back(i);
#if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] No valid scene.nodes found. Defaulting to root-level nodes[].");
#endif
    }

    if (rootNodeIndices.empty()) 
    {
#if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] No root nodes available. Scene is empty or malformed.");
#endif
        return false;
    }

    const auto& nodes = doc["nodes"];
    int instanceIndex = 0;

    for (int nodeIndex : rootNodeIndices) 
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            continue;

        const json& rootNode = nodes[nodeIndex];
        ParseGLTFNodeRecursive(rootNode, XMMatrixIdentity(), doc, nodes, instanceIndex);
    }

#if defined(_DEBUG_SCENEMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLTF Scene Load Complete. Total Instances: %d", instanceIndex);
#endif

    return (instanceIndex > 0);
}

// --------------------------------------------------------------------------------------------------
void SceneManager::ParseGLTFNodeRecursive(const json& node, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex)
{
    if (instanceIndex >= MAX_SCENE_MODELS)
        return;

    bool hasMesh = node.contains("mesh") && node["mesh"].is_number_integer();

    // === Load and Decompose Node Transform
    XMMATRIX nodeTransform = GetNodeWorldMatrix(node);

    // Decompose for baking scale
    XMVECTOR outScale, outRot, outTrans;
    XMMatrixDecompose(&outScale, &outRot, &outTrans, nodeTransform);
    XMFLOAT3 scale;
    XMStoreFloat3(&scale, outScale);
    bool hasNonIdentityScale = (fabs(scale.x - 1.0f) > 0.0001f || fabs(scale.y - 1.0f) > 0.0001f || fabs(scale.z - 1.0f) > 0.0001f);

    if (hasMesh)
    {
        int meshIndex = node["mesh"];
        if (!doc.contains("meshes") || !doc["meshes"].is_array()) return;

        const auto& meshes = doc["meshes"];
        if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;

        std::wstring modelName = L"GLTF_Mesh_" + std::to_wstring(meshIndex);
        int modelSlot = -1;

        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.name == modelName)
            {
                modelSlot = m;
                break;
            }
        }

        if (modelSlot == -1)
        {
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name.empty())
                {
                    modelSlot = m;
                    models[m].m_modelInfo.name = modelName;
                    models[m].m_modelInfo.ID = m;
                    models[m].m_modelInfo.vertices.clear();
                    models[m].m_modelInfo.indices.clear();

                    LoadGLTFMeshPrimitives(meshIndex, doc, models[m]); // assume modular primitive loader
                    break;
                }
            }
        }

        if (modelSlot == -1) return;

        // === Bake scale into geometry if present
        if (hasNonIdentityScale)
        {
            for (auto& v : models[modelSlot].m_modelInfo.vertices)
            {
                v.position.x *= scale.x;
                v.position.y *= scale.y;
                v.position.z *= scale.z;
            }

            nodeTransform *= XMMatrixScaling(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        }

        // === Compute World Transform (after scale adjustment)
        XMMATRIX worldTransform = parentTransform * nodeTransform;

        // === Register Model to Scene Slot
        scene_models[instanceIndex].CopyFrom(models[modelSlot]);
        scene_models[instanceIndex].m_modelInfo.worldMatrix = worldTransform;

        scene_models[instanceIndex].m_modelInfo.textures = models[modelSlot].m_modelInfo.textures;
        scene_models[instanceIndex].m_modelInfo.textureSRVs = models[modelSlot].m_modelInfo.textureSRVs;
        scene_models[instanceIndex].m_modelInfo.normalMapSRVs = models[modelSlot].m_modelInfo.normalMapSRVs;

        // Extract position + scale from matrix (post-bake)
        XMStoreFloat3(&scene_models[instanceIndex].m_modelInfo.scale, XMVectorSet(1.0f, 1.0f, 1.0f, 0));
        XMFLOAT4X4 f4x4;
        XMStoreFloat4x4(&f4x4, worldTransform);
        scene_models[instanceIndex].m_modelInfo.position = XMFLOAT3(f4x4._41, f4x4._42, f4x4._43);
        scene_models[instanceIndex].m_modelInfo.ID = instanceIndex;
        scene_models[instanceIndex].m_modelInfo.name = L"Node_" + std::to_wstring(instanceIndex) + L"_Mesh_" + std::to_wstring(meshIndex);

        scene_models[instanceIndex].SetupModelForRendering(scene_models[instanceIndex].m_modelInfo.ID);
        scene_models[instanceIndex].ApplyDefaultLightingFromManager(lightsManager);
#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] scene_models[%d] lighting: %d local lights applied.",
            instanceIndex,
            (int)scene_models[instanceIndex].m_modelInfo.localLights.size());
#endif

        scene_models[instanceIndex].m_isLoaded = true;
        scene_models[instanceIndex].bIsDestroyed = false;

        // === Exporter-specific patch logic ===
        const std::wstring& exp = m_lastDetectedExporter;

        if (exp == L"OBJ2GLTF" || exp == L"FBX2GLTF")
        {
            // Force positive scale to avoid mirrored models
            scene_models[instanceIndex].m_modelInfo.scale.x = std::abs(scene_models[instanceIndex].m_modelInfo.scale.x);
            scene_models[instanceIndex].m_modelInfo.scale.y = std::abs(scene_models[instanceIndex].m_modelInfo.scale.y);
            scene_models[instanceIndex].m_modelInfo.scale.z = std::abs(scene_models[instanceIndex].m_modelInfo.scale.z);

#if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] OBJ2GLTF/FBX2GLTF patch: absolute scale enforced.");
#endif
        }
        else if (exp == L"Blender")
        {
            // Optional: placeholder for Blender-specific patching
#if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Blender scene loaded. No patch applied.");
#endif
        }

#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] scene_models[%d] created: \"%ls\" | Pos(%.2f, %.2f, %.2f) | Scale baked",
            instanceIndex,
            scene_models[instanceIndex].m_modelInfo.name.c_str(),
            f4x4._41, f4x4._42, f4x4._43);
#endif

        ++instanceIndex;
    }

    // === Process Children
    if (node.contains("children") && node["children"].is_array())
    {
        for (const auto& childIndex : node["children"])
        {
            if (!childIndex.is_number_integer()) continue;
            int ci = childIndex.get<int>();
            if (ci < 0 || ci >= (int)allNodes.size()) continue;

            ParseGLTFNodeRecursive(allNodes[ci], parentTransform * nodeTransform, doc, allNodes, instanceIndex);
        }
    }
}

// --------------------------------------------------------------------------------------------------
#include <unordered_map>

void SceneManager::LoadGLTFMeshPrimitives(int meshIndex, const json& doc, Model& model)
{
    if (!doc.contains("meshes") || !doc.contains("accessors") || !doc.contains("bufferViews")) return;

    const auto& meshes = doc["meshes"];
    const auto& accessors = doc["accessors"];
    const auto& bufferViews = doc["bufferViews"];

    if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) return;
    const auto& mesh = meshes[meshIndex];

    if (!mesh.contains("primitives")) return;

    for (const auto& prim : mesh["primitives"])
    {
        if (!prim.contains("attributes")) continue;
        const auto& attributes = prim["attributes"];
        int posAccessor = attributes.value("POSITION", -1);
        int idxAccessor = prim.value("indices", -1);

        if (posAccessor < 0 || posAccessor >= (int)accessors.size() || idxAccessor < 0 || idxAccessor >= (int)accessors.size())
            continue;

        // Load RAW vertices
        const auto& posAcc = accessors[posAccessor];
        int posViewIdx = posAcc["bufferView"];
        size_t posOffset = bufferViews[posViewIdx].value("byteOffset", 0) + posAcc.value("byteOffset", 0);
        int vertexCount = posAcc["count"];

        std::vector<Vertex> rawVertices(vertexCount);
        for (int vi = 0; vi < vertexCount; ++vi)
        {
            rawVertices[vi].position.x = *reinterpret_cast<const float*>(&gltfBinaryData[posOffset + vi * 12 + 0]);
            rawVertices[vi].position.y = *reinterpret_cast<const float*>(&gltfBinaryData[posOffset + vi * 12 + 4]);
            rawVertices[vi].position.z = *reinterpret_cast<const float*>(&gltfBinaryData[posOffset + vi * 12 + 8]);
            rawVertices[vi].normal = XMFLOAT3(0, 1, 0);
            rawVertices[vi].texCoord = XMFLOAT2(0, 0);
        }

        // Load NORMAL if present
        if (attributes.contains("NORMAL"))
        {
            int normAcc = attributes["NORMAL"];
            const auto& norm = accessors[normAcc];
            int normView = norm["bufferView"];
            size_t normOffset = bufferViews[normView].value("byteOffset", 0) + norm.value("byteOffset", 0);

            for (int vi = 0; vi < vertexCount; ++vi)
            {
                rawVertices[vi].normal.x = *reinterpret_cast<const float*>(&gltfBinaryData[normOffset + vi * 12 + 0]);
                rawVertices[vi].normal.y = *reinterpret_cast<const float*>(&gltfBinaryData[normOffset + vi * 12 + 4]);
                rawVertices[vi].normal.z = *reinterpret_cast<const float*>(&gltfBinaryData[normOffset + vi * 12 + 8]);
            }
        }

        // Load TEXCOORD if present
        if (attributes.contains("TEXCOORD_0"))
        {
            int texAcc = attributes["TEXCOORD_0"];
            const auto& tex = accessors[texAcc];
            int texView = tex["bufferView"];
            size_t texOffset = bufferViews[texView].value("byteOffset", 0) + tex.value("byteOffset", 0);

            for (int vi = 0; vi < vertexCount; ++vi)
            {
                rawVertices[vi].texCoord.x = *reinterpret_cast<const float*>(&gltfBinaryData[texOffset + vi * 8 + 0]);
                rawVertices[vi].texCoord.y = *reinterpret_cast<const float*>(&gltfBinaryData[texOffset + vi * 8 + 4]);
            }
        }

        // Load RAW indices
        const auto& idxAcc = accessors[idxAccessor];
        int idxViewIdx = idxAcc["bufferView"];
        int idxCount = idxAcc["count"];
        int idxComponentType = idxAcc["componentType"];
        size_t idxOffset = bufferViews[idxViewIdx].value("byteOffset", 0) + idxAcc.value("byteOffset", 0);

        std::vector<uint32_t> rawIndices(idxCount);

        for (int k = 0; k < idxCount; ++k)
        {
            switch (idxComponentType)
            {
            case 5121: rawIndices[k] = gltfBinaryData[idxOffset + k]; break; // UNSIGNED_BYTE
            case 5123: rawIndices[k] = *reinterpret_cast<const uint16_t*>(&gltfBinaryData[idxOffset + k * 2]); break; // UNSIGNED_SHORT
            case 5125: rawIndices[k] = *reinterpret_cast<const uint32_t*>(&gltfBinaryData[idxOffset + k * 4]); break; // UNSIGNED_INT
            }
        }

        // === Weld Now After Having RawVertices + RawIndices
        struct VertexKey
        {
            XMFLOAT3 pos, norm;
            XMFLOAT2 uv;

            bool operator==(const VertexKey& other) const
            {
                return memcmp(this, &other, sizeof(VertexKey)) == 0;
            }
        };

        struct VertexKeyHasher
        {
            size_t operator()(const VertexKey& key) const
            {
                size_t h1 = std::hash<float>()(key.pos.x) ^ std::hash<float>()(key.pos.y) ^ std::hash<float>()(key.pos.z);
                size_t h2 = std::hash<float>()(key.norm.x) ^ std::hash<float>()(key.norm.y) ^ std::hash<float>()(key.norm.z);
                size_t h3 = std::hash<float>()(key.uv.x) ^ std::hash<float>()(key.uv.y);
                return h1 ^ h2 ^ h3;
            }
        };

        std::unordered_map<VertexKey, uint32_t, VertexKeyHasher> uniqueVerts;
        model.m_modelInfo.vertices.clear();
        model.m_modelInfo.indices.clear();

        for (auto idx : rawIndices)
        {
            const Vertex& v = rawVertices[idx];
            VertexKey key{ v.position, v.normal, v.texCoord };

            auto it = uniqueVerts.find(key);
            if (it != uniqueVerts.end())
            {
                model.m_modelInfo.indices.push_back(it->second);
            }
            else
            {
                uint32_t newIndex = static_cast<uint32_t>(model.m_modelInfo.vertices.size());
                uniqueVerts[key] = newIndex;
                model.m_modelInfo.vertices.push_back(v);
                model.m_modelInfo.indices.push_back(newIndex);
            }
        }

        // === Generate Tangents if TEXCOORDs present
        if (!model.m_modelInfo.vertices.empty() && !model.m_modelInfo.indices.empty())
        {
            std::vector<XMFLOAT3> tangentAccum(model.m_modelInfo.vertices.size(), XMFLOAT3(0, 0, 0));

            for (size_t i = 0; i < model.m_modelInfo.indices.size(); i += 3)
            {
                uint32_t i0 = model.m_modelInfo.indices[i];
                uint32_t i1 = model.m_modelInfo.indices[i + 1];
                uint32_t i2 = model.m_modelInfo.indices[i + 2];

                const Vertex& v0 = model.m_modelInfo.vertices[i0];
                const Vertex& v1 = model.m_modelInfo.vertices[i1];
                const Vertex& v2 = model.m_modelInfo.vertices[i2];

                XMVECTOR p0 = XMLoadFloat3(&v0.position);
                XMVECTOR p1 = XMLoadFloat3(&v1.position);
                XMVECTOR p2 = XMLoadFloat3(&v2.position);

                float du1 = v1.texCoord.x - v0.texCoord.x;
                float dv1 = v1.texCoord.y - v0.texCoord.y;
                float du2 = v2.texCoord.x - v0.texCoord.x;
                float dv2 = v2.texCoord.y - v0.texCoord.y;

                XMVECTOR deltaPos1 = p1 - p0;
                XMVECTOR deltaPos2 = p2 - p0;

                float r = (du1 * dv2 - du2 * dv1);
                r = (fabs(r) < 1e-8f) ? 1.0f : 1.0f / r;

                XMVECTOR tangent = (deltaPos1 * dv2 - deltaPos2 * dv1) * r;

                XMFLOAT3 tan;
                XMStoreFloat3(&tan, tangent);

                tangentAccum[i0].x += tan.x; tangentAccum[i0].y += tan.y; tangentAccum[i0].z += tan.z;
                tangentAccum[i1].x += tan.x; tangentAccum[i1].y += tan.y; tangentAccum[i1].z += tan.z;
                tangentAccum[i2].x += tan.x; tangentAccum[i2].y += tan.y; tangentAccum[i2].z += tan.z;
            }

            for (size_t i = 0; i < model.m_modelInfo.vertices.size(); ++i)
            {
                XMVECTOR tan = XMLoadFloat3(&tangentAccum[i]);
                tan = XMVector3Normalize(tan);
                XMStoreFloat3(&model.m_modelInfo.vertices[i].tangent, tan);
            }
        }

        // Handle material if available
        if (prim.contains("material"))
        {
            int matIndex = prim["material"];
            BindGLTFMaterialTexturesToModel(matIndex, model.m_modelInfo, model, doc);
        }
    }
}

// --------------------------------------------------------------------------------------------------
XMMATRIX SceneManager::GetNodeWorldMatrix(const json& node)
{
    bool hasValidTransform = false;
    XMMATRIX S = XMMatrixIdentity();
    XMMATRIX R = XMMatrixIdentity();
    XMMATRIX T = XMMatrixIdentity();

    // === Full Matrix override
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16)
    {
        XMFLOAT4X4 mtx{};
        for (int i = 0; i < 16; ++i)
        {
            if (!node["matrix"][i].is_number())
                return XMMatrixIdentity();

            ((float*)&mtx)[i] = node["matrix"][i].get<float>();
        }
        return XMLoadFloat4x4(&mtx);
    }

    // === Scale
    if (node.contains("scale") && node["scale"].is_array()) {
        const auto& s = node["scale"];
        if (s.size() == 3 && s[0].is_number() && s[1].is_number() && s[2].is_number()) {
            float sx = s[0].get<float>();
            float sy = s[1].get<float>();
            float sz = s[2].get<float>();
            S = XMMatrixScaling(sx, sy, sz);
            hasValidTransform = true;
        }
    }

    // === Rotation
    if (node.contains("rotation") && node["rotation"].is_array()) {
        const auto& r = node["rotation"];
        if (r.size() == 4 && r[0].is_number() && r[1].is_number() && r[2].is_number() && r[3].is_number()) {
            float qx = r[0].get<float>();
            float qy = r[1].get<float>();
            float qz = r[2].get<float>();
            float qw = r[3].get<float>();
            XMVECTOR quat = XMVectorSet(qx, qy, qz, qw);
            R = XMMatrixRotationQuaternion(quat);
            hasValidTransform = true;
        }
    }

    // === Translation
    if (node.contains("translation") && node["translation"].is_array()) {
        const auto& t = node["translation"];
        if (t.size() == 3 && t[0].is_number() && t[1].is_number() && t[2].is_number()) {
            float tx = t[0].get<float>();
            float ty = t[1].get<float>();
            float tz = t[2].get<float>();
            T = XMMatrixTranslation(tx, ty, tz);
            hasValidTransform = true;

#if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[SceneManager] Translation Parsed = (%.3f, %.3f, %.3f)", tx, ty, tz);
#endif
        }
    }

    if (!hasValidTransform)
    {
#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Node has no transform. Using identity.");
#endif
    }

    XMMATRIX finalMatrix = T * R * S;
    XMFLOAT4X4 dbgMatrix;
    XMStoreFloat4x4(&dbgMatrix, finalMatrix);

    #if defined(_DEBUG_SCENEMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[SceneManager] Node TRS → Translation=(%.3f, %.3f, %.3f), Scale=(%.3f, %.3f, %.3f)",
        dbgMatrix._41, dbgMatrix._42, dbgMatrix._43,
        dbgMatrix._11, dbgMatrix._22, dbgMatrix._33);
    #endif

    return finalMatrix;
}

// --------------------------------------------------------------------------------------------------
bool SceneManager::ParseMaterialsFromGLTF(const json& doc)
{
    if (!doc.contains("materials") || !doc["materials"].is_array())
        return false;

#if defined(_DEBUG_SCENEMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Parsing GLTF materials[] array.");
#endif

    const auto& materials = doc["materials"];
    for (size_t i = 0; i < materials.size(); ++i)
    {
        const auto& mat = materials[i];

#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[SceneManager] Material[%d]", (int)i);
#endif

        if (mat.contains("pbrMetallicRoughness"))
        {
            const auto& pbr = mat["pbrMetallicRoughness"];
            if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array())
            {
                const auto& color = pbr["baseColorFactor"];
                float r = color[0].get<float>();
                float g = color[1].get<float>();
                float b = color[2].get<float>();
                float a = color[3].get<float>();
#if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  BaseColorFactor: RGBA(%f, %f, %f, %f)", r, g, b, a);
#endif
            }

            if (pbr.contains("metallicFactor"))
            {
                float metallic = pbr["metallicFactor"].get<float>();
#if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  MetallicFactor: %f", metallic);
#endif
            }

            if (pbr.contains("roughnessFactor"))
            {
                float roughness = pbr["roughnessFactor"].get<float>();
#if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  RoughnessFactor: %f", roughness);
#endif
            }

            if (pbr.contains("baseColorTexture"))
            {
                int texIndex = pbr["baseColorTexture"]["index"].get<int>();
#if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  Albedo Texture Index: %d", texIndex);
#endif
            }
        }

        if (mat.contains("alphaMode"))
        {
            std::string mode = mat["alphaMode"].get<std::string>();
#if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  AlphaMode: %hs", mode.c_str());
#endif
        }

        if (mat.contains("alphaCutoff"))
        {
            float cutoff = mat["alphaCutoff"].get<float>();
#if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"  AlphaCutoff: %f", cutoff);
#endif
        }
    }

    return true;
}

// --------------------------------------------------------------------------------------------------
void SceneManager::BindGLTFMaterialTexturesToModel(int materialIndex, ModelInfo& info, Model& model, const json& doc)
{
    if (!doc.contains("materials") || !doc.contains("textures") || !doc.contains("images"))
        return;

    const auto& materials = doc["materials"];
    const auto& textures = doc["textures"];
    const auto& images = doc["images"];

    if (materialIndex < 0 || materialIndex >= (int)materials.size())
        return;

    const auto& mat = materials[materialIndex];
    Material newMat;
    newMat.name = mat.contains("name") ? mat["name"].get<std::string>() : "Material" + std::to_string(materialIndex);

    bool hasDiffuseTexture = false;

    if (mat.contains("pbrMetallicRoughness"))
    {
        const auto& pbr = mat["pbrMetallicRoughness"];
        if (pbr.contains("baseColorTexture"))
        {
            int texIndex = pbr["baseColorTexture"].value("index", -1);
            if (texIndex >= 0 && texIndex < (int)textures.size())
            {
                int imgIndex = textures[texIndex].value("source", -1);
                if (imgIndex >= 0 && imgIndex < (int)images.size())
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    std::wstring wuri = sysUtils.StripQuotes(sysUtils.ToWString(uri));
                    std::filesystem::path fullTexPath = AssetsDir / wuri;

                    auto tex = std::make_shared<Texture>();
                    if (tex->LoadFromFile(fullTexPath))
                    {
                        info.textures.push_back(tex);
                        info.textureSRVs.push_back(tex->GetSRV());
                        newMat.diffuseTexture = tex;
                        newMat.diffuseMapPath = uri;
                        hasDiffuseTexture = true;

                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_INFO,
                                L"[SceneManager] Model[%d] material[%d] → Albedo: %ls",
                                info.ID, materialIndex, fullTexPath.c_str());
                        #endif
                    }
                }
            }
        }
    }

    // === Fallback: Assign Default White Texture if Diffuse Not Found ===
    if (!hasDiffuseTexture)
    {
        auto fallbackTex = std::make_shared<Texture>();
        fallbackTex->CreateSolidColorTexture(1, 1, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f)); // 1x1 pure white texture

        info.textures.push_back(fallbackTex);
        info.textureSRVs.push_back(fallbackTex->GetSRV());
        newMat.diffuseTexture = fallbackTex;
        newMat.diffuseMapPath = "DEFAULT_WHITE";

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[SceneManager] Model[%d] material[%d] → Assigned Default White Diffuse Texture.",
                info.ID, materialIndex);
        #endif
    }

    // Load Normal Map (optional)
    if (mat.contains("normalTexture"))
    {
        int texIndex = mat["normalTexture"].value("index", -1);
        if (texIndex >= 0 && texIndex < (int)textures.size())
        {
            int imgIndex = textures[texIndex].value("source", -1);
            if (imgIndex >= 0 && imgIndex < (int)images.size())
            {
                std::string uri = images[imgIndex].value("uri", "");
                std::wstring wuri = sysUtils.StripQuotes(sysUtils.ToWString(uri));
                std::filesystem::path fullTexPath = AssetsDir / wuri;

                auto tex = std::make_shared<Texture>();
                if (tex->LoadFromFile(fullTexPath))
                {
                    info.textures.push_back(tex);
                    info.normalMapSRVs.push_back(tex->GetSRV());
                    newMat.normalMap = tex;
                    newMat.normalMapPath = uri;

                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] Model[%d] material[%d] → Normal Map: %ls",
                            info.ID, materialIndex, fullTexPath.c_str());
                    #endif
                }
            }
        }
    }

    info.materials.push_back(newMat.name);
    model.m_materials[newMat.name] = newMat;
}

// ============================================================================
// ParseGLTFCamera(...) - Applies GLTF camera to DX11Renderer via macros
// ============================================================================
void SceneManager::ParseGLTFCamera(const nlohmann::json& gltf, Camera& camera, float windowWidth, float windowHeight)
{
    try
    {
        bGltfCameraParsed = false;
        if (!gltf.contains("nodes") || !gltf.contains("cameras"))
        {
#if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[GLTF]: No cameras or nodes found. Reverting to SetupDefaultCamera().");
#endif
    		camera.SetupDefaultCamera(windowWidth, windowHeight);

            return;
        }

        const auto& nodes = gltf["nodes"];
        const auto& cameras = gltf["cameras"];
        int cameraNodeIndex = -1;

        // Find first node that references a camera
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i].contains("camera"))
            {
                cameraNodeIndex = static_cast<int>(i);
                break;
            }
        }

        if (cameraNodeIndex == -1)
        {
#if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[GLTF]: No camera node found. Reverting to SetupDefaultCamera().");
#endif
            camera.SetupDefaultCamera(windowWidth, windowHeight);
            return;
        }

        const auto& node = nodes[cameraNodeIndex];
        int camIndex = node.value("camera", -1);

        if (camIndex < 0 || camIndex >= (int)cameras.size())
        {
#if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[GLTF]: Invalid camera index (%d). Reverting to default.", camIndex);
#endif
            camera.SetupDefaultCamera(windowWidth, windowHeight);
            return;
        }

        const auto& cam = cameras[camIndex];
        if (!cam.contains("type") || cam["type"] != "perspective" || !cam.contains("perspective"))
        {
#if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[GLTF]: Unsupported camera type or missing perspective.");
#endif
            camera.SetupDefaultCamera(windowWidth, windowHeight);
            return;
        }

        // --- Projection Parameters
        const auto& persp = cam["perspective"];
        float yfov = persp.value("yfov", 0.785f); // Default ~45°
        yfov = std::clamp(yfov, XMConvertToRadians(30.0f), XMConvertToRadians(90.0f));

        float nearZ = persp.value("znear", 0.1f);
        if (nearZ < 0.01f) nearZ = 0.01f;

        float farZ = persp.value("zfar", 1000.0f);
        if (farZ < nearZ + 1.0f) farZ = nearZ + 1000.0f;

        float aspect = persp.value("aspectRatio", windowWidth / windowHeight);

        camera.SetProjectionMatrix(XMMatrixPerspectiveFovLH(yfov, aspect, nearZ, farZ));

        // --- Eye Position
        XMFLOAT3 eyePos = { 0.0f, 0.0f, -5.0f };
        if (node.contains("translation") && node["translation"].is_array())
        {
            eyePos.x = node["translation"][0];
            eyePos.y = node["translation"][1];
            eyePos.z = node["translation"][2];

            if (m_lastDetectedExporter == L"Sketchfab")
            {
                eyePos.x *= 0.01f;
                eyePos.y *= 0.01f;
                eyePos.z *= 0.01f;

#if defined(_DEBUG_CAMERA_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Sketchfab Camera: Applied 0.01 scale to eye position.");
#endif
            }
        }

        XMVECTOR eye = XMVectorSet(eyePos.x, eyePos.y, eyePos.z, 1.0f);
        XMVECTOR target = XMVectorSet(0.0f, 0.01f, 0.0f, 0.0f); // Default forward
        XMVECTOR forward;

        // --- Rotation → Forward Vector
        if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() == 4)
        {
            float qx = node["rotation"][0];
            float qy = node["rotation"][1];
            float qz = node["rotation"][2];
            float qw = node["rotation"][3];

            XMVECTOR quat = XMVectorSet(qx, qy, qz, qw);
            XMMATRIX rotMatrix = XMMatrixRotationQuaternion(quat);
            forward = XMVector3TransformNormal(XMVectorSet(0, 0, -1, 0), rotMatrix);
            target = XMVectorAdd(eye, forward);

#if defined(_DEBUG_CAMERA_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTF CAMERA] Forward Quaternion = (%.3f, %.3f, %.3f, %.3f)", qx, qy, qz, qw);
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTF CAMERA] EyePos = (%.3f, %.3f, %.3f)", eyePos.x, eyePos.y, eyePos.z);
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTF CAMERA] Forward Vector = (%.3f, %.3f, %.3f)",
                XMVectorGetX(forward), XMVectorGetY(forward), XMVectorGetZ(forward));
#endif
        }
        else
        {
#if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[GLTF CAMERA] Missing rotation quaternion, using default forward.");
#endif
            target = XMVectorAdd(eye, target);
        }

        if (m_lastDetectedExporter == L"Sketchfab")
        {
            // Rotate forward vector +90°X to match scene up direction (fix look-at)
            XMMATRIX fixRot = XMMatrixRotationX(XMConvertToRadians(90.0f));
            forward = XMVector3TransformNormal(forward, fixRot);
            forward = XMVectorScale(forward, 0.01f);                    // scale camera's forward distance too
            target = XMVectorAdd(eye, forward);

#if defined(_DEBUG_CAMERA_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Sketchfab camera forward vector rotated +90°X to match model patch.");
#endif
        }

        // --- Final View Matrix
        XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        XMMATRIX view = XMMatrixLookAtLH(eye, target, upVec);

        // --- FIX: Enforce GLTF start position and orientation
        camera.viewMatrix = view;
        camera.position = eyePos;
        camera.target = XMFLOAT3(XMVectorGetX(target), XMVectorGetY(target), XMVectorGetZ(target));
        // DO NOT call UpdateViewMatrix() here, it would override GLTF settings
        bGltfCameraParsed = true;
    }
    catch (const std::exception& ex)
    {
        std::string msg = ex.what();
        std::wstring wmsg(msg.begin(), msg.end());
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] ParseGLTFCamera() Exception: %ls", wmsg.c_str());
        camera.SetupDefaultCamera(windowWidth, windowHeight);
    }
}

// ---------------------------------------------------------------------------------
bool SceneManager::ParseGLTFLights(const json& doc)
{
    if (!doc.contains("extensions") || !doc["extensions"].contains("KHR_lights_punctual"))
        return false;

#if defined(_DEBUG_SCENEMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Parsing KHR_lights_punctual extension.");
#endif

    const auto& ext = doc["extensions"]["KHR_lights_punctual"];
    const auto& lights = ext["lights"];
    const auto& nodes = doc["nodes"];

    std::vector<LightStruct> parsedLights;
    std::vector<bool> lightUsed(lights.size(), false);

    // --- Parse each light definition ---
    for (const auto& light : lights)
    {
        LightStruct out = {};
        out.active = 1;

        std::string type = light.value("type", "point");
        if (type == "point")            out.type = int(LightType::POINT);
        else if (type == "spot")        out.type = int(LightType::SPOT);
        else if (type == "directional") out.type = int(LightType::DIRECTIONAL);
        else continue;

        auto colorArray = light.value("color", std::vector<float>{1.0f, 1.0f, 1.0f});
        out.color = XMFLOAT3(colorArray[0], colorArray[1], colorArray[2]);
        out.intensity = light.value("intensity", 1.0f);
        out.range = light.value("range", 1000.0f);

        if (out.type == int(LightType::SPOT) && light.contains("spot"))
        {
            const auto& spot = light["spot"];
            out.innerCone = spot.value("innerConeAngle", 0.0f);
            out.outerCone = spot.value("outerConeAngle", XM_PIDIV4);
        }

        parsedLights.push_back(out);
    }

    // --- Match node-bound lights ---
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& node = nodes[i];
        if (!node.contains("extensions")) continue;

        const auto& nodeExt = node["extensions"];
        if (!nodeExt.contains("KHR_lights_punctual")) continue;

        int lightIndex = nodeExt["KHR_lights_punctual"].value("light", -1);
        if (lightIndex < 0 || lightIndex >= (int)parsedLights.size()) continue;

        lightUsed[lightIndex] = true;
        LightStruct lref = parsedLights[lightIndex];

        // --- Set position from node transform ---
        XMMATRIX nodeMatrix = GetNodeWorldMatrix(node);
        XMFLOAT4X4 xf;
        XMStoreFloat4x4(&xf, nodeMatrix);

        lref.position = XMFLOAT3(xf._41, xf._42, xf._43);

        // --- Set direction for directional or spot lights ---
        if (lref.type == int(LightType::DIRECTIONAL) || lref.type == int(LightType::SPOT))
        {
            XMVECTOR defaultForward = XMVectorSet(0, 0, -1, 0);
            XMVECTOR worldDir = XMVector3TransformNormal(defaultForward, nodeMatrix);
            XMStoreFloat3(&lref.direction, worldDir);
        }
        else
        {
            lref.direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
        }

        // --- Register Light ---
        std::wstring lightName = L"GLTF_Light_" + std::to_wstring(lightIndex);
        lightsManager.CreateLight(lightName, lref);

#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Light[%d] Bound: Type=%d Pos=(%.2f, %.2f, %.2f) Dir=(%.2f, %.2f, %.2f) Color=(%.2f, %.2f, %.2f)",
            (int)lightIndex, lref.type,
            lref.position.x, lref.position.y, lref.position.z,
            lref.direction.x, lref.direction.y, lref.direction.z,
            lref.color.x, lref.color.y, lref.color.z);
#endif
    }

    // --- Register unbound lights as globals ---
    for (size_t i = 0; i < parsedLights.size(); ++i)
    {
        if (lightUsed[i]) continue;

        LightStruct lref = parsedLights[i];
        lref.position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        lref.direction = XMFLOAT3(0.0f, 0.0f, -1.0f);

        std::wstring lightName = L"GLTF_Light_" + std::to_wstring(i);
        lightsManager.CreateLight(lightName, lref);

#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Light[%d] Unbound: Defaulted to origin and forward.", (int)i);
#endif
    }

#if defined(_DEBUG_SCENEMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"[SceneManager] ParseGLTFLights() completed. Total lights created: %d", (int)parsedLights.size());
#endif

    return !parsedLights.empty();
}

// --------------------------------------------------------------------------------------------------
void SceneManager::AutoFrameSceneToCamera(float fovYRadians, float padding)
{
    using namespace DirectX;

    XMFLOAT3 sceneMin = { FLT_MAX, FLT_MAX, FLT_MAX };
    XMFLOAT3 sceneMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    bool foundVerts = false;

    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (!scene_models[i].m_isLoaded) continue;

        const auto& verts = scene_models[i].m_modelInfo.vertices;
        XMMATRIX wm = scene_models[i].m_modelInfo.worldMatrix;

        for (const auto& v : verts)
        {
            XMVECTOR pos = XMLoadFloat3(&v.position);
            pos = XMVector3TransformCoord(pos, wm);
            XMFLOAT3 worldPos;
            XMStoreFloat3(&worldPos, pos);

            sceneMin.x = std::min(sceneMin.x, worldPos.x);
            sceneMin.y = std::min(sceneMin.y, worldPos.y);
            sceneMin.z = std::min(sceneMin.z, worldPos.z);
            sceneMax.x = std::max(sceneMax.x, worldPos.x);
            sceneMax.y = std::max(sceneMax.y, worldPos.y);
            sceneMax.z = std::max(sceneMax.z, worldPos.z);

            foundVerts = true;
        }
    }

    if (!foundVerts) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] AutoFrameSceneToCamera(): No models with geometry.");
        return;
    }

    // Calculate scene center and bounding radius
    XMFLOAT3 center = {
        (sceneMin.x + sceneMax.x) * 0.5f,
        (sceneMin.y + sceneMax.y) * 0.5f,
        (sceneMin.z + sceneMax.z) * 0.5f
    };

    XMVECTOR vCenter = XMLoadFloat3(&center);
    XMVECTOR vCorner = XMLoadFloat3(&sceneMax);
    float radius = XMVectorGetX(XMVector3Length(vCorner - vCenter)) * padding;

    float distance = radius / std::tan(fovYRadians * 0.5f);

    // Move camera along +Z or -Z depending on your default forward vector
    XMFLOAT3 camPos;
    camPos = { center.x, center.y, center.z - distance };

    // Update camera
    if (myRenderer) {
        if (!myRenderer->wasResizing.load())
        {
            myRenderer->myCamera.SetPosition(camPos.x, camPos.y, camPos.z);
            myRenderer->myCamera.SetTarget(center);
            myRenderer->myCamera.SetNearFar(0.1f, std::max(1000.0f, radius * 5.0f));
        }

#if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] Auto-framed camera at distance %.2f. Scene center: (%.2f, %.2f, %.2f)",
            distance, center.x, center.y, center.z);
#endif
    }
}

// --------------------------------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------------------------------
bool SceneManager::IsSketchfabScene() const
{
	return m_lastDetectedExporter == L"Sketchfab";
}

const std::wstring& SceneManager::GetLastDetectedExporter() const
{
    return m_lastDetectedExporter;
}

// --------------------------------------------------------------------------------------------------
void SceneManager::DetectGLTFExporter(const nlohmann::json& doc)
{
    m_lastDetectedExporter = L"Unknown";

    if (!doc.contains("asset") || !doc["asset"].is_object())
    {
#if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] GLTF 'asset' section missing for exporter detection.");
#endif
       return;
    }

    const auto& asset = doc["asset"];

    if (asset.contains("generator"))
    {
        std::string generatorStr = asset["generator"].get<std::string>();
        std::wstring wGeneratorStr(generatorStr.begin(), generatorStr.end());
#if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[SceneManager] GLTF Exporter Generator String: %s", wGeneratorStr.c_str());
#endif
        if (generatorStr.find("Blender") != std::string::npos)
            m_lastDetectedExporter = L"Blender";
        else if (generatorStr.find("Sketchfab") != std::string::npos)
            m_lastDetectedExporter = L"Sketchfab";
        else if (generatorStr.find("obj2gltf") != std::string::npos)
            m_lastDetectedExporter = L"OBJ2GLTF";
        else if (generatorStr.find("FBX2glTF") != std::string::npos)
            m_lastDetectedExporter = L"FBX2GLTF";
        else if (generatorStr.find("glTF-Transform") != std::string::npos)
            m_lastDetectedExporter = L"glTF-Transform";
        else
            m_lastDetectedExporter = std::wstring(generatorStr.begin(), generatorStr.end());

#if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Detected GLTF Exporter: %s", m_lastDetectedExporter.c_str());
#endif
    }
    else
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] No 'generator' field found in GLTF asset block.");
        #endif
    }
}

// --------------------------------------------------------------------------------------------------
bool SceneManager::SaveSceneState(const std::wstring& path)
{
    std::ofstream outFile(path, std::ios::binary);
    if (!outFile.is_open())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to open file for saving: " + path);
        return false;
    }

    const char header[4] = { 'G', 'L', 'T', 'B' };
    uint32_t version = 0x0100;
    uint32_t count = 0;

    // Count how many scene_models are valid
    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (scene_models[i].m_isLoaded)
            ++count;
    }

    // Header
    outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
    outFile.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));
    outFile.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));

    wchar_t exporterName[64] = {};
    wcsncpy_s(exporterName, m_lastDetectedExporter.c_str(), 63);
    outFile.write(reinterpret_cast<const char*>(exporterName), sizeof(exporterName));

    // === Write Camera Position and Target ===
    XMFLOAT3 camPos = myRenderer->myCamera.GetPosition();
    XMFLOAT3 camTarget = myRenderer->myCamera.target;
    outFile.write(reinterpret_cast<const char*>(&camPos), sizeof(XMFLOAT3));
    outFile.write(reinterpret_cast<const char*>(&camTarget), sizeof(XMFLOAT3));

    // Model Entries
    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (!scene_models[i].m_isLoaded) continue;

        const ModelInfo& info = scene_models[i].m_modelInfo;
        SceneModelStateBinary entry = {};
        entry.ID = info.ID;
        wcsncpy_s(entry.name, info.name.c_str(), 63);
        entry.position[0] = info.position.x;
        entry.position[1] = info.position.y;
        entry.position[2] = info.position.z;
        entry.rotation[0] = info.rotation.x;
        entry.rotation[1] = info.rotation.y;
        entry.rotation[2] = info.rotation.z;
        entry.scale[0] = info.scale.x;
        entry.scale[1] = info.scale.y;
        entry.scale[2] = info.scale.z;

        outFile.write(reinterpret_cast<const char*>(&entry), sizeof(SceneModelStateBinary));
    }

    outFile.close();
#if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Scene state saved to " + path);
#endif
    return true;
}

bool SceneManager::LoadSceneState(const std::wstring& path)
{
    std::ifstream inFile(path, std::ios::binary);
    if (!inFile.is_open())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to open file for loading: " + path);
        return false;
    }

    char header[4] = {};
    uint32_t version = 0;
    uint32_t count = 0;
    wchar_t exporterName[64] = {};

    inFile.read(reinterpret_cast<char*>(&header), sizeof(header));
    inFile.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
    inFile.read(reinterpret_cast<char*>(&count), sizeof(uint32_t));
    inFile.read(reinterpret_cast<char*>(exporterName), sizeof(exporterName));

    m_lastDetectedExporter = exporterName;
    // === Read Camera Position and Target ===
    XMFLOAT3 camPos = {};
    XMFLOAT3 camTarget = {};
    inFile.read(reinterpret_cast<char*>(&camPos), sizeof(XMFLOAT3));
    inFile.read(reinterpret_cast<char*>(&camTarget), sizeof(XMFLOAT3));

    // Set camera position and orientation
    if (myRenderer && !threadManager.threadVars.bIsResizing.load())
    {
        myRenderer->myCamera.SetPosition(camPos.x, camPos.y, camPos.z);
        myRenderer->myCamera.SetTarget(camTarget);
        myRenderer->myCamera.UpdateViewMatrix();
    }

    int instanceIndex = 0;
    for (uint32_t i = 0; i < count && instanceIndex < MAX_SCENE_MODELS; ++i)
    {
        SceneModelStateBinary entry = {};
        inFile.read(reinterpret_cast<char*>(&entry), sizeof(SceneModelStateBinary));

        std::wstring modelName = entry.name;
        int modelSlot = -1;

        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.name == modelName)
            {
                modelSlot = m;
                break;
            }
        }

        if (modelSlot == -1)
        {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Skipping model \"%ls\" — not found in base models[]", modelName.c_str());
            continue;
        }

        // Register model into scene
        scene_models[instanceIndex].CopyFrom(models[modelSlot]);
        ModelInfo& info = scene_models[instanceIndex].m_modelInfo;

        info.name = modelName;
        info.ID = entry.ID;
        info.position = XMFLOAT3(entry.position[0], entry.position[1], entry.position[2]);
        info.rotation = XMFLOAT3(entry.rotation[0], entry.rotation[1], entry.rotation[2]);
        info.scale = XMFLOAT3(entry.scale[0], entry.scale[1], entry.scale[2]);

        scene_models[instanceIndex].SetupModelForRendering(info.ID);
        scene_models[instanceIndex].ApplyDefaultLightingFromManager(lightsManager);
        scene_models[instanceIndex].m_isLoaded = true;
        scene_models[instanceIndex].bIsDestroyed = false;

        ++instanceIndex;
    }

    inFile.close();
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Scene state loaded from " + path);
    #endif
    return true;
}
