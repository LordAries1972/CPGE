// ---------------------------------------------------------------------------------------------------------------
// OpenGLRenderFrame.cpp  —  OpenGL Render Loop & Scene Rendering
// ---------------------------------------------------------------------------------------------------------------
// Implements OpenGLRenderer::RenderFrame() and the scene-specific rendering helpers
// RenderGamePlay() and RenderIntroMovie().
//
// Pipeline order (mirrors DXRenderFrame.cpp / VULKAN_RenderFrame.cpp):
//   1) Safety guards, acquire exclusive lock
//   2) Clear colour + depth buffers, calculate delta time
//   3) Update camera animation
//   4) Scene-specific 3D rendering (RenderGamePlay / RenderIntroMovie)
//   5) 2D overlay composite (FX scrollers, particles, text scrollers)
//   6) FX rendering (fades, starfield, tunnel)
//   7) GUI rendering
//   8) SwapBuffers (present)
// ---------------------------------------------------------------------------------------------------------------
/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ---------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_OPENGL__)

#include "OpenGLRenderer.h"
#include "BuildInfo.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "OpenGLFXManager.h"
#include "GUIManager.h"
#include "ConsoleWindow.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"
#include "ThreadManager.h"

extern HWND                  hwnd;
extern HINSTANCE             hInst;
extern GUIManager            guiManager;
extern ConsoleWindow         consoleWindow;
extern Debug                 debug;
extern ExceptionHandler      exceptionHandler;
extern SystemUtils           sysUtils;
extern SceneManager          scene;
extern ThreadManager         threadManager;
extern GLFXManager           fxManager;
extern Vector2               myMouseCoords;
extern Model                 models[MAX_MODELS];
extern LightsManager         lightsManager;
extern MoviePlayer           moviePlayer;
extern WindowMetrics         winMetrics;
extern ScreenRecorder        screenRecorder;
extern Configuration         config;
extern std::atomic<bool>     bResizeInProgress;
extern std::atomic<bool>     bFullScreenTransition;
extern bool                  bResizing;

#ifdef __USE_SCRIPT_MANAGER__
#include "ScriptManager.h"
extern ScriptManager scriptManager;
#endif

#pragma warning(push)
#pragma warning(disable: 4101)

// ---------------------------------------------------------------------------------------------------------------
// 3D scene rendering helper
// ---------------------------------------------------------------------------------------------------------------
// Upload per-draw shader data for a single model primitive.
//
// Two shader paths share this function:
//   UBO path (ModelVertex/ModelPixel.glsl) — matrices/lights/material come from UBOs
//     already uploaded by RenderGamePlay(); only texture bindings are needed here.
//     All individual-uniform locations (u.uModel, u.uView, …) are -1 for this path,
//     so every glUniform* call below is guarded by ">= 0" and becomes a no-op.
//
//   Embedded fallback (k_3dVertGLSL / k_3dFragGLSL) — uses individual uniforms, no UBOs.
//     LoadShaders() populates the m_uniforms3D locations when the embedded shaders are
//     active, so the guarded uploads below fire and provide matrices/lights/material.
static void UploadModelUniformsCached(
    const OpenGLRenderer::CachedUniforms3D& u,
    const glm::mat4& view, const glm::mat4& proj,
    const glm::vec3& camPos, const Matrix4x4& worldMat4,
    const std::vector<LightStruct>& allLights, int lightCount,
    const ModelInfo& mi)
{
    // ── Individual matrix/camera uniforms (embedded fallback only) ────────────────
    // For the embedded k_3dVertGLSL shader: worldMat4 is DX row-major; uploading
    // with GL_FALSE causes OpenGL to interpret it column-major, which effectively
    // transposes it — matching the OpenGL column-major convention for the same transform.
    // view and proj are GLM column-major and are uploaded directly (no transpose).
    if (u.uModel      >= 0) glUniformMatrix4fv(u.uModel,      1, GL_FALSE, &worldMat4.m[0][0]);
    if (u.uView       >= 0) glUniformMatrix4fv(u.uView,       1, GL_FALSE, glm::value_ptr(view));
    if (u.uProjection >= 0) glUniformMatrix4fv(u.uProjection, 1, GL_FALSE, glm::value_ptr(proj));
    if (u.uViewPos    >= 0) glUniform3f(u.uViewPos, camPos.x, camPos.y, camPos.z);
    if (u.uAlpha      >= 0) glUniform1f(u.uAlpha, 1.0f);

    // ── Lights (embedded fallback only) ─────────────────────────────────────────
    if (u.uLightCount >= 0) glUniform1i(u.uLightCount, lightCount);
    for (int i = 0; i < lightCount && i < 8; ++i) {
        const auto& ll = u.lights[i];
        const auto& ls = allLights[i];
        if (ll.position  >= 0) glUniform3f(ll.position,  ls.position.x,  ls.position.y,  ls.position.z);
        if (ll.direction >= 0) glUniform3f(ll.direction, ls.direction.x, ls.direction.y, ls.direction.z);
        if (ll.color     >= 0) glUniform3f(ll.color,     ls.color.x,     ls.color.y,     ls.color.z);
        if (ll.ambient   >= 0) glUniform3f(ll.ambient,   ls.ambient.x,   ls.ambient.y,   ls.ambient.z);
        if (ll.intensity >= 0) glUniform1f(ll.intensity, ls.intensity);
        if (ll.range     >= 0) glUniform1f(ll.range,     ls.range);
        if (ll.innerCone >= 0) glUniform1f(ll.innerCone, ls.innerCone);
        if (ll.outerCone >= 0) glUniform1f(ll.outerCone, ls.outerCone);
        if (ll.type      >= 0) glUniform1i(ll.type,      ls.type);
        if (ll.active    >= 0) glUniform1i(ll.active,    ls.active);
    }
    // Legacy single-light fallback uniforms in k_3dFragGLSL
    if (u.uLightDir   >= 0) {
        glm::vec3 dir = (lightCount > 0) ? glm::vec3(allLights[0].direction.x, allLights[0].direction.y, allLights[0].direction.z) : glm::vec3(0,-1,0);
        glUniform3f(u.uLightDir,   dir.x, dir.y, dir.z);
    }
    if (u.uLightColor >= 0) {
        glm::vec3 col = (lightCount > 0) ? glm::vec3(allLights[0].color.x, allLights[0].color.y, allLights[0].color.z) : glm::vec3(1,1,1);
        glUniform3f(u.uLightColor, col.x, col.y, col.z);
    }
    if (u.uAmbient    >= 0) {
        glm::vec3 amb = (lightCount > 0) ? glm::vec3(allLights[0].ambient.x, allLights[0].ambient.y, allLights[0].ambient.z) : glm::vec3(0.2f,0.2f,0.2f);
        glUniform3f(u.uAmbient, amb.x, amb.y, amb.z);
    }

    // ── Material uniforms (embedded fallback only) ────────────────────────────
    bool hasDiffuse  = !mi.textureIDs.empty()    && mi.textureIDs[0]    != 0;
    bool hasNormal   = !mi.normalMapIDs.empty()  && mi.normalMapIDs[0]  != 0;
    bool hasMetallic = mi.useMetallicMap         && mi.metallicTexID    != 0;
    bool hasRoughness= mi.useRoughnessMap        && mi.roughnessTexID   != 0;
    bool hasAO       = mi.useAOMap               && mi.aoTexID          != 0;
    bool hasEnv      = mi.useEnvironmentMap      && mi.envTexID         != 0;
    bool hasGloss    = mi.useGlossMap            && mi.glossTexID       != 0;
    bool hasEmissive = mi.useEmissiveMap         && mi.emissiveTexID    != 0;

    if (u.uHasDiffuse    >= 0) glUniform1i(u.uHasDiffuse,    hasDiffuse  ? 1 : 0);
    if (u.uHasNormalMap  >= 0) glUniform1i(u.uHasNormalMap,  hasNormal   ? 1 : 0);
    if (u.uHasGlossMap   >= 0) glUniform1i(u.uHasGlossMap,   hasGloss    ? 1 : 0);
    if (u.uHasEmissiveMap>= 0) glUniform1i(u.uHasEmissiveMap,hasEmissive ? 1 : 0);
    if (u.uNormalScale   >= 0) glUniform1f(u.uNormalScale,   hasNormal   ? 1.0f : 0.0f);
    if (u.uMetallic      >= 0) glUniform1f(u.uMetallic,      mi.metallic);
    if (u.uRoughness     >= 0) glUniform1f(u.uRoughness,     mi.roughness);
    if (u.uKd            >= 0) glUniform3f(u.uKd,            1.0f, 1.0f, 1.0f);
    if (u.uKa            >= 0) glUniform3f(u.uKa,            0.35f, 0.35f, 0.35f);
    if (u.uKs            >= 0) glUniform3f(u.uKs,            0.5f, 0.5f, 0.5f);
    if (u.uNs            >= 0) glUniform1f(u.uNs,            32.0f);
    if (u.uEmissiveFactor>= 0) glUniform3f(u.uEmissiveFactor,0.0f, 0.0f, 0.0f);
    if (u.uEmissiveStr   >= 0) glUniform1f(u.uEmissiveStr,   1.0f);

    // ── Texture bindings (both paths) ────────────────────────────────────────
    // Bind each map to its fixed texture unit.  Sampler uniforms were set once in
    // LoadShaders() so only the GL_TEXTURE* state needs to change per draw call.
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,      hasDiffuse  ? mi.textureIDs[0]  : 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,      hasNormal   ? mi.normalMapIDs[0]: 0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D,      hasMetallic ? mi.metallicTexID  : 0);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,      hasRoughness? mi.roughnessTexID : 0);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D,      hasAO       ? mi.aoTexID        : 0);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_CUBE_MAP,hasEnv      ? mi.envTexID       : 0);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D,      hasGloss    ? mi.glossTexID    : 0);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,      hasEmissive ? mi.emissiveTexID : 0);
    // t8: shadow depth map for PCF — bind when present, zero when absent.
    glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D,      mi.shadowTexID != 0 ? mi.shadowTexID : 0);
    glActiveTexture(GL_TEXTURE0); // Leave unit 0 active (conventional default)
}

// Fallback path for custom per-model shaders: sets sampler uniforms by name then binds textures.
// UBOs (ConstantBuffer binding 0, MaterialBuffer binding 4, GlobalLightBuffer binding 3) are
// already uploaded in RenderGamePlay() and remain bound globally, so custom shaders that use
// the same layout(std140, binding=N) declarations will receive the correct data automatically.
static void UploadModelUniformsDynamic(GLuint prog,
    const glm::mat4& /*view*/, const glm::mat4& /*proj*/,
    const glm::vec3& /*camPos*/, const Matrix4x4& /*worldMat4*/,
    const std::vector<LightStruct>& /*allLights*/, int /*lightCount*/,
    const ModelInfo& mi)
{
    auto loc = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    // Set sampler uniforms to their fixed texture units.
    GLint lD = loc("diffuseTexture"), lN = loc("normalMap"),   lMe = loc("metallicMap");
    GLint lR = loc("roughnessMap"),   lA = loc("aoMap"),        lE  = loc("environmentMap");
    GLint lG = loc("glossMap"),       lEM = loc("emissiveMap"), lS  = loc("shadowMap");
    if (lD  >= 0) glUniform1i(lD,  TEXTURE_UNIT_DIFFUSE);
    if (lN  >= 0) glUniform1i(lN,  TEXTURE_UNIT_NORMAL);
    if (lMe >= 0) glUniform1i(lMe, TEXTURE_UNIT_METALLIC);
    if (lR  >= 0) glUniform1i(lR,  TEXTURE_UNIT_ROUGHNESS);
    if (lA  >= 0) glUniform1i(lA,  TEXTURE_UNIT_AO);
    if (lE  >= 0) glUniform1i(lE,  TEXTURE_UNIT_ENVIRONMENT);
    if (lG  >= 0) glUniform1i(lG,  TEXTURE_UNIT_GLOSS);
    if (lEM >= 0) glUniform1i(lEM, TEXTURE_UNIT_EMISSIVE);
    if (lS  >= 0) glUniform1i(lS,  TEXTURE_UNIT_SHADOW);

    // Bind model textures to the matching units.
    bool hasDiffuse  = !mi.textureIDs.empty()    && mi.textureIDs[0]    != 0;
    bool hasNormal   = !mi.normalMapIDs.empty()  && mi.normalMapIDs[0]  != 0;
    bool hasMetallic = mi.useMetallicMap         && mi.metallicTexID    != 0;
    bool hasRoughness= mi.useRoughnessMap        && mi.roughnessTexID   != 0;
    bool hasAO       = mi.useAOMap               && mi.aoTexID          != 0;
    bool hasEnv      = mi.useEnvironmentMap      && mi.envTexID         != 0;
    bool hasGloss    = mi.useGlossMap            && mi.glossTexID       != 0;
    bool hasEmissive = mi.useEmissiveMap         && mi.emissiveTexID    != 0;
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,      hasDiffuse  ? mi.textureIDs[0]  : 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,      hasNormal   ? mi.normalMapIDs[0]: 0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D,      hasMetallic ? mi.metallicTexID  : 0);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,      hasRoughness? mi.roughnessTexID : 0);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D,      hasAO       ? mi.aoTexID        : 0);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_CUBE_MAP,hasEnv      ? mi.envTexID       : 0);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D,      hasGloss      ? mi.glossTexID    : 0);
    glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D,      hasEmissive   ? mi.emissiveTexID : 0);
    // t8: shadow depth map for PCF — bind when present, zero when absent.
    glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D,      mi.shadowTexID != 0 ? mi.shadowTexID : 0);
    glActiveTexture(GL_TEXTURE0);
}

inline void OpenGLRenderer::RenderGamePlay(float deltaTime)
{
    if (!bIsInitialized.load()) return;
    if (!threadManager.threadVars.bLoaderTaskFinished.load()) return;

    // Restore 3D pipeline states
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    // LH system: use clockwise front faces to match DirectX convention
    if (config.myConfig.BackCulling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);
    } else {
        glDisable(GL_CULL_FACE);
    }

    glm::vec3 camPos = myCamera.GetPosition();
    glm::mat4 view   = myCamera.GetViewMatrix();
    glm::mat4 proj   = myCamera.GetProjectionMatrix();

    auto allLights = lightsManager.GetAllLights();
    int  lightCount = static_cast<int>(allLights.size());
    if (lightCount > MAX_GLOBAL_LIGHTS) lightCount = MAX_GLOBAL_LIGHTS;

    // Upload GlobalLightBuffer UBO BEFORE model draw calls — mirrors DXRenderFrame.cpp approach
    // so that models using ModelPixel.glsl (PBR, UBO-based, binding 3) see correct scene lights
    // on every frame.  The built-in Blinn-Phong shader receives lights via individual uniforms
    // (UploadModelUniformsCached) but this upload ensures parity when PBR is active.
    {
        GlobalLightBuffer glbuf{};
        glbuf.numLights = lightCount;
        for (int i = 0; i < lightCount; ++i)
            memcpy(&glbuf.lights[i], &allLights[i], sizeof(LightStruct));

        GLuint uboID = m_uniformBuffers[UNIFORM_GLOBAL_LIGHT_BUFFER].bufferID;
        if (uboID != 0)
        {
            glBindBuffer(GL_UNIFORM_BUFFER, uboID);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GlobalLightBuffer), &glbuf);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
            // Re-bind to the correct GLSL binding point so all subsequent draw calls
            // can read it from layout(binding=3) in ModelPixel.glsl.
            glBindBufferBase(GL_UNIFORM_BUFFER, GLSL_BINDING_GLOBAL_LIGHT, uboID);
        }
    }

    // Build view/proj as Matrix4x4 for model info.
    // CONVENTION (must match mi.worldMatrix): all ModelInfo matrices are stored in
    // DX row-vector convention (row-major memory), exactly like the XMMATRIX stubs
    // used by SceneManager.  When the raw bytes of such a matrix are uploaded into a
    // std140 mat4, OpenGL reads them column-major — an implicit transpose — which
    // yields the column-vector matrix ModelVertex.glsl expects (uProj*uView*uWorld*p).
    // GLM stores column-vector matrices in column-major memory, so the DX-convention
    // row-major equivalent has the SAME byte layout: copy element-for-element with
    // matching indices (m[r][c] = view[r][c]).  The previous index-swapped copy
    // (m[r][c] = view[c][r]) produced V^T/P^T in the shader while uWorld arrived
    // correctly, garbling gl_Position and making every model invisible.
    Matrix4x4 viewMat4, projMat4;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            viewMat4.m[r][c] = view[r][c];
            projMat4.m[r][c] = proj[r][c];
        }
    XMFLOAT3 camPosF3 = { camPos.x, camPos.y, camPos.z };

#if defined(_DEBUG_RENDER_WIREFRAME_)
    glPolygonMode(GL_FRONT_AND_BACK, bWireframeMode ? GL_LINE : GL_FILL);
#endif

    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (!scene.scene_models[i].m_isLoaded) continue;
        if (scene.scene_models[i].m_modelInfo.bIsTransformProxy) continue;
        if (scene.scene_models[i].m_modelInfo.vertices.empty()) continue;

        ModelInfo& mi = scene.scene_models[i].m_modelInfo;
        mi.fxActive        = false;
        mi.viewMatrix      = viewMat4;
        mi.projectionMatrix = projMat4;
        mi.cameraPosition  = camPosF3;

        // Choose shader: prefer per-model, fall back to renderer's built-in 3D shader
        const bool useBuiltin = (mi.shaderProgram == 0 || mi.shaderProgram == m_3dShaderProgram.programID);
        GLuint prog = useBuiltin ? m_3dShaderProgram.programID : mi.shaderProgram;
        if (prog == 0) {
            scene.scene_models[i].Render(deltaTime);
            continue;
        }

        glUseProgram(prog);

        // ── ConstantBuffer UBO (GLSL binding 0) ──────────────────────────────
        // Upload world/view/proj/camPos/scale per-model so the vertex shader sees
        // the correct transforms.  XMMATRIX = Matrix4x4 in OpenGL builds.
        {
            ConstantBuffer cb{};
            cb.worldMatrix      = mi.worldMatrix;
            cb.viewMatrix       = mi.viewMatrix;
            cb.projectionMatrix = mi.projectionMatrix;
            cb.cameraPosition   = { camPos.x, camPos.y, camPos.z };
            cb.modelScale       = mi.scale;
            GLuint cbUBO = m_uniformBuffers[UNIFORM_VIEW_MATRIX].bufferID;
            if (cbUBO != 0) {
                glBindBuffer(GL_UNIFORM_BUFFER, cbUBO);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ConstantBuffer), &cb);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
            }
        }

        // ── MaterialBuffer UBO (GLSL binding 4) ──────────────────────────────
        // Upload PBR material properties and texture-presence flags so the
        // fragment shader samples the correct maps and applies the right colours.
        {
            bool hasDiffuse  = !mi.textureIDs.empty()    && mi.textureIDs[0]    != 0;
            bool hasNormal   = !mi.normalMapIDs.empty()  && mi.normalMapIDs[0]  != 0;
            bool hasMetallic = mi.useMetallicMap         && mi.metallicTexID    != 0;
            bool hasRoughness= mi.useRoughnessMap        && mi.roughnessTexID   != 0;
            bool hasAO       = mi.useAOMap               && mi.aoTexID          != 0;
            bool hasEnv      = mi.useEnvironmentMap      && mi.envTexID         != 0;
            bool hasGloss    = mi.useGlossMap            && mi.glossTexID       != 0;
            bool hasEmissive = mi.useEmissiveMap         && mi.emissiveTexID    != 0;

            // Resolve the first parsed material as active — mirrors DX11 Model::Render()
            // which reads m_materials.begin()->second into the b4 material buffer.
            // Without this the GL path rendered every model with hardcoded white Kd,
            // losing all solid-colour materials and PBR scalars from the importers.
            const Material* srcMat = nullptr;
            if (!scene.scene_models[i].m_materials.empty())
                srcMat = &(scene.scene_models[i].m_materials.begin()->second);

            GLMaterialUBO mat{};
            if (srcMat)
            {
                mat.Ka[0] = srcMat->Ka.x; mat.Ka[1] = srcMat->Ka.y; mat.Ka[2] = srcMat->Ka.z;
                mat.Kd[0] = srcMat->Kd.x; mat.Kd[1] = srcMat->Kd.y; mat.Kd[2] = srcMat->Kd.z;
                mat.Ks[0] = srcMat->Ks.x; mat.Ks[1] = srcMat->Ks.y; mat.Ks[2] = srcMat->Ks.z;
                mat.Ns               = srcMat->Ns;
                mat.Metallic         = srcMat->Metallic;
                mat.Roughness        = srcMat->Roughness;
                mat.ReflectionStrength = srcMat->Reflection;
                mat.EmissiveFactor[0]= srcMat->emissiveFactor.x;
                mat.EmissiveFactor[1]= srcMat->emissiveFactor.y;
                mat.EmissiveFactor[2]= srcMat->emissiveFactor.z;
                mat.EmissiveStrength = srcMat->emissiveStrength;
            }
            else
            {
                // No parsed material — same defaults as the DX11 fallback branch.
                mat.Ka[0] = 0.1f;  mat.Ka[1] = 0.1f;  mat.Ka[2] = 0.1f;
                mat.Kd[0] = 0.8f;  mat.Kd[1] = 0.8f;  mat.Kd[2] = 0.8f;
                mat.Ks[0] = 1.0f;  mat.Ks[1] = 1.0f;  mat.Ks[2] = 1.0f;
                mat.Ns               = 16.0f;
                mat.Metallic         = mi.metallic;
                mat.Roughness        = mi.roughness;
                mat.ReflectionStrength = mi.reflectionStrength;
                mat.EmissiveFactor[0]= 0.0f; mat.EmissiveFactor[1] = 0.0f; mat.EmissiveFactor[2] = 0.0f;
                mat.EmissiveStrength = 1.0f;
            }

            // Derive ambient from base colour so each material keeps its own hue.
            // GLTF has no Ka concept — a fraction of Kd is PBR-correct and prevents
            // every material from reading as flat grey (mirrors DX11 Model::Render).
            if (mat.Ka[0] <= 0.0001f && mat.Ka[1] <= 0.0001f && mat.Ka[2] <= 0.0001f)
            {
                mat.Ka[0] = mat.Kd[0] * 0.15f;
                mat.Ka[1] = mat.Kd[1] * 0.15f;
                mat.Ka[2] = mat.Kd[2] * 0.15f;
            }

            mat.useMetallicMap   = hasMetallic  ? 1.0f : 0.0f;
            mat.useRoughnessMap  = hasRoughness ? 1.0f : 0.0f;
            mat.useAOMap         = hasAO        ? 1.0f : 0.0f;
            mat.useEnvMap        = hasEnv       ? 1.0f : 0.0f;
            // NormalScale <= 0 tells the shader to skip normal-map sampling entirely.
            mat.NormalScale      = hasNormal    ? 1.0f : -1.0f;
            // useDiffuseMap: honour the importer's decision (solid-colour materials use
            // Kd directly) but never enable sampling when no texture is bound — the
            // same belt-and-suspenders guard as DX12 RenderDX12 (resource must exist).
            mat.useDiffuseMap    = (mi.useDiffuseMap && hasDiffuse) ? 1.0f : 0.0f;
            mat.useGlossMap      = hasGloss     ? 1.0f : 0.0f;
            mat.useEmissiveMap   = hasEmissive  ? 1.0f : 0.0f;

            GLuint matUBO = m_uniformBuffers[UNIFORM_MATERIAL_BUFFER].bufferID;
            if (matUBO != 0 && m_uniformBuffers[UNIFORM_MATERIAL_BUFFER].isAllocated) {
                glBindBuffer(GL_UNIFORM_BUFFER, matUBO);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GLMaterialUBO), &mat);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
            }
        }

        // ── EnvBuffer UBO (GLSL binding 5) ───────────────────────────────────
        // Upload per-model environment properties (intensity, tint, Fresnel F0).
        // Mirrors DX11 Model::UpdateEnvironmentBuffer() — uses ModelInfo fields
        // set by SetEnvironmentProperties() or defaulted to safe dielectric values.
        {
            GLEnvUBO env = {};
            env.envIntensity   = mi.envIntensity;
            env.envTint[0]     = mi.envTint.x;
            env.envTint[1]     = mi.envTint.y;
            env.envTint[2]     = mi.envTint.z;
            env.mipLODBias     = mi.mipLODBias;
            env.fresnel0       = mi.fresnel0;
            GLuint envUBO = m_uniformBuffers[UNIFORM_ENVIRONMENT_BUFFER].bufferID;
            if (envUBO != 0 && m_uniformBuffers[UNIFORM_ENVIRONMENT_BUFFER].isAllocated) {
                glBindBuffer(GL_UNIFORM_BUFFER, envUBO);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GLEnvUBO), &env);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
            }
        }

        // ── ShadowBuffer UBO (GLSL binding 6) ────────────────────────────────
        // Upload per-model shadow parameters.  Mirrors DX11 Model::Render() which
        // writes useShadowMap=(shadowSRV!=null), bias=0.001, strength=0.8, size=2048.
        // useShadowMap=0 leaves shadow rendering disabled until a shadow pass provides t8.
        {
            GLShadowUBO shadow = {};
            shadow.shadowBias     = 0.001f;                             // depth bias — prevents shadow acne
            shadow.shadowStrength = 0.8f;                               // shadow darkness multiplier
            shadow.useShadowMap   = (mi.shadowTexID != 0) ? 1.0f : 0.0f;  // enable only when t8 is present
            shadow.shadowMapSize  = 2048.0f;                            // PCF texel-offset denominator
            GLuint shadowUBO = m_uniformBuffers[UNIFORM_SHADOW_BUFFER].bufferID;
            if (shadowUBO != 0 && m_uniformBuffers[UNIFORM_SHADOW_BUFFER].isAllocated) {
                glBindBuffer(GL_UNIFORM_BUFFER, shadowUBO);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GLShadowUBO), &shadow);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
            }
        }

        // ── Texture bindings + (for built-in) legacy sampler path ─────────────
        if (useBuiltin && m_uniforms3D.populated)
            UploadModelUniformsCached(m_uniforms3D, view, proj, camPos, mi.worldMatrix, allLights, lightCount, mi);
        else
            UploadModelUniformsDynamic(prog, view, proj, camPos, mi.worldMatrix, allLights, lightCount, mi);

        // Lazy VAO creation: Upload() leaves VAO=0 so that the VAO is built here
        // on the render thread (render context). VAOs are NOT shared between GL
        // contexts; creating them on the loader context causes an access violation
        // in nvoglv64.dll when the render thread calls glBindVertexArray.
        // VBO/EBO are buffer objects and ARE shared — safe to use here directly.
        if (mi.VAO == 0 && mi.VBO != 0 && !mi.indices.empty()) {
            // Vertex struct layout (OpenGL path, Includes.h non-DX):
            //   float position[3]  → offset  0, 12 bytes
            //   float normal[3]    → offset 12, 12 bytes
            //   float texCoord[2]  → offset 24,  8 bytes
            //   float tangent[4]   → offset 32, 16 bytes  (xyz=tangent, w=handedness for bitangent)
            // Total stride = 48 bytes.  Shader loc 3 expects vec4 (aTangent).
            constexpr GLsizei kStride = (3 + 3 + 2 + 4) * sizeof(float); // 48 bytes
            glGenVertexArrays(1, &mi.VAO);
            glBindVertexArray(mi.VAO);
            glBindBuffer(GL_ARRAY_BUFFER,         mi.VBO);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mi.EBO);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void*)0);                     // position
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, (void*)(3 * sizeof(float)));  // normal
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride, (void*)(6 * sizeof(float)));  // texCoord
            glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, kStride, (void*)(8 * sizeof(float)));  // tangent (vec4)
            glBindVertexArray(0);
        }

        // Bind model's geometry buffers and issue draw call
        if (mi.VAO != 0 && !mi.indices.empty()) {
            glBindVertexArray(mi.VAO);
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(mi.indices.size()),
                           GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }

        glUseProgram(0);
        // Unbind all texture units used by the 3D shader (t0-t8) to prevent stale bindings.
        // t8 is the shadow depth map; unbind both TEXTURE_2D and TEXTURE_CUBE_MAP for safety.
        for (int tu = 8; tu >= 0; --tu) {
            glActiveTexture(GL_TEXTURE0 + tu);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
        glActiveTexture(GL_TEXTURE0);

        scene.scene_models[i].Render(deltaTime);
    }

#if defined(_DEBUG_RENDER_WIREFRAME_)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Intro movie rendering helper  (mirrors DX11Renderer::RenderIntroMovie)
// ---------------------------------------------------------------------------------------------------------------
inline void OpenGLRenderer::RenderIntroMovie()
{
    if (!moviePlayer.IsPlaying()) return;

    // Advance the movie to the current frame (mirrors DX11 UpdateFrame call)
    moviePlayer.UpdateFrame();

#if defined(_WIN32) || defined(_WIN64)
    // Retrieve current RGBA frame from MoviePlayer and upload / update a GL texture
    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (frameData && fw > 0 && fh > 0) {
        static GLuint movieTexID = 0;
        static uint32_t lastW = 0, lastH = 0;

        if (!movieTexID) glGenTextures(1, &movieTexID);
        glBindTexture(GL_TEXTURE_2D, movieTexID);
        // CPU buffer is RGBA (MoviePlayer::UpdateVideoTextureCPU swaps BGRA→RGBA for OpenGL)
        if (fw != lastW || fh != lastH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            lastW = fw; lastH = fh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        // Blit movie to fill entire screen
        DrawVideoFrame(
            Vector2(0.0f, 0.0f),
            Vector2(static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight)),
            MyColor(255, 255, 255, 255),
            movieTexID);
    }

    // Company logo overlay: half size, bottom-left corner (skip if zoom FX is rendering it)
    {
        int logoIdx = int(BlitObj2DIndexType::IMG_COMPANYLOGO);
        if (logoIdx >= 0 && logoIdx < MAX_TEXTURE_BUFFERS &&
            m_2dTextures[logoIdx].isLoaded &&
            !fxManager.IsImageZoomActive(logoIdx)) {
            int halfW = m_2dTextures[logoIdx].width  / 2;
            int halfH = m_2dTextures[logoIdx].height / 2;
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO,
                               0, iOrigHeight - halfH, halfW, halfH);
        }
    }

    // Spacebar to skip movie — mirrors DX11 skip check
    if (GetAsyncKeyState(' ') & 0x8000)
    {
        moviePlayer.Stop();
        scene.bSceneSwitching = true;
        fxManager.FadeToBlack(1.0f, 0.06f);
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// RenderFrame  —  main render loop (mirrors DXRenderFrame.cpp / VULKAN_RenderFrame.cpp)
// ---------------------------------------------------------------------------------------------------------------
void OpenGLRenderer::RenderFrame()
{
    // ---- Safety guards ----
    if (bHasCleanedUp || m_glContext.renderingContext == nullptr)
        return;

#if defined(RENDERER_IS_THREAD) && (defined(_WIN32) || defined(_WIN64))
    // Make the OpenGL context current in THIS render thread.
    // Initialize() released it from the main thread so we own it here.
    if (!wglMakeCurrent(m_glContext.deviceContext, m_glContext.renderingContext)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] wglMakeCurrent failed — render thread cannot acquire GL context");
        return;
    }
#endif

    if (threadManager.threadVars.bIsShuttingDown.load() ||
        bIsMinimized.load()                             ||
        threadManager.threadVars.bIsResizing.load()     ||
        !bIsInitialized.load())
        return;

    ThreadLockHelper exclusiveLock(threadManager, renderFrameLockName, 50);
    if (!exclusiveLock.IsLocked()) return;

    if (threadManager.threadVars.bIsRendering.load()) return;

    try
    {
        exceptionHandler.RecordFunctionCall("OpenGLRenderer::RenderFrame");
        threadManager.threadVars.bIsRendering.store(true);

        #if defined(_DEBUG) && defined(_DEBUG_RENDERER_)
            FLOAT clearR = 0.01f, clearG = 0.01f, clearB = 0.01f;
        #else
            FLOAT clearR = 0.0f, clearG = 0.0f, clearB = 0.0f;
        #endif

        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
            (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            if (status == ThreadStatus::Paused) {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (threadManager.threadVars.bIsResizing.load() || bIsMinimized.load()) {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            threadManager.threadVars.bIsRendering.store(true);

            EvictStaleTextCache();  // advance frame counter; evict unused text textures

            // ---- Delta time ----
            auto  now      = std::chrono::steady_clock::now();
            float rawDelta = std::chrono::duration<float>(now - lastFrameTime).count();

            // Simple 8-frame weighted smoothing
            static float history[8] = {};
            static int   histIdx    = 0;
            history[histIdx] = std::clamp(rawDelta, 1.0f / 120.0f, 1.0f / 10.0f);
            histIdx = (histIdx + 1) % 8;
            float weightedSum = 0.0f, totalWeight = 0.0f;
            for (int i = 0; i < 8; ++i) {
                float w = static_cast<float>(i + 1) / 8.0f;
                weightedSum += history[(histIdx - 1 - i + 8) % 8] * w;
                totalWeight += w;
            }
            float deltaTime = std::clamp(weightedSum / totalWeight, 0.001f, 0.1f);
            lastFrameTime   = now;

            #ifdef __USE_SCRIPT_MANAGER__
                scriptManager.Update(deltaTime);
            #endif

            myCamera.UpdateJumpAnimation();

            // ---- Determine render dimensions ----
            int renderW = iOrigWidth, renderH = iOrigHeight;
            #if defined(_WIN32) || defined(_WIN64)
                if (winMetrics.isFullScreen) {
                    renderW = winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left;
                    renderH = winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top;
                } else {
                    renderW = winMetrics.clientWidth;
                    renderH = winMetrics.clientHeight;
                }
            #endif
            glViewport(0, 0, renderW, renderH);
            m_renderTargetWidth  = renderW;
            m_renderTargetHeight = renderH;
            iOrigWidth           = renderW;
            iOrigHeight          = renderH;

            // ---- Clear buffers ----
            glClearColor(clearR, clearG, clearB, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            // ---- Background images (scene-aware, before 3D) ----
            RenderBackgroundImage();

            // ---- Starfield background pass (before 3D models so geometry renders on top) ----
            fxManager.RenderBackground();

            // ---- Scene-specific 3D rendering ----
            switch (scene.stSceneType)
            {
                #if defined(_DEBUG)
                    case SceneType::SCENE_EXPERIMENT:
                        break;
                #endif

                case SceneType::SCENE_INTRO:
                    break;

                case SceneType::SCENE_GAMETITLE:
                    if (threadManager.threadVars.bLoaderTaskFinished.load()) {
                        int iModelID = scene.FindParentModelID(SplashShipName);
                        if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                            scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                        RenderGamePlay(deltaTime);
                    }
                    break;

                case SceneType::SCENE_GAMEPLAY:
                {
                    int iModelID = scene.FindParentModelID(ShipName1);
                    if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                        scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    RenderGamePlay(deltaTime);
                    break;
                }

                default: break;
            }

            // ---- 2D scene overlays (rendered after 3D so they appear in front) ----
            switch (scene.stSceneType)
            {
                case SceneType::SCENE_INTRO:
                    // Skip normal blit when zoom FX is rendering the splash image
                    if (m_2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)].isLoaded &&
                        !fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_SPLASH1)))
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                    break;

                case SceneType::SCENE_INTRO_MOVIE:
                    RenderIntroMovie();
                    break;

                case SceneType::SCENE_GAMETITLE:
                    if (!threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        fxManager.RenderLoadingText();
                    }
                    break;

                case SceneType::SCENE_GAMEPLAY:
                    if (!threadManager.threadVars.bLoaderTaskFinished.load() ||
                        fxManager.HasActiveLoadingTextEffects())
                        fxManager.RenderLoadingText();
                    break;

                default: break;
            }

            // ---- FX: 2D overlay effects (scrollers, particles, text) ----
            fxManager.Render2D();

            // ---- FPS / debug info display ----
            if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo)
            {
                static auto lastFPSTime  = std::chrono::steady_clock::now();
                static int  frameCounter = 0;
                auto        curTime      = std::chrono::steady_clock::now();
                float       elapsed      = std::chrono::duration<float>(curTime - lastFPSTime).count();
                ++frameCounter;
                if (elapsed >= 1.0f) {
                    fps          = static_cast<float>(frameCounter) / elapsed;
                    frameCounter = 0;
                    lastFPSTime  = curTime;
                }

                #if defined(_WIN32) || defined(_WIN64)
                    glm::vec3 coords = myCamera.GetPosition();
                    const float dbgFontSize = std::clamp(static_cast<float>(renderH) / 108.0f, 8.0f, 12.0f);
                    std::wstring dbgText =
                        L"FPS: " + std::to_wstring(fps) +
                        L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) +
                        L", y" + std::to_wstring(myMouseCoords.y) +
                        L"\nClient Width: " + std::to_wstring(iOrigWidth) + L", Client Height:" + std::to_wstring(iOrigHeight) +
                        L"\nCamera X: " + std::to_wstring(coords.x) +
                        L", Y: "  + std::to_wstring(coords.y) +
                        L", Z: "  + std::to_wstring(coords.z) +
                        L", Yaw: " + std::to_wstring(myCamera.m_yaw) +
                        L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n" +
                        L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n";
                    DrawMyText(dbgText, Vector2(0.0f, 0.0f), MyColor(255, 255, 255, 255), dbgFontSize);
                #endif
            }

            // ---- Debug OSD (F2 toggle notification) ----
            if (bDebugOSDActive)
            {
                float osdElapsed = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - debugOSDStartTime).count();
                if (osdElapsed < 5.0f) {
                    std::wstring osdMsg = config.myConfig.showDebugInfo
                        ? L"=> Debug Info: ENABLED"
                        : L"=> Debug Info: DISABLED";
                    DrawMyText(osdMsg, Vector2(10.0f, 80.0f), MyColor(255, 220, 0, 255), 14.0f);
                } else {
                    bDebugOSDActive = false;
                }
            }

            // ---- Loading spinner (fallback when loading text effects are inactive) ----
            if (!threadManager.threadVars.bLoaderTaskFinished.load())
            {
                delay++;
                if (delay > 3) { loadIndex = (loadIndex + 1) % 9; delay = 0; }
                // Animated circle texture (sprite sheet: 9 valid frames of 32x32,
                // frame 9 skipped — one frame in the sheet is corrupt/invalid).
                if (m_2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)].isLoaded) {
                    iPosX = loadIndex << 5;
                    Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                        iOrigWidth - 34, iOrigHeight - 49, iPosX, 0, 32, 32);
                }
            }

            // ---- Renderer info overlay (bottom-right corner) ----
            // Format mirrors DX11: "CPGE Windows OpenGL v0.0.1370"
            #if defined(_WIN32) || defined(_WIN64)
                if (USE_RENDERER_INFO && !scene.bSceneSwitching)
                {
                    bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE  ||
                                scene.stSceneType == SceneType::SCENE_GAMEPLAY   ||
                                scene.stSceneType == SceneType::SCENE_INTRO      ||
                                scene.stSceneType == SceneType::SCENE_INTRO_MOVIE||
                                scene.stSceneType == SceneType::SCENE_GAMEOVER);
                    #if defined(_DEBUG)
                        riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
                    #endif

                    if (riShow) {
                        // OpenGL version string is slightly smaller than DX11/Vulkan
                        const float riFontSize = std::clamp(
                            static_cast<float>(iOrigHeight) / 86.0f, 8.0f, 12.0f);

                        // Full version string — identical format to DXRenderFrame.cpp
                        const std::wstring riText =
                            std::wstring(GAME_NAME_W L" " PLATFORM_NAME_W L" " RENDERER_NAME_W L" v") +
                            std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                            std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                            std::to_wstring(CURRENT_BUILD);

                        int tw = 0, th = 0;
                        GLuint riTex = RenderTextToTexture(riText, L"Arial", riFontSize,
                            MyColor(220, 220, 220, 255), tw, th);
                        if (riTex) {
                            // Align to bottom-right corner, 4 px inset from each edge
                            Render2DQuad(riTex,
                                iOrigWidth  - tw - 4,
                                iOrigHeight - th - 2,
                                tw, th, 0, 0, tw, th, MyColor(255, 255, 255, 255), false);
                            glDeleteTextures(1, &riTex);
                        }
                    }
                }
            #endif

            // ---- GUI rendering ----
            // Rendered after 3D so windows always appear in front of scene geometry.
            guiManager.Render();

            // Console window rendering is now handled by GUIManager::Render()
            // via the GUIWindow::onCustomRender callback set in ConsoleWindow::CreateInGUIManager().

            // ---- Custom cursor ----
            if (m_2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)].isLoaded)
                Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR,
                    static_cast<int>(myMouseCoords.x),
                    static_cast<int>(myMouseCoords.y));

            // ---- REC indicator (blinking) ----
            if (screenRecorder.IsRecording())
            {
                static int recBlinkCounter = 0;
                recBlinkCounter = (recBlinkCounter + 1) % 60;
                if (recBlinkCounter < 30) {
                    DrawMyText(L"* REC",
                        Vector2(static_cast<float>(renderW) - 80.0f, 9.0f),
                        MyColor::Red(), 18.0f);
                }
            }

            // ---- FX: fullscreen effects (fades, starfield, tunnel) ----
            // Rendered LAST (after GUI and console) so fades overlay EVERYTHING including the
            // console window — mirrors DXRenderFrame.cpp where fxManager.Render() executes
            // after D2D EndDraw (which contains all GUI/console content).
            fxManager.Render();


            // ---- Present frame ----
            #if defined(_WIN32) || defined(_WIN64)
                // ---- Screen recorder: capture back-buffer BEFORE Present (mirrors DX11 behaviour) ----
                if (screenRecorder.IsRecording())
                    screenRecorder.CaptureFrame(static_cast<UINT>(renderW), static_cast<UINT>(renderH));

                // Apply per-frame VSync setting — update swap interval if config changed
                static bool lastVSync = true;
                bool vsyncNow = config.myConfig.enableVSync;
                if (vsyncNow != lastVSync) {
                    if (WGLEW_EXT_swap_control)
                        wglSwapIntervalEXT(vsyncNow ? 1 : 0);
                    lastVSync = vsyncNow;
                }

                auto presentStart = std::chrono::steady_clock::now();
                SwapBuffers(m_glContext.deviceContext);

                // Software frame cap when VSync is disabled (~60 FPS target, mirrors DX11 behaviour)
                if (!vsyncNow) {
                    const auto targetFrame = std::chrono::milliseconds(16);
                    auto presentEnd  = std::chrono::steady_clock::now();
                    auto frameTime   = std::chrono::duration_cast<std::chrono::milliseconds>(presentEnd - presentStart);
                    if (frameTime < targetFrame)
                        std::this_thread::sleep_for(targetFrame - frameTime);
                }
            #elif defined(__linux__)
                glXSwapBuffers(m_glContext.display, m_glContext.window);
            #elif defined(__ANDROID__)
                eglSwapBuffers(m_glContext.eglDisplay, m_glContext.eglSurface);
            #endif

            // ---- Frame counter ----
            ++frameCount;

        } // end while

        threadManager.threadVars.bIsRendering.store(false);
    }
    catch (const std::exception& e)
    {
        threadManager.threadVars.bIsRendering.store(false);
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Exception: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
        exceptionHandler.LogException(e, "OpenGLRenderer::RenderFrame");
    }
}

#pragma warning(pop)

#endif // __USE_OPENGL__
