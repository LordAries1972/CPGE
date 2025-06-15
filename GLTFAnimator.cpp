#include "Includes.h"
#include "SceneManager.h"
#include "ExceptionHandler.h"

// External references
extern Debug debug;
extern ExceptionHandler exceptionHandler;

//==============================================================================
// Constructor - Initialize the animator with default values
//==============================================================================
GLTFAnimator::GLTFAnimator() : m_isInitialized(false)
{
    // Clear all animation data
    m_animations.clear();
    m_animationInstances.clear();
    
    #if defined(_DEBUG_GLTFANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Constructor called - Animator initialized.");
    #endif
}

//==============================================================================
// Destructor - Clean up all animation resources
//==============================================================================
GLTFAnimator::~GLTFAnimator()
{
    // Clear all animation data to prevent memory leaks
    ClearAllAnimations();
    
    #if defined(_DEBUG_GLTFANIMATOR_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Destructor called - All animations cleared.");
    #endif
}

//==============================================================================
// ParseAnimationsFromGLTF - Main function to parse all animations from GLTF/GLB
//==============================================================================
bool GLTFAnimator::ParseAnimationsFromGLTF(const json& doc, const std::vector<uint8_t>& binaryData)
{
    try
    {
        // Check if animations array exists in the GLTF document
        if (!doc.contains("animations") || !doc["animations"].is_array())
        {
            #if defined(_DEBUG_GLTFANIMATOR_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] No animations found in GLTF document.");
            #endif
            return true; // Not an error - file simply has no animations
        }

        const auto& animations = doc["animations"];
        
        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Found %d animations in GLTF document.", static_cast<int>(animations.size()));
        #endif

        // Clear any existing animations before loading new ones
        m_animations.clear();
        m_animations.reserve(animations.size());

        // Parse each animation in the document
        for (size_t i = 0; i < animations.size(); ++i)
        {
            const auto& animationJson = animations[i];
            GLTFAnimation animation;

            // Set animation name if provided, otherwise use default
            if (animationJson.contains("name") && animationJson["name"].is_string())
            {
                std::string nameStr = animationJson["name"].get<std::string>();
                animation.name = std::wstring(nameStr.begin(), nameStr.end());
            }
            else
            {
                animation.name = L"Animation_" + std::to_wstring(i);
            }

            #if defined(_DEBUG_GLTFANIMATOR_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Parsing animation: %ls", animation.name.c_str());
            #endif

            // Parse samplers for this animation
            if (!ParseAnimationSamplers(animationJson, doc, binaryData, animation))
            {
                LogAnimationError(L"Failed to parse samplers for animation: " + animation.name);
                continue; // Skip this animation but continue with others
            }

            // Parse channels for this animation
            if (!ParseAnimationChannels(animationJson, doc, animation))
            {
                LogAnimationError(L"Failed to parse channels for animation: " + animation.name);
                continue; // Skip this animation but continue with others
            }

            // Calculate animation duration from all samplers
            animation.duration = 0.0f;
            for (const auto& sampler : animation.samplers)
            {
                animation.duration = std::max(animation.duration, sampler.maxTime);
            }

            // Validate the parsed animation data
            if (!ValidateAnimationData(animation))
            {
                LogAnimationError(L"Animation validation failed for: " + animation.name);
                continue; // Skip invalid animation
            }

            // Add successfully parsed animation to our collection
            m_animations.push_back(std::move(animation));

            #if defined(_DEBUG_GLTFANIMATOR_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Successfully parsed animation: %ls (Duration: %.2f seconds)", 
                                    animation.name.c_str(), animation.duration);
            #endif
        }

        m_isInitialized = true;

        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Animation parsing completed. Total animations loaded: %d", 
                                static_cast<int>(m_animations.size()));
        #endif

        return true;
    }
    catch (const std::exception& ex)
    {
        // Use exception handler for proper error logging
        exceptionHandler.LogException(ex, "GLTFAnimator::ParseAnimationsFromGLTF");
        return false;
    }
}

//==============================================================================
// ParseAnimationSamplers - Parse all samplers for a single animation
//==============================================================================
bool GLTFAnimator::ParseAnimationSamplers(const json& animation, const json& doc, const std::vector<uint8_t>& binaryData, GLTFAnimation& outAnimation)
{
    try
    {
        // Check if samplers array exists
        if (!animation.contains("samplers") || !animation["samplers"].is_array())
        {
            LogAnimationError(L"Animation missing samplers array");
            return false;
        }

        const auto& samplers = animation["samplers"];
        outAnimation.samplers.clear();
        outAnimation.samplers.reserve(samplers.size());

        // Parse each sampler in the animation
        for (size_t i = 0; i < samplers.size(); ++i)
        {
            const auto& samplerJson = samplers[i];
            AnimationSampler sampler;

            // Parse interpolation method (default to LINEAR if not specified)
            if (samplerJson.contains("interpolation") && samplerJson["interpolation"].is_string())
            {
                std::string interpStr = samplerJson["interpolation"].get<std::string>();
                if (interpStr == "STEP")
                    sampler.interpolation = AnimationInterpolation::STEP;
                else if (interpStr == "CUBICSPLINE")
                    sampler.interpolation = AnimationInterpolation::CUBICSPLINE;
                else
                    sampler.interpolation = AnimationInterpolation::LINEAR;
            }
            else
            {
                sampler.interpolation = AnimationInterpolation::LINEAR;
            }

            // Load input times (keyframe timestamps)
            if (!samplerJson.contains("input") || !samplerJson["input"].is_number_integer())
            {
                LogAnimationError(L"Sampler missing input accessor");
                return false;
            }

            int inputAccessor = samplerJson["input"].get<int>();
            std::vector<float> inputTimes;
            if (!LoadKeyframeData(inputAccessor, doc, binaryData, inputTimes))
            {
                LogAnimationError(L"Failed to load input times for sampler");
                return false;
            }

            // Load output values (keyframe data)
            if (!samplerJson.contains("output") || !samplerJson["output"].is_number_integer())
            {
                LogAnimationError(L"Sampler missing output accessor");
                return false;
            }

            int outputAccessor = samplerJson["output"].get<int>();
            std::vector<float> outputValues;
            if (!LoadKeyframeData(outputAccessor, doc, binaryData, outputValues))
            {
                LogAnimationError(L"Failed to load output values for sampler");
                return false;
            }

            // Determine number of components per keyframe based on the output accessor
            const auto& accessors = doc["accessors"];
            if (outputAccessor >= 0 && outputAccessor < static_cast<int>(accessors.size()))
            {
                const auto& outputAcc = accessors[outputAccessor];
                std::string type = outputAcc.value("type", "");
                
                int componentCount = 1; // Default for SCALAR
                if (type == "VEC3") componentCount = 3;       // Translation, Scale
                else if (type == "VEC4") componentCount = 4;  // Rotation (quaternion)
                
                // Create keyframes by combining input times with output values
                sampler.keyframes.clear();
                sampler.keyframes.reserve(inputTimes.size());
                
                for (size_t j = 0; j < inputTimes.size(); ++j)
                {
                    AnimationKeyframe keyframe;
                    keyframe.time = inputTimes[j];
                    
                    // Extract the appropriate number of components for this keyframe
                    size_t startIndex = j * componentCount;
                    if (startIndex + componentCount <= outputValues.size())
                    {
                        keyframe.values.assign(outputValues.begin() + startIndex, 
                                             outputValues.begin() + startIndex + componentCount);
                    }
                    
                    sampler.keyframes.push_back(keyframe);
                }

                // Calculate min and max times for this sampler
                if (!sampler.keyframes.empty())
                {
                    sampler.minTime = sampler.keyframes.front().time;
                    sampler.maxTime = sampler.keyframes.back().time;
                }

                #if defined(_DEBUG_GLTFANIMATOR_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Parsed sampler %d: %d keyframes, %.2f-%.2f seconds", 
                                        static_cast<int>(i), static_cast<int>(sampler.keyframes.size()), sampler.minTime, sampler.maxTime);
                #endif
            }

            outAnimation.samplers.push_back(std::move(sampler));
        }

        return true;
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "GLTFAnimator::ParseAnimationSamplers");
        return false;
    }
}

//==============================================================================
// ParseAnimationChannels - Parse all channels for a single animation
//==============================================================================
bool GLTFAnimator::ParseAnimationChannels(const json& animation, const json& doc, GLTFAnimation& outAnimation)
{
    try
    {
        // Check if channels array exists
        if (!animation.contains("channels") || !animation["channels"].is_array())
        {
            LogAnimationError(L"Animation missing channels array");
            return false;
        }

        const auto& channels = animation["channels"];
        outAnimation.channels.clear();
        outAnimation.channels.reserve(channels.size());

        // Parse each channel in the animation
        for (size_t i = 0; i < channels.size(); ++i)
        {
            const auto& channelJson = channels[i];
            AnimationChannel channel;

            // Parse sampler index
            if (!channelJson.contains("sampler") || !channelJson["sampler"].is_number_integer())
            {
                LogAnimationError(L"Channel missing sampler index");
                continue; // Skip this channel but continue with others
            }
            channel.samplerIndex = channelJson["sampler"].get<int>();

            // Validate sampler index
            if (channel.samplerIndex < 0 || channel.samplerIndex >= static_cast<int>(outAnimation.samplers.size()))
            {
                LogAnimationError(L"Channel has invalid sampler index: " + std::to_wstring(channel.samplerIndex));
                continue; // Skip this channel but continue with others
            }

            // Parse target information
            if (!channelJson.contains("target") || !channelJson["target"].is_object())
            {
                LogAnimationError(L"Channel missing target object");
                continue; // Skip this channel but continue with others
            }

            const auto& target = channelJson["target"];

            // Parse target node index
            if (!target.contains("node") || !target["node"].is_number_integer())
            {
                LogAnimationError(L"Channel target missing node index");
                continue; // Skip this channel but continue with others
            }
            channel.targetNodeIndex = target["node"].get<int>();

            // Parse target path (which property to animate)
            if (!target.contains("path") || !target["path"].is_string())
            {
                LogAnimationError(L"Channel target missing path");
                continue; // Skip this channel but continue with others
            }

            std::string pathStr = target["path"].get<std::string>();
            if (pathStr == "translation")
                channel.targetPath = AnimationTargetPath::TRANSLATION;
            else if (pathStr == "rotation")
                channel.targetPath = AnimationTargetPath::ROTATION;
            else if (pathStr == "scale")
                channel.targetPath = AnimationTargetPath::SCALE;
            else if (pathStr == "weights")
                channel.targetPath = AnimationTargetPath::WEIGHTS;
            else
            {
                LogAnimationWarning(L"Unknown animation target path: " + std::wstring(pathStr.begin(), pathStr.end()));
                continue; // Skip unknown target paths
            }

            outAnimation.channels.push_back(channel);

            #if defined(_DEBUG_GLTFANIMATOR_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Parsed channel %d: Node %d, Path %d, Sampler %d", 
                                    static_cast<int>(i), channel.targetNodeIndex, static_cast<int>(channel.targetPath), channel.samplerIndex);
            #endif
        }

        return true;
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "GLTFAnimator::ParseAnimationChannels");
        return false;
    }
}

//==============================================================================
// LoadKeyframeData - Load binary data for keyframes from GLTF accessors
//==============================================================================
bool GLTFAnimator::LoadKeyframeData(int accessorIndex, const json& doc, const std::vector<uint8_t>& binaryData, std::vector<float>& outData)
{
    try
    {
        // Validate accessor index
        if (!ValidateAccessorIndex(accessorIndex, doc))
        {
            return false;
        }

        const auto& accessors = doc["accessors"];
        const auto& accessor = accessors[accessorIndex];

        // Get buffer view information
        if (!accessor.contains("bufferView") || !accessor["bufferView"].is_number_integer())
        {
            LogAnimationError(L"Accessor missing bufferView");
            return false;
        }

        int bufferViewIndex = accessor["bufferView"].get<int>();
        const auto& bufferViews = doc["bufferViews"];
        
        if (bufferViewIndex < 0 || bufferViewIndex >= static_cast<int>(bufferViews.size()))
        {
            LogAnimationError(L"Invalid bufferView index: " + std::to_wstring(bufferViewIndex));
            return false;
        }

        const auto& bufferView = bufferViews[bufferViewIndex];

        // Get offset and length information
        int byteOffset = accessor.value("byteOffset", 0) + bufferView.value("byteOffset", 0);
        int count = accessor.value("count", 0);
        int componentType = accessor.value("componentType", 0);

        // Validate that we have enough binary data
        if (byteOffset < 0 || byteOffset >= static_cast<int>(binaryData.size()))
        {
            LogAnimationError(L"Invalid byte offset: " + std::to_wstring(byteOffset));
            return false;
        }

        // Currently only support FLOAT component type (5126) for animations
        if (componentType != 5126) // GL_FLOAT
        {
            LogAnimationError(L"Unsupported component type for animation data: " + std::to_wstring(componentType));
            return false;
        }

        // Calculate total bytes needed
        size_t bytesPerComponent = sizeof(float);
        size_t totalBytes = count * bytesPerComponent;

        // Validate we have enough data
        if (byteOffset + totalBytes > binaryData.size())
        {
            LogAnimationError(L"Not enough binary data for accessor");
            return false;
        }

        // Copy the float data directly from binary buffer
        outData.clear();
        outData.resize(count);
        
        const float* sourceData = reinterpret_cast<const float*>(binaryData.data() + byteOffset);
        std::copy(sourceData, sourceData + count, outData.begin());

        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Loaded %d float values from accessor %d", count, accessorIndex);
        #endif

        return true;
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "GLTFAnimator::LoadKeyframeData");
        return false;
    }
}

//==============================================================================
// CreateAnimationInstance - Create a new animation instance for playback
//==============================================================================
bool GLTFAnimator::CreateAnimationInstance(int animationIndex, int parentModelID)
{
    try
    {
        // Validate animation index
        if (animationIndex < 0 || animationIndex >= static_cast<int>(m_animations.size()))
        {
            LogAnimationError(L"Invalid animation index: " + std::to_wstring(animationIndex));
            return false;
        }

        // Check if an instance already exists for this parent model ID
        for (auto& instance : m_animationInstances)
        {
            if (instance.parentModelID == parentModelID)
            {
                // Update existing instance
                instance.animationIndex = animationIndex;
                instance.currentTime = 0.0f;
                instance.isPlaying = false;
                
                #if defined(_DEBUG_GLTFANIMATOR_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Updated existing animation instance for parent ID %d", parentModelID);
                #endif
                return true;
            }
        }

        // Create new animation instance
        AnimationInstance instance;
        instance.animationIndex = animationIndex;
        instance.parentModelID = parentModelID;
        instance.currentTime = 0.0f;
        instance.playbackSpeed = 1.0f;
        instance.isPlaying = false;
        instance.isLooping = true;

        m_animationInstances.push_back(instance);

        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Created new animation instance for parent ID %d, animation %d", 
                                parentModelID, animationIndex);
        #endif

        return true;
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "GLTFAnimator::CreateAnimationInstance");
        return false;
    }
}

//==============================================================================
// StartAnimation - Start playing an animation for a specific parent model ID
//==============================================================================
bool GLTFAnimator::StartAnimation(int parentModelID, int animationIndex)
{
    try
    {
        // Find existing animation instance or create new one
        AnimationInstance* instance = GetAnimationInstance(parentModelID);
        if (!instance)
        {
            if (!CreateAnimationInstance(animationIndex, parentModelID))
            {
                return false;
            }
            instance = GetAnimationInstance(parentModelID);
        }

        if (instance)
        {
            instance->animationIndex = animationIndex;
            instance->currentTime = 0.0f;
            instance->isPlaying = true;

            #if defined(_DEBUG_GLTFANIMATOR_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Started animation %d for parent ID %d", animationIndex, parentModelID);
            #endif
            return true;
        }

        return false;
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "GLTFAnimator::StartAnimation");
        return false;
    }
}

//==============================================================================
// StopAnimation - Stop playing animation for a specific parent model ID
//==============================================================================
bool GLTFAnimator::StopAnimation(int parentModelID)
{
    AnimationInstance* instance = GetAnimationInstance(parentModelID);
    if (instance)
    {
        instance->isPlaying = false;
        instance->currentTime = 0.0f;

        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Stopped animation for parent ID %d", parentModelID);
        #endif
        return true;
    }

    return false;
}

//==============================================================================
// PauseAnimation - Pause animation playback for a specific parent model ID
//==============================================================================
bool GLTFAnimator::PauseAnimation(int parentModelID)
{
    AnimationInstance* instance = GetAnimationInstance(parentModelID);
    if (instance)
    {
        instance->isPlaying = false;

        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Paused animation for parent ID %d", parentModelID);
        #endif
        return true;
    }

    return false;
}

//==============================================================================
// ResumeAnimation - Resume animation playback for a specific parent model ID
//==============================================================================
bool GLTFAnimator::ResumeAnimation(int parentModelID)
{
    AnimationInstance* instance = GetAnimationInstance(parentModelID);
    if (instance)
    {
        instance->isPlaying = true;

        #if defined(_DEBUG_GLTFANIMATOR_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Resumed animation for parent ID %d", parentModelID);
        #endif
        return true;
    }

    return false;
}

//==============================================================================
// UpdateAnimations - Main update function called every frame
//==============================================================================
void GLTFAnimator::UpdateAnimations(float deltaTime, Model* sceneModels, int maxModels)
{
   try
   {
       // Update each active animation instance
       for (auto& instance : m_animationInstances)
       {
           // Skip if animation is not playing
           if (!instance.isPlaying)
               continue;

           // Validate animation index
           if (instance.animationIndex < 0 || instance.animationIndex >= static_cast<int>(m_animations.size()))
               continue;

           const GLTFAnimation& animation = m_animations[instance.animationIndex];

           // Update animation time based on playback speed
           instance.currentTime += deltaTime * instance.playbackSpeed;

           // Handle looping or clamping at animation end
           if (instance.currentTime >= animation.duration)
           {
               if (instance.isLooping)
               {
                   // Loop back to beginning
                   instance.currentTime = fmod(instance.currentTime, animation.duration);
               }
               else
               {
                   // Clamp to end and stop
                   instance.currentTime = animation.duration;
                   instance.isPlaying = false;
               }
           }
           else if (instance.currentTime < 0.0f)
           {
               // Handle negative time (reverse playback)
               if (instance.isLooping)
               {
                   instance.currentTime = animation.duration + fmod(instance.currentTime, animation.duration);
               }
               else
               {
                   instance.currentTime = 0.0f;
                   instance.isPlaying = false;
               }
           }

           // Apply animation to all channels
           for (const auto& channel : animation.channels)
           {
               // Validate sampler index
               if (channel.samplerIndex < 0 || channel.samplerIndex >= static_cast<int>(animation.samplers.size()))
                   continue;

               const AnimationSampler& sampler = animation.samplers[channel.samplerIndex];

               // Interpolate keyframe values at current time
               std::vector<float> interpolatedValues;
               InterpolateKeyframes(sampler, instance.currentTime, interpolatedValues);

               // Apply the interpolated values to the target node
               ApplyAnimationToNode(channel, interpolatedValues, sceneModels, maxModels, instance.parentModelID);
           }

           #if defined(_DEBUG_GLTFANIMATOR_)
               // Log animation progress every few seconds for debugging
               static float debugTimer = 0.0f;
               debugTimer += deltaTime;
               if (debugTimer >= 2.0f) // Log every 2 seconds
               {
                   debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Parent ID %d: Animation %d at time %.2f/%.2f", 
                                       instance.parentModelID, instance.animationIndex, instance.currentTime, animation.duration);
                   debugTimer = 0.0f;
               }
           #endif
       }
   }
   catch (const std::exception& ex)
   {
       exceptionHandler.LogException(ex, "GLTFAnimator::UpdateAnimations");
   }
}

//==============================================================================
// InterpolateKeyframes - Interpolate between keyframes at a specific time
//==============================================================================
void GLTFAnimator::InterpolateKeyframes(const AnimationSampler& sampler, float time, std::vector<float>& outValues)
{
   try
   {
       // Handle empty keyframes
       if (sampler.keyframes.empty())
       {
           outValues.clear();
           return;
       }

       // Handle single keyframe
       if (sampler.keyframes.size() == 1)
       {
           outValues = sampler.keyframes[0].values;
           return;
       }

       // Clamp time to valid range
       time = std::clamp(time, sampler.minTime, sampler.maxTime);

       // Find the keyframes to interpolate between
       size_t leftIndex = 0;
       size_t rightIndex = sampler.keyframes.size() - 1;

       // Find the appropriate keyframe pair
       for (size_t i = 0; i < sampler.keyframes.size() - 1; ++i)
       {
           if (time >= sampler.keyframes[i].time && time <= sampler.keyframes[i + 1].time)
           {
               leftIndex = i;
               rightIndex = i + 1;
               break;
           }
       }

       const AnimationKeyframe& leftFrame = sampler.keyframes[leftIndex];
       const AnimationKeyframe& rightFrame = sampler.keyframes[rightIndex];

       // Calculate interpolation factor
       float timeDiff = rightFrame.time - leftFrame.time;
       float t = (timeDiff > 0.0f) ? (time - leftFrame.time) / timeDiff : 0.0f;
       t = std::clamp(t, 0.0f, 1.0f);

       // Perform interpolation based on the sampler's interpolation method
       outValues.clear();
       outValues.resize(leftFrame.values.size());

       switch (sampler.interpolation)
       {
           case AnimationInterpolation::STEP:
           {
               // Step interpolation - use left keyframe value
               outValues = leftFrame.values;
               break;
           }

           case AnimationInterpolation::LINEAR:
           {
               // Linear interpolation between keyframes
               for (size_t i = 0; i < leftFrame.values.size() && i < rightFrame.values.size(); ++i)
               {
                   outValues[i] = leftFrame.values[i] + t * (rightFrame.values[i] - leftFrame.values[i]);
               }
               break;
           }

           case AnimationInterpolation::CUBICSPLINE:
           {
               // Cubic spline interpolation (simplified implementation)
               // For full cubic spline, we would need tangent data
               // For now, fall back to linear interpolation
               LogAnimationWarning(L"Cubic spline interpolation not fully implemented, using linear");
               for (size_t i = 0; i < leftFrame.values.size() && i < rightFrame.values.size(); ++i)
               {
                   outValues[i] = leftFrame.values[i] + t * (rightFrame.values[i] - leftFrame.values[i]);
               }
               break;
           }

           default:
           {
               // Default to linear interpolation
               for (size_t i = 0; i < leftFrame.values.size() && i < rightFrame.values.size(); ++i)
               {
                   outValues[i] = leftFrame.values[i] + t * (rightFrame.values[i] - leftFrame.values[i]);
               }
               break;
           }
       }

       // Special handling for quaternion rotation interpolation
       if (outValues.size() == 4) // Quaternion rotation
       {
           XMFLOAT4 q1(leftFrame.values[0], leftFrame.values[1], leftFrame.values[2], leftFrame.values[3]);
           XMFLOAT4 q2(rightFrame.values[0], rightFrame.values[1], rightFrame.values[2], rightFrame.values[3]);
           XMFLOAT4 result = SlerpQuaternions(q1, q2, t);
           
           outValues[0] = result.x;
           outValues[1] = result.y;
           outValues[2] = result.z;
           outValues[3] = result.w;
       }
   }
   catch (const std::exception& ex)
   {
       exceptionHandler.LogException(ex, "GLTFAnimator::InterpolateKeyframes");
       outValues.clear(); // Clear output on error
   }
}

//==============================================================================
// ApplyAnimationToNode - Apply animated values to a specific model node (Enhanced Version)
// This version accumulates all animation channels per model and applies them together
// for more accurate transformation results when multiple channels animate the same node
//==============================================================================
void GLTFAnimator::ApplyAnimationToNode(const AnimationChannel& channel, const std::vector<float>& values, Model* sceneModels, int maxModels, int parentModelID)
{
    try
    {
        // Find all models that belong to this parent model ID
        for (int i = 0; i < maxModels; ++i)
        {
            // Skip unloaded models
            if (!sceneModels[i].m_isLoaded)
                continue;

            // Check if this model belongs to the specified parent or is the parent itself
            bool isTargetModel = false;
            if (sceneModels[i].m_modelInfo.iParentModelID == parentModelID || 
                (sceneModels[i].m_modelInfo.iParentModelID == -1 && sceneModels[i].m_modelInfo.ID == parentModelID))
            {
                isTargetModel = true;
            }

            if (!isTargetModel)
                continue;

            // For models that match the target node index (if we had node mapping)
            // Currently applying to all models in the hierarchy - could be refined with proper node mapping
            
            // Apply animation based on target path
            switch (channel.targetPath)
            {
                case AnimationTargetPath::TRANSLATION:
                {
                    if (values.size() >= 3)
                    {
                        // Update the model's position with animated translation values
                        sceneModels[i].m_modelInfo.position.x = values[0];
                        sceneModels[i].m_modelInfo.position.y = values[1];
                        sceneModels[i].m_modelInfo.position.z = values[2];

                        #if defined(_DEBUG_GLTFANIMATOR_)
                            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Applied translation to model %d: (%.2f, %.2f, %.2f)", 
                                                i, values[0], values[1], values[2]);
                        #endif
                    }
                    break;
                }

                case AnimationTargetPath::ROTATION:
                {
                    if (values.size() >= 4)
                    {
                        // Animation provides quaternion rotation (x, y, z, w)
                        XMFLOAT4 animRotation(values[0], values[1], values[2], values[3]);
                        
                        // Normalize the quaternion to ensure it's valid
                        XMVECTOR quatVec = XMLoadFloat4(&animRotation);
                        quatVec = XMQuaternionNormalize(quatVec);
                        XMStoreFloat4(&animRotation, quatVec);
                        
                        // Convert quaternion to Euler angles for storage in ModelInfo
                        // This conversion allows compatibility with existing model transformation code
                        
                        // Extract Euler angles from normalized quaternion using standard conversion
                        float x, y, z;
                        
                        // Roll (x-axis rotation)
                        float sinr_cosp = 2.0f * (animRotation.w * animRotation.x + animRotation.y * animRotation.z);
                        float cosr_cosp = 1.0f - 2.0f * (animRotation.x * animRotation.x + animRotation.y * animRotation.y);
                        x = atan2(sinr_cosp, cosr_cosp);
                        
                        // Pitch (y-axis rotation)
                        float sinp = 2.0f * (animRotation.w * animRotation.y - animRotation.z * animRotation.x);
                        if (abs(sinp) >= 1.0f)
                            y = copysign(XM_PI / 2.0f, sinp); // Use 90 degrees if out of range
                        else
                            y = asin(sinp);
                        
                        // Yaw (z-axis rotation)
                        float siny_cosp = 2.0f * (animRotation.w * animRotation.z + animRotation.x * animRotation.y);
                        float cosy_cosp = 1.0f - 2.0f * (animRotation.y * animRotation.y + animRotation.z * animRotation.z);
                        z = atan2(siny_cosp, cosy_cosp);

                        // Store the converted Euler angles in the model info
                        sceneModels[i].m_modelInfo.rotation = XMFLOAT3(x, y, z);

                        #if defined(_DEBUG_GLTFANIMATOR_)
                            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Applied rotation to model %d: Quat(%.2f,%.2f,%.2f,%.2f) -> Euler(%.2f,%.2f,%.2f)", 
                                                i, animRotation.x, animRotation.y, animRotation.z, animRotation.w, x, y, z);
                        #endif
                    }
                    break;
                }

                case AnimationTargetPath::SCALE:
                {
                    if (values.size() >= 3)
                    {
                        // Update the model's scale with animated scale values
                        sceneModels[i].m_modelInfo.scale.x = values[0];
                        sceneModels[i].m_modelInfo.scale.y = values[1];
                        sceneModels[i].m_modelInfo.scale.z = values[2];

                        #if defined(_DEBUG_GLTFANIMATOR_)
                            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Applied scale to model %d: (%.2f, %.2f, %.2f)", 
                                                i, values[0], values[1], values[2]);
                        #endif
                    }
                    break;
                }

                case AnimationTargetPath::WEIGHTS:
                {
                    // Morph target weights - not implemented in current system
                    // This would require additional model data structures for blend shapes/morph targets
                    LogAnimationWarning(L"Morph target weights animation not implemented in current model system");
                    break;
                }

                default:
                {
                    LogAnimationWarning(L"Unknown animation target path encountered");
                    break;
                }
            }

            // Rebuild the world matrix using the CreateTransformMatrix helper function
            // This ensures all transformation components are properly combined
            XMFLOAT4 quaternionRotation;
            
            // Convert current Euler rotation back to quaternion for matrix creation
            XMVECTOR eulerVec = XMQuaternionRotationRollPitchYaw(
                sceneModels[i].m_modelInfo.rotation.x, 
                sceneModels[i].m_modelInfo.rotation.y, 
                sceneModels[i].m_modelInfo.rotation.z);
            XMStoreFloat4(&quaternionRotation, eulerVec);
            
            // Create the complete transformation matrix using all current transformation components
            sceneModels[i].m_modelInfo.worldMatrix = CreateTransformMatrix(
                sceneModels[i].m_modelInfo.position,    // Translation component
                quaternionRotation,                      // Rotation component (as quaternion)
                sceneModels[i].m_modelInfo.scale        // Scale component
            );

            #if defined(_DEBUG_GLTFANIMATOR_)
                // Log the final transformation matrix for debugging purposes
                XMFLOAT4X4 matrixDebug;
                XMStoreFloat4x4(&matrixDebug, sceneModels[i].m_modelInfo.worldMatrix);
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[GLTFAnimator] Updated world matrix for model %d: Translation(%.2f,%.2f,%.2f)", 
                                    i, matrixDebug._41, matrixDebug._42, matrixDebug._43);
            #endif
        }
    }
    catch (const std::exception& ex)
    {
        exceptionHandler.LogException(ex, "GLTFAnimator::ApplyAnimationToNode");
    }
}

//==============================================================================
// SlerpQuaternions - Spherical linear interpolation between two quaternions
//==============================================================================
XMFLOAT4 GLTFAnimator::SlerpQuaternions(const XMFLOAT4& q1, const XMFLOAT4& q2, float t)
{
   try
   {
       XMVECTOR quat1 = XMLoadFloat4(&q1);
       XMVECTOR quat2 = XMLoadFloat4(&q2);
       
       // Perform spherical linear interpolation
       XMVECTOR result = XMQuaternionSlerp(quat1, quat2, t);
       
       XMFLOAT4 output;
       XMStoreFloat4(&output, result);
       return output;
   }
   catch (const std::exception& ex)
   {
       exceptionHandler.LogException(ex, "GLTFAnimator::SlerpQuaternions");
       return q1; // Return first quaternion on error
   }
}

//==============================================================================
// Query and Control Functions
//==============================================================================

int GLTFAnimator::GetAnimationCount() const
{
   return static_cast<int>(m_animations.size());
}

const GLTFAnimation* GLTFAnimator::GetAnimation(int index) const
{
   if (index >= 0 && index < static_cast<int>(m_animations.size()))
   {
       return &m_animations[index];
   }
   return nullptr;
}

AnimationInstance* GLTFAnimator::GetAnimationInstance(int parentModelID)
{
   for (auto& instance : m_animationInstances)
   {
       if (instance.parentModelID == parentModelID)
       {
           return &instance;
       }
   }
   return nullptr;
}

bool GLTFAnimator::SetAnimationSpeed(int parentModelID, float speed)
{
   AnimationInstance* instance = GetAnimationInstance(parentModelID);
   if (instance)
   {
       instance->playbackSpeed = speed;
       return true;
   }
   return false;
}

bool GLTFAnimator::SetAnimationLooping(int parentModelID, bool looping)
{
   AnimationInstance* instance = GetAnimationInstance(parentModelID);
   if (instance)
   {
       instance->isLooping = looping;
       return true;
   }
   return false;
}

bool GLTFAnimator::SetAnimationTime(int parentModelID, float time)
{
   AnimationInstance* instance = GetAnimationInstance(parentModelID);
   if (instance && instance->animationIndex >= 0 && instance->animationIndex < static_cast<int>(m_animations.size()))
   {
       const GLTFAnimation& animation = m_animations[instance->animationIndex];
       instance->currentTime = std::clamp(time, 0.0f, animation.duration);
       return true;
   }
   return false;
}

float GLTFAnimator::GetAnimationTime(int parentModelID) const
{
   for (const auto& instance : m_animationInstances)
   {
       if (instance.parentModelID == parentModelID)
       {
           return instance.currentTime;
       }
   }
   return 0.0f;
}

float GLTFAnimator::GetAnimationDuration(int animationIndex) const
{
   if (animationIndex >= 0 && animationIndex < static_cast<int>(m_animations.size()))
   {
       return m_animations[animationIndex].duration;
   }
   return 0.0f;
}

bool GLTFAnimator::IsAnimationPlaying(int parentModelID) const
{
   for (const auto& instance : m_animationInstances)
   {
       if (instance.parentModelID == parentModelID)
       {
           return instance.isPlaying;
       }
   }
   return false;
}

//==============================================================================
// Cleanup and Utility Functions
//==============================================================================

void GLTFAnimator::ClearAllAnimations()
{
   m_animations.clear();
   m_animationInstances.clear();
   m_isInitialized = false;

   #if defined(_DEBUG_GLTFANIMATOR_)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] All animations cleared.");
   #endif
}

void GLTFAnimator::RemoveAnimationInstance(int parentModelID)
{
   auto it = std::remove_if(m_animationInstances.begin(), m_animationInstances.end(),
       [parentModelID](const AnimationInstance& instance) {
           return instance.parentModelID == parentModelID;
       });
   
   if (it != m_animationInstances.end())
   {
       m_animationInstances.erase(it, m_animationInstances.end());
       
       #if defined(_DEBUG_GLTFANIMATOR_)
           debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Removed animation instance for parent ID %d", parentModelID);
       #endif
   }
}

void GLTFAnimator::DebugPrintAnimationInfo() const
{
   #if defined(_DEBUG_GLTFANIMATOR_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Total animations loaded: %d", static_cast<int>(m_animations.size()));
       
       for (size_t i = 0; i < m_animations.size(); ++i)
       {
           const GLTFAnimation& anim = m_animations[i];
           debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Animation %d: %ls (Duration: %.2f, Samplers: %d, Channels: %d)", 
                               static_cast<int>(i), anim.name.c_str(), anim.duration, 
                               static_cast<int>(anim.samplers.size()), static_cast<int>(anim.channels.size()));
       }

       debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Active animation instances: %d", static_cast<int>(m_animationInstances.size()));
       
       for (const auto& instance : m_animationInstances)
       {
           debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] Instance: Parent ID %d, Animation %d, Time %.2f, Playing: %s", 
                               instance.parentModelID, instance.animationIndex, instance.currentTime, 
                               instance.isPlaying ? L"Yes" : L"No");
       }
   #endif
}

//==============================================================================
// Validation and Error Handling Functions
//==============================================================================

bool GLTFAnimator::ValidateAnimationData(const GLTFAnimation& animation) const
{
   // Check if animation has valid duration
   if (animation.duration <= 0.0f)
   {
       LogAnimationWarning(L"Animation has zero or negative duration: " + animation.name);
       return false;
   }

   // Check if animation has samplers and channels
   if (animation.samplers.empty())
   {
       LogAnimationWarning(L"Animation has no samplers: " + animation.name);
       return false;
   }

   if (animation.channels.empty())
   {
       LogAnimationWarning(L"Animation has no channels: " + animation.name);
       return false;
   }

   // Validate each channel references a valid sampler
   for (const auto& channel : animation.channels)
   {
       if (channel.samplerIndex < 0 || channel.samplerIndex >= static_cast<int>(animation.samplers.size()))
       {
           LogAnimationWarning(L"Channel references invalid sampler index in animation: " + animation.name);
           return false;
       }
   }

   return true;
}

bool GLTFAnimator::ValidateAccessorIndex(int accessorIndex, const json& doc) const
{
   if (!doc.contains("accessors") || !doc["accessors"].is_array())
   {
       LogAnimationError(L"GLTF document missing accessors array");
       return false;
   }

   const auto& accessors = doc["accessors"];
   if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size()))
   {
       LogAnimationError(L"Invalid accessor index: " + std::to_wstring(accessorIndex));
       return false;
   }

   return true;
}

void GLTFAnimator::LogAnimationError(const std::wstring& errorMessage) const
{
   #if defined(_DEBUG_GLTFANIMATOR_)
       debug.logDebugMessage(LogLevel::LOG_ERROR, L"[GLTFAnimator] ERROR: %ls", errorMessage.c_str());
   #endif
}

void GLTFAnimator::LogAnimationWarning(const std::wstring& warningMessage) const
{
   #if defined(_DEBUG_GLTFANIMATOR_)
       debug.logDebugMessage(LogLevel::LOG_WARNING, L"[GLTFAnimator] WARNING: %ls", warningMessage.c_str());
   #endif
}

void GLTFAnimator::LogAnimationInfo(const std::wstring& infoMessage) const
{
   #if defined(_DEBUG_GLTFANIMATOR_)
       debug.logDebugMessage(LogLevel::LOG_INFO, L"[GLTFAnimator] %ls", infoMessage.c_str());
   #endif
}

//==============================================================================
// CreateTransformMatrix - Creates a transformation matrix from TRS components
// Combines translation, rotation (quaternion), and scale into a single world matrix
// Order: Scale * Rotation * Translation (SRT order for proper transformation)
//==============================================================================
XMMATRIX GLTFAnimator::CreateTransformMatrix(const XMFLOAT3& translation, const XMFLOAT4& rotation, const XMFLOAT3& scale)
{
    try
    {
        // Create scale matrix from scale components
        XMMATRIX scaleMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);
        
        // Create rotation matrix from quaternion
        XMVECTOR rotationQuat = XMLoadFloat4(&rotation);
        
        // Normalize the quaternion to ensure it's a valid rotation
        rotationQuat = XMQuaternionNormalize(rotationQuat);
        
        // Convert quaternion to rotation matrix
        XMMATRIX rotationMatrix = XMMatrixRotationQuaternion(rotationQuat);
        
        // Create translation matrix from translation components
        XMMATRIX translationMatrix = XMMatrixTranslation(translation.x, translation.y, translation.z);
        
        // Combine transformations in the correct order: Scale * Rotation * Translation
        // This order ensures that scaling happens first in local space, then rotation, then translation
        XMMATRIX worldMatrix = scaleMatrix * rotationMatrix * translationMatrix;
        
        #if defined(_DEBUG_GLTFANIMATOR_)
            // Log the transformation components for debugging
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[GLTFAnimator] CreateTransformMatrix - T:(%.2f,%.2f,%.2f) R:(%.2f,%.2f,%.2f,%.2f) S:(%.2f,%.2f,%.2f)",
                translation.x, translation.y, translation.z,
                rotation.x, rotation.y, rotation.z, rotation.w,
                scale.x, scale.y, scale.z);
        #endif
        
        return worldMatrix;
    }
    catch (const std::exception& ex)
    {
        // Handle any exceptions that might occur during matrix creation
        exceptionHandler.LogException(ex, "GLTFAnimator::CreateTransformMatrix");
        
        // Return identity matrix as fallback in case of error
        return XMMatrixIdentity();
    }
}

