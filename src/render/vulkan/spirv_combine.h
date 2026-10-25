#pragma once
// ============================================================================
// src/render/vulkan/spirv_combine.h -- separate texture / sampler bindings to combined image samplers
// ============================================================================
// NOT RETAIL (P8, ruled 2026-10-25: docs/android/P8_VULKAN.md §3). dxbc-spirv keeps D3D's model: a
// texture register t and a sampler register s are two descriptors (SAMPLED_IMAGE at BindingOfTexture(t),
// SAMPLER at BindingOfSampler(s)) joined by OpSampledImage at each sample. This pass rewrites the module
// so that each (t, s) pair the program samples with is ONE COMBINED_IMAGE_SAMPLER descriptor: fewer
// descriptors per draw (on Adreno, half the texture-side pushes).
//
// THE MAPPING (shader_convert.h, "COMBINED IMAGE SAMPLERS"):
//   - texture t's PRIMARY pair, (t, the lowest sampler register it is sampled with), is the combined
//     descriptor at BindingOfTexture(t); a texture only read without a sampler (Load, a size query)
//     is combined too, with no sampler register (the descriptor carries a placeholder sampler);
//   - every further pair (t, s') of the same texture (D3D allows a texture sampled with several
//     samplers) gets its own combined descriptor at BindingOfSampler(k), k = 0, 1, ... in (t, s')
//     order: the sampler bindings are free once nothing is a separate sampler;
//   - a read without a sampler goes through OpImage of the primary combined descriptor.
// The pairs are found statically: every OpSampledImage must join a load of a texture variable with
// a load of a sampler variable (dxbc-spirv's form for SM4 / SM5 without descriptor arrays). Anything
// else (a descriptor array, a buffer SRV, a sampler used outside OpSampledImage) fails the pass with
// a message, and the conversion fails with it: never a silent fallback.
//
// No Vulkan header, no device (tests/unit/vulkan_shader_convert_test.cpp, tools/vulkan/shader_check.cpp).
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

namespace VideoVulkan {
namespace Shaders {

struct ShaderReflection;

// Rewrites `spirv` (one module of the stage whose descriptors live in `set`) in place and records the
// pairs in refl->textureSampler / refl->extraPairs / refl->combined. False with *why on any form the
// pass does not handle (the module is left unchanged then).
bool CombineImageSamplers(std::vector<uint32_t>* spirv, uint32_t set, ShaderReflection* refl, std::string* why);

}  // namespace Shaders
}  // namespace VideoVulkan
