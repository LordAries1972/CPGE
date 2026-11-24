#pragma once

// ============================================================
// assembly.h -- C++ declarations for x86/64 MASM routines
// Implementation in assembly.asm, assembled by ml64.exe
//
// All routines follow the Windows x64 calling convention and
// are declared extern "C" so the C++ compiler does not mangle
// their names -- the assembler exports undecorated labels.
// ============================================================

#include <cstddef>      // size_t
#include <cstring>      // std::memcpy / std::memset (portable fallback below)
#include "Platform.h"   // PLATFORM_WINDOWS / PLATFORM_LINUX / PLATFORM_ANDROID

#if defined(PLATFORM_WINDOWS) && defined(_WIN64)
// Windows x64: the routines are implemented in assembly.asm (ml64.exe) and
// follow the Windows x64 calling convention; extern "C" keeps their names
// undecorated so the assembler exports match.
#ifdef __cplusplus
extern "C" {
#endif

// Fast memory copy -- copies Length bytes from Source to Destination.
// Uses REP MOVSQ for bulk quad-word transfer and REP MOVSB for the
// trailing 0-7 bytes.  Source and Destination must NOT overlap.
void MemoryCopy(const void* Source, void* Destination, size_t Length);

// Fast memory zero -- zeroes Length bytes at Destination.
// Uses REP STOSQ for bulk zero and REP STOSB for the trailing 0-7 bytes.
// Equivalent to memset(Destination, 0, Length) at hardware bandwidth.
void MemoryZero(void* Destination, size_t Length);

// Fixed-size 4x4 float matrix copy (64 bytes).
// Uses four 128-bit SSE MOVUPS loads + stores -- avoids loop overhead
// for this extremely common fixed size used in every render path.
// Source and Destination must each point to at least 64 bytes.
void MatrixCopy4x4F(const void* Source, void* Destination);

#ifdef __cplusplus
}
#endif

#else
// Portable fallback (Linux / Android / macOS / iOS): the MASM routines do
// not exist here, so the same three entry points are provided as inline
// CRT wrappers with identical signatures and semantics. The compiler
// inlines them to REP MOVSB / REP STOSB sequences, which is the same
// hardware path the assembly versions use, so render-frame hot paths keep
// working without a link dependency on assembly.asm.
inline void MemoryCopy(const void* Source, void* Destination, size_t Length)
{
    std::memcpy(Destination, Source, Length);
}

inline void MemoryZero(void* Destination, size_t Length)
{
    std::memset(Destination, 0, Length);
}

inline void MatrixCopy4x4F(const void* Source, void* Destination)
{
    std::memcpy(Destination, Source, 64);
}
#endif
