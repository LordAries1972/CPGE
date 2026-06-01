#pragma once

#include "Includes.h"
#if defined(__USE_DIRECTX_11__)
    #include "DX11Renderer.h"
#elif defined(__USE_DIRECTX_12__)
    #include "DX12Renderer.h"
#elif defined(__USE_OPENGL__)
    #include "OpenGLRenderer.h"
#elif defined(__USE_VULKAN__)
    #include "VULKAN_Renderer.h"
#endif
#include "GLTFAnimator.h"
#include "BlenderImports.h"

#include <nlohmann/json.hpp>

#if defined(PLATFORM_WINDOWS)
	#include "WinSystem.h"
#endif

using json = nlohmann::json;

//#include "Models.h"
//#include "Lights.h"

#define BLENDER_UNIT_SCALER 1.0f											// Blender v4+ scaling metric unit 1.0 = 1 meter.
constexpr int MAX_SCENE_MODELS = MAX_MODELS;								// This is the max number of models we can have in a scene.

// Forward declarations
class Model;
struct ModelInfo;

//class DX11Renderer;

enum SceneType
{
	SCENE_NONE = 0,
	SCENE_INITIALISE,
	SCENE_GAMETITLE,
	SCENE_INTRO,
	SCENE_INTRO_MOVIE,
	SCENE_GAMEPLAY,
	SCENE_GAMEOVER,
	SCENE_CREDITS,
	SCENE_HIGHSCORES,
	SCENE_EDITOR,
	SCENE_LOAD_MP3,
	#if defined(_DEBUG)
		SCENE_EXPERIMENT,
	#endif
};

struct SceneModelStateBinary
{
	int32_t ID;
	wchar_t name[64];
	float position[3];
	float rotation[3];
	float scale[3];
};

//=============================================================================================================
class SceneManager
{
public:
	// Public functions & declarations.
	SceneManager();
	~SceneManager();

	SceneType stSceneType;
	LONGLONG sceneFrameCounter = 0;

	bool bGltfCameraParsed = false;
	bool bSceneSwitching = false;
	std::vector<uint8_t> gltfBinaryData;                                             // Loaded .bin buffer (temporary global for parsing)
	bool bAnimationsLoaded = false;                                                  // Flag indicating if animations were loaded from current scene
	bool bLoadedFromCache  = false;                                                  // Set true when cache fast-path was used; callers must NOT clear models[]

	// Global animator instance
	GLTFAnimator gltfAnimator;

	// Our Models Buffer, Resources & Data that will be rendered in A GIVEN scene.
	Model scene_models[MAX_SCENE_MODELS];

	// Releases all scene-local model data and resets state (does not touch global models[])
	void CleanUp();

	bool Initialize(std::shared_ptr<Renderer> renderer);
	bool SaveSceneState(const std::wstring& path);
	bool LoadSceneState(const std::wstring& path);

	bool SaveCache(const std::string& filepath);
	bool LoadCache(const std::string& filepath);
	bool IsSketchfabScene() const;

	bool ParseGLTFScene(const std::wstring& gltfFile);
	bool ParseGLBScene(const std::wstring& glbFile);                                 // Parses GLB 2.0 binary format with parent-child relationships

	void SetGotoScene(SceneType gotoScene);
	void InitiateScene();
	SceneType GetGotoScene();

	void AutoFrameSceneToCamera(float fovYRadians = XMConvertToRadians(60.0f), float padding = 1.2f);
	const std::wstring& GetLastDetectedExporter() const;
	void UpdateSceneAnimations(float deltaTime);                                    // Updates all active animations in the scene
	int FindParentModelID(const std::wstring& modelName);                           // Retrieves Model ID (Parent) from Model Name

	void DiagnoseGLBParsing(const std::wstring& glbFile);

private:
	bool bIsDestroyed = false;
	bool isSketchfab = false;
	SceneType stOurGotoScene = SCENE_NONE;

	std::wstring               m_lastDetectedExporter = L"Unknown";
	std::wstring               m_currentSceneFile;                 // Set before each recursive parse pass; used by NodeRecursive for write-back
	BlenderImports::ImportConfig m_blenderConfig;                  // Built once per GLTF/GLB load

	void DetectGLTFExporter(const nlohmann::json& doc);
	XMMATRIX GetNodeWorldMatrix(const json& node,
	                             const BlenderImports::ImportConfig& cfg);
	bool ParseMaterialsFromGLTF(const json& doc);
	void BindGLTFMaterialTexturesToModel(int materialIndex, ModelInfo& info, Model& model, const json& doc);
	std::shared_ptr<Texture> LoadGLTFImage(const json& imageEntry, const json& doc);  // URI file or embedded GLB bufferView → Texture
	void ParseGLTFCamera(const nlohmann::json& gltf, Camera& camera, float windowWidth, float windowHeight);
	bool ParseGLTFLights(const json& doc);
	void ParseGLTFNodeRecursive(const json& node, int nodeIndex, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex, int parentModelID);
	void ParseGLBNodeRecursive(const json& node, int nodeIndex, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex, int parentModelID);
	int  FindParentModelIDForAnimation(int animationIndex);                          // Resolves root parent model ID from animation channel node targets
	// primitiveFilter: -1 = all primitives into one model (legacy), >= 0 = only that primitive index
	void LoadGLTFMeshPrimitives(int meshIndex, const json& doc, Model& model, int primitiveFilter = -1);

#if defined(__USE_DIRECTX_11__)
	DX11Renderer* myRenderer = nullptr;
#elif defined(__USE_DIRECTX_12__)
	DX12Renderer* myRenderer = nullptr;
#elif defined(__USE_OPENGL__)
	OpenGLRenderer* myRenderer = nullptr;
#elif defined(__USE_VULKAN__)
	VulkanRenderer* myRenderer = nullptr;
#else
	void* myRenderer = nullptr;
#endif
};

// --------------------------------------------------------------------------------------------------