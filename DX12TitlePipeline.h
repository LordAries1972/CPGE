/*
/----------------------------------------------------------------------------------------------------------\
 DX12TitlePipeline.h - Native DirectX 12 pipeline for SCENE_GAMETITLE (and its GPU frame profiler)

 WHY THIS EXISTS
 ===============
 The title screen used to be assembled from Direct2D draws (via the DX11-on-12 interop layer) wrapped
 around the real 3D pass: an extra command-list close/execute, two interop Acquire/Release/Flush round
 trips and a full-screen D2D bitmap blit EVERY frame.  Everything below is now ordinary D3D12 recorded on
 the one open command list.  Direct2D is left with what it is genuinely good at: text and GUI widgets.

   Pass order on the single command list (back buffer -> RENDER_TARGET once, never bounced):
     BACKDROP  : background image (with the intro zoom) -> company logo -> 3D starfield -> fireworks
     3D MODELS : shadows / reflections / models (DX12RenderFrame.cpp)
     TSOO LOGO : centred logo with the fade-strobe alpha, drawn over the 3D models
     (D2D overlay for GUI / text is then recorded straight onto the same back buffer)

 CORRECTNESS NOTES (genuine DX12 requirements)
 =============================================
 * The particle instance buffer is a RING with one slice per frame in flight.  A single CPU-rewritten
   buffer races with the GPU still executing the previous 1-2 frames.
 * All state (root signature, heaps, PSO) is set once per pass, not once per draw.
 * Viewport, scissor and render targets are set explicitly - no reliance on leftover state.

 DX12GpuProfiler (debug builds)
 ==============================
 D3D12 timestamp queries bracket the passes so the F12 timing capture can show GPU time per pass.  The CPU
 timings alone cannot tell GPU-bound from CPU-bound: "present" simply absorbs whatever the GPU is late by.
\-----------------------------------------------------------------------------------------------------------/
*/

#pragma once

#include "Includes.h"

#if defined(__USE_DIRECTX_12__)

class DX12Renderer;

// -------------------------------------------------------------------------------------------------------------
// GPU timestamp profiler (debug builds only).  One query/readback slice per frame in flight.
// -------------------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
class DX12GpuProfiler {
public:
    // Stamps are written in this order every profiled frame (skipped passes are filled in automatically,
    // so a pass that did not run reads as 0 ms).
    enum Stamp : UINT {
        STAMP_FRAME_START = 0,
        STAMP_BACKDROP,                                                         // after the native backdrop (bg, logo, stars, fireworks)
        STAMP_SHADOWS,                                                          // after planar planning + reflection probe + shadow depth passes
        STAMP_REFLECTIONS,                                                      // after the planar mirror pass + live capture face
        STAMP_MODELS,                                                           // after the main model loop
        STAMP_TSOO,                                                             // after the TSOO logo
        STAMP_FRAME_END,                                                        // after the D2D overlay (written by the closer list)
        STAMP_COUNT
    };

    struct Result {
        double backdropMs    = 0.0;
        double shadowsMs     = 0.0;
        double reflectionsMs = 0.0;
        double modelsMs      = 0.0;
        double tsooMs        = 0.0;
        double overlayMs     = 0.0;                                             // includes the gap while D2D/11On12 work is submitted
        double totalMs       = 0.0;
    };

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, UINT frameCount);
    void Shutdown();

    void BeginFrame(UINT frameIndex, bool enabled);                             // Decide per frame whether stamps are written
    bool Active() const { return m_active; }
    void StampUpTo(ID3D12GraphicsCommandList* cl, Stamp stamp);                 // Writes every not-yet-written stamp up to and including 'stamp'
    void Resolve(ID3D12GraphicsCommandList* cl);                                // Writes FRAME_END and resolves into the readback buffer
    bool Collect(UINT frameIndex, Result& out);                                 // Read back this slice's PREVIOUS use (call after its fence)

private:
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_queryHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  m_readback;
    UINT64 m_frequency  = 0;
    UINT   m_frameCount = 0;
    UINT   m_frame      = 0;
    UINT   m_next       = 0;
    bool   m_active     = false;
    bool   m_valid[8]   = {};
};
#endif // _DEBUG

// -------------------------------------------------------------------------------------------------------------
// Native title-screen layer.
// -------------------------------------------------------------------------------------------------------------
class DX12TitlePipeline {
public:
    static constexpr UINT kSlotBackground  = 0;                                 // IMG_GAMEINTRO1
    static constexpr UINT kSlotCompanyLogo = 1;                                 // IMG_COMPANYLOGO
    static constexpr UINT kSlotTSOO        = 2;                                 // IMG_TSOO
    static constexpr UINT kSlotCount       = 3;                                 // Must match DX12_SPRITE2D_SRV_COUNT
    static constexpr UINT kFrames          = 3;                                 // Must match DX12Renderer::FrameCount
    static constexpr UINT kMaxParticles    = 1024;                              // Per frame: 72 stars + 6 rockets x 48 particles x 3 trail steps (864) + rocket trails, with headroom

    // Matches the HLSL ParticleInstance (32 bytes).
    struct ParticleInstance { float centerX, centerY, halfW, halfH, r, g, b, a; };

    bool Initialize(DX12Renderer* owner);                                       // Root signatures, PSOs, particle ring + SRV
    void Shutdown();
    bool LoadSlotTexture(UINT slot, const std::wstring& filename);              // WIC-decode straight to a native SRV (bypasses D2D)

    bool IsReady() const { return m_ready; }
    bool HasSlot(UINT slot) const { return slot < kSlotCount && m_tex[slot] != nullptr; }

    // Particle batch.  FXManager's DrawFXPixel() reaches these through the DX12Renderer forwarders.
    void BeginParticles(UINT frameIndex);
    void EndParticles() { m_batchActive = false; }
    bool IsBatchActive() const { return m_batchActive; }
    void QueueParticle(int x, int y, float pixelSize, const DirectX::XMFLOAT4& color);

    // Frame recording (render thread, command list open, back buffer in RENDER_TARGET).
    void RecordBackdrop(ID3D12GraphicsCommandList* cl, UINT frameIndex);        // bg + logo + stars + fireworks
    void RecordTSOO(ID3D12GraphicsCommandList* cl, UINT frameIndex);            // TSOO logo with strobe alpha

private:
    void BindTargets(ID3D12GraphicsCommandList* cl, UINT frameIndex) const;     // Viewport, scissor, RTV + DSV
    void BeginImages(ID3D12GraphicsCommandList* cl) const;                      // Root signature / heap / PSO for textured quads
    void DrawImage(ID3D12GraphicsCommandList* cl, UINT slot, float x, float y, float w, float h, float alpha) const;
    void FlushParticles(ID3D12GraphicsCommandList* cl, UINT frameIndex) const;  // One DrawInstanced for the whole batch

    bool CreateImagePipeline();
    bool CreateParticlePipeline();

    DX12Renderer* m_owner = nullptr;
    bool m_ready = false;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_imageRS;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_imagePSO;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_particleRS;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_particlePSO;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_tex[kSlotCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_retired[kSlotCount];               // Previous texture of a reloaded slot, kept alive one extra generation for in-flight frames
    D3D12_GPU_DESCRIPTOR_HANDLE            m_texSRV[kSlotCount] = {};
    UINT                                   m_texW[kSlotCount]   = {};
    UINT                                   m_texH[kSlotCount]   = {};

    Microsoft::WRL::ComPtr<ID3D12Resource> m_particleBuffer;                    // Upload heap, kFrames slices of kMaxParticles, persistently mapped
    ParticleInstance*                      m_particleMapped = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE            m_particleSRV    = {};
    bool                                   m_batchActive    = false;
    UINT                                   m_batchFrame     = 0;
    UINT                                   m_batchCount     = 0;
};

#endif // __USE_DIRECTX_12__
