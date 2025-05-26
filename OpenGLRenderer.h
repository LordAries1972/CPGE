// -------------------------------------------------------------------------------------------------------------
// Notes Last Modified: 07-03-2025
//
// This is the implementation of the OpenGLRenderer class, which is a concrete implementation of the Renderer.
// It uses OpenGL for rendering 3D graphics and platform-specific APIs for 2D graphics and text rendering.
// The class is responsible for initializing the rendering pipeline, loading shaders, and rendering 3D/2D objects.
// 
// The OpenGLRenderer class is designed to be used in a multithreaded environment, where the rendering is done
// in a separate thread to avoid blocking the main thread. The class provides methods for rendering 2D and 3D
// objects, as well as text rendering and texture loading.
// 
// PLATFORM SUPPORT:
// ==================
// This implementation supports Windows, Linux, Android, iOS, and macOS systems with proper conditional
// compilation directives for each platform's specific OpenGL and windowing requirements.
// -------------------------------------------------------------------------------------------------------------
#pragma once

//-------------------------------------------------------------------------------------------------
// OpenGLRenderer.h - OpenGL Renderer Interface
//-------------------------------------------------------------------------------------------------
#include "Includes.h"
#include "Renderer.h"                               // We must include the abstract base class here!!!!

#if defined(__USE_OPENGL__)

// Platform-specific includes for OpenGL context creation and management
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>
// Include modern OpenGL extensions for Windows
#ifndef GLEW_STATIC
#define GLEW_STATIC
#endif
#include <GL/glew.h>
#include <GL/wglew.h>
#elif defined(__linux__)
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <GL/glew.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#elif defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>
#include <android/native_window.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    // iOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
    // macOS
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <OpenGL/OpenGL.h>
#endif
#endif

#include "Vectors.h"
#include "Color.h"
#include "Models.h"
#include "ThreadManager.h"
#include "ConstantBuffer.h"

const std::string RENDERER_NAME = "OpenGLRenderer";

// Reserved Shader Uniform Locations for Render Pipeline
const int UNIFORM_VIEW_MATRIX = 0;                                              // View Matrix Uniform Location   
const int UNIFORM_PROJECTION_MATRIX = 1;                                        // Projection Matrix Uniform Location
const int UNIFORM_MODEL_MATRIX = 2;                                             // Model Matrix Uniform Location
const int UNIFORM_CAMERA_POSITION = 3;                                          // Camera Position Uniform Location
const int UNIFORM_LIGHT_BUFFER = 4;                                             // Model Light Buffer Uniform Location
const int UNIFORM_GLOBAL_LIGHT_BUFFER = 5;                                      // Global Light Buffer Uniform Location
const int UNIFORM_MATERIAL_BUFFER = 6;                                          // Material Buffer Uniform Location
const int UNIFORM_ENVIRONMENT_BUFFER = 7;                                       // Environment Settings Buffer Uniform Location

// Reserved Texture Units for Fragment Shader
const int TEXTURE_UNIT_DIFFUSE = 0;                                             // Diffuse Textures Unit
const int TEXTURE_UNIT_NORMAL = 1;                                              // Normal Texture Mappings Unit
const int TEXTURE_UNIT_METALLIC = 2;                                            // Metallic Mappings Unit
const int TEXTURE_UNIT_ROUGHNESS = 3;                                           // Roughness Mappings Unit
const int TEXTURE_UNIT_AO = 4;                                                  // Ambient Occlusion Mapping Unit
const int TEXTURE_UNIT_ENVIRONMENT = 5;                                         // Environment Mappings for Reflections Unit

// Forward declarations
class Debug;
class SystemUtils;
class GUIManager;
class FXManager;
class Camera;
class Model;

// Platform-specific context structures
#if defined(_WIN32) || defined(_WIN64)
struct OpenGLContext {
    HDC deviceContext;                                                      // Windows Device Context
    HGLRC renderingContext;                                                 // Windows OpenGL Rendering Context
    HWND windowHandle;                                                      // Window Handle
    PIXELFORMATDESCRIPTOR pixelFormatDescriptor;                            // Pixel Format Descriptor
};
#elif defined(__linux__)
struct OpenGLContext {
    Display* display;                                                       // X11 Display
    Window window;                                                          // X11 Window
    GLXContext glxContext;                                                  // GLX Context
    XVisualInfo* visualInfo;                                                // Visual Information
};
#elif defined(__ANDROID__)
struct OpenGLContext {
    EGLDisplay eglDisplay;                                                  // EGL Display
    EGLContext eglContext;                                                  // EGL Context
    EGLSurface eglSurface;                                                  // EGL Surface
    ANativeWindow* nativeWindow;                                            // Android Native Window
};
#elif defined(__APPLE__)
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
struct OpenGLContext {
    void* eaglContext;                                                  // iOS EAGL Context (void* to avoid Objective-C in header)
    void* framebuffer;                                                  // iOS Framebuffer
    void* renderbuffer;                                                 // iOS Renderbuffer
};
#else
struct OpenGLContext {
    void* nsOpenGLContext;                                              // macOS NSOpenGLContext (void* to avoid Objective-C in header)
    void* nsView;                                                       // macOS NSView
};
#endif
#endif

struct AvailOpenGLModes {                                                       // Details of Available Screen Resolution Mode from OpenGL enumeration
    bool InUse;                                                                 // Flag indicating if this mode is currently in use
    int iWidth;                                                                 // Screen width in pixels
    int iHeight;                                                                // Screen height in pixels
    int iBPP;                                                                   // Bits per pixel color depth
    int iRefreshRate;                                                           // Monitor refresh rate in Hz
    int iMonitor;                                                               // Monitor index for multi-monitor setups
};

struct AvailOpenGLScreenModes {
    int iAdapter = 0;                                                           // Graphics adapter index
    std::vector<AvailOpenGLModes> modes;                                        // Dynamic storage for available modes
};

// OpenGL Texture Storage Structure
struct OpenGLTexture {
    GLuint textureID;                                                           // OpenGL texture identifier
    GLenum target;                                                              // Texture target (GL_TEXTURE_2D, etc.)
    GLsizei width;                                                              // Texture width in pixels
    GLsizei height;                                                             // Texture height in pixels
    GLenum format;                                                              // Internal format (GL_RGBA, etc.)
    bool isLoaded;                                                              // Flag indicating if texture is loaded
};

// OpenGL Shader Program Structure
struct OpenGLShaderProgram {
    GLuint programID;                                                           // OpenGL shader program identifier
    GLuint vertexShaderID;                                                      // Vertex shader identifier
    GLuint fragmentShaderID;                                                    // Fragment shader identifier
    bool isLinked;                                                              // Flag indicating if program is linked
};

// OpenGL Buffer Object Structure
struct OpenGLBuffer {
    GLuint bufferID;                                                            // OpenGL buffer object identifier
    GLenum target;                                                              // Buffer target (GL_ARRAY_BUFFER, etc.)
    GLenum usage;                                                               // Buffer usage pattern (GL_STATIC_DRAW, etc.)
    GLsizeiptr size;                                                            // Buffer size in bytes
    bool isAllocated;                                                           // Flag indicating if buffer is allocated
};

// -------------------------------------------------------------------------------------------------------------
// Our Main OpenGL Renderer Class
// -------------------------------------------------------------------------------------------------------------
class OpenGLRenderer : public Renderer {
public:
    OpenGLRenderer();                                                           // Constructor - Initialize OpenGL renderer
    ~OpenGLRenderer();                                                          // Destructor - Cleanup OpenGL resources

    // These are used when we resize our window
    int iOrigWidth = DEFAULT_WINDOW_WIDTH;                                      // Original window width
    int iOrigHeight = DEFAULT_WINDOW_HEIGHT;                                    // Original window height

    // Default toggle flag for displaying models in Wireframe mode.
    // In Runtime, use the F2 key to toggle status.
    bool bWireframeMode = false;                                                // Wireframe rendering mode flag

    // Instantiate our required classes & structures.
    Camera myCamera;                                                            // Camera instance for view transformations
    GFXObjQueue My2DBlitQueue[MAX_2D_IMG_QUEUE_OBJS];                          // Our 2D Blit Queue for sprite rendering
    AvailOpenGLScreenModes screenModes[MAX_SCREEN_MONITORS];                    // Available screen modes for each monitor

    // OpenGL-specific data structures
    OpenGLContext m_glContext;                                                  // Platform-specific OpenGL context
    OpenGLTexture m_2dTextures[MAX_TEXTURE_BUFFERS];                           // 2D texture storage array
    OpenGLTexture m_3dTextures[MAX_TEXTURE_BUFFERS_3D];                        // 3D texture storage array
    OpenGLShaderProgram m_mainShaderProgram;                                    // Main rendering shader program
    OpenGLBuffer m_uniformBuffers[8];                                           // Uniform buffer objects for shader data

    GLuint m_framebufferID;                                                     // Main framebuffer object identifier
    GLuint m_colorTextureID;                                                    // Color attachment texture identifier
    GLuint m_depthTextureID;                                                    // Depth attachment texture identifier
    GLuint m_vertexArrayID;                                                     // Vertex Array Object identifier

    std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now(); // Frame timing for delta calculations

#if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    void TestDrawTriangle();                                                // Used to test pipeline functionality only!
    void SetDebugMode(int mode);                                            // Set OpenGL debug output mode
#endif

    // Override all pure virtual functions from the base class Renderer
    void Initialize(HWND hwnd, HINSTANCE hInstance) override;                   // Initialize OpenGL renderer with platform window
    bool StartRendererThreads();                                               // Start rendering and loader threads
    void RenderFrame() override;                                                // Main rendering loop function
    void LoaderTaskThread() override;                                           // Asset loading thread function
    void Cleanup() override;                                                    // Cleanup all OpenGL resources

    // Our function and procedure definitions for this class.
    bool LoadTexture(int textureId, const std::wstring& filename, bool is2D);   // Load texture from file into OpenGL
    bool LoadAllKnownTextures();                                                // Load all predefined textures
    bool Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl, BlitObj2DType objType, BlitObj2DDetails objDetails, CanBlitType BlitType); // Add 2D object to rendering queue

    // Draws a single X x Y sized pixel at the specified position with the given RGBA color.
    void Blit2DColoredPixel(int x, int y, float pixelSize, Vector4 color);      // Render colored pixel using OpenGL

    void Resize(uint32_t width, uint32_t height) override;                      // Resize OpenGL viewport and framebuffers
    void WaitForGPUToFinish();                                                  // Wait for all OpenGL commands to complete
    void UnloadTexture(int textureId, bool is2D);                               // Unload texture from OpenGL memory
    void Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY);               // Render 2D object at position
    void Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight); // Render 2D object scaled to size
    void Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY); // Render 2D object with texture offset
    void Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY); // Render wrapped 2D object with offset
    void Clear2DBlitQueue();                                                    // Clear all objects from 2D rendering queue
    void ResumeLoader(bool isResizing = false) override;                        // Resume asset loading thread

    // Video Frame Rendering.
    void DrawVideoFrame(const Vector2& position, const Vector2& size, const MyColor& tintColor, GLuint textureID); // Render video frame texture

    // GuiManager Render functions.
    void DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight); // Render centered text

    // Helper Functions
    bool SetFullScreen(void) override;                                          // Set display to fullscreen mode
    bool SetFullExclusive(uint32_t width, uint32_t height) override;            // Set exclusive fullscreen with specific resolution
    bool SetWindowedScreen(void) override;                                      // Set display to windowed mode

    // Base class overrides
    void DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) override; // Render filled rectangle
    void DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) override; // Render text string
    void DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) override; // Render text in bounds
    void DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, const std::wstring& fontName); // Render text with specific font

    void DrawTexture(int textureId, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) override; // Render textured quad
    void RendererName(std::string sThisName) override;                          // Set renderer identification name

    float GetCharacterWidth(wchar_t character, float FontSize) override;        // Calculate character width for text layout
    float GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& fontName); // Calculate character width with specific font
    float CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth) override; // Calculate total text width
    float CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) override; // Calculate text height

    // Make render mutex accessible to other components that need OpenGL synchronization
    static std::mutex& GetRenderMutex() { return s_renderMutex; }               // Access rendering synchronization mutex

    // Mutexes & Atomics for thread safety
    std::mutex globalMutex;                                                     // Global operation mutex
    static std::mutex s_renderMutex;                                            // Static rendering synchronization mutex
    std::atomic<bool> wasResizing{ false };                                     // Atomic flag for resize state tracking
    std::atomic<bool> GLBusy{ false };                                          // Atomic flag for OpenGL operation state

private:
    bool bHasCleanedUp = false;                                                 // Flag to prevent double cleanup
    bool m_supportsEffects = true;                                              // Flag indicating effect support capability
    std::string sName;                                                          // Renderer instance name
    std::chrono::steady_clock::time_point lastTime;                             // Last frame timestamp for timing
    int frameCount = 0;                                                         // Frame counter for performance metrics
    int m_renderTargetWidth = DEFAULT_WINDOW_WIDTH;                             // Current render target width
    int m_renderTargetHeight = DEFAULT_WINDOW_HEIGHT;                           // Current render target height
    int delay = 0;                                                              // Loading animation delay counter
    int loadIndex = 0;                                                          // Loading animation frame index
    int iPosX = 0;                                                              // Temporary position X coordinate
    float fps = 0.0f;                                                           // Current frames per second
    uint32_t prevWindowedWidth = 0;                                             // Previous windowed mode width
    uint32_t prevWindowedHeight = 0;                                            // Previous windowed mode height

    // Thread Lock Names
    std::string renderFrameLockName = "opengl_renderer_frame_lock";             // Render frame synchronization lock name
    std::string GLLockName = "opengl_render_lock";                              // OpenGL operation synchronization lock name

    // Our private function and procedure definitions for this class.
    bool CreateOpenGLContext(HWND hwnd);                                        // Create platform-specific OpenGL context
    bool InitializeOpenGLExtensions();                                          // Initialize OpenGL extensions and function pointers
    void CreateFramebufferObjects();                                            // Create OpenGL framebuffer objects
    void SetupViewport();                                                       // Setup OpenGL viewport configuration
    void SetupRenderStates();                                                   // Setup OpenGL rendering pipeline states
    void LoadShaders();                                                          // Load and compile OpenGL shaders
    void UpdateUniformBuffers();                                                // Update OpenGL uniform buffer objects
    void CleanupTextures();                                                     // Cleanup all OpenGL texture resources
    Vector4 ConvertColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);          // Convert color format for OpenGL
    inline void ThrowError(const std::string& message);                         // Error handling and logging function

    // OpenGL-specific helper functions
    GLuint CompileShader(const std::string& source, GLenum shaderType);         // Compile individual OpenGL shader
    GLuint CreateShaderProgram(const std::string& vertexSource, const std::string& fragmentSource); // Create complete shader program
    bool CheckOpenGLError(const std::string& operation);                        // Check for OpenGL errors
    void SetupPlatformSpecificContext();                                        // Setup platform-specific OpenGL context
    void CleanupPlatformSpecificContext();                                      // Cleanup platform-specific OpenGL context

    // Our private OpenGL resource management
    std::atomic<bool> playing{ false };                                         // Atomic flag for playback state

    // Mutexes for thread safety
    static std::mutex s_loaderMutex;                                            // Static mutex for loader thread synchronization
};

// We must do this so that our renderers know of our global reference
// and for type casting to the desired interface we intend to use.
extern std::shared_ptr<Renderer> renderer;                                     // Global renderer interface pointer
// Other main base external references.
extern Debug debug;                                                             // Global debug logging system

extern std::atomic<bool> bResizeInProgress;                                    // Prevents multiple resize operations
extern std::atomic<bool> bFullScreenTransition;                                // Prevents handling during fullscreen transitions

#endif // #if defined(__USE_OPENGL__)