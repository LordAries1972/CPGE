#pragma once

// =============================================================================
// ModelAnimator.h  --  Universal animation dispatcher
//
// Owns one GLTFAnimator and one FBXAnimator as public members.
// Dispatch methods inspect the target model's ImportType and route the call
// to the correct underlying animator automatically, so render-frame code
// never needs to know which importer produced a given model.
//
// Usage:
//   scene.modelAnimator.SetModels(scene_models, MAX_SCENE_MODELS);
//   scene.modelAnimator.StartAnimation(iModelID);
//   scene.modelAnimator.UpdateAnimations(deltaTime);   // replaces both old calls
// =============================================================================

#include "GLTFAnimator.h"
#include "FBXAnimator.h"
#include "Models.h"

// Forward declaration
class Model;

class ModelAnimator
{
public:
    ModelAnimator();
    ~ModelAnimator() = default;

    // ------------------------------------------------------------------
    // Public sub-animators  (SceneManager may call these directly when it
    // needs GLTF- or FBX-specific APIs that are not part of the unified
    // interface, e.g. ParseAnimationsFromFBX or FindParentModelIDForAnimation)
    // ------------------------------------------------------------------
    GLTFAnimator gltfAnimator;
    FBXAnimator  fbxAnimator;

    // ------------------------------------------------------------------
    // Bind the scene model array (must be called once after scene init)
    // ------------------------------------------------------------------
    void SetModels(Model* models, int maxModels);

    // ------------------------------------------------------------------
    // Unified dispatch methods -- route to the correct sub-animator
    // based on scene_models[iModelID].m_modelInfo.importType.
    // ------------------------------------------------------------------

    // Returns true when either sub-animator has an active instance for this ID.
    bool IsAnimationPlaying(int parentModelID) const;

    // Start the given clip (default 0) on the correct sub-animator.
    bool StartAnimation(int parentModelID, int animationIndex = 0);

    // Stop / pause / resume -- applied to whichever animator owns the instance.
    bool StopAnimation   (int parentModelID);
    bool PauseAnimation  (int parentModelID);
    bool ResumeAnimation (int parentModelID);

    // Advance time and write animLocalTRS for ALL active instances on both animators.
    void UpdateAnimations(float deltaTime);

    // Query the current play-head (checks both animators)
    float GetAnimationTime(int parentModelID) const;

    // Playback parameter setters (applied to whichever animator owns the instance)
    bool SetAnimationSpeed    (int parentModelID, float speed);
    bool SetAnimationLooping  (int parentModelID, bool looping);
    bool SetAnimationTime     (int parentModelID, float time);
    void SetAnimationDirection(int parentModelID, AnimationDirection direction);
    bool AtAnimationEndFrame  (int parentModelID, int& frameIndex);
    void HoldAnimationAtFrame (int parentModelID, int frameIndex);
    void ForceAnimationReset  (int parentModelID);

    // Wipe all clips and instances from both sub-animators.
    void ClearAllAnimations();

private:
    Model* m_models    = nullptr;
    int    m_maxModels = 0;

    // Resolve import type for any model in the scene array.
    ImportType GetModelImportType(int modelID) const;
};
