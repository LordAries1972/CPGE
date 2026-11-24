// -------------------------------------------------------------------------------------------------------------
// linux-opengl-render.cpp — Linux OpenGL implementation
// -------------------------------------------------------------------------------------------------------------
// Platform-specific implementation of OpenGLRenderer, split out of OpenGLRenderer.cpp /
// OpenGLRenderFrame.cpp (platform split, 24 Nov 2026) so that working on one platform
// means working in one file. This file is the platform: it contains NO platform guards
// and is compiled only on its platform (Linux/CMakeLists.txt, CrossPlatformGameEngine.vcxproj).
// The code below is moved verbatim from the shared files; behaviour is unchanged.
// -------------------------------------------------------------------------------------------------------------
#define NOMINMAX
#include "Includes.h"
#include "linux-opengl-render.h"

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
    // thread can have it current any more.
    if (m_loaderGLContext) {
        glXDestroyContext(m_glContext.display, m_loaderGLContext);
        m_loaderGLContext = nullptr;
    }
    if (m_glContext.glxContext) {
        glXMakeCurrent(m_glContext.display, None, nullptr);
        glXDestroyContext(m_glContext.display, m_glContext.glxContext);
        m_glContext.glxContext = nullptr;
    }
    if (m_glContext.visualInfo) {
        XFree(m_glContext.visualInfo);
        m_glContext.visualInfo = nullptr;
    }
    if (m_glContext.display) {
        XCloseDisplay(m_glContext.display);
        m_glContext.display = nullptr;
    }
}

void OpenGLRenderer::SetupPlatformSpecificContext()
{
    // Platform-specific post-context setup (vsync, debug output, etc.)
    // VSync via GLX_EXT_swap_control, loaded directly (this also runs
    // before GLEW is initialised, so glXGetProcAddress is used instead
    // of the GLEW extension pointer).
    using PFNGLXSWAPINTERVALEXTPROC = void (*)(Display*, GLXDrawable, int);
    auto glXSwapIntervalEXT = reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>(
        glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT")));
    if (glXSwapIntervalEXT && m_glContext.display && m_glContext.window)
        glXSwapIntervalEXT(m_glContext.display, m_glContext.window, config.myConfig.enableVSync ? 1 : 0);
}

bool OpenGLRenderer::InitializeOpenGLExtensions()
{
    GLenum err = glewInit();
    return err == GLEW_OK;
}

bool OpenGLRenderer::CreateOpenGLContext(NativeWindowHandle hWnd)
{
    // Linux/GLX: the X11 window was created by main.cpp and passed in as
    // an HWND (an X11 Window carried through uintptr_t — see Platform.h).
    // Open our own Display connection: the renderer owns its GLX context
    // and closes this connection again in CleanupPlatformSpecificContext.
    // (KeyboardHandler opens a second, independent connection for input.)
    ::Window xWindow = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(hWnd));
    if (xWindow == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] No X11 window passed to CreateOpenGLContext");
        return false;
    }
    m_glContext.display = XOpenDisplay(nullptr);
    if (!m_glContext.display)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] XOpenDisplay failed — no X server available");
        return false;
    }
    m_glContext.window = xWindow;

    // Pick a framebuffer config: RGBA8, 24-bit depth, 8-bit stencil,
    // double-buffered; 4x MSAA samples when the config asks for it.
    // main.cpp creates the window from this same first matching config,
    // so the window visual and the context visual always agree.
    const bool wantMSAA = config.myConfig.antiAliasingEnabled && config.myConfig.msaaEnabled;
    int fbAttribs[] = {
        GLX_X_RENDERABLE,  True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8,
        GLX_BLUE_SIZE,     8,
        GLX_ALPHA_SIZE,    8,
        GLX_DEPTH_SIZE,    24,
        GLX_STENCIL_SIZE,  8,
        GLX_DOUBLEBUFFER,  True,
        GLX_SAMPLE_BUFFERS, wantMSAA ? 1 : 0,
        GLX_SAMPLES,        wantMSAA ? 4 : 0,
        None
    };
    int fbCount = 0;
    GLXFBConfig* fbConfigs = glXChooseFBConfig(m_glContext.display, DefaultScreen(m_glContext.display), fbAttribs, &fbCount);
    if (!fbConfigs || fbCount == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] glXChooseFBConfig found no suitable framebuffer config");
        if (fbConfigs) XFree(fbConfigs);
        XCloseDisplay(m_glContext.display); m_glContext.display = nullptr;
        return false;
    }
    GLXFBConfig fbConfig = fbConfigs[0];
    m_glContext.visualInfo = glXGetVisualFromFBConfig(m_glContext.display, fbConfig);
    if (!m_glContext.visualInfo)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] glXGetVisualFromFBConfig failed");
        XFree(fbConfigs);
        XCloseDisplay(m_glContext.display); m_glContext.display = nullptr;
        return false;
    }

    // Create a 3.3 core-profile context. glXCreateContextAttribsARB is an
    // extension, so load it directly with glXGetProcAddress (GLEW is not
    // initialised yet — that happens in InitializeOpenGLExtensions after
    // this call returns). Fall back to a legacy context if unavailable.
    using PFNGLXCREATECONTEXTATTRIBSARBPROC = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    auto glXCreateContextAttribsARB = reinterpret_cast<PFNGLXCREATECONTEXTATTRIBSARBPROC>(
        glXGetProcAddress(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    if (glXCreateContextAttribsARB)
    {
        int ctxAttribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
            GLX_CONTEXT_MINOR_VERSION_ARB, 3,
            GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            #if defined(_DEBUG)
                GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
            #endif
            None
        };
        m_glContext.glxContext = glXCreateContextAttribsARB(m_glContext.display, fbConfig, nullptr, True, ctxAttribs);
        // Shared loader context (Linux mirror of the Windows WGL loader
        // context): same 3.3 core attribs, sharing GL objects with the
        // main context so the loader thread can upload textures.
        if (m_glContext.glxContext)
            m_loaderGLContext = glXCreateContextAttribsARB(m_glContext.display, fbConfig, m_glContext.glxContext, True, ctxAttribs);
    }
    XFree(fbConfigs);
    if (!m_glContext.glxContext)
        m_glContext.glxContext = glXCreateContext(m_glContext.display, m_glContext.visualInfo, nullptr, True);
    // Legacy fallback for the loader context (no attribs extension, or
    // the attribs creation above failed): a legacy context sharing
    // objects with the main context is enough for texture uploads.
    if (!m_loaderGLContext && m_glContext.glxContext)
        m_loaderGLContext = glXCreateContext(m_glContext.display, m_glContext.visualInfo, m_glContext.glxContext, True);
    if (!m_glContext.glxContext)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] GLX context creation failed");
        XFree(m_glContext.visualInfo); m_glContext.visualInfo = nullptr;
        XCloseDisplay(m_glContext.display); m_glContext.display = nullptr;
        return false;
    }
    if (!m_loaderGLContext)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[OpenGLRenderer] Shared loader context creation failed — textures must load on the render thread");
    else
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] Shared loader GLX context created");
    if (!glXMakeCurrent(m_glContext.display, m_glContext.window, m_glContext.glxContext))
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] glXMakeCurrent failed");
        if (m_loaderGLContext) { glXDestroyContext(m_glContext.display, m_loaderGLContext); m_loaderGLContext = nullptr; }
        glXDestroyContext(m_glContext.display, m_glContext.glxContext); m_glContext.glxContext = nullptr;
        XFree(m_glContext.visualInfo); m_glContext.visualInfo = nullptr;
        XCloseDisplay(m_glContext.display); m_glContext.display = nullptr;
        return false;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] GLX OpenGL 3.3 Core context created");
    return true;
}

void OpenGLRenderer::ReleasePlatformContext()
{
    // Transfer GLX context ownership from main thread to render thread.
    if (m_glContext.display && m_glContext.glxContext)
        glXMakeCurrent(m_glContext.display, None, nullptr);
}

void OpenGLRenderer::DetectPlatformGPUCaps()
{
}

void OpenGLRenderer::InitPlatformTextBackend()
{
}

void OpenGLRenderer::PresentPlatformFrame()
{
    glXSwapBuffers(m_glContext.display, m_glContext.window);
}

void OpenGLRenderer::RebindPlatformContext()
{
    if (m_pendingContextRebind.exchange(false)) {
        glXMakeCurrent(m_glContext.display, None, nullptr);
        glXMakeCurrent(m_glContext.display, m_glContext.window, m_glContext.glxContext);
    }
}

bool OpenGLRenderer::MakePlatformContextCurrent()
{
    if (!glXMakeCurrent(m_glContext.display, m_glContext.window, m_glContext.glxContext)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] glXMakeCurrent failed — render thread cannot acquire GL context");
        return false;
    }
    return true;
}

bool OpenGLRenderer::HasPlatformGLContext() const
{
    return m_glContext.glxContext != nullptr;
}

void OpenGLRenderer::DrawIntroMovieFrame()
{
}

// Small GL helpers factored out of shared functions (platform split, 24 Nov 2026;
// Phase D EGL work) — the desktop enums they use do not exist in OpenGL ES.
void OpenGLRenderer::SetPlatformLineSmoothing(bool enable)
{
    if (enable) glEnable(GL_LINE_SMOOTH); else glDisable(GL_LINE_SMOOTH);
}

void OpenGLRenderer::SetPlatformMultisampling(bool enable)
{
    if (enable) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
}

void OpenGLRenderer::SetupPlatformShadowTextureParams(GLenum target, const float border[4])
{
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
}

void OpenGLRenderer::SetPlatformDrawBufferNone()
{
    glDrawBuffer(GL_NONE);
}

#endif // __USE_OPENGL__
