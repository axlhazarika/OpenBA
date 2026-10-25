// ============================================================================
// src/platform/posix/gfx_posix.cpp -- the POSIX half of src/platform/common/platform_gfx.h
// ============================================================================
// Only ReleaseLibrary lives here. The two graphics entry-point resolvers of that header
// (D3D11CreateDevice / CreateDXGIFactory1) are the dx11 backend's, which is Windows-only
// since Vulkan P5: off Windows the renderer is src/render/vulkan, whose Vulkan loader SDL
// opens (src/render/vulkan/vk_api.h). Their dlopen bodies went with the Phase D.4 renderer route.
// ============================================================================

#include "platform_gfx.h"

#include <dlfcn.h>

namespace Platform {

// FreeLibrary's twin (Phase D.4 SDL-5): the module is a dlopen handle (platform_gfx.h).
void ReleaseLibrary(void* module) {
    dlclose(module);
}

}  // namespace Platform
