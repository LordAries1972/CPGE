#pragma once

//-------------------------------------------------------------------------------------------------
// DX12Models.h — DirectX 12 Model System
//
// This header is the DX12-pipeline companion to Models.h.  The Model and Texture classes are
// declared in Models.h (which already handles DX12 via the combined DX11/DX12 guards) so this
// file simply includes it and adds the DX12Renderer includes needed by the DX12-specific
// implementations in DX12Models.cpp.
//
// DX12 Models use the DirectX 11-on-12 compatibility device so that the same DX11-style GPU
// resource API can be used for vertex/index/constant/texture buffers.  This is consistent with
// how DX12RenderFrame.cpp already renders models:
//   scene.scene_models[i].Render(m_dx11Dx12Compat.dx11Context.Get(), deltaTime);
//
// DO NOT INCLUDE THIS FILE DIRECTLY — it is automatically included through Models.h when
// __USE_DIRECTX_12__ is defined.
//-------------------------------------------------------------------------------------------------

#if defined(__USE_DIRECTX_12__)

#include "Models.h"
#include "DX12Renderer.h"

#endif // defined(__USE_DIRECTX_12__)
