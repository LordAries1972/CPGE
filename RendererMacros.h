// -----------------------------------------------------------------------------
// INLINE FUNCTION: WithDX11Renderer
//
// Executes the given lambda/function only if the current renderer is a valid
// DX11Renderer and DX12Renderer with an initialized D3D device.
//
// Usage example:
// WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11) {
//     dx11->m_d3dDevice->CreateVertexShader(...);
// });
// 
// If it is only one line of code then your better off doing this instead
// example:
// std::shared_ptr<DX11Renderer> dx11;
// -----------------------------------------------------------------------------
#pragma once

//#include "DX11Renderer.h"
#include <memory>
#include <functional>                           // Required for std::function
#include "Renderer.h"
#include "DX11Renderer.h"
//#include "DX12Renderer.h"

// DirectX 11 MACROS
#if defined(__USE_DIRECTX_11__)
// Forward declare DX11Renderer to break circular dependency
class DX11Renderer;

template <typename TResult>
inline TResult WithDX11RendererRet(const std::function<TResult(std::shared_ptr<DX11Renderer>)>& action)
{
    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
    if (dx11 && dx11->m_d3dDevice)
        return action(dx11);

    return TResult(); // or handle fallback
}

inline void WithDX11Renderer(const std::function<void(std::shared_ptr<DX11Renderer>)>& action)
{
    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
    if (dx11 && dx11->m_d3dDevice)
    {
        action(dx11);
    }
}

#endif

// DirectX 12 MACROS
#if defined(__USE_DIRECTX_12__)

template <typename TResult>
inline TResult WithDX12RendererRet(const std::function<TResult(std::shared_ptr<DX12Renderer>)>& action)
{
    auto dx12 = std::dynamic_pointer_cast<DX12Renderer>(renderer);
    if (dx12 && dx12->m_d3dDevice)
        return action(dx12);

    return TResult(); // or handle fallback
}

inline void WithDX12Renderer(const std::function<void(std::shared_ptr<DX12Renderer>)>& action)
{
    auto dx12 = std::dynamic_pointer_cast<DX12Renderer>(renderer);
    if (dx12 && dx12->m_d3dDevice)
    {
        action(dx11);
    }
}


#endif