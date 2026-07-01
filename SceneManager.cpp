// SceneManager.cpp (continued)
#include "Includes.h"
#include "SceneManager.h"
#include "BlenderImports.h"
#include "FBXImport.h"
#include "ThreadLockHelper.h"
#include "Renderer.h"
#if defined(__USE_DIRECTX_11__)
    #include "DX11Renderer.h"
#elif defined(__USE_DIRECTX_12__)
    #include "DX12Renderer.h"
#elif defined(__USE_OPENGL__)
    #include "OpenGLRenderer.h"
#elif defined(__USE_VULKAN__)
    #include "VULKAN_Renderer.h"
#endif

#include "FXManager.h"
#include "Debug.h"
#include "Lights.h"

#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID) || defined(PLATFORM_APPLE) || defined(PLATFORM_IOS)
    #include <unistd.h>
    #include <sys/stat.h>
#endif

#include <nlohmann/json.hpp>
#include <unordered_set>

using json = nlohmann::json;

extern Model models[MAX_MODELS];                                                    // Global Base Model Pool
extern Debug debug;
extern LightsManager lightsManager;
extern ThreadManager threadManager;
extern SystemUtils   sysUtils;
extern FXManager     fxManager;

// Abstract Renderer Pointer
extern std::shared_ptr<Renderer> renderer;

// --------------------------------------------------------------------------------------------------
// Constructor
// Initializes scene state, default type, and model registry.
// --------------------------------------------------------------------------------------------------
SceneManager::SceneManager()
{
    stSceneType = SCENE_INTRO;

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Constructor called. Scene type set to SCENE_INTRO.");
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

    #if defined(_DEBUG_SCENEMANAGER_)
        int _firstDestroyed = -1, _lastDestroyed = -1, _destroyCount = 0;
    #endif
    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (scene_models[i].m_isLoaded)
        {
            scene_models[i].DestroyModel();
            #if defined(_DEBUG_SCENEMANAGER_)
                if (_firstDestroyed < 0) _firstDestroyed = i;
                _lastDestroyed = i;
                ++_destroyCount;
            #endif
        }
    }

#if defined(__USE_VULKAN__)
    // Vulkan: DestroyModel() frees GPU buffers from scene_models[] and nulls their handles,
    // but the models[] cache entries that were CopyFrom'd still hold the same now-freed handles.
    // On the next scene load the fast-path cache restore (CopyFrom → bGpuReady check) would
    // copy those stale handles into scene_models[] and use them in draw calls → crash.
    // Reset all Vulkan GPU handles and bGpuReady on every models[] entry that was GPU-ready
    // so the next ParseGLTFScene always calls SetupModelForRendering to create fresh buffers.
    //
    // ALSO: Release Texture shared_ptrs from models[] cache NOW, while the renderer (and
    // therefore the VkDevice) is still alive.  scene_models[i].DestroyModel() above already
    // reduced the Texture ref-count by 1, but models[] is a program-lifetime global whose
    // destructor fires AFTER main() returns - at that point the VkDevice is gone, so
    // Texture::~Texture() cannot call vkFreeMemory/vkDestroyImage, producing the 720+
    // VkDeviceMemory leaks reported by the validation layer.  Clearing here drops the final
    // reference while the device is valid and lets the Texture destructors clean up correctly.
    for (int mi = 0; mi < MAX_MODELS; ++mi)
    {
        if (!models[mi].m_modelInfo.bGpuReady) continue;
        // Release embedded texture GPU resources (Texture::~Texture fires here while device alive)
        models[mi].m_modelInfo.textures.clear();
        models[mi].m_materials.clear();
        models[mi].m_modelInfo.vertexBuffer        = VK_NULL_HANDLE;
        models[mi].m_modelInfo.vertexBufferMemory  = VK_NULL_HANDLE;
        models[mi].m_modelInfo.indexBuffer         = VK_NULL_HANDLE;
        models[mi].m_modelInfo.indexBufferMemory   = VK_NULL_HANDLE;
        models[mi].m_modelInfo.uniformBuffer               = VK_NULL_HANDLE;
        models[mi].m_modelInfo.uniformBufferMemory         = VK_NULL_HANDLE;
        models[mi].m_modelInfo.uniformBufferMapped         = nullptr;
        models[mi].m_modelInfo.materialUniformBuffer       = VK_NULL_HANDLE;
        models[mi].m_modelInfo.materialUniformBufferMemory = VK_NULL_HANDLE;
        models[mi].m_modelInfo.materialUniformBufferMapped = nullptr;
        models[mi].m_modelInfo.descriptorSet               = VK_NULL_HANDLE;
        models[mi].m_modelInfo.textureDescriptorSet        = VK_NULL_HANDLE;
        models[mi].m_modelInfo.pipeline                    = VK_NULL_HANDLE;
        models[mi].m_modelInfo.pipelineLayout              = VK_NULL_HANDLE;
        models[mi].m_modelInfo.bGpuReady                   = false;
    }
#endif

    #if defined(_DEBUG_SCENEMANAGER_)
        if (_destroyCount > 0)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[SceneManager] scene_models[%d-%d] released (%d total). CleanUp() complete.",
                _firstDestroyed, _lastDestroyed, _destroyCount);
        else
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[SceneManager] CleanUp() called - no loaded models to release.");
    #endif
}

bool SceneManager::Initialize(std::shared_ptr<Renderer> renderer)
{
#if defined(__USE_DIRECTX_11__)
    {
        auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
        if (!dx11)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] DX11Renderer cast failed.");
            return false;
        }
        myRenderer = dx11.get();
    }
#elif defined(__USE_DIRECTX_12__)
    {
        auto dx12 = std::dynamic_pointer_cast<DX12Renderer>(renderer);
        if (!dx12)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] DX12Renderer cast failed.");
            return false;
        }
        myRenderer = dx12.get();
    }
#elif defined(__USE_OPENGL__)
    {
        auto gl = std::dynamic_pointer_cast<OpenGLRenderer>(renderer);
        if (!gl)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] OpenGLRenderer cast failed.");
            return false;
        }
        myRenderer = gl.get();
    }
#elif defined(__USE_VULKAN__)
    {
        auto vk = std::dynamic_pointer_cast<VulkanRenderer>(renderer);
        if (!vk)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SceneManager] VulkanRenderer cast failed.");
            return false;
        }
        myRenderer = vk.get();
    }
#endif

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Initialize() called.");
    #endif

    sceneFrameCounter = 0;

    // Bind scene model array so UpdateAnimations() needs no extra arguments from callers
    modelAnimator.SetModels(scene_models, MAX_SCENE_MODELS);

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
bool SceneManager::ParseGLBScene(const std::wstring& glbFile, bool bCacheOnly)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        const auto _sceneLoadBegin = std::chrono::high_resolution_clock::now();
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] ParseGLBScene() LOAD BEGIN - %ls", glbFile.c_str());
    #endif

    bLoadedFromCache = false;
    m_fbxCameras.clear();   // clear FBX camera list on every new scene load
    lightsManager.ClearLights(); // clear lights from any prior scene before parsing new ones

    // Loading-text progress helper -- same pattern as showStage in IOLoaderThread.
    auto showStage = [](const wchar_t* msg) {
        TextRenderStyle s;
        s.fontName = LoadingTextFX::kFontName;
        s.fontSize = 20.0f;
        s.centered = true;
        fxManager.ShowLoadingText(msg,
            XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
            0.2f, 0.05f,
            XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f),
            0.0f, -1.0f, &s);
    };

    // =========================================================================
    // GEOMETRY PRE-CACHE FAST PATH
    // On second+ load of the same GLB, models[] already holds GPU-ready geometry
    // and full scene-instance data (transforms, hierarchy, TRS) written back on
    // the first parse.  Re-parse the JSON+BIN chunk FIRST to restore per-load
    // session state (exporter, blender axis config, camera, lights, materials,
    // animations) BEFORE restoring scene_models[], so that geometry flip, winding
    // order, UV orientation, and animation binding all match the first load.
    // GPU resources and textures are rebuilt/rebound for every active renderer.
    // =========================================================================
    {
        int cacheCount = 0;
        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.sourceSceneFile == glbFile &&
                models[m].m_modelInfo.bGpuReady &&
                models[m].m_modelInfo.cachedInstanceIndex >= 0)
                ++cacheCount;
        }

        if (cacheCount > 0)
        {
            // --- Step 1: Parse GLB document BEFORE touching any scene_models[] ---
            // DetectGLTFExporter + BlenderImports::BuildConfig must run first so
            // m_blenderConfig is correct when any transform or geometry code runs.
            bool miniParseOK = false;
            json miniDoc;

            if (std::filesystem::exists(glbFile))
            {
                std::ifstream miniF(glbFile, std::ios::binary);
                if (miniF.is_open())
                {
                    struct { uint32_t magic, version, length; } hdr{};
                    miniF.read(reinterpret_cast<char*>(&hdr), 12);
                    if (hdr.magic == 0x46546C67 && hdr.version == 2)
                    {
                        struct { uint32_t len, type; } jc{};
                        miniF.read(reinterpret_cast<char*>(&jc), 8);
                        if (jc.type == 0x4E4F534A && jc.len > 0)
                        {
                            std::string jsonStr(jc.len, '\0');
                            miniF.read(&jsonStr[0], jc.len);

                            gltfBinaryData.clear();
                            size_t binOff = 20 + jc.len;
                            while (binOff % 4) ++binOff;
                            miniF.seekg(static_cast<std::streamoff>(binOff));
                            struct { uint32_t len, type; } bc{};
                            if (miniF.read(reinterpret_cast<char*>(&bc), 8) && bc.type == 0x004E4942)
                            {
                                gltfBinaryData.resize(bc.len);
                                miniF.read(reinterpret_cast<char*>(gltfBinaryData.data()), bc.len);
                            }

                            try { miniDoc = json::parse(jsonStr); miniParseOK = true; }
                            catch (const std::exception&)
                            {
                                debug.logLevelMessage(LogLevel::LOG_ERROR,
                                    L"[SceneManager] CACHE-RESTORE: GLB JSON parse failed - falling through to full parse");
                            }
                        }
                    }
                    miniF.close();
                }
            }

            if (!miniParseOK)
            {
                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING,
                        L"[SceneManager] ParseGLBScene() mini-parse failed - falling through to full parse");
                #endif
            }
            else
            {
                // --- Step 2: Restore per-load session state (same order as full parse) ---
                {
                    std::string generator;
                    if (miniDoc.contains("asset") && miniDoc["asset"].contains("generator") &&
                        miniDoc["asset"]["generator"].is_string())
                        generator = miniDoc["asset"]["generator"].get<std::string>();
                    DetectGLTFExporter(miniDoc);
                    m_blenderConfig = BlenderImports::BuildConfig(generator, miniDoc);
                }
                if (myRenderer)
                    ParseGLTFCamera(miniDoc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
                ParseGLTFLights(miniDoc);
                EnsureDefaultSunLight();
                ParseMaterialsFromGLTF(miniDoc);
                modelAnimator.gltfAnimator.ClearAllAnimations();
                bAnimationsLoaded = modelAnimator.gltfAnimator.ParseAnimationsFromGLTF(miniDoc, gltfBinaryData);
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    std::wstring(L"[SceneManager] CACHE-RESTORE GLB: animations parsed=") +
                    (bAnimationsLoaded ? L"true" : L"false") + L" count=" +
                    std::to_wstring(modelAnimator.gltfAnimator.GetAnimationCount()));
                if (bAnimationsLoaded) modelAnimator.gltfAnimator.DebugPrintAnimationInfo();

                // Cache-only mode: models[] is already GPU-ready; skip scene_models[] restore.
                if (bCacheOnly)
                {
                    bLoadedFromCache = true;
                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        L"[SceneManager] ParseGLBScene() CACHE HIT (cache-only mode) -- models[] GPU-ready, scene_models[] left empty.");
                    return true;
                }

                // --- Step 3: Restore scene_models from cache ---
                int instanceIndex = 0;
                for (int m = 0; m < MAX_MODELS; ++m)
                {
                    const ModelInfo& cache = models[m].m_modelInfo;
                    if (cache.sourceSceneFile != glbFile) continue;
                    if (!cache.bGpuReady)                 continue;
                    int idx = cache.cachedInstanceIndex;
                    if (idx < 0 || idx >= MAX_SCENE_MODELS) continue;

                    if (cache.bIsTransformOnly)
                    {
                        scene_models[idx].DestroyModel();
                        scene_models[idx].m_modelInfo.bIsTransformOnly  = true;
                        scene_models[idx].m_modelInfo.bIsTransformProxy = true;
                    }
                    else
                    {
                        scene_models[idx].CopyFrom(models[m]);

                        // Restore CPU geometry if CopyFrom left it empty
                        if (scene_models[idx].m_modelInfo.vertices.empty() &&
                            !models[m].m_modelInfo.vertices.empty())
                        {
                            scene_models[idx].m_modelInfo.vertices = models[m].m_modelInfo.vertices;
                            scene_models[idx].m_modelInfo.indices  = models[m].m_modelInfo.indices;
                        }

                        // --- GPU rebuild check: all renderers ---
                        // Vulkan: check actual handle validity - do NOT rely on bGpuReady.
                        // bGpuReady can be true on a models[] entry whose handles were freed
                        // by a prior CleanUp() (stale-after-free crash on next scene visit).
                        bool gpuRebuildNeeded = false;
                        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            if (!scene_models[idx].m_modelInfo.vertexBuffer  ||
                                !scene_models[idx].m_modelInfo.indexBuffer   ||
                                !scene_models[idx].m_modelInfo.constantBuffer)
                                gpuRebuildNeeded = true;
                        #elif defined(__USE_VULKAN__)
                            if (scene_models[idx].m_modelInfo.vertexBuffer  == VK_NULL_HANDLE ||
                                scene_models[idx].m_modelInfo.indexBuffer   == VK_NULL_HANDLE ||
                                scene_models[idx].m_modelInfo.uniformBuffer == VK_NULL_HANDLE)
                                gpuRebuildNeeded = true;
                        #elif defined(__USE_OPENGL__)
                            if (scene_models[idx].m_modelInfo.VAO == 0 ||
                                scene_models[idx].m_modelInfo.VBO == 0 ||
                                scene_models[idx].m_modelInfo.EBO == 0)
                                gpuRebuildNeeded = true;
                        #endif
                        if (gpuRebuildNeeded && !scene_models[idx].m_modelInfo.vertices.empty())
                        {
                            scene_models[idx].SetupModelForRendering(idx);
                            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                models[m].m_modelInfo.constantBuffer      = scene_models[idx].m_modelInfo.constantBuffer;
                                models[m].m_modelInfo.vertexBuffer        = scene_models[idx].m_modelInfo.vertexBuffer;
                                models[m].m_modelInfo.indexBuffer         = scene_models[idx].m_modelInfo.indexBuffer;
                                models[m].m_modelInfo.lightConstantBuffer = scene_models[idx].m_modelInfo.lightConstantBuffer;
                                models[m].m_modelInfo.materialBuffer      = scene_models[idx].m_modelInfo.materialBuffer;
                                models[m].m_modelInfo.debugConstantBuffer = scene_models[idx].m_modelInfo.debugConstantBuffer;
                                models[m].m_modelInfo.samplerState        = scene_models[idx].m_modelInfo.samplerState;
                                models[m].m_modelInfo.textureSRVs         = scene_models[idx].m_modelInfo.textureSRVs;
                                models[m].m_modelInfo.normalMapSRVs       = scene_models[idx].m_modelInfo.normalMapSRVs;
                            #elif defined(__USE_VULKAN__)
                                models[m].m_modelInfo.vertexBuffer        = scene_models[idx].m_modelInfo.vertexBuffer;
                                models[m].m_modelInfo.vertexBufferMemory  = scene_models[idx].m_modelInfo.vertexBufferMemory;
                                models[m].m_modelInfo.indexBuffer         = scene_models[idx].m_modelInfo.indexBuffer;
                                models[m].m_modelInfo.indexBufferMemory   = scene_models[idx].m_modelInfo.indexBufferMemory;
                                models[m].m_modelInfo.uniformBuffer               = scene_models[idx].m_modelInfo.uniformBuffer;
                                models[m].m_modelInfo.uniformBufferMemory         = scene_models[idx].m_modelInfo.uniformBufferMemory;
                                models[m].m_modelInfo.uniformBufferMapped         = scene_models[idx].m_modelInfo.uniformBufferMapped;
                                models[m].m_modelInfo.materialUniformBuffer       = scene_models[idx].m_modelInfo.materialUniformBuffer;
                                models[m].m_modelInfo.materialUniformBufferMemory = scene_models[idx].m_modelInfo.materialUniformBufferMemory;
                                models[m].m_modelInfo.materialUniformBufferMapped = scene_models[idx].m_modelInfo.materialUniformBufferMapped;
                                models[m].m_modelInfo.pipeline                    = scene_models[idx].m_modelInfo.pipeline;
                                models[m].m_modelInfo.pipelineLayout              = scene_models[idx].m_modelInfo.pipelineLayout;
                                models[m].m_modelInfo.descriptorSet               = scene_models[idx].m_modelInfo.descriptorSet;
                                models[m].m_modelInfo.textureDescriptorSet        = scene_models[idx].m_modelInfo.textureDescriptorSet;
                            #elif defined(__USE_OPENGL__)
                                models[m].m_modelInfo.VAO           = scene_models[idx].m_modelInfo.VAO;
                                models[m].m_modelInfo.VBO           = scene_models[idx].m_modelInfo.VBO;
                                models[m].m_modelInfo.EBO           = scene_models[idx].m_modelInfo.EBO;
                                models[m].m_modelInfo.shaderProgram = scene_models[idx].m_modelInfo.shaderProgram;
                            #endif
                            models[m].m_modelInfo.bGpuReady = true;
                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"[SceneManager] CACHE-RESTORE '%ls' - GPU rebuild triggered (missing core buffers)",
                                    cache.name.c_str());
                            #endif
                        }

                        // Restore CPU material strings
                        if (scene_models[idx].m_materials.empty() && !models[m].m_materials.empty())
                            scene_models[idx].m_materials = models[m].m_materials;
                        if (scene_models[idx].m_modelInfo.materials.empty() &&
                            !models[m].m_modelInfo.materials.empty())
                            scene_models[idx].m_modelInfo.materials = models[m].m_modelInfo.materials;
                    }

                    // Common fields - set for both geometry and transform-only nodes
                    scene_models[idx].m_modelInfo.ID                    = idx;
                    scene_models[idx].m_modelInfo.name                  = cache.name;
                    scene_models[idx].m_modelInfo.worldMatrix            = cache.worldMatrix;
                    scene_models[idx].m_modelInfo.iParentModelID         = cache.iParentModelID;
                    scene_models[idx].m_modelInfo.gltfNodeIndex          = cache.gltfNodeIndex;
                    // Always reset animLocal to base (rest pose) on cache restore - ensures
                    // clean animation state regardless of what was cached after the last session.
                    scene_models[idx].m_modelInfo.baseLocalTranslation   = cache.baseLocalTranslation;
                    scene_models[idx].m_modelInfo.baseLocalRotationQuat  = cache.baseLocalRotationQuat;
                    scene_models[idx].m_modelInfo.baseLocalScale         = cache.baseLocalScale;
                    scene_models[idx].m_modelInfo.animLocalTranslation   = cache.baseLocalTranslation;
                    scene_models[idx].m_modelInfo.animLocalRotationQuat  = cache.baseLocalRotationQuat;
                    scene_models[idx].m_modelInfo.animLocalScale         = cache.baseLocalScale;
                    scene_models[idx].m_modelInfo.bHasBaseLocalTRS       = cache.bHasBaseLocalTRS;
                    scene_models[idx].m_modelInfo.bIsTransformOnly       = cache.bIsTransformOnly;
                    scene_models[idx].m_modelInfo.bIsTransformProxy      = cache.bIsTransformProxy;
                    scene_models[idx].m_modelInfo.position               = cache.position;
                    scene_models[idx].ApplyDefaultLightingFromManager(lightsManager);
                    scene_models[idx].m_isLoaded = true;

                    #if defined(_DEBUG_SCENEMANAGER_)
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        if (!cache.bIsTransformOnly && !scene_models[idx].m_modelInfo.vertices.empty())
                        {
                            XMFLOAT4X4 cMat; XMStoreFloat4x4(&cMat, cache.worldMatrix);
                            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                                L"[SceneManager] CACHE-RESTORE '%ls' vert[0].z=%.4f world_tz=%.4f",
                                cache.name.c_str(),
                                scene_models[idx].m_modelInfo.vertices[0].position.z,
                                cMat._43);
                        }
                    #endif
                    #endif

                    instanceIndex = std::max(instanceIndex, idx + 1);
                }

                // --- Step 4: Rebind textures and materials - ALL renderers ---
                // Always clear stale GPU handles and rebind from the fresh GLB document.
                // This fixes scene-switch SRV staleness, wrong-scene descriptor sets,
                // and unbound PBR material data on every renderer.
                // Results are written back to models[] so the next reload gets them too.
                if (instanceIndex > 0 && miniDoc.contains("materials"))
                {
                    const auto& matsArr = miniDoc["materials"];
                    for (int ti = 0; ti < instanceIndex; ++ti)
                    {
                        if (!scene_models[ti].m_isLoaded) continue;
                        if (scene_models[ti].m_modelInfo.bIsTransformProxy) continue;
                        if (scene_models[ti].m_modelInfo.bIsTransformOnly) continue;
                        if (scene_models[ti].m_modelInfo.materials.empty()) continue;

                        const std::string& matName = scene_models[ti].m_modelInfo.materials[0];
                        for (int mi = 0; mi < static_cast<int>(matsArr.size()); ++mi)
                        {
                            if (matsArr[mi].value("name", "") != matName) continue;

                            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                scene_models[ti].m_modelInfo.textures.clear();
                                scene_models[ti].m_modelInfo.textureSRVs.clear();
                                scene_models[ti].m_modelInfo.normalMapSRVs.clear();
                                scene_models[ti].m_modelInfo.metallicMapSRV.Reset();
                                scene_models[ti].m_modelInfo.roughnessMapSRV.Reset();
                                scene_models[ti].m_modelInfo.aoMapSRV.Reset();
                                scene_models[ti].m_modelInfo.emissiveMapSRV.Reset();
                            #elif defined(__USE_OPENGL__)
                                scene_models[ti].m_modelInfo.textureIDs.clear();
                                scene_models[ti].m_modelInfo.normalMapIDs.clear();
                                scene_models[ti].m_modelInfo.metallicTexID  = 0;
                                scene_models[ti].m_modelInfo.roughnessTexID = 0;
                                scene_models[ti].m_modelInfo.aoTexID        = 0;
                                scene_models[ti].m_modelInfo.emissiveTexID  = 0;
                            #elif defined(__USE_VULKAN__)
                                // Clear the textures vector so BindGLTFMaterialTexturesToModel
                                // starts fresh - without this, textures accumulate on every
                                // scene revisit (same bug as the DX11 path would have without
                                // textureSRVs.clear()).
                                // NOTE: Do NOT null descriptorSet here - BindGLTFMaterialTexturesToModel
                                // only rebuilds textureDescriptorSet (set=1); descriptorSet (set=0)
                                // holds the UBO bindings allocated in SetupModelForRendering and must
                                // remain valid, otherwise the draw guard in VULKAN_RenderFrame always
                                // fails and nothing renders.
                                scene_models[ti].m_modelInfo.textures.clear();
                            #endif
                            BindGLTFMaterialTexturesToModel(mi, scene_models[ti].m_modelInfo, scene_models[ti], miniDoc);

                            #if defined(__USE_DIRECTX_12__)
                                // DX12: the rebind above only refreshes the DX11-on-12 SRVs.
                                // SetupModelForRendering already ran in Step 3 (before this
                                // rebind), so the native DX12 side still holds the pre-rebind
                                // state - NULL resources on a cache.dat restore.  Re-upload
                                // the new Texture objects and force the descriptor heap slots
                                // to be rewritten on the next draw, otherwise the model
                                // renders untextured/invisible while all texture-load logs
                                // report success.
                                scene_models[ti].RefreshDX12Textures();
                            #elif defined(__USE_OPENGL__)
                                // OpenGL: same hole as DX12 - the rebind above only creates
                                // Texture objects and Material entries; the GL handles in
                                // ModelInfo (textureIDs / normalMapIDs / PBR IDs) were
                                // cleared before the rebind and stay empty unless rebuilt
                                // here, leaving the model untextured on every scene revisit.
                                scene_models[ti].RefreshOpenGLTextures();
                            #endif

                            for (int m2 = 0; m2 < MAX_MODELS; ++m2)
                            {
                                if (models[m2].m_modelInfo.cachedInstanceIndex == ti &&
                                    models[m2].m_modelInfo.sourceSceneFile    == glbFile)
                                {
                                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                        models[m2].m_modelInfo.textures           = scene_models[ti].m_modelInfo.textures;
                                        models[m2].m_modelInfo.textureSRVs        = scene_models[ti].m_modelInfo.textureSRVs;
                                        models[m2].m_modelInfo.normalMapSRVs      = scene_models[ti].m_modelInfo.normalMapSRVs;
                                        models[m2].m_modelInfo.metallicMapSRV     = scene_models[ti].m_modelInfo.metallicMapSRV;
                                        models[m2].m_modelInfo.roughnessMapSRV    = scene_models[ti].m_modelInfo.roughnessMapSRV;
                                        models[m2].m_modelInfo.aoMapSRV           = scene_models[ti].m_modelInfo.aoMapSRV;
                                        models[m2].m_modelInfo.emissiveMapSRV     = scene_models[ti].m_modelInfo.emissiveMapSRV;
                                        models[m2].m_modelInfo.emissiveMapTexture = scene_models[ti].m_modelInfo.emissiveMapTexture;
                                        models[m2].m_modelInfo.useEmissiveMap     = scene_models[ti].m_modelInfo.useEmissiveMap;
                                        models[m2].m_materials                    = scene_models[ti].m_materials;
                                    #elif defined(__USE_OPENGL__)
                                        models[m2].m_modelInfo.textureIDs      = scene_models[ti].m_modelInfo.textureIDs;
                                        models[m2].m_modelInfo.normalMapIDs    = scene_models[ti].m_modelInfo.normalMapIDs;
                                        models[m2].m_modelInfo.metallicTexID   = scene_models[ti].m_modelInfo.metallicTexID;
                                        models[m2].m_modelInfo.roughnessTexID  = scene_models[ti].m_modelInfo.roughnessTexID;
                                        models[m2].m_modelInfo.aoTexID         = scene_models[ti].m_modelInfo.aoTexID;
                                        models[m2].m_modelInfo.glossTexID      = scene_models[ti].m_modelInfo.glossTexID;
                                        models[m2].m_modelInfo.emissiveTexID   = scene_models[ti].m_modelInfo.emissiveTexID;
                                        models[m2].m_modelInfo.useEmissiveMap  = scene_models[ti].m_modelInfo.useEmissiveMap;
                                        models[m2].m_materials                 = scene_models[ti].m_materials;
                                    #elif defined(__USE_VULKAN__)
                                        // Write textures back to the models[] cache so subsequent
                                        // scene visits restore the correct Vulkan texture handles.
                                        models[m2].m_modelInfo.textures               = scene_models[ti].m_modelInfo.textures;
                                        models[m2].m_modelInfo.descriptorSet          = scene_models[ti].m_modelInfo.descriptorSet;
                                        models[m2].m_modelInfo.textureDescriptorSet   = scene_models[ti].m_modelInfo.textureDescriptorSet;
                                        models[m2].m_modelInfo.materialUniformBuffer       = scene_models[ti].m_modelInfo.materialUniformBuffer;
                                        models[m2].m_modelInfo.materialUniformBufferMemory = scene_models[ti].m_modelInfo.materialUniformBufferMemory;
                                        models[m2].m_modelInfo.materialUniformBufferMapped = scene_models[ti].m_modelInfo.materialUniformBufferMapped;
                                        models[m2].m_materials                 = scene_models[ti].m_materials;
                                    #endif
                                    #if defined(_DEBUG_SCENEMANAGER_)
                                        debug.logDebugMessage(LogLevel::LOG_INFO,
                                            L"[SceneManager] TEXTURE-RELOAD '%ls' - written back to models[%d]",
                                            scene_models[ti].m_modelInfo.name.c_str(), m2);
                                    #endif
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }

                // --- Step 5: Start animations ---
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    std::wstring(L"[SceneManager] CACHE-RESTORE GLB Step 5: bAnimationsLoaded=") +
                    (bAnimationsLoaded ? L"true" : L"false") + L" animCount=" +
                    std::to_wstring(modelAnimator.gltfAnimator.GetAnimationCount()) +
                    L" instances=" + std::to_wstring(instanceIndex));
                if (bAnimationsLoaded && modelAnimator.gltfAnimator.GetAnimationCount() > 0)
                {
                    for (int animIdx = 0; animIdx < modelAnimator.gltfAnimator.GetAnimationCount(); ++animIdx)
                    {
                        int parentID = FindParentModelIDForAnimation(animIdx);
                        debug.logLevelMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] CACHE-RESTORE GLB anim[" + std::to_wstring(animIdx) +
                            L"] parentID=" + std::to_wstring(parentID));
                        if (parentID < 0) continue;
                        bool created = modelAnimator.gltfAnimator.CreateAnimationInstance(animIdx, parentID);
                        debug.logLevelMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] CACHE-RESTORE GLB anim[" + std::to_wstring(animIdx) +
                            L"] instance " + (created ? L"CREATED" : L"FAILED") +
                            L" parentID=" + std::to_wstring(parentID));
                        if (created)
                        {
                            modelAnimator.gltfAnimator.ForceAnimationReset(parentID);
                            modelAnimator.gltfAnimator.SetAnimationSpeed(parentID, 0.75f);
                            modelAnimator.gltfAnimator.SetAnimationLooping(parentID, true);
                            modelAnimator.gltfAnimator.StartAnimation(parentID, animIdx);
                        }
                    }
                }

                if (instanceIndex > 0)
                {
                    #if defined(_DEBUG_SCENEMANAGER_)
                    {
                        auto _e  = std::chrono::high_resolution_clock::now();
                        auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(_e - _sceneLoadBegin).count();
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] ParseGLBScene() CACHE HIT - ENGINE LOAD TIME: %lld ms - %d instances restored from %d cache entries",
                            _ms, instanceIndex, cacheCount);
                    }
                    #endif
                    bLoadedFromCache = true;
                    return true;
                }

                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING,
                        L"[SceneManager] ParseGLBScene() cache had %d entries but rebuild yielded 0 - falling through to full parse",
                        cacheCount);
                #endif
            }
        }
    }

    // Check if the GLB file exists on the filesystem
    if (!std::filesystem::exists(glbFile)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] GLB file not found: %ls", glbFile.c_str());
        #endif
        return false;
    }

    // Show progress before the potentially slow file I/O begins.
    showStage(L"Reading GLB file...");

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
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] GLB header length mismatch (header=%d actual=%d) - Blender export bug, using actual size.",
                header.length, static_cast<uint32_t>(actualFileSize));
        #endif
    }

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

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLB Exporter Detected: %ls", m_lastDetectedExporter.c_str());
    #endif

    // Build Blender import configuration (coordinate flip, winding, version caps).
    {
        std::string generator;
        if (doc.contains("asset") && doc["asset"].contains("generator") &&
            doc["asset"]["generator"].is_string())
            generator = doc["asset"]["generator"].get<std::string>();
        m_blenderConfig = BlenderImports::BuildConfig(generator, doc);
    }

    // Parse camera, lights, and materials using existing GLTF parsing functions
    // NOTE: The embedded binary data in gltfBinaryData is now available for these functions
    showStage(L"Parsing scene data...");
    ParseGLTFCamera(doc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
    ParseGLTFLights(doc);
    EnsureDefaultSunLight();
    ParseMaterialsFromGLTF(doc);

    // Parse animations from GLB document and store them in the global animator
    bAnimationsLoaded = modelAnimator.gltfAnimator.ParseAnimationsFromGLTF(doc, gltfBinaryData);
    if (bAnimationsLoaded)
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Successfully loaded %d animations from GLB", modelAnimator.gltfAnimator.GetAnimationCount());
        #endif
        modelAnimator.gltfAnimator.DebugPrintAnimationInfo();
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
    showStage(L"Building scene geometry...");
    int instanceIndex = 0;

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Starting GLB node processing with %d root nodes.", static_cast<int>(rootNodeIndices.size()));
    #endif

    // Tag which file is being parsed so NodeRecursive write-backs can store the source path.
    m_currentSceneFile = glbFile;

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
                ParseGLBNodeRecursive(rootNode, rootNodeIndex, identity, doc, nodes, instanceIndex, -1);     // Pass node index for animation node mapping.
            }
        }
    }

    // Auto-initialise all animations discovered during loading.
    // Each animation's channel node indices are matched against loaded scene models to
    // resolve the root parent, removing the need for hardcoded per-scene animation startup.
    if (bAnimationsLoaded && modelAnimator.gltfAnimator.GetAnimationCount() > 0)
    {
        for (int animIdx = 0; animIdx < modelAnimator.gltfAnimator.GetAnimationCount(); ++animIdx)
        {
            int parentID = FindParentModelIDForAnimation(animIdx);
            if (parentID < 0)
                continue;

            bool created = modelAnimator.gltfAnimator.CreateAnimationInstance(animIdx, parentID);
            if (created)
            {
                modelAnimator.gltfAnimator.ForceAnimationReset(parentID);
                modelAnimator.gltfAnimator.SetAnimationSpeed(parentID, 0.75f);
                modelAnimator.gltfAnimator.SetAnimationLooping(parentID, true);
                modelAnimator.gltfAnimator.StartAnimation(parentID, animIdx);
            }
        }
    }

    #if defined(_DEBUG_SCENEMANAGER_)
    {
        auto _sceneLoadEnd = std::chrono::high_resolution_clock::now();
        auto _sceneLoadMs  = std::chrono::duration_cast<std::chrono::milliseconds>(_sceneLoadEnd - _sceneLoadBegin).count();
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] ParseGLBScene() LOAD END - ENGINE LOAD TIME: %lld ms - Instances: %d - Exporter: %ls",
            _sceneLoadMs, instanceIndex, m_lastDetectedExporter.c_str());
    }
    #endif

    // Cache-only mode: GPU resources are now in models[] only.
    // Clear all populated scene_models[] entries so the renderer skips them.
    // (Dynamic scenes assemble their visible set at runtime via PutModelToScene.)
    if (bCacheOnly && instanceIndex > 0)
    {
        for (int i = 0; i < instanceIndex; ++i)
        {
            scene_models[i].m_modelInfo  = ModelInfo{};
            scene_models[i].m_materials.clear();
            scene_models[i].m_isLoaded   = false;
            scene_models[i].bInitialized = false;
        }
        debug.logLevelMessage(LogLevel::LOG_INFO,
            (L"[SceneManager] ParseGLBScene() cache-only mode -- " +
             std::to_wstring(instanceIndex) + L" model(s) cached in models[], scene_models[] cleared.").c_str());
    }

    // Return success if at least one model instance was created
    return (instanceIndex > 0);
}


// --------------------------------------------------------------------------------------------------
// SceneManager::ParseGLBNodeRecursive()
// Recursively processes GLTF nodes from GLB files with parent-child relationship tracking.
// Sets iParentModelID = -1 for parent models, assigns parent's instanceIndex to children.
// This function handles the hierarchical structure and ensures proper parent-child linkage.
// --------------------------------------------------------------------------------------------------
void SceneManager::ParseGLBNodeRecursive(const json& node, int nodeIndex, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex, int parentModelID)
{
    // Prevent buffer overflow by checking maximum scene model limit
    if (instanceIndex >= MAX_SCENE_MODELS)
        return;

    // Check if this node contains a mesh to be rendered
    bool hasMesh = node.contains("mesh") && node["mesh"].is_number_integer();
    
    // Store the current instance index as potential parent ID for children
    int currentParentID = parentModelID;

    // Load and decompose the node's local transformation matrix
    XMMATRIX nodeTransform = GetNodeWorldMatrix(node, m_blenderConfig);

    // Decompose transformation matrix to extract scale for potential geometry baking
    XMVECTOR outScale, outRot, outTrans;
    XMMatrixDecompose(&outScale, &outRot, &outTrans, nodeTransform);
    XMFLOAT3 scale;
    XMStoreFloat3(&scale, outScale);

    // Capture base local TRS from this GLB node for correct animation evaluation.
    // Blender 4.4+ exports GLTF animations as LOCAL TRS values.
    XMFLOAT3 baseLocalTranslation;
    XMFLOAT4 baseLocalRotationQuat;
    XMFLOAT3 baseLocalScale;
    XMStoreFloat3(&baseLocalTranslation, outTrans);
    XMStoreFloat4(&baseLocalRotationQuat, outRot);
    XMStoreFloat3(&baseLocalScale, outScale);
    
    // Check if the node has non-identity scale that needs to be baked into geometry
    bool hasNonIdentityScale = (fabs(scale.x - 1.0f) > 0.0001f || fabs(scale.y - 1.0f) > 0.0001f || fabs(scale.z - 1.0f) > 0.0001f);

    // Process mesh if present in this node.
    // Each GLB mesh primitive is created as its own scene_models entry so that
    // every sub-mesh gets its own material, textures and draw call.
    if (hasMesh)
    {
        // Extract mesh index from the node definition
        int meshIndex = node["mesh"];

        // Validate that meshes array exists in the GLTF document
        if (!doc.contains("meshes") || !doc["meshes"].is_array()) return;

        const auto& meshes = doc["meshes"];

        // Validate mesh index is within valid range
        if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;

        // Determine base name for this node
        std::wstring modelName;
        if (node.contains("name") && node["name"].is_string())
        {
            std::string nodeName = node["name"];
            modelName = sysUtils.ToWString(nodeName);
        }
        else
        {
            modelName = L"GLBNode_" + std::to_wstring(instanceIndex) + L"_Mesh_" + std::to_wstring(meshIndex);
        }

        // Determine number of primitives so we can loop over them
        const auto& primsMesh = meshes[meshIndex];
        int numPrimitives = 1;
        if (primsMesh.contains("primitives") && primsMesh["primitives"].is_array())
            numPrimitives = static_cast<int>(primsMesh["primitives"].size());

        // Pre-compute world transform once for all primitives of this node.
        // Scale is baked per-primitive into geometry; adjust the matrix accordingly.
        XMMATRIX effectiveNodeTransform = nodeTransform;
        if (hasNonIdentityScale)
            effectiveNodeTransform *= XMMatrixScaling(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        XMMATRIX worldTransform = parentTransform * effectiveNodeTransform;

        XMFLOAT4X4 f4x4;
        XMStoreFloat4x4(&f4x4, worldTransform);

        // Track the first primitive's instanceIndex so child nodes can parent to it.
        int firstPrimInstanceIndex = instanceIndex;

        for (int primIdx = 0; primIdx < numPrimitives && instanceIndex < MAX_SCENE_MODELS; ++primIdx)
        {
            // Primitive 0 keeps the base node name for backward-compatible animation lookups.
            // Subsequent primitives get a "_pN" suffix so they have unique cache keys.
            std::wstring primName = (primIdx == 0)
                ? modelName
                : (modelName + L"_p" + std::to_wstring(primIdx));

            // --- Find or create per-primitive entry in the global model cache (single pass) ---
            int modelSlot = -1;
            int firstFree = -1;
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name == primName &&
                    (models[m].m_modelInfo.sourceSceneFile.empty() ||
                     models[m].m_modelInfo.sourceSceneFile == m_currentSceneFile))
                { modelSlot = m; break; }
                if (firstFree < 0 && models[m].m_modelInfo.name.empty()) firstFree = m;
            }

            if (modelSlot < 0 && firstFree >= 0)
            {
                modelSlot = firstFree;
                models[modelSlot].m_modelInfo.name = primName;
                models[modelSlot].m_modelInfo.ID   = modelSlot;
                models[modelSlot].m_modelInfo.vertices.clear();
                models[modelSlot].m_modelInfo.indices.clear();
                models[modelSlot].m_modelInfo.textures.clear();
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                models[modelSlot].m_modelInfo.textureSRVs.clear();
                models[modelSlot].m_modelInfo.normalMapSRVs.clear();
#endif
                LoadGLTFMeshPrimitives(meshIndex, doc, models[modelSlot], primIdx);

                if (hasNonIdentityScale)
                {
                    for (auto& v : models[modelSlot].m_modelInfo.vertices)
                    {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        v.position.x *= scale.x;
                        v.position.y *= scale.y;
                        v.position.z *= scale.z;
#else
                        v.position[0] *= scale.x;
                        v.position[1] *= scale.y;
                        v.position[2] *= scale.z;
#endif
                    }
                }
            }

            if (modelSlot < 0)
            {
                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] No free model cache slot for primitive %d of '%ls' - skipping", primIdx, modelName.c_str());
                #endif
                continue;
            }

            // Skip empty primitives (can happen when a primitive has no valid geometry)
            if (models[modelSlot].m_modelInfo.vertices.empty() || models[modelSlot].m_modelInfo.indices.empty())
            {
                models[modelSlot].m_modelInfo.name.clear();
                continue;
            }

            // cache.dat restore hole: LoadCache() restores geometry + names into models[]
            // but Texture objects and Material structs cannot be serialised to disk.  On
            // the first full parse after startup the slot is FOUND by name above, so the
            // creation branch (which binds materials/textures inside
            // LoadGLTFMeshPrimitives) never runs - the model would render with no
            // materials and no textures until a later cache-restore rebind.  Bind the
            // primitive's material now, exactly as the creation branch would have.
            if (models[modelSlot].m_materials.empty() &&
                primsMesh.contains("primitives") && primsMesh["primitives"].is_array() &&
                primIdx < (int)primsMesh["primitives"].size())
            {
                int rebindMatIdx = primsMesh["primitives"][primIdx].value("material", -1);
                if (rebindMatIdx >= 0)
                {
                    // Clear the cache.dat-restored material-name list first so the bind
                    // below does not append a duplicate name entry.
                    models[modelSlot].m_modelInfo.materials.clear();
                    BindGLTFMaterialTexturesToModel(rebindMatIdx,
                        models[modelSlot].m_modelInfo, models[modelSlot], doc);
                }
            }

            // --- Populate scene_models[instanceIndex] ---
            scene_models[instanceIndex].CopyFrom(models[modelSlot]);
            scene_models[instanceIndex].m_modelInfo.worldMatrix = worldTransform;

            if (primIdx == 0)
            {
                scene_models[instanceIndex].m_modelInfo.iParentModelID        = parentModelID;
                scene_models[instanceIndex].m_modelInfo.gltfNodeIndex         = nodeIndex;
                scene_models[instanceIndex].m_modelInfo.importType            = ImportType::GLTF;
                scene_models[instanceIndex].m_modelInfo.baseLocalTranslation  = baseLocalTranslation;
                scene_models[instanceIndex].m_modelInfo.baseLocalRotationQuat = baseLocalRotationQuat;
                scene_models[instanceIndex].m_modelInfo.baseLocalScale        = baseLocalScale;
                scene_models[instanceIndex].m_modelInfo.animLocalTranslation  = baseLocalTranslation;
                scene_models[instanceIndex].m_modelInfo.animLocalRotationQuat = baseLocalRotationQuat;
                scene_models[instanceIndex].m_modelInfo.animLocalScale        = baseLocalScale;
            }
            else
            {
                static const XMFLOAT3 identT = { 0.0f, 0.0f, 0.0f };
                static const XMFLOAT4 identR = { 0.0f, 0.0f, 0.0f, 1.0f };
                static const XMFLOAT3 identS = { 1.0f, 1.0f, 1.0f };
                scene_models[instanceIndex].m_modelInfo.iParentModelID        = firstPrimInstanceIndex;
                scene_models[instanceIndex].m_modelInfo.gltfNodeIndex         = -1;
                scene_models[instanceIndex].m_modelInfo.baseLocalTranslation  = identT;
                scene_models[instanceIndex].m_modelInfo.baseLocalRotationQuat = identR;
                scene_models[instanceIndex].m_modelInfo.baseLocalScale        = identS;
                scene_models[instanceIndex].m_modelInfo.animLocalTranslation  = identT;
                scene_models[instanceIndex].m_modelInfo.animLocalRotationQuat = identR;
                scene_models[instanceIndex].m_modelInfo.animLocalScale        = identS;
            }
            scene_models[instanceIndex].m_modelInfo.bHasBaseLocalTRS = true;

            scene_models[instanceIndex].m_modelInfo.position = XMFLOAT3(f4x4._41, f4x4._42, f4x4._43);
            XMStoreFloat3(&scene_models[instanceIndex].m_modelInfo.scale, XMVectorSet(1.0f, 1.0f, 1.0f, 0));

            scene_models[instanceIndex].m_modelInfo.ID   = instanceIndex;
            scene_models[instanceIndex].m_modelInfo.name = primName;

            scene_models[instanceIndex].SetupModelForRendering(instanceIndex);
#if defined(__USE_VULKAN__)
            // SetupModelForRendering created the material UBO and texture descriptor set with defaults.
            // Upload the real material data now that those GPU buffers exist.
            // Prefer textures already resident in m_materials (loaded by LoadGLTFMeshPrimitives /
            // BindGLTFMaterialTexturesToModel via CopyFrom) to avoid a second disk/memory decode.
            // Fall back to a fresh BindGLTFMaterialTexturesToModel only if m_materials is empty.
            if (!scene_models[instanceIndex].m_materials.empty())
            {
                // Use whichever material was stored during the first bind pass; for single-material
                // GLTF meshes this is always the first (and only) entry in the map.
                for (auto& [matName, mat] : scene_models[instanceIndex].m_materials)
                    UploadFBXMaterialToVulkanModel(mat, scene_models[instanceIndex].m_modelInfo);
            }
            else if (doc.contains("meshes") && meshIndex < (int)doc["meshes"].size())
            {
                // Fallback: first bind pass produced no materials; try a fresh load from the GLTF doc.
                const auto& glbPrims = doc["meshes"][meshIndex]["primitives"];
                if (glbPrims.is_array() && primIdx < (int)glbPrims.size())
                {
                    int glbMatIdx = glbPrims[primIdx].value("material", -1);
                    if (glbMatIdx >= 0)
                        BindGLTFMaterialTexturesToModel(glbMatIdx,
                            scene_models[instanceIndex].m_modelInfo,
                            scene_models[instanceIndex], doc);
                }
            }
#endif
            scene_models[instanceIndex].ApplyDefaultLightingFromManager(lightsManager);
            scene_models[instanceIndex].m_isLoaded = true;

            // --- Write-back: full GPU-ready copy into models[] for fast-path reloads ---
            // CopyFrom captures vertex/index buffers, SRVs, shaders, and textures via ComPtr/shared_ptr
            // AddRef - the GPU resources stay alive in both models[] and scene_models[].
            models[modelSlot].CopyFrom(scene_models[instanceIndex]);
            models[modelSlot].m_isLoaded                         = true;
            models[modelSlot].m_modelInfo.ID                     = modelSlot;   // restore slot ID (CopyFrom brings in instanceIndex)
            models[modelSlot].m_modelInfo.sourceSceneFile        = m_currentSceneFile;
            models[modelSlot].m_modelInfo.cachedInstanceIndex    = instanceIndex;
            models[modelSlot].m_modelInfo.bGpuReady              = true;

            #if defined(_DEBUG_SCENEMANAGER_)
                if (GetLastDetectedExporter() == L"Sketchfab")
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SceneManager] Sketchfab GLB scene loaded. Patch applied.");

                debug.logDebugMessage(LogLevel::LOG_INFO,
                    L"[SceneManager] scene_models[%d] prim[%d/%d] \"%ls\" | ParentID:%d | Pos(%.2f,%.2f,%.2f)",
                    instanceIndex, primIdx, numPrimitives - 1, primName.c_str(),
                    scene_models[instanceIndex].m_modelInfo.iParentModelID, f4x4._41, f4x4._42, f4x4._43);

                if (!models[modelSlot].m_modelInfo.vertices.empty())
                {
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    float vert0z = models[modelSlot].m_modelInfo.vertices[0].position.z;
                    #else
                    float vert0z = models[modelSlot].m_modelInfo.vertices[0].position[2];
                    #endif
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"[SceneManager] CACHE-WRITE '%ls' vert[0].z=%.4f world_tz=%.4f",
                        primName.c_str(), vert0z, f4x4._43);
                }
            #endif

            ++instanceIndex;
        }

        // Children of this node parent to the first primitive's slot
        currentParentID = firstPrimInstanceIndex;
    }

    // =====================================================================
    // Transform-Only Node Support (Blender empties / grouping nodes)
    // If this node has NO mesh but HAS children, we must still create an
    // instance so that animations and parent transforms propagate correctly.
    // =====================================================================
    if (!hasMesh && node.contains("children") && node["children"].is_array() && !node["children"].empty())
    {
        // Prevent buffer overflow by checking maximum scene model limit
        if (instanceIndex >= MAX_SCENE_MODELS)
            return;

        // Compute the final world transformation matrix by combining parent and node transforms
        XMMATRIX worldTransform = parentTransform * nodeTransform;

        // Create a transform-only instance (no mesh, no GPU buffers)
        scene_models[instanceIndex].DestroyModel();                                              // Ensure a clean instance state.
        scene_models[instanceIndex].m_isLoaded = true;                                           // Must be "loaded" so animator can update it.
        scene_models[instanceIndex].m_modelInfo.bIsTransformOnly = true;                          // Mark as transform-only (do not render).
        scene_models[instanceIndex].m_modelInfo.bIsTransformProxy = true;                         // Keep alias flag in sync.
        scene_models[instanceIndex].m_modelInfo.ID = instanceIndex;
        scene_models[instanceIndex].m_modelInfo.iParentModelID = parentModelID;
        scene_models[instanceIndex].m_modelInfo.gltfNodeIndex = nodeIndex;
        scene_models[instanceIndex].m_modelInfo.importType    = ImportType::GLTF;
        scene_models[instanceIndex].m_modelInfo.worldMatrix = worldTransform;

        // Store base local TRS so animations can evaluate in LOCAL space.
        scene_models[instanceIndex].m_modelInfo.baseLocalTranslation = baseLocalTranslation;
        scene_models[instanceIndex].m_modelInfo.baseLocalRotationQuat = baseLocalRotationQuat;
        scene_models[instanceIndex].m_modelInfo.baseLocalScale = baseLocalScale;
        scene_models[instanceIndex].m_modelInfo.animLocalTranslation = baseLocalTranslation;
        scene_models[instanceIndex].m_modelInfo.animLocalRotationQuat = baseLocalRotationQuat;
        scene_models[instanceIndex].m_modelInfo.animLocalScale = baseLocalScale;
        scene_models[instanceIndex].m_modelInfo.bHasBaseLocalTRS = true;

        // Assign a descriptive name for debugging
        if (node.contains("name") && node["name"].is_string())
        {
            std::string nodeName = node["name"];
            scene_models[instanceIndex].m_modelInfo.name = sysUtils.ToWString(nodeName);
        }
        else
        {
            scene_models[instanceIndex].m_modelInfo.name = L"TransformNode_" + std::to_wstring(nodeIndex);
        }

        // --- Write-back: cache transform-only node data in models[] for fast-path reloads ---
        {
            int freeSlot = -1;
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name == scene_models[instanceIndex].m_modelInfo.name) { freeSlot = m; break; }
                if (freeSlot < 0 && models[m].m_modelInfo.name.empty()) freeSlot = m;
            }
            if (freeSlot >= 0)
            {
                ModelInfo& cm = models[freeSlot].m_modelInfo;
                cm.name               = scene_models[instanceIndex].m_modelInfo.name;
                cm.worldMatrix        = scene_models[instanceIndex].m_modelInfo.worldMatrix;
                cm.iParentModelID     = parentModelID;
                cm.gltfNodeIndex      = nodeIndex;
                cm.baseLocalTranslation   = baseLocalTranslation;
                cm.baseLocalRotationQuat  = baseLocalRotationQuat;
                cm.baseLocalScale         = baseLocalScale;
                cm.animLocalTranslation   = baseLocalTranslation;
                cm.animLocalRotationQuat  = baseLocalRotationQuat;
                cm.animLocalScale         = baseLocalScale;
                cm.bHasBaseLocalTRS   = true;
                cm.bIsTransformOnly   = true;
                cm.bIsTransformProxy  = true;
                {
                    XMFLOAT4X4 tXf; XMStoreFloat4x4(&tXf, worldTransform);
                    cm.position = XMFLOAT3(tXf._41, tXf._42, tXf._43);
                }
                cm.sourceSceneFile    = m_currentSceneFile;
                cm.cachedInstanceIndex = instanceIndex;
                cm.bGpuReady          = true;
            }
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] Created transform-only node instance %d for GLB node %d (ParentID: %d)",
                instanceIndex, nodeIndex, parentModelID);
        #endif

        // This transform-only node becomes the parent for its children.
        currentParentID = instanceIndex;
        ++instanceIndex;
    }

    // Process child nodes recursively, passing current node as parent
    if (node.contains("children") && node["children"].is_array())
    {
        for (const auto& childIndex : node["children"])
        {
            if (!childIndex.is_number_integer()) continue;
            int ci = childIndex.get<int>();
            if (ci < 0 || ci >= (int)allNodes.size()) continue;
            ParseGLBNodeRecursive(allNodes[ci], ci, parentTransform * nodeTransform, doc, allNodes, instanceIndex, currentParentID);
        }
    }
}

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------
bool SceneManager::ParseGLTFScene(const std::wstring& gltfFile, bool bCacheOnly)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        const auto _sceneLoadBegin = std::chrono::high_resolution_clock::now();
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] ParseGLTFScene() LOAD BEGIN - %ls", gltfFile.c_str());
    #endif

    bLoadedFromCache = false;
    m_fbxCameras.clear();   // clear FBX camera list on every new scene load
    lightsManager.ClearLights(); // clear lights from any prior scene before parsing new ones

    // Loading-text progress helper -- same pattern as showStage in IOLoaderThread.
    auto showStage = [](const wchar_t* msg) {
        TextRenderStyle s;
        s.fontName = LoadingTextFX::kFontName;
        s.fontSize = 20.0f;
        s.centered = true;
        fxManager.ShowLoadingText(msg,
            XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
            0.2f, 0.05f,
            XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f),
            0.0f, -1.0f, &s);
    };

    // =========================================================================
    // GEOMETRY PRE-CACHE FAST PATH
    // Same structure as the GLB path: parse the GLTF document FIRST to restore
    // per-load session state (exporter, blender axis config, camera, lights,
    // materials, animations) BEFORE restoring scene_models[], then rebuild GPU
    // resources and rebind materials/textures for every active renderer.
    // =========================================================================
    {
        int cacheCount = 0;
        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.sourceSceneFile == gltfFile &&
                models[m].m_modelInfo.bGpuReady &&
                models[m].m_modelInfo.cachedInstanceIndex >= 0)
                ++cacheCount;
        }

        if (cacheCount > 0)
        {
            // --- Step 1: Parse GLTF document BEFORE touching any scene_models[] ---
            bool miniParseOK = false;
            json miniDoc;

            if (std::filesystem::exists(gltfFile))
            {
                std::ifstream miniF(gltfFile);
                if (miniF.is_open())
                {
                    try { miniDoc = json::parse(miniF); miniParseOK = true; }
                    catch (const std::exception&)
                    {
                        debug.logLevelMessage(LogLevel::LOG_ERROR,
                            L"[SceneManager] CACHE-RESTORE: GLTF JSON parse failed - falling through to full parse");
                    }
                    miniF.close();
                }
            }

            if (!miniParseOK)
            {
                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING,
                        L"[SceneManager] ParseGLTFScene() mini-parse failed - falling through to full parse");
                #endif
            }
            else
            {
                // --- Step 2: Restore per-load session state (same order as full parse) ---
                {
                    std::string generator;
                    if (miniDoc.contains("asset") && miniDoc["asset"].contains("generator") &&
                        miniDoc["asset"]["generator"].is_string())
                        generator = miniDoc["asset"]["generator"].get<std::string>();
                    DetectGLTFExporter(miniDoc);
                    m_blenderConfig = BlenderImports::BuildConfig(generator, miniDoc);
                }
                if (myRenderer)
                    ParseGLTFCamera(miniDoc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
                ParseGLTFLights(miniDoc);
                EnsureDefaultSunLight();
                ParseMaterialsFromGLTF(miniDoc);
                modelAnimator.gltfAnimator.ClearAllAnimations();
                // Reload external .bin file before animation parsing - the cache path only
                // reads the JSON above; without this, gltfBinaryData is empty/stale and
                // ParseAnimationsFromGLTF cannot read keyframe data.
                if (miniDoc.contains("buffers") && miniDoc["buffers"].is_array() && !miniDoc["buffers"].empty())
                {
                    std::string binUri = miniDoc["buffers"][0].value("uri", "");
                    if (!binUri.empty())
                    {
                        std::filesystem::path binPath = std::filesystem::path(gltfFile).parent_path() / binUri;
                        std::ifstream binF(binPath, std::ios::binary);
                        if (binF.is_open())
                        {
                            binF.seekg(0, std::ios::end);
                            size_t binSz = static_cast<size_t>(binF.tellg());
                            binF.seekg(0);
                            gltfBinaryData.resize(binSz);
                            binF.read(reinterpret_cast<char*>(gltfBinaryData.data()), binSz);
                            binF.close();
                            debug.logLevelMessage(LogLevel::LOG_INFO,
                                L"[SceneManager] CACHE-RESTORE GLTF: reloaded .bin (" +
                                std::to_wstring(binSz) + L" bytes) for animation keyframes");
                        }
                        else
                        {
                            gltfBinaryData.clear();
                            debug.logLevelMessage(LogLevel::LOG_WARNING,
                                L"[SceneManager] CACHE-RESTORE GLTF: could not open .bin file - animation keyframes will be missing");
                        }
                    }
                }
                bAnimationsLoaded = modelAnimator.gltfAnimator.ParseAnimationsFromGLTF(miniDoc, gltfBinaryData);
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    std::wstring(L"[SceneManager] CACHE-RESTORE GLTF: animations parsed=") +
                    (bAnimationsLoaded ? L"true" : L"false") + L" count=" +
                    std::to_wstring(modelAnimator.gltfAnimator.GetAnimationCount()));
                if (bAnimationsLoaded) modelAnimator.gltfAnimator.DebugPrintAnimationInfo();

                // Cache-only mode: models[] is already GPU-ready; skip scene_models[] restore.
                if (bCacheOnly)
                {
                    bLoadedFromCache = true;
                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        L"[SceneManager] ParseGLTFScene() CACHE HIT (cache-only mode) -- models[] GPU-ready, scene_models[] left empty.");
                    return true;
                }

                // --- Step 3: Restore scene_models from cache ---
                int instanceIndex = 0;
                for (int m = 0; m < MAX_MODELS; ++m)
                {
                    const ModelInfo& cache = models[m].m_modelInfo;
                    if (cache.sourceSceneFile != gltfFile) continue;
                    if (!cache.bGpuReady)                  continue;
                    int idx = cache.cachedInstanceIndex;
                    if (idx < 0 || idx >= MAX_SCENE_MODELS) continue;

                    if (cache.bIsTransformOnly)
                    {
                        scene_models[idx].DestroyModel();
                        scene_models[idx].m_modelInfo.bIsTransformOnly  = true;
                        scene_models[idx].m_modelInfo.bIsTransformProxy = true;
                    }
                    else
                    {
                        scene_models[idx].CopyFrom(models[m]);

                        // Restore CPU geometry if CopyFrom left it empty
                        if (scene_models[idx].m_modelInfo.vertices.empty() &&
                            !models[m].m_modelInfo.vertices.empty())
                        {
                            scene_models[idx].m_modelInfo.vertices = models[m].m_modelInfo.vertices;
                            scene_models[idx].m_modelInfo.indices  = models[m].m_modelInfo.indices;
                        }

                        // --- GPU rebuild check: all renderers ---
                        // Vulkan: check actual handle validity - do NOT rely on bGpuReady.
                        // bGpuReady can be true on a models[] entry whose handles were freed
                        // by a prior CleanUp() (stale-after-free crash on next scene visit).
                        bool gpuRebuildNeeded = false;
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        if (!scene_models[idx].m_modelInfo.vertexBuffer  ||
                            !scene_models[idx].m_modelInfo.indexBuffer   ||
                            !scene_models[idx].m_modelInfo.constantBuffer)
                            gpuRebuildNeeded = true;
#elif defined(__USE_VULKAN__)
                        if (scene_models[idx].m_modelInfo.vertexBuffer  == VK_NULL_HANDLE ||
                            scene_models[idx].m_modelInfo.indexBuffer   == VK_NULL_HANDLE ||
                            scene_models[idx].m_modelInfo.uniformBuffer == VK_NULL_HANDLE)
                            gpuRebuildNeeded = true;
#elif defined(__USE_OPENGL__)
                        if (scene_models[idx].m_modelInfo.VAO == 0 ||
                            scene_models[idx].m_modelInfo.VBO == 0 ||
                            scene_models[idx].m_modelInfo.EBO == 0)
                            gpuRebuildNeeded = true;
#endif
                        if (gpuRebuildNeeded && !scene_models[idx].m_modelInfo.vertices.empty())
                        {
                            scene_models[idx].SetupModelForRendering(idx);
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            models[m].m_modelInfo.constantBuffer      = scene_models[idx].m_modelInfo.constantBuffer;
                            models[m].m_modelInfo.vertexBuffer        = scene_models[idx].m_modelInfo.vertexBuffer;
                            models[m].m_modelInfo.indexBuffer         = scene_models[idx].m_modelInfo.indexBuffer;
                            models[m].m_modelInfo.lightConstantBuffer = scene_models[idx].m_modelInfo.lightConstantBuffer;
                            models[m].m_modelInfo.materialBuffer      = scene_models[idx].m_modelInfo.materialBuffer;
                            models[m].m_modelInfo.debugConstantBuffer = scene_models[idx].m_modelInfo.debugConstantBuffer;
                            models[m].m_modelInfo.samplerState        = scene_models[idx].m_modelInfo.samplerState;
                            models[m].m_modelInfo.textureSRVs         = scene_models[idx].m_modelInfo.textureSRVs;
                            models[m].m_modelInfo.normalMapSRVs       = scene_models[idx].m_modelInfo.normalMapSRVs;
#elif defined(__USE_VULKAN__)
                            models[m].m_modelInfo.vertexBuffer        = scene_models[idx].m_modelInfo.vertexBuffer;
                            models[m].m_modelInfo.vertexBufferMemory  = scene_models[idx].m_modelInfo.vertexBufferMemory;
                            models[m].m_modelInfo.indexBuffer         = scene_models[idx].m_modelInfo.indexBuffer;
                            models[m].m_modelInfo.indexBufferMemory   = scene_models[idx].m_modelInfo.indexBufferMemory;
                            models[m].m_modelInfo.uniformBuffer               = scene_models[idx].m_modelInfo.uniformBuffer;
                            models[m].m_modelInfo.uniformBufferMemory         = scene_models[idx].m_modelInfo.uniformBufferMemory;
                            models[m].m_modelInfo.uniformBufferMapped         = scene_models[idx].m_modelInfo.uniformBufferMapped;
                            models[m].m_modelInfo.materialUniformBuffer       = scene_models[idx].m_modelInfo.materialUniformBuffer;
                            models[m].m_modelInfo.materialUniformBufferMemory = scene_models[idx].m_modelInfo.materialUniformBufferMemory;
                            models[m].m_modelInfo.materialUniformBufferMapped = scene_models[idx].m_modelInfo.materialUniformBufferMapped;
                            models[m].m_modelInfo.pipeline                    = scene_models[idx].m_modelInfo.pipeline;
                            models[m].m_modelInfo.pipelineLayout              = scene_models[idx].m_modelInfo.pipelineLayout;
                            models[m].m_modelInfo.descriptorSet               = scene_models[idx].m_modelInfo.descriptorSet;
                            models[m].m_modelInfo.textureDescriptorSet        = scene_models[idx].m_modelInfo.textureDescriptorSet;
#elif defined(__USE_OPENGL__)
                            models[m].m_modelInfo.VAO           = scene_models[idx].m_modelInfo.VAO;
                            models[m].m_modelInfo.VBO           = scene_models[idx].m_modelInfo.VBO;
                            models[m].m_modelInfo.EBO           = scene_models[idx].m_modelInfo.EBO;
                            models[m].m_modelInfo.shaderProgram = scene_models[idx].m_modelInfo.shaderProgram;
#endif
                            models[m].m_modelInfo.bGpuReady = true;
                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"[SceneManager] CACHE-RESTORE '%ls' - GPU rebuild triggered (missing core buffers)",
                                    cache.name.c_str());
                            #endif
                        }

                        // Restore CPU material strings
                        if (scene_models[idx].m_materials.empty() && !models[m].m_materials.empty())
                            scene_models[idx].m_materials = models[m].m_materials;
                        if (scene_models[idx].m_modelInfo.materials.empty() &&
                            !models[m].m_modelInfo.materials.empty())
                            scene_models[idx].m_modelInfo.materials = models[m].m_modelInfo.materials;
                    }

                    // Common fields - set for both geometry and transform-only nodes
                    scene_models[idx].m_modelInfo.ID                    = idx;
                    scene_models[idx].m_modelInfo.name                  = cache.name;
                    scene_models[idx].m_modelInfo.worldMatrix            = cache.worldMatrix;
                    scene_models[idx].m_modelInfo.iParentModelID         = cache.iParentModelID;
                    scene_models[idx].m_modelInfo.gltfNodeIndex          = cache.gltfNodeIndex;
                    // Always reset animLocal to base (rest pose) on cache restore - ensures
                    // clean animation state regardless of what was cached after the last session.
                    scene_models[idx].m_modelInfo.baseLocalTranslation   = cache.baseLocalTranslation;
                    scene_models[idx].m_modelInfo.baseLocalRotationQuat  = cache.baseLocalRotationQuat;
                    scene_models[idx].m_modelInfo.baseLocalScale         = cache.baseLocalScale;
                    scene_models[idx].m_modelInfo.animLocalTranslation   = cache.baseLocalTranslation;
                    scene_models[idx].m_modelInfo.animLocalRotationQuat  = cache.baseLocalRotationQuat;
                    scene_models[idx].m_modelInfo.animLocalScale         = cache.baseLocalScale;
                    scene_models[idx].m_modelInfo.bHasBaseLocalTRS       = cache.bHasBaseLocalTRS;
                    scene_models[idx].m_modelInfo.bIsTransformOnly       = cache.bIsTransformOnly;
                    scene_models[idx].m_modelInfo.bIsTransformProxy      = cache.bIsTransformProxy;
                    scene_models[idx].m_modelInfo.position               = cache.position;
                    scene_models[idx].ApplyDefaultLightingFromManager(lightsManager);
                    scene_models[idx].m_isLoaded = true;

                    #if defined(_DEBUG_SCENEMANAGER_)
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        if (!cache.bIsTransformOnly && !scene_models[idx].m_modelInfo.vertices.empty())
                        {
                            XMFLOAT4X4 cMat; XMStoreFloat4x4(&cMat, cache.worldMatrix);
                            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                                L"[SceneManager] CACHE-RESTORE '%ls' vert[0].z=%.4f world_tz=%.4f",
                                cache.name.c_str(),
                                scene_models[idx].m_modelInfo.vertices[0].position.z,
                                cMat._43);
                        }
                    #endif
                    #endif

                    instanceIndex = std::max(instanceIndex, idx + 1);
                }

                // --- Step 4: Rebind textures and materials - ALL renderers ---
                if (instanceIndex > 0 && miniDoc.contains("materials"))
                {
                    const auto& matsArr = miniDoc["materials"];
                    for (int ti = 0; ti < instanceIndex; ++ti)
                    {
                        if (!scene_models[ti].m_isLoaded) continue;
                        if (scene_models[ti].m_modelInfo.bIsTransformProxy) continue;
                        if (scene_models[ti].m_modelInfo.bIsTransformOnly) continue;
                        if (scene_models[ti].m_modelInfo.materials.empty()) continue;

                        const std::string& matName = scene_models[ti].m_modelInfo.materials[0];
                        for (int mi = 0; mi < static_cast<int>(matsArr.size()); ++mi)
                        {
                            if (matsArr[mi].value("name", "") != matName) continue;

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            scene_models[ti].m_modelInfo.textures.clear();
                            scene_models[ti].m_modelInfo.textureSRVs.clear();
                            scene_models[ti].m_modelInfo.normalMapSRVs.clear();
                            scene_models[ti].m_modelInfo.metallicMapSRV.Reset();
                            scene_models[ti].m_modelInfo.roughnessMapSRV.Reset();
                            scene_models[ti].m_modelInfo.aoMapSRV.Reset();
                            scene_models[ti].m_modelInfo.emissiveMapSRV.Reset();
#elif defined(__USE_OPENGL__)
                            scene_models[ti].m_modelInfo.textureIDs.clear();
                            scene_models[ti].m_modelInfo.normalMapIDs.clear();
                            scene_models[ti].m_modelInfo.metallicTexID  = 0;
                            scene_models[ti].m_modelInfo.roughnessTexID = 0;
                            scene_models[ti].m_modelInfo.aoTexID        = 0;
                            scene_models[ti].m_modelInfo.emissiveTexID  = 0;
#elif defined(__USE_VULKAN__)
                            scene_models[ti].m_modelInfo.textures.clear();
                            // NOTE: Do NOT null descriptorSet here - same reason as GLB cache-restore
                            // Step 4: BindGLTFMaterialTexturesToModel only rebuilds textureDescriptorSet
                            // (set=1); nulling descriptorSet (set=0) permanently breaks the draw guard.
#endif
                            BindGLTFMaterialTexturesToModel(mi, scene_models[ti].m_modelInfo, scene_models[ti], miniDoc);

#if defined(__USE_DIRECTX_12__)
                            // DX12: re-upload the rebound textures to the native D3D12
                            // heap and force descriptor rewrite - the rebind above only
                            // fixes the DX11-on-12 SRVs (see GLB cache-restore Step 4).
                            scene_models[ti].RefreshDX12Textures();
#elif defined(__USE_OPENGL__)
                            // OpenGL: rebuild the GL texture handles from the freshly
                            // rebound materials - the rebind above does not write into
                            // textureIDs/normalMapIDs (see GLB cache-restore Step 4).
                            scene_models[ti].RefreshOpenGLTextures();
#endif

                            for (int m2 = 0; m2 < MAX_MODELS; ++m2)
                            {
                                if (models[m2].m_modelInfo.cachedInstanceIndex == ti &&
                                    models[m2].m_modelInfo.sourceSceneFile    == gltfFile)
                                {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                    models[m2].m_modelInfo.textures           = scene_models[ti].m_modelInfo.textures;
                                    models[m2].m_modelInfo.textureSRVs        = scene_models[ti].m_modelInfo.textureSRVs;
                                    models[m2].m_modelInfo.normalMapSRVs      = scene_models[ti].m_modelInfo.normalMapSRVs;
                                    models[m2].m_modelInfo.metallicMapSRV     = scene_models[ti].m_modelInfo.metallicMapSRV;
                                    models[m2].m_modelInfo.roughnessMapSRV    = scene_models[ti].m_modelInfo.roughnessMapSRV;
                                    models[m2].m_modelInfo.aoMapSRV           = scene_models[ti].m_modelInfo.aoMapSRV;
                                    models[m2].m_modelInfo.emissiveMapSRV     = scene_models[ti].m_modelInfo.emissiveMapSRV;
                                    models[m2].m_modelInfo.emissiveMapTexture = scene_models[ti].m_modelInfo.emissiveMapTexture;
                                    models[m2].m_modelInfo.useEmissiveMap     = scene_models[ti].m_modelInfo.useEmissiveMap;
                                    models[m2].m_materials                    = scene_models[ti].m_materials;
#elif defined(__USE_OPENGL__)
                                    models[m2].m_modelInfo.textureIDs      = scene_models[ti].m_modelInfo.textureIDs;
                                    models[m2].m_modelInfo.normalMapIDs    = scene_models[ti].m_modelInfo.normalMapIDs;
                                    models[m2].m_modelInfo.metallicTexID   = scene_models[ti].m_modelInfo.metallicTexID;
                                    models[m2].m_modelInfo.roughnessTexID  = scene_models[ti].m_modelInfo.roughnessTexID;
                                    models[m2].m_modelInfo.aoTexID         = scene_models[ti].m_modelInfo.aoTexID;
                                    models[m2].m_modelInfo.glossTexID      = scene_models[ti].m_modelInfo.glossTexID;
                                    models[m2].m_modelInfo.emissiveTexID   = scene_models[ti].m_modelInfo.emissiveTexID;
                                    models[m2].m_modelInfo.useEmissiveMap  = scene_models[ti].m_modelInfo.useEmissiveMap;
                                    models[m2].m_materials                 = scene_models[ti].m_materials;
#elif defined(__USE_VULKAN__)
                                    models[m2].m_modelInfo.textures        = scene_models[ti].m_modelInfo.textures;
                                    models[m2].m_modelInfo.descriptorSet   = scene_models[ti].m_modelInfo.descriptorSet;
                                    models[m2].m_materials                 = scene_models[ti].m_materials;
#endif
                                    #if defined(_DEBUG_SCENEMANAGER_)
                                        debug.logDebugMessage(LogLevel::LOG_INFO,
                                            L"[SceneManager] TEXTURE-RELOAD '%ls' - written back to models[%d]",
                                            scene_models[ti].m_modelInfo.name.c_str(), m2);
                                    #endif
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }

                // --- Step 5: Start animations ---
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    std::wstring(L"[SceneManager] CACHE-RESTORE GLTF Step 5: bAnimationsLoaded=") +
                    (bAnimationsLoaded ? L"true" : L"false") + L" animCount=" +
                    std::to_wstring(modelAnimator.gltfAnimator.GetAnimationCount()) +
                    L" instances=" + std::to_wstring(instanceIndex));
                if (bAnimationsLoaded && modelAnimator.gltfAnimator.GetAnimationCount() > 0)
                {
                    for (int animIdx = 0; animIdx < modelAnimator.gltfAnimator.GetAnimationCount(); ++animIdx)
                    {
                        int parentID = FindParentModelIDForAnimation(animIdx);
                        debug.logLevelMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] CACHE-RESTORE GLTF anim[" + std::to_wstring(animIdx) +
                            L"] parentID=" + std::to_wstring(parentID));
                        if (parentID < 0) continue;
                        bool created = modelAnimator.gltfAnimator.CreateAnimationInstance(animIdx, parentID);
                        debug.logLevelMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] CACHE-RESTORE GLTF anim[" + std::to_wstring(animIdx) +
                            L"] instance " + (created ? L"CREATED" : L"FAILED") +
                            L" parentID=" + std::to_wstring(parentID));
                        if (created)
                        {
                            modelAnimator.gltfAnimator.ForceAnimationReset(parentID);
                            modelAnimator.gltfAnimator.SetAnimationSpeed(parentID, 0.75f);
                            modelAnimator.gltfAnimator.SetAnimationLooping(parentID, true);
                            modelAnimator.gltfAnimator.StartAnimation(parentID, animIdx);
                        }
                    }
                }

                if (instanceIndex > 0)
                {
                    #if defined(_DEBUG_SCENEMANAGER_)
                    {
                        auto _e  = std::chrono::high_resolution_clock::now();
                        auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(_e - _sceneLoadBegin).count();
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] ParseGLTFScene() CACHE HIT - ENGINE LOAD TIME: %lld ms - %d instances restored from %d cache entries",
                            _ms, instanceIndex, cacheCount);
                    }
                    #endif
                    bLoadedFromCache = true;
                    return true;
                }

                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING,
                        L"[SceneManager] ParseGLTFScene() cache had %d entries but rebuild yielded 0 - falling through to full parse",
                        cacheCount);
                #endif
            }
        }
    }

    // Check if the GLTF file exists on the filesystem
    if (!std::filesystem::exists(gltfFile)) {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[SceneManager] File not found: %ls", gltfFile.c_str());
        #endif
        return false;
    }

    // Show progress before the potentially slow file I/O begins.
    showStage(L"Reading GLTF file...");

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

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] GLTF Exporter Detected: %ls", m_lastDetectedExporter.c_str());
    #endif

    // Build Blender import configuration (coordinate flip, winding, version caps).
    {
        std::string generator;
        if (doc.contains("asset") && doc["asset"].contains("generator") &&
            doc["asset"]["generator"].is_string())
            generator = doc["asset"]["generator"].get<std::string>();
        m_blenderConfig = BlenderImports::BuildConfig(generator, doc);
    }

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
    showStage(L"Parsing scene data...");
    ParseGLTFCamera(doc, myRenderer->myCamera, myRenderer->iOrigWidth, myRenderer->iOrigHeight);
    ParseGLTFLights(doc);
    EnsureDefaultSunLight();
    ParseMaterialsFromGLTF(doc);

    // Parse animations from GLTF document and store them in the global animator
    bAnimationsLoaded = modelAnimator.gltfAnimator.ParseAnimationsFromGLTF(doc, gltfBinaryData);
    if (bAnimationsLoaded)
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] Successfully loaded %d animations from GLTF", modelAnimator.gltfAnimator.GetAnimationCount());
        #endif
        modelAnimator.gltfAnimator.DebugPrintAnimationInfo();
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

    // Tag which file is being parsed so NodeRecursive write-backs can store the source path.
    m_currentSceneFile = gltfFile;

    // Get reference to the nodes array for recursive parsing
    showStage(L"Building scene geometry...");
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
        ParseGLTFNodeRecursive(rootNode, nodeIndex, XMMatrixIdentity(), doc, nodes, instanceIndex, -1);
    }

    // Auto-initialise all animations discovered during loading.
    // Each animation's channel node indices are matched against loaded scene models to
    // resolve the root parent, removing the need for hardcoded per-scene animation startup.
    if (bAnimationsLoaded && modelAnimator.gltfAnimator.GetAnimationCount() > 0)
    {
        for (int animIdx = 0; animIdx < modelAnimator.gltfAnimator.GetAnimationCount(); ++animIdx)
        {
            int parentID = FindParentModelIDForAnimation(animIdx);
            if (parentID < 0)
                continue;

            bool created = modelAnimator.gltfAnimator.CreateAnimationInstance(animIdx, parentID);
            if (created)
            {
                modelAnimator.gltfAnimator.ForceAnimationReset(parentID);
                modelAnimator.gltfAnimator.SetAnimationSpeed(parentID, 0.75f);
                modelAnimator.gltfAnimator.SetAnimationLooping(parentID, true);
                modelAnimator.gltfAnimator.StartAnimation(parentID, animIdx);
            }
        }
    }

    #if defined(_DEBUG_SCENEMANAGER_)
    {
        auto _sceneLoadEnd = std::chrono::high_resolution_clock::now();
        auto _sceneLoadMs  = std::chrono::duration_cast<std::chrono::milliseconds>(_sceneLoadEnd - _sceneLoadBegin).count();
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] ParseGLTFScene() LOAD END - ENGINE LOAD TIME: %lld ms - Instances: %d - Exporter: %ls",
            _sceneLoadMs, instanceIndex, m_lastDetectedExporter.c_str());
    }
    #endif

    // Cache-only mode: GPU resources are now in models[] only.
    // Clear all populated scene_models[] entries so the renderer skips them.
    if (bCacheOnly && instanceIndex > 0)
    {
        for (int i = 0; i < instanceIndex; ++i)
        {
            scene_models[i].m_modelInfo  = ModelInfo{};
            scene_models[i].m_materials.clear();
            scene_models[i].m_isLoaded   = false;
            scene_models[i].bInitialized = false;
        }
        debug.logLevelMessage(LogLevel::LOG_INFO,
            (L"[SceneManager] ParseGLTFScene() cache-only mode -- " +
             std::to_wstring(instanceIndex) + L" model(s) cached in models[], scene_models[] cleared.").c_str());
    }

    // Validate GLTF animation channel node mapping against created scene model instances.
    // This helps catch the common case where animations target non-mesh nodes (no scene_models entry),
    // or where node indices were not preserved during parsing.
    #if defined(_DEBUG_SCENEMANAGER_)
        if (bAnimationsLoaded)
        {
            for (int animIndex = 0; animIndex < modelAnimator.gltfAnimator.GetAnimationCount(); ++animIndex)
            {
                const GLTFAnimation* anim = modelAnimator.gltfAnimator.GetAnimation(animIndex);
                if (!anim)
                    continue;

                for (size_t c = 0; c < anim->channels.size(); ++c)
                {
                    const AnimationChannel& ch = anim->channels[c];
                    bool foundTarget = false;

                    for (int mi = 0; mi < instanceIndex; ++mi)
                    {
                        if (scene_models[mi].m_isLoaded && scene_models[mi].m_modelInfo.gltfNodeIndex == ch.targetNodeIndex)
                        {
                            foundTarget = true;
                            break;
                        }
                    }

                    if (!foundTarget)
                    {
                        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Animation %d channel %d targets GLTF node %d, but no scene model instance was created for that node.", animIndex, (int)c, ch.targetNodeIndex);
                    }
                }
            }
        }
    #endif

    // Return success if at least one model instance was created
    return (instanceIndex > 0);
}


// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------
void SceneManager::ParseGLTFNodeRecursive(const json& node, int nodeIndex, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex, int parentModelID)
{
    // Prevent buffer overflow by checking maximum scene model limit
    if (instanceIndex >= MAX_SCENE_MODELS)
        return;

    // Check if this node contains a mesh to be rendered
    bool hasMesh = node.contains("mesh") && node["mesh"].is_number_integer();

    // Store the current instance index as potential parent ID for children
    int currentParentID = parentModelID;

    // Load and decompose the node's local transformation matrix
    XMMATRIX nodeTransform = GetNodeWorldMatrix(node, m_blenderConfig);

    // Decompose transformation matrix to extract scale for potential geometry baking
    XMVECTOR outScale, outRot, outTrans;
    XMMatrixDecompose(&outScale, &outRot, &outTrans, nodeTransform);
    XMFLOAT3 scale;
    XMStoreFloat3(&scale, outScale);

    // Capture base local TRS from this GLTF node for correct animation evaluation.
    // Blender 4.4+ exports GLTF animations as LOCAL TRS values.
    XMFLOAT3 baseLocalTranslation;
    XMFLOAT4 baseLocalRotationQuat;
    XMFLOAT3 baseLocalScale;
    XMStoreFloat3(&baseLocalTranslation, outTrans);
    XMStoreFloat4(&baseLocalRotationQuat, outRot);
    XMStoreFloat3(&baseLocalScale, outScale);

    // Check if the node has non-identity scale that needs to be baked into geometry
    bool hasNonIdentityScale = (fabs(scale.x - 1.0f) > 0.0001f || fabs(scale.y - 1.0f) > 0.0001f || fabs(scale.z - 1.0f) > 0.0001f);

    // Process mesh if present in this node.
    // Each GLTF mesh primitive is created as its own scene_models entry so that
    // every sub-mesh gets its own material, textures and draw call.
    if (hasMesh)
    {
        // Extract mesh index from the node definition
        int meshIndex = node["mesh"];

        // Validate that meshes array exists in the GLTF document
        if (!doc.contains("meshes") || !doc["meshes"].is_array()) return;

        const auto& meshes = doc["meshes"];

        // Validate mesh index is within valid range
        if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;

        // Determine base name for this node
        std::wstring modelName;
        if (node.contains("name") && node["name"].is_string())
            modelName = sysUtils.ToWString(node["name"].get<std::string>());
        else
            modelName = L"Node_" + std::to_wstring(instanceIndex) + L"_Mesh_" + std::to_wstring(meshIndex);

        // Determine number of primitives so we can loop over them
        const auto& primsMesh = meshes[meshIndex];
        int numPrimitives = 1;
        if (primsMesh.contains("primitives") && primsMesh["primitives"].is_array())
            numPrimitives = static_cast<int>(primsMesh["primitives"].size());

        // Pre-compute world transform once for all primitives of this node.
        // Scale is baked per-primitive into geometry; adjust the matrix accordingly.
        XMMATRIX effectiveNodeTransform = nodeTransform;
        if (hasNonIdentityScale)
            effectiveNodeTransform *= XMMatrixScaling(1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z);
        XMMATRIX worldTransform = parentTransform * effectiveNodeTransform;

        XMFLOAT4X4 f4x4;
        XMStoreFloat4x4(&f4x4, worldTransform);

        // Track the first primitive's instanceIndex so child nodes can parent to it.
        int firstPrimInstanceIndex = instanceIndex;

        for (int primIdx = 0; primIdx < numPrimitives && instanceIndex < MAX_SCENE_MODELS; ++primIdx)
        {
            // Primitive 0 keeps the base node name for backward-compatible animation lookups.
            // Subsequent primitives get a "_pN" suffix so they have unique cache keys.
            std::wstring primName = (primIdx == 0)
                ? modelName
                : (modelName + L"_p" + std::to_wstring(primIdx));

            // --- Find or create per-primitive entry in the global model cache (single pass) ---
            int modelSlot = -1;
            int firstFree = -1;
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name == primName &&
                    (models[m].m_modelInfo.sourceSceneFile.empty() ||
                     models[m].m_modelInfo.sourceSceneFile == m_currentSceneFile))
                { modelSlot = m; break; }
                if (firstFree < 0 && models[m].m_modelInfo.name.empty()) firstFree = m;
            }

            if (modelSlot < 0 && firstFree >= 0)
            {
                modelSlot = firstFree;
                models[modelSlot].m_modelInfo.name = primName;
                models[modelSlot].m_modelInfo.ID   = modelSlot;
                models[modelSlot].m_modelInfo.vertices.clear();
                models[modelSlot].m_modelInfo.indices.clear();
                models[modelSlot].m_modelInfo.textures.clear();
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                models[modelSlot].m_modelInfo.textureSRVs.clear();
                models[modelSlot].m_modelInfo.normalMapSRVs.clear();
#endif
                LoadGLTFMeshPrimitives(meshIndex, doc, models[modelSlot], primIdx);

                if (hasNonIdentityScale)
                {
                    for (auto& v : models[modelSlot].m_modelInfo.vertices)
                    {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        v.position.x *= scale.x;
                        v.position.y *= scale.y;
                        v.position.z *= scale.z;
#else
                        v.position[0] *= scale.x;
                        v.position[1] *= scale.y;
                        v.position[2] *= scale.z;
#endif
                    }
                }
            }

            if (modelSlot < 0)
            {
                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] No free model cache slot for primitive %d of '%ls' - skipping", primIdx, modelName.c_str());
                #endif
                continue;
            }

            if (models[modelSlot].m_modelInfo.vertices.empty() || models[modelSlot].m_modelInfo.indices.empty())
            {
                models[modelSlot].m_modelInfo.name.clear();
                continue;
            }

            // cache.dat restore hole: LoadCache() restores geometry + names into models[]
            // but Texture objects and Material structs cannot be serialised to disk.  On
            // the first full parse after startup the slot is FOUND by name above, so the
            // creation branch (which binds materials/textures inside
            // LoadGLTFMeshPrimitives) never runs - the model would render with no
            // materials and no textures until a later cache-restore rebind.  Bind the
            // primitive's material now, exactly as the creation branch would have.
            if (models[modelSlot].m_materials.empty() &&
                primsMesh.contains("primitives") && primsMesh["primitives"].is_array() &&
                primIdx < (int)primsMesh["primitives"].size())
            {
                int rebindMatIdx = primsMesh["primitives"][primIdx].value("material", -1);
                if (rebindMatIdx >= 0)
                {
                    // Clear the cache.dat-restored material-name list first so the bind
                    // below does not append a duplicate name entry.
                    models[modelSlot].m_modelInfo.materials.clear();
                    BindGLTFMaterialTexturesToModel(rebindMatIdx,
                        models[modelSlot].m_modelInfo, models[modelSlot], doc);
                }
            }

            // --- Populate scene_models[instanceIndex] ---
            scene_models[instanceIndex].CopyFrom(models[modelSlot]);
            scene_models[instanceIndex].m_modelInfo.worldMatrix = worldTransform;

            if (primIdx == 0)
            {
                scene_models[instanceIndex].m_modelInfo.iParentModelID        = parentModelID;
                scene_models[instanceIndex].m_modelInfo.gltfNodeIndex         = nodeIndex;
                scene_models[instanceIndex].m_modelInfo.importType            = ImportType::GLTF;
                scene_models[instanceIndex].m_modelInfo.baseLocalTranslation  = baseLocalTranslation;
                scene_models[instanceIndex].m_modelInfo.baseLocalRotationQuat = baseLocalRotationQuat;
                scene_models[instanceIndex].m_modelInfo.baseLocalScale        = baseLocalScale;
                scene_models[instanceIndex].m_modelInfo.animLocalTranslation  = baseLocalTranslation;
                scene_models[instanceIndex].m_modelInfo.animLocalRotationQuat = baseLocalRotationQuat;
                scene_models[instanceIndex].m_modelInfo.animLocalScale        = baseLocalScale;
            }
            else
            {
                static const XMFLOAT3 identT = { 0.0f, 0.0f, 0.0f };
                static const XMFLOAT4 identR = { 0.0f, 0.0f, 0.0f, 1.0f };
                static const XMFLOAT3 identS = { 1.0f, 1.0f, 1.0f };
                scene_models[instanceIndex].m_modelInfo.iParentModelID        = firstPrimInstanceIndex;
                scene_models[instanceIndex].m_modelInfo.gltfNodeIndex         = -1;
                scene_models[instanceIndex].m_modelInfo.baseLocalTranslation  = identT;
                scene_models[instanceIndex].m_modelInfo.baseLocalRotationQuat = identR;
                scene_models[instanceIndex].m_modelInfo.baseLocalScale        = identS;
                scene_models[instanceIndex].m_modelInfo.animLocalTranslation  = identT;
                scene_models[instanceIndex].m_modelInfo.animLocalRotationQuat = identR;
                scene_models[instanceIndex].m_modelInfo.animLocalScale        = identS;
            }
            scene_models[instanceIndex].m_modelInfo.bHasBaseLocalTRS = true;

            scene_models[instanceIndex].m_modelInfo.position = XMFLOAT3(f4x4._41, f4x4._42, f4x4._43);
            XMStoreFloat3(&scene_models[instanceIndex].m_modelInfo.scale, XMVectorSet(1.0f, 1.0f, 1.0f, 0));

            scene_models[instanceIndex].m_modelInfo.ID   = instanceIndex;
            scene_models[instanceIndex].m_modelInfo.name = primName;

            scene_models[instanceIndex].SetupModelForRendering(instanceIndex);
#if defined(__USE_VULKAN__)
            // SetupModelForRendering created the material UBO and texture descriptor set with defaults.
            // Upload the real material data now that those GPU buffers exist.
            // Prefer textures already resident in m_materials (loaded by LoadGLTFMeshPrimitives /
            // BindGLTFMaterialTexturesToModel via CopyFrom) to avoid a second disk/memory decode.
            // Fall back to a fresh BindGLTFMaterialTexturesToModel only if m_materials is empty.
            if (!scene_models[instanceIndex].m_materials.empty())
            {
                // Use whichever material was stored during the first bind pass; for single-material
                // GLTF meshes this is always the first (and only) entry in the map.
                for (auto& [matName, mat] : scene_models[instanceIndex].m_materials)
                    UploadFBXMaterialToVulkanModel(mat, scene_models[instanceIndex].m_modelInfo);
            }
            else if (doc.contains("meshes") && meshIndex < (int)doc["meshes"].size())
            {
                // Fallback: first bind pass produced no materials; try a fresh load from the GLTF doc.
                const auto& gltfPrims = doc["meshes"][meshIndex]["primitives"];
                if (gltfPrims.is_array() && primIdx < (int)gltfPrims.size())
                {
                    int gltfMatIdx = gltfPrims[primIdx].value("material", -1);
                    if (gltfMatIdx >= 0)
                        BindGLTFMaterialTexturesToModel(gltfMatIdx,
                            scene_models[instanceIndex].m_modelInfo,
                            scene_models[instanceIndex], doc);
                }
            }
#endif
            scene_models[instanceIndex].ApplyDefaultLightingFromManager(lightsManager);
            scene_models[instanceIndex].m_isLoaded = true;

            // --- Write-back: full GPU-ready copy into models[] for fast-path reloads ---
            // CopyFrom captures vertex/index buffers, SRVs, shaders, and textures via ComPtr/shared_ptr
            // AddRef - the GPU resources stay alive in both models[] and scene_models[].
            models[modelSlot].CopyFrom(scene_models[instanceIndex]);
            models[modelSlot].m_isLoaded                         = true;
            models[modelSlot].m_modelInfo.ID                     = modelSlot;   // restore slot ID (CopyFrom brings in instanceIndex)
            models[modelSlot].m_modelInfo.sourceSceneFile        = m_currentSceneFile;
            models[modelSlot].m_modelInfo.cachedInstanceIndex    = instanceIndex;
            models[modelSlot].m_modelInfo.bGpuReady              = true;

            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO,
                    L"[SceneManager] scene_models[%d] prim[%d/%d] \"%ls\" | ParentID:%d | Pos(%.2f,%.2f,%.2f)",
                    instanceIndex, primIdx, numPrimitives - 1, primName.c_str(),
                    scene_models[instanceIndex].m_modelInfo.iParentModelID, f4x4._41, f4x4._42, f4x4._43);

                if (!models[modelSlot].m_modelInfo.vertices.empty())
                {
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    float vert0z = models[modelSlot].m_modelInfo.vertices[0].position.z;
                    #else
                    float vert0z = models[modelSlot].m_modelInfo.vertices[0].position[2];
                    #endif
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"[SceneManager] CACHE-WRITE '%ls' vert[0].z=%.4f world_tz=%.4f",
                        primName.c_str(), vert0z, f4x4._43);
                }
            #endif

            ++instanceIndex;
        }

        // Children of this node parent to the first primitive's slot
        currentParentID = firstPrimInstanceIndex;
    }

    // =====================================================================
    // Transform-Only Node Support (Blender empties / grouping nodes)
    // =====================================================================
    if (!hasMesh && node.contains("children") && node["children"].is_array() && !node["children"].empty())
    {
        if (instanceIndex >= MAX_SCENE_MODELS)
            return;

        XMMATRIX worldTransform = parentTransform * nodeTransform;

        scene_models[instanceIndex].DestroyModel();
        scene_models[instanceIndex].m_isLoaded = true;
        scene_models[instanceIndex].m_modelInfo.bIsTransformOnly  = true;
        scene_models[instanceIndex].m_modelInfo.bIsTransformProxy = true;
        scene_models[instanceIndex].m_modelInfo.ID               = instanceIndex;
        scene_models[instanceIndex].m_modelInfo.iParentModelID   = parentModelID;
        scene_models[instanceIndex].m_modelInfo.gltfNodeIndex    = nodeIndex;
        scene_models[instanceIndex].m_modelInfo.importType       = ImportType::GLTF;
        scene_models[instanceIndex].m_modelInfo.worldMatrix      = worldTransform;

        scene_models[instanceIndex].m_modelInfo.baseLocalTranslation   = baseLocalTranslation;
        scene_models[instanceIndex].m_modelInfo.baseLocalRotationQuat  = baseLocalRotationQuat;
        scene_models[instanceIndex].m_modelInfo.baseLocalScale         = baseLocalScale;
        scene_models[instanceIndex].m_modelInfo.animLocalTranslation   = baseLocalTranslation;
        scene_models[instanceIndex].m_modelInfo.animLocalRotationQuat  = baseLocalRotationQuat;
        scene_models[instanceIndex].m_modelInfo.animLocalScale         = baseLocalScale;
        scene_models[instanceIndex].m_modelInfo.bHasBaseLocalTRS = true;

        // Assign a descriptive name for debugging
        if (node.contains("name") && node["name"].is_string())
        {
            std::string nodeName = node["name"];
            scene_models[instanceIndex].m_modelInfo.name = sysUtils.ToWString(nodeName);
        }
        else
        {
            scene_models[instanceIndex].m_modelInfo.name = L"TransformNode_" + std::to_wstring(nodeIndex);
        }

        // --- Write-back: cache transform-only node data in models[] for fast-path reloads ---
        {
            int freeSlot = -1;
            for (int m = 0; m < MAX_MODELS; ++m)
            {
                if (models[m].m_modelInfo.name == scene_models[instanceIndex].m_modelInfo.name) { freeSlot = m; break; }
                if (freeSlot < 0 && models[m].m_modelInfo.name.empty()) freeSlot = m;
            }
            if (freeSlot >= 0)
            {
                ModelInfo& cm = models[freeSlot].m_modelInfo;
                cm.name               = scene_models[instanceIndex].m_modelInfo.name;
                cm.worldMatrix        = scene_models[instanceIndex].m_modelInfo.worldMatrix;
                cm.iParentModelID     = parentModelID;
                cm.gltfNodeIndex      = nodeIndex;
                cm.baseLocalTranslation   = baseLocalTranslation;
                cm.baseLocalRotationQuat  = baseLocalRotationQuat;
                cm.baseLocalScale         = baseLocalScale;
                cm.animLocalTranslation   = baseLocalTranslation;
                cm.animLocalRotationQuat  = baseLocalRotationQuat;
                cm.animLocalScale         = baseLocalScale;
                cm.bHasBaseLocalTRS   = true;
                cm.bIsTransformOnly   = true;
                cm.bIsTransformProxy  = true;
                {
                    XMFLOAT4X4 tXf; XMStoreFloat4x4(&tXf, worldTransform);
                    cm.position = XMFLOAT3(tXf._41, tXf._42, tXf._43);
                }
                cm.sourceSceneFile    = m_currentSceneFile;
                cm.cachedInstanceIndex = instanceIndex;
                cm.bGpuReady          = true;
            }
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] Created transform-only node instance %d for GLTF node %d (ParentID: %d)",
                instanceIndex, nodeIndex, parentModelID);
        #endif

        // This transform-only node becomes the parent for its children.
        currentParentID = instanceIndex;
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
            ParseGLTFNodeRecursive(allNodes[ci], ci, parentTransform * nodeTransform, doc, allNodes, instanceIndex, currentParentID);
        }
    }
}


// --------------------------------------------------------------------------------------------------

// Enhanced LoadGLTFMeshPrimitives with comprehensive debug output
// Replace the existing LoadGLTFMeshPrimitives function with this version
void SceneManager::LoadGLTFMeshPrimitives(int meshIndex, const json& doc, Model& model, int primitiveFilter)
{
    #if defined(_DEBUG_SCENEMANAGER_)
    if (primitiveFilter == 0)
    {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[SceneManager] LoadGLTFMeshPrimitives() meshIndex: %d | binData: %d bytes", meshIndex, static_cast<int>(gltfBinaryData.size()));
    }
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
    if (primitiveFilter == 0)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] Mesh[%d] has %d primitive(s) to process.",
            meshIndex, static_cast<int>(mesh["primitives"].size()));
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

    // Lock texture vector mutations for this model instance.
    // This prevents cross-thread corruption (loader thread vs render thread).
    {
        std::string lockName = "model_texture_update_" + std::to_string(model.m_modelInfo.ID);
        ThreadLockHelper lock(threadManager, lockName.c_str(), 5000);

        // If we could not acquire the lock, we must not mutate shared texture containers.
        if (!lock.IsLocked())
        {
            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] LoadGLTFMeshPrimitives() could not acquire lock for model ID %d - skipping texture pre-allocation.", model.m_modelInfo.ID);
            #endif
            return;
        }

        model.m_modelInfo.textures.clear();                   // Clear textures list for this model instance.
        model.m_modelInfo.textures.reserve(maxTexturesNeeded); // Reserve to avoid frequent reallocation.
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        model.m_modelInfo.textureSRVs.clear();
        model.m_modelInfo.textureSRVs.reserve(maxTexturesNeeded);
        model.m_modelInfo.normalMapSRVs.clear();
        model.m_modelInfo.normalMapSRVs.reserve(maxTexturesNeeded);
#endif
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(
            LogLevel::LOG_INFO,
            L"[SceneManager] PRE-ALLOCATED %d texture slots on models[] array for %d primitives",
            static_cast<int>(maxTexturesNeeded),
            static_cast<int>(numPrimitives));
    #endif

    int primLoopIdx = -1;
    for (const auto& prim : mesh["primitives"])
    {
        ++primLoopIdx;
        // When a specific primitive is requested, skip all others.
        if (primitiveFilter >= 0 && primLoopIdx != primitiveFilter)
            continue;

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

        // Decode float3 positions and apply GLTF→DX coordinate conversion.
        for (int vi = 0; vi < vertexCount; ++vi)
        {
            const size_t base = posOffset + (size_t)vi * (size_t)12;
            XMFLOAT3 rawPos = {
                *reinterpret_cast<const float*>(&gltfBinaryData[base + 0]),
                *reinterpret_cast<const float*>(&gltfBinaryData[base + 4]),
                *reinterpret_cast<const float*>(&gltfBinaryData[base + 8])
            };
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            rawVertices[(size_t)vi].position = BlenderImports::ConvertPosition(rawPos, m_blenderConfig.flipAxes);
            rawVertices[(size_t)vi].normal   = XMFLOAT3(0, 1, 0);
            rawVertices[(size_t)vi].texCoord = XMFLOAT2(0, 0);
#else
            { XMFLOAT3 _cp = BlenderImports::ConvertPosition(rawPos, m_blenderConfig.flipAxes);
              rawVertices[(size_t)vi].position[0]=_cp.x; rawVertices[(size_t)vi].position[1]=_cp.y; rawVertices[(size_t)vi].position[2]=_cp.z; }
            rawVertices[(size_t)vi].normal[0]=0; rawVertices[(size_t)vi].normal[1]=1; rawVertices[(size_t)vi].normal[2]=0;
            rawVertices[(size_t)vi].texCoord[0]=0; rawVertices[(size_t)vi].texCoord[1]=0;
#endif
        }

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(
                LogLevel::LOG_INFO,
                L"[SceneManager] First vertex position: (%.3f, %.3f, %.3f)",
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                rawVertices[0].position.x,
                rawVertices[0].position.y,
                rawVertices[0].position.z);
#else
                rawVertices[0].position[0],
                rawVertices[0].position[1],
                rawVertices[0].position[2]);
#endif
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
                            const size_t base = normOffset + (size_t)vi * (size_t)12;
                            XMFLOAT3 rawNorm = {
                                *reinterpret_cast<const float*>(&gltfBinaryData[base + 0]),
                                *reinterpret_cast<const float*>(&gltfBinaryData[base + 4]),
                                *reinterpret_cast<const float*>(&gltfBinaryData[base + 8])
                            };
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            rawVertices[(size_t)vi].normal =
                                BlenderImports::ConvertNormal(rawNorm, m_blenderConfig.flipAxes);
#else
                            { XMFLOAT3 _cn = BlenderImports::ConvertNormal(rawNorm, m_blenderConfig.flipAxes);
                              rawVertices[(size_t)vi].normal[0]=_cn.x; rawVertices[(size_t)vi].normal[1]=_cn.y; rawVertices[(size_t)vi].normal[2]=_cn.z; }
#endif
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
        // Load UVs (TEXCOORD_0) if present
        // IMPORTANT: GLTF TEXCOORD accessors are not guaranteed to be float2 tightly packed.
        //            They may be UNORM U8/U16 and bufferViews may specify byteStride.
        //            Incorrect decoding here will commonly produce all-zero UVs, which will sample
        //            a single texel (often black), making the entire model appear black.
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
                    const auto& texView = bufferViews[texViewIdx];

                    // Resolve accessor metadata.
                    int texComponentType = texAcc.value("componentType", 5126); // 5126 = FLOAT
                    bool texNormalized = texAcc.value("normalized", false);
                    std::string texType = texAcc.value("type", "VEC2");

                    // We only support VEC2 for TEXCOORD_0.
                    int componentCount = 2;
                    if (texType != "VEC2")
                    {
                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] TEXCOORD_0 accessor type is not VEC2 (type=%hs). Using default UVs.", texType.c_str());
                        #endif
                        componentCount = 0;
                    }

                    // Determine byte size per component.
                    size_t bytesPerComponent = 0;
                    if (texComponentType == 5126) bytesPerComponent = 4; // FLOAT
                    if (texComponentType == 5123) bytesPerComponent = 2; // UNSIGNED_SHORT
                    if (texComponentType == 5122) bytesPerComponent = 2; // SHORT
                    if (texComponentType == 5121) bytesPerComponent = 1; // UNSIGNED_BYTE
                    if (texComponentType == 5120) bytesPerComponent = 1; // BYTE

                    if ((componentCount == 2) && (bytesPerComponent > 0))
                    {
                        // Compute base offset.
                        size_t texOffset = (size_t)texView.value("byteOffset", 0) + (size_t)texAcc.value("byteOffset", 0);

                        // Respect byteStride if present; otherwise assume tightly packed VEC2.
                        size_t stride = (size_t)texView.value("byteStride", 0);
                        if (stride == 0)
                        {
                            stride = (size_t)componentCount * bytesPerComponent;
                        }

                        // Bounds check using last element address.
                        const size_t elementBytes = (size_t)componentCount * bytesPerComponent;
                        const size_t lastByte = texOffset + (size_t)(vertexCount - 1) * stride + elementBytes;

                        if (texOffset < gltfBinaryData.size() && lastByte <= gltfBinaryData.size())
                        {
                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"[SceneManager] Loading TEXCOORD_0 (componentType=%d normalized=%d stride=%d) from offset %d",
                                    texComponentType,
                                    texNormalized ? 1 : 0,
                                    static_cast<int>(stride),
                                    static_cast<int>(texOffset));
                            #endif

                            for (int vi = 0; vi < vertexCount; ++vi)
                            {
                                const size_t base = texOffset + (size_t)vi * stride;

                                float u = 0.0f;
                                float v = 0.0f;

                                // Decode based on accessor component type.
                                if (texComponentType == 5126)
                                {
                                    // FLOAT
                                    u = *reinterpret_cast<const float*>(&gltfBinaryData[base + 0]);
                                    v = *reinterpret_cast<const float*>(&gltfBinaryData[base + 4]);
                                }
                                else if (texComponentType == 5123)
                                {
                                    // UNSIGNED_SHORT
                                    const uint16_t uu = *reinterpret_cast<const uint16_t*>(&gltfBinaryData[base + 0]);
                                    const uint16_t vv = *reinterpret_cast<const uint16_t*>(&gltfBinaryData[base + 2]);
                                    if (texNormalized)
                                    {
                                        u = (float)uu / 65535.0f;
                                        v = (float)vv / 65535.0f;
                                    }
                                    else
                                    {
                                        u = (float)uu;
                                        v = (float)vv;
                                    }
                                }
                                else if (texComponentType == 5122)
                                {
                                    // SHORT
                                    const int16_t uu = *reinterpret_cast<const int16_t*>(&gltfBinaryData[base + 0]);
                                    const int16_t vv = *reinterpret_cast<const int16_t*>(&gltfBinaryData[base + 2]);
                                    if (texNormalized)
                                    {
                                        u = std::max(-1.0f, (float)uu / 32767.0f);
                                        v = std::max(-1.0f, (float)vv / 32767.0f);
                                    }
                                    else
                                    {
                                        u = (float)uu;
                                        v = (float)vv;
                                    }
                                }
                                else if (texComponentType == 5121)
                                {
                                    // UNSIGNED_BYTE
                                    const uint8_t uu = gltfBinaryData[base + 0];
                                    const uint8_t vv = gltfBinaryData[base + 1];
                                    if (texNormalized)
                                    {
                                        u = (float)uu / 255.0f;
                                        v = (float)vv / 255.0f;
                                    }
                                    else
                                    {
                                        u = (float)uu;
                                        v = (float)vv;
                                    }
                                }
                                else if (texComponentType == 5120)
                                {
                                    // BYTE
                                    const int8_t uu = *reinterpret_cast<const int8_t*>(&gltfBinaryData[base + 0]);
                                    const int8_t vv = *reinterpret_cast<const int8_t*>(&gltfBinaryData[base + 1]);
                                    if (texNormalized)
                                    {
                                        u = std::max(-1.0f, (float)uu / 127.0f);
                                        v = std::max(-1.0f, (float)vv / 127.0f);
                                    }
                                    else
                                    {
                                        u = (float)uu;
                                        v = (float)vv;
                                    }
                                }

                                // Store decoded UVs.
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                rawVertices[(size_t)vi].texCoord.x = u;
                                rawVertices[(size_t)vi].texCoord.y = v;
#else
                                rawVertices[(size_t)vi].texCoord[0] = u;
                                rawVertices[(size_t)vi].texCoord[1] = v;
#endif
                            }

                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"[SceneManager] First vertex UV: (%.6f, %.6f)",
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                    rawVertices[0].texCoord.x,
                                    rawVertices[0].texCoord.y);
#else
                                    rawVertices[0].texCoord[0],
                                    rawVertices[0].texCoord[1]);
#endif
                            #endif
                        }
                        else
                        {
                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(
                                    LogLevel::LOG_WARNING,
                                    L"[SceneManager] TEXCOORD_0 data out of bounds. Offset=%d LastByte=%d BufferSize=%d. Using default UVs.",
                                    static_cast<int>(texOffset),
                                    static_cast<int>(lastByte),
                                    static_cast<int>(gltfBinaryData.size()));
                            #endif
                        }
                    }
                    else
                    {
                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_WARNING,
                                L"[SceneManager] TEXCOORD_0 accessor has unsupported componentType=%d or componentCount=%d. Using default UVs.",
                                texComponentType,
                                componentCount);
                        #endif
                    }
                }
            }
        }

        // -----------------------------
        // UV settings: KHR_texture_transform (offset / rotation / scale)
        // -----------------------------
        // The GLTF 2.0 KHR_texture_transform extension attaches a 2D affine
        // transform to a textureInfo (we honour the baseColorTexture one, which
        // exporters apply uniformly across a material's maps).  Baking it into
        // the primitive's vertex UVs here applies the transform on EVERY
        // renderer (DX11/DX12/OpenGL/Vulkan) with no shader changes, and covers
        // both the .gltf and .glb paths since they share this loader.
        // Spec: uv' = Translation(offset) * Rotation(rotation) * Scale(scale) * uv
        {
            int uvMatIdx = prim.value("material", -1);
            if (uvMatIdx >= 0 && doc.contains("materials") &&
                uvMatIdx < (int)doc["materials"].size())
            {
                const auto& uvMat = doc["materials"][uvMatIdx];
                if (uvMat.contains("pbrMetallicRoughness") &&
                    uvMat["pbrMetallicRoughness"].contains("baseColorTexture") &&
                    uvMat["pbrMetallicRoughness"]["baseColorTexture"].contains("extensions") &&
                    uvMat["pbrMetallicRoughness"]["baseColorTexture"]["extensions"].contains("KHR_texture_transform"))
                {
                    const auto& tt = uvMat["pbrMetallicRoughness"]["baseColorTexture"]["extensions"]["KHR_texture_transform"];

                    float uvOffX = 0.0f, uvOffY = 0.0f;       // offset  (default 0,0)
                    float uvSclX = 1.0f, uvSclY = 1.0f;       // scale   (default 1,1)
                    float uvRot  = 0.0f;                      // rotation in radians, CCW (default 0)
                    if (tt.contains("offset") && tt["offset"].is_array() && tt["offset"].size() >= 2)
                    {
                        uvOffX = tt["offset"][0].get<float>();
                        uvOffY = tt["offset"][1].get<float>();
                    }
                    if (tt.contains("scale") && tt["scale"].is_array() && tt["scale"].size() >= 2)
                    {
                        uvSclX = tt["scale"][0].get<float>();
                        uvSclY = tt["scale"][1].get<float>();
                    }
                    if (tt.contains("rotation") && tt["rotation"].is_number())
                        uvRot = tt["rotation"].get<float>();

                    const bool bHasTransform = (uvOffX != 0.0f || uvOffY != 0.0f ||
                                                uvSclX != 1.0f || uvSclY != 1.0f ||
                                                uvRot  != 0.0f);
                    if (bHasTransform)
                    {
                        const float cr = cosf(uvRot);
                        const float sr = sinf(uvRot);
                        for (auto& uvV : rawVertices)
                        {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            float u0 = uvV.texCoord.x * uvSclX;     // scale first
                            float v0 = uvV.texCoord.y * uvSclY;
                            uvV.texCoord.x =  cr * u0 + sr * v0 + uvOffX;  // then rotate + offset
                            uvV.texCoord.y = -sr * u0 + cr * v0 + uvOffY;
#else
                            float u0 = uvV.texCoord[0] * uvSclX;    // scale first
                            float v0 = uvV.texCoord[1] * uvSclY;
                            uvV.texCoord[0] =  cr * u0 + sr * v0 + uvOffX; // then rotate + offset
                            uvV.texCoord[1] = -sr * u0 + cr * v0 + uvOffY;
#endif
                        }

                        #if defined(_DEBUG_SCENEMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_INFO,
                                L"[SceneManager] KHR_texture_transform baked into UVs (mat=%d offset=%.3f,%.3f scale=%.3f,%.3f rot=%.3f)",
                                uvMatIdx, uvOffX, uvOffY, uvSclX, uvSclY, uvRot);
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
            size_t operator()(const VertexKey& key) const noexcept
            {
                // FNV-1a over raw struct bytes - dramatically fewer collisions than XOR chaining.
                const uint8_t* p   = reinterpret_cast<const uint8_t*>(&key);
                const uint8_t* end = p + sizeof(VertexKey);
                size_t hash = 14695981039346656037ULL;
                for (; p != end; ++p)
                {
                    hash ^= static_cast<size_t>(*p);
                    hash *= 1099511628211ULL;
                }
                return hash;
            }
        };

        std::unordered_map<VertexKey, uint32_t, VertexKeyHasher> uniqueVerts;
        uniqueVerts.reserve(rawVertices.size());
        model.m_modelInfo.vertices.reserve(rawVertices.size());
        model.m_modelInfo.indices.reserve(rawIndices.size());

        for (size_t ii = 0; ii < rawIndices.size(); ++ii)
        {
            const uint32_t idx = rawIndices[ii];        // Validated index.
            const Vertex& v = rawVertices[(size_t)idx]; // Safe vertex fetch.
            VertexKey key;                               // Vertex key for hashing.
            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                key.pos  = v.position;
                key.norm = v.normal;
                key.uv   = v.texCoord;
            #else
                key.pos  = { v.position[0], v.position[1], v.position[2] };
                key.norm = { v.normal[0], v.normal[1], v.normal[2] };
                key.uv   = { v.texCoord[0], v.texCoord[1] };
            #endif

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

        // Reverse winding order when the GLTF→DX coordinate flip changes handedness.
        // (Odd number of axis flips reverses triangle winding.)
        if (m_blenderConfig.fixWinding && BlenderImports::NeedsWindingFlip(m_blenderConfig.flipAxes))
            BlenderImports::FixWindingOrder(model.m_modelInfo.indices);

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

                #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    XMVECTOR p0 = XMLoadFloat3(&v0.position);
                    XMVECTOR p1 = XMLoadFloat3(&v1.position);
                    XMVECTOR p2 = XMLoadFloat3(&v2.position);
                #else
                    XMVECTOR p0 = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(v0.position));
                    XMVECTOR p1 = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(v1.position));
                    XMVECTOR p2 = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(v2.position));
                #endif

                #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    float du1 = v1.texCoord.x - v0.texCoord.x;
                    float dv1 = v1.texCoord.y - v0.texCoord.y;
                    float du2 = v2.texCoord.x - v0.texCoord.x;
                    float dv2 = v2.texCoord.y - v0.texCoord.y;
                #else
                    float du1 = v1.texCoord[0] - v0.texCoord[0];
                    float dv1 = v1.texCoord[1] - v0.texCoord[1];
                    float du2 = v2.texCoord[0] - v0.texCoord[0];
                    float dv2 = v2.texCoord[1] - v0.texCoord[1];
                #endif

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
                #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    XMVECTOR tan = XMLoadFloat3(&tangentAccum[i]);
                    tan = XMVector3Normalize(tan);
                    XMStoreFloat3(&model.m_modelInfo.vertices[i].tangent, tan);
                #else
                    XMVECTOR tan = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&tangentAccum[i]));
                    tan = XMVector3Normalize(tan);
                    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(model.m_modelInfo.vertices[i].tangent), tan);
                    // tangent[3] is the handedness sign used by the GLSL shader to compute bitangent.
                    // B = cross(N, T) * tangent.w - must be non-zero or bitangent is always zero.
                    model.m_modelInfo.vertices[i].tangent[3] = 1.0f;
                #endif
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
XMMATRIX SceneManager::GetNodeWorldMatrix(const json& node,
                                           const BlenderImports::ImportConfig& cfg)
{
    bool hasValidTransform = false;
    XMMATRIX S = XMMatrixIdentity();
    XMMATRIX R = XMMatrixIdentity();
    XMMATRIX T = XMMatrixIdentity();

    // === Full Matrix override (GLTF column-major → DX row-major = transpose on load)
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16)
    {
        XMFLOAT4X4 mtx{};
        for (int i = 0; i < 16; ++i)
        {
            if (!node["matrix"][i].is_number())
                return XMMatrixIdentity();
            ((float*)&mtx)[i] = node["matrix"][i].get<float>();
        }
        XMMATRIX loaded = XMLoadFloat4x4(&mtx);
        // Apply GLTF→DX coordinate conversion: F * M * F
        return BlenderImports::ConvertNodeMatrix(loaded, cfg.flipAxes);
    }

    // === Scale (unchanged by reflection)
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

    // === Rotation - convert quaternion for GLTF→DX handedness
    if (node.contains("rotation") && node["rotation"].is_array()) {
        const auto& r = node["rotation"];
        if (r.size() == 4 && r[0].is_number() && r[1].is_number() && r[2].is_number() && r[3].is_number()) {
            XMFLOAT4 rawQ = { r[0].get<float>(), r[1].get<float>(),
                              r[2].get<float>(), r[3].get<float>() };
            XMFLOAT4 dxQ  = BlenderImports::ConvertQuat(rawQ, cfg.flipAxes);
            XMVECTOR quat  = XMVectorSet(dxQ.x, dxQ.y, dxQ.z, dxQ.w);
            R = XMMatrixRotationQuaternion(quat);
            hasValidTransform = true;
        }
    }

    // === Translation - negate Z (or other flagged axes) for GLTF→DX
    if (node.contains("translation") && node["translation"].is_array()) {
        const auto& t = node["translation"];
        if (t.size() == 3 && t[0].is_number() && t[1].is_number() && t[2].is_number()) {
            XMFLOAT3 rawT = { t[0].get<float>(), t[1].get<float>(), t[2].get<float>() };
            XMFLOAT3 dxT  = BlenderImports::ConvertTranslation(rawT, cfg.flipAxes);
            T = XMMatrixTranslation(dxT.x, dxT.y, dxT.z);
            hasValidTransform = true;

            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"[SceneManager] Translation GLTF=(%.3f,%.3f,%.3f) → DX=(%.3f,%.3f,%.3f)",
                    rawT.x, rawT.y, rawT.z, dxT.x, dxT.y, dxT.z);
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

    #if defined(_DEBUG_SCENEMANAGER_)
    {
        XMFLOAT4X4 dbgMatrix;
        XMStoreFloat4x4(&dbgMatrix, finalMatrix);
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[SceneManager] Node TRS(DX) -> Translation=(%.3f, %.3f, %.3f), Scale=(%.3f, %.3f, %.3f)",
            dbgMatrix._41, dbgMatrix._42, dbgMatrix._43,
            dbgMatrix._11, dbgMatrix._22, dbgMatrix._33);
    }
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

        // Always log: index, name, and key PBR properties on one line
        std::string matName = mat.value("name", "(unnamed)");
        float r=1,g=1,b=1,a=1, metallic=0, roughness=0.5f;
        bool  hasDiffuseTex=false, hasNormalTex=false, hasOrmTex=false, hasAoTex=false;
        std::string alphaMode = mat.value("alphaMode", "OPAQUE");

        if (mat.contains("pbrMetallicRoughness"))
        {
            const auto& pbr = mat["pbrMetallicRoughness"];
            if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array() &&
                pbr["baseColorFactor"].size() >= 4) {
                r = pbr["baseColorFactor"][0]; g = pbr["baseColorFactor"][1];
                b = pbr["baseColorFactor"][2]; a = pbr["baseColorFactor"][3];
            }
            if (pbr.contains("metallicFactor"))   metallic  = pbr["metallicFactor"].get<float>();
            if (pbr.contains("roughnessFactor"))  roughness = pbr["roughnessFactor"].get<float>();
            hasDiffuseTex = pbr.contains("baseColorTexture");
            hasOrmTex     = pbr.contains("metallicRoughnessTexture");
        }
        hasNormalTex = mat.contains("normalTexture");
        hasAoTex     = mat.contains("occlusionTexture");

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[SceneManager] Material[%d] \"%hs\"  Kd=(%.2f,%.2f,%.2f,%.2f)  M=%.2f R=%.2f  "
                L"alpha=%hs  tex=[diff:%d norm:%d orm:%d ao:%d]",
                (int)i, matName.c_str(), r, g, b, a, metallic, roughness,
                alphaMode.c_str(),
                (int)hasDiffuseTex, (int)hasNormalTex, (int)hasOrmTex, (int)hasAoTex);
        #endif
    }

    return true;
}

// --------------------------------------------------------------------------------------------------
// Loads a GLTF image entry as a Texture: tries URI (external file) first, then falls back to the
// embedded GLB binary buffer via bufferView.  Returns nullptr if both paths fail.
// --------------------------------------------------------------------------------------------------
std::shared_ptr<Texture> SceneManager::LoadGLTFImage(const json& imageEntry, const json& doc)
{
    std::string uri = imageEntry.value("uri", "");
    if (!uri.empty())
    {
        std::wstring wuri = sysUtils.StripQuotes(sysUtils.ToWString(uri));
        std::filesystem::path fullTexPath = AssetsDir / wuri;

        #if defined(_DEBUG_SCENEMANAGER_)
            // Log the full resolved path so the user can verify it is being constructed correctly.
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] LoadGLTFImage: URI='%hs' -> path='%ls' | exists=%s",
                uri.c_str(), fullTexPath.wstring().c_str(),
                std::filesystem::exists(fullTexPath) ? L"YES" : L"NO");
        #endif

        auto tex = std::make_shared<Texture>();
        if (tex->LoadFromFile(fullTexPath))
            return tex;

        #if defined(_DEBUG_SCENEMANAGER_)
            // Log with a clear failure reason so we know whether the path was wrong or the
            // device/WIC pipeline failed.  file-exists is re-checked here to distinguish
            // "path mismatch" from "LoadFromFile internal failure".
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[SceneManager] LoadGLTFImage: LoadFromFile FAILED for '%ls' (file on disk: %s) - model will use fallback",
                fullTexPath.wstring().c_str(),
                std::filesystem::exists(fullTexPath) ? L"YES - decode/upload error" : L"NO - bad path");
        #endif

        return nullptr;
    }

    // Embedded GLB buffer: resolve bufferView → byte range → decode in-memory
    if (!imageEntry.contains("bufferView") || gltfBinaryData.empty())
        return nullptr;
    if (!doc.contains("bufferViews"))
        return nullptr;

    int bvIdx = imageEntry.value("bufferView", -1);
    const auto& bufferViews = doc["bufferViews"];
    if (bvIdx < 0 || bvIdx >= (int)bufferViews.size())
        return nullptr;

    const auto& bv   = bufferViews[bvIdx];
    size_t byteOffset = static_cast<size_t>(bv.value("byteOffset", 0));
    size_t byteLength = static_cast<size_t>(bv.value("byteLength", 0));

    if (byteLength == 0 || byteOffset + byteLength > gltfBinaryData.size())
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[SceneManager] LoadGLTFImage: bufferView %d out of range (offset=%zu len=%zu bufSize=%zu)",
                bvIdx, byteOffset, byteLength, gltfBinaryData.size());
        #endif
        return nullptr;
    }

    auto tex = std::make_shared<Texture>();
    if (tex->LoadFromMemory(gltfBinaryData.data() + byteOffset, byteLength))
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] LoadGLTFImage: loaded embedded image from bufferView %d (%zu bytes)", bvIdx, byteLength);
        #endif
        return tex;
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_WARNING,
            L"[SceneManager] LoadGLTFImage: failed to decode embedded image from bufferView %d", bvIdx);
    #endif
    return nullptr;
}

// --------------------------------------------------------------------------------------------------
void SceneManager::BindGLTFMaterialTexturesToModel(int materialIndex, ModelInfo& info, Model& model, const json& doc)
{
    // "materials" is required; "textures" and "images" are optional - materials may use
    // only baseColorFactor with no image textures, in which case the solid-colour fallback
    // at the end of this function still needs to run.
    if (!doc.contains("materials"))
        return;

    const auto& materials  = doc["materials"];
    const bool hasTextures = doc.contains("textures") && doc["textures"].is_array();
    const bool hasImages   = doc.contains("images")   && doc["images"].is_array();
    const auto& textures   = hasTextures ? doc["textures"] : json::array();
    const auto& images     = hasImages   ? doc["images"]   : json::array();

    if (materialIndex < 0 || materialIndex >= (int)materials.size())
        return;

    // Lock all texture/material container mutations for this model instance.
    // This prevents cross-thread corruption between loader and renderer.
    std::string lockName = "model_texture_update_" + std::to_string(info.ID);
    ThreadLockHelper lock(threadManager, lockName.c_str(), 5000);

    if (!lock.IsLocked())
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Could not acquire lock for material bind on model ID %d (material %d) - skipping.", info.ID, materialIndex);
        #endif
        return;
    }

    const auto& mat = materials[materialIndex];
    Material newMat;
    newMat.name = mat.contains("name") ? mat["name"].get<std::string>() : "Material" + std::to_string(materialIndex);

    // Apply all GLTF 2.0 PBR properties via BlenderImports (handles Blender 3.x–5.x).
    BlenderImports::ApplyPBRMaterial(newMat, mat, m_blenderConfig);

    bool hasDiffuseTexture = false;

    if (mat.contains("pbrMetallicRoughness"))
    {
        const auto& pbr = mat["pbrMetallicRoughness"];

        // === Base Colour Texture (albedo / t0) ===
        if (pbr.contains("baseColorTexture"))
        {
            int texIndex = pbr["baseColorTexture"].value("index", -1);
            if (texIndex >= 0 && texIndex < (int)textures.size())
            {
                // --- UV settings: GLTF sampler wrap modes (wrapS/wrapT) ---
                // GLTF constants: 10497=REPEAT (default), 33071=CLAMP_TO_EDGE,
                // 33648=MIRRORED_REPEAT.  Normalised into ModelInfo as
                // 0=REPEAT / 1=CLAMP / 2=MIRROR and applied to the diffuse
                // sampler by each renderer (DX11/DX12 sampler desc, OpenGL
                // glTexParameteri in RefreshOpenGLTextures, Vulkan wrap-aware
                // sampler in the texture descriptor write below).
                {
                    auto mapWrap = [](int gltfWrap) -> int {
                        if (gltfWrap == 33071) return 1;    // CLAMP_TO_EDGE
                        if (gltfWrap == 33648) return 2;    // MIRRORED_REPEAT
                        return 0;                           // REPEAT (10497 / unknown)
                    };
                    info.uvWrapU = 0;
                    info.uvWrapV = 0;
                    int samplerIdx = textures[texIndex].value("sampler", -1);
                    if (samplerIdx >= 0 && doc.contains("samplers") &&
                        doc["samplers"].is_array() && samplerIdx < (int)doc["samplers"].size())
                    {
                        const auto& smp = doc["samplers"][samplerIdx];
                        info.uvWrapU = mapWrap(smp.value("wrapS", 10497));
                        info.uvWrapV = mapWrap(smp.value("wrapT", 10497));

                        #if defined(_DEBUG_SCENEMANAGER_)
                            if (info.uvWrapU != 0 || info.uvWrapV != 0)
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"[SceneManager] Model[%d] material[%d] UV wrap modes: U=%d V=%d (0=repeat 1=clamp 2=mirror)",
                                    info.ID, materialIndex, info.uvWrapU, info.uvWrapV);
                        #endif
                    }
                }

                int imgIndex = textures[texIndex].value("source", -1);
                if (imgIndex >= 0 && imgIndex < (int)images.size())
                {
                    const auto& imgEntry = images[imgIndex];
                    std::string uri = imgEntry.value("uri", "");

                    // Heuristic: some Blender exports mis-assign a normal map as baseColorTexture;
                    // only applicable when a URI name is available (embedded images are never mis-named).
                    std::string uriLower = uri;
                    std::transform(uriLower.begin(), uriLower.end(), uriLower.begin(),
                        [](unsigned char c) { return static_cast<char>(::tolower(c)); });
                    bool looksLikeNormal  = uriLower.find("normal") != std::string::npos;
                    bool hasExplicitNormal = mat.contains("normalTexture");

                    auto tex = LoadGLTFImage(imgEntry, doc);
                    if (tex)
                    {
                        info.textures.push_back(tex);

                        if (looksLikeNormal && !hasExplicitNormal)
                        {
                            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                // Register the SRV so DX11/DX12 SetupModelForRendering can find
                                // this Texture via findTex() and upload it to the GPU correctly.
                                info.normalMapSRVs.push_back(tex->GetSRV());
                            #endif
                            newMat.normalMap     = tex;
                            newMat.normalMapPath = uri;
                            hasDiffuseTexture    = false;

                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(LogLevel::LOG_WARNING,
                                    L"[SceneManager] Model[%d] material[%d] baseColorTexture re-routed as NormalMap (name heuristic)",
                                    info.ID, materialIndex);
                            #endif
                        }
                        else
                        {
                            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                // Register the SRV so DX11/DX12 SetupModelForRendering can find
                                // this Texture via findTex() and upload it to the GPU correctly.
                                // Without this, DX12 sees textureSRVs as empty and loads the
                                // brick fallback instead of the model's actual diffuse image.
                                info.textureSRVs.push_back(tex->GetSRV());
                            #endif
                            newMat.diffuseTexture = tex;
                            newMat.diffuseMapPath = uri.empty() ? "(embedded)" : uri;
                            hasDiffuseTexture     = true;
                            info.useDiffuseMap    = true;   // real texture: shader samples t0 and multiplies by Kd

                            #if defined(_DEBUG_SCENEMANAGER_)
                                debug.logDebugMessage(LogLevel::LOG_INFO,
                                    L"[SceneManager] Model[%d] material[%d] -> Albedo (%hs)",
                                    info.ID, materialIndex, uri.empty() ? "embedded" : uri.c_str());
                            #endif
                        }
                    }
                    #if defined(_DEBUG_SCENEMANAGER_)
                    else
                    {
                        // LoadGLTFImage returned null: the URI was found in the GLTF but the
                        // texture could not be loaded.  Check the LoadFromFile output above for
                        // the specific reason (bad path, missing device, WIC failure, etc.).
                        debug.logDebugMessage(LogLevel::LOG_ERROR,
                            L"[SceneManager] Model[%d] material[%d]: LoadGLTFImage returned NULL for "
                            L"baseColorTexture (imgIndex=%d uri='%hs') - solid-colour fallback will be used",
                            info.ID, materialIndex, imgIndex, uri.c_str());
                    }
                    #endif
                }
            }
        }

        // === Metallic-Roughness Texture (GLTF 2.0 ORM: G=roughness, B=metallic) ===
        if (pbr.contains("metallicRoughnessTexture"))
        {
            int texIndex = pbr["metallicRoughnessTexture"].value("index", -1);
            if (texIndex >= 0 && texIndex < (int)textures.size())
            {
                int imgIndex = textures[texIndex].value("source", -1);
                if (imgIndex >= 0 && imgIndex < (int)images.size())
                {
                    auto tex = LoadGLTFImage(images[imgIndex], doc);
                    if (tex)
                    {
                        info.textures.push_back(tex);
                        // Both slots share the same texture; shader samples G for roughness, B for metallic
                        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            info.metallicMap     = tex;
                            info.roughnessMap    = tex;
                            info.metallicMapSRV  = tex->GetSRV();
                            info.roughnessMapSRV = tex->GetSRV();
                        #elif defined(__USE_VULKAN__)
                            // Vulkan: texture view is resolved from newMat.metallicMap in
                            // the BindGLTFMaterialTexturesToModel Vulkan section below.
                            // useMetallicMap/useRoughnessMap drive matData.useORM in the material UBO.
                        #endif
                        info.useMetallicMap  = true;
                        info.useRoughnessMap = true;
                        newMat.metallicMap   = tex;
                        newMat.roughnessMap  = tex;

                        #if defined(_DEBUG_SCENEMANAGER_)
                            std::string uri2 = images[imgIndex].value("uri", "");
                            debug.logDebugMessage(LogLevel::LOG_INFO,
                                L"[SceneManager] Model[%d] material[%d] -> MetallicRoughness (%hs)",
                                info.ID, materialIndex, uri2.empty() ? "embedded" : uri2.c_str());
                        #endif
                    }
                    #if defined(_DEBUG_SCENEMANAGER_)
                    else
                    {
                        // MetallicRoughness texture found in GLTF but failed to load.
                        std::string uri2 = images[imgIndex].value("uri", "");
                        debug.logDebugMessage(LogLevel::LOG_ERROR,
                            L"[SceneManager] Model[%d] material[%d]: MetallicRoughness texture load FAILED "
                            L"(imgIndex=%d uri='%hs') - PBR maps will be missing",
                            info.ID, materialIndex, imgIndex, uri2.empty() ? "embedded" : uri2.c_str());
                    }
                    #endif
                }
            }
        }
    }

    // === Fallback: Create a solid-colour 1x1 texture from the material's baseColorFactor.
    // ApplyPBRMaterial() already loaded baseColorFactor into newMat.Kd / newMat.dissolve,
    // so materials with no texture (plain Blender colours) render with the correct colour.
    if (!hasDiffuseTexture)
    {
        float alpha = newMat.dissolve;
        XMFLOAT3 Kd = newMat.Kd;

        // Frosted-glass treatment: BLEND materials that are nearly invisible get a
        // minimum opacity and a slight white tint so they read as tinted glass rather
        // than vanishing entirely.  Threshold 0.65 keeps intentionally opaque surfaces
        // (dissolve >= 0.65) unchanged.
        if (newMat.alphaMode == "BLEND" && alpha < 0.65f)
        {
            alpha = std::max(alpha, 0.50f);         // never more than 50% transparent
            const float frost = 0.35f;              // mix 35% white into the colour
            Kd.x = Kd.x * (1.0f - frost) + frost;
            Kd.y = Kd.y * (1.0f - frost) + frost;
            Kd.z = Kd.z * (1.0f - frost) + frost;
        }

        auto fallbackTex = std::make_shared<Texture>();
        // Use white 1x1 -- actual colour comes via Kd in the material constant buffer.
        // (texture_colour x Kd would double-apply the diffuse and darken solid-colour materials)
        bool solidOk = fallbackTex->CreateSolidColorTexture(1, 1, XMFLOAT4(1.0f, 1.0f, 1.0f, alpha));

        info.textures.push_back(fallbackTex);
        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            info.textureSRVs.push_back(fallbackTex->GetSRV());
        #endif

        newMat.diffuseTexture = fallbackTex;
        newMat.diffuseMapPath = "SOLID_COLOR";
        info.useDiffuseMap    = false;          // solid-colour: shader uses Kd directly, not texture sample

        #if defined(_DEBUG_SCENEMANAGER_)
            if (!solidOk)
            {
                // CreateSolidColorTexture failed - the D3D device is probably not yet available.
                // textureSRVs[0] will be null; SetupModelForRendering will call LoadFallbackTexture.
                debug.logDebugMessage(LogLevel::LOG_ERROR,
                    L"[SceneManager] Model[%d] material[%d] -> CreateSolidColorTexture FAILED "
                    L"(device unavailable?) - brick fallback will be used instead",
                    info.ID, materialIndex);
            }
            else
            {
                debug.logDebugMessage(LogLevel::LOG_WARNING,
                    L"[SceneManager] Model[%d] material[%d] -> Solid colour fallback (%.2f, %.2f, %.2f, a=%.2f) alphaMode=%hs.",
                    info.ID, materialIndex, Kd.x, Kd.y, Kd.z, alpha, newMat.alphaMode.c_str());
            }
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
                auto tex = LoadGLTFImage(images[imgIndex], doc);
                if (tex)
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    info.textures.push_back(tex);
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        info.normalMapSRVs.push_back(tex->GetSRV());
                    #endif
                    newMat.normalMap     = tex;
                    newMat.normalMapPath = uri.empty() ? "(embedded)" : uri;

                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] Model[%d] material[%d] -> Normal Map (%hs)",
                            info.ID, materialIndex, uri.empty() ? "embedded" : uri.c_str());
                    #endif
                }
                #if defined(_DEBUG_SCENEMANAGER_)
                else
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    debug.logDebugMessage(LogLevel::LOG_ERROR,
                        L"[SceneManager] Model[%d] material[%d]: Normal Map load FAILED "
                        L"(imgIndex=%d uri='%hs') - normals will be flat",
                        info.ID, materialIndex, imgIndex, uri.empty() ? "embedded" : uri.c_str());
                }
                #endif
            }
        }
    }

    // === Ambient Occlusion Texture (optional, t4) ===
    if (mat.contains("occlusionTexture"))
    {
        int texIndex = mat["occlusionTexture"].value("index", -1);
        if (texIndex >= 0 && texIndex < (int)textures.size())
        {
            int imgIndex = textures[texIndex].value("source", -1);
            if (imgIndex >= 0 && imgIndex < (int)images.size())
            {
                auto tex = LoadGLTFImage(images[imgIndex], doc);
                if (tex)
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    info.textures.push_back(tex);
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        info.aoMap    = tex;
                        info.aoMapSRV = tex->GetSRV();
                    #elif defined(__USE_VULKAN__)
                        // Vulkan: texture view is resolved from newMat.aoMap in the
                        // BindGLTFMaterialTexturesToModel Vulkan section below.
                        // useAOMap drives matData.useAO in the material UBO.
                    #endif
                    info.useAOMap = true;
                    newMat.aoMap  = tex;

                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] Model[%d] material[%d] -> AO map (%hs)",
                            info.ID, materialIndex, uri.empty() ? "embedded" : uri.c_str());
                    #endif
                }
                #if defined(_DEBUG_SCENEMANAGER_)
                else
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    debug.logDebugMessage(LogLevel::LOG_ERROR,
                        L"[SceneManager] Model[%d] material[%d]: AO Map load FAILED "
                        L"(imgIndex=%d uri='%hs') - AO will be disabled",
                        info.ID, materialIndex, imgIndex, uri.empty() ? "embedded" : uri.c_str());
                }
                #endif
            }
        }
    }

    // === Emissive Texture (optional, t7 / Vulkan set=1 binding=5) ===
    // GLTF 2.0 spec: emissiveTexture is a top-level material key (not inside pbrMetallicRoughness).
    // The emissive colour output is: emissiveTexture.rgb * emissiveFactor * emissiveStrength.
    // When no texture is present the shader falls back to vec3(1,1,1), making emissiveFactor
    // the sole colour - so a white [1,1,1] factor with no texture produces solid white emission.
    if (mat.contains("emissiveTexture"))
    {
        int texIndex = mat["emissiveTexture"].value("index", -1);
        if (texIndex >= 0 && texIndex < (int)textures.size())
        {
            int imgIndex = textures[texIndex].value("source", -1);
            if (imgIndex >= 0 && imgIndex < (int)images.size())
            {
                auto tex = LoadGLTFImage(images[imgIndex], doc);
                if (tex)
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    info.textures.push_back(tex);
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        info.emissiveMapTexture = tex;
                        info.emissiveMapSRV     = tex->GetSRV();
                    #endif
                    info.useEmissiveMap    = true;
                    newMat.emissiveMap     = tex;
                    newMat.emissiveMapPath = uri.empty() ? "(embedded)" : uri;

                    debug.logDebugMessage(LogLevel::LOG_INFO,
                        L"[SceneManager] Model[%d] material[%d] -> Emissive map (%hs) "
                        L"factor=(%.2f,%.2f,%.2f) strength=%.2f",
                        info.ID, materialIndex,
                        uri.empty() ? "embedded" : uri.c_str(),
                        newMat.emissiveFactor.x, newMat.emissiveFactor.y, newMat.emissiveFactor.z,
                        newMat.emissiveStrength);
                }
                #if defined(_DEBUG_SCENEMANAGER_)
                else
                {
                    std::string uri = images[imgIndex].value("uri", "");
                    debug.logDebugMessage(LogLevel::LOG_ERROR,
                        L"[SceneManager] Model[%d] material[%d]: Emissive map load FAILED "
                        L"(imgIndex=%d uri='%hs') - flat emissiveFactor will be used",
                        info.ID, materialIndex, imgIndex, uri.empty() ? "embedded" : uri.c_str());
                }
                #endif
            }
        }
    }
    #if defined(_DEBUG_SCENEMANAGER_)
    else
    {
        // Warn when the material declares a non-black emissiveFactor but exports no texture.
        // This is the Blender export bug that causes solid-white emission: the emission colour
        // input of Principled BSDF must be connected to an image texture node for Blender to
        // write an emissiveTexture entry; a plain colour connection only writes emissiveFactor.
        const auto& ef = newMat.emissiveFactor;
        if (ef.x > 0.001f || ef.y > 0.001f || ef.z > 0.001f)
        {
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[SceneManager] Model[%d] material[%d] \"%hs\": emissiveFactor=(%.2f,%.2f,%.2f)x%.2f "
                L"but NO emissiveTexture in GLTF -- shader will emit solid colour. "
                L"In Blender: connect a texture node to the Emission Color socket of Principled BSDF before exporting.",
                info.ID, materialIndex, newMat.name.c_str(),
                ef.x, ef.y, ef.z, newMat.emissiveStrength);
        }
    }
    #endif

    // PBR scalars (Kd, Metallic, Roughness, emissive, alpha, extensions) were
    // already applied by BlenderImports::ApplyPBRMaterial() above the texture block.

    info.materials.push_back(newMat.name);
    model.m_materials[newMat.name] = newMat;

    #if defined(__USE_VULKAN__)
        // ---- Vulkan: Upload material UBO and update texture descriptor set ----
        // The model's materialUniformBuffer was created in SetupModelForRendering with defaults.
        // Now overwrite it with the actual parsed material values.
        if (info.materialUniformBufferMapped)
        {
            struct VKMatUBO {
                float Kd[3];       float metallic;
                float Ka[3];       float roughness;
                float emissive[3]; float emissiveStrength;
                float normalScale; float useNormal;  float useORM;        float useAO;
                float useDiffuseMap; float useGlossMap; float useEmissiveMap; float _pad;
            };
            VKMatUBO matData{};
            matData.Kd[0]            = newMat.Kd.x;
            matData.Kd[1]            = newMat.Kd.y;
            matData.Kd[2]            = newMat.Kd.z;
            matData.metallic         = newMat.Metallic;
            matData.Ka[0]            = newMat.Ka.x;
            matData.Ka[1]            = newMat.Ka.y;
            matData.Ka[2]            = newMat.Ka.z;
            matData.roughness        = newMat.Roughness;
            matData.emissive[0]      = newMat.emissiveFactor.x;
            matData.emissive[1]      = newMat.emissiveFactor.y;
            matData.emissive[2]      = newMat.emissiveFactor.z;
            matData.emissiveStrength = newMat.emissiveStrength;
            matData.normalScale      = newMat.normalScale > 0.0f ? newMat.normalScale : 1.0f;
            matData.useNormal        = (newMat.normalMap  != nullptr) ? 1.0f : 0.0f;
            matData.useORM           = (info.useMetallicMap || info.useRoughnessMap) ? 1.0f : 0.0f;
            matData.useAO            = info.useAOMap ? 1.0f : 0.0f;
            matData.useDiffuseMap    = info.useDiffuseMap  ? 1.0f : 0.0f;
            matData.useGlossMap      = info.useGlossMap    ? 1.0f : 0.0f;
            matData.useEmissiveMap   = info.useEmissiveMap ? 1.0f : 0.0f;
            std::memcpy(info.materialUniformBufferMapped, &matData, sizeof(matData));

            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[SceneManager] Model[%d] material \"%hs\" UBO uploaded: "
                L"Kd=(%.2f,%.2f,%.2f) M=%.2f R=%.2f emit=(%.2f,%.2f,%.2f)x%.2f "
                L"useNorm=%d useORM=%d useAO=%d useDiff=%d useGloss=%d useEmit=%d",
                info.ID, newMat.name.c_str(),
                matData.Kd[0], matData.Kd[1], matData.Kd[2],
                matData.metallic, matData.roughness,
                matData.emissive[0], matData.emissive[1], matData.emissive[2], matData.emissiveStrength,
                (int)matData.useNormal, (int)matData.useORM, (int)matData.useAO,
                (int)matData.useDiffuseMap, (int)matData.useGlossMap, (int)matData.useEmissiveMap);
        }

        // Update the per-model texture descriptor set (set=1) with actual textures.
        // textureDescriptorSet was allocated in SetupModelForRendering with fallback images.
        if (info.textureDescriptorSet != VK_NULL_HANDLE)
        {
            auto vkrPtr = std::dynamic_pointer_cast<VulkanRenderer>(renderer);
            if (vkrPtr)
            {
                VkDevice  device  = vkrPtr->GetVkDevice();
                // UV settings: honour the importer's wrap modes (GLTF sampler
                // wrapS/wrapT) - falls back to the default REPEAT sampler.
                VkSampler sampler = vkrPtr->GetSamplerForWrap(info.uvWrapU, info.uvWrapV);

                // Resolve views for each slot - fall back to renderer defaults when absent.
                VkImageView diffuseView = newMat.diffuseTexture && newMat.diffuseTexture->GetImageView() != VK_NULL_HANDLE
                                        ? newMat.diffuseTexture->GetImageView()
                                        : vkrPtr->GetDefaultDiffuseView();
                VkImageView normalView  = newMat.normalMap     && newMat.normalMap->GetImageView()     != VK_NULL_HANDLE
                                        ? newMat.normalMap->GetImageView()
                                        : vkrPtr->GetDefaultNormalView();
                VkImageView ormView     = newMat.metallicMap   && newMat.metallicMap->GetImageView()   != VK_NULL_HANDLE
                                        ? newMat.metallicMap->GetImageView()
                                        : vkrPtr->GetDefaultOrmView();
                VkImageView aoView      = newMat.aoMap         && newMat.aoMap->GetImageView()         != VK_NULL_HANDLE
                                        ? newMat.aoMap->GetImageView()
                                        : vkrPtr->GetDefaultAoView();
                // Gloss/emissive: fall back to white diffuse texture; shader skips sampling when use*Map==0.
                VkImageView glossView    = newMat.glossMap    && newMat.glossMap->GetImageView()    != VK_NULL_HANDLE
                                        ? newMat.glossMap->GetImageView()
                                        : vkrPtr->GetDefaultDiffuseView();
                VkImageView emissiveView = newMat.emissiveMap && newMat.emissiveMap->GetImageView() != VK_NULL_HANDLE
                                        ? newMat.emissiveMap->GetImageView()
                                        : vkrPtr->GetDefaultDiffuseView();

                VkImageView views[6] = { diffuseView, normalView, ormView, aoView, glossView, emissiveView };
                std::array<VkWriteDescriptorSet, 6> writes{};
                std::array<VkDescriptorImageInfo,  6> imgInfos{};
                for (uint32_t b = 0; b < 6; ++b) {
                    imgInfos[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfos[b].imageView   = views[b];
                    imgInfos[b].sampler     = sampler;
                    writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[b].dstSet          = info.textureDescriptorSet;
                    writes[b].dstBinding      = b;
                    writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[b].descriptorCount = 1;
                    writes[b].pImageInfo      = &imgInfos[b];
                }
                vkUpdateDescriptorSets(device, 6, writes.data(), 0, nullptr);
            }
        }
    #endif
}

// ============================================================================
// UploadFBXMaterialToVulkanModel -- Vulkan-only post-setup material upload.
// SetupModelForRendering creates the per-model material UBO and texture
// descriptor set with engine defaults; this pushes the real FBX material
// values (built by FBXImporter::BuildMaterial) into those GPU resources.
// Mirrors the Vulkan section of BindGLTFMaterialTexturesToModel so FBX models
// receive the same material/texture treatment as GLTF models.
// No-op on non-Vulkan builds.
// ============================================================================
void SceneManager::UploadFBXMaterialToVulkanModel(const Material& mat, ModelInfo& info)
{
    #if defined(__USE_VULKAN__)
        // ---- Material UBO: overwrite the defaults written by SetupModelForRendering ----
        if (info.materialUniformBufferMapped)
        {
            // Layout must match the MatUBO struct in SetupModelForRendering and the shader.
            struct VKMatUBO {
                float Kd[3];       float metallic;
                float Ka[3];       float roughness;
                float emissive[3]; float emissiveStrength;
                float normalScale; float useNormal;  float useORM;        float useAO;
                float useDiffuseMap; float useGlossMap; float useEmissiveMap; float _pad;
            };
            VKMatUBO matData{};
            matData.Kd[0]            = mat.Kd.x;
            matData.Kd[1]            = mat.Kd.y;
            matData.Kd[2]            = mat.Kd.z;
            matData.metallic         = mat.Metallic;
            matData.Ka[0]            = mat.Ka.x;
            matData.Ka[1]            = mat.Ka.y;
            matData.Ka[2]            = mat.Ka.z;
            matData.roughness        = mat.Roughness;
            matData.emissive[0]      = mat.emissiveFactor.x;
            matData.emissive[1]      = mat.emissiveFactor.y;
            matData.emissive[2]      = mat.emissiveFactor.z;
            matData.emissiveStrength = mat.emissiveStrength;
            matData.normalScale      = mat.normalScale > 0.0f ? mat.normalScale : 1.0f;
            matData.useNormal        = (mat.normalMap != nullptr) ? 1.0f : 0.0f;
            matData.useORM           = (info.useMetallicMap || info.useRoughnessMap) ? 1.0f : 0.0f;
            matData.useAO            = info.useAOMap      ? 1.0f : 0.0f;
            matData.useDiffuseMap    = info.useDiffuseMap ? 1.0f : 0.0f;
            matData.useGlossMap      = info.useGlossMap    ? 1.0f : 0.0f;   // GLTF may have gloss; FBX defaults false
            matData.useEmissiveMap   = info.useEmissiveMap ? 1.0f : 0.0f;   // GLTF may have emissive; FBX defaults false
            std::memcpy(info.materialUniformBufferMapped, &matData, sizeof(matData));

            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[SceneManager] FBX model[%d] material \"%hs\" UBO uploaded: "
                L"Kd=(%.2f,%.2f,%.2f) M=%.2f R=%.2f useNorm=%d useORM=%d useAO=%d useDiff=%d",
                info.ID, mat.name.c_str(),
                matData.Kd[0], matData.Kd[1], matData.Kd[2],
                matData.metallic, matData.roughness,
                (int)matData.useNormal, (int)matData.useORM, (int)matData.useAO,
                (int)matData.useDiffuseMap);
        }

        // ---- Texture descriptor set (set=1): replace the fallback views ----
        if (info.textureDescriptorSet != VK_NULL_HANDLE)
        {
            auto vkrPtr = std::dynamic_pointer_cast<VulkanRenderer>(renderer);
            if (vkrPtr)
            {
                VkDevice  device  = vkrPtr->GetVkDevice();
                // UV settings: honour the importer's wrap modes (FBX WrapModeU/V)
                // - falls back to the default REPEAT sampler.
                VkSampler sampler = vkrPtr->GetSamplerForWrap(info.uvWrapU, info.uvWrapV);

                // Resolve views for each slot -- fall back to renderer defaults when absent.
                VkImageView diffuseView = mat.diffuseTexture && mat.diffuseTexture->GetImageView() != VK_NULL_HANDLE
                                        ? mat.diffuseTexture->GetImageView()
                                        : vkrPtr->GetDefaultDiffuseView();
                VkImageView normalView  = mat.normalMap     && mat.normalMap->GetImageView()     != VK_NULL_HANDLE
                                        ? mat.normalMap->GetImageView()
                                        : vkrPtr->GetDefaultNormalView();
                VkImageView ormView     = mat.metallicMap   && mat.metallicMap->GetImageView()   != VK_NULL_HANDLE
                                        ? mat.metallicMap->GetImageView()
                                        : vkrPtr->GetDefaultOrmView();
                VkImageView aoView      = mat.aoMap         && mat.aoMap->GetImageView()         != VK_NULL_HANDLE
                                        ? mat.aoMap->GetImageView()
                                        : vkrPtr->GetDefaultAoView();
                // Gloss/emissive: use loaded views when present; shader skips sampling when use*Map==0.
                VkImageView glossView    = mat.glossMap    && mat.glossMap->GetImageView()    != VK_NULL_HANDLE
                                        ? mat.glossMap->GetImageView()
                                        : vkrPtr->GetDefaultDiffuseView();
                VkImageView emissiveView = mat.emissiveMap && mat.emissiveMap->GetImageView() != VK_NULL_HANDLE
                                        ? mat.emissiveMap->GetImageView()
                                        : vkrPtr->GetDefaultDiffuseView();

                VkImageView views[6] = { diffuseView, normalView, ormView, aoView, glossView, emissiveView };
                std::array<VkWriteDescriptorSet, 6> writes{};
                std::array<VkDescriptorImageInfo,  6> imgInfos{};
                for (uint32_t b = 0; b < 6; ++b) {
                    imgInfos[b].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfos[b].imageView   = views[b];
                    imgInfos[b].sampler     = sampler;
                    writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[b].dstSet          = info.textureDescriptorSet;
                    writes[b].dstBinding      = b;
                    writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[b].descriptorCount = 1;
                    writes[b].pImageInfo      = &imgInfos[b];
                }
                vkUpdateDescriptorSets(device, 6, writes.data(), 0, nullptr);
            }
        }
    #else
        // Non-Vulkan builds: material data flows through m_materials / SRV slots instead.
        (void)mat;
        (void)info;
    #endif
}

// ============================================================================
// ParseFBXCameras -- extract all cameras from the loaded FBX scene,
// convert positions/targets to engine LH Y-up space, store in m_fbxCameras,
// and immediately apply the first camera to myRenderer->myCamera.
//
// Camera position:  comes from the FBXModel whose attributeID == camera.id.
//   BuildTransformMatrix() returns the full TRS in FBX RH space; we extract
//   the translation column and apply the same coord flip used for model nodes.
// Camera target:    interestPos stored in the FBXCamera struct (already in FBX
//   object space - apply the same flip).
// FOV / near / far: used directly from FBXCamera.fovY / nearPlane / farPlane.
// ============================================================================
void SceneManager::ParseFBXCameras(const FBXScene& fbx)
{
    m_fbxCameras.clear();

    for (const auto& fc : fbx.cameras)
    {
        ParsedFBXCamera pc;
        pc.name      = std::wstring(fc.name.begin(), fc.name.end());
        pc.fovYDeg   = fc.fovY;
        pc.nearPlane = (fc.nearPlane > 0.001f) ? fc.nearPlane : 0.1f;
        pc.farPlane  = (fc.farPlane  > pc.nearPlane + 1.0f) ? fc.farPlane : 10000.0f;

        // Look-at (interest) position: apply same coord flip as model translations
        if (fbx.upAxis == 2)
            pc.target = XMFLOAT3(fc.interestPos.x, fc.interestPos.z, -fc.interestPos.y);
        else
            pc.target = XMFLOAT3(fc.interestPos.x, fc.interestPos.y, -fc.interestPos.z);

        // Up vector: apply coord flip so it points in engine Y-up
        if (fbx.upAxis == 2)
            pc.up = XMFLOAT3(fc.upVector.x, fc.upVector.z, -fc.upVector.y);
        else
            pc.up = XMFLOAT3(fc.upVector.x, fc.upVector.y, -fc.upVector.z);

        // Camera position: find the FBXModel node whose attributeID links to this camera
        bool foundPos = false;
        for (const auto& m : fbx.models)
        {
            if (m.attributeID != fc.id) continue;

            XMMATRIX world = m_fbxImporter.BuildTransformMatrix(m.transform);
            XMFLOAT4X4 xf;
            XMStoreFloat4x4(&xf, world);
            const XMFLOAT3 rawPos(xf._41, xf._42, xf._43);

            // Apply coord flip (same as light/model position conversion)
            if (fbx.upAxis == 2)
                pc.position = XMFLOAT3(rawPos.x, rawPos.z, -rawPos.y);
            else
                pc.position = XMFLOAT3(rawPos.x, rawPos.y, -rawPos.z);

            foundPos = true;
            break;
        }

        if (!foundPos)
        {
            // No owning model node found; fall back to a reasonable default viewpoint
            pc.position = XMFLOAT3(0.0f, 0.0f, -10.0f);
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[SceneManager] FBX Camera '" + pc.name + L"': no owning model node found, using default position.");
        }

        m_fbxCameras.push_back(pc);

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] FBX Camera '%ls': pos=(%.1f,%.1f,%.1f) target=(%.1f,%.1f,%.1f) "
                L"fovY=%.1f near=%.3f far=%.1f",
                pc.name.c_str(),
                pc.position.x, pc.position.y, pc.position.z,
                pc.target.x,   pc.target.y,   pc.target.z,
                pc.fovYDeg, pc.nearPlane, pc.farPlane);
        #endif
    }

    // Apply the first FBX camera immediately to the engine camera
    if (!m_fbxCameras.empty() && myRenderer)
    {
        const ParsedFBXCamera& first = m_fbxCameras[0];
        Camera& cam = myRenderer->myCamera;

        // Projection: convert FOV to radians and clamp to sane range
        const float fovRad = std::clamp(
            first.fovYDeg * (XM_PI / 180.0f),
            XMConvertToRadians(10.0f),
            XMConvertToRadians(120.0f));
        const float aspect = (myRenderer->iOrigHeight > 0)
            ? static_cast<float>(myRenderer->iOrigWidth) / static_cast<float>(myRenderer->iOrigHeight)
            : (16.0f / 9.0f);

        cam.SetNearFarPlanes(first.nearPlane, first.farPlane);
        cam.SetFieldOfView(first.fovYDeg);

        // Camera apply: DX uses XMMATRIX/XMFLOAT3; Vulkan/OpenGL uses glm types.
        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            cam.position = first.position;
            cam.target   = first.target;
            cam.up       = first.up;

            cam.SetProjectionMatrix(XMMatrixPerspectiveFovLH(fovRad, aspect, first.nearPlane, first.farPlane));

            const XMVECTOR eye = XMVectorSet(first.position.x, first.position.y, first.position.z, 1.0f);
            const XMVECTOR tgt = XMVectorSet(first.target.x,   first.target.y,   first.target.z,   1.0f);
            const XMVECTOR up  = XMVectorSet(first.up.x,       first.up.y,       first.up.z,        0.0f);
            cam.SetViewMatrix(XMMatrixLookAtLH(eye, tgt, up));

            // Keep forward in sync so SetYawPitchFromForward() below uses the new view
            XMStoreFloat3(&cam.forward, XMVector3Normalize(XMVectorSubtract(tgt, eye)));

            // Flag so the loader thread does not override with SetupDefaultCamera
            cam.bCameraJumped = true;
        #else
            cam.position = glm::vec3(first.position.x, first.position.y, first.position.z);
            cam.target   = glm::vec3(first.target.x,   first.target.y,   first.target.z);
            cam.up       = glm::vec3(first.up.x,        first.up.y,       first.up.z);

            #if defined(__USE_OPENGL__)
                // Left-handed projection -- matches OpenGLCamera's perspectiveLH_NO convention
                cam.SetProjectionMatrix(glm::perspectiveLH_NO(fovRad, aspect, first.nearPlane, first.farPlane));
            #elif defined(__USE_VULKAN__)
                // Vulkan LH [0,1]-depth projection with Y-down NDC flip -- rebuilt internally
                // from the near/far/FOV values applied via SetNearFarPlanes/SetFieldOfView above
                cam.UpdateProjectionMatrix();
            #endif

            // lookAtLH: glm::lookAt (RH) mirrored the scene on screen X relative to the
            // left-handed view matrices used everywhere else in the GL/Vulkan camera code.
            cam.viewMatrix = glm::lookAtLH(
                glm::vec3(first.position.x, first.position.y, first.position.z),
                glm::vec3(first.target.x,   first.target.y,   first.target.z),
                glm::vec3(first.up.x,       first.up.y,       first.up.z));

            // Keep forward in sync so SetYawPitchFromForward() below uses the new view
            cam.forward = glm::normalize(cam.target - cam.position);

            // Flag so the loader thread does not override with SetupDefaultCamera / SetPosition.
            // Mirrors the DX11/DX12 branch above - must be set for all renderer paths.
            cam.bCameraJumped = true;
        #endif

        // Derive yaw/pitch from the computed forward vector so mouse look stays consistent
        cam.SetYawPitchFromForward();
        bGltfCameraParsed = true;

        {
            wchar_t buf[256];
            swprintf_s(buf, L"[SceneManager] FBX Camera '%ls' applied: pos=(%.1f,%.1f,%.1f) target=(%.1f,%.1f,%.1f)",
                first.name.c_str(),
                first.position.x, first.position.y, first.position.z,
                first.target.x,   first.target.y,   first.target.z);
            debug.logLevelMessage(LogLevel::LOG_INFO, buf);
        }
    }
}

// ============================================================================
// GotoCamera -- jump instantly or animate the engine camera to a named FBX camera.
// Returns true if the camera was found and the jump was initiated.
// Returns false if no camera with the given name exists in m_fbxCameras.
//
// AnimateTowards=false: position, target, and projection are applied immediately
//   via SetViewMatrix/SetProjectionMatrix -- no animation frames required.
// AnimateTowards=true:  target and projection are set immediately so the scene
//   renders correctly while the camera animates; camera.JumpTo() handles the
//   smooth position transition over multiple frames.
// ============================================================================
bool SceneManager::GotoCamera(const std::wstring& cameraName, bool AnimateTowards)
{
    if (!myRenderer) return false;

    // Find the requested camera by name in the cached FBX camera list
    const ParsedFBXCamera* found = nullptr;
    for (const auto& pc : m_fbxCameras)
    {
        if (pc.name == cameraName)
        {
            found = &pc;
            break;
        }
    }

    if (!found)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"[SceneManager] GotoCamera('" + cameraName + L"'): camera not found in FBX camera list.");
        return false;
    }

    Camera& cam = myRenderer->myCamera;

    // Build projection from FOV and current viewport
    const float fovRad = std::clamp(
        found->fovYDeg * (XM_PI / 180.0f),
        XMConvertToRadians(10.0f),
        XMConvertToRadians(120.0f));
    const float aspect = (myRenderer->iOrigHeight > 0)
        ? static_cast<float>(myRenderer->iOrigWidth) / static_cast<float>(myRenderer->iOrigHeight)
        : (16.0f / 9.0f);

    cam.SetNearFarPlanes(found->nearPlane, found->farPlane);
    cam.SetFieldOfView(found->fovYDeg);

    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        cam.SetProjectionMatrix(XMMatrixPerspectiveFovLH(fovRad, aspect, found->nearPlane, found->farPlane));
    #else
        cam.SetProjectionMatrix(glm::perspective(fovRad, aspect, found->nearPlane, found->farPlane));
    #endif

    if (!AnimateTowards)
    {
        // Instant jump: apply position, target, and view matrix in one step
        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            cam.position = found->position;
            cam.target   = found->target;
            cam.up       = found->up;

            const XMVECTOR eye = XMVectorSet(found->position.x, found->position.y, found->position.z, 1.0f);
            const XMVECTOR tgt = XMVectorSet(found->target.x,   found->target.y,   found->target.z,   1.0f);
            const XMVECTOR up  = XMVectorSet(found->up.x,       found->up.y,       found->up.z,        0.0f);
            cam.SetViewMatrix(XMMatrixLookAtLH(eye, tgt, up));
        #else
            cam.position = glm::vec3(found->position.x, found->position.y, found->position.z);
            cam.target   = glm::vec3(found->target.x,   found->target.y,   found->target.z);
            cam.up       = glm::vec3(found->up.x,        found->up.y,       found->up.z);

            cam.viewMatrix = glm::lookAt(
                glm::vec3(found->position.x, found->position.y, found->position.z),
                glm::vec3(found->target.x,   found->target.y,   found->target.z),
                glm::vec3(found->up.x,       found->up.y,       found->up.z));
        #endif

        cam.SetYawPitchFromForward();

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] GotoCamera('%ls'): instant jump to (%.1f,%.1f,%.1f)",
                cameraName.c_str(), found->position.x, found->position.y, found->position.z);
        #endif
    }
    else
    {
        // Animated jump: set target and up immediately; JumpTo() handles smooth position animation.
        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            cam.target = found->target;
            cam.up     = found->up;
        #else
            cam.target = glm::vec3(found->target.x, found->target.y, found->target.z);
            cam.up     = glm::vec3(found->up.x,     found->up.y,     found->up.z);
        #endif

        cam.JumpTo(found->position.x, found->position.y, found->position.z,
                   2 /*speed*/, true /*FocusOnTarget*/);

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] GotoCamera('%ls'): animating to (%.1f,%.1f,%.1f)",
                cameraName.c_str(), found->position.x, found->position.y, found->position.z);
        #endif
    }

    // All renderer paths: flag the camera as scene-positioned so loader
    // thread guards do not override with the default (0,6,-80) position.
    cam.bCameraJumped = true;
    return true;
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

        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            camera.SetProjectionMatrix(XMMatrixPerspectiveFovLH(yfov, aspect, nearZ, farZ));
        #elif defined(__USE_OPENGL__)
            // Left-handed projection to match OpenGLCamera's perspectiveLH_NO convention.
            // glm::perspective is right-handed and mirrors the scene on screen X relative
            // to every other view/projection build in the engine.
            camera.SetProjectionMatrix(glm::perspectiveLH_NO(yfov, aspect, nearZ, farZ));
        #elif defined(__USE_VULKAN__)
            // Vulkan requires the left-handed [0,1]-depth projection with the Y-down NDC
            // flip (VulkanCamera::MakeVulkanProjection).  Feed the GLTF parameters through
            // the camera's own rebuild path instead of glm::perspective (RH, [-1,1] depth).
            camera.SetNearFarPlanes(nearZ, farZ);
            camera.SetFieldOfView(glm::degrees(yfov));
            camera.UpdateProjectionMatrix();
        #endif

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

            // GLTF node translation is RH Y-up -- convert to engine LH space with the
            // same FLIP_Z conversion applied to geometry, nodes, and lights.  Without
            // this the camera lands on the wrong side of the Z-flipped scene and views
            // every model from behind, which appears as an X-axis mirror on screen.
            eyePos = BlenderImports::ConvertTranslation(eyePos, m_blenderConfig.flipAxes);
        }

        XMVECTOR eye = XMVectorSet(eyePos.x, eyePos.y, eyePos.z, 1.0f);         // Camera eye position.

        // GLTF cameras look down local -Z in RH space; the FLIP_Z conversion maps
        // that to +Z in engine LH space.  Initialised here to avoid uninitialized
        // memory usage when the node has no rotation.
        XMVECTOR forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);                 // Default forward direction (engine LH space).
        XMVECTOR target = XMVectorAdd(eye, forward);                            // Default look-at target.

        // --- Rotation -> Forward Vector
        if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() == 4)
        {
            float qx = node["rotation"][0];
            float qy = node["rotation"][1];
            float qz = node["rotation"][2];
            float qw = node["rotation"][3];

            // Convert the RH GLTF quaternion to engine LH space (FLIP_Z negates qx/qy)
            // -- the same conversion GetNodeWorldMatrix applies to node rotations --
            // then rotate the engine-space local forward (+Z after conversion).
            XMFLOAT4 lhQ = BlenderImports::ConvertQuat(XMFLOAT4(qx, qy, qz, qw), m_blenderConfig.flipAxes);
            XMVECTOR quat = XMVectorSet(lhQ.x, lhQ.y, lhQ.z, lhQ.w);
            XMMATRIX rotMatrix = XMMatrixRotationQuaternion(quat);
            forward = XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotMatrix);
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
            target = XMVectorAdd(eye, forward);                                 // Use initialized default forward vector.
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
        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            camera.viewMatrix = view;
            camera.position = eyePos;
            camera.target = XMFLOAT3(XMVectorGetX(target), XMVectorGetY(target), XMVectorGetZ(target));
            XMStoreFloat3(&camera.forward, XMVector3Normalize(forward));           // Keep forward in sync with the view
        #else
            // lookAtLH: every other view-matrix build in the OpenGL/Vulkan camera code is
            // left-handed -- glm::lookAt (RH) here mirrored the scene on screen X.
            camera.viewMatrix = glm::lookAtLH(
                glm::vec3(eyePos.x, eyePos.y, eyePos.z),
                glm::vec3(XMVectorGetX(target), XMVectorGetY(target), XMVectorGetZ(target)),
                glm::vec3(0.0f, 1.0f, 0.0f));
            camera.position = glm::vec3(eyePos.x, eyePos.y, eyePos.z);
            camera.target   = glm::vec3(XMVectorGetX(target), XMVectorGetY(target), XMVectorGetZ(target));
            camera.forward  = glm::normalize(camera.target - camera.position);     // Keep forward in sync with the view
        #endif
        // Sync yaw/pitch with the scene-defined view so the first mouse-look does not
        // snap the camera back to the stale yaw=0/pitch=0 orientation.
        camera.SetYawPitchFromForward();

        // DO NOT call UpdateViewMatrix() here, it would override GLTF settings
        bGltfCameraParsed = true;

        // Signal the loader thread that a scene camera is active so it does not
        // override position/projection with the hard-coded default (0,6,-80).
        // Required for all renderer paths - mirrors the FBX camera path.
        camera.bCameraJumped = true;
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
        XMMATRIX nodeMatrix = GetNodeWorldMatrix(node, m_blenderConfig);
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

// ==================================================================================================
// SceneManager::EnsureDefaultSunLight()
// Called after every scene lights-parse step. If the lightsManager is still empty (the scene had
// no embedded lights), injects a single default directional sun so that DX11/DX12/GL/Vulkan shaders
// never receive globalLightCount == 0 and render models black.
// ==================================================================================================
void SceneManager::EnsureDefaultSunLight()
{
    if (lightsManager.GetLightCount() > 0)
        return;

    LightStruct sun    = {};
    sun.type           = int(LightType::DIRECTIONAL);
    sun.active         = 1;
    sun.color          = XMFLOAT3(1.0f, 0.95f, 0.88f);  // warm white sunlight
    sun.ambient        = XMFLOAT3(0.25f, 0.25f, 0.28f); // soft blue-grey fill light
    sun.intensity      = 1.5f;
    sun.baseIntensity  = 1.5f;
    sun.direction      = XMFLOAT3(0.3f, -0.7f, -0.6f);  // from upper-left front
    sun.range          = 10000.0f;
    sun.animMode       = int(LightAnimMode::None);
    sun.Shiningness    = 32.0f;
    sun.Reflection     = 0.5f;

    lightsManager.CreateLight(L"DefaultSun", sun);
    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[SceneManager] No scene lights found -- injected DefaultSun directional light");
    #endif
}

// ==================================================================================================
// SceneManager::ParseSceneAutoDetect()
// Detects the scene file format from the file extension and/or binary magic, then routes to the
// appropriate parser: .glb -> ParseGLBScene, .gltf -> ParseGLTFScene, .fbx -> ParseFBXScene.
// ==================================================================================================
bool SceneManager::ParseSceneAutoDetect(const std::wstring& sceneFile, bool bCacheOnly)
{
    if (sceneFile.empty())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[SceneManager] ParseSceneAutoDetect(): empty file path.");
        return false;
    }

    // Lower-case extension check
    std::wstring ext;
    const auto dot = sceneFile.rfind(L'.');
    if (dot != std::wstring::npos)
    {
        ext = sceneFile.substr(dot + 1);
        for (auto& c : ext) c = static_cast<wchar_t>(tolower(c));
    }

    // Check binary magic when extension alone is ambiguous
    auto readMagic = [&](size_t n, std::vector<uint8_t>& out) -> bool
    {
        std::ifstream f(sceneFile, std::ios::binary);
        if (!f.is_open()) return false;
        out.resize(n);
        f.read(reinterpret_cast<char*>(out.data()), n);
        return f.gcount() == static_cast<std::streamsize>(n);
    };

    if (ext == L"glb")
    {
        return ParseGLBScene(sceneFile, bCacheOnly);
    }
    else if (ext == L"gltf")
    {
        return ParseGLTFScene(sceneFile, bCacheOnly);
    }
    else if (ext == L"fbx")
    {
        return ParseFBXScene(sceneFile, bCacheOnly);
    }
    else
    {
        // No known extension -- peek at binary magic
        std::vector<uint8_t> magic;
        if (readMagic(23, magic))
        {
            // GLB: "glTF" 0x46546C67
            if (magic.size() >= 4 &&
                magic[0] == 0x67 && magic[1] == 0x6C &&
                magic[2] == 0x54 && magic[3] == 0x46)
                return ParseGLBScene(sceneFile, bCacheOnly);

            // FBX binary: "Kaydara FBX Binary  \x00\x1a\x00"
            static const uint8_t kFBX[23] = {
                'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',
                ' ',' ','\x00','\x1a','\x00'
            };
            if (memcmp(magic.data(), kFBX, 23) == 0)
                return ParseFBXScene(sceneFile, bCacheOnly);
        }

        debug.logLevelMessage(LogLevel::LOG_ERROR,
            (L"[SceneManager] ParseSceneAutoDetect(): unknown format for '" + sceneFile + L"'").c_str());
        return false;
    }
}

// ==================================================================================================
// SceneManager::ParseFBXScene()
// Parses an FBX 7.x file (binary or ASCII) and populates scene_models[] exactly like ParseGLBScene.
// Supports: vertices, UV maps, normals/tangents, materials, lights, cameras,
//           parent-child hierarchy, animations, shadow data.
// ==================================================================================================
bool SceneManager::ParseFBXScene(const std::wstring& fbxFile, bool bCacheOnly)
{
    #if defined(_DEBUG_SCENEMANAGER_)
        const auto _t0 = std::chrono::high_resolution_clock::now();
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] ParseFBXScene() BEGIN -- %ls", fbxFile.c_str());
    #endif

    bLoadedFromCache = false;
    m_fbxCameras.clear();   // always start fresh - stale cameras from a prior scene must not persist
    lightsManager.ClearLights(); // clear lights from any prior scene before parsing new ones

    // Loading-text progress helper -- same pattern as showStage in IOLoaderThread.
    auto showStage = [](const wchar_t* msg) {
        TextRenderStyle s;
        s.fontName = LoadingTextFX::kFontName;
        s.fontSize = 20.0f;
        s.centered = true;
        fxManager.ShowLoadingText(msg,
            XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
            0.2f, 0.05f,
            XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f),
            0.0f, -1.0f, &s);
    };

    // =========================================================================
    // FBX GEOMETRY CACHE FAST-PATH
    // On second+ visit to the same FBX scene (or after a cache.dat restore),
    // models[] already holds GPU-ready geometry.  Re-parse the FBX file FIRST
    // to restore per-load session state (cameras, lights, material data), then
    // restore scene_models[] from cache and rebind textures -- matching the
    // ParseGLBScene cache-restore pattern exactly.
    // =========================================================================
    {
        int cacheCount = 0;
        for (int m = 0; m < MAX_MODELS; ++m)
        {
            if (models[m].m_modelInfo.sourceSceneFile == fbxFile &&
                models[m].m_modelInfo.bGpuReady &&
                models[m].m_modelInfo.cachedInstanceIndex >= 0)
                ++cacheCount;
        }

        if (cacheCount > 0)
        {
            // ---- Step 1: Parse FBX to restore per-load session state ----
            // Cameras, lights and material data must be refreshed before touching
            // scene_models[] so that texture lookups and Kd values are valid.
            bool fbxMiniParseOK = m_fbxImporter.LoadFile(fbxFile);
            if (!fbxMiniParseOK)
            {
                debug.logLevelMessage(LogLevel::LOG_WARNING,
                    (L"[SceneManager] FBX CACHE-RESTORE: LoadFile failed for '" + fbxFile +
                     L"' -- falling through to full re-parse").c_str());
            }
            else
            {
                const FBXScene& cFbx   = m_fbxImporter.GetScene();
                std::wstring    cBaseDir = fbxFile;
                {
                    const auto sl = cBaseDir.find_last_of(L"\\/");
                    if (sl != std::wstring::npos) cBaseDir = cBaseDir.substr(0, sl);
                }
                m_currentSceneFile = fbxFile;

                // Cameras
                ParseFBXCameras(cFbx);

                // Lights
                for (size_t li = 0; li < cFbx.lights.size(); ++li)
                {
                    const FBXLight& fl = cFbx.lights[li];
                    LightStruct ls{};
                    ls.active    = 1;
                    ls.intensity = fl.intensity / 100.0f;
                    ls.color     = fl.color;
                    ls.range     = fl.range;
                    switch (fl.lightType)
                    {
                        case FBXLightType::Directional: ls.type = int(LightType::DIRECTIONAL); break;
                        case FBXLightType::Spot:        ls.type = int(LightType::SPOT);        break;
                        default:                        ls.type = int(LightType::POINT);       break;
                    }
                    if (fl.lightType == FBXLightType::Spot)
                    {
                        ls.innerCone = fl.innerAngle * (XM_PI / 180.0f);
                        ls.outerCone = fl.outerAngle * (XM_PI / 180.0f);
                    }
                    for (const auto& mdl : cFbx.models)
                    {
                        if (mdl.attributeID != fl.id) continue;
                        XMMATRIX    world = m_fbxImporter.BuildTransformMatrix(mdl.transform);
                        XMFLOAT4X4  xf;   XMStoreFloat4x4(&xf, world);
                        const XMFLOAT3 rawPos(xf._41, xf._42, xf._43);
                        if (cFbx.upAxis == 2) ls.position = XMFLOAT3(rawPos.x, rawPos.z, -rawPos.y);
                        else                  ls.position = XMFLOAT3(rawPos.x, rawPos.y, -rawPos.z);
                        XMVECTOR fwdRH = XMVector3TransformNormal(XMVectorSet(0,0,-1,0), world);
                        XMFLOAT3 fRH;  XMStoreFloat3(&fRH, fwdRH);
                        if (cFbx.upAxis == 2) ls.direction = XMFLOAT3(fRH.x, fRH.z, -fRH.y);
                        else                  ls.direction = XMFLOAT3(fRH.x, fRH.y, -fRH.z);
                        break;
                    }
                    lightsManager.CreateLight(L"FBX_Light_" + std::to_wstring(li), ls);
                }
                EnsureDefaultSunLight();

                debug.logLevelMessage(LogLevel::LOG_INFO,
                    (L"[SceneManager] FBX CACHE-RESTORE: parsed OK -- models=" +
                     std::to_wstring(cFbx.models.size()) + L" mats=" +
                     std::to_wstring(cFbx.materials.size()) + L" lights=" +
                     std::to_wstring(cFbx.lights.size())).c_str());

                // Cache-only mode: models[] is already GPU-ready; skip scene_models[] restore.
                if (bCacheOnly)
                {
                    bLoadedFromCache = true;
                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        L"[SceneManager] ParseFBXScene() CACHE HIT (cache-only mode) -- models[] GPU-ready, scene_models[] left empty.");
                    return true;
                }

                // ---- Step 2: Restore scene_models[] geometry from cache ----
                int instanceIndex = 0;
                for (int m = 0; m < MAX_MODELS; ++m)
                {
                    const ModelInfo& cache = models[m].m_modelInfo;
                    if (cache.sourceSceneFile != fbxFile) continue;
                    if (!cache.bGpuReady)                 continue;
                    int idx = cache.cachedInstanceIndex;
                    if (idx < 0 || idx >= MAX_SCENE_MODELS) continue;

                    if (cache.bIsTransformOnly)
                    {
                        scene_models[idx].DestroyModel();
                        scene_models[idx].m_modelInfo.bIsTransformOnly  = true;
                        scene_models[idx].m_modelInfo.bIsTransformProxy = true;
                    }
                    else
                    {
                        scene_models[idx].CopyFrom(models[m]);

                        // Restore CPU geometry if CopyFrom left it empty
                        if (scene_models[idx].m_modelInfo.vertices.empty() &&
                            !models[m].m_modelInfo.vertices.empty())
                        {
                            scene_models[idx].m_modelInfo.vertices = models[m].m_modelInfo.vertices;
                            scene_models[idx].m_modelInfo.indices  = models[m].m_modelInfo.indices;
                        }

                        // GPU rebuild check -- device reset / cache.dat restore
                        bool gpuRebuildNeeded = false;
                        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            if (!scene_models[idx].m_modelInfo.vertexBuffer  ||
                                !scene_models[idx].m_modelInfo.indexBuffer   ||
                                !scene_models[idx].m_modelInfo.constantBuffer)
                                gpuRebuildNeeded = true;
                        #elif defined(__USE_VULKAN__)
                            if (scene_models[idx].m_modelInfo.vertexBuffer  == VK_NULL_HANDLE ||
                                scene_models[idx].m_modelInfo.indexBuffer   == VK_NULL_HANDLE ||
                                scene_models[idx].m_modelInfo.uniformBuffer == VK_NULL_HANDLE)
                                gpuRebuildNeeded = true;
                        #elif defined(__USE_OPENGL__)
                            if (scene_models[idx].m_modelInfo.VAO == 0 ||
                                scene_models[idx].m_modelInfo.VBO == 0 ||
                                scene_models[idx].m_modelInfo.EBO == 0)
                                gpuRebuildNeeded = true;
                        #endif
                        if (gpuRebuildNeeded && !scene_models[idx].m_modelInfo.vertices.empty())
                        {
                            scene_models[idx].SetupModelForRendering(idx);
                            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                models[m].m_modelInfo.vertexBuffer        = scene_models[idx].m_modelInfo.vertexBuffer;
                                models[m].m_modelInfo.indexBuffer         = scene_models[idx].m_modelInfo.indexBuffer;
                                models[m].m_modelInfo.constantBuffer      = scene_models[idx].m_modelInfo.constantBuffer;
                                models[m].m_modelInfo.lightConstantBuffer = scene_models[idx].m_modelInfo.lightConstantBuffer;
                                models[m].m_modelInfo.materialBuffer      = scene_models[idx].m_modelInfo.materialBuffer;
                                models[m].m_modelInfo.samplerState        = scene_models[idx].m_modelInfo.samplerState;
                                models[m].m_modelInfo.textureSRVs         = scene_models[idx].m_modelInfo.textureSRVs;
                                models[m].m_modelInfo.normalMapSRVs       = scene_models[idx].m_modelInfo.normalMapSRVs;
                            #elif defined(__USE_VULKAN__)
                                models[m].m_modelInfo.vertexBuffer               = scene_models[idx].m_modelInfo.vertexBuffer;
                                models[m].m_modelInfo.vertexBufferMemory         = scene_models[idx].m_modelInfo.vertexBufferMemory;
                                models[m].m_modelInfo.indexBuffer                = scene_models[idx].m_modelInfo.indexBuffer;
                                models[m].m_modelInfo.indexBufferMemory          = scene_models[idx].m_modelInfo.indexBufferMemory;
                                models[m].m_modelInfo.uniformBuffer              = scene_models[idx].m_modelInfo.uniformBuffer;
                                models[m].m_modelInfo.uniformBufferMemory        = scene_models[idx].m_modelInfo.uniformBufferMemory;
                                models[m].m_modelInfo.uniformBufferMapped        = scene_models[idx].m_modelInfo.uniformBufferMapped;
                                models[m].m_modelInfo.materialUniformBuffer      = scene_models[idx].m_modelInfo.materialUniformBuffer;
                                models[m].m_modelInfo.materialUniformBufferMemory= scene_models[idx].m_modelInfo.materialUniformBufferMemory;
                                models[m].m_modelInfo.materialUniformBufferMapped= scene_models[idx].m_modelInfo.materialUniformBufferMapped;
                                models[m].m_modelInfo.pipeline                   = scene_models[idx].m_modelInfo.pipeline;
                                models[m].m_modelInfo.pipelineLayout             = scene_models[idx].m_modelInfo.pipelineLayout;
                                models[m].m_modelInfo.descriptorSet              = scene_models[idx].m_modelInfo.descriptorSet;
                                models[m].m_modelInfo.textureDescriptorSet       = scene_models[idx].m_modelInfo.textureDescriptorSet;
                            #elif defined(__USE_OPENGL__)
                                models[m].m_modelInfo.VAO           = scene_models[idx].m_modelInfo.VAO;
                                models[m].m_modelInfo.VBO           = scene_models[idx].m_modelInfo.VBO;
                                models[m].m_modelInfo.EBO           = scene_models[idx].m_modelInfo.EBO;
                                models[m].m_modelInfo.shaderProgram = scene_models[idx].m_modelInfo.shaderProgram;
                            #endif
                            models[m].m_modelInfo.bGpuReady = true;

                            debug.logLevelMessage(LogLevel::LOG_INFO,
                                (L"[SceneManager] FBX CACHE-RESTORE: GPU rebuild triggered for '" +
                                 cache.name + L"'").c_str());
                        }

                        // Restore CPU material structs (empty after cache.dat reload)
                        if (scene_models[idx].m_materials.empty() && !models[m].m_materials.empty())
                            scene_models[idx].m_materials = models[m].m_materials;
                        if (scene_models[idx].m_modelInfo.materials.empty() &&
                            !models[m].m_modelInfo.materials.empty())
                            scene_models[idx].m_modelInfo.materials = models[m].m_modelInfo.materials;
                    }

                    // Common fields -- set for both geometry and transform-only nodes
                    scene_models[idx].m_modelInfo.ID                    = idx;
                    scene_models[idx].m_modelInfo.name                  = cache.name;
                    scene_models[idx].m_modelInfo.worldMatrix            = cache.worldMatrix;
                    scene_models[idx].m_modelInfo.iParentModelID         = cache.iParentModelID;
                    scene_models[idx].m_modelInfo.gltfNodeIndex          = cache.gltfNodeIndex;
                    scene_models[idx].m_modelInfo.baseLocalTranslation   = cache.baseLocalTranslation;
                    scene_models[idx].m_modelInfo.baseLocalRotationQuat  = cache.baseLocalRotationQuat;
                    scene_models[idx].m_modelInfo.baseLocalScale         = cache.baseLocalScale;
                    scene_models[idx].m_modelInfo.animLocalTranslation   = cache.baseLocalTranslation;
                    scene_models[idx].m_modelInfo.animLocalRotationQuat  = cache.baseLocalRotationQuat;
                    scene_models[idx].m_modelInfo.animLocalScale         = cache.baseLocalScale;
                    scene_models[idx].m_modelInfo.bHasBaseLocalTRS       = cache.bHasBaseLocalTRS;
                    scene_models[idx].m_modelInfo.bIsTransformOnly       = cache.bIsTransformOnly;
                    scene_models[idx].m_modelInfo.bIsTransformProxy      = cache.bIsTransformProxy;
                    scene_models[idx].m_modelInfo.position               = cache.position;
                    scene_models[idx].ApplyDefaultLightingFromManager(lightsManager);
                    scene_models[idx].m_isLoaded = true;

                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        (L"[SceneManager] FBX CACHE-RESTORE: hit '" + cache.name +
                         L"' -> scene_models[" + std::to_wstring(idx) + L"]").c_str());

                    instanceIndex = std::max(instanceIndex, idx + 1);
                }

                // ---- Step 3: Rebind textures from fresh FBX material data ----
                // SRVs and shared_ptr<Texture> objects do not survive cache.dat reload or
                // device reset.  Rebuild them here from the freshly-parsed FBX materials,
                // mirroring the GLB cache restore Step 4 (BindGLTFMaterialTexturesToModel).
                for (int ti = 0; ti < instanceIndex; ++ti)
                {
                    if (!scene_models[ti].m_isLoaded)                    continue;
                    if (scene_models[ti].m_modelInfo.bIsTransformProxy)  continue;
                    if (scene_models[ti].m_modelInfo.bIsTransformOnly)   continue;
                    if (scene_models[ti].m_modelInfo.materials.empty())  continue;

                    const std::string& matName = scene_models[ti].m_modelInfo.materials[0];

                    // Find the FBX material by name in the freshly-parsed scene
                    const FBXMaterial* fbxMat = nullptr;
                    for (const auto& mat : cFbx.materials)
                    {
                        if (mat.name == matName) { fbxMat = &mat; break; }
                    }
                    if (!fbxMat)
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING,
                            (L"[SceneManager] FBX CACHE-RESTORE: mat '" +
                             std::wstring(matName.begin(), matName.end()) +
                             L"' not found -- textures not rebound for '" +
                             scene_models[ti].m_modelInfo.name + L"'").c_str());
                        continue;
                    }

                    // Build fresh engineMat (Kd, normal map, roughness, etc.)
                    Material cEngMat;
                    m_fbxImporter.BuildMaterial(*fbxMat, cBaseDir, cEngMat);

                    // Clear stale SRV handles from prior session / cache.dat restore
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        scene_models[ti].m_modelInfo.textures.clear();
                        scene_models[ti].m_modelInfo.textureSRVs.clear();
                        scene_models[ti].m_modelInfo.normalMapSRVs.clear();
                        scene_models[ti].m_modelInfo.metallicMapSRV.Reset();
                        scene_models[ti].m_modelInfo.roughnessMapSRV.Reset();
                        scene_models[ti].m_modelInfo.aoMapSRV.Reset();
                        scene_models[ti].m_modelInfo.emissiveMapSRV.Reset();
                    #elif defined(__USE_OPENGL__)
                        scene_models[ti].m_modelInfo.textureIDs.clear();
                        scene_models[ti].m_modelInfo.normalMapIDs.clear();
                        scene_models[ti].m_modelInfo.metallicTexID  = 0;
                        scene_models[ti].m_modelInfo.roughnessTexID = 0;
                        scene_models[ti].m_modelInfo.aoTexID        = 0;
                        scene_models[ti].m_modelInfo.emissiveTexID  = 0;
                    #endif

                    // No file texture -- create WHITE 1x1 so shader reads Kd unchanged
                    // (white x Kd = Kd; using Kd colour in both texture and Kd would darken via Kd*Kd)
                    if (!cEngMat.diffuseTexture)
                    {
                        auto fb = std::make_shared<Texture>();
                        if (fb->CreateSolidColorTexture(1, 1, XMFLOAT4(1.0f, 1.0f, 1.0f, fbxMat->opacity)))
                            cEngMat.diffuseTexture = fb;
                    }

                    // Register shared_ptr texture objects
                    auto cAddTex = [&](std::shared_ptr<Texture> t)
                    {
                        if (!t) return;
                        scene_models[ti].m_modelInfo.textures.push_back(t);
                    };
                    cAddTex(cEngMat.diffuseTexture);
                    cAddTex(cEngMat.normalMap);
                    cAddTex(cEngMat.roughnessMap);
                    cAddTex(cEngMat.metallicMap);
                    cAddTex(cEngMat.aoMap);
                    cAddTex(cEngMat.emissiveMap);

                    // Bind SRVs / texture IDs per renderer
                    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                        if (cEngMat.diffuseTexture)
                            scene_models[ti].m_modelInfo.textureSRVs.push_back(cEngMat.diffuseTexture->GetSRV());
                        if (cEngMat.normalMap)
                            scene_models[ti].m_modelInfo.normalMapSRVs.push_back(cEngMat.normalMap->GetSRV());
                        if (cEngMat.roughnessMap)
                            scene_models[ti].m_modelInfo.roughnessMapSRV = cEngMat.roughnessMap->GetSRV();
                        if (cEngMat.metallicMap)
                            scene_models[ti].m_modelInfo.metallicMapSRV  = cEngMat.metallicMap->GetSRV();
                        if (cEngMat.aoMap)
                            scene_models[ti].m_modelInfo.aoMapSRV        = cEngMat.aoMap->GetSRV();
                        if (cEngMat.emissiveMap)
                        {
                            scene_models[ti].m_modelInfo.emissiveMapSRV     = cEngMat.emissiveMap->GetSRV();
                            scene_models[ti].m_modelInfo.emissiveMapTexture = cEngMat.emissiveMap;
                            scene_models[ti].m_modelInfo.useEmissiveMap     = true;
                        }
                    #endif

                    // Update material struct and metallic/roughness scalars
                    scene_models[ti].m_materials[matName]          = cEngMat;
                    scene_models[ti].m_modelInfo.metallic           = cEngMat.Metallic;
                    scene_models[ti].m_modelInfo.roughness          = cEngMat.Roughness;

                    #if defined(__USE_DIRECTX_12__)
                        // DX12: re-upload the rebound textures to the native D3D12 heap and
                        // force descriptor rewrite - the SRV rebind above only fixes the
                        // DX11-on-12 side (see GLB cache-restore Step 4 for details).
                        scene_models[ti].RefreshDX12Textures();
                    #endif

                    // Write texture handles back to models[] so the next reload gets them too
                    for (int m2 = 0; m2 < MAX_MODELS; ++m2)
                    {
                        if (models[m2].m_modelInfo.cachedInstanceIndex != ti) continue;
                        if (models[m2].m_modelInfo.sourceSceneFile     != fbxFile) continue;
                        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            models[m2].m_modelInfo.textures           = scene_models[ti].m_modelInfo.textures;
                            models[m2].m_modelInfo.textureSRVs        = scene_models[ti].m_modelInfo.textureSRVs;
                            models[m2].m_modelInfo.normalMapSRVs      = scene_models[ti].m_modelInfo.normalMapSRVs;
                            models[m2].m_modelInfo.metallicMapSRV     = scene_models[ti].m_modelInfo.metallicMapSRV;
                            models[m2].m_modelInfo.roughnessMapSRV    = scene_models[ti].m_modelInfo.roughnessMapSRV;
                            models[m2].m_modelInfo.aoMapSRV           = scene_models[ti].m_modelInfo.aoMapSRV;
                            models[m2].m_modelInfo.emissiveMapSRV     = scene_models[ti].m_modelInfo.emissiveMapSRV;
                            models[m2].m_modelInfo.emissiveMapTexture = scene_models[ti].m_modelInfo.emissiveMapTexture;
                            models[m2].m_modelInfo.useEmissiveMap     = scene_models[ti].m_modelInfo.useEmissiveMap;
                        #endif

                        models[m2].m_materials                 = scene_models[ti].m_materials;
                        break;
                    }

                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        (L"[SceneManager] FBX CACHE-RESTORE: textures rebound '" +
                         scene_models[ti].m_modelInfo.name + L"' mat='" +
                         std::wstring(matName.begin(), matName.end()) +
                         L"' Kd=(" + std::to_wstring(cEngMat.Kd.x) +
                         L"," + std::to_wstring(cEngMat.Kd.y) +
                         L"," + std::to_wstring(cEngMat.Kd.z) + L")").c_str());
                }

                // ---- Step 4: Rebuild FBX ID -> scene_models[] slot map for animation binding ----
                // Match FBX model names against cached scene_models[] names;
                // first sub-mesh always keeps the original FBX model name.
                std::unordered_map<int64_t, int> cIDToSlot;
                for (const auto& fbxMdl : cFbx.models)
                {
                    std::wstring wFBXName(fbxMdl.name.begin(), fbxMdl.name.end());
                    for (int ti = 0; ti < instanceIndex; ++ti)
                    {
                        if (!scene_models[ti].m_isLoaded) continue;
                        if (scene_models[ti].m_modelInfo.name == wFBXName)
                        {
                            cIDToSlot[fbxMdl.id] = ti;
                            break;
                        }
                    }
                }

                // ---- Step 5: Start FBX animations ----
                bAnimationsLoaded = false;
                if (!cFbx.animStacks.empty())
                {
                    std::vector<GLTFAnimation> cAnims;
                    m_fbxImporter.ConvertAnimations(cIDToSlot, cAnims);
                    if (!cAnims.empty())
                    {
                        bAnimationsLoaded = true;
                        for (int ai = 0; ai < static_cast<int>(cAnims.size()); ++ai)
                        {
                            debug.logLevelMessage(LogLevel::LOG_INFO,
                                (L"[SceneManager] FBX CACHE-RESTORE: anim '" +
                                 cAnims[ai].name + L"' dur=" +
                                 std::to_wstring(cAnims[ai].duration) + L"s").c_str());
                        }
                    }
                }

                // ---- Step 6: Done ----
                if (instanceIndex > 0)
                {
                    bLoadedFromCache = true;
                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        (L"[SceneManager] FBX CACHE HIT -- " +
                         std::to_wstring(instanceIndex) +
                         L" instance(s) restored from models[] pool").c_str());
                    return true;
                }

                debug.logLevelMessage(LogLevel::LOG_WARNING,
                    (L"[SceneManager] FBX CACHE-RESTORE: " +
                     std::to_wstring(cacheCount) +
                     L" cache entries present but rebuild yielded 0 -- falling through to full parse").c_str());
            }
        }
    }

    if (!std::filesystem::exists(fbxFile))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            (L"[SceneManager] ParseFBXScene(): file not found: " + fbxFile).c_str());
        return false;
    }

    // Show progress before the potentially slow FBX file parse begins.
    showStage(L"Reading FBX file...");

    // Parse the FBX file
    if (!m_fbxImporter.LoadFile(fbxFile))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            (L"[SceneManager] ParseFBXScene(): FBXImporter failed to parse: " + fbxFile).c_str());
        return false;
    }

    const FBXScene& fbx = m_fbxImporter.GetScene();

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[SceneManager] FBX GlobalSettings: upAxis=%d unitScaleFactor=%.6f models=%d geoms=%d mats=%d lights=%d animStacks=%d",
            fbx.upAxis, fbx.unitScaleFactor,
            (int)fbx.models.size(), (int)fbx.geometries.size(),
            (int)fbx.materials.size(), (int)fbx.lights.size(), (int)fbx.animStacks.size());
    #endif

    // Base directory for texture path resolution
    std::wstring baseDir = fbxFile;
    const auto lastSlash = baseDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) baseDir = baseDir.substr(0, lastSlash);

    // Store source file for cache key
    m_currentSceneFile = fbxFile;

    // -------------------------------------------------------------------------
    // 1. Camera -- apply the first FBX camera to the renderer camera
    // -------------------------------------------------------------------------
    // Extract all FBX cameras, convert to engine space, apply first one immediately.
    // ParseFBXCameras sets bGltfCameraParsed and myRenderer->myCamera.bCameraJumped.
    showStage(L"Parsing scene data...");
    ParseFBXCameras(fbx);

    // -------------------------------------------------------------------------
    // 2. Lights -- register in lightsManager
    // -------------------------------------------------------------------------
    for (size_t li = 0; li < fbx.lights.size(); ++li)
    {
        const FBXLight& fl = fbx.lights[li];
        LightStruct ls{};
        ls.active    = 1;
        ls.intensity = fl.intensity / 100.0f; // normalise from FBX candela-like scale
        ls.color     = fl.color;
        ls.range     = fl.range;

        switch (fl.lightType)
        {
        case FBXLightType::Directional: ls.type = int(LightType::DIRECTIONAL); break;
        case FBXLightType::Spot:        ls.type = int(LightType::SPOT);        break;
        default:                        ls.type = int(LightType::POINT);       break;
        }

        if (fl.lightType == FBXLightType::Spot)
        {
            ls.innerCone = fl.innerAngle * (XM_PI / 180.0f);
            ls.outerCone = fl.outerAngle * (XM_PI / 180.0f);
        }

        // Resolve world position/direction from the model that owns this light attribute.
        // BuildTransformMatrix returns the full TRS in FBX right-handed space;
        // we must convert position and direction to engine left-handed space.
        for (const auto& m : fbx.models)
        {
            if (m.attributeID == fl.id)
            {
                XMMATRIX world = m_fbxImporter.BuildTransformMatrix(m.transform);
                XMFLOAT4X4 xf;
                XMStoreFloat4x4(&xf, world);

                // Position: apply coord flip (same as static translation conversion)
                const XMFLOAT3 rawPos(xf._41, xf._42, xf._43);
                if (fbx.upAxis == 2)
                    ls.position = XMFLOAT3(rawPos.x, rawPos.z, -rawPos.y); // Z-up RH -> Y-up LH
                else
                    ls.position = XMFLOAT3(rawPos.x, rawPos.y, -rawPos.z); // Y-up RH -> Y-up LH

                // Direction: transform local -Z forward (FBX RH convention) through the
                // world rotation, then flip the resulting world-space vector to engine LH.
                XMVECTOR fwdRH = XMVector3TransformNormal(XMVectorSet(0,0,-1,0), world);
                XMFLOAT3 fRH; XMStoreFloat3(&fRH, fwdRH);
                if (fbx.upAxis == 2)
                    ls.direction = XMFLOAT3(fRH.x, fRH.z, -fRH.y);
                else
                    ls.direction = XMFLOAT3(fRH.x, fRH.y, -fRH.z);
                break;
            }
        }

        std::wstring lightName = L"FBX_Light_" + std::to_wstring(li);
        lightsManager.CreateLight(lightName, ls);

        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[SceneManager] FBX Light[%d] '%hs' type=%d intensity=%.2f",
                static_cast<int>(li), fl.name.c_str(), ls.type, ls.intensity);
        #endif
    }
    EnsureDefaultSunLight();

    // -------------------------------------------------------------------------
    // 3. Build a map from FBX model ID -> parent ID for hierarchy resolution,
    //    and a map from FBX model ID -> scene_models[] slot for animation binding
    // -------------------------------------------------------------------------
    std::unordered_map<int64_t, int> fbxIDToSlot;   // fbxModel.id -> scene_models[] index
    int instanceIndex = 0;

    // Sort models so parents come before children (topological order by parentID=0 first)
    // Simple two-pass: process root models first, then children
    std::vector<int> sortedModelIdx;
    {
        // First pass: roots (parentID == 0 or parent not in scene)
        for (int i = 0; i < static_cast<int>(fbx.models.size()); ++i)
            if (fbx.models[i].parentID == 0 || fbx.modelByID.find(fbx.models[i].parentID) == fbx.modelByID.end())
                sortedModelIdx.push_back(i);
        // Second pass: children (any not yet added)
        std::unordered_set<int> added(sortedModelIdx.begin(), sortedModelIdx.end());
        for (int i = 0; i < static_cast<int>(fbx.models.size()); ++i)
            if (added.find(i) == added.end()) sortedModelIdx.push_back(i);
    }

    // -------------------------------------------------------------------------
    // 4. Instantiate each model into scene_models[]
    // -------------------------------------------------------------------------
    showStage(L"Building scene geometry...");
    for (int mi : sortedModelIdx)
    {
        if (instanceIndex >= MAX_SCENE_MODELS) break;

        const FBXModel& fbxModel = fbx.models[mi];

        // Resolve parent slot (for iParentModelID)
        int parentSlot = -1;
        if (fbxModel.parentID != 0)
        {
            auto pit = fbxIDToSlot.find(fbxModel.parentID);
            if (pit != fbxIDToSlot.end()) parentSlot = pit->second;
        }

        // Find the associated geometry
        const FBXGeometry* geom = nullptr;
        if (fbxModel.geometryID != 0)
        {
            auto git = fbx.geometryByID.find(fbxModel.geometryID);
            if (git != fbx.geometryByID.end())
                geom = &fbx.geometries[git->second];
        }

        const bool isMesh = (fbxModel.type == "Mesh" && geom != nullptr && !geom->polygonVertexIndex.empty());

        // Warn if a Mesh-type model has no resolved geometry -- indicates a connection resolution failure
        if (!isMesh && fbxModel.type == "Mesh")
        {
            std::wstring wn(fbxModel.name.begin(), fbxModel.name.end());
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                (L"[SceneManager] ParseFBXScene: '" + wn +
                 L"' type=Mesh but geomID=" + std::to_wstring(fbxModel.geometryID) +
                 L" geom=" + std::wstring(geom ? L"found" : L"null") +
                 L" pvi=" + std::to_wstring(geom ? geom->polygonVertexIndex.size() : 0) +
                 L" -- no geometry resolved; treated as transform-only").c_str());
        }

        // Find or create a slot in models[] by FBX model name first (supports cache.dat restore
        // and scene revisit where the slot is already occupied with bGpuReady data).
        // Falls back to a slot with an empty name if no existing slot matches.
        int modelSlot = -1;
        int firstEmpty = -1;
        {
            std::wstring wfbxName(fbxModel.name.begin(), fbxModel.name.end());
            for (int ms = 0; ms < MAX_MODELS; ++ms)
            {
                if (models[ms].m_modelInfo.name == wfbxName &&
                    (models[ms].m_modelInfo.sourceSceneFile.empty() ||
                     models[ms].m_modelInfo.sourceSceneFile == fbxFile))
                {
                    modelSlot = ms;
                    break;
                }
                if (firstEmpty < 0 && models[ms].m_modelInfo.name.empty())
                    firstEmpty = ms;
            }
            if (modelSlot < 0 && firstEmpty >= 0)
                modelSlot = firstEmpty;
        }
        if (modelSlot < 0)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[SceneManager] ParseFBXScene(): models[] pool exhausted.");
            break;
        }

        Model& mdl = models[modelSlot];
        mdl.m_modelInfo = ModelInfo{};
        mdl.m_modelInfo.ID              = instanceIndex;
        mdl.m_modelInfo.iParentModelID  = parentSlot;
        mdl.m_modelInfo.gltfNodeIndex   = mi;          // Reused for FBX model index
        mdl.m_modelInfo.name            = std::wstring(fbxModel.name.begin(), fbxModel.name.end());
        mdl.m_modelInfo.sourceSceneFile = fbxFile;

        // Local TRS from FBX transform (converted to engine coordinate space)
        {
            // Translation
            XMFLOAT3 t = fbxModel.transform.translation;
            // Apply coord flip (Z-up or Y-up -> engine LH Y-up)
            if (fbx.upAxis == 2)
                t = XMFLOAT3(t.x, t.z, -t.y);
            else
                t = XMFLOAT3(t.x, t.y, -t.z);
            mdl.m_modelInfo.baseLocalTranslation = t;
            mdl.m_modelInfo.animLocalTranslation  = t;

            // Scale (no axis flip on scale)
            mdl.m_modelInfo.baseLocalScale = fbxModel.transform.scale;
            mdl.m_modelInfo.animLocalScale = fbxModel.transform.scale;

            // Rotation: Euler (FBX RH space) -> matrix -> flip to engine LH -> quaternion
            XMMATRIX rotM = m_fbxImporter.EulerToMatrix(fbxModel.transform.rotationEuler,
                                                          fbxModel.transform.rotationOrder);
            rotM = m_fbxImporter.ApplyRotationFlip(rotM); // RH -> LH coordinate space
            XMVECTOR q = XMQuaternionRotationMatrix(rotM);
            XMFLOAT4 qf;
            XMStoreFloat4(&qf, q);
            mdl.m_modelInfo.baseLocalRotationQuat = qf;
            mdl.m_modelInfo.animLocalRotationQuat  = qf;
            mdl.m_modelInfo.bHasBaseLocalTRS = true;

            // World matrix: combine local TRS
            XMMATRIX world = XMMatrixScaling(
                    fbxModel.transform.scale.x,
                    fbxModel.transform.scale.y,
                    fbxModel.transform.scale.z) *
                XMMatrixRotationQuaternion(q) *
                XMMatrixTranslation(t.x, t.y, t.z);

            // If there is a parent, multiply by parent world matrix
            if (parentSlot >= 0 && scene_models[parentSlot].m_isLoaded)
                world = world * scene_models[parentSlot].m_modelInfo.worldMatrix;

            mdl.m_modelInfo.worldMatrix = world;
        }

        // Copy to scene_models slot
        scene_models[instanceIndex].m_modelInfo = mdl.m_modelInfo;

        #if defined(_DEBUG_SCENEMANAGER_)
        {
            XMFLOAT4X4 wm; XMStoreFloat4x4(&wm, mdl.m_modelInfo.worldMatrix);
            const XMFLOAT3& lt = mdl.m_modelInfo.baseLocalTranslation;
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"[SceneManager] FBX slot[%d] '%hs' type='%hs' isMesh=%hs | "
                L"lclT=(%.3f,%.3f,%.3f) scale=(%.3f,%.3f,%.3f) worldPos=(%.3f,%.3f,%.3f) parent=%d",
                instanceIndex,
                fbxModel.name.c_str(), fbxModel.type.c_str(), isMesh ? "yes" : "no",
                lt.x, lt.y, lt.z,
                fbxModel.transform.scale.x, fbxModel.transform.scale.y, fbxModel.transform.scale.z,
                wm._41, wm._42, wm._43,
                parentSlot);
        }
        #endif

        if (!isMesh)
        {
            // Transform-only node (empty, camera, light, null, or no geometry)
            scene_models[instanceIndex].m_modelInfo.bIsTransformOnly  = true;
            scene_models[instanceIndex].m_modelInfo.bIsTransformProxy = true;
            scene_models[instanceIndex].m_isLoaded = true;
            mdl.m_modelInfo.bGpuReady           = false;
            mdl.m_modelInfo.cachedInstanceIndex = instanceIndex;
            fbxIDToSlot[fbxModel.id] = instanceIndex;
            scene_models[instanceIndex].m_modelInfo.importType   = ImportType::FBX;
            scene_models[instanceIndex].m_modelInfo.fbxNodeIndex = instanceIndex;
            scene_models[instanceIndex].m_modelInfo.fbxNodeName  = fbxModel.name;
            ++instanceIndex;
            continue;
        }

        // ---- Mesh: triangulate geometry (splits into per-material sub-meshes) ----
        std::vector<Vertex>   verts;
        std::vector<uint32_t> idx;
        std::vector<int32_t>  triMatIdx;
        if (!m_fbxImporter.TriangulateGeometry(*geom, verts, idx, triMatIdx))
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                (L"[SceneManager] ParseFBXScene(): triangulation failed for '" +
                 mdl.m_modelInfo.name + L"' -- skipping mesh.").c_str());
            scene_models[instanceIndex].m_modelInfo.bIsTransformOnly  = true;
            scene_models[instanceIndex].m_modelInfo.bIsTransformProxy = true;
            scene_models[instanceIndex].m_isLoaded = true;
            fbxIDToSlot[fbxModel.id] = instanceIndex;
            scene_models[instanceIndex].m_modelInfo.importType   = ImportType::FBX;
            scene_models[instanceIndex].m_modelInfo.fbxNodeIndex = instanceIndex;
            scene_models[instanceIndex].m_modelInfo.fbxNodeName  = fbxModel.name;
            ++instanceIndex;
            continue;
        }

        // Collect unique material slots in first-appearance order
        std::vector<int32_t> uniqueMatSlots;
        {
            std::unordered_set<int32_t> seenSlots;
            for (int32_t s : triMatIdx)
            {
                if (seenSlots.insert(s).second)
                    uniqueMatSlots.push_back(s);
            }
            if (uniqueMatSlots.empty())
                uniqueMatSlots.push_back(0);
        }

        // One scene_models / models entry per unique material slot
        const int firstSubMeshInstIdx = instanceIndex;
        bool      firstSubMesh        = true;
        for (int32_t matSlot : uniqueMatSlots)
        {
            if (instanceIndex >= MAX_SCENE_MODELS) break;

            int          subInstIdx = instanceIndex;
            int          subMdlSlot = modelSlot;
            std::wstring subName    = mdl.m_modelInfo.name;

            if (!firstSubMesh)
            {
                // Find a free models[] slot for this additional sub-mesh
                subMdlSlot = -1;
                for (int ms = 0; ms < MAX_MODELS; ++ms)
                {
                    if (models[ms].m_modelInfo.name.empty())
                    {
                        subMdlSlot = ms;
                        break;
                    }
                }
                if (subMdlSlot < 0)
                {
                    debug.logLevelMessage(LogLevel::LOG_ERROR,
                        L"[SceneManager] ParseFBXScene(): models[] pool exhausted for sub-mesh.");
                    break;
                }

                subName = mdl.m_modelInfo.name + L"_mat" + std::to_wstring(matSlot);

                // Fresh ModelInfo: copy only transform/metadata from the base mesh
                ModelInfo subInfo{};
                subInfo.ID             = subInstIdx;
                subInfo.name           = subName;
                subInfo.iParentModelID = firstSubMeshInstIdx;
                subInfo.gltfNodeIndex  = -1;
                subInfo.sourceSceneFile        = mdl.m_modelInfo.sourceSceneFile;
                subInfo.worldMatrix            = mdl.m_modelInfo.worldMatrix;
                subInfo.baseLocalTranslation   = mdl.m_modelInfo.baseLocalTranslation;
                subInfo.animLocalTranslation   = mdl.m_modelInfo.animLocalTranslation;
                subInfo.baseLocalScale         = mdl.m_modelInfo.baseLocalScale;
                subInfo.animLocalScale         = mdl.m_modelInfo.animLocalScale;
                subInfo.baseLocalRotationQuat  = mdl.m_modelInfo.baseLocalRotationQuat;
                subInfo.animLocalRotationQuat  = mdl.m_modelInfo.animLocalRotationQuat;
                subInfo.bHasBaseLocalTRS       = mdl.m_modelInfo.bHasBaseLocalTRS;
                subInfo.fxActive               = fbxModel.castShadow ? 1 : 0;
                scene_models[subInstIdx].m_modelInfo = subInfo;
                models[subMdlSlot].m_modelInfo       = subInfo;
            }

            // Extract geometry triangles that belong to this matSlot
            {
                std::unordered_map<uint32_t, uint32_t> vertRemap;
                std::vector<Vertex>   subVerts;
                std::vector<uint32_t> subIdx;
                const size_t numTris = triMatIdx.size();
                for (size_t t = 0; t < numTris; ++t)
                {
                    if (triMatIdx[t] != matSlot) continue;
                    for (int vi = 0; vi < 3; ++vi)
                    {
                        uint32_t origIdx = idx[t * 3 + vi];
                        auto it = vertRemap.find(origIdx);
                        if (it == vertRemap.end())
                        {
                            uint32_t newVtx            = static_cast<uint32_t>(subVerts.size());
                            vertRemap[origIdx]         = newVtx;
                            subVerts.push_back(verts[origIdx]);
                            subIdx.push_back(newVtx);
                        }
                        else
                        {
                            subIdx.push_back(it->second);
                        }
                    }
                }
                scene_models[subInstIdx].m_modelInfo.vertices = std::move(subVerts);
                scene_models[subInstIdx].m_modelInfo.indices  = std::move(subIdx);
                models[subMdlSlot].m_modelInfo.vertices       = scene_models[subInstIdx].m_modelInfo.vertices;
                models[subMdlSlot].m_modelInfo.indices        = scene_models[subInstIdx].m_modelInfo.indices;
            }

            // ---- Material for this sub-mesh ----
            const FBXMaterial* fbxMatPtr = nullptr;
            if (matSlot < static_cast<int32_t>(fbxModel.materialIDs.size()))
            {
                auto matIt = fbx.materialByID.find(fbxModel.materialIDs[matSlot]);
                if (matIt != fbx.materialByID.end())
                    fbxMatPtr = &fbx.materials[matIt->second];
            }

            // engineMat lives at this scope so the Vulkan GPU-upload step after
            // SetupModelForRendering can push its values into the material UBO and
            // texture descriptor set (mirrors the GLTF post-setup bind).
            Material engineMat;
            bool     hasEngineMat = false;

            if (fbxMatPtr)
            {
                m_fbxImporter.BuildMaterial(*fbxMatPtr, baseDir, engineMat);
                hasEngineMat = true;

                // No diffuse texture -- create a WHITE 1x1 solid-colour stand-in.
                // Actual diffuse colour is carried by Kd in the material constant buffer.
                // Shader: albedoColor = sample(white) x Kd = Kd (no double-apply)
                // Done BEFORE the material is stored in m_materials so that every
                // pipeline (DX/OpenGL/Vulkan) sees the final material including the
                // stand-in texture and its opacity-carrying alpha.
                if (!engineMat.diffuseTexture)
                {
                    auto fb = std::make_shared<Texture>();
                    if (fb->CreateSolidColorTexture(1, 1, XMFLOAT4(1.0f, 1.0f, 1.0f, fbxMatPtr->opacity)))
                    {
                        engineMat.diffuseTexture  = fb;
                        engineMat.diffuseMapPath  = "SOLID_COLOR";  // used by useDiffuseMap check below
                    }
                }

                std::string matName = fbxMatPtr->name;
                scene_models[subInstIdx].m_modelInfo.materials.push_back(matName);
                models[subMdlSlot].m_modelInfo.materials.push_back(matName);
                scene_models[subInstIdx].m_materials[matName] = engineMat;
                models[subMdlSlot].m_materials[matName]       = engineMat;

                scene_models[subInstIdx].m_modelInfo.metallic  = engineMat.Metallic;
                scene_models[subInstIdx].m_modelInfo.roughness = engineMat.Roughness;
                models[subMdlSlot].m_modelInfo.metallic        = engineMat.Metallic;
                models[subMdlSlot].m_modelInfo.roughness       = engineMat.Roughness;

                // ---- UV settings (FBX texture node) ----
                // Wrap modes feed the per-renderer diffuse sampler (DX11/DX12
                // sampler desc, OpenGL glTexParameteri, Vulkan wrap-aware sampler).
                scene_models[subInstIdx].m_modelInfo.uvWrapU = engineMat.uvWrapU;
                scene_models[subInstIdx].m_modelInfo.uvWrapV = engineMat.uvWrapV;
                models[subMdlSlot].m_modelInfo.uvWrapU       = engineMat.uvWrapU;
                models[subMdlSlot].m_modelInfo.uvWrapV       = engineMat.uvWrapV;

                // Bake the texture's UV transform into this sub-mesh's UVs so it
                // applies on every pipeline with no shader changes (same approach
                // as the GLTF KHR_texture_transform bake): scale → rotate → translate.
                if (engineMat.uvTranslationU != 0.0f || engineMat.uvTranslationV != 0.0f ||
                    engineMat.uvScalingU     != 1.0f || engineMat.uvScalingV     != 1.0f ||
                    engineMat.uvRotationDeg  != 0.0f)
                {
                    const float uvRotRad = engineMat.uvRotationDeg * (3.14159265358979323846f / 180.0f);
                    const float cr = cosf(uvRotRad);
                    const float sr = sinf(uvRotRad);
                    for (auto& uvV : scene_models[subInstIdx].m_modelInfo.vertices)
                    {
                        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            float u0 = uvV.texCoord.x * engineMat.uvScalingU;
                            float v0 = uvV.texCoord.y * engineMat.uvScalingV;
                            uvV.texCoord.x =  cr * u0 + sr * v0 + engineMat.uvTranslationU;
                            uvV.texCoord.y = -sr * u0 + cr * v0 + engineMat.uvTranslationV;
                        #else
                            float u0 = uvV.texCoord[0] * engineMat.uvScalingU;
                            float v0 = uvV.texCoord[1] * engineMat.uvScalingV;
                            uvV.texCoord[0] =  cr * u0 + sr * v0 + engineMat.uvTranslationU;
                            uvV.texCoord[1] = -sr * u0 + cr * v0 + engineMat.uvTranslationV;
                        #endif
                    }
                    // Keep the models[] cache copy in sync with the baked UVs.
                    models[subMdlSlot].m_modelInfo.vertices = scene_models[subInstIdx].m_modelInfo.vertices;

                    #if defined(_DEBUG_SCENEMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"[SceneManager] FBX UV transform baked for '%ls' (T=%.3f,%.3f S=%.3f,%.3f R=%.2fdeg)",
                            subName.c_str(), engineMat.uvTranslationU, engineMat.uvTranslationV,
                            engineMat.uvScalingU, engineMat.uvScalingV, engineMat.uvRotationDeg);
                    #endif
                }

                // Register textures in shared list
                auto addTex = [&](std::shared_ptr<Texture> t)
                {
                    if (!t) return;
                    scene_models[subInstIdx].m_modelInfo.textures.push_back(t);
                    models[subMdlSlot].m_modelInfo.textures.push_back(t);
                };
                addTex(engineMat.diffuseTexture);
                addTex(engineMat.normalMap);
                addTex(engineMat.roughnessMap);
                addTex(engineMat.metallicMap);
                addTex(engineMat.aoMap);

                // Populate DX SRV slots required by SetupModelForRendering
                #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    if (engineMat.diffuseTexture)
                    {
                        scene_models[subInstIdx].m_modelInfo.textureSRVs.push_back(engineMat.diffuseTexture->GetSRV());
                        models[subMdlSlot].m_modelInfo.textureSRVs.push_back(engineMat.diffuseTexture->GetSRV());
                    }
                    if (engineMat.normalMap)
                    {
                        scene_models[subInstIdx].m_modelInfo.normalMapSRVs.push_back(engineMat.normalMap->GetSRV());
                        models[subMdlSlot].m_modelInfo.normalMapSRVs.push_back(engineMat.normalMap->GetSRV());
                    }
                    if (engineMat.roughnessMap)
                    {
                        scene_models[subInstIdx].m_modelInfo.roughnessMapSRV = engineMat.roughnessMap->GetSRV();
                        models[subMdlSlot].m_modelInfo.roughnessMapSRV       = engineMat.roughnessMap->GetSRV();
                    }
                    if (engineMat.metallicMap)
                    {
                        scene_models[subInstIdx].m_modelInfo.metallicMapSRV = engineMat.metallicMap->GetSRV();
                        models[subMdlSlot].m_modelInfo.metallicMapSRV       = engineMat.metallicMap->GetSRV();
                    }
                    if (engineMat.aoMap)
                    {
                        scene_models[subInstIdx].m_modelInfo.aoMapSRV = engineMat.aoMap->GetSRV();
                        models[subMdlSlot].m_modelInfo.aoMapSRV       = engineMat.aoMap->GetSRV();
                    }
                #endif

                // useDiffuseMap: true = real texture at t0; false = shader uses Kd directly.
                // Set for ALL pipelines -- the DX shaders read it from the material constant
                // buffer and the Vulkan material UBO upload below reads it from ModelInfo.
                {
                    bool hasTex = (engineMat.diffuseMapPath != "SOLID_COLOR" && !engineMat.diffuseMapPath.empty());
                    scene_models[subInstIdx].m_modelInfo.useDiffuseMap = hasTex;
                    models[subMdlSlot].m_modelInfo.useDiffuseMap       = hasTex;
                }

                // PBR map flags drive matData.useORM / matData.useAO in the Vulkan material
                // UBO and the equivalent constant-buffer flags on the other pipelines.
                {
                    bool hasMetallic  = (engineMat.metallicMap  != nullptr);
                    bool hasRoughness = (engineMat.roughnessMap != nullptr);
                    bool hasAO        = (engineMat.aoMap        != nullptr);
                    scene_models[subInstIdx].m_modelInfo.useMetallicMap  = hasMetallic;
                    models[subMdlSlot].m_modelInfo.useMetallicMap        = hasMetallic;
                    scene_models[subInstIdx].m_modelInfo.useRoughnessMap = hasRoughness;
                    models[subMdlSlot].m_modelInfo.useRoughnessMap       = hasRoughness;
                    scene_models[subInstIdx].m_modelInfo.useAOMap        = hasAO;
                    models[subMdlSlot].m_modelInfo.useAOMap              = hasAO;
                }

                #if defined(_DEBUG_SCENEMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_INFO,
                        L"[SceneManager] FBX material slot=%d '%hs' -> sub-mesh '%ls'",
                        matSlot, matName.c_str(), subName.c_str());
                #endif
            }

            // Shadow flag
            scene_models[subInstIdx].m_modelInfo.fxActive = fbxModel.castShadow ? 1 : 0;

            // ---- GPU upload ----
            scene_models[subInstIdx].ApplyDefaultLightingFromManager(lightsManager);
            if (!scene_models[subInstIdx].SetupModelForRendering(subInstIdx))
            {
                debug.logLevelMessage(LogLevel::LOG_ERROR,
                    (L"[SceneManager] ParseFBXScene(): SetupModelForRendering failed for '" +
                     subName + L"'").c_str());
            }
            else
            {
                #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                    models[subMdlSlot].m_modelInfo.vertexBuffer        = scene_models[subInstIdx].m_modelInfo.vertexBuffer;
                    models[subMdlSlot].m_modelInfo.indexBuffer         = scene_models[subInstIdx].m_modelInfo.indexBuffer;
                    models[subMdlSlot].m_modelInfo.constantBuffer      = scene_models[subInstIdx].m_modelInfo.constantBuffer;
                    models[subMdlSlot].m_modelInfo.lightConstantBuffer = scene_models[subInstIdx].m_modelInfo.lightConstantBuffer;
                    models[subMdlSlot].m_modelInfo.materialBuffer      = scene_models[subInstIdx].m_modelInfo.materialBuffer;
                    models[subMdlSlot].m_modelInfo.samplerState        = scene_models[subInstIdx].m_modelInfo.samplerState;
                    models[subMdlSlot].m_modelInfo.textureSRVs         = scene_models[subInstIdx].m_modelInfo.textureSRVs;
                    models[subMdlSlot].m_modelInfo.normalMapSRVs       = scene_models[subInstIdx].m_modelInfo.normalMapSRVs;
                #elif defined(__USE_VULKAN__)
                    models[subMdlSlot].m_modelInfo.vertexBuffer                = scene_models[subInstIdx].m_modelInfo.vertexBuffer;
                    models[subMdlSlot].m_modelInfo.vertexBufferMemory          = scene_models[subInstIdx].m_modelInfo.vertexBufferMemory;
                    models[subMdlSlot].m_modelInfo.indexBuffer                 = scene_models[subInstIdx].m_modelInfo.indexBuffer;
                    models[subMdlSlot].m_modelInfo.indexBufferMemory           = scene_models[subInstIdx].m_modelInfo.indexBufferMemory;
                    models[subMdlSlot].m_modelInfo.uniformBuffer               = scene_models[subInstIdx].m_modelInfo.uniformBuffer;
                    models[subMdlSlot].m_modelInfo.uniformBufferMemory         = scene_models[subInstIdx].m_modelInfo.uniformBufferMemory;
                    models[subMdlSlot].m_modelInfo.uniformBufferMapped         = scene_models[subInstIdx].m_modelInfo.uniformBufferMapped;
                    models[subMdlSlot].m_modelInfo.materialUniformBuffer       = scene_models[subInstIdx].m_modelInfo.materialUniformBuffer;
                    models[subMdlSlot].m_modelInfo.materialUniformBufferMemory = scene_models[subInstIdx].m_modelInfo.materialUniformBufferMemory;
                    models[subMdlSlot].m_modelInfo.materialUniformBufferMapped = scene_models[subInstIdx].m_modelInfo.materialUniformBufferMapped;
                    models[subMdlSlot].m_modelInfo.pipeline                    = scene_models[subInstIdx].m_modelInfo.pipeline;
                    models[subMdlSlot].m_modelInfo.pipelineLayout              = scene_models[subInstIdx].m_modelInfo.pipelineLayout;
                    models[subMdlSlot].m_modelInfo.descriptorSet               = scene_models[subInstIdx].m_modelInfo.descriptorSet;
                    models[subMdlSlot].m_modelInfo.textureDescriptorSet        = scene_models[subInstIdx].m_modelInfo.textureDescriptorSet;

                    // SetupModelForRendering created the material UBO and texture descriptor
                    // set with engine DEFAULTS (white diffuse, flat normal).  Upload the real
                    // FBX material values and texture views now -- mirrors the post-setup
                    // BindGLTFMaterialTexturesToModel call in the GLTF path.  Without this,
                    // FBX models render with the white fallback material on Vulkan.
                    // models[subMdlSlot] shares the same mapped UBO pointer and descriptor
                    // set handles (copied above), so one upload covers both entries.
                    if (hasEngineMat)
                        UploadFBXMaterialToVulkanModel(engineMat, scene_models[subInstIdx].m_modelInfo);
                #elif defined(__USE_OPENGL__)
                    models[subMdlSlot].m_modelInfo.VAO           = scene_models[subInstIdx].m_modelInfo.VAO;
                    models[subMdlSlot].m_modelInfo.VBO           = scene_models[subInstIdx].m_modelInfo.VBO;
                    models[subMdlSlot].m_modelInfo.EBO           = scene_models[subInstIdx].m_modelInfo.EBO;
                    models[subMdlSlot].m_modelInfo.shaderProgram = scene_models[subInstIdx].m_modelInfo.shaderProgram;
                #endif

                models[subMdlSlot].m_modelInfo.bGpuReady          = true;
                models[subMdlSlot].m_modelInfo.cachedInstanceIndex = subInstIdx;
                models[subMdlSlot].bInitialized                    = true;
                models[subMdlSlot].m_isLoaded                      = true;
            }

            scene_models[subInstIdx].bInitialized = true;
            scene_models[subInstIdx].m_isLoaded   = true;

            // First sub-mesh owns the FBX model ID mapping; extras are its dependants
            if (firstSubMesh)
            {
                fbxIDToSlot[fbxModel.id] = subInstIdx;
                firstSubMesh = false;
            }
            // Tag every sub-mesh so ModelAnimator can dispatch to FBXAnimator
            scene_models[subInstIdx].m_modelInfo.importType   = ImportType::FBX;
            scene_models[subInstIdx].m_modelInfo.fbxNodeIndex = subInstIdx;
            scene_models[subInstIdx].m_modelInfo.fbxNodeName  = fbxModel.name;

            ++instanceIndex;

            #if defined(_DEBUG_SCENEMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO,
                    L"[SceneManager] FBX sub-mesh '%ls' matSlot=%d -> scene_models[%d] verts=%d idx=%d parent=%d",
                    subName.c_str(), matSlot, subInstIdx,
                    static_cast<int>(scene_models[subInstIdx].m_modelInfo.vertices.size()),
                    static_cast<int>(scene_models[subInstIdx].m_modelInfo.indices.size()),
                    parentSlot);
            #endif
        } // end per-material sub-mesh loop
    }

    // -------------------------------------------------------------------------
    // 5. Animations
    // -------------------------------------------------------------------------
    showStage(L"Loading animations...");
    bAnimationsLoaded = false;
    if (!fbx.animStacks.empty())
    {
        // Parse animations natively via FBXAnimator (no GLTF conversion needed)
        modelAnimator.fbxAnimator.ClearAllAnimations();
        if (modelAnimator.fbxAnimator.ParseAnimationsFromFBX(fbx, fbxIDToSlot))
        {
            int clipCount = modelAnimator.fbxAnimator.GetAnimationCount();
            bAnimationsLoaded = (clipCount > 0);

            if (bAnimationsLoaded)
            {
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    (std::wstring(L"[SceneManager] ParseFBXScene(): ") +
                     std::to_wstring(clipCount) + L" animation clip(s) loaded from FBX.").c_str());

                // Auto-start each clip on the root model of its first channel
                for (int animIdx = 0; animIdx < clipCount; ++animIdx)
                {
                    const FBXAnimationClip* clip = modelAnimator.fbxAnimator.GetClip(animIdx);
                    if (!clip || clip->channels.empty()) continue;

                    // Walk up the parent chain from the first channel's slot to find the root
                    int rootSlot = clip->channels[0].targetModelSlot;
                    while (rootSlot >= 0 && rootSlot < MAX_SCENE_MODELS)
                    {
                        int par = scene_models[rootSlot].m_modelInfo.iParentModelID;
                        if (par < 0 || !scene_models[par].m_isLoaded) break;
                        rootSlot = par;
                    }

                    modelAnimator.fbxAnimator.CreateAnimationInstance(animIdx, rootSlot);
                    modelAnimator.fbxAnimator.SetAnimationLooping(rootSlot, true);
                    modelAnimator.fbxAnimator.SetAnimationSpeed(rootSlot, 1.0f);
                    modelAnimator.fbxAnimator.StartAnimation(rootSlot, animIdx);

                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        (std::wstring(L"[SceneManager] FBX clip '") +
                         std::wstring(clip->name.begin(), clip->name.end()) +
                         L"' started on root slot " + std::to_wstring(rootSlot)).c_str());
                }
            }
        }
        else
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[SceneManager] ParseFBXScene(): FBXAnimator::ParseAnimationsFromFBX() failed.");
        }
    }

    // -------------------------------------------------------------------------
    // 6. Done
    // -------------------------------------------------------------------------
    #if defined(_DEBUG_SCENEMANAGER_)
    {
        auto _t1  = std::chrono::high_resolution_clock::now();
        auto _ms  = std::chrono::duration_cast<std::chrono::milliseconds>(_t1 - _t0).count();
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[SceneManager] ParseFBXScene() COMPLETE in %lld ms -- %d model instances",
            _ms, instanceIndex);
    }
    #endif

    // Cache-only mode: GPU resources are now in models[] only.
    // Clear all populated scene_models[] entries so the renderer skips them.
    // (Dynamic scenes assemble their visible set at runtime via PutModelToScene.)
    if (bCacheOnly && instanceIndex > 0)
    {
        for (int i = 0; i < instanceIndex; ++i)
        {
            scene_models[i].m_modelInfo  = ModelInfo{};
            scene_models[i].m_materials.clear();
            scene_models[i].m_isLoaded   = false;
            scene_models[i].bInitialized = false;
        }
        debug.logLevelMessage(LogLevel::LOG_INFO,
            (L"[SceneManager] ParseFBXScene() cache-only mode -- " +
             std::to_wstring(instanceIndex) + L" model(s) cached in models[], scene_models[] cleared.").c_str());
    }

    debug.logLevelMessage(LogLevel::LOG_INFO,
        (L"[SceneManager] ParseFBXScene(): loaded " + std::to_wstring(instanceIndex) +
         L" model(s) from '" + fbxFile + L"'").c_str());

    return instanceIndex > 0;
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
            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                XMVECTOR pos = XMLoadFloat3(&v.position);
            #else
                XMVECTOR pos = XMVectorSet(v.position[0], v.position[1], v.position[2], 0.0f);
            #endif

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
            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                myRenderer->myCamera.SetTarget(center);
            #else
                myRenderer->myCamera.SetTarget(glm::vec3(center.x, center.y, center.z));
            #endif
            
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
    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        XMFLOAT3 camPos    = myRenderer->myCamera.GetPosition();
        XMFLOAT3 camTarget = myRenderer->myCamera.target;
    #else
        glm::vec3 _gp = myRenderer->myCamera.GetPosition();
        glm::vec3 _gt = myRenderer->myCamera.target;
        XMFLOAT3 camPos    = { _gp.x, _gp.y, _gp.z };
        XMFLOAT3 camTarget = { _gt.x, _gt.y, _gt.z };
    #endif

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
        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            myRenderer->myCamera.SetTarget(camTarget);
        #else
            myRenderer->myCamera.SetTarget(glm::vec3(camTarget.x, camTarget.y, camTarget.z));
        #endif
        
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
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[SceneManager] Skipping model \"%ls\" - not found in base models[]", modelName.c_str());
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
    // Only update animations if they were successfully loaded from the current scene
    if (bAnimationsLoaded)
    {
        // Update all animations -- dispatches to GLTFAnimator or FBXAnimator via ModelAnimator
        modelAnimator.UpdateAnimations(deltaTime);
    }
}

// --------------------------------------------------------------------------------------------------
// SceneManager::FindParentModelIDForAnimation()
// Primary: matches the animation's name (set in Blender) against root model names via
// FindParentModelID - the user names the animation to match its owning model or armature.
// Fallback: for armature animations whose channels target bone child-nodes rather than the
// armature root, scans channel targetNodeIndex against each model's gltfNodeIndex and walks
// up the parent chain to reach the root. Returns root model array index, or -1 if not found.
// --------------------------------------------------------------------------------------------------
int SceneManager::FindParentModelIDForAnimation(int animationIndex)
{
    const GLTFAnimation* anim = modelAnimator.gltfAnimator.GetAnimation(animationIndex);
    if (!anim || anim->channels.empty())
        return -1;

    // Primary: use the animation name the user set in Blender to find the owning root model
    if (!anim->name.empty())
    {
        int parentID = FindParentModelID(anim->name);
        if (parentID >= 0)
            return parentID;
    }

    // Fallback: armature animations target bone nodes - scan channel node indices and walk
    // up the parent chain to the root (armature or mesh root)
    for (const auto& channel : anim->channels)
    {
        if (channel.targetNodeIndex < 0)
            continue;

        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            if (!scene_models[i].m_isLoaded)
                continue;
            if (scene_models[i].m_modelInfo.gltfNodeIndex != channel.targetNodeIndex)
                continue;

            int modelIdx = i;
            while (scene_models[modelIdx].m_modelInfo.iParentModelID != -1)
            {
                int pid = scene_models[modelIdx].m_modelInfo.iParentModelID;
                if (pid < 0 || pid >= MAX_SCENE_MODELS || !scene_models[pid].m_isLoaded)
                    break;
                modelIdx = pid;
            }
            return modelIdx;
        }
    }
    return -1;
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

    // Model not found in scene_models array - log once per unique name to avoid per-frame spam
    #if defined(_DEBUG_SCENEMANAGER_)
        static std::unordered_set<std::wstring> s_notFoundLogged;
        if (s_notFoundLogged.find(modelName) == s_notFoundLogged.end())
        {
            s_notFoundLogged.insert(modelName);
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"[SceneManager] Model \"%ls\" not found in scene_models array (logged once)", modelName.c_str());
        }
    #endif

    return -1;  // Return -1 to indicate model not found
}

// --------------------------------------------------------------------------------------------------
// SceneManager::PutModelToScene()
// Retrieves a named model from the global models[] cache and injects it into scene_models[].
// Validation: model must be GPU-ready (bGpuReady), loaded (m_isLoaded), initialized
// (bInitialized), and not destroyed (bIsDestroyed == false) - equivalent to IsActive() + bGpuReady.
// If bIncChildren is true, all primitive siblings (iParentModelID == root's cachedInstanceIndex)
// are also copied alongside the root, with their parent ID re-pointed to the new scene slot.
// The entire group is positioned at atWorldCoords by overriding the world matrix translation.
// If bStartAnim is true, a new animation instance is created and started on the parent scene model.
// Returns the new parent scene_models[] ID, or -1 on failure.
// --------------------------------------------------------------------------------------------------
int SceneManager::PutModelToScene(std::wstring name, XMFLOAT3 atWorldCoords, bool bIncChildren, bool bStartAnim)
{
    // --- Step 1: Locate the named root model in the global models[] cache ---
    // Accept the first model that matches by name, is GPU-ready, and passes IsActive().
    // Prefer iParentModelID == -1 (scene-graph root); fall back to any match if not found.
    int rootCacheSlot = -1;
    for (int i = 0; i < MAX_MODELS; ++i)
    {
        if (!models[i].IsActive())              continue;   // Must be loaded, initialized, not destroyed
        if (!models[i].m_modelInfo.bGpuReady)   continue;   // Must have valid GPU buffers
        if (models[i].m_modelInfo.name != name)  continue;   // Name must match

        // Prefer scene-graph roots (iParentModelID == -1); keep searching if this is a primitive child
        if (models[i].m_modelInfo.iParentModelID == -1)
        {
            rootCacheSlot = i;
            break;
        }
        if (rootCacheSlot < 0)
            rootCacheSlot = i;   // Fallback: use first name-match even if it's a primitive child
    }

    if (rootCacheSlot < 0)
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[SceneManager::PutModelToScene] Model not found or not GPU-ready in cache: " + name);
        #endif
        return -1;
    }

    // --- Step 2: Collect models[] slots to inject (root + optional primitive siblings) ---
    // Primitive siblings have iParentModelID == the scene_models[] slot (cachedInstanceIndex)
    // that the root occupied on its most recent scene load.
    std::vector<int> cacheSlots;
    cacheSlots.push_back(rootCacheSlot);

    if (bIncChildren)
    {
        int rootCachedIdx = models[rootCacheSlot].m_modelInfo.cachedInstanceIndex;
        if (rootCachedIdx >= 0)
        {
            for (int i = 0; i < MAX_MODELS; ++i)
            {
                if (i == rootCacheSlot)                                 continue;
                if (!models[i].IsActive())                              continue;
                if (!models[i].m_modelInfo.bGpuReady)                  continue;
                if (models[i].m_modelInfo.iParentModelID == rootCachedIdx)
                    cacheSlots.push_back(i);
            }
        }
    }

    // --- Step 3: Find enough free slots in scene_models[] (lowest-index-first) ---
    std::vector<int> freeSlots;
    for (int i = 0; i < MAX_SCENE_MODELS && (int)freeSlots.size() < (int)cacheSlots.size(); ++i)
    {
        if (!scene_models[i].m_isLoaded)
            freeSlots.push_back(i);
    }

    if ((int)freeSlots.size() < (int)cacheSlots.size())
    {
        #if defined(_DEBUG_SCENEMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[SceneManager::PutModelToScene] Not enough free scene_models[] slots for: " + name);
        #endif
        return -1;
    }

    // --- Step 4: Build a new world matrix with the requested position ---
    // Preserve the existing rotation and scale; override only the translation column.
    XMFLOAT4X4 f4x4;
    XMStoreFloat4x4(&f4x4, models[rootCacheSlot].m_modelInfo.worldMatrix);
    f4x4._41 = atWorldCoords.x;
    f4x4._42 = atWorldCoords.y;
    f4x4._43 = atWorldCoords.z;
    XMMATRIX newWorldMatrix = XMLoadFloat4x4(&f4x4);

    int newParentSceneID = freeSlots[0];

    // --- Step 5: Copy each model into its allocated scene_models[] slot ---
    for (int ci = 0; ci < (int)cacheSlots.size(); ++ci)
    {
        int cacheSlot = cacheSlots[ci];
        int sceneSlot = freeSlots[ci];

        // Copy all GPU resources, geometry, materials, and textures from the cache entry.
        // ComPtr/shared_ptr AddRef keeps GPU resources alive in both buffers.
        scene_models[sceneSlot].CopyFrom(models[cacheSlot]);

        // Override world position (matrix translation + position field)
        scene_models[sceneSlot].m_modelInfo.worldMatrix = newWorldMatrix;
        scene_models[sceneSlot].m_modelInfo.position    = atWorldCoords;

        // Assign the new scene slot ID
        scene_models[sceneSlot].m_modelInfo.ID = sceneSlot;

        if (ci == 0)
        {
            // Root: placed independently in the scene, no scene-graph parent
            scene_models[sceneSlot].m_modelInfo.iParentModelID = -1;
        }
        else
        {
            // Primitive siblings: reparent to the new parent scene slot
            scene_models[sceneSlot].m_modelInfo.iParentModelID = newParentSceneID;
        }

        // Establish GPU state for this slot and apply scene lighting
        scene_models[sceneSlot].SetupModelForRendering(sceneSlot);
        scene_models[sceneSlot].ApplyDefaultLightingFromManager(lightsManager);
        scene_models[sceneSlot].m_isLoaded = true;

        // Keep the cache entry's cachedInstanceIndex current
        models[cacheSlot].m_modelInfo.cachedInstanceIndex = sceneSlot;
    }

    // --- Step 6: Start animation on the new parent scene model if requested ---
    if (bStartAnim && bAnimationsLoaded && modelAnimator.gltfAnimator.GetAnimationCount() > 0)
    {
        int animIdx = models[rootCacheSlot].m_modelInfo.iAnimationIndex;
        if (animIdx < 0) animIdx = 0;   // Default to first animation if the model has no explicit index

        modelAnimator.gltfAnimator.CreateAnimationInstance(animIdx, newParentSceneID);
        modelAnimator.gltfAnimator.StartAnimation(newParentSceneID, animIdx);
    }

    #if defined(_DEBUG_SCENEMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[SceneManager::PutModelToScene] Injected \"" + name +
            L"\" into scene_models[" + std::to_wstring(newParentSceneID) +
            L"] with " + std::to_wstring(cacheSlots.size()) + L" primitive(s) at (" +
            std::to_wstring(atWorldCoords.x) + L", " +
            std::to_wstring(atWorldCoords.y) + L", " +
            std::to_wstring(atWorldCoords.z) + L").");
    #endif

    return newParentSceneID;
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

// --------------------------------------------------------------------------------------------------
// Binary I/O helpers - internal to this translation unit.
// --------------------------------------------------------------------------------------------------
namespace {
    // Write a wstring as (uint32_t charCount, wchar_t[charCount]).
    inline void CacheWriteWStr(std::ofstream& f, const std::wstring& s)
    {
        uint32_t n = static_cast<uint32_t>(s.size());
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        if (n) f.write(reinterpret_cast<const char*>(s.data()), n * sizeof(wchar_t));
    }
    inline void CacheReadWStr(std::ifstream& f, std::wstring& s)
    {
        uint32_t n = 0;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        s.resize(n);
        if (n) f.read(reinterpret_cast<char*>(s.data()), n * sizeof(wchar_t));
    }
    // Write a narrow string as (uint32_t byteCount, char[byteCount]).
    inline void CacheWriteStr(std::ofstream& f, const std::string& s)
    {
        uint32_t n = static_cast<uint32_t>(s.size());
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        if (n) f.write(s.data(), n);
    }
    inline void CacheReadStr(std::ifstream& f, std::string& s)
    {
        uint32_t n = 0;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        s.resize(n);
        if (n) f.read(s.data(), n);
    }
}

// Magic + version constants for the cache file header.
static constexpr uint32_t CACHE_MAGIC      = 0x4D444C43u; // 'CLDM'
static constexpr uint32_t CACHE_VERSION    = 1u;

// --------------------------------------------------------------------------------------------------
// SceneManager::SaveCache()
// Serialises the global models[] base pool to a binary cache file so subsequent launches can
// skip the expensive GLTF/GLB parse pass.  Call this before scene.CleanUp() on exit.
// Output is conditional on _DEBUG_SCENEMANAGER_.
// --------------------------------------------------------------------------------------------------
bool SceneManager::SaveCache(const std::string& filepath)
{
    // Count models that carry actual geometry data.
    uint32_t saveCount = 0;
    for (int i = 0; i < MAX_MODELS; ++i)
        if (models[i].m_isLoaded) ++saveCount;

    if (saveCount == 0)
    {
        #if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"[SceneManager] SaveCache: no loaded models to cache - file not written.");
        #endif
        return true;
    }

    // Do not overwrite an existing cache file - if one is already on disk it was
    // written by a previous session and remains valid; let it stand.
    if (std::filesystem::exists(filepath))
    {
        #if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"[SceneManager] SaveCache: cache file already exists - skipping write.");
        #endif
        return true;
    }

    #if defined(PLATFORM_WINDOWS)
        std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
    #elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
        std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
    #elif defined(PLATFORM_APPLE) || defined(PLATFORM_IOS)
        std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
    #else
        std::ofstream f(filepath, std::ios::binary | std::ios::trunc);
    #endif

    if (!f.is_open())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[SceneManager] SaveCache: failed to open '" +
            std::wstring(filepath.begin(), filepath.end()) + L"' for writing.");
        return false;
    }

    // --- File header ---
    const uint32_t vertSz = static_cast<uint32_t>(sizeof(Vertex));
    f.write(reinterpret_cast<const char*>(&CACHE_MAGIC),   sizeof(CACHE_MAGIC));
    f.write(reinterpret_cast<const char*>(&CACHE_VERSION), sizeof(CACHE_VERSION));
    f.write(reinterpret_cast<const char*>(&vertSz),        sizeof(vertSz));
    f.write(reinterpret_cast<const char*>(&saveCount),     sizeof(saveCount));

    // --- Per-model data ---
    for (int i = 0; i < MAX_MODELS; ++i)
    {
        if (!models[i].m_isLoaded) continue;

        const Model&     mdl  = models[i];
        const ModelInfo& info = mdl.m_modelInfo;

        // Slot index so we restore into the same slot on load.
        uint32_t slotIdx = static_cast<uint32_t>(i);
        f.write(reinterpret_cast<const char*>(&slotIdx), sizeof(slotIdx));

        // Integer/flag fields
        int32_t  ID              = info.ID;
        int32_t  parentID        = info.iParentModelID;
        int32_t  gltfNodeIdx     = info.gltfNodeIndex;
        int32_t  cachedInstIdx   = info.cachedInstanceIndex;
        int32_t  iAnimIdx        = info.iAnimationIndex;
        int32_t  fxID_v          = info.fxID;
        uint8_t  bTransOnly      = info.bIsTransformOnly  ? 1u : 0u;
        uint8_t  bTransProxy     = info.bIsTransformProxy ? 1u : 0u;
        uint8_t  bHasBase        = info.bHasBaseLocalTRS  ? 1u : 0u;
        uint8_t  bGpuReady_v     = info.bGpuReady         ? 1u : 0u;
        uint8_t  bFxActive       = info.fxActive          ? 1u : 0u;
        uint8_t  bIsLoaded       = mdl.m_isLoaded         ? 1u : 0u;
        uint8_t  bInited         = mdl.bInitialized       ? 1u : 0u;
        uint8_t  bUseMetallic    = info.useMetallicMap    ? 1u : 0u;
        uint8_t  bUseRoughness   = info.useRoughnessMap   ? 1u : 0u;
        uint8_t  bUseAO          = info.useAOMap          ? 1u : 0u;
        uint8_t  bUseEnv         = info.useEnvironmentMap ? 1u : 0u;

        f.write(reinterpret_cast<const char*>(&ID),            sizeof(ID));
        f.write(reinterpret_cast<const char*>(&parentID),      sizeof(parentID));
        f.write(reinterpret_cast<const char*>(&gltfNodeIdx),   sizeof(gltfNodeIdx));
        f.write(reinterpret_cast<const char*>(&cachedInstIdx), sizeof(cachedInstIdx));
        f.write(reinterpret_cast<const char*>(&iAnimIdx),      sizeof(iAnimIdx));
        f.write(reinterpret_cast<const char*>(&fxID_v),        sizeof(fxID_v));
        f.write(reinterpret_cast<const char*>(&bTransOnly),    sizeof(bTransOnly));
        f.write(reinterpret_cast<const char*>(&bTransProxy),   sizeof(bTransProxy));
        f.write(reinterpret_cast<const char*>(&bHasBase),      sizeof(bHasBase));
        f.write(reinterpret_cast<const char*>(&bGpuReady_v),   sizeof(bGpuReady_v));
        f.write(reinterpret_cast<const char*>(&bFxActive),     sizeof(bFxActive));
        f.write(reinterpret_cast<const char*>(&bIsLoaded),     sizeof(bIsLoaded));
        f.write(reinterpret_cast<const char*>(&bInited),       sizeof(bInited));
        f.write(reinterpret_cast<const char*>(&bUseMetallic),  sizeof(bUseMetallic));
        f.write(reinterpret_cast<const char*>(&bUseRoughness), sizeof(bUseRoughness));
        f.write(reinterpret_cast<const char*>(&bUseAO),        sizeof(bUseAO));
        f.write(reinterpret_cast<const char*>(&bUseEnv),       sizeof(bUseEnv));

        // Transform fields (XMFLOAT3 == Vector3 on all platforms; both are float x,y,z)
        f.write(reinterpret_cast<const char*>(&info.position),       sizeof(XMFLOAT3));
        f.write(reinterpret_cast<const char*>(&info.scale),          sizeof(XMFLOAT3));
        f.write(reinterpret_cast<const char*>(&info.rotation),       sizeof(XMFLOAT3));
        f.write(reinterpret_cast<const char*>(&info.cameraPosition), sizeof(XMFLOAT3));

        f.write(reinterpret_cast<const char*>(&info.baseLocalTranslation),   sizeof(XMFLOAT3));
        f.write(reinterpret_cast<const char*>(&info.baseLocalRotationQuat),  sizeof(XMFLOAT4));
        f.write(reinterpret_cast<const char*>(&info.baseLocalScale),         sizeof(XMFLOAT3));
        f.write(reinterpret_cast<const char*>(&info.animLocalTranslation),   sizeof(XMFLOAT3));
        f.write(reinterpret_cast<const char*>(&info.animLocalRotationQuat),  sizeof(XMFLOAT4));
        f.write(reinterpret_cast<const char*>(&info.animLocalScale),         sizeof(XMFLOAT3));

        // PBR floats - envTint accessed component-by-component for cross-platform safety.
        float envTintArr[3] = { info.envTint.x, info.envTint.y, info.envTint.z };
        f.write(reinterpret_cast<const char*>(&info.metallic),           sizeof(float));
        f.write(reinterpret_cast<const char*>(&info.roughness),          sizeof(float));
        f.write(reinterpret_cast<const char*>(&info.reflectionStrength), sizeof(float));
        f.write(reinterpret_cast<const char*>(&info.envIntensity),       sizeof(float));
        f.write(reinterpret_cast<const char*>(&info.mipLODBias),         sizeof(float));
        f.write(reinterpret_cast<const char*>(&info.fresnel0),           sizeof(float));
        f.write(reinterpret_cast<const char*>(envTintArr),               sizeof(envTintArr));

        // Strings
        CacheWriteWStr(f, info.name);
        CacheWriteWStr(f, info.sourceSceneFile);

        // Geometry: vertices
        uint32_t vCount = static_cast<uint32_t>(info.vertices.size());
        f.write(reinterpret_cast<const char*>(&vCount), sizeof(vCount));
        if (vCount) f.write(reinterpret_cast<const char*>(info.vertices.data()), vCount * sizeof(Vertex));

        // Geometry: indices
        uint32_t iCount = static_cast<uint32_t>(info.indices.size());
        f.write(reinterpret_cast<const char*>(&iCount), sizeof(iCount));
        if (iCount) f.write(reinterpret_cast<const char*>(info.indices.data()), iCount * sizeof(uint32_t));

        // GLTF binary blob
        uint32_t binSz = static_cast<uint32_t>(info.gltfBinaryBuffer.size());
        f.write(reinterpret_cast<const char*>(&binSz), sizeof(binSz));
        if (binSz) f.write(reinterpret_cast<const char*>(info.gltfBinaryBuffer.data()), binSz);

        // Material name list
        uint32_t matCount = static_cast<uint32_t>(info.materials.size());
        f.write(reinterpret_cast<const char*>(&matCount), sizeof(matCount));
        for (const auto& mat : info.materials)
            CacheWriteStr(f, mat);
    }

    f.close();

    #if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[SceneManager] Models cache saved to '" +
            std::wstring(filepath.begin(), filepath.end()) +
            L"' (" + std::to_wstring(saveCount) + L" models written).");
    #endif

    return true;
}

// --------------------------------------------------------------------------------------------------
// SceneManager::LoadCache()
// Deserialises the models[] base pool from the binary cache produced by SaveCache().
// Call this after scene.Initialize() on startup.  If the file is absent the engine falls
// back to a full GLTF/GLB reload (reported via debug output when _DEBUG_SCENEMANAGER_ is set).
// --------------------------------------------------------------------------------------------------
bool SceneManager::LoadCache(const std::string& filepath)
{
    // --- Platform-specific file existence check ---
    #if defined(PLATFORM_WINDOWS)
        bool cacheExists = (GetFileAttributesA(filepath.c_str()) != INVALID_FILE_ATTRIBUTES);
    #elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
        bool cacheExists = (::access(filepath.c_str(), F_OK) == 0);
    #elif defined(PLATFORM_APPLE) || defined(PLATFORM_IOS)
        bool cacheExists = (::access(filepath.c_str(), F_OK) == 0);
    #else
        bool cacheExists = std::filesystem::exists(filepath);
    #endif

    if (!cacheExists)
    {
        #if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[SceneManager] Models cache '" +
                std::wstring(filepath.begin(), filepath.end()) +
                L"' not found - a full model reload is required.");
        #endif
        return false;
    }

    #if defined(PLATFORM_WINDOWS)
        std::ifstream f(filepath, std::ios::binary);
    #elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
        std::ifstream f(filepath, std::ios::binary);
    #elif defined(PLATFORM_APPLE) || defined(PLATFORM_IOS)
        std::ifstream f(filepath, std::ios::binary);
    #else
        std::ifstream f(filepath, std::ios::binary);
    #endif

    if (!f.is_open())
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[SceneManager] LoadCache: failed to open '" +
            std::wstring(filepath.begin(), filepath.end()) + L"'.");
        return false;
    }

    // --- Validate header ---
    uint32_t magic = 0, version = 0, vertSz = 0, loadedCount = 0;
    f.read(reinterpret_cast<char*>(&magic),       sizeof(magic));
    f.read(reinterpret_cast<char*>(&version),     sizeof(version));
    f.read(reinterpret_cast<char*>(&vertSz),      sizeof(vertSz));
    f.read(reinterpret_cast<char*>(&loadedCount), sizeof(loadedCount));

    if (magic != CACHE_MAGIC || version != CACHE_VERSION || vertSz != sizeof(Vertex))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[SceneManager] LoadCache: cache header mismatch (stale or corrupt) - ignoring file.");
        return false;
    }

    #if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[SceneManager] Loading models cache from '" +
            std::wstring(filepath.begin(), filepath.end()) +
            L"' (" + std::to_wstring(loadedCount) + L" models)...");
    #endif

    // --- Read per-model records ---
    for (uint32_t n = 0; n < loadedCount; ++n)
    {
        uint32_t slotIdx = 0;
        f.read(reinterpret_cast<char*>(&slotIdx), sizeof(slotIdx));

        if (static_cast<int>(slotIdx) >= MAX_MODELS)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[SceneManager] LoadCache: slot index out of range - cache is corrupt, aborting.");
            return false;
        }

        Model&     mdl  = models[slotIdx];
        ModelInfo& info = mdl.m_modelInfo;

        // Integer/flag fields
        int32_t  ID, parentID, gltfNodeIdx, cachedInstIdx, iAnimIdx, fxID_v;
        uint8_t  bTransOnly, bTransProxy, bHasBase, bGpuReady_v, bFxActive;
        uint8_t  bIsLoaded, bInited, bUseMetallic, bUseRoughness, bUseAO, bUseEnv;

        f.read(reinterpret_cast<char*>(&ID),            sizeof(ID));
        f.read(reinterpret_cast<char*>(&parentID),      sizeof(parentID));
        f.read(reinterpret_cast<char*>(&gltfNodeIdx),   sizeof(gltfNodeIdx));
        f.read(reinterpret_cast<char*>(&cachedInstIdx), sizeof(cachedInstIdx));
        f.read(reinterpret_cast<char*>(&iAnimIdx),      sizeof(iAnimIdx));
        f.read(reinterpret_cast<char*>(&fxID_v),        sizeof(fxID_v));
        f.read(reinterpret_cast<char*>(&bTransOnly),    sizeof(bTransOnly));
        f.read(reinterpret_cast<char*>(&bTransProxy),   sizeof(bTransProxy));
        f.read(reinterpret_cast<char*>(&bHasBase),      sizeof(bHasBase));
        f.read(reinterpret_cast<char*>(&bGpuReady_v),   sizeof(bGpuReady_v));
        f.read(reinterpret_cast<char*>(&bFxActive),     sizeof(bFxActive));
        f.read(reinterpret_cast<char*>(&bIsLoaded),     sizeof(bIsLoaded));
        f.read(reinterpret_cast<char*>(&bInited),       sizeof(bInited));
        f.read(reinterpret_cast<char*>(&bUseMetallic),  sizeof(bUseMetallic));
        f.read(reinterpret_cast<char*>(&bUseRoughness), sizeof(bUseRoughness));
        f.read(reinterpret_cast<char*>(&bUseAO),        sizeof(bUseAO));
        f.read(reinterpret_cast<char*>(&bUseEnv),       sizeof(bUseEnv));

        info.ID                  = ID;
        info.iParentModelID      = parentID;
        info.gltfNodeIndex       = gltfNodeIdx;
        info.cachedInstanceIndex = cachedInstIdx;
        info.iAnimationIndex     = iAnimIdx;
        info.fxID                = fxID_v;
        info.bIsTransformOnly    = bTransOnly    != 0;
        info.bIsTransformProxy   = bTransProxy   != 0;
        info.bHasBaseLocalTRS    = bHasBase      != 0;
        info.bGpuReady           = false; // GPU objects cannot be serialised; rebuilt on first scene load
        info.fxActive            = bFxActive     != 0;
        info.useMetallicMap      = bUseMetallic  != 0;
        info.useRoughnessMap     = bUseRoughness != 0;
        info.useAOMap            = bUseAO        != 0;
        info.useEnvironmentMap   = bUseEnv       != 0;
        mdl.m_isLoaded           = bIsLoaded     != 0;
        mdl.bInitialized         = bInited       != 0;

        // Transforms
        f.read(reinterpret_cast<char*>(&info.position),       sizeof(XMFLOAT3));
        f.read(reinterpret_cast<char*>(&info.scale),          sizeof(XMFLOAT3));
        f.read(reinterpret_cast<char*>(&info.rotation),       sizeof(XMFLOAT3));
        f.read(reinterpret_cast<char*>(&info.cameraPosition), sizeof(XMFLOAT3));

        f.read(reinterpret_cast<char*>(&info.baseLocalTranslation),   sizeof(XMFLOAT3));
        f.read(reinterpret_cast<char*>(&info.baseLocalRotationQuat),  sizeof(XMFLOAT4));
        f.read(reinterpret_cast<char*>(&info.baseLocalScale),         sizeof(XMFLOAT3));
        f.read(reinterpret_cast<char*>(&info.animLocalTranslation),   sizeof(XMFLOAT3));
        f.read(reinterpret_cast<char*>(&info.animLocalRotationQuat),  sizeof(XMFLOAT4));
        f.read(reinterpret_cast<char*>(&info.animLocalScale),         sizeof(XMFLOAT3));

        // PBR floats
        float envTintArr[3] = {};
        f.read(reinterpret_cast<char*>(&info.metallic),           sizeof(float));
        f.read(reinterpret_cast<char*>(&info.roughness),          sizeof(float));
        f.read(reinterpret_cast<char*>(&info.reflectionStrength), sizeof(float));
        f.read(reinterpret_cast<char*>(&info.envIntensity),       sizeof(float));
        f.read(reinterpret_cast<char*>(&info.mipLODBias),         sizeof(float));
        f.read(reinterpret_cast<char*>(&info.fresnel0),           sizeof(float));
        f.read(reinterpret_cast<char*>(envTintArr),               sizeof(envTintArr));
        info.envTint.x = envTintArr[0];
        info.envTint.y = envTintArr[1];
        info.envTint.z = envTintArr[2];

        // Strings
        CacheReadWStr(f, info.name);
        CacheReadWStr(f, info.sourceSceneFile);

        // Geometry: vertices
        uint32_t vCount = 0;
        f.read(reinterpret_cast<char*>(&vCount), sizeof(vCount));
        info.vertices.resize(vCount);
        if (vCount) f.read(reinterpret_cast<char*>(info.vertices.data()), vCount * sizeof(Vertex));

        // Geometry: indices
        uint32_t iCount = 0;
        f.read(reinterpret_cast<char*>(&iCount), sizeof(iCount));
        info.indices.resize(iCount);
        if (iCount) f.read(reinterpret_cast<char*>(info.indices.data()), iCount * sizeof(uint32_t));

        // GLTF binary blob
        uint32_t binSz = 0;
        f.read(reinterpret_cast<char*>(&binSz), sizeof(binSz));
        info.gltfBinaryBuffer.resize(binSz);
        if (binSz) f.read(reinterpret_cast<char*>(info.gltfBinaryBuffer.data()), binSz);

        // Material name list
        uint32_t matCount = 0;
        f.read(reinterpret_cast<char*>(&matCount), sizeof(matCount));
        info.materials.resize(matCount);
        for (uint32_t m = 0; m < matCount; ++m)
            CacheReadStr(f, info.materials[m]);

        // Re-create GPU objects from cached geometry.
        // COM objects cannot survive to disk; rebuild them now from the vertices/indices
        // we just deserialised so the fast-path has valid buffers to copy on first load.
        // Texture SRVs fall back to solid-colour stand-ins until the first full scene
        // parse re-binds the actual asset textures and writes them back via CopyFrom.
        #if defined(__USE_DIRECTX_11__)
            if (!info.bIsTransformOnly && !info.vertices.empty())
            {
                if (mdl.SetupModelForRendering())
                    info.bGpuReady = true;
            }
        #endif
    }

    f.close();

    #if defined(_DEBUG_SCENEMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[SceneManager] Models cache loaded successfully (" +
            std::to_wstring(loadedCount) + L" models restored from cache).");
    #endif

    return true;
}