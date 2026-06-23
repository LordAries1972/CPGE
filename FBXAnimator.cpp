// Suppress Includes.h global math stubs -- this file defines its own stubs below.
#define CPGE_MATH_STUBS_DEFINED
#include "Includes.h"
#include "FBXAnimator.h"
#include "SceneManager.h"
#include "ExceptionHandler.h"

// ── Non-DirectX math stubs ─────────────────────────────────────────────────
// When building without DX11/DX12 (and not Windows+Vulkan), supply minimal
// XM-function equivalents so the animation code compiles unchanged.
#if !defined(__USE_DIRECTX_11__) && !defined(__USE_DIRECTX_12__) && !(defined(__USE_VULKAN__) && defined(PLATFORM_WINDOWS))
#include <cmath>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

// XMFLOAT4X4 bridge (not in Includes.h for non-DX builds)
struct XMFLOAT4X4
{
    union
    {
        struct { float _11,_12,_13,_14, _21,_22,_23,_24, _31,_32,_33,_34, _41,_42,_43,_44; };
        float m[4][4];
    };
    XMFLOAT4X4() { for(int i=0;i<4;i++) for(int j=0;j<4;j++) m[i][j]=(i==j)?1.f:0.f; }
};

// Basic scalar conversion
inline float XMConvertToRadians(float deg) { return deg * static_cast<float>(M_PI / 180.0); }

// Vector helpers
inline Vector4 XMLoadFloat4(const Vector4* v)               { return *v; }
inline void    XMStoreFloat4(Vector4* d, Vector4 v)         { *d = v; }
inline Vector4 XMVectorSet(float x,float y,float z,float w) { return {x,y,z,w}; }

inline Vector4 XMQuaternionNormalize(Vector4 q)
{
    float len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len > 1e-6f) { float inv=1.f/len; return {q.x*inv,q.y*inv,q.z*inv,q.w*inv}; }
    return {0.f,0.f,0.f,1.f};
}

inline Vector4 XMQuaternionSlerp(Vector4 q0, Vector4 q1, float t)
{
    float d = q0.x*q1.x + q0.y*q1.y + q0.z*q1.z + q0.w*q1.w;
    if (d < 0.f) { q1.x=-q1.x; q1.y=-q1.y; q1.z=-q1.z; q1.w=-q1.w; d=-d; }
    Vector4 r;
    if (d > 0.9995f)
    {
        r = {q0.x+t*(q1.x-q0.x), q0.y+t*(q1.y-q0.y), q0.z+t*(q1.z-q0.z), q0.w+t*(q1.w-q0.w)};
    }
    else
    {
        float theta0 = acosf(d), theta = theta0*t;
        float s0 = cosf(theta) - d*sinf(theta)/sinf(theta0);
        float s1 = sinf(theta)/sinf(theta0);
        r = {s0*q0.x+s1*q1.x, s0*q0.y+s1*q1.y, s0*q0.z+s1*q1.z, s0*q0.w+s1*q1.w};
    }
    return XMQuaternionNormalize(r);
}

// Matrix basics
inline Matrix4x4 XMMatrixIdentity() { return Matrix4x4(); }

inline Matrix4x4 XMMatrixScaling(float x, float y, float z)
    { Matrix4x4 m; m.m[0][0]=x; m.m[1][1]=y; m.m[2][2]=z; return m; }
inline Matrix4x4 XMMatrixScalingFromVector(Vector4 v)
    { return XMMatrixScaling(v.x,v.y,v.z); }
inline Matrix4x4 XMMatrixTranslation(float x, float y, float z)
    { Matrix4x4 m; m.m[3][0]=x; m.m[3][1]=y; m.m[3][2]=z; return m; }
inline Matrix4x4 XMMatrixTranslationFromVector(Vector4 v)
    { return XMMatrixTranslation(v.x,v.y,v.z); }

// Row-vector DirectX convention rotation matrices
inline Matrix4x4 XMMatrixRotationX(float a)
{
    Matrix4x4 m; float c=cosf(a), s=sinf(a);
    m.m[1][1]=c; m.m[1][2]=s; m.m[2][1]=-s; m.m[2][2]=c; return m;
}
inline Matrix4x4 XMMatrixRotationY(float a)
{
    Matrix4x4 m; float c=cosf(a), s=sinf(a);
    m.m[0][0]=c; m.m[0][2]=-s; m.m[2][0]=s; m.m[2][2]=c; return m;
}
inline Matrix4x4 XMMatrixRotationZ(float a)
{
    Matrix4x4 m; float c=cosf(a), s=sinf(a);
    m.m[0][0]=c; m.m[0][1]=s; m.m[1][0]=-s; m.m[1][1]=c; return m;
}

inline Matrix4x4 XMMatrixRotationQuaternion(Vector4 q)
{
    float x=q.x,y=q.y,z=q.z,w=q.w;
    Matrix4x4 m;
    m.m[0][0]=1-2*(y*y+z*z); m.m[0][1]=2*(x*y+z*w);   m.m[0][2]=2*(x*z-y*w);
    m.m[1][0]=2*(x*y-z*w);   m.m[1][1]=1-2*(x*x+z*z); m.m[1][2]=2*(y*z+x*w);
    m.m[2][0]=2*(x*z+y*w);   m.m[2][1]=2*(y*z-x*w);   m.m[2][2]=1-2*(x*x+y*y);
    return m;
}

inline Matrix4x4 operator*(const Matrix4x4& a, const Matrix4x4& b)
{
    Matrix4x4 r;
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) { r.m[i][j]=0; for(int k=0;k<4;k++) r.m[i][j]+=a.m[i][k]*b.m[k][j]; }
    return r;
}

// XMFLOAT4X4 <-> Matrix4x4 bridge
inline Matrix4x4 XMLoadFloat4x4(const XMFLOAT4X4* p)
{
    Matrix4x4 m; for(int i=0;i<4;i++) for(int j=0;j<4;j++) m.m[i][j]=p->m[i][j]; return m;
}
inline void XMStoreFloat4x4(XMFLOAT4X4* d, const Matrix4x4& s)
{
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) d->m[i][j]=s.m[i][j];
}

// Extract quaternion from rotation matrix (DirectX row-vector convention, Shepperd method)
inline Vector4 XMQuaternionRotationMatrix(const Matrix4x4& m)
{
    float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    Vector4 q = {0,0,0,1};
    if (trace > 0.0f)
    {
        float s = sqrtf(trace + 1.0f);
        q.w = s * 0.5f;
        s = 0.5f / s;
        q.x = (m.m[1][2] - m.m[2][1]) * s;
        q.y = (m.m[2][0] - m.m[0][2]) * s;
        q.z = (m.m[0][1] - m.m[1][0]) * s;
    }
    else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2])
    {
        float s = 2.0f * sqrtf(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);
        q.w = (m.m[0][1] - m.m[1][0]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[0][1] + m.m[1][0]) / s;
        q.z = (m.m[2][0] + m.m[0][2]) / s;
    }
    else if (m.m[1][1] > m.m[2][2])
    {
        float s = 2.0f * sqrtf(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);
        q.w = (m.m[2][0] - m.m[0][2]) / s;
        q.x = (m.m[0][1] + m.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[1][2] + m.m[2][1]) / s;
    }
    else
    {
        float s = 2.0f * sqrtf(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);
        q.w = (m.m[1][0] - m.m[0][1]) / s;
        q.x = (m.m[2][0] + m.m[0][2]) / s;
        q.y = (m.m[1][2] + m.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return XMQuaternionNormalize(q);
}

#ifndef XM_PIDIV2
    #define XM_PIDIV2 1.5707963267948966f
#endif
#ifndef XM_PI
    #define XM_PI 3.14159265358979323846f
#endif
#endif // non-DX stub block

// ── External singletons ──────────────────────────────────────────────────────
extern Debug            debug;
extern ExceptionHandler exceptionHandler;

//==============================================================================
// Constructor / Destructor
//==============================================================================
FBXAnimator::FBXAnimator() : m_isInitialized(false)
{
    m_clips.clear();
    m_animationInstances.clear();

    #if defined(_DEBUG_FBXANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[FBXAnimator] Initialized.");
    #endif
}

FBXAnimator::~FBXAnimator()
{
    ClearAllAnimations();
}

//==============================================================================
// ParseAnimationsFromFBX
// Converts all AnimStacks in an FBXScene into FBXAnimationClip list.
// fbxIDToModelSlot must already be populated by ParseFBXScene().
//==============================================================================
bool FBXAnimator::ParseAnimationsFromFBX(
    const FBXScene&                          fbxScene,
    const std::unordered_map<int64_t, int>&  fbxIDToModelSlot)
{
    try
    {
        m_clips.clear();
        m_animationInstances.clear();
        m_isInitialized = false;

        // Store coordinate system info for ApplyCoordFlip / ApplyRotationFlip
        m_upAxis    = fbxScene.upAxis;
        m_frontAxis = fbxScene.frontAxis;

        if (fbxScene.animStacks.empty())
        {
            LogInfo(L"No animation stacks found in FBX scene.");
            return true; // Not an error
        }

        // --- Build fast-lookup maps for animation objects ---
        std::unordered_map<int64_t, const FBXAnimLayer*>     layerByID;
        std::unordered_map<int64_t, const FBXAnimCurveNode*> cNodeByID;
        std::unordered_map<int64_t, const FBXAnimCurve*>     curveByID;

        for (const auto& layer : fbxScene.animLayers)    layerByID[layer.id]   = &layer;
        for (const auto& cnode : fbxScene.animCurveNodes) cNodeByID[cnode.id]  = &cnode;
        for (const auto& curve : fbxScene.animCurves)    curveByID[curve.id]   = &curve;

        // --- Process each AnimStack as one FBXAnimationClip ---
        for (const auto& stack : fbxScene.animStacks)
        {
            FBXAnimationClip clip;
            clip.name = stack.name;

            // Duration from stack timing (convert FBX time units -> seconds)
            clip.startTime = static_cast<float>(
                static_cast<double>(stack.localStart) / FBX_TIME_UNIT);
            float endTime = static_cast<float>(
                static_cast<double>(stack.localStop)  / FBX_TIME_UNIT);
            clip.duration  = endTime - clip.startTime;

            // Build channels from all layers
            for (int64_t layerID : stack.layerIDs)
            {
                auto lit = layerByID.find(layerID);
                if (lit == layerByID.end()) continue;

                for (int64_t cnodeID : lit->second->curveNodeIDs)
                {
                    auto cit = cNodeByID.find(cnodeID);
                    if (cit == cNodeByID.end()) continue;
                    const FBXAnimCurveNode& cnode = *cit->second;

                    // Skip curve nodes not connected to any model slot
                    if (cnode.targetModelID == 0) continue;
                    auto sit = fbxIDToModelSlot.find(cnode.targetModelID);
                    if (sit == fbxIDToModelSlot.end()) continue;
                    int targetSlot = sit->second;

                    // Map FBX property name to animation path
                    AnimationTargetPath path;
                    if      (cnode.propertyName == "Lcl Translation") path = AnimationTargetPath::TRANSLATION;
                    else if (cnode.propertyName == "Lcl Rotation")    path = AnimationTargetPath::ROTATION;
                    else if (cnode.propertyName == "Lcl Scaling")     path = AnimationTargetPath::SCALE;
                    else continue; // Skip unknown properties

                    FBXAnimChannel ch;
                    ch.targetModelSlot = targetSlot;
                    ch.targetNodeName  = cnode.name;
                    ch.path            = path;
                    ch.defaultX        = cnode.defaultX;
                    ch.defaultY        = cnode.defaultY;
                    ch.defaultZ        = cnode.defaultZ;

                    // Copy pre-rotation and rotation order from the FBX model node
                    auto mit = fbxScene.modelByID.find(cnode.targetModelID);
                    if (mit != fbxScene.modelByID.end() &&
                        mit->second >= 0 && mit->second < static_cast<int>(fbxScene.models.size()))
                    {
                        const FBXModel& fbxModel = fbxScene.models[mit->second];
                        ch.preRotation   = fbxModel.transform.preRotation;
                        ch.rotationOrder = fbxModel.transform.rotationOrder;
                    }

                    // Resolve axis curves (deep-copy so the clip is self-contained)
                    if (cnode.curveXID != 0)
                    {
                        auto it = curveByID.find(cnode.curveXID);
                        if (it != curveByID.end()) ch.curveX = *it->second;
                    }
                    if (cnode.curveYID != 0)
                    {
                        auto it = curveByID.find(cnode.curveYID);
                        if (it != curveByID.end()) ch.curveY = *it->second;
                    }
                    if (cnode.curveZID != 0)
                    {
                        auto it = curveByID.find(cnode.curveZID);
                        if (it != curveByID.end()) ch.curveZ = *it->second;
                    }

                    // Update duration from actual curve data when stack timing is unavailable
                    if (clip.duration <= 0.0f)
                    {
                        for (const FBXAnimCurve* crv : {&ch.curveX, &ch.curveY, &ch.curveZ})
                        {
                            if (!crv->keyTimes.empty())
                            {
                                float t = static_cast<float>(
                                    static_cast<double>(crv->keyTimes.back()) / FBX_TIME_UNIT);
                                clip.duration = std::max(clip.duration, t);
                            }
                        }
                    }

                    clip.channels.push_back(std::move(ch));
                }
            }

            if (!clip.channels.empty() && clip.duration > 0.0f)
            {
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    (std::wstring(L"[FBXAnimator] Clip '") +
                     std::wstring(clip.name.begin(), clip.name.end()) +
                     L"' duration=" + std::to_wstring(clip.duration) +
                     L"s  channels=" + std::to_wstring(clip.channels.size())).c_str());

                m_clips.push_back(std::move(clip));
            }
        }

        m_isInitialized = !m_clips.empty();

        debug.logLevelMessage(LogLevel::LOG_INFO,
            (std::wstring(L"[FBXAnimator] ParseAnimationsFromFBX complete: ") +
             std::to_wstring(m_clips.size()) + L" clip(s) loaded.").c_str());

        return true;
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "FBXAnimator::ParseAnimationsFromFBX");
        return false;
    }
}

//==============================================================================
// CreateAnimationInstance
//==============================================================================
bool FBXAnimator::CreateAnimationInstance(int animationIndex, int parentModelID)
{
    if (animationIndex < 0 || animationIndex >= static_cast<int>(m_clips.size()))
    {
        LogError(L"CreateAnimationInstance: invalid clip index " + std::to_wstring(animationIndex));
        return false;
    }

    // Update existing instance if one already exists for this model
    for (auto& inst : m_animationInstances)
    {
        if (inst.parentModelID == parentModelID)
        {
            inst.animationIndex = animationIndex;
            inst.currentTime    = m_clips[animationIndex].startTime;
            inst.isPlaying      = false;
            return true;
        }
    }

    // Create a fresh instance
    AnimationInstance inst;
    inst.animationIndex = animationIndex;
    inst.parentModelID  = parentModelID;
    inst.currentTime    = m_clips[animationIndex].startTime;
    inst.playbackSpeed  = 1.0f;
    inst.isPlaying      = false;
    inst.isLooping      = true;
    m_animationInstances.push_back(inst);
    return true;
}

//==============================================================================
// StartAnimation
//==============================================================================
bool FBXAnimator::StartAnimation(int parentModelID, int animationIndex)
{
    if (animationIndex < 0 || animationIndex >= static_cast<int>(m_clips.size()))
        return false;

    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (!inst)
    {
        AnimationInstance newInst;
        newInst.parentModelID  = parentModelID;
        newInst.animationIndex = animationIndex;
        newInst.currentTime    = m_clips[animationIndex].startTime;
        newInst.playbackSpeed  = 1.0f;
        newInst.isPlaying      = true;
        newInst.isLooping      = true;
        m_animationInstances.push_back(newInst);
    }
    else
    {
        inst->animationIndex = animationIndex;
        inst->currentTime    = m_clips[animationIndex].startTime;
        inst->isPlaying      = true;
    }

    #if defined(_DEBUG_FBXANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            (std::wstring(L"[FBXAnimator] StartAnimation: parentID=") +
             std::to_wstring(parentModelID) + L" clipIdx=" + std::to_wstring(animationIndex)).c_str());
    #endif
    return true;
}

//==============================================================================
// StopAnimation
//==============================================================================
bool FBXAnimator::StopAnimation(int parentModelID)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst)
    {
        inst->isPlaying  = false;
        inst->currentTime = 0.0f;
        return true;
    }
    return false;
}

//==============================================================================
// PauseAnimation
//==============================================================================
bool FBXAnimator::PauseAnimation(int parentModelID)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst) { inst->isPlaying = false; return true; }
    return false;
}

//==============================================================================
// ResumeAnimation
//==============================================================================
bool FBXAnimator::ResumeAnimation(int parentModelID)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst) { inst->isPlaying = true; return true; }
    return false;
}

//==============================================================================
// ForceAnimationReset
//==============================================================================
void FBXAnimator::ForceAnimationReset(int parentModelID)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst)
    {
        inst->currentTime = (inst->animationIndex >= 0 &&
                             inst->animationIndex < static_cast<int>(m_clips.size()))
                            ? m_clips[inst->animationIndex].startTime
                            : 0.0f;
        inst->isPlaying = true;
    }
}

//==============================================================================
// UpdateAnimations -- evaluates FBX curves, applies coordinate conversion,
// writes animLocal TRS, and recomposes world matrices via hierarchy walk.
// Mirrors the GLTFAnimator::UpdateAnimations() control flow exactly.
//==============================================================================
void FBXAnimator::UpdateAnimations(float deltaTime, Model* sceneModels, int maxModels)
{
    try
    {
        for (auto& inst : m_animationInstances)
        {
            if (!inst.isPlaying) continue;

            if (inst.animationIndex < 0 || inst.animationIndex >= static_cast<int>(m_clips.size()))
                continue;

            const FBXAnimationClip& clip = m_clips[inst.animationIndex];
            if (clip.duration <= 0.0f) continue;

            float animStart = clip.startTime;
            float animEnd   = clip.startTime + clip.duration;

            // --- Reset animated local TRS to base pose before applying channels ---
            ResetLocalTRSToBase(inst.parentModelID, sceneModels, maxModels);

            // --- Advance time with direction / bounce support ---
            float effectiveDelta = deltaTime * std::abs(inst.playbackSpeed);

            if (inst.direction == AnimationDirection::REVERSE)
            {
                effectiveDelta = -effectiveDelta;
            }
            else if (inst.direction == AnimationDirection::BOUNCE)
            {
                if (!inst.bounceGoingForward)
                    effectiveDelta = -effectiveDelta;
            }
            else
            {
                // FORWARD / NONE: honour playbackSpeed sign directly
                effectiveDelta = deltaTime * inst.playbackSpeed;
            }

            inst.currentTime += effectiveDelta;

            // --- Bounds handling: loop / stop / bounce ---
            if (inst.currentTime >= animEnd)
            {
                if (inst.direction == AnimationDirection::BOUNCE)
                {
                    inst.currentTime        = animEnd;
                    inst.bounceGoingForward = false;
                }
                else if (inst.isLooping)
                {
                    float overTime  = inst.currentTime - animEnd;
                    inst.currentTime = animStart;
                    if (overTime > 0.0001f && overTime < clip.duration)
                        inst.currentTime = animStart + overTime;
                }
                else
                {
                    inst.currentTime = animEnd;
                    inst.isPlaying   = false;
                }
            }
            else if (inst.currentTime < animStart)
            {
                if (inst.direction == AnimationDirection::BOUNCE)
                {
                    inst.currentTime        = animStart;
                    inst.bounceGoingForward = true;
                    if (!inst.isLooping)
                        inst.isPlaying = false;
                }
                else if (inst.isLooping)
                {
                    float underTime  = animStart - inst.currentTime;
                    inst.currentTime = animEnd - underTime;
                    if (inst.currentTime <= animStart || inst.currentTime >= animEnd)
                        inst.currentTime = animEnd - 0.001f;
                }
                else
                {
                    inst.currentTime = animStart;
                    inst.isPlaying   = false;
                }
            }

            // Safety clamp
            inst.currentTime = std::clamp(inst.currentTime, animStart, animEnd);

            // --- Apply all channels ---
            for (const auto& ch : clip.channels)
                ApplyChannelToSlot(ch, inst.currentTime, sceneModels, maxModels);

            // --- Recompose world matrices from updated local TRS ---
            RecomposeWorldFromLocalTRS(inst.parentModelID, sceneModels, maxModels);
        }
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "FBXAnimator::UpdateAnimations");
    }
}

//==============================================================================
// ApplyChannelToSlot
// Evaluates the three FBX curves for one channel and writes the result into
// the target model's animLocalTranslation / Rotation / Scale.
//==============================================================================
void FBXAnimator::ApplyChannelToSlot(const FBXAnimChannel& ch, float timeSeconds,
                                      Model* sceneModels, int maxModels)
{
    int slot = ch.targetModelSlot;
    if (slot < 0 || slot >= maxModels || !sceneModels[slot].m_isLoaded) return;

    ModelInfo& info = sceneModels[slot].m_modelInfo;

    float vx = EvaluateCurveOrDefault(ch.curveX, timeSeconds, ch.defaultX);
    float vy = EvaluateCurveOrDefault(ch.curveY, timeSeconds, ch.defaultY);
    float vz = EvaluateCurveOrDefault(ch.curveZ, timeSeconds, ch.defaultZ);

    switch (ch.path)
    {
        case AnimationTargetPath::TRANSLATION:
        {
            // FBX right-handed -> engine left-handed: negate Z for Y-up
            XMFLOAT3 flipped = ApplyCoordFlip({ vx, vy, vz });
            info.animLocalTranslation = flipped;
            break;
        }

        case AnimationTargetPath::ROTATION:
        {
            // Pre-rotation is added in FBX right-handed space, then the whole
            // thing is converted to a quaternion in engine left-handed space.
            XMFLOAT3 totalEuler = {
                ch.preRotation.x + vx,
                ch.preRotation.y + vy,
                ch.preRotation.z + vz
            };

            XMMATRIX rotM       = EulerToMatrix(totalEuler, ch.rotationOrder);
            XMMATRIX rotFlipped = ApplyRotationFlip(rotM);
            XMFLOAT4 quat       = MatrixToQuaternion(rotFlipped);

            // Normalize and store
            XMVECTOR qv = XMLoadFloat4(&quat);
            qv = XMQuaternionNormalize(qv);
            XMStoreFloat4(&info.animLocalRotationQuat, qv);
            break;
        }

        case AnimationTargetPath::SCALE:
        {
            // Scale is not affected by coordinate handedness flip
            info.animLocalScale = { vx, vy, vz };
            break;
        }

        default: break;
    }
}

//==============================================================================
// EvaluateCurveOrDefault
//==============================================================================
float FBXAnimator::EvaluateCurveOrDefault(const FBXAnimCurve& curve,
                                           float timeSeconds, float def) const
{
    return curve.keyTimes.empty() ? def : EvaluateCurve(curve, timeSeconds);
}

//==============================================================================
// EvaluateCurve
// Evaluates an FBXAnimCurve at the given time (seconds).
// Interpolation type is derived from keyAttrFlags for each key segment.
//==============================================================================
float FBXAnimator::EvaluateCurve(const FBXAnimCurve& curve, float timeSeconds) const
{
    if (curve.keyTimes.empty())   return 0.0f;
    if (curve.keyTimes.size()==1) return curve.keyValues[0];

    // Convert seconds -> FBX time units for comparison
    int64_t timeFBX = static_cast<int64_t>(
        static_cast<double>(timeSeconds) * FBX_TIME_UNIT);

    if (timeFBX <= curve.keyTimes.front()) return curve.keyValues.front();
    if (timeFBX >= curve.keyTimes.back())  return curve.keyValues.back();

    for (size_t i = 0; i + 1 < curve.keyTimes.size(); ++i)
    {
        if (timeFBX >= curve.keyTimes[i] && timeFBX < curve.keyTimes[i + 1])
        {
            int interp = GetInterpolationTypeForKey(curve, static_cast<int>(i));

            if (interp == 0) // Constant / Step
                return curve.keyValues[i];

            // Linear (also used as approximation for Cubic)
            double dt = static_cast<double>(curve.keyTimes[i + 1] - curve.keyTimes[i]);
            double t  = dt > 0.0 ?
                static_cast<double>(timeFBX - curve.keyTimes[i]) / dt : 0.0;
            return curve.keyValues[i] +
                   static_cast<float>(t) * (curve.keyValues[i + 1] - curve.keyValues[i]);
        }
    }
    return curve.keyValues.back();
}

//==============================================================================
// GetInterpolationTypeForKey
// Returns 0 (Constant/Step) or 1 (Linear/Cubic approximated as linear).
// keyAttrFlags / keyAttrRefCount encode a run-length list of flags:
//   keyAttrRefCount[i] keys share keyAttrFlags[i].
// FBX flag bits: 0x00000002 = Constant, 0x00000004 = Linear, 0x00000008 = Cubic.
//==============================================================================
int FBXAnimator::GetInterpolationTypeForKey(const FBXAnimCurve& curve, int keyIndex) const
{
    if (curve.keyAttrFlags.empty()) return 1; // Default: linear

    int counted = 0;
    for (size_t i = 0; i < curve.keyAttrFlags.size(); ++i)
    {
        int refCount = (i < curve.keyAttrRefCount.size()) ? curve.keyAttrRefCount[i] : 1;
        counted += refCount;
        if (keyIndex < counted)
        {
            // Bit 1 (0x2) = Constant/Step
            if (curve.keyAttrFlags[i] & 0x00000002) return 0;
            // Bit 2 (0x4) = Linear, Bit 3 (0x8) = Cubic -> both treated as linear
            return 1;
        }
    }
    return 1;
}

//==============================================================================
// ApplyCoordFlip
// Converts a vector from FBX right-handed Y-up to engine left-handed Y-up
// by negating Z.  Matches FBXImporter::ApplyCoordFlip() for Y-up scenes.
//==============================================================================
XMFLOAT3 FBXAnimator::ApplyCoordFlip(const XMFLOAT3& v) const
{
    // Y-up, Z-front (Blender default): negate Z to flip handedness.
    // Z-up scenes (m_upAxis==2) would need a different transform, but
    // Blender and most Maya/Max FBX exports use Y-up after export.
    return { v.x, v.y, -v.z };
}

//==============================================================================
// ApplyRotationFlip
// Converts a rotation matrix from FBX right-handed Y-up to engine left-handed
// Y-up via similarity transform M*R*M where M = diag(1, 1, -1).
// Effect: negates _13, _23, _31, _32; leaves _33 unchanged.
//==============================================================================
XMMATRIX FBXAnimator::ApplyRotationFlip(const XMMATRIX& rotM) const
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, rotM);

    m._13 = -m._13;   // row 0, col 2
    m._23 = -m._23;   // row 1, col 2
    m._31 = -m._31;   // row 2, col 0
    m._32 = -m._32;   // row 2, col 1
    // m._33 is negated twice (row 2 AND col 2) -> unchanged

    return XMLoadFloat4x4(&m);
}

//==============================================================================
// EulerToMatrix
// Converts FBX Euler angles (degrees, right-handed) to a rotation matrix
// using the rotation order encoded by 'order' (0=XYZ ... 5=ZYX).
//==============================================================================
XMMATRIX FBXAnimator::EulerToMatrix(const XMFLOAT3& degXYZ, int order) const
{
    float rx = XMConvertToRadians(degXYZ.x);
    float ry = XMConvertToRadians(degXYZ.y);
    float rz = XMConvertToRadians(degXYZ.z);

    XMMATRIX Rx = XMMatrixRotationX(rx);
    XMMATRIX Ry = XMMatrixRotationY(ry);
    XMMATRIX Rz = XMMatrixRotationZ(rz);

    switch (order)
    {
        case 0: return Rx * Ry * Rz; // XYZ (Blender default)
        case 1: return Rx * Rz * Ry; // XZY
        case 2: return Ry * Rx * Rz; // YXZ
        case 3: return Ry * Rz * Rx; // YZX
        case 4: return Rz * Rx * Ry; // ZXY
        case 5: return Rz * Ry * Rx; // ZYX
        default: return Rx * Ry * Rz;
    }
}

//==============================================================================
// MatrixToQuaternion
// Extracts a unit quaternion from a rotation matrix using the DirectX
// row-vector Shepperd method (matches XMQuaternionRotationMatrix semantics).
//==============================================================================
XMFLOAT4 FBXAnimator::MatrixToQuaternion(const XMMATRIX& m) const
{
    XMVECTOR q = XMQuaternionRotationMatrix(m);
    q = XMQuaternionNormalize(q);
    XMFLOAT4 result;
    XMStoreFloat4(&result, q);
    return result;
}

//==============================================================================
// SlerpQuaternions
//==============================================================================
XMFLOAT4 FBXAnimator::SlerpQuaternions(const XMFLOAT4& q1, const XMFLOAT4& q2, float t) const
{
    XMVECTOR v1 = XMLoadFloat4(&q1);
    XMVECTOR v2 = XMLoadFloat4(&q2);
    XMVECTOR r  = XMQuaternionSlerp(v1, v2, t);
    XMFLOAT4 out;
    XMStoreFloat4(&out, r);
    return out;
}

//==============================================================================
// Hierarchy helpers  (identical logic to GLTFAnimator)
//==============================================================================
bool FBXAnimator::IsInHierarchy(int modelIndex, int rootParentID,
                                  const Model* sceneModels, int maxModels) const
{
    if (modelIndex < 0 || modelIndex >= maxModels) return false;
    if (rootParentID < 0 || rootParentID >= maxModels) return false;

    int current = modelIndex;
    while (current >= 0 && current < maxModels)
    {
        if (current == rootParentID) return true;
        int parent = sceneModels[current].m_modelInfo.iParentModelID;
        if (parent < 0) break;
        current = parent;
    }
    return false;
}

void FBXAnimator::ResetLocalTRSToBase(int rootParentID,
                                       Model* sceneModels, int maxModels) const
{
    for (int i = 0; i < maxModels; ++i)
    {
        if (!sceneModels[i].m_isLoaded) continue;
        if (!IsInHierarchy(i, rootParentID, sceneModels, maxModels)) continue;
        if (!sceneModels[i].m_modelInfo.bHasBaseLocalTRS) continue;

        ModelInfo& info = sceneModels[i].m_modelInfo;
        info.animLocalTranslation  = info.baseLocalTranslation;
        info.animLocalRotationQuat = info.baseLocalRotationQuat;
        info.animLocalScale        = info.baseLocalScale;
    }
}

void FBXAnimator::RecomposeWorldFromLocalTRS(int rootParentID,
                                              Model* sceneModels, int maxModels) const
{
    for (int i = 0; i < maxModels; ++i)
    {
        if (!sceneModels[i].m_isLoaded) continue;
        if (!IsInHierarchy(i, rootParentID, sceneModels, maxModels)) continue;

        const ModelInfo& info = sceneModels[i].m_modelInfo;
        const XMFLOAT3& t = info.animLocalTranslation;
        const XMFLOAT4& q = info.animLocalRotationQuat;
        const XMFLOAT3& s = info.animLocalScale;

        XMVECTOR tV = XMVectorSet(t.x, t.y, t.z, 1.0f);
        XMVECTOR qV = XMQuaternionNormalize(XMLoadFloat4(&q));
        XMVECTOR sV = XMVectorSet(s.x, s.y, s.z, 0.0f);

        XMMATRIX localM = XMMatrixScalingFromVector(sV) *
                           XMMatrixRotationQuaternion(qV) *
                           XMMatrixTranslationFromVector(tV);

        int parentID = info.iParentModelID;
        if (parentID >= 0 && parentID < maxModels && sceneModels[parentID].m_isLoaded)
            sceneModels[i].m_modelInfo.worldMatrix = sceneModels[parentID].m_modelInfo.worldMatrix * localM;
        else
            sceneModels[i].m_modelInfo.worldMatrix = localM;

        // Update convenience world position
        XMFLOAT4X4 w;
        XMStoreFloat4x4(&w, sceneModels[i].m_modelInfo.worldMatrix);
        sceneModels[i].m_modelInfo.position = XMFLOAT3(w._41, w._42, w._43);
    }
}

//==============================================================================
// Query and Control
//==============================================================================
int FBXAnimator::GetAnimationCount() const
{
    return static_cast<int>(m_clips.size());
}

const FBXAnimationClip* FBXAnimator::GetClip(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_clips.size()))
        return &m_clips[index];
    return nullptr;
}

AnimationInstance* FBXAnimator::GetAnimationInstance(int parentModelID)
{
    for (auto& inst : m_animationInstances)
        if (inst.parentModelID == parentModelID) return &inst;
    return nullptr;
}

bool FBXAnimator::IsAnimationPlaying(int parentModelID) const
{
    for (const auto& inst : m_animationInstances)
        if (inst.parentModelID == parentModelID) return inst.isPlaying;
    return false;
}

float FBXAnimator::GetAnimationTime(int parentModelID) const
{
    for (const auto& inst : m_animationInstances)
        if (inst.parentModelID == parentModelID) return inst.currentTime;
    return 0.0f;
}

float FBXAnimator::GetAnimationDuration(int clipIndex) const
{
    if (clipIndex >= 0 && clipIndex < static_cast<int>(m_clips.size()))
        return m_clips[clipIndex].duration;
    return 0.0f;
}

bool FBXAnimator::SetAnimationSpeed(int parentModelID, float speed)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst) { inst->playbackSpeed = speed; return true; }
    return false;
}

bool FBXAnimator::SetAnimationLooping(int parentModelID, bool looping)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst) { inst->isLooping = looping; return true; }
    return false;
}

bool FBXAnimator::SetAnimationTime(int parentModelID, float time)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (inst && inst->animationIndex >= 0 &&
        inst->animationIndex < static_cast<int>(m_clips.size()))
    {
        const FBXAnimationClip& clip = m_clips[inst->animationIndex];
        inst->currentTime = std::clamp(time, clip.startTime, clip.startTime + clip.duration);
        return true;
    }
    return false;
}

void FBXAnimator::SetAnimationDirection(int parentModelID, AnimationDirection direction)
{
    if (direction == AnimationDirection::NONE) return;

    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (!inst) return;

    inst->direction = direction;
    if (direction == AnimationDirection::BOUNCE)
        inst->bounceGoingForward = (inst->playbackSpeed >= 0.0f);
}

bool FBXAnimator::AtAnimationEndFrame(int parentModelID, int& frameIndex)
{
    const AnimationInstance* inst = nullptr;
    for (const auto& i : m_animationInstances)
        if (i.parentModelID == parentModelID) { inst = &i; break; }

    if (!inst || inst->animationIndex < 0 ||
        inst->animationIndex >= static_cast<int>(m_clips.size()))
        return false;

    const FBXAnimationClip& clip = m_clips[inst->animationIndex];
    float endTime = clip.startTime + clip.duration;

    if (inst->currentTime >= endTime)
    {
        // Use the key count of the first non-empty X curve from the first channel
        for (const auto& ch : clip.channels)
        {
            if (!ch.curveX.keyTimes.empty())
            {
                frameIndex = static_cast<int>(ch.curveX.keyTimes.size()) - 1;
                return true;
            }
        }
        frameIndex = 0;
        return true;
    }
    return false;
}

void FBXAnimator::HoldAnimationAtFrame(int parentModelID, int frameIndex)
{
    AnimationInstance* inst = GetAnimationInstance(parentModelID);
    if (!inst || inst->animationIndex < 0 ||
        inst->animationIndex >= static_cast<int>(m_clips.size()))
        return;

    const FBXAnimationClip& clip = m_clips[inst->animationIndex];
    for (const auto& ch : clip.channels)
    {
        if (ch.curveX.keyTimes.empty()) continue;

        int idx = std::clamp(frameIndex, 0,
                             static_cast<int>(ch.curveX.keyTimes.size()) - 1);
        inst->currentTime = static_cast<float>(
            static_cast<double>(ch.curveX.keyTimes[idx]) / FBX_TIME_UNIT);
        inst->isPlaying   = false;
        return;
    }
}

//==============================================================================
// Cleanup
//==============================================================================
void FBXAnimator::ClearAllAnimations()
{
    m_clips.clear();
    m_animationInstances.clear();
    m_isInitialized = false;
}

void FBXAnimator::RemoveAnimationInstance(int parentModelID)
{
    auto it = std::remove_if(m_animationInstances.begin(), m_animationInstances.end(),
        [parentModelID](const AnimationInstance& i){ return i.parentModelID == parentModelID; });
    if (it != m_animationInstances.end())
        m_animationInstances.erase(it, m_animationInstances.end());
}

//==============================================================================
// Logging helpers
//==============================================================================
void FBXAnimator::LogInfo(const std::wstring& msg) const
{
    #if defined(_DEBUG_FBXANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_INFO, (L"[FBXAnimator] " + msg).c_str());
    #else
        (void)msg;
    #endif
}
void FBXAnimator::LogWarning(const std::wstring& msg) const
{
    #if defined(_DEBUG_FBXANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, (L"[FBXAnimator] " + msg).c_str());
    #else
        (void)msg;
    #endif
}
void FBXAnimator::LogError(const std::wstring& msg) const
{
    #if defined(_DEBUG_FBXANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, (L"[FBXAnimator] " + msg).c_str());
    #else
        (void)msg;
    #endif
}
