// -------------------------------------------------------------------------------------------------------------
// OpenGLRenderer.cpp - OpenGL Renderer Implementation
// 
// This file contains the complete implementation of the OpenGLRenderer class for cross-platform
// OpenGL rendering support. It handles initialization, resource management, and rendering operations
// for Windows, Linux, Android, iOS, and macOS platforms using conditional compilation.
//
// The implementation follows the same architectural patterns as DX11Renderer but uses OpenGL
// instead of DirectX for graphics operations.
// -------------------------------------------------------------------------------------------------------------

#define NOMINMAX                                                                // Prevent Windows min/max macro conflicts

#include "Includes.h"

// OpenGL Required Headers & Linking
#include "Renderer.h"

// Perform Renderer to USE Test.
// This is done to ensure we only include required code.
// Meaning, if we are NOT using this Renderer, forget it
// and DO NOT include its code.
#if defined(__USE_OPENGL__)
#include "OpenGLRenderer.h"
#include "Debug.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "DX_FXManager.h"
#include "GUIManager.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "MoviePlayer.h"

#if defined(__USE_MP3PLAYER__)
#include "WinMediaPlayer.h"
#elif defined(__USE_XMPLAYER__)
#include "XMMODPlayer.h"
#endif

// Platform-specific linking directives
#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "opengl32.lib")                                            // Link Windows OpenGL library
#pragma comment(lib, "glu32.lib")                                               // Link Windows GLU library
#pragma comment(lib, "glew32.lib")                                              // Link GLEW extension library
#endif

// Static member initialization
std::mutex OpenGLRenderer::s_renderMutex;                                      // Initialize static render synchronization mutex
std::mutex OpenGLRenderer::s_loaderMutex;                                      // Initialize static loader synchronization mutex

class LightsManager;                                                            // Forward declaration for lighting system

// External global references required by the renderer
extern HWND hwnd;                                                               // Main window handle
extern HINSTANCE hInst;                                                         // Application instance handle
extern GUIManager guiManager;                                                   // GUI management system
extern Debug debug;                                                             // Debug logging system
extern SystemUtils sysUtils;                                                    // System utilities
extern SceneManager scene;                                                      // Scene management system
extern ThreadManager threadManager;                                             // Thread management system
extern FXManager fxManager;                                                     // Effects management system
extern Vector2 myMouseCoords;                                                   // Current mouse coordinates
extern Model models[MAX_MODELS];                                                // Global model array
extern LightsManager lightsManager;                                             // Lighting management system
extern MoviePlayer moviePlayer;                                                 // Video playback system
extern WindowMetrics winMetrics;                                                // Window metrics and properties

extern bool bResizing;                                                          // Window resize state flag
extern std::atomic<bool> bResizeInProgress;                                     // Prevents multiple resize operations
extern std::atomic<bool> bFullScreenTransition;                                // Prevents handling during fullscreen transitions

#if defined(__USE_MP3PLAYER__)
extern MediaPlayer player;                                                      // Media player for MP3 audio
#elif defined(__USE_XMPLAYER__)
extern XMMODPlayer xmPlayer;                                                    // XM/MOD music player
#endif

// Constructor/Destructor
OpenGLRenderer::OpenGLRenderer()
{
    // IMPORTANT: Set the RendererType to OpenGL SO that the Engine knows which renderer to use and refer too.
    sName = threadManager.getThreadName(THREAD_RENDERER);                       // Get thread name from thread manager
    RenderType = RendererType::RT_OpenGL;                                       // Set renderer type to OpenGL

    // Initialize OpenGL context structure to zero
    SecureZeroMemory(&m_glContext, sizeof(m_glContext));                        // Clear OpenGL context structure
    SecureZeroMemory(&m_2dTextures, sizeof(m_2dTextures));                      // Clear 2D texture array
    SecureZeroMemory(&m_3dTextures, sizeof(m_3dTextures));                      // Clear 3D texture array
    SecureZeroMemory(&My2DBlitQueue, sizeof(My2DBlitQueue));                    // Clear 2D blit queue
    SecureZeroMemory(&screenModes, sizeof(screenModes));                        // Clear screen modes array
    SecureZeroMemory(&m_uniformBuffers, sizeof(m_uniformBuffers));              // Clear uniform buffer array

    // Initialize OpenGL object IDs to invalid values
    m_framebufferID = 0;                                                        // Initialize framebuffer ID
    m_colorTextureID = 0;                                                       // Initialize color texture ID
    m_depthTextureID = 0;                                                       // Initialize depth texture ID
    m_vertexArrayID = 0;                                                        // Initialize vertex array ID

    // Initialize shader program structure
    m_mainShaderProgram.programID = 0;                                          // Initialize main shader program ID
    m_mainShaderProgram.vertexShaderID = 0;                                     // Initialize vertex shader ID
    m_mainShaderProgram.fragmentShaderID = 0;                                   // Initialize fragment shader ID
    m_mainShaderProgram.isLinked = false;                                       // Set shader program as not linked

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"OpenGLRenderer: Constructor initialized successfully"); // Log successful constructor initialization
#endif
}

OpenGLRenderer::~OpenGLRenderer()
{
    if (bIsDestroyed.load()) return;                                            // Prevent double destruction

    Cleanup();                                                                  // Perform cleanup operations
    debug.logLevelMessage(LogLevel::LOG_INFO, L"OpenGLRenderer: Cleaned up and Destroyed!"); // Log successful destruction
    bIsDestroyed.store(true);                                                   // Mark as destroyed
}

//-----------------------------------------
// Core Rendering Interface
//-----------------------------------------
void OpenGLRenderer::Initialize(HWND hwnd, HINSTANCE hInstance) {
    // Set the Renderer Name
    RendererName(RENDERER_NAME);                                                // Set renderer identification name
    iOrigWidth = winMetrics.clientWidth;                                        // Store original window width
    iOrigHeight = winMetrics.clientHeight;                                      // Store original window height

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"OpenGLRenderer: Initializing with dimensions %dx%d", iOrigWidth, iOrigHeight); // Log initialization parameters
#endif

    // Initialize OpenGL Context and Extensions
    CreateOpenGLContext(hwnd);                                                  // Create platform-specific OpenGL context
    InitializeOpenGLExtensions();                                               // Initialize OpenGL extensions and function pointers
    CreateFramebufferObjects();                                                 // Create OpenGL framebuffer objects
    SetupViewport();                                                            // Setup OpenGL viewport configuration
    SetupRenderStates();                                                        // Setup OpenGL rendering pipeline states
    LoadShaders();                                                              // Load and compile OpenGL shaders

    // Create Uniform Buffer Objects for shader data
    for (int i = 0; i < 8; ++i) {                                               // Create 8 uniform buffer objects
        glGenBuffers(1, &m_uniformBuffers[i].bufferID);                         // Generate OpenGL buffer object
        m_uniformBuffers[i].target = GL_UNIFORM_BUFFER;                         // Set buffer target type
        m_uniformBuffers[i].usage = GL_DYNAMIC_DRAW;                            // Set buffer usage pattern
        m_uniformBuffers[i].isAllocated = false;                                // Mark as not yet allocated

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
        if (!CheckOpenGLError("Create Uniform Buffer " + std::to_string(i))) {  // Check for OpenGL errors
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"OpenGLRenderer: Failed to create uniform buffer %d", i); // Log error if buffer creation failed
            return;                                                             // Exit initialization on error
        }
#endif
    }

    // Initialize Camera Uniform Buffer (equivalent to DX11's camera constant buffer)
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniformBuffers[UNIFORM_VIEW_MATRIX].bufferID); // Bind camera uniform buffer
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ConstantBuffer), nullptr, GL_DYNAMIC_DRAW); // Allocate buffer memory
    m_uniformBuffers[UNIFORM_VIEW_MATRIX].size = sizeof(ConstantBuffer);        // Store buffer size
    m_uniformBuffers[UNIFORM_VIEW_MATRIX].isAllocated = true;                   // Mark as allocated
    glBindBufferBase(GL_UNIFORM_BUFFER, UNIFORM_VIEW_MATRIX, m_uniformBuffers[UNIFORM_VIEW_MATRIX].bufferID); // Bind to uniform buffer binding point

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    if (!CheckOpenGLError("Camera Uniform Buffer Setup")) {                     // Check for OpenGL errors
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"OpenGLRenderer: Failed to create camera uniform buffer"); // Log error if camera buffer setup failed
        return;                                                                 // Exit initialization on error
    }
#endif

    // Initialize our Camera to default values
    if (!threadManager.threadVars.bIsResizing.load())                           // Only initialize camera if not resizing
    {
        myCamera.SetupDefaultCamera(iOrigWidth, iOrigHeight);                   // Setup camera with default parameters
    }

    // Create Global Light Uniform Buffer (equivalent to DX11's global light buffer)
    glBindBuffer(GL_UNIFORM_BUFFER, m_uniformBuffers[UNIFORM_GLOBAL_LIGHT_BUFFER].bufferID); // Bind global light uniform buffer
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GlobalLightBuffer), nullptr, GL_DYNAMIC_DRAW); // Allocate buffer memory
    m_uniformBuffers[UNIFORM_GLOBAL_LIGHT_BUFFER].size = sizeof(GlobalLightBuffer); // Store buffer size
    m_uniformBuffers[UNIFORM_GLOBAL_LIGHT_BUFFER].isAllocated = true;           // Mark as allocated
    glBindBufferBase(GL_UNIFORM_BUFFER, UNIFORM_GLOBAL_LIGHT_BUFFER, m_uniformBuffers[UNIFORM_GLOBAL_LIGHT_BUFFER].bufferID); // Bind to uniform buffer binding point

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    if (!CheckOpenGLError("Global Light Buffer Setup")) {                       // Check for OpenGL errors
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"OpenGLRenderer: Failed to create global light buffer"); // Log error if light buffer setup failed
        return;                                                                 // Exit initialization on error
    }
#endif

    sysUtils.DisableMouseCursor();                                              // Hide system mouse cursor for custom rendering

    bIsInitialized.store(true);                                                 // Mark renderer as initialized
    if (threadManager.threadVars.bIsResizing.load())                            // Check if currently resizing
    {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"OpenGLRenderer: Rendering Engine Initialised and Activated."); // Log successful initialization
    }
    else
    {
        // We are resizing the window, so restart the loading sequence.
        threadManager.ResumeThread(THREAD_LOADER);                              // Resume loader thread if not resizing
    }

    threadManager.threadVars.bIsResizing.store(false);                          // Clear resizing flag

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"OpenGLRenderer: Initialization completed successfully"); // Log successful completion
#endif
}

bool OpenGLRenderer::StartRendererThreads()
{
    bool result = true;                                                         // Initialize success flag
    try
    {
        // Initialise and Start the Loader Thread
        threadManager.SetThread(THREAD_LOADER, [this]() { LoaderTaskThread(); }, true); // Set loader thread with lambda function
        threadManager.StartThread(THREAD_LOADER);                              // Start the loader thread

        // Initialize & start the renderer thread
#ifdef RENDERER_IS_THREAD
        threadManager.SetThread(THREAD_RENDERER, [this]() { RenderFrame(); }, true); // Set renderer thread with lambda function
        threadManager.StartThread(THREAD_RENDERER);                            // Start the renderer thread
#endif

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"OpenGLRenderer: Renderer threads started successfully"); // Log successful thread startup
#endif
    }
    catch (const std::exception& e)                                             // Catch any exceptions during thread creation
    {
        result = false;                                                         // Set failure flag

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"OpenGLRenderer: Exception in StartRendererThreads: %s",
            std::wstring(e.what(), e.what() + strlen(e.what())).c_str());       // Log exception details
#endif
    }

    return result;                                                              // Return success/failure status
}

#endif