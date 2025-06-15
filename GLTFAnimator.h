#pragma once

#include "Includes.h"
#include "Models.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace DirectX;

// Forward declarations
class SceneManager;
class Model;

//==============================================================================
// GLTFAnimator Class Declaration
// Handles parsing, storage, and playback of GLTF/GLB animations
//==============================================================================
class GLTFAnimator
{
public:
    // Constructor and destructor
    GLTFAnimator();
    ~GLTFAnimator();

    // Core animation management functions
    bool ParseAnimationsFromGLTF(const json& doc, const std::vector<uint8_t>& binaryData);
    bool CreateAnimationInstance(int animationIndex, int parentModelID);
    bool StartAnimation(int parentModelID, int animationIndex = 0);
    bool StopAnimation(int parentModelID);
    bool PauseAnimation(int parentModelID);
    bool ResumeAnimation(int parentModelID);
    void UpdateAnimations(float deltaTime, Model* sceneModels, int maxModels);

    // Animation query and control functions
    int GetAnimationCount() const;
    const GLTFAnimation* GetAnimation(int index) const;
    AnimationInstance* GetAnimationInstance(int parentModelID);
    bool SetAnimationSpeed(int parentModelID, float speed);
    bool SetAnimationLooping(int parentModelID, bool looping);
    bool SetAnimationTime(int parentModelID, float time);
    float GetAnimationTime(int parentModelID) const;
    float GetAnimationDuration(int animationIndex) const;
    bool IsAnimationPlaying(int parentModelID) const;

    // Cleanup and utility functions
    void ClearAllAnimations();
    void RemoveAnimationInstance(int parentModelID);
    void DebugPrintAnimationInfo() const;

private:
    // Internal data storage
    std::vector<GLTFAnimation> m_animations;                                         // All loaded animations
    std::vector<AnimationInstance> m_animationInstances;                            // Currently playing animation instances
    bool m_isInitialized;                                                           // Whether animator has been properly initialized

    // Internal animation processing functions
    bool ParseAnimationSamplers(const json& animation, const json& doc, const std::vector<uint8_t>& binaryData, GLTFAnimation& outAnimation);
    bool ParseAnimationChannels(const json& animation, const json& doc, GLTFAnimation& outAnimation);
    bool LoadKeyframeData(int accessorIndex, const json& doc, const std::vector<uint8_t>& binaryData, std::vector<float>& outData);
    void InterpolateKeyframes(const AnimationSampler& sampler, float time, std::vector<float>& outValues);
    void ApplyAnimationToNode(const AnimationChannel& channel, const std::vector<float>& values, Model* sceneModels, int maxModels, int parentModelID);
    XMMATRIX CreateTransformMatrix(const XMFLOAT3& translation, const XMFLOAT4& rotation, const XMFLOAT3& scale);
    XMFLOAT4 SlerpQuaternions(const XMFLOAT4& q1, const XMFLOAT4& q2, float t);

    // Utility functions for data validation and error handling
    bool ValidateAnimationData(const GLTFAnimation& animation) const;
    bool ValidateAccessorIndex(int accessorIndex, const json& doc) const;
    void LogAnimationError(const std::wstring& errorMessage) const;
    void LogAnimationWarning(const std::wstring& warningMessage) const;
    void LogAnimationInfo(const std::wstring& infoMessage) const;
};

// Global animator instance declaration
extern GLTFAnimator gltfAnimator;