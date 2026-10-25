// ============================================================================
// src/platform/common/platform_gfx.h — the graphics-library seam (Phase D.4 RENDER §C.1)
// ============================================================================
// NOT RETAIL as a seam. The renderer (src/render/dx11/) links against no
// graphics library: it resolves its only two non-COM entry points,
// D3D11CreateDevice and CreateDXGIFactory1, at first use. Where they come from
// is the platform's business, so the resolution lives behind this header
// (docs/runtime_port/phase_d/plans/RENDER.md §C.1, header spelling per
// D4_RULES C1). No OS header is included here.
//
// Implementations:
//   Windows  src/platform/win/gfx_win.cpp — GetModuleHandleW / LoadLibraryW /
//            GetProcAddress on d3d11.dll and dxgi.dll (the body that was
//            devicedata.cpp's ResolveGfxImport, moved unchanged).
//   POSIX    none: the dx11 backend is Windows-only since Vulkan P5, so only
//            ReleaseLibrary has a body there (src/platform/posix/gfx_posix.cpp).
//
// GfxProc is the opaque entry-point type the caller casts to its own
// D3D11CreateDevice_t / CreateDXGIFactory1_t. On Win64 it is EXACTLY the
// Windows SDK's FARPROC (`INT_PTR (WINAPI*)()`: INT_PTR and intptr_t are both
// `long long`, and x64 has one calling convention, so WINAPI is no part of the
// type), which gfx_win.cpp static_asserts. So the move adds no cast: the one
// FARPROC -> typed-pointer cast per entry point stays where it was, at the
// dx11 caller (`// CAST-OK: win32`).
//
// Both return null when the entry point cannot be resolved (after printing a
// "[Dx11] FAILED to resolve" line on stderr); the renderer then does not start.
// ============================================================================
#pragma once

#include <cstdint>

namespace Platform {

using GfxProc = std::intptr_t (*)();

// dxgi.dll!CreateDXGIFactory1 (Windows only: the dx11 backend's).
GfxProc ResolveCreateDXGIFactory1();

// d3d11.dll!D3D11CreateDevice (Windows only: the dx11 backend's).
GfxProc ResolveD3D11CreateDevice();

// Release one reference on a loaded module (Phase D.4 SDL-5; was
// RtEngineLifecyclePlatformFreeLibrary in src/platform/win/engine_lifecycle_win.cpp).
// Windows: FreeLibrary(module). POSIX: dlclose(module). Engine::Shutdown (0x1800db5e8, the
// three renderer plugin slots at Video::Device+0xB0) and Object::Factory::Shutdown /
// Engine::Shutdown (0x1800db639, Factory::_compilerModule) call it on every non-null
// module they hold. Today the slots hold the sentinel src/app/video_interface.cpp loads
// (an extra reference on the exe itself) and the compiler module is always null, so a
// module handed here must come from the platform's loader (LoadLibraryW / dlopen).
void ReleaseLibrary(void* module);

}  // namespace Platform
