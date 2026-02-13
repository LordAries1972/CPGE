// SceneManager.cpp (continued)
#include "Includes.h"
#include "SceneManager.h"
#include "ThreadLockHelper.h"
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

//==============================================================================
// SceneManager::ParseGLBScene()
// Parses GLB 2.0 binary format files and loads them into the scene with proper parent-child relationships.
// GLB format: 12-byte header + JSON chunk + embedded BIN chunk (all binary data is self-contained)
// Parent models have iParentModelID = -1, children reference their parent's instanceIndex
//==============================================================================
bool SceneManager::ParseGLBScene(const std::wstring& glbFile)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] ParseGLBScene() - Opening GLB binary file.");
    #endif

    // Check if the GLB file exists on the filesystem
    if (!std::filesystem::exists(glbFile)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] GLB file not found: %ls", glbFile.c_str());
        #endif
        return false;
    }

    // Open the GLB file in binary mode for reading
    std::ifstream file(glbFile, std::ios::binary);
    if (!file.is_open()) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to open GLB file: %ls", glbFile.c_str());
        #endif
        return false;
    }

    // GLB Header Structure: magic(4) + version(4) + length(4) = 12 bytes
    struct GLBHeader {
        uint32_t magic;                                                              // Should be 'glTF' (0x46546C67)
        uint32_t version;                                                           // GLB version (should be 2)
        uint32_t length;                                                            // Total file length including header
    };

    // Read the 12-byte GLB header from the file
    GLBHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(GLBHeader));
    if (file.gcount() != sizeof(GLBHeader)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to read GLB header - file too small.");
        #endif
        file.close();
        return false;
    }

    // Validate GLB magic number (0x46546C67 = 'glTF' in little-endian)
    if (header.magic != 0x46546C67) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Invalid GLB magic number: 0x%08X", header.magic);
        #endif
        file.close();
        return false;
    }

    // Validate GLB version (must be 2 for GLB 2.0 format)
    if (header.version != 2) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Unsupported GLB version: %d", header.version);
        #endif
        file.close();
        return false;
    }

    // Get actual file size for validation
    file.seekg(0, std::ios::end);
    std::streampos actualFileSize = file.tellg();
    file.seekg(sizeof(GLBHeader), std::ios::beg);                                  // Reset to after header

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLB Header validated - Version: %d, Header length: %d bytes, Actual file: %d bytes", 
            header.version, header.length, static_cast<uint32_t>(actualFileSize));
    #endif

    // Handle Blender GLB export bug where header.length is incorrect
    if (header.length != static_cast<uint32_t>(actualFileSize)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] GLB header length mismatch - using actual file size for validation (Blender export bug)");
        #endif
    }
    
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLB Header validated - Version: %d, Total length: %d bytes", header.version, header.length);
    #endif

    // GLB Chunk Structure: chunkLength(4) + chunkType(4) + chunkData(chunkLength)
    struct GLBChunk {
        uint32_t length;                                                            // Length of chunk data in bytes
        uint32_t type;                                                              // Chunk type identifier
    };

    // Read the first chunk header (should be JSON chunk)
    GLBChunk jsonChunk;
    file.read(reinterpret_cast<char*>(&jsonChunk), sizeof(GLBChunk));
    if (file.gcount() != sizeof(GLBChunk)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to read JSON chunk header.");
        #endif
        file.close();
        return false;
    }

    // Validate JSON chunk type (0x4E4F534A = 'JSON' in little-endian)
    if (jsonChunk.type != 0x4E4F534A) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Expected JSON chunk, got type: 0x%08X", jsonChunk.type);
        #endif
        file.close();
        return false;
    }

    // Read the JSON chunk data into a string buffer
    std::string jsonData(jsonChunk.length, '\0');
    file.read(&jsonData[0], jsonChunk.length);
    if (file.gcount() != static_cast<std::streamsize>(jsonChunk.length)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to read JSON chunk data (%d bytes).", jsonChunk.length);
        #endif
        file.close();
        return false;
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] JSON chunk loaded successfully (%d bytes).", jsonChunk.length);
    #endif

    // Check if there's a BIN chunk following the JSON chunk (embedded binary data)
    gltfBinaryData.clear();                                                         // Clear any existing binary data
    
    // Calculate where BIN chunk should start (after JSON chunk with 4-byte alignment)
    size_t binChunkStart = 20 + jsonChunk.length;                                  // JSON start + JSON data
    
    // Align to 4-byte boundary as required by GLB specification
    while (binChunkStart % 4 != 0) {
        binChunkStart++;
    }
    
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Looking for BIN chunk at byte offset: %d", static_cast<int>(binChunkStart));
    #endif
    
    // Check if we have enough bytes left for a BIN chunk header
    if (binChunkStart + 8 < static_cast<size_t>(actualFileSize)) {
        // Seek to potential BIN chunk location
        file.seekg(binChunkStart);
        
        GLBChunk binChunk;
        if (file.read(reinterpret_cast<char*>(&binChunk), sizeof(GLBChunk))) {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Found chunk: length=%d, type=0x%08X", 
                    binChunk.length, binChunk.type);
            #endif
            
            // Validate BIN chunk type (0x004E4942 = 'BIN\0' in little-endian)
            if (binChunk.type == 0x004E4942) {
                // Read the BIN chunk data into our global binary buffer
                gltfBinaryData.resize(binChunk.length);
                file.read(reinterpret_cast<char*>(gltfBinaryData.data()), binChunk.length);
                
                if (file.gcount() == static_cast<std::streamsize>(binChunk.length)) {
                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] BIN chunk loaded successfully (%d bytes).", binChunk.length);
                    #endif
                } else {
                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to read complete BIN chunk data.");
                    #endif
                }
            } else {
                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Invalid BIN chunk type: 0x%08X", binChunk.type);
                #endif
            }
        } else {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] Failed to read potential BIN chunk header");
            #endif
        }
    } else {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] No space for BIN chunk - GLB contains JSON only");
        #endif
    }

    // Close the GLB file as we have read all required data
    file.close();

    // Parse the JSON data using nlohmann::json library
    json doc;
    try {
        doc = json::parse(jsonData);
        
        // CRITICAL DEBUG: Check what we actually parsed
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] JSON PARSING DEBUG:");
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] JSON string length: %d characters", static_cast<int>(jsonData.length()));
            
            // Check if accessors exist and how many
            if (doc.contains("accessors") && doc["accessors"].is_array()) {
                int accessorCount = static_cast<int>(doc["accessors"].size());
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Found %d accessors in parsed JSON", accessorCount);
                
                // Print details of each accessor
                for (int i = 0; i < accessorCount; ++i) {
                    const auto& acc = doc["accessors"][i];
                    int count = acc.value("count", 0);
                    std::string type = acc.value("type", "UNKNOWN");
                    int componentType = acc.value("componentType", 0);
                    int bufferView = acc.value("bufferView", -1);
                    
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager]   Accessor %d: count=%d, type=%hs, componentType=%d, bufferView=%d", 
                                        i, count, type.c_str(), componentType, bufferView);
                }
            } else {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] NO ACCESSORS FOUND in parsed JSON!");
            }
            
            // Check if bufferViews exist
            if (doc.contains("bufferViews") && doc["bufferViews"].is_array()) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Found %d bufferViews", static_cast<int>(doc["bufferViews"].size()));
            } else {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] NO BUFFER VIEWS FOUND in parsed JSON!");
            }
            
            // Check if animations exist and what accessors they reference
            if (doc.contains("animations") && doc["animations"].is_array()) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Found %d animations", static_cast<int>(doc["animations"].size()));
                
                const auto& anims = doc["animations"];
                for (size_t a = 0; a < anims.size(); ++a) {
                    const auto& anim = anims[a];
                    if (anim.contains("samplers") && anim["samplers"].is_array()) {
                        const auto& samplers = anim["samplers"];
                        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager]   Animation %d has %d samplers", static_cast<int>(a), static_cast<int>(samplers.size()));
                        
                        for (size_t s = 0; s < samplers.size(); ++s) {
                            const auto& sampler = samplers[s];
                            int input = sampler.value("input", -1);
                            int output = sampler.value("output", -1);
                            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager]     Sampler %d: input=%d, output=%d", static_cast<int>(s), input, output);
                        }
                    }
                }
            } else {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] NO ANIMATIONS FOUND in parsed JSON!");
            }
            
            // CRITICAL: Print first 500 and last 500 characters of JSON to see if truncation
            std::string jsonStart = jsonData.substr(0, std::min(500, static_cast<int>(jsonData.length())));
            std::string jsonEnd = jsonData.length() > 500 ? jsonData.substr(jsonData.length() - 500) : "";
            
            std::wstring wJsonStart(jsonStart.begin(), jsonStart.end());
            std::wstring wJsonEnd(jsonEnd.begin(), jsonEnd.end());
            
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] JSON START: %ls", wJsonStart.c_str());
            if (!jsonEnd.empty()) {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] JSON END: %ls", wJsonEnd.c_str());
            }
        #endif
    }
    catch (const std::exception& ex) {
        // Convert narrow string exception message to wide string for debug output
        std::wstring werror(ex.what(), ex.what() + strlen(ex.what()));
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] JSON parse error in GLB: %ls", werror.c_str());
        #endif
        return false;
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] GLB JSON parsed successfully - proceeding with scene analysis.");
    #endif

    // Detect the exporter information for compatibility handling
    DetectGLTFExporter(doc);
    bool isSketchfab = (m_lastDetectedExporter == L"Sketchfab");

    // Initialize exporter detection with unknown default
    m_lastDetectedExporter = L"Unknown Exporter";

    // Check for asset information in the GLTF document
    if (doc.contains("asset") && doc["asset"].is_object()) {
        const auto& asset = doc["asset"];

        // Extract generator information if available
        if (asset.contains("generator") && asset["generator"].is_string()) {
            std::string generator = asset["generator"];
            std::string lowerGen = generator;
            // Convert generator string to lowercase for case-insensitive comparison
            std::transform(lowerGen.begin(), lowerGen.end(), lowerGen.begin(), ::tolower);

            // Identify common GLTF exporters for compatibility handling
            if (lowerGen.find("blender") != std::string::npos) {
                m_lastDetectedExporter = L"Blender";
            }
            else if (lowerGen.find("sketchfab") != std::string::npos) {
                m_lastDetectedExporter = L"Sketchfab";
            }
            else if (lowerGen.find("obj") != std::string::npos || lowerGen.find("fbx") != std::string::npos) {
                m_lastDetectedExporter = L"Converted (OBJ/FBX)";
            }
            else {
                // Convert the generator string to wide string for storage
                m_lastDetectedExporter = std::wstring(generator.begin(), generator.end());
            }
        }
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLB Exporter Detected: %ls", m_lastDetectedExporter.c_str());
    #endif

    // Parse camera, lights, and materials using existing GLTF parsing functions
    // NOTE: The embedded binary data in gltfBinaryData is now available for these functions
    ParseGLTFCamera(doc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
    ParseGLTFLights(doc);
    ParseMaterialsFromGLTF(doc);

    // Parse animations from GLB document and store them in the global animator
    bAnimationsLoaded = gltfAnimator.ParseAnimationsFromGLTF(doc, gltfBinaryData);
    if (bAnimationsLoaded)
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Successfully loaded %d animations from GLB", gltfAnimator.GetAnimationCount());
        #endif
        gltfAnimator.DebugPrintAnimationInfo();
    }
    
    // Build the list of root node indices from the scene definition
    std::vector<int> rootNodeIndices;
    if (doc.contains("scenes") && doc["scenes"].is_array() && !doc["scenes"].empty()) 
    {
        // Use the first scene definition to get root nodes
        const auto& scene0 = doc["scenes"][0];
        if (scene0.contains("nodes") && scene0["nodes"].is_array()) 
        {
            // Extract all root node indices from the scene
            for (const auto& n : scene0["nodes"]) 
            {
                if (n.is_number_integer()) {
                    rootNodeIndices.push_back(n.get<int>());
                }
            }
        }
    }

    // Fallback: if no valid scene nodes found, use all top-level nodes as roots
    if (rootNodeIndices.empty() && doc.contains("nodes") && doc["nodes"].is_array()) 
    {
        const auto& nodes = doc["nodes"];
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
            rootNodeIndices.push_back(i);
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] No valid scene.nodes found. Defaulting to root-level nodes[].");
        #endif
    }

    // Validate that we have at least one root node to process
    if (rootNodeIndices.empty()) 
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] No root nodes available. GLB scene is empty or malformed.");
        #endif
        return false;
    }

    // Start processing nodes recursively from the root nodes
    int instanceIndex = 0;

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Starting GLB node processing with %d root nodes.", static_cast<int>(rootNodeIndices.size()));
    #endif

    // Process each root node and its children recursively
    for (int rootNodeIndex : rootNodeIndices) 
    {
        if (doc.contains("nodes") && doc["nodes"].is_array()) 
        {
            const auto& nodes = doc["nodes"];
            if (rootNodeIndex >= 0 && rootNodeIndex < static_cast<int>(nodes.size())) 
            {
                const auto& rootNode = nodes[rootNodeIndex];
                XMMATRIX identity = XMMatrixIdentity();                            // Root nodes start with identity transform
                
                // Parse this root node and all its children recursively
                ParseGLBNodeRecursive(rootNode, identity, doc, nodes, instanceIndex, -1);
            }
        }
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLB parsing completed. Total Instances: %d", instanceIndex);
    #endif

    // Return success if at least one model instance was created
    return (instanceIndex > 0);
}

// --------------------------------------------------------------------------------------------------
// SceneManager::ParseGLBNodeRecursive()
// Recursively processes GLTF nodes from GLB files with parent-child relationship tracking.
// Sets iParentModelID = -1 for parent models, assigns parent's instanceIndex to children.
// This function handles the hierarchical structure and ensures proper parent-child linkage.
// --------------------------------------------------------------------------------------------------
void SceneManager::ParseGLBNodeRecursive(const json& node, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex, int parentModelID)
{
    // Prevent buffer overflow by checking maximum scene model limit
    if (instanceIndex >= MAX_SCENE_MODELS)
        return;

    // Check if this node contains a mesh to be rendered
    bool hasMesh = node.contains("mesh") && node["mesh"].is_number_integer();
    
    // Store the current instance index as potential parent ID for children
    int currentParentID = parentModelID;

    // Load and decompose the node's local transformation matrix
    XMMATRIX nodeTransform = GetNodeWorldMatrix(node);

    // Decompose transformation matrix to extract scale for potential geometry baking
    XMVECTOR outScale, outRot, outTrans;
    XMMatrixDecompose(&outScale, &outRot, &outTrans, nodeTransform);
    XMFLOAT3 scale;
    XMStoreFloat3(&scale, outScale);
    
    // Check if the node has non-identity scale that needs to be baked into geometry
    bool hasNonIdentityScale = (fabs(scale.x - 1.0f) > 0.0001f || fabs(scale.y - 1.0f) > 0.0001f || fabs(scale.z - 1.0f) > 0.0001f);

    // Process mesh if present in this node
    if (hasMesh)
    {
        // Extract mesh index from the node definition
        int meshIndex = node["mesh"];
        
        // Validate that meshes array exists in the GLTF document
        if (!doc.contains("meshes") || !doc["meshes"].is_array()) return;

        const auto& meshes = doc["meshes"];
        
        // Validate mesh index is within valid range
        if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;

        // Extract proper model name from GLB node FIRST, only use default if none exists**
        std::wstring modelName;
        if (node.contains("name") && node["name"].is_string())
        {
            // Use the actual node name from GLB file
            std::string nodeName = node["name"];
            modelName = sysUtils.ToWString(nodeName);
//            modelName = std::wstring(nodeName.begin(), nodeName.end());
            scene_models[instanceIndex].m_modelInfo.name = modelName;
            
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Using node name from GLB: \"%ls\"", scene_models[instanceIndex].m_modelInfo.name.c_str());
            #endif
        }
        else
        {
            // No name provided in GLB node - generate default name and log this situation
            modelName = L"GLBNode_" + std::to_wstring(instanceIndex) + L"_Mesh_" + std::to_wstring(meshIndex);
            scene_models[instanceIndex].m_modelInfo.name = modelName;
            
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] No name found in GLB node - using default name: \"%ls\"", scene_models[instanceIndex].m_modelInfo.name.c_str());
            #endif
        }
        
        int modelSlot = -1;

        // Search for existing model with the same name in global models array
        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.name == modelName)
            {
                modelSlot = m;                                                      // Found existing model, reuse it
                break;
            }
        }

        // If model not found, create new model in first available slot
        if (modelSlot == -1)
        {
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name.empty())
                {
                    modelSlot = m;                                                  // Found empty slot for new model
                    models[m].m_modelInfo.name = modelName;                        // Assign the model name
                    models[m].m_modelInfo.ID = m;                                  // Set the model ID
                    models[m].m_modelInfo.vertices.clear();                       // Clear any existing vertex data
                    models[m].m_modelInfo.indices.clear();                        // Clear any existing index data

                    // Load mesh geometry data using existing GLTF primitive loader
                    LoadGLTFMeshPrimitives(meshIndex, doc, models[m]);
                    break;
                }
            }
        }

        // If no available model slot found, skip this mesh
        if (modelSlot == -1) return;

        // Bake scale into vertex geometry if the node has non-identity scale
        if (hasNonIdentityScale)
        {
            for (auto& v : models[modelSlot].m_modelInfo.vertices)
            {
                // Apply scale transformation to vertex positions
                v.position.x *= scale.x;
                v.position.y *= scale.y;
                v.position.z *= scale.z;
            }

            // Remove scale from transformation matrix since it's now baked into geometry
            nodeTransform *= XMMatrixScaling(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        }

        // Compute the final world transformation matrix by combining parent and node transforms
        XMMATRIX worldTransform = parentTransform * nodeTransform;

        // Copy model data from global models array to scene-specific model instance
        scene_models[instanceIndex].CopyFrom(models[modelSlot]);
        scene_models[instanceIndex].m_modelInfo.worldMatrix = worldTransform;

        scene_models[instanceIndex].m_modelInfo.textures = models[modelSlot].m_modelInfo.textures;
        scene_models[instanceIndex].m_modelInfo.textureSRVs = models[modelSlot].m_modelInfo.textureSRVs;
        scene_models[instanceIndex].m_modelInfo.normalMapSRVs = models[modelSlot].m_modelInfo.normalMapSRVs;

        // === CRITICAL FIX: Pre-allocate texture vectors AFTER copy to prevent reallocation ===
        // Vector assignment copies SIZE not CAPACITY, so we must re-reserve on the scene_model
        // Calculate based on number of primitives in the source mesh
        if (doc.contains("meshes") && doc["meshes"].is_array())
        {
            const auto& meshes = doc["meshes"];
            if (meshIndex >= 0 && meshIndex < (int)meshes.size())
            {
                const auto& mesh = meshes[meshIndex];
                if (mesh.contains("primitives") && mesh["primitives"].is_array())
                {
                    size_t numPrimitives = mesh["primitives"].size();
                    size_t maxTexturesNeeded = numPrimitives * 3;  // 3 textures per primitive max
                    
                    // Reserve capacity on the SCENE model's vectors (not the global model)
                    scene_models[instanceIndex].m_modelInfo.textures.reserve(maxTexturesNeeded);
                    scene_models[instanceIndex].m_modelInfo.textureSRVs.reserve(maxTexturesNeeded);
                    scene_models[instanceIndex].m_modelInfo.normalMapSRVs.reserve(maxTexturesNeeded);
                    
                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO, 
                            L"[SceneManager] Pre-allocated %d texture slots for scene_models[%d] (%d primitives)", 
                            static_cast<int>(maxTexturesNeeded), instanceIndex, static_cast<int>(numPrimitives));
                    #endif
                }
            }
        }

        // Set parent-child relationship: -1 for parent models, parent's instanceIndex for children
        scene_models[instanceIndex].m_modelInfo.iParentModelID = parentModelID;

        // Extract position from the world transformation matrix for positioning
        XMFLOAT4X4 f4x4;
        XMStoreFloat4x4(&f4x4, worldTransform);
        scene_models[instanceIndex].m_modelInfo.position = XMFLOAT3(f4x4._41, f4x4._42, f4x4._43);
        
        // Reset scale to identity since it's been baked into geometry
        XMStoreFloat3(&scene_models[instanceIndex].m_modelInfo.scale, XMVectorSet(1.0f, 1.0f, 1.0f, 0));
        
        // Assign unique instance ID and descriptive name
        scene_models[instanceIndex].m_modelInfo.ID = instanceIndex;
        scene_models[instanceIndex].m_modelInfo.name = modelName;

        // Setup model for rendering and apply default lighting
        scene_models[instanceIndex].SetupModelForRendering(scene_models[instanceIndex].m_modelInfo.ID);
        scene_models[instanceIndex].ApplyDefaultLightingFromManager(lightsManager);
        
        scene_models[instanceIndex].m_isLoaded = true;

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] scene_models[%d] lighting: %d local lights applied.",
                instanceIndex, static_cast<int>(scene_models[instanceIndex].m_modelInfo.localLights.size()));
        #endif

        // Apply exporter-specific patches for compatibility if needed
        const std::wstring& exp = GetLastDetectedExporter();
        if (exp == L"Sketchfab")
        {
            // Apply Sketchfab-specific transformations or corrections if needed
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Sketchfab GLB scene loaded. Patch applied.");
            #endif
        }
        else if (exp == L"Blender")
        {
            // Apply Blender-specific transformations or corrections if needed
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Blender GLB scene loaded. No patch applied.");
            #endif
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] scene_models[%d] created: \"%ls\" | ParentID: %d | Pos(%.2f, %.2f, %.2f) | Scale baked",
                instanceIndex,
                scene_models[instanceIndex].m_modelInfo.name.c_str(),
                scene_models[instanceIndex].m_modelInfo.iParentModelID,
                f4x4._41, f4x4._42, f4x4._43);
        #endif

        // Update current parent ID for child nodes (if this node becomes a parent)
        currentParentID = instanceIndex;
        
        // Increment instance counter for next model
        ++instanceIndex;
    }

    // Process child nodes recursively, passing current node as parent
    if (node.contains("children") && node["children"].is_array())
    {
        for (const auto& childIndex : node["children"])
        {
            // Validate child index is a valid integer
            if (!childIndex.is_number_integer()) continue;
            int ci = childIndex.get<int>();
            
            // Validate child index is within valid range
            if (ci < 0 || ci >= (int)allNodes.size()) continue;

            // Recursively parse child node with updated parent transformation and parent ID
            ParseGLBNodeRecursive(allNodes[ci], parentTransform * nodeTransform, doc, allNodes, instanceIndex, currentParentID);
        }
    }
}

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------
bool SceneManager::ParseGLTFScene(const std::wstring& gltfFile)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] ParseGLTFScene() - Opening GLTF file.");
    #endif

    // Check if the GLTF file exists on the filesystem
    if (!std::filesystem::exists(gltfFile)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] File not found: %ls", gltfFile.c_str());
        #endif
        return false;
    }

    // Open the GLTF file for reading
    std::ifstream file(gltfFile);
    if (!file.is_open()) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] Failed to open GLTF: %ls", gltfFile.c_str());
        #endif
        return false;
    }

    // Parse the JSON document from the GLTF file
    json doc;
    try {
        file >> doc;
        file.close();
    }
    catch (const std::exception& ex) {
        // Convert narrow string exception message to wide string for debug output
        std::wstring werror(ex.what(), ex.what() + strlen(ex.what()));
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] JSON parse error: %ls", werror.c_str());
        return false;
    }

    // Detect the exporter information for compatibility handling
    DetectGLTFExporter(doc);
    bool isSketchfab = (m_lastDetectedExporter == L"Sketchfab");

    // Initialize exporter detection with unknown default
    m_lastDetectedExporter = L"Unknown Exporter";

    // Check for asset information in the GLTF document
    if (doc.contains("asset") && doc["asset"].is_object()) {
        const auto& asset = doc["asset"];

        // Extract generator information if available
        if (asset.contains("generator") && asset["generator"].is_string()) {
            std::string generator = asset["generator"];
            std::string lowerGen = generator;
            // Convert generator string to lowercase for case-insensitive comparison
            std::transform(lowerGen.begin(), lowerGen.end(), lowerGen.begin(), ::tolower);

            // Identify common GLTF exporters for compatibility handling
            if (lowerGen.find("blender") != std::string::npos) {
                m_lastDetectedExporter = L"Blender";
            }
            else if (lowerGen.find("sketchfab") != std::string::npos) {
                m_lastDetectedExporter = L"Sketchfab";
            }
            else if (lowerGen.find("obj") != std::string::npos || lowerGen.find("fbx") != std::string::npos) {
                m_lastDetectedExporter = L"Converted (OBJ/FBX)";
            }
            else {
                // Convert the generator string to wide string for storage
                m_lastDetectedExporter = std::wstring(generator.begin(), generator.end());
            }
        }
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLTF Exporter Detected: %ls", m_lastDetectedExporter.c_str());
    #endif

    // Load binary .bin file if referenced in buffers array
    if (doc.contains("buffers") && doc["buffers"].is_array() && !doc["buffers"].empty()) {
        const auto& buffers = doc["buffers"];
        std::string uri = buffers[0].value("uri", "");
        
        // Only load external .bin file if URI is provided (not embedded data)
        if (!uri.empty()) {
            std::filesystem::path binPath = std::filesystem::path(gltfFile).parent_path() / uri;

            std::ifstream bin(binPath, std::ios::binary);
            if (bin.is_open()) {
                // Read the binary file into the global binary data buffer
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
        } else {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] No external .bin file referenced - using embedded data.");
            #endif
        }
    }

    // Parse camera, lights, and materials using existing GLTF parsing functions
    ParseGLTFCamera(doc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
    ParseGLTFLights(doc);
    ParseMaterialsFromGLTF(doc);

    // Parse animations from GLTF document and store them in the global animator
    bAnimationsLoaded = gltfAnimator.ParseAnimationsFromGLTF(doc, gltfBinaryData);
    if (bAnimationsLoaded)
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Successfully loaded %d animations from GLTF", gltfAnimator.GetAnimationCount());
        #endif
        gltfAnimator.DebugPrintAnimationInfo();
    }
    
    // Build the list of root node indices from the scene definition
    std::vector<int> rootNodeIndices;
    if (doc.contains("scenes") && doc["scenes"].is_array() && !doc["scenes"].empty()) 
    {
        // Use the first scene definition to get root nodes
        const auto& scene0 = doc["scenes"][0];
        if (scene0.contains("nodes") && scene0["nodes"].is_array()) 
        {
            // Extract all root node indices from the scene
            for (const auto& n : scene0["nodes"]) 
            {
                if (n.is_number_integer()) {
                    rootNodeIndices.push_back(n.get<int>());
                }
            }
        }
    }

    // Fallback: if no valid scene nodes found, use all top-level nodes as roots
    if (rootNodeIndices.empty() && doc.contains("nodes") && doc["nodes"].is_array()) 
    {
        const auto& nodes = doc["nodes"];
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
            rootNodeIndices.push_back(i);
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] No valid scene.nodes found. Defaulting to root-level nodes[].");
        #endif
    }

    // Validate that we have at least one root node to process
    if (rootNodeIndices.empty()) 
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] No root nodes available. Scene is empty or malformed.");
        #endif
        return false;
    }

    // Get reference to the nodes array for recursive parsing
    const auto& nodes = doc["nodes"];
    int instanceIndex = 0;                                                          // Counter for scene model instances

    // Process each root node and its children recursively with parent-child tracking
    for (int nodeIndex : rootNodeIndices) 
    {
        // Validate node index is within valid range
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            continue;

        const json& rootNode = nodes[nodeIndex];
        // Parse this root node as a parent (parentModelID = -1) 
        ParseGLTFNodeRecursive(rootNode, XMMatrixIdentity(), doc, nodes, instanceIndex, -1);
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLTF Scene Load Complete. Total Instances: %d", instanceIndex);
    #endif

    // Return success if at least one model instance was created
    return (instanceIndex > 0);
}

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------
void SceneManager::ParseGLTFNodeRecursive(const json& node, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex, int parentModelID)
{
    // Prevent buffer overflow by checking maximum scene model limit
    if (instanceIndex >= MAX_SCENE_MODELS)
        return;

    // Check if this node contains a mesh to be rendered
    bool hasMesh = node.contains("mesh") && node["mesh"].is_number_integer();
    
    // Store the current instance index as potential parent ID for children
    int currentParentID = parentModelID;

    // Load and decompose the node's local transformation matrix
    XMMATRIX nodeTransform = GetNodeWorldMatrix(node);

    // Decompose transformation matrix to extract scale for potential geometry baking
    XMVECTOR outScale, outRot, outTrans;
    XMMatrixDecompose(&outScale, &outRot, &outTrans, nodeTransform);
    XMFLOAT3 scale;
    XMStoreFloat3(&scale, outScale);
    
    // Check if the node has non-identity scale that needs to be baked into geometry
    bool hasNonIdentityScale = (fabs(scale.x - 1.0f) > 0.0001f || fabs(scale.y - 1.0f) > 0.0001f || fabs(scale.z - 1.0f) > 0.0001f);

    // Process mesh if present in this node
    if (hasMesh)
    {
        // Extract mesh index from the node definition
        int meshIndex = node["mesh"];
        
        // Validate that meshes array exists in the GLTF document
        if (!doc.contains("meshes") || !doc["meshes"].is_array()) return;

        const auto& meshes = doc["meshes"];
        
        // Validate mesh index is within valid range
        if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;

        // Extract proper model name from GLTF node FIRST, only use default if none exists**
        std::wstring modelName;
        if (node.contains("name") && node["name"].is_string())
        {
            // Use the actual node name from GLTF file
            std::string nodeName = node["name"];
            modelName = sysUtils.ToWString(nodeName);
//            modelName = std::wstring(nodeName.begin(), nodeName.end());
            scene_models[instanceIndex].m_modelInfo.name = modelName;
            
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Using node name from GLTF: \"%ls\"", scene_models[instanceIndex].m_modelInfo.name.c_str());
            #endif
        }
        else
        {
            // No name provided in GLTF node - generate default name and log this situation
            modelName = L"Node_" + std::to_wstring(instanceIndex) + L"_Mesh_" + std::to_wstring(meshIndex);
            scene_models[instanceIndex].m_modelInfo.name = modelName;
            
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] No name found in GLTF node - using default name: \"%ls\"", scene_models[instanceIndex].m_modelInfo.name.c_str());
            #endif
        }

        int modelSlot = -1;

        // Search for existing model with the same name in global models array
        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.name == modelName)
            {
                modelSlot = m;                                                      // Found existing model, reuse it
                break;
            }
        }

        // If model not found, create new model in first available slot
        if (modelSlot == -1)
        {
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name.empty())
                {
                    modelSlot = m;                                                  // Found empty slot for new model
                    models[m].m_modelInfo.name = modelName;                         // Assign the model name
                    models[m].m_modelInfo.ID = m;                                   // Set the model ID
                    models[m].m_modelInfo.vertices.clear();
                    models[m].m_modelInfo.indices.clear();
                    models[m].m_modelInfo.textures.clear();                         // Clear texture vectors too
                    models[m].m_modelInfo.textureSRVs.clear();
                    models[m].m_modelInfo.normalMapSRVs.clear();

                    // Load mesh geometry data using existing GLTF primitive loader
                    LoadGLTFMeshPrimitives(meshIndex, doc, models[m]);
                    break;
                }
            }
        }

        // If no available model slot found, skip this mesh
        if (modelSlot == -1) return;

        // Bake scale into vertex geometry if the node has non-identity scale
        if (hasNonIdentityScale)
        {
            for (auto& v : models[modelSlot].m_modelInfo.vertices)
            {
                // Apply scale transformation to vertex positions
                v.position.x *= scale.x;
                v.position.y *= scale.y;
                v.position.z *= scale.z;
            }

            // Remove scale from transformation matrix since it's now baked into geometry
            nodeTransform *= XMMatrixScaling(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        }

        // Compute the final world transformation matrix by combining parent and node transforms
        XMMATRIX worldTransform = parentTransform * nodeTransform;

        // Copy model data from global models array to scene-specific model instance
        scene_models[instanceIndex].CopyFrom(models[modelSlot]);
        scene_models[instanceIndex].m_modelInfo.worldMatrix = worldTransform;

        // Copy texture and material data from the source model
        //scene_models[instanceIndex].m_modelInfo.textures = models[modelSlot].m_modelInfo.textures;
        //scene_models[instanceIndex].m_modelInfo.textureSRVs = models[modelSlot].m_modelInfo.textureSRVs;
        //scene_models[instanceIndex].m_modelInfo.normalMapSRVs = models[modelSlot].m_modelInfo.normalMapSRVs;

        // === CRITICAL FIX: Pre-allocate texture vectors AFTER copy to prevent reallocation ===
        // Vector assignment copies SIZE not CAPACITY, so we must re-reserve on the scene_model
        // Calculate based on number of primitives in the source mesh
        if (doc.contains("meshes") && doc["meshes"].is_array())
        {
            const auto& meshes = doc["meshes"];
            if (meshIndex >= 0 && meshIndex < (int)meshes.size())
            {
                const auto& mesh = meshes[meshIndex];
                if (mesh.contains("primitives") && mesh["primitives"].is_array())
                {
                    size_t numPrimitives = mesh["primitives"].size();
                    size_t maxTexturesNeeded = numPrimitives * 3;  // 3 textures per primitive max
                    
                    // Reserve capacity on the SCENE model's vectors (not the global model)
                    scene_models[instanceIndex].m_modelInfo.textures.reserve(maxTexturesNeeded);
                    scene_models[instanceIndex].m_modelInfo.textureSRVs.reserve(maxTexturesNeeded);
                    scene_models[instanceIndex].m_modelInfo.normalMapSRVs.reserve(maxTexturesNeeded);
                    
                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO, 
                            L"[SceneManager] Pre-allocated %d texture slots for scene_models[%d] (%d primitives)", 
                            static_cast<int>(maxTexturesNeeded), instanceIndex, static_cast<int>(numPrimitives));
                    #endif
                }
            }
        }

        // Set parent-child relationship: -1 for parent models, parent's instanceIndex for children
        scene_models[instanceIndex].m_modelInfo.iParentModelID = parentModelID;

        // Extract position from the world transformation matrix for positioning
        XMFLOAT4X4 f4x4;
        XMStoreFloat4x4(&f4x4, worldTransform);
        scene_models[instanceIndex].m_modelInfo.position = XMFLOAT3(f4x4._41, f4x4._42, f4x4._43);
        
        // Reset scale to identity since it's been baked into geometry
        XMStoreFloat3(&scene_models[instanceIndex].m_modelInfo.scale, XMVectorSet(1.0f, 1.0f, 1.0f, 0));
        
        // Assign unique instance ID and descriptive name
        scene_models[instanceIndex].m_modelInfo.ID = instanceIndex;
        scene_models[instanceIndex].m_modelInfo.name = modelName;

        // Setup model for rendering and apply default lighting
        scene_models[instanceIndex].SetupModelForRendering(scene_models[instanceIndex].m_modelInfo.ID);
        scene_models[instanceIndex].ApplyDefaultLightingFromManager(lightsManager);
        
        scene_models[instanceIndex].m_isLoaded = true;

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] scene_models[%d] lighting: %d local lights applied.",
                instanceIndex, static_cast<int>(scene_models[instanceIndex].m_modelInfo.localLights.size()));
        #endif

        // Apply exporter-specific patches for compatibility if needed
        const std::wstring& exp = GetLastDetectedExporter();
        if (exp == L"Sketchfab")
        {
            // Apply Sketchfab-specific transformations or corrections if needed
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Sketchfab GLTF scene loaded. Patch applied.");
            #endif
        }
        else if (exp == L"Blender")
        {
            // Apply Blender-specific transformations or corrections if needed
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Blender GLTF scene loaded. No patch applied.");
            #endif
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] scene_models[%d] created: \"%ls\" | ParentID: %d | Pos(%.2f, %.2f, %.2f) | Scale baked",
                instanceIndex,
                scene_models[instanceIndex].m_modelInfo.name.c_str(),
                scene_models[instanceIndex].m_modelInfo.iParentModelID,
                f4x4._41, f4x4._42, f4x4._43);
        #endif

        // Update current parent ID for child nodes (if this node becomes a parent)
        currentParentID = instanceIndex;
        
        // Increment instance counter for next model
        ++instanceIndex;
    }

    // Process child nodes recursively, passing current node as parent
    if (node.contains("children") && node["children"].is_array())
    {
        for (const auto& childIndex : node["children"])
        {
            // Validate child index is a valid integer
            if (!childIndex.is_number_integer()) continue;
            int ci = childIndex.get<int>();
            
            // Validate child index is within valid range
            if (ci < 0 || ci >= (int)allNodes.size()) continue;

            // Recursively parse child node with updated parent transformation and parent ID
            ParseGLTFNodeRecursive(allNodes[ci], parentTransform * nodeTransform, doc, allNodes, instanceIndex, currentParentID);
        }
    }
}

// --------------------------------------------------------------------------------------------------

// Enhanced LoadGLTFMeshPrimitives with comprehensive debug output
// Replace the existing LoadGLTFMeshPrimitives function with this version
void SceneManager::LoadGLTFMeshPrimitives(int meshIndex, const json& doc, Model& model)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] LoadGLTFMeshPrimitives() - meshIndex: %d", meshIndex);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] gltfBinaryData size: %d bytes", static_cast<int>(gltfBinaryData.size()));
    #endif

    // Validate required GLTF sections. (Prevents invalid JSON access.)
    if (!doc.contains("meshes") || !doc.contains("accessors") || !doc.contains("bufferViews"))
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] Missing required GLB sections: meshes, accessors, or bufferViews");
        #endif
        return;
    }

    const auto& meshes = doc["meshes"];
    const auto& accessors = doc["accessors"];
    const auto& bufferViews = doc["bufferViews"];

    // Validate mesh index range. (Prevents out of range mesh selection.)
    if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size()))
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_ERROR,
                L"[SceneManager] Invalid mesh index: %d (max: %d)",
                meshIndex,
                static_cast<int>(meshes.size()));
        #endif
        return;
    }

    const auto& mesh = meshes[meshIndex];

    // Validate primitives array. (No primitives means no geometry.)
    if (!mesh.contains("primitives"))
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] Mesh has no primitives array");
        #endif
        return;
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(
            LogLevel::LOG_INFO,
            L"[SceneManager] Processing %d primitives",
            static_cast<int>(mesh["primitives"].size()));
    #endif

    // Clear output arrays for this model. (Ensures no stale geometry remains.)
    model.m_modelInfo.vertices.clear();
    model.m_modelInfo.indices.clear();

    // Validate binary buffer presence. (GLB must have data for accessors.)
    if (gltfBinaryData.empty())
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[SceneManager] CRITICAL: gltfBinaryData is empty - cannot load vertex/index data!");
        #endif
        return;
    }

    // Pre-allocate texture vectors to prevent reallocations. (Keeps existing behavior.)
    size_t numPrimitives = mesh["primitives"].size(); // Number of primitives to process.
    size_t maxTexturesNeeded = numPrimitives * 3;     // Albedo + Normal + Fallback worst-case.

    model.m_modelInfo.textures.clear();               // Clear textures list for this model instance.
    model.m_modelInfo.textures.reserve(maxTexturesNeeded); // Reserve to avoid frequent reallocation.
    model.m_modelInfo.textureSRVs.clear();            // Clear SRV list for this model instance.
    model.m_modelInfo.textureSRVs.reserve(maxTexturesNeeded); // Reserve to match texture list.
    model.m_modelInfo.normalMapSRVs.clear();          // Clear normal SRV list for this model instance.
    model.m_modelInfo.normalMapSRVs.reserve(maxTexturesNeeded); // Reserve to match expected usage.

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(
            LogLevel::LOG_INFO,
            L"[SceneManager] PRE-ALLOCATED %d texture slots on models[] array for %d primitives",
            static_cast<int>(maxTexturesNeeded),
            static_cast<int>(numPrimitives));
    #endif

    for (const auto& prim : mesh["primitives"])
    {
        // Ensure primitive has attributes. (Required for POSITION and others.)
        if (!prim.contains("attributes"))
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] Primitive missing attributes - skipping");
            #endif
            continue;
        }

        const auto& attributes = prim["attributes"];
        int posAccessor = attributes.value("POSITION", -1);
        int idxAccessor = prim.value("indices", -1);

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] Position accessor: %d, Index accessor: %d",
                posAccessor,
                idxAccessor);
        #endif

        // Validate accessor indices. (Prevents out-of-range JSON accessors.)
        if (posAccessor < 0 || posAccessor >= (int)accessors.size() || idxAccessor < 0 || idxAccessor >= (int)accessors.size())
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] Invalid accessor indices - pos: %d, idx: %d (max: %d)",
                    posAccessor,
                    idxAccessor,
                    static_cast<int>(accessors.size()));
            #endif
            continue;
        }

        // -----------------------------
        // Load RAW positions (float3)
        // -----------------------------
        const auto& posAcc = accessors[posAccessor];
        int posViewIdx = posAcc.value("bufferView", -1);

        // Validate bufferView index for positions.
        if (posViewIdx < 0 || posViewIdx >= (int)bufferViews.size())
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] Invalid position bufferView index: %d (max: %d)",
                    posViewIdx,
                    static_cast<int>(bufferViews.size()));
            #endif
            continue;
        }

        size_t posOffset = (size_t)bufferViews[posViewIdx].value("byteOffset", 0) + (size_t)posAcc.value("byteOffset", 0);
        int vertexCount = posAcc.value("count", 0);

        // Validate vertexCount and buffer bounds for positions. (12 bytes per vertex for float3.)
        if (vertexCount <= 0)
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] POSITION accessor has zero vertices - skipping primitive");
            #endif
            continue;
        }

        const size_t posBytesNeeded = (size_t)vertexCount * (size_t)12; // float3
        if (posOffset > gltfBinaryData.size() || (posOffset + posBytesNeeded) > gltfBinaryData.size())
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] CRITICAL: Position data out of bounds. Offset=%d Needed=%d BufferSize=%d",
                    static_cast<int>(posOffset),
                    static_cast<int>(posBytesNeeded),
                    static_cast<int>(gltfBinaryData.size()));
            #endif
            continue;
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] Loading %d vertices from offset %d",
                vertexCount,
                static_cast<int>(posOffset));
        #endif

        std::vector<Vertex> rawVertices((size_t)vertexCount); // Allocate vertex container for this primitive.

        // Decode float3 positions.
        for (int vi = 0; vi < vertexCount; ++vi)
        {
            const size_t base = posOffset + (size_t)vi * (size_t)12; // Base byte offset for this vertex.
            rawVertices[(size_t)vi].position.x = *reinterpret_cast<const float*>(&gltfBinaryData[base + 0]);
            rawVertices[(size_t)vi].position.y = *reinterpret_cast<const float*>(&gltfBinaryData[base + 4]);
            rawVertices[(size_t)vi].position.z = *reinterpret_cast<const float*>(&gltfBinaryData[base + 8]);
            rawVertices[(size_t)vi].normal = XMFLOAT3(0, 1, 0);       // Default normal if none provided.
            rawVertices[(size_t)vi].texCoord = XMFLOAT2(0, 0);        // Default UV if none provided.
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] First vertex position: (%.3f, %.3f, %.3f)",
                rawVertices[0].position.x,
                rawVertices[0].position.y,
                rawVertices[0].position.z);
        #endif

        // -----------------------------
        // Load normals (float3) if present
        // -----------------------------
        if (attributes.contains("NORMAL"))
        {
            int normAccIdx = attributes.value("NORMAL", -1); // Normal accessor index.
            if (normAccIdx >= 0 && normAccIdx < (int)accessors.size())
            {
                const auto& normAcc = accessors[normAccIdx];
                int normViewIdx = normAcc.value("bufferView", -1);

                // Validate normal bufferView.
                if (normViewIdx >= 0 && normViewIdx < (int)bufferViews.size())
                {
                    size_t normOffset = (size_t)bufferViews[normViewIdx].value("byteOffset", 0) + (size_t)normAcc.value("byteOffset", 0);
                    const size_t normBytesNeeded = (size_t)vertexCount * (size_t)12; // float3 normals.

                    // Validate bounds for normals.
                    if (normOffset <= gltfBinaryData.size() && (normOffset + normBytesNeeded) <= gltfBinaryData.size())
                    {
                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Loading normals from offset %d", static_cast<int>(normOffset));
                        #endif

                        for (int vi = 0; vi < vertexCount; ++vi)
                        {
                            const size_t base = normOffset + (size_t)vi * (size_t)12; // Normal base offset.
                            rawVertices[(size_t)vi].normal.x = *reinterpret_cast<const float*>(&gltfBinaryData[base + 0]);
                            rawVertices[(size_t)vi].normal.y = *reinterpret_cast<const float*>(&gltfBinaryData[base + 4]);
                            rawVertices[(size_t)vi].normal.z = *reinterpret_cast<const float*>(&gltfBinaryData[base + 8]);
                        }
                    }
                    else
                    {
                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(
                                LogLevel::LOG_WARNING,
                                L"[SceneManager] NORMAL data out of bounds. Offset=%d Needed=%d BufferSize=%d. Using default normals.",
                                static_cast<int>(normOffset),
                                static_cast<int>(normBytesNeeded),
                                static_cast<int>(gltfBinaryData.size()));
                        #endif
                    }
                }
                else
                {
                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Invalid normal bufferView index: %d. Using default normals.", normViewIdx);
                    #endif
                }
            }
        }

        // -----------------------------
        // Load UVs (float2) if present
        // -----------------------------
        if (attributes.contains("TEXCOORD_0"))
        {
            int texAccIdx = attributes.value("TEXCOORD_0", -1); // UV accessor index.
            if (texAccIdx >= 0 && texAccIdx < (int)accessors.size())
            {
                const auto& texAcc = accessors[texAccIdx];
                int texViewIdx = texAcc.value("bufferView", -1);

                // Validate UV bufferView.
                if (texViewIdx >= 0 && texViewIdx < (int)bufferViews.size())
                {
                    size_t texOffset = (size_t)bufferViews[texViewIdx].value("byteOffset", 0) + (size_t)texAcc.value("byteOffset", 0);
                    const size_t texBytesNeeded = (size_t)vertexCount * (size_t)8; // float2 uvs.

                    // Validate bounds for UVs.
                    if (texOffset <= gltfBinaryData.size() && (texOffset + texBytesNeeded) <= gltfBinaryData.size())
                    {
                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Loading texture coordinates from offset %d", static_cast<int>(texOffset));
                        #endif

                        for (int vi = 0; vi < vertexCount; ++vi)
                        {
                            const size_t base = texOffset + (size_t)vi * (size_t)8; // UV base offset.
                            rawVertices[(size_t)vi].texCoord.x = *reinterpret_cast<const float*>(&gltfBinaryData[base + 0]);
                            rawVertices[(size_t)vi].texCoord.y = *reinterpret_cast<const float*>(&gltfBinaryData[base + 4]);
                        }
                    }
                    else
                    {
                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(
                                LogLevel::LOG_WARNING,
                                L"[SceneManager] TEXCOORD_0 data out of bounds. Offset=%d Needed=%d BufferSize=%d. Using default UVs.",
                                static_cast<int>(texOffset),
                                static_cast<int>(texBytesNeeded),
                                static_cast<int>(gltfBinaryData.size()));
                        #endif
                    }
                }
            }
        }

        // -----------------------------
        // Load indices (u8/u16/u32)
        // -----------------------------
        const auto& idxAcc = accessors[idxAccessor];
        int idxViewIdx = idxAcc.value("bufferView", -1);

        // Validate index bufferView before accessing.
        if (idxViewIdx < 0 || idxViewIdx >= (int)bufferViews.size())
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] Invalid index bufferView index: %d (max: %d)",
                    idxViewIdx,
                    static_cast<int>(bufferViews.size()) - 1);
            #endif
            continue;
        }

        int idxCount = idxAcc.value("count", 0);                 // Number of indices.
        int idxComponentType = idxAcc.value("componentType", 0); // GLTF component type.
        size_t idxOffset = (size_t)bufferViews[idxViewIdx].value("byteOffset", 0) + (size_t)idxAcc.value("byteOffset", 0);

        // Validate idxCount.
        if (idxCount <= 0)
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SceneManager] Indices accessor has zero indices - skipping primitive");
            #endif
            continue;
        }

        // Determine index stride in bytes.
        size_t bytesPerIndex = 0; // Byte size per index based on component type.
        if (idxComponentType == 5121) bytesPerIndex = 1; // UNSIGNED_BYTE
        if (idxComponentType == 5123) bytesPerIndex = 2; // UNSIGNED_SHORT
        if (idxComponentType == 5125) bytesPerIndex = 4; // UNSIGNED_INT

        // Reject unsupported component types.
        if (bytesPerIndex == 0)
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] Unsupported index componentType: %d - skipping primitive",
                    idxComponentType);
            #endif
            continue;
        }

        const size_t idxBytesNeeded = (size_t)idxCount * bytesPerIndex; // Total bytes needed for indices.

        // Validate index data bounds.
        if (idxOffset > gltfBinaryData.size() || (idxOffset + idxBytesNeeded) > gltfBinaryData.size())
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] CRITICAL: Index data out of bounds. Offset=%d Needed=%d BufferSize=%d - skipping primitive",
                    static_cast<int>(idxOffset),
                    static_cast<int>(idxBytesNeeded),
                    static_cast<int>(gltfBinaryData.size()));
            #endif
            continue;
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] Loading %d indices from offset %d (component type: %d)",
                idxCount,
                static_cast<int>(idxOffset),
                idxComponentType);
        #endif

        std::vector<uint32_t> rawIndices((size_t)idxCount); // Index buffer for this primitive.

        // Decode indices to uint32_t.
        for (int k = 0; k < idxCount; ++k)
        {
            const size_t base = idxOffset + (size_t)k * bytesPerIndex; // Base offset for current index.
            if (idxComponentType == 5121) rawIndices[(size_t)k] = (uint32_t)gltfBinaryData[base];
            if (idxComponentType == 5123) rawIndices[(size_t)k] = (uint32_t)(*reinterpret_cast<const uint16_t*>(&gltfBinaryData[base]));
            if (idxComponentType == 5125) rawIndices[(size_t)k] = (uint32_t)(*reinterpret_cast<const uint32_t*>(&gltfBinaryData[base]));
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] Raw data loaded - vertices: %d, indices: %d",
                static_cast<int>(rawVertices.size()),
                static_cast<int>(rawIndices.size()));
        #endif

        // -----------------------------
        // Validate indices against vertex count (CRITICAL)
        // -----------------------------
        bool hasInvalidIndex = false; // Tracks whether this primitive references invalid vertices.
        uint32_t maxIndexSeen = 0;     // Tracks the maximum index value seen for debug output.

        for (size_t i = 0; i < rawIndices.size(); ++i)
        {
            const uint32_t idx = rawIndices[i]; // Index value.
            if (idx > maxIndexSeen) maxIndexSeen = idx; // Track maximum index.
            if ((size_t)idx >= rawVertices.size()) hasInvalidIndex = true; // Detect out-of-range index.
        }

        if (hasInvalidIndex)
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(
                    LogLevel::LOG_ERROR,
                    L"[SceneManager] CRITICAL: Primitive has out-of-range indices. MaxIndex=%d VertexCount=%d - skipping primitive to prevent heap corruption",
                    static_cast<int>(maxIndexSeen),
                    static_cast<int>(rawVertices.size()));
            #endif
            continue;
        }

        // -----------------------------
        // Weld vertices (safe, since indices are validated)
        // -----------------------------
        struct VertexKey
        {
            XMFLOAT3 pos;  // Position component for hashing.
            XMFLOAT3 norm; // Normal component for hashing.
            XMFLOAT2 uv;   // UV component for hashing.

            bool operator==(const VertexKey& other) const
            {
                return memcmp(this, &other, sizeof(VertexKey)) == 0; // Bitwise compare for exact match.
            }
        };

        struct VertexKeyHasher
        {
            size_t operator()(const VertexKey& key) const
            {
                size_t h1 = std::hash<float>()(key.pos.x) ^ std::hash<float>()(key.pos.y) ^ std::hash<float>()(key.pos.z); // Hash position.
                size_t h2 = std::hash<float>()(key.norm.x) ^ std::hash<float>()(key.norm.y) ^ std::hash<float>()(key.norm.z); // Hash normal.
                size_t h3 = std::hash<float>()(key.uv.x) ^ std::hash<float>()(key.uv.y); // Hash uv.
                return h1 ^ h2 ^ h3; // Combine hashes.
            }
        };

        std::unordered_map<VertexKey, uint32_t, VertexKeyHasher> uniqueVerts; // Tracks unique vertex mapping.

        for (size_t ii = 0; ii < rawIndices.size(); ++ii)
        {
            const uint32_t idx = rawIndices[ii];        // Validated index.
            const Vertex& v = rawVertices[(size_t)idx]; // Safe vertex fetch.
            VertexKey key;                               // Vertex key for hashing.
            key.pos = v.position;                        // Copy position.
            key.norm = v.normal;                         // Copy normal.
            key.uv = v.texCoord;                         // Copy uv.

            auto it = uniqueVerts.find(key);             // Find existing vertex mapping.
            if (it != uniqueVerts.end())
            {
                model.m_modelInfo.indices.push_back(it->second); // Reuse existing welded index.
            }
            else
            {
                uint32_t newIndex = static_cast<uint32_t>(model.m_modelInfo.vertices.size()); // New welded index.
                uniqueVerts[key] = newIndex;               // Store mapping.
                model.m_modelInfo.vertices.push_back(v);   // Store unique vertex.
                model.m_modelInfo.indices.push_back(newIndex); // Store index.
            }
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] After welding - Final vertices: %d, Final indices: %d",
                static_cast<int>(model.m_modelInfo.vertices.size()),
                static_cast<int>(model.m_modelInfo.indices.size()));
        #endif

        // -----------------------------
        // Generate Tangents (requires triangle list)
        // -----------------------------
        if (!model.m_modelInfo.vertices.empty() && model.m_modelInfo.indices.size() >= 3)
        {
            // Ensure we do not walk beyond the index array if it is not a multiple of 3.
            const size_t triLimit = (model.m_modelInfo.indices.size() / 3) * 3; // Largest multiple of 3.

            std::vector<XMFLOAT3> tangentAccum(model.m_modelInfo.vertices.size(), XMFLOAT3(0, 0, 0)); // Accumulator array.

            for (size_t i = 0; i < triLimit; i += 3)
            {
                uint32_t i0 = model.m_modelInfo.indices[i + 0]; // Triangle index 0.
                uint32_t i1 = model.m_modelInfo.indices[i + 1]; // Triangle index 1.
                uint32_t i2 = model.m_modelInfo.indices[i + 2]; // Triangle index 2.

                // Validate triangle indices into vertex array (defensive).
                if ((size_t)i0 >= model.m_modelInfo.vertices.size()) continue; // Skip invalid triangle.
                if ((size_t)i1 >= model.m_modelInfo.vertices.size()) continue; // Skip invalid triangle.
                if ((size_t)i2 >= model.m_modelInfo.vertices.size()) continue; // Skip invalid triangle.

                const Vertex& v0 = model.m_modelInfo.vertices[(size_t)i0]; // Vertex 0.
                const Vertex& v1 = model.m_modelInfo.vertices[(size_t)i1]; // Vertex 1.
                const Vertex& v2 = model.m_modelInfo.vertices[(size_t)i2]; // Vertex 2.

                XMVECTOR p0 = XMLoadFloat3(&v0.position); // Load position 0.
                XMVECTOR p1 = XMLoadFloat3(&v1.position); // Load position 1.
                XMVECTOR p2 = XMLoadFloat3(&v2.position); // Load position 2.

                float du1 = v1.texCoord.x - v0.texCoord.x; // Delta u for edge 0->1.
                float dv1 = v1.texCoord.y - v0.texCoord.y; // Delta v for edge 0->1.
                float du2 = v2.texCoord.x - v0.texCoord.x; // Delta u for edge 0->2.
                float dv2 = v2.texCoord.y - v0.texCoord.y; // Delta v for edge 0->2.

                XMVECTOR deltaPos1 = p1 - p0;              // Position delta 0->1.
                XMVECTOR deltaPos2 = p2 - p0;              // Position delta 0->2.

                float r = (du1 * dv2 - du2 * dv1);         // Compute determinant.
                r = (fabs(r) < 1e-8f) ? 1.0f : 1.0f / r;   // Prevent divide-by-zero.

                XMVECTOR tangent = (deltaPos1 * dv2 - deltaPos2 * dv1) * r; // Tangent direction.

                XMFLOAT3 tan;                               // Temporary tangent storage.
                XMStoreFloat3(&tan, tangent);               // Store tangent vector.

                tangentAccum[(size_t)i0].x += tan.x; tangentAccum[(size_t)i0].y += tan.y; tangentAccum[(size_t)i0].z += tan.z; // Accumulate.
                tangentAccum[(size_t)i1].x += tan.x; tangentAccum[(size_t)i1].y += tan.y; tangentAccum[(size_t)i1].z += tan.z; // Accumulate.
                tangentAccum[(size_t)i2].x += tan.x; tangentAccum[(size_t)i2].y += tan.y; tangentAccum[(size_t)i2].z += tan.z; // Accumulate.
            }

            for (size_t i = 0; i < model.m_modelInfo.vertices.size(); ++i)
            {
                XMVECTOR tan = XMLoadFloat3(&tangentAccum[i]); // Load accumulated tangent.
                tan = XMVector3Normalize(tan);                 // Normalize tangent.
                XMStoreFloat3(&model.m_modelInfo.vertices[i].tangent, tan); // Store to vertex.
            }
        }

        // Handle material if available. (This now runs after safe geometry processing.)
        if (prim.contains("material"))
        {
            int matIndex = prim.value("material", -1); // Material index for this primitive.
            BindGLTFMaterialTexturesToModel(matIndex, model.m_modelInfo, model, doc); // Bind textures/material into model.
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

// --------------------------------------------------------------------------------------------------
// SceneManager::UpdateSceneAnimations()
// Updates all active animations in the current scene by delegating to the global animator
// Should be called every frame with the current deltaTime to maintain smooth animation playback
// --------------------------------------------------------------------------------------------------
void SceneManager::UpdateSceneAnimations(float deltaTime)
{
#if defined(_DEBUG_SCENEMANAGER_)
    static float debugTimer = 0.0f;
    debugTimer += deltaTime;
    if (debugTimer >= 5.0f) // Log every 5 seconds
    {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[SceneManager] Updating scene animations with deltaTime: %.4f", deltaTime);
        debugTimer = 0.0f;
    }
#endif

    // Only update animations if they were successfully loaded from the current scene
    if (bAnimationsLoaded)
    {
        // Update all animations using the scene models array
        gltfAnimator.UpdateAnimations(deltaTime, scene_models, MAX_SCENE_MODELS);
    }
}

// --------------------------------------------------------------------------------------------------
// SceneManager::FindParentModelID()
// Retrieves the ID from the appropriate Model Name within the scene_models array.
// Returns the ModelID for the specified model, or -1 if model not found.
// This function searches through all loaded scene models to find the matching name.
// --------------------------------------------------------------------------------------------------
int SceneManager::FindParentModelID(const std::wstring& modelName)
{
    // Search through all scene models to find the specified model name
    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        // Check if this slot contains a loaded model with matching name
        if (scene_models[i].m_isLoaded && scene_models[i].m_modelInfo.name == modelName && 
            scene_models[i].m_modelInfo.iParentModelID == -1)
        {
            // Found the model, return its parent ID
            int parentID = i;
            return parentID;
        }
    }

    // Model not found in scene_models array
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Model \"%ls\" not found in scene_models array", modelName.c_str());
    #endif
    
    return -1;  // Return -1 to indicate model not found
}

// --------------------------------------------------------------------------------------------------
// SceneManager::DiagnoseGLBParsing()
// Diagnostic function to identify where GLB parsing is failing.
// Add this to SceneManager.h as a private function and call it in ParseGLBScene.
// This will help identify if the issue is with binary data, vertex loading, or model creation.
// --------------------------------------------------------------------------------------------------
void SceneManager::DiagnoseGLBParsing(const std::wstring& glbFile)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] === GLB DIAGNOSTIC START ===");
    #endif

    // Check if ParseGLBScene was successful
    bool parseResult = ParseGLBScene(glbFile);
    
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] ParseGLBScene() result: %s", parseResult ? L"SUCCESS" : L"FAILED");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] gltfBinaryData size: %d bytes", static_cast<int>(gltfBinaryData.size()));
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Detected exporter: %ls", m_lastDetectedExporter.c_str());
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Animations loaded: %s", bAnimationsLoaded ? L"YES" : L"NO");
    #endif

    // Check scene_models array for loaded models
    int loadedModels = 0;
    int modelsWithVertices = 0;
    int modelsWithIndices = 0;
    
    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (scene_models[i].m_isLoaded)
        {
            loadedModels++;
            
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, 
                    L"[SceneManager] scene_models[%d]: \"%ls\" | Vertices: %d | Indices: %d | ParentID: %d",
                    i, 
                    scene_models[i].m_modelInfo.name.c_str(),
                    static_cast<int>(scene_models[i].m_modelInfo.vertices.size()),
                    static_cast<int>(scene_models[i].m_modelInfo.indices.size()),
                    scene_models[i].m_modelInfo.iParentModelID);
            #endif
            
            if (!scene_models[i].m_modelInfo.vertices.empty()) modelsWithVertices++;
            if (!scene_models[i].m_modelInfo.indices.empty()) modelsWithIndices++;
        }
    }
    
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Total loaded models: %d", loadedModels);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Models with vertices: %d", modelsWithVertices);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Models with indices: %d", modelsWithIndices);
        
        if (loadedModels == 0)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] CRITICAL: No models loaded into scene_models[]!");
        }
        else if (modelsWithVertices == 0)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] CRITICAL: Models loaded but contain no vertex data!");
        }
        else if (modelsWithIndices == 0)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] CRITICAL: Models loaded but contain no index data!");
        }
        else
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Models appear to have valid geometry data.");
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] === GLB DIAGNOSTIC END ===");
    #endif
}