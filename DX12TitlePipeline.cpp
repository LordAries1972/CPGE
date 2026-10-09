/*
/----------------------------------------------------------------------------------------------------------\
 DX12TitlePipeline.cpp - Native DirectX 12 pipeline for SCENE_GAMETITLE (see DX12TitlePipeline.h)
\-----------------------------------------------------------------------------------------------------------/
*/

#include "Includes.h"

#if defined(__USE_DIRECTX_12__)

#include "DX12TitlePipeline.h"
#include "DX12Renderer.h"
#include "Debug.h"
#include "FXManager.h"

extern Debug debug;
extern FXManager fxManager;

using Microsoft::WRL::ComPtr;

static_assert(DX12TitlePipeline::kFrames == DX12Renderer::FrameCount, "DX12TitlePipeline::kFrames must equal DX12Renderer::FrameCount");
static_assert(DX12TitlePipeline::kSlotCount == DX12_SPRITE2D_SRV_COUNT, "DX12TitlePipeline::kSlotCount must equal DX12_SPRITE2D_SRV_COUNT");

// =============================================================================================================
// DX12GpuProfiler
// =============================================================================================================
#if defined(_DEBUG)

bool DX12GpuProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, UINT frameCount)
{
    Shutdown();
    if (!device || !queue || frameCount == 0 || frameCount > 8)
        return false;

    if (FAILED(queue->GetTimestampFrequency(&m_frequency)) || m_frequency == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12GpuProfiler: timestamp frequency unavailable - GPU timing disabled.");
        return false;
    }

    D3D12_QUERY_HEAP_DESC qh = {};
    qh.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = frameCount * STAMP_COUNT;
    if (FAILED(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&m_queryHeap))))
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12GpuProfiler: CreateQueryHeap failed - GPU timing disabled.");
        return false;
    }

    CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
    CD3DX12_RESOURCE_DESC   bufDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(frameCount) * STAMP_COUNT * sizeof(UINT64));
    if (FAILED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readback))))
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12GpuProfiler: readback buffer creation failed - GPU timing disabled.");
        m_queryHeap.Reset();
        return false;
    }
    m_readback->SetName(L"DX12GpuProfilerReadback");

    m_frameCount = frameCount;
    for (bool& v : m_valid) v = false;
    return true;
}

void DX12GpuProfiler::Shutdown()
{
    m_queryHeap.Reset();
    m_readback.Reset();
    m_frequency  = 0;
    m_frameCount = 0;
    m_active     = false;
    for (bool& v : m_valid) v = false;
}

void DX12GpuProfiler::BeginFrame(UINT frameIndex, bool enabled)
{
    m_active = enabled && m_queryHeap && m_readback && frameIndex < m_frameCount;
    m_frame  = frameIndex;
    m_next   = 0;
}

void DX12GpuProfiler::StampUpTo(ID3D12GraphicsCommandList* cl, Stamp stamp)
{
    if (!m_active || !cl) return;
    while (m_next <= static_cast<UINT>(stamp) && m_next < STAMP_COUNT)
    {
        cl->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_frame * STAMP_COUNT + m_next);
        ++m_next;
    }
}

void DX12GpuProfiler::Resolve(ID3D12GraphicsCommandList* cl)
{
    if (!m_active || !cl) return;
    StampUpTo(cl, STAMP_FRAME_END);
    cl->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        m_frame * STAMP_COUNT, STAMP_COUNT,
        m_readback.Get(), static_cast<UINT64>(m_frame) * STAMP_COUNT * sizeof(UINT64));
    m_valid[m_frame] = true;
    m_active = false;
}

bool DX12GpuProfiler::Collect(UINT frameIndex, Result& out)
{
    if (!m_readback || frameIndex >= m_frameCount || !m_valid[frameIndex])
        return false;
    m_valid[frameIndex] = false;

    const UINT64 offset = static_cast<UINT64>(frameIndex) * STAMP_COUNT * sizeof(UINT64);
    D3D12_RANGE readRange = { static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + STAMP_COUNT * sizeof(UINT64)) };
    UINT64* data = nullptr;
    if (FAILED(m_readback->Map(0, &readRange, reinterpret_cast<void**>(&data))) || !data)
        return false;

    UINT64 t[STAMP_COUNT];
    memcpy(t, reinterpret_cast<const uint8_t*>(data) + offset, sizeof(t));
    D3D12_RANGE noWrite = { 0, 0 };
    m_readback->Unmap(0, &noWrite);

    const double toMs = 1000.0 / static_cast<double>(m_frequency);
    auto span = [&](Stamp a, Stamp b) -> double {
        return (t[b] >= t[a]) ? static_cast<double>(t[b] - t[a]) * toMs : 0.0;
    };

    out.backdropMs    = span(STAMP_FRAME_START, STAMP_BACKDROP);
    out.shadowsMs     = span(STAMP_BACKDROP,    STAMP_SHADOWS);
    out.reflectionsMs = span(STAMP_SHADOWS,     STAMP_REFLECTIONS);
    out.modelsMs      = span(STAMP_REFLECTIONS, STAMP_MODELS);
    out.tsooMs        = span(STAMP_MODELS,      STAMP_TSOO);
    out.overlayMs     = span(STAMP_TSOO,        STAMP_FRAME_END);
    out.totalMs       = span(STAMP_FRAME_START, STAMP_FRAME_END);
    return true;
}

#endif // _DEBUG

// =============================================================================================================
// DX12TitlePipeline - creation
// =============================================================================================================
bool DX12TitlePipeline::Initialize(DX12Renderer* owner)
{
    Shutdown();
    m_owner = owner;
    if (!m_owner || !m_owner->m_d3d12Device || !m_owner->m_cbvSrvUavHeap.heap)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: renderer not ready for pipeline creation.");
        return false;
    }

    if (!CreateImagePipeline() || !CreateParticlePipeline())
    {
        Shutdown();
        return false;
    }

    m_ready = true;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DX12TitlePipeline: native title pipeline created successfully.");
    return true;
}

void DX12TitlePipeline::Shutdown()
{
    m_ready = false;
    m_batchActive = false;
    m_batchCount  = 0;
    if (m_particleBuffer && m_particleMapped)
        m_particleBuffer->Unmap(0, nullptr);
    m_particleMapped = nullptr;
    m_particleBuffer.Reset();
    m_particlePSO.Reset();
    m_particleRS.Reset();
    m_imagePSO.Reset();
    m_imageRS.Reset();
    for (UINT i = 0; i < kSlotCount; ++i)
    {
        m_tex[i].Reset();
        m_retired[i].Reset();
        m_texSRV[i] = {};
        m_texW[i] = m_texH[i] = 0;
    }
}

bool DX12TitlePipeline::CreateImagePipeline()
{
    ID3D12Device* device = m_owner->m_d3d12Device.Get();
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0;
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace  = 0;
    rootParams[0].Constants.Num32BitValues = 8;                                 // {left, topNDC, right, bottomNDC, alpha, pad, pad, pad}
    rootParams[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    rootParams[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister   = 0;
    sampler.RegisterSpace    = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized, rsError;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &rsError);
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: image root signature serialize failed (0x%08X)", hr);
        return false;
    }
    hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_imageRS));
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: image root signature creation failed (0x%08X)", hr);
        return false;
    }
    m_imageRS->SetName(L"TitleImageRS");

    // VS: quad from SV_VertexID, corners lerped between the root-constant NDC rect.
    static const char vsSource[] =
        "cbuffer RectCB:register(b0){float4 rectNDC;float4 alphaPad;};"
        "struct VSOut{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};"
        "VSOut main(uint id:SV_VertexID){"
        "VSOut o;"
        "o.uv=float2((id&1u)?1.0f:0.0f,(id&2u)?0.0f:1.0f);"
        "float x=lerp(rectNDC.x,rectNDC.z,o.uv.x);"
        "float y=lerp(rectNDC.y,rectNDC.w,o.uv.y);"
        "o.pos=float4(x,y,0.0f,1.0f);"
        "return o;}";

    // PS: premultiplied-alpha texture (WIC PBGRA decode), scaled uniformly by the root-constant alpha.
    static const char psSource[] =
        "cbuffer RectCB:register(b0){float4 rectNDC;float4 alphaPad;};"
        "struct VSOut{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;};"
        "Texture2D<float4> t:register(t0);SamplerState s:register(s0);"
        "float4 main(VSOut i):SV_TARGET{return t.Sample(s,i.uv)*alphaPad.x;}";

    ComPtr<ID3DBlob> vsBlob, psBlob, compErr;
    hr = D3DCompile(vsSource, sizeof(vsSource) - 1, "TitleImageVS", nullptr, nullptr, "main", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &compErr);
    if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: image VS compile failed."); return false; }
    hr = D3DCompile(psSource, sizeof(psSource) - 1, "TitleImagePS", nullptr, nullptr, "main", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &compErr);
    if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: image PS compile failed."); return false; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_imageRS.Get();
    pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // Premultiplied alpha: src=ONE, dst=INV_SRC_ALPHA.
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.SampleMask               = UINT_MAX;
    pso.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.DepthStencilState                = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable    = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.StencilEnable  = FALSE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    pso.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count      = m_owner->GetMsaaSampleCount();                 // drawn into the main (possibly multisampled) target

    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_imagePSO));
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: image PSO creation failed (0x%08X)", hr);
        return false;
    }
    m_imagePSO->SetName(L"TitleImagePSO");
    return true;
}

bool DX12TitlePipeline::CreateParticlePipeline()
{
    ID3D12Device* device = m_owner->m_d3d12Device.Get();
    HRESULT hr;

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0;
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    // [0] = first element of this frame's slice in the ring, [1] = the instance StructuredBuffer.
    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace  = 0;
    rootParams[0].Constants.Num32BitValues = 1;
    rootParams[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    rootParams[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters   = rootParams;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    ComPtr<ID3DBlob> serialized, rsError;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &rsError);
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle root signature serialize failed (0x%08X)", hr);
        return false;
    }
    hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_particleRS));
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle root signature creation failed (0x%08X)", hr);
        return false;
    }
    m_particleRS->SetName(L"TitleParticleRS");

    // VS: one instance per SV_InstanceID (SV_InstanceID excludes StartInstanceLocation, so the
    // ring slice is selected with the baseIndex root constant), quad expanded from SV_VertexID.
    static const char vsSource[] =
        "cbuffer BaseCB:register(b0){uint baseIndex;};"
        "struct ParticleInstance{float2 centerNDC;float2 halfSize;float4 color;};"
        "StructuredBuffer<ParticleInstance> g_particles:register(t0);"
        "struct VSOut{float4 pos:SV_POSITION;float4 color:COLOR0;};"
        "VSOut main(uint vid:SV_VertexID,uint iid:SV_InstanceID){"
        "VSOut o;"
        "ParticleInstance p=g_particles[baseIndex+iid];"
        "float2 corner=float2((vid&1u)?1.0f:-1.0f,(vid&2u)?-1.0f:1.0f);"
        "o.pos=float4(p.centerNDC+corner*p.halfSize,0.0f,1.0f);"
        "o.color=p.color;"
        "return o;}";

    static const char psSource[] =
        "struct VSOut{float4 pos:SV_POSITION;float4 color:COLOR0;};"
        "float4 main(VSOut i):SV_TARGET{return i.color;}";

    ComPtr<ID3DBlob> vsBlob, psBlob, compErr;
    hr = D3DCompile(vsSource, sizeof(vsSource) - 1, "TitleParticleVS", nullptr, nullptr, "main", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &compErr);
    if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle VS compile failed."); return false; }
    hr = D3DCompile(psSource, sizeof(psSource) - 1, "TitleParticlePS", nullptr, nullptr, "main", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &compErr);
    if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle PS compile failed."); return false; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_particleRS.Get();
    pso.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    pso.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // Straight (non-premultiplied) alpha: matches the D2D FillRectangle look these particles replace.
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
    pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
    pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    pso.SampleMask               = UINT_MAX;
    pso.RasterizerState          = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.DepthStencilState                = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable    = FALSE;
    pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.StencilEnable  = FALSE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets      = 1;
    pso.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count      = m_owner->GetMsaaSampleCount();                 // drawn into the main (possibly multisampled) target

    hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_particlePSO));
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle PSO creation failed (0x%08X)", hr);
        return false;
    }
    m_particlePSO->SetName(L"TitleParticlePSO");

    // Particle ring: kFrames slices of kMaxParticles, persistently mapped upload heap.  The CPU only ever
    // writes the slice of the frame it is recording, which the frame fence has proven the GPU has finished with.
    const UINT64 totalElems = static_cast<UINT64>(kFrames) * kMaxParticles;
    const UINT64 bufSize    = totalElems * sizeof(ParticleInstance);
    CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC   bufDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
    hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_particleBuffer));
    if (FAILED(hr))
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle ring creation failed (0x%08X)", hr);
        return false;
    }
    m_particleBuffer->SetName(L"TitleParticleRing");

    CD3DX12_RANGE noRead(0, 0);
    hr = m_particleBuffer->Map(0, &noRead, reinterpret_cast<void**>(&m_particleMapped));
    if (FAILED(hr) || !m_particleMapped)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: particle ring map failed (0x%08X)", hr);
        m_particleMapped = nullptr;
        return false;
    }
    memset(m_particleMapped, 0, static_cast<size_t>(bufSize));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                  = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement        = 0;
    srvDesc.Buffer.NumElements         = static_cast<UINT>(totalElems);
    srvDesc.Buffer.StructureByteStride = sizeof(ParticleInstance);
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

    DX12DescriptorHeap& heap = m_owner->m_cbvSrvUavHeap;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(heap.cpuStart, DX12_SPRITE2D_PARTICLE_SRV_BASE, heap.handleIncrementSize);
    device->CreateShaderResourceView(m_particleBuffer.Get(), &srvDesc, cpu);
    m_particleSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(heap.gpuStart, DX12_SPRITE2D_PARTICLE_SRV_BASE, heap.handleIncrementSize);
    return true;
}

// =============================================================================================================
// DX12TitlePipeline - texture loading (WIC straight to a native SRV, bypassing Direct2D)
// =============================================================================================================
bool DX12TitlePipeline::LoadSlotTexture(UINT slot, const std::wstring& filename)
{
    if (slot >= kSlotCount || !m_owner || !m_owner->m_d3d12Device || !m_owner->m_commandQueue)
        return false;

    try {
        ID3D12Device* device = m_owner->m_d3d12Device.Get();

        ComPtr<IWICImagingFactory> wicFactory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) return false;

        ComPtr<IWICBitmapDecoder> decoder;
        hr = wicFactory->CreateDecoderFromFilename(filename.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr))
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: WIC decoder failed for file: %s", filename.c_str());
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = wicFactory->CreateFormatConverter(&converter);
        if (FAILED(hr)) return false;

        // Premultiplied BGRA - matches the D2D bitmap load path and the image PSO's blend state.
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return false;

        UINT texW = 0, texH = 0;
        converter->GetSize(&texW, &texH);
        if (texW == 0 || texH == 0) return false;

        const UINT srcRowPitch = texW * 4;
        std::vector<BYTE> pixels(static_cast<size_t>(srcRowPitch) * texH);
        hr = converter->CopyPixels(nullptr, srcRowPitch, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return false;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = texW;
        texDesc.Height           = texH;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> newTex;
        CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
        hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&newTex));
        if (FAILED(hr))
        {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"DX12TitlePipeline: texture resource creation failed (0x%08X)", hr);
            return false;
        }
        std::wstring resName = L"TitleTexture_" + std::to_wstring(slot);
        newTex->SetName(resName.c_str());

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = {};
        UINT   numRows = 0;
        UINT64 rowSizeInBytes = 0, uploadBufferSize = 0;
        device->GetCopyableFootprints(&texDesc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &uploadBufferSize);

        ComPtr<ID3D12Resource> uploadBuffer;
        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC   uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
        hr = device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
        if (FAILED(hr)) return false;

        void* mappedData = nullptr;
        CD3DX12_RANGE readRange(0, 0);
        hr = uploadBuffer->Map(0, &readRange, &mappedData);
        if (FAILED(hr)) return false;

        BYTE* dstData = static_cast<BYTE*>(mappedData) + layout.Offset;
        const UINT dstRowPitch = static_cast<UINT>(layout.Footprint.RowPitch);
        const UINT copyBytes   = std::min(srcRowPitch, static_cast<UINT>(rowSizeInBytes));
        for (UINT row = 0; row < texH; ++row)
            memcpy(dstData + static_cast<size_t>(row) * dstRowPitch, pixels.data() + static_cast<size_t>(row) * srcRowPitch, copyBytes);
        uploadBuffer->Unmap(0, nullptr);

        // One-shot upload on a private allocator/list/fence so this is safe from the loader thread
        // while the render thread owns the frame command list.
        ComPtr<ID3D12CommandAllocator> uploadAllocator;
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAllocator));
        if (FAILED(hr)) return false;

        ComPtr<ID3D12GraphicsCommandList> uploadList;
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadList));
        if (FAILED(hr)) return false;

        CD3DX12_TEXTURE_COPY_LOCATION dstLocation(newTex.Get(), 0);
        CD3DX12_TEXTURE_COPY_LOCATION srcLocation(uploadBuffer.Get(), layout);
        uploadList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

        CD3DX12_RESOURCE_BARRIER srBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            newTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        uploadList->ResourceBarrier(1, &srBarrier);

        hr = uploadList->Close();
        if (FAILED(hr)) return false;

        ID3D12CommandList* lists[] = { uploadList.Get() };
        m_owner->m_commandQueue->ExecuteCommandLists(1, lists);

        {
            ComPtr<ID3D12Fence> uploadFence;
            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));
            if (FAILED(hr)) return false;
            HANDLE uploadEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!uploadEvent) return false;
            m_owner->m_commandQueue->Signal(uploadFence.Get(), 1);
            uploadFence->SetEventOnCompletion(1, uploadEvent);
            const DWORD waitResult = WaitForSingleObject(uploadEvent, 5000);
            CloseHandle(uploadEvent);
            if (waitResult != WAIT_OBJECT_0)
            {
                // The copy may still be in flight; do not publish a texture the GPU has not finished writing.
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"DX12TitlePipeline: texture upload timed out.");
                return false;
            }
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;

        DX12DescriptorHeap& heap = m_owner->m_cbvSrvUavHeap;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(heap.cpuStart, DX12_SPRITE2D_SRV_BASE + slot, heap.handleIncrementSize);
        device->CreateShaderResourceView(newTex.Get(), &srvDesc, cpu);

        m_texSRV[slot] = CD3DX12_GPU_DESCRIPTOR_HANDLE(heap.gpuStart, DX12_SPRITE2D_SRV_BASE + slot, heap.handleIncrementSize);
        m_texW[slot]   = texW;
        m_texH[slot]   = texH;
        m_retired[slot] = m_tex[slot];                                          // A reload may race frames still sampling the old texture: retire, do not free
        m_tex[slot]    = newTex;                                                // Published last: HasSlot() only turns true once everything above is done

        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DX12TitlePipeline: texture slot %u loaded. Size: %ux%u", slot, texW, texH);
        return true;
    }
    catch (const std::exception& e) {
        std::wstring errorMsg = std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"DX12TitlePipeline: exception in LoadSlotTexture: %s", errorMsg.c_str());
        return false;
    }
}

// =============================================================================================================
// DX12TitlePipeline - particle batch
// =============================================================================================================
void DX12TitlePipeline::BeginParticles(UINT frameIndex)
{
    m_batchFrame  = (frameIndex < kFrames) ? frameIndex : 0;
    m_batchCount  = 0;
    m_batchActive = m_ready;
}

void DX12TitlePipeline::QueueParticle(int x, int y, float pixelSize, const DirectX::XMFLOAT4& color)
{
    if (!m_particleMapped || !m_owner || m_owner->iOrigWidth <= 0 || m_owner->iOrigHeight <= 0)
        return;
    if (m_batchCount >= kMaxParticles)
        return;

    const float invW   = 1.0f / static_cast<float>(m_owner->iOrigWidth);
    const float invH   = 1.0f / static_cast<float>(m_owner->iOrigHeight);
    const float halfPx = pixelSize * 0.5f;
    const float cx     = static_cast<float>(x) + halfPx;
    const float cy     = static_cast<float>(y) + halfPx;

    ParticleInstance& inst = m_particleMapped[static_cast<size_t>(m_batchFrame) * kMaxParticles + m_batchCount++];
    inst.centerX = cx * invW * 2.0f - 1.0f;
    inst.centerY = 1.0f - cy * invH * 2.0f;
    inst.halfW   = halfPx * invW * 2.0f;
    inst.halfH   = halfPx * invH * 2.0f;
    inst.r = color.x; inst.g = color.y; inst.b = color.z; inst.a = color.w;
}

// =============================================================================================================
// DX12TitlePipeline - recording
// =============================================================================================================
void DX12TitlePipeline::BindTargets(ID3D12GraphicsCommandList* cl, UINT frameIndex) const
{
    const float w = static_cast<float>(m_owner->iOrigWidth);
    const float h = static_cast<float>(m_owner->iOrigHeight);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, w, h, 0.0f, 1.0f };
    D3D12_RECT     sr = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sr);

    // Colour only: 2D layers need no depth, and the PSOs declare DSVFormat UNKNOWN.
    // The main target: the multisampled colour target while MSAA is on (resolved later), else the back buffer.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_owner->MainRTV(frameIndex);
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}

void DX12TitlePipeline::BeginImages(ID3D12GraphicsCommandList* cl) const
{
    cl->SetGraphicsRootSignature(m_imageRS.Get());
    ID3D12DescriptorHeap* heaps[] = { m_owner->m_cbvSrvUavHeap.heap.Get() };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetPipelineState(m_imagePSO.Get());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cl->IASetVertexBuffers(0, 0, nullptr);
    cl->IASetIndexBuffer(nullptr);
}

void DX12TitlePipeline::DrawImage(ID3D12GraphicsCommandList* cl, UINT slot, float x, float y, float w, float h, float alpha) const
{
    if (slot >= kSlotCount || !m_tex[slot])
        return;

    const float invW = 1.0f / static_cast<float>(m_owner->iOrigWidth);
    const float invH = 1.0f / static_cast<float>(m_owner->iOrigHeight);
    const float rootConsts[8] = {
        x       * invW * 2.0f - 1.0f,                                           // left
        1.0f - y       * invH * 2.0f,                                           // top
        (x + w) * invW * 2.0f - 1.0f,                                           // right
        1.0f - (y + h) * invH * 2.0f,                                           // bottom
        alpha, 0.0f, 0.0f, 0.0f };

    cl->SetGraphicsRoot32BitConstants(0, 8, rootConsts, 0);
    cl->SetGraphicsRootDescriptorTable(1, m_texSRV[slot]);
    cl->DrawInstanced(4, 1, 0, 0);
}

void DX12TitlePipeline::FlushParticles(ID3D12GraphicsCommandList* cl, UINT frameIndex) const
{
    if (m_batchCount == 0)
        return;

    const UINT baseIndex = (frameIndex < kFrames ? frameIndex : 0) * kMaxParticles;
    cl->SetGraphicsRootSignature(m_particleRS.Get());
    ID3D12DescriptorHeap* heaps[] = { m_owner->m_cbvSrvUavHeap.heap.Get() };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetPipelineState(m_particlePSO.Get());
    cl->SetGraphicsRoot32BitConstants(0, 1, &baseIndex, 0);
    cl->SetGraphicsRootDescriptorTable(1, m_particleSRV);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cl->IASetVertexBuffers(0, 0, nullptr);
    cl->IASetIndexBuffer(nullptr);
    cl->DrawInstanced(4, m_batchCount, 0, 0);
}

// The intro zoom is a centre-crop of the source (Blit2DCenteredZoom).  That is identical to drawing the
// whole texture into a rect enlarged by 1/(1-z) about the destination centre; the overhang is clipped by the
// viewport, so no UV change is required.
// Computed in floats on purpose: the zoom is slow, and snapping the rect to whole pixels makes the image
// step 1px at a time (and the truncating /2 shifts it half a pixel back and forth) - visible as jerking.
static void ZoomedRect(float zoom, float x, float y, float w, float h, float& ox, float& oy, float& ow, float& oh)
{
    const float z     = std::clamp(zoom, 0.0f, 0.75f);
    const float scale = 1.0f / (1.0f - z);
    ow = w * scale;
    oh = h * scale;
    ox = x - (ow - w) * 0.5f;
    oy = y - (oh - h) * 0.5f;
}

void DX12TitlePipeline::RecordBackdrop(ID3D12GraphicsCommandList* cl, UINT frameIndex)
{
    if (!m_ready || !cl) return;

    const int W = m_owner->iOrigWidth;
    const int H = m_owner->iOrigHeight;

    BindTargets(cl, frameIndex);
    BeginImages(cl);

    // 1) Full-screen title background, with the looping intro zoom.
    if (m_tex[kSlotBackground])
    {
        float bx, by, bw, bh;
        ZoomedRect(fxManager.GetImageZoomLevel(int(BlitObj2DIndexType::IMG_GAMEINTRO1)), 0.0f, 0.0f,
            static_cast<float>(W), static_cast<float>(H), bx, by, bw, bh);
        DrawImage(cl, kSlotBackground, bx, by, bw, bh, 1.0f);
    }

    // 2) Company logo, bottom-left at half size.
    if (m_tex[kSlotCompanyLogo])
    {
        const float logoW = static_cast<float>(m_texW[kSlotCompanyLogo]) * 0.5f;
        const float logoH = static_cast<float>(m_texH[kSlotCompanyLogo]) * 0.5f;
        float lx, ly, lw, lh;
        ZoomedRect(fxManager.GetImageZoomLevel(int(BlitObj2DIndexType::IMG_COMPANYLOGO)), 0.0f,
            static_cast<float>(H) - logoH, logoW, logoH, lx, ly, lw, lh);
        DrawImage(cl, kSlotCompanyLogo, lx, ly, lw, lh, 1.0f);
    }

    // 3) Starfield + fireworks: FXManager queues instances into this frame's ring slice through
    //    DrawFXPixel(); one instanced draw then renders every particle.
    BeginParticles(frameIndex);
    try { fxManager.Render(true); }
    catch (const std::exception&) {}
    try { fxManager.RenderFireworks(); }
    catch (const std::exception&) {}
    EndParticles();
    FlushParticles(cl, frameIndex);
}

void DX12TitlePipeline::RecordTSOO(ID3D12GraphicsCommandList* cl, UINT frameIndex)
{
    if (!m_ready || !cl || !m_tex[kSlotTSOO]) return;

    const int startX = (m_owner->iOrigWidth  - 536) / 2;                        // Centred horizontally
    const int startY = (m_owner->iOrigHeight - 466) / 2;                        // Centred vertically
    const float alpha = fxManager.IsImageFadeStrobeActive(BlitObj2DIndexType::IMG_TSOO)
        ? fxManager.GetImageFadeStrobeAlpha(BlitObj2DIndexType::IMG_TSOO)
        : 1.0f;

    BindTargets(cl, frameIndex);
    BeginImages(cl);
    DrawImage(cl, kSlotTSOO, startX, startY, 536, 466, alpha);
}

#endif // __USE_DIRECTX_12__
