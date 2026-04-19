/*  /--------------------------------------------------------------------------------------\
    This code file contains all the global Shader Load Routines that you will be
    using for your Gaming Project.

    This has been seperated as for the following reasons:-

    1) Platform dependency resulting a large file itself (this .cpp file).

    2) Keeps routines isolated from the ShaderManager class itself.

    3) All Calls here are Public, so all other areas of your code can access these 
       calls via extern referencing when required.

    4) A Helper class has been provided if you rather use this method for listing
       all your required shaders for the appropriate scenes.  See class SceneShaderManager

    You will need to modify the following for your project

    MyShaders vector      - In Includes.h where the current filenames for your shaders go.
    CreateScenePrograms() - Where Scene Programs are created
    \--------------------------------------------------------------------------------------/ */

#include "Includes.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "Renderer.h"
#include "ShaderManager.h"
#include "SceneManager.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"

extern Debug debug;
extern ExceptionHandler exceptionHandler;
extern ShaderManager shaderManager;
extern ThreadManager threadMangaer;
extern SceneManager scene;

class SceneShaderManager {
private:
    std::unordered_map<SceneType, std::vector<std::string>> m_sceneShaders;

public:
    void Initialize() {
        // Define shaders needed for each scene type
//        m_sceneShaders[SCENE_SPLASH] = { "SplashVertex", "SplashPixel", "FadeTransitionPixel" };
        m_sceneShaders[SCENE_GAMEPLAY] = { "ModelVertex", "ModelPixel" };
    }

    void LoadSceneShaders(SceneType sceneType) {
        auto it = m_sceneShaders.find(sceneType);
        if (it != m_sceneShaders.end()) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Loading shaders for scene type: %d", (int)sceneType);

            for (const std::string& shaderName : it->second) {
                if (!shaderManager.DoesShaderExist(shaderName)) {
                    LoadShaderByName(shaderName);
                }
            }

            // Create scene-specific programs
            CreateScenePrograms(sceneType);
        }
    }

    void UnloadSceneShaders(SceneType sceneType) {
        auto it = m_sceneShaders.find(sceneType);
        if (it != m_sceneShaders.end()) {
            for (const std::string& shaderName : it->second) {
                // Only unload if not used by other scenes
                if (CanUnloadShader(shaderName)) {
                    shaderManager.UnloadShader(shaderName);
                }
            }
        }
    }

private:
    void LoadShaderByName(const std::string& shaderName) {
        // Determine shader properties from name
        ShaderType type = shaderManager.GetShaderTypeFromName(shaderName);

        // Create proper file path with correct concatenation - fix the path building issue
        std::wstring shaderFileName = std::wstring(shaderName.begin(), shaderName.end()) + L".hlsl";
        auto path = ShadersDir / shaderFileName;

        // Create appropriate profile
        ShaderProfile profile = CreateProfileForShader(shaderName);

        if (!shaderManager.LoadShader(shaderName, path, type, profile)) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load scene shader: %hs", shaderName.c_str());
            #endif
        }
        else {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully loaded scene shader: %hs from path: %ls", shaderName.c_str(), path.c_str());
            #endif
        }
    }

    ShaderProfile CreateProfileForShader(const std::string& shaderName) {
        ShaderProfile profile;
        profile.optimized = true;

        // Add specific defines based on shader name
        if (shaderName.find("Lighting") != std::string::npos) {
            profile.defines.push_back("MAX_LIGHTS=" + std::to_string(MAX_LIGHTS));
            profile.defines.push_back("USE_DYNAMIC_LIGHTING");
        }

        if (shaderName.find("Particle") != std::string::npos) {
            profile.defines.push_back("MAX_PARTICLES=1024");
            profile.defines.push_back("USE_GPU_SIMULATION");
        }

        if (shaderName.find("Debug") != std::string::npos) {
            profile.debugInfo = true;
            profile.optimized = false;
        }

        return profile;
    }

    void CreateScenePrograms(SceneType sceneType) {
        switch (sceneType) {
            case SCENE_SPLASH:
                break;

            case SCENE_GAMEPLAY:
                shaderManager.CreateShaderProgram("GameplayModelProgram", "ModelVertex", "ModelPixel");
                break;
        }
    }

    bool CanUnloadShader(const std::string& shaderName) {
        // Check if shader is used by multiple scenes
        int usageCount = 0;
        for (const auto& pair : m_sceneShaders) {
            const auto& shaderList = pair.second;
            if (std::find(shaderList.begin(), shaderList.end(), shaderName) != shaderList.end()) {
                usageCount++;
            }
        }

        return usageCount <= 1;  // Only unload if used by one scene or less
    }
};

bool LoadAllShaders()
{
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[LoadAllShaders] Starting to load all critical shaders.");
    #endif

    // Define critical shaders that should always be loaded
    std::vector<std::string> Shaders = MyShaders;

    for (const std::string& shaderName : Shaders) {
        if (!shaderManager.DoesShaderExist(shaderName)) {
            // Load critical shader
            ShaderType type = shaderManager.GetShaderTypeFromName(shaderName);

            // Create proper file path with correct concatenation - fix the path building issue
            std::wstring shaderFileName = std::wstring(shaderName.begin(), shaderName.end()) + L".hlsl";
            auto path = ShadersDir / shaderFileName;

            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[LoadAllShaders] Attempting to load shader: %hs from path: %ls", shaderName.c_str(), path.c_str());
            #endif

            if (!shaderManager.LoadShader(shaderName, path, type))
            {
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"Shader: %hs has failed to load from path: %ls!", shaderName.c_str(), path.c_str());
                #endif
                return false;
            }
            else
            {
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"Shader: %hs loaded successfully from path: %ls", shaderName.c_str(), path.c_str());
                #endif
            }
        }
        else
        {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Shader: %hs already exists, skipping load.", shaderName.c_str());
            #endif
        }
    } // End of for (const std::string& shaderName : Shaders)

    // Now create the programs
    if (!shaderManager.CreateShaderProgram("ModelProgram", "ModelVertex", "ModelPixel")) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LoadAllShaders] Failed to create GameplayModelProgram shader program!");
        #endif
        return false;
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[LoadAllShaders] All shaders loaded and programs created successfully.");
    #endif

    return true;
}