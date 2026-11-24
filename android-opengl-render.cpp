// -------------------------------------------------------------------------------------------------------------
// android-opengl-render.cpp — Android OpenGL implementation
// -------------------------------------------------------------------------------------------------------------
// Platform-specific implementation of OpenGLRenderer, split out of OpenGLRenderer.cpp /
// OpenGLRenderFrame.cpp (platform split, 24 Nov 2026) so that working on one platform
// means working in one file. This file is the platform: it contains NO platform guards
// and is compiled only on its platform (Linux/CMakeLists.txt, CrossPlatformGameEngine.vcxproj).
// The code below is moved verbatim from the shared files except where marked Phase D:
// CreateOpenGLContext is the real EGL path (written for Phase D step 2, replacing the
// stub the split left behind), and the SetPlatform* helpers are the ES versions of the
// desktop GL calls the shared code used to make inline.
// -------------------------------------------------------------------------------------------------------------
#define NOMINMAX
#include "Includes.h"
#include "android-opengl-render.h"

// The NDK's GLES3/gl3ext.h omits the EXT_texture_filter_anisotropic enums
// (GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT / GL_TEXTURE_MAX_ANISOTROPY_EXT);
// the ES2 extension header defines them and is valid alongside the ES3
// headers. Needed by LoadTextureFromFilePlatform below.
#include <GLES2/gl2ext.h>

#if defined(__USE_OPENGL__)

bool OpenGLRenderer::SetWindowedScreen()
{
    return false;
}

bool OpenGLRenderer::SetFullExclusive(uint32_t width, uint32_t height)
{
    return false;
}

bool OpenGLRenderer::SetFullScreen()
{
    return false;
}

float OpenGLRenderer::CalculateTextHeight(const std::wstring& text, float fontSize,
                                           float containerHeight)
{
    return fontSize * 1.5f;
}

float OpenGLRenderer::CalculateTextWidth(const std::wstring& text, float fontSize,
                                          float containerWidth)
{
    return static_cast<float>(text.size()) * fontSize * 0.6f;
}

float OpenGLRenderer::GetCharacterWidth(wchar_t character, float fontSize,
                                         const std::wstring& fontName)
{
    return fontSize * 0.6f;
}

float OpenGLRenderer::GetCharacterWidth(wchar_t character, float fontSize, bool bold)
{
    return fontSize * 0.6f;
}

GLuint OpenGLRenderer::RenderTextCenteredToTexture(const std::wstring& text, float fontSize,
    const MyColor& color, int cw, int ch, int& outW, int& outH, bool bold)
{
    outW = cw; outH = ch;
    int tw = 0, th = 0;
    return RenderTextToTexture(text, L"Arial", fontSize, color, tw, th);
}

void OpenGLRenderer::DrawMyTextStyled(const std::wstring& text, const Vector2& position,
    const MyColor& color, const TextRenderStyle& style)
{
    if (text.empty()) return;
    int tw = 0, th = 0;
    // Linux / Android: delegate to DrawMyTextWithFont (no style attributes yet)
    DrawMyTextWithFont(text, position, color, style.fontSize,
                    style.fontName.empty() ? L"Sans" : style.fontName);
}

GLuint OpenGLRenderer::RenderTextToTexture(const std::wstring& text,
    const std::wstring& fontName, float fontSize,
    const MyColor& color, int& outW, int& outH)
{
    outW = 0; outH = 0;
    return 0;
}

bool OpenGLRenderer::LoadTextureFromFilePlatform(const std::wstring& filename, bool is2D, OpenGLTexture& slot)
{
    // ── Non-Windows path: stb_image via OpenGLModelUtils ──────────────────
    GLuint texID = OpenGLModelUtils::LoadGLTextureFromFile(filename);
    if (!texID) {
        // LoadGLTextureFromFile returns 0 both when the file cannot be
        // opened and when the GL upload fails (e.g. no current GL
        // context on this thread). The old message blamed the file in
        // both cases; distinguish them so the log points at the real
        // cause. (The error_code overload never throws.)
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(filename), ec)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[OpenGLRenderer] LoadTexture: cannot open texture file: " + filename);
        } else {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[OpenGLRenderer] LoadTexture: GL texture upload failed (no current GL context on this thread?): " + filename);
        }
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texID);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (is2D) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        GLfloat maxAniso = 1.0f;
        // EXT_texture_filter_anisotropic: the enums come from GLES2/gl2ext.h
        // (the NDK's GLES3/gl3ext.h omits them). On a device without the
        // extension the query errors and maxAniso stays 1.0f, so the block
        // below is skipped — graceful degradation, no crash.
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        if (maxAniso > 1.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
    }

    GLint W = 0, H = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &W);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &H);
    glBindTexture(GL_TEXTURE_2D, 0);

    slot.textureID = texID;
    slot.width     = static_cast<GLsizei>(W);
    slot.height    = static_cast<GLsizei>(H);
    slot.format    = GL_RGBA8;
    slot.target    = GL_TEXTURE_2D;
    slot.isLoaded  = true;
    return true;
}

void OpenGLRenderer::ShutdownPlatformTextBackend()
{
}

void OpenGLRenderer::RestorePlatformDisplayMode()
{
}

void OpenGLRenderer::CleanupPlatformSpecificContext()
{
    // Destroy the shared loader context first: the loader thread is
    // already joined by this point (see main.cpp shutdown order), so no
    // thread can have it current any more. (Linux mirror.)
    if (m_loaderGLContext != EGL_NO_CONTEXT && m_glContext.eglDisplay != EGL_NO_DISPLAY) {
        eglDestroyContext(m_glContext.eglDisplay, m_loaderGLContext);
        m_loaderGLContext = EGL_NO_CONTEXT;
    }
    if (m_glContext.eglLoaderSurface != EGL_NO_SURFACE && m_glContext.eglDisplay != EGL_NO_DISPLAY) {
        eglDestroySurface(m_glContext.eglDisplay, m_glContext.eglLoaderSurface);
        m_glContext.eglLoaderSurface = EGL_NO_SURFACE;
    }
    if (m_glContext.eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_glContext.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_glContext.eglContext  != EGL_NO_CONTEXT) eglDestroyContext(m_glContext.eglDisplay, m_glContext.eglContext);
        if (m_glContext.eglSurface  != EGL_NO_SURFACE) eglDestroySurface(m_glContext.eglDisplay, m_glContext.eglSurface);
        eglTerminate(m_glContext.eglDisplay);
        m_glContext.eglDisplay = EGL_NO_DISPLAY;
        m_glContext.eglContext = EGL_NO_CONTEXT;
        m_glContext.eglSurface = EGL_NO_SURFACE;
    }
}

void OpenGLRenderer::SetupPlatformSpecificContext()
{
    // Platform-specific post-context setup (vsync, debug output, etc.)
    // VSync via eglSwapInterval (this runs right after CreateOpenGLContext,
    // with the context current on the main thread).
    if (m_glContext.eglDisplay != EGL_NO_DISPLAY)
        eglSwapInterval(m_glContext.eglDisplay, config.myConfig.enableVSync ? 1 : 0);
}

bool OpenGLRenderer::InitializeOpenGLExtensions()
{
    // GLES3 needs no extension initialisation (there is no GLEW on Android).
    // The shared code previously called glewInit() here, which does not exist
    // on Android; returning true keeps the extension step a no-op.
    return true;
}

bool OpenGLRenderer::CreateOpenGLContext(NativeWindowHandle hWnd)
{
    // Android/EGL (Phase D): the ANativeWindow was created by the activity
    // and passed in as a void* (see Renderer.h — on Android the handle is
    // the ANativeWindow*). No display to open: EGL uses the default display.
    // Config mirrors the GLX attribs (RGBA8, 24-bit depth, 8-bit stencil,
    // window surface; 4x MSAA samples when the Video settings ask for it).
    ANativeWindow* window = static_cast<ANativeWindow*>(hWnd);
    m_glContext.eglDisplay = EGL_NO_DISPLAY;
    m_glContext.eglContext = EGL_NO_CONTEXT;
    m_glContext.eglSurface = EGL_NO_SURFACE;
    m_glContext.eglLoaderSurface = EGL_NO_SURFACE;
    m_glContext.nativeWindow = nullptr;
    m_loaderGLContext = EGL_NO_CONTEXT;
    if (!window)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] No ANativeWindow passed to CreateOpenGLContext");
        return false;
    }
    m_glContext.nativeWindow = window;

    m_glContext.eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_glContext.eglDisplay == EGL_NO_DISPLAY)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] eglGetDisplay failed — no EGL display available");
        return false;
    }
    EGLint eglMajor = 0, eglMinor = 0;
    if (eglInitialize(m_glContext.eglDisplay, &eglMajor, &eglMinor) == EGL_FALSE)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] eglInitialize failed");
        m_glContext.eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    const bool wantMSAA = config.myConfig.antiAliasingEnabled && config.myConfig.msaaEnabled;
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_SAMPLE_BUFFERS, wantMSAA ? 1 : 0,
        EGL_SAMPLES, wantMSAA ? 4 : 0,
        EGL_NONE
    };
    EGLConfig eglConfig = nullptr;
    EGLint numConfigs = 0;
    if (eglChooseConfig(m_glContext.eglDisplay, configAttribs, &eglConfig, 1, &numConfigs) == EGL_FALSE || numConfigs == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] eglChooseConfig found no suitable EGL config");
        eglTerminate(m_glContext.eglDisplay);
        m_glContext.eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    // GLES 3.1 context (compute shaders need 3.1; the texture-size query
    // uses the gl31.h enums). Fall back to 3.0 when 3.1 is unavailable.
    const EGLint ctxAttribs31[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
    const EGLint ctxAttribs30[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
    const EGLint* ctxAttribs = ctxAttribs31;
    m_glContext.eglContext = eglCreateContext(m_glContext.eglDisplay, eglConfig, EGL_NO_CONTEXT, ctxAttribs31);
    if (m_glContext.eglContext == EGL_NO_CONTEXT)
    {
        ctxAttribs = ctxAttribs30;
        m_glContext.eglContext = eglCreateContext(m_glContext.eglDisplay, eglConfig, EGL_NO_CONTEXT, ctxAttribs30);
    }
    if (m_glContext.eglContext == EGL_NO_CONTEXT)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] eglCreateContext failed");
        eglTerminate(m_glContext.eglDisplay);
        m_glContext.eglDisplay = EGL_NO_DISPLAY;
        return false;
    }
    // Shared loader context (EGL mirror of the WGL/GLX loader context):
    // same version attribs, sharing GL objects with the main context so
    // the loader thread can upload textures.
    m_loaderGLContext = eglCreateContext(m_glContext.eglDisplay, eglConfig, m_glContext.eglContext, ctxAttribs);
    if (m_loaderGLContext == EGL_NO_CONTEXT)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[OpenGLRenderer] Shared loader EGL context creation failed — textures must load on the render thread");
    else
    {
        // The loader context needs a drawable to be current on, and it must
        // not steal the window surface from the render thread: a 1x1 pbuffer
        // surface (core EGL, works on every device — unlike a surfaceless
        // current context, which needs EGL_KHR_surfaceless_context).
        const EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        m_glContext.eglLoaderSurface = eglCreatePbufferSurface(m_glContext.eglDisplay, eglConfig, pbufferAttribs);
        if (m_glContext.eglLoaderSurface == EGL_NO_SURFACE)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[OpenGLRenderer] Loader pbuffer surface creation failed — textures must load on the render thread");
            eglDestroyContext(m_glContext.eglDisplay, m_loaderGLContext);
            m_loaderGLContext = EGL_NO_CONTEXT;
        }
        else
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] Shared loader EGL context created");
    }

    m_glContext.eglSurface = eglCreateWindowSurface(m_glContext.eglDisplay, eglConfig, window, nullptr);
    if (m_glContext.eglSurface == EGL_NO_SURFACE)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] eglCreateWindowSurface failed");
        if (m_loaderGLContext != EGL_NO_CONTEXT) { eglDestroyContext(m_glContext.eglDisplay, m_loaderGLContext); m_loaderGLContext = EGL_NO_CONTEXT; }
        if (m_glContext.eglLoaderSurface != EGL_NO_SURFACE) { eglDestroySurface(m_glContext.eglDisplay, m_glContext.eglLoaderSurface); m_glContext.eglLoaderSurface = EGL_NO_SURFACE; }
        eglDestroyContext(m_glContext.eglDisplay, m_glContext.eglContext); m_glContext.eglContext = EGL_NO_CONTEXT;
        eglTerminate(m_glContext.eglDisplay); m_glContext.eglDisplay = EGL_NO_DISPLAY;
        return false;
    }
    if (eglMakeCurrent(m_glContext.eglDisplay, m_glContext.eglSurface, m_glContext.eglSurface, m_glContext.eglContext) == EGL_FALSE)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] eglMakeCurrent failed");
        eglDestroySurface(m_glContext.eglDisplay, m_glContext.eglSurface); m_glContext.eglSurface = EGL_NO_SURFACE;
        if (m_loaderGLContext != EGL_NO_CONTEXT) { eglDestroyContext(m_glContext.eglDisplay, m_loaderGLContext); m_loaderGLContext = EGL_NO_CONTEXT; }
        if (m_glContext.eglLoaderSurface != EGL_NO_SURFACE) { eglDestroySurface(m_glContext.eglDisplay, m_glContext.eglLoaderSurface); m_glContext.eglLoaderSurface = EGL_NO_SURFACE; }
        eglDestroyContext(m_glContext.eglDisplay, m_glContext.eglContext); m_glContext.eglContext = EGL_NO_CONTEXT;
        eglTerminate(m_glContext.eglDisplay); m_glContext.eglDisplay = EGL_NO_DISPLAY;
        return false;
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] EGL OpenGL ES 3.x context created (EGL %d.%d)", eglMajor, eglMinor);
    return true;
}

void OpenGLRenderer::ReleasePlatformContext()
{
    // Transfer the EGL context from the main thread to the render thread:
    // unbind it here; the render thread rebinds it with
    // MakePlatformContextCurrent(). (Linux mirror: glXMakeCurrent(display,
    // None, nullptr).)
    if (m_glContext.eglDisplay != EGL_NO_DISPLAY)
        eglMakeCurrent(m_glContext.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void OpenGLRenderer::DetectPlatformGPUCaps()
{
}

void OpenGLRenderer::InitPlatformTextBackend()
{
}

void OpenGLRenderer::PresentPlatformFrame()
{
    eglSwapBuffers(m_glContext.eglDisplay, m_glContext.eglSurface);
}

void OpenGLRenderer::RebindPlatformContext()
{
}

bool OpenGLRenderer::MakePlatformContextCurrent()
{
    if (eglMakeCurrent(m_glContext.eglDisplay, m_glContext.eglSurface, m_glContext.eglSurface, m_glContext.eglContext) == EGL_FALSE) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] eglMakeCurrent failed — render thread cannot acquire GL context");
        return false;
    }
    return true;
}

bool OpenGLRenderer::HasPlatformGLContext() const
{
    return m_glContext.eglContext != EGL_NO_CONTEXT;
}

void OpenGLRenderer::DrawIntroMovieFrame()
{
}

// Small GL helpers factored out of shared functions (platform split, 24 Nov 2026;
// Phase D EGL work) — the desktop enums they use do not exist in OpenGL ES.
void OpenGLRenderer::SetPlatformLineSmoothing(bool /*enable*/)
{
    // OpenGL ES has no line smoothing (GL_LINE_SMOOTH was removed); lines
    // are aliased. The outline draw in DrawFilledPolygon still runs, just
    // without smoothing.
}

void OpenGLRenderer::SetPlatformMultisampling(bool /*enable*/)
{
    // No GL_MULTISAMPLE enable in ES: multisampling is a property of the
    // EGL config (SAMPLE_BUFFERS / SAMPLES, chosen in CreateOpenGLContext
    // from the same Video settings), not a toggle.
}

void OpenGLRenderer::SetupPlatformShadowTextureParams(GLenum target, const float* /*border*/)
{
    // Border clamp is ES 3.2 only (GL_CLAMP_TO_BORDER / GL_TEXTURE_BORDER_COLOR
    // live in gl32.h) and the context is ES 3.1, so clamp to the edge instead:
    // out-of-range shadow lookups read the edge texel rather than the border
    // colour (1.0 = lit). Same min/mag/compare setup as desktop — only the
    // wrap mode differs.
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void OpenGLRenderer::SetPlatformDrawBufferNone()
{
    // No glDrawBuffer in ES; glDrawBuffers (ES 3.0) with a single GL_NONE
    // is the equivalent for the depth-only shadow FBO.
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
}

#endif // __USE_OPENGL__
