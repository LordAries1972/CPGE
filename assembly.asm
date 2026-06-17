; ============================================================
; assembly.asm -- x86/64 (MASM) optimisation routines for CPGE
; Assembled with ml64.exe (Microsoft Macro Assembler for x64)
;
; All routines follow the Windows x64 calling convention:
;   RCX, RDX, R8, R9  -- first four integer/pointer arguments
;   XMM0-XMM3         -- first four floating-point arguments
;   RAX                -- integer/pointer return value
;   RSI, RDI, RBX, RBP, R12-R15 -- non-volatile (callee-saved)
;   RCX, RDX, R8, R9, R10, R11  -- volatile (may be trashed by callee)
;
; Shadow space (32 bytes) is provided by the caller per ABI,
; but these leaf-style routines do not need to use it.
; ============================================================

.code

; ────────────────────────────────────────────────────────────
; MemoryCopy(Source:rcx, Destination:rdx, Length:r8)
;
;   Fast memory copy using REP MOVSQ (quad-word moves, 8 bytes
;   per clock on modern Intel/AMD) followed by REP MOVSB for
;   the 0-7 trailing bytes.  Throughput approaches hardware
;   memory bandwidth and is significantly faster than a naive
;   byte loop for buffers of any size.
;
;   NOTE: Source and Destination must NOT overlap.
;         For overlapping regions use memmove semantics instead.
;
; Parameters (Windows x64):
;   RCX -- const void* Source      (source buffer)
;   RDX -- void*       Destination (destination buffer)
;   R8  -- size_t      Length      (byte count)
; ────────────────────────────────────────────────────────────
MemoryCopy PROC
    push    rsi                 ; save non-volatile registers (callee-saved)
    push    rdi

    mov     rsi, rcx            ; rsi = source pointer
    mov     rdi, rdx            ; rdi = destination pointer
    mov     rcx, r8             ; rcx = total byte count

    ; ── Bulk copy: 8 bytes per REP iteration (quad-word) ────
    mov     rax, rcx            ; rax = total length (saved for remainder)
    shr     rcx, 3              ; rcx = qword count  (length / 8)
    rep     movsq               ; copy 8 bytes per iteration; rsi/rdi advance

    ; ── Trailing bytes: 0-7 bytes remaining ─────────────────
    mov     rcx, rax            ; restore original length
    and     rcx, 7              ; rcx = length mod 8  (0-7 remaining bytes)
    rep     movsb               ; copy 1 byte per iteration

    pop     rdi                 ; restore non-volatile registers
    pop     rsi
    ret
MemoryCopy ENDP

; ────────────────────────────────────────────────────────────
; MemoryZero(Destination:rcx, Length:rdx)
;
;   Fast memory zeroing using REP STOSQ (8 zero bytes per clock
;   on modern Intel/AMD) followed by REP STOSB for the 0-7 trailing
;   bytes.  Equivalent to memset(dst, 0, n) but bypasses the CRT
;   and operates at hardware memory-bandwidth throughput.
;
;   Use this wherever a GPU-mapped region or host buffer must be
;   cleared before uploading: GlobalLightBuffer zero-pass, constant
;   buffer initialisation, staging buffer resets, etc.
;
; Parameters (Windows x64):
;   RCX -- void*  Destination (buffer to zero)
;   RDX -- size_t Length      (byte count)
; ────────────────────────────────────────────────────────────
MemoryZero PROC
    push    rdi                 ; save non-volatile register

    mov     rdi, rcx            ; rdi = destination pointer
    xor     rax, rax            ; rax = 0  (fill value for STOSQ / STOSB)
    mov     rcx, rdx            ; rcx = total byte count

    ; ── Bulk zero: 8 bytes per REP iteration (quad-word) ────
    mov     r9, rcx             ; r9  = total length (saved for remainder)
    shr     rcx, 3              ; rcx = qword count  (length / 8)
    rep     stosq               ; zero 8 bytes per iteration; rdi advances

    ; ── Trailing bytes: 0-7 bytes remaining ─────────────────
    mov     rcx, r9             ; restore original length
    and     rcx, 7              ; rcx = length mod 8  (0-7 remaining)
    rep     stosb               ; zero 1 byte per iteration

    pop     rdi                 ; restore non-volatile register
    ret
MemoryZero ENDP

; ────────────────────────────────────────────────────────────
; MatrixCopy4x4F(Source:rcx, Destination:rdx)
;
;   Optimised fixed-size 64-byte copy for 4×4 float matrices
;   (16 floats × 4 bytes = 64 bytes).  Uses four 128-bit SSE
;   MOVUPS loads followed by four MOVUPS stores — avoids the
;   loop/division overhead of the general MemoryCopy for this
;   extremely common fixed size.
;
;   XMM0-XMM3 are volatile (caller-saved) in the Windows x64 ABI
;   so no push/pop is required — this is a pure leaf function.
;
;   Works correctly on unaligned addresses (MOVUPS vs. MOVAPS).
;   When Source is 16-byte aligned, CPU micro-ops are identical
;   to MOVAPS; no penalty on modern Intel/AMD micro-architectures.
;
; Parameters (Windows x64):
;   RCX -- const float* Source      (64-byte source matrix)
;   RDX -- float*       Destination (64-byte destination matrix)
; ────────────────────────────────────────────────────────────
MatrixCopy4x4F PROC
    ; ── Load all four rows (16 bytes each) ──────────────────
    movups  xmm0, xmmword ptr [rcx]         ; row 0 (bytes  0-15)
    movups  xmm1, xmmword ptr [rcx + 16]    ; row 1 (bytes 16-31)
    movups  xmm2, xmmword ptr [rcx + 32]    ; row 2 (bytes 32-47)
    movups  xmm3, xmmword ptr [rcx + 48]    ; row 3 (bytes 48-63)

    ; ── Store all four rows ──────────────────────────────────
    movups  xmmword ptr [rdx],      xmm0    ; row 0
    movups  xmmword ptr [rdx + 16], xmm1    ; row 1
    movups  xmmword ptr [rdx + 32], xmm2    ; row 2
    movups  xmmword ptr [rdx + 48], xmm3    ; row 3

    ret
MatrixCopy4x4F ENDP

; ============================================================
; CONDITIONAL ASSEMBLY — OS and Renderer Guards
; ============================================================
;
; MASM supports conditional assembly via IFDEF / IFNDEF.
; To activate a guarded section, pass the define to ml64.exe:
;
;   CMakeLists.txt (inside the MSVC / ASM_MASM block):
;     set_source_files_properties(assembly.asm PROPERTIES
;         COMPILE_OPTIONS
;             "/DPLATFORM_WINDOWS;/D__USE_DIRECTX_11__;/D__USE_DIRECTX_12__;/D__USE_OPENGL__;/D__USE_VULKAN__")
;
;   vcxproj:  MASM item  →  Additional Options  →  /DPLATFORM_WINDOWS
;
; On Windows all four renderer defines are active simultaneously
; (the engine compiles all backends and selects at runtime), so
; all IFDEF renderer blocks are included in a Windows build.
; The OS guard (PLATFORM_WINDOWS) is the most practically useful:
; it gates routines that call OS or DirectX structures by value.
; ============================================================

; ── Platform: Windows ───────────────────────────────────────
IFDEF PLATFORM_WINDOWS
; Windows x64 note: all three routines above (MemoryCopy, MemoryZero,
; MatrixCopy4x4F) are already Windows x64 only because they use the
; Windows calling convention and are assembled by ml64.exe.
; Additional Windows-exclusive routines (e.g. AVX2-accelerated copies
; using 256-bit VMOVUPS when CPUID confirms AVX2 support, or routines
; touching DXGI/D3D memory layout constants) belong here.
ENDIF   ; PLATFORM_WINDOWS

; ── Renderer: DirectX 11 ────────────────────────────────────
IFDEF __USE_DIRECTX_11__
; DX11-specific hot paths (camera constant-buffer stride, D3D11
; mapped-subresource patterns) can be placed here.
; Example: a specialised ClearFloat4(dest, value[4]) that writes a
; D3D11_FLOAT4 clear colour to a typed UAV staging region.
ENDIF   ; __USE_DIRECTX_11__

; ── Renderer: DirectX 12 ────────────────────────────────────
IFDEF __USE_DIRECTX_12__
; DX12-specific helpers: upload-heap memcpy patterns, descriptor heap
; stride copies, or barrier-batch zero-fill routines belong here.
ENDIF   ; __USE_DIRECTX_12__

; ── Renderer: OpenGL ────────────────────────────────────────
IFDEF __USE_OPENGL__
; OpenGL-specific helpers: UBO staging-buffer zero, tightly-packed
; vertex attribute interleave copies belong here.
ENDIF   ; __USE_OPENGL__

; ── Renderer: Vulkan ────────────────────────────────────────
IFDEF __USE_VULKAN__
; Vulkan-specific helpers: VkCameraUBO copy (224-byte fixed struct),
; push-constant staging, or mapped device-memory zero belong here.
ENDIF   ; __USE_VULKAN__

END
