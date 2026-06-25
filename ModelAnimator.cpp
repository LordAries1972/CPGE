// =============================================================================
// ModelAnimator.cpp  --  Universal animation dispatcher implementation
// =============================================================================

#include "Includes.h"
#include "ModelAnimator.h"

//==============================================================================
// Constructor
//==============================================================================
ModelAnimator::ModelAnimator() : m_models(nullptr), m_maxModels(0)
{
}

//==============================================================================
// SetModels
// Bind the scene model array so UpdateAnimations() can pass it to both
// sub-animators without the caller having to repeat the arguments.
//==============================================================================
void ModelAnimator::SetModels(Model* models, int maxModels)
{
    m_models    = models;
    m_maxModels = maxModels;
}

//==============================================================================
// GetModelImportType
//==============================================================================
ImportType ModelAnimator::GetModelImportType(int modelID) const
{
    if (!m_models || modelID < 0 || modelID >= m_maxModels)
        return ImportType::NONE;

    if (!m_models[modelID].m_isLoaded)
        return ImportType::NONE;

    return m_models[modelID].m_modelInfo.importType;
}

//==============================================================================
// IsAnimationPlaying
// True if EITHER sub-animator has an active playing instance for this model.
//==============================================================================
bool ModelAnimator::IsAnimationPlaying(int parentModelID) const
{
    return gltfAnimator.IsAnimationPlaying(parentModelID) ||
           fbxAnimator.IsAnimationPlaying(parentModelID);
}

//==============================================================================
// StartAnimation
//==============================================================================
bool ModelAnimator::StartAnimation(int parentModelID, int animationIndex)
{
    switch (GetModelImportType(parentModelID))
    {
        case ImportType::GLTF: return gltfAnimator.StartAnimation(parentModelID, animationIndex);
        case ImportType::FBX:  return fbxAnimator.StartAnimation(parentModelID, animationIndex);
        default:
            // Unknown import type -- try both (handles NONE / future types)
            if (gltfAnimator.GetAnimationCount() > 0)
                return gltfAnimator.StartAnimation(parentModelID, animationIndex);
            if (fbxAnimator.GetAnimationCount() > 0)
                return fbxAnimator.StartAnimation(parentModelID, animationIndex);
            return false;
    }
}

//==============================================================================
// StopAnimation
//==============================================================================
bool ModelAnimator::StopAnimation(int parentModelID)
{
    bool a = gltfAnimator.StopAnimation(parentModelID);
    bool b = fbxAnimator.StopAnimation(parentModelID);
    return a || b;
}

//==============================================================================
// PauseAnimation
//==============================================================================
bool ModelAnimator::PauseAnimation(int parentModelID)
{
    bool a = gltfAnimator.PauseAnimation(parentModelID);
    bool b = fbxAnimator.PauseAnimation(parentModelID);
    return a || b;
}

//==============================================================================
// ResumeAnimation
//==============================================================================
bool ModelAnimator::ResumeAnimation(int parentModelID)
{
    bool a = gltfAnimator.ResumeAnimation(parentModelID);
    bool b = fbxAnimator.ResumeAnimation(parentModelID);
    return a || b;
}

//==============================================================================
// UpdateAnimations
// Advances time and writes animLocalTRS for all active instances on both
// sub-animators.  Uses the scene model array set via SetModels().
//==============================================================================
void ModelAnimator::UpdateAnimations(float deltaTime)
{
    if (!m_models || m_maxModels <= 0) return;

    gltfAnimator.UpdateAnimations(deltaTime, m_models, m_maxModels);
    fbxAnimator.UpdateAnimations(deltaTime, m_models, m_maxModels);
}

//==============================================================================
// GetAnimationTime
//==============================================================================
float ModelAnimator::GetAnimationTime(int parentModelID) const
{
    if (gltfAnimator.IsAnimationPlaying(parentModelID))
        return gltfAnimator.GetAnimationTime(parentModelID);

    if (fbxAnimator.IsAnimationPlaying(parentModelID))
        return fbxAnimator.GetAnimationTime(parentModelID);

    // Return whichever sub-animator has an instance, even if not playing
    float t = gltfAnimator.GetAnimationTime(parentModelID);
    if (t != 0.0f) return t;
    return fbxAnimator.GetAnimationTime(parentModelID);
}

//==============================================================================
// SetAnimationSpeed
//==============================================================================
bool ModelAnimator::SetAnimationSpeed(int parentModelID, float speed)
{
    bool a = gltfAnimator.SetAnimationSpeed(parentModelID, speed);
    bool b = fbxAnimator.SetAnimationSpeed(parentModelID, speed);
    return a || b;
}

//==============================================================================
// SetAnimationLooping
//==============================================================================
bool ModelAnimator::SetAnimationLooping(int parentModelID, bool looping)
{
    bool a = gltfAnimator.SetAnimationLooping(parentModelID, looping);
    bool b = fbxAnimator.SetAnimationLooping(parentModelID, looping);
    return a || b;
}

//==============================================================================
// SetAnimationTime
//==============================================================================
bool ModelAnimator::SetAnimationTime(int parentModelID, float time)
{
    bool a = gltfAnimator.SetAnimationTime(parentModelID, time);
    bool b = fbxAnimator.SetAnimationTime(parentModelID, time);
    return a || b;
}

//==============================================================================
// SetAnimationDirection
//==============================================================================
void ModelAnimator::SetAnimationDirection(int parentModelID, AnimationDirection direction)
{
    gltfAnimator.SetAnimationDirection(parentModelID, direction);
    fbxAnimator.SetAnimationDirection(parentModelID, direction);
}

//==============================================================================
// AtAnimationEndFrame
//==============================================================================
bool ModelAnimator::AtAnimationEndFrame(int parentModelID, int& frameIndex)
{
    if (gltfAnimator.IsAnimationPlaying(parentModelID))
        return gltfAnimator.AtAnimationEndFrame(parentModelID, frameIndex);

    if (fbxAnimator.IsAnimationPlaying(parentModelID))
        return fbxAnimator.AtAnimationEndFrame(parentModelID, frameIndex);

    // Neither is playing -- check if either has a stopped instance
    int dummy = 0;
    if (gltfAnimator.AtAnimationEndFrame(parentModelID, dummy))
        { frameIndex = dummy; return true; }
    if (fbxAnimator.AtAnimationEndFrame(parentModelID, dummy))
        { frameIndex = dummy; return true; }

    return false;
}

//==============================================================================
// HoldAnimationAtFrame
//==============================================================================
void ModelAnimator::HoldAnimationAtFrame(int parentModelID, int frameIndex)
{
    gltfAnimator.HoldAnimationAtFrame(parentModelID, frameIndex);
    fbxAnimator.HoldAnimationAtFrame(parentModelID, frameIndex);
}

//==============================================================================
// ForceAnimationReset
//==============================================================================
void ModelAnimator::ForceAnimationReset(int parentModelID)
{
    gltfAnimator.ForceAnimationReset(parentModelID);
    fbxAnimator.ForceAnimationReset(parentModelID);
}

//==============================================================================
// ClearAllAnimations
//==============================================================================
void ModelAnimator::ClearAllAnimations()
{
    gltfAnimator.ClearAllAnimations();
    fbxAnimator.ClearAllAnimations();
}
