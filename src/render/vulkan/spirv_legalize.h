#pragma once
// ============================================================================
// src/render/vulkan/spirv_legalize.h -- lowering of OpBitFieldInsert for the Adreno driver
// ============================================================================
// NOT RETAIL (P8 round 2, docs/android/P8_VULKAN.md §6). The Adreno 830 driver (1.3.284, shader compiler
// E031.47.18.56) fails vkCreateGraphicsPipelines with VK_ERROR_UNKNOWN ("AdrenoVK: Shader compilation
// failed for shaderType: 0") on a vertex shader holding OpBitFieldInsert: ext.ambient_occlusion's
// VSFullscreen (dxbc-spirv's form of SM5 `bfi`), so the AO pass never ran on the S25. No retail program
// has one (vulkan-shader-check). This pass rewrites each OpBitFieldInsert whose offset and count are
// 32-bit constants as the same bit operation in shifts and masks:
//   result = ((insert << offset) & mask) | (base & ~mask),   mask = ((1 << count) - 1) << offset
// An insert with a non-constant offset or count, or on a vector, is left as it is.
//
// No Vulkan header, no device (tests/unit/vulkan_ao_convert_test.cpp).
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace VideoVulkan {
namespace Shaders {

// Rewrites `spirv` in place. *changed counts the instructions rewritten. False with *why on a module
// the pass cannot read (left unchanged then).
bool LowerBitFieldInsert(std::vector<uint32_t>* spirv, uint32_t* changed, std::string* why);

}  // namespace Shaders
}  // namespace VideoVulkan
