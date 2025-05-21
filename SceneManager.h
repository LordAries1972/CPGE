#pragma once

#include "Includes.h"
#include "DX11Renderer.h"
#include <nlohmann/json.hpp>

#if defined(_WIN32) || defined(_WIN64)
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
	SCENE_SPLASH,
	SCENE_INTRO,
	SCENE_INTRO_MOVIE,
	SCENE_GAMEPLAY,
	SCENE_GAMEOVER,
	SCENE_CREDITS,
	SCENE_EDITOR,
	SCENE_LOAD_MP3,
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

	// Our Models Buffer, Resources & Data that will be rendered in A GIVEN scene.
	Model scene_models[MAX_SCENE_MODELS];

	// Releases all scene-local model data and resets state (does not touch global models[])
	void CleanUp();

	bool Initialize(std::shared_ptr<Renderer> renderer);
	bool SaveSceneState(const std::wstring& path);
	bool LoadSceneState(const std::wstring& path);
	bool IsSketchfabScene() const;
	bool ParseGLTFScene(const std::wstring& gltfFile);
	
	void SetGotoScene(SceneType gotoScene);
	void InitiateScene();
	SceneType GetGotoScene();

	void AutoFrameSceneToCamera(float fovYRadians = XMConvertToRadians(60.0f), float padding = 1.2f);
	const std::wstring& GetLastDetectedExporter() const;

private:
	bool bIsDestroyed = false;
	bool isSketchfab = false;
	SceneType stOurGotoScene = SCENE_NONE;

	std::wstring m_lastDetectedExporter = L"Unknown";

	void DetectGLTFExporter(const nlohmann::json& doc);
	XMMATRIX GetNodeWorldMatrix(const json& node);
	bool ParseMaterialsFromGLTF(const json& doc);
	void BindGLTFMaterialTexturesToModel(int materialIndex, ModelInfo& info, Model& model, const json& doc);
	void ParseGLTFCamera(const nlohmann::json& gltf, Camera& camera, float windowWidth, float windowHeight);
	bool ParseGLTFLights(const json& doc);
	void ParseGLTFNodeRecursive(const json& node, const XMMATRIX& parentTransform, const json& doc, const json& allNodes, int& instanceIndex);
	void LoadGLTFMeshPrimitives(int meshIndex, const json& doc, Model& model);

	DX11Renderer* myRenderer = nullptr;												 // Pointer to the DX11 renderer
};

// --------------------------------------------------------------------------------------------------