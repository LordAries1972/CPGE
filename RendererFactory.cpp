#include "Includes.h"
#include "Renderer.h"

#include "Debug.h"

#if defined(__USE_DIRECTX_11__)
    #include "DX11Renderer.h"
#elif defined(__USE_DIRECTX_12__)
    #include "DX12Renderer.h"
#elif defined(__USE_OPENGL__)
    #include "OpenGLRenderer.h"
#elif defined(__USE_VULKAN__)
    #include "VulkanRenderer.h"
#endif

std::shared_ptr<Renderer> renderer;

int CreateRendererInstance()
{
#if defined(__USE_DIRECTX_11__)
    renderer = std::make_shared<DX11Renderer>();
#elif defined(__USE_DIRECTX_12__)
    renderer = std::make_shared<DX12Renderer>();
#elif defined(__USE_OPENGL__)
    renderer = std::make_shared<OpenGLRenderer>();
#elif defined(__USE_VULKAN__)
    renderer = std::make_shared<VulkanRenderer>();
#else
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"NO valid Rendering Engine has been selected in configuration.");
    return EXIT_FAILURE;
#endif

    if (!renderer)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Renderer creation failed.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
