#pragma once

// =============================================================================
// FBXAnimator.h  --  Native FBX animation playback system
//
// Parallel to GLTFAnimator but stores and evaluates FBX animation data
// natively (FBXAnimStack / FBXAnimCurve) rather than converting to GLTF format.
//
// Pipeline:
//   ParseFBXScene()  ->  FBXAnimator::ParseAnimationsFromFBX()
//                        (builds FBXAnimationClip list from FBXScene stacks)
//   Per frame        ->  FBXAnimator::UpdateAnimations()
//                        (evaluates FBX curves, converts RH->LH, writes
//                         animLocalTRS, recomposes worldMatrix hierarchy)
// =============================================================================

#include "Includes.h"
#include "Models.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "FBXImport.h"

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__) || (defined(__USE_VULKAN__) && defined(PLATFORM_WINDOWS))
    using namespace DirectX;
#endif

// Forward declaration
class Model;

//==============================================================================
// FBXAnimChannel -- one animated property on one FBX model node.
// The three FBX animation curves (X, Y, Z) are resolved and copied at parse
// time so UpdateAnimations() needs no further lookups into the FBXScene.
//==============================================================================
struct FBXAnimChannel
{
    int                 targetModelSlot = -1;                        // Direct scene_models[] slot (resolved at parse time)
    std::string         targetNodeName;                              // FBX model name  (debug / logging)
    AnimationTargetPath path            = AnimationTargetPath::TRANSLATION;

    // Raw per-axis FBX animation curves
    FBXAnimCurve curveX;
    FBXAnimCurve curveY;
    FBXAnimCurve curveZ;

    // Static default values used when a curve is absent for an axis
    float defaultX = 0.0f;
    float defaultY = 0.0f;
    float defaultZ = 0.0f;

    // FBX node rotation properties needed for correct evaluation
    XMFLOAT3 preRotation   = { 0.0f, 0.0f, 0.0f };  // Degrees -- added before animated Euler before conversion
    int      rotationOrder = 0;                        // 0=XYZ  1=XZY  2=YXZ  3=YZX  4=ZXY  5=ZYX
};

//==============================================================================
// FBXAnimationClip -- one FBX AnimStack expressed in engine-friendly form.
// Equivalent to GLTFAnimation but backed by native FBX curve data.
//==============================================================================
struct FBXAnimationClip
{
    std::string                name;
    float                      startTime = 0.0f;   // Seconds (usually 0)
    float                      duration  = 0.0f;   // Seconds
    std::vector<FBXAnimChannel> channels;
};

//==============================================================================
// FBXAnimator -- native FBX animation playback, parallel to GLTFAnimator.
//
// Public interface mirrors GLTFAnimator exactly so ModelAnimator can dispatch
// to either animator transparently.
//==============================================================================
class FBXAnimator
{
public:
    FBXAnimator();
    ~FBXAnimator();

    // --- Parse ---
    // Build animation clips from an already-loaded FBXScene.
    // fbxIDToModelSlot maps FBX model int64_t ID --> scene_models[] slot.
    bool ParseAnimationsFromFBX(const FBXScene&                              fbxScene,
                                 const std::unordered_map<int64_t, int>&     fbxIDToModelSlot);

    // --- Playback control ---
    bool CreateAnimationInstance(int animationIndex, int parentModelID);
    bool StartAnimation(int parentModelID, int animationIndex = 0);
    bool StopAnimation(int parentModelID);
    bool PauseAnimation(int parentModelID);
    bool ResumeAnimation(int parentModelID);
    void UpdateAnimations(float deltaTime, Model* sceneModels, int maxModels);
    void ForceAnimationReset(int parentModelID);

    // --- Queries ---
    int                     GetAnimationCount() const;
    const FBXAnimationClip* GetClip(int index) const;
    AnimationInstance*      GetAnimationInstance(int parentModelID);
    bool                    IsAnimationPlaying(int parentModelID) const;
    float                   GetAnimationTime(int parentModelID) const;
    float                   GetAnimationDuration(int clipIndex) const;

    // --- Playback parameters ---
    bool SetAnimationSpeed(int parentModelID, float speed);
    bool SetAnimationLooping(int parentModelID, bool looping);
    bool SetAnimationTime(int parentModelID, float time);
    void SetAnimationDirection(int parentModelID, AnimationDirection direction);
    bool AtAnimationEndFrame(int parentModelID, int& frameIndex);
    void HoldAnimationAtFrame(int parentModelID, int frameIndex);

    // --- Cleanup ---
    void ClearAllAnimations();
    void RemoveAnimationInstance(int parentModelID);

private:
    // --- Data ---
    std::vector<FBXAnimationClip>  m_clips;
    std::vector<AnimationInstance> m_animationInstances;
    bool m_isInitialized = false;

    // Scene-level coordinate system (copied from FBXScene::globalSettings at parse time)
    int m_upAxis    = 1;   // 0=X 1=Y 2=Z
    int m_frontAxis = 2;

    // --- Curve evaluation ---
    float EvaluateCurve(const FBXAnimCurve& curve, float timeSeconds) const;
    float EvaluateCurveOrDefault(const FBXAnimCurve& curve, float timeSeconds, float def) const;
    int   GetInterpolationTypeForKey(const FBXAnimCurve& curve, int keyIndex) const;

    // --- Coordinate conversion (mirrors FBXImporter private helpers) ---
    // ApplyCoordFlip: negates Z for Y-up right-handed -> Y-up left-handed
    XMFLOAT3 ApplyCoordFlip(const XMFLOAT3& v) const;
    // ApplyRotationFlip: M*R*M where M=diag(1,1,-1) for Y-up scenes
    XMMATRIX ApplyRotationFlip(const XMMATRIX& rotM) const;
    // EulerToMatrix: converts degrees XYZ Euler (FBX right-handed) to rotation matrix
    XMMATRIX EulerToMatrix(const XMFLOAT3& degXYZ, int order) const;
    // MatrixToQuaternion: extracts unit quaternion from rotation matrix (DirectX convention)
    XMFLOAT4 MatrixToQuaternion(const XMMATRIX& m) const;
    XMFLOAT4 SlerpQuaternions(const XMFLOAT4& q1, const XMFLOAT4& q2, float t) const;

    // --- Animation application ---
    void ApplyChannelToSlot(const FBXAnimChannel& ch, float timeSeconds,
                             Model* sceneModels, int maxModels);

    // --- Hierarchy helpers (same pattern as GLTFAnimator) ---
    bool IsInHierarchy(int modelIndex, int rootParentID,
                        const Model* sceneModels, int maxModels) const;
    void ResetLocalTRSToBase(int rootParentID, Model* sceneModels, int maxModels) const;
    void RecomposeWorldFromLocalTRS(int rootParentID, Model* sceneModels, int maxModels) const;

    // --- Logging ---
    void LogInfo   (const std::wstring& msg) const;
    void LogWarning(const std::wstring& msg) const;
    void LogError  (const std::wstring& msg) const;
};
