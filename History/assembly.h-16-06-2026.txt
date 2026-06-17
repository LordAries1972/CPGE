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
