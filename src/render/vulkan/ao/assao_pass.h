#pragma once
// ============================================================================
// src/render/vulkan/ao/assao_pass.h -- ext.ambient_occlusion on the Vulkan backend (Intel ASSAO)
// ============================================================================
// NOT RETAIL. docs/extensions/AO.md, docs/vulkan/PARITY.md. The DX11 backend runs Intel's own
// ASSAODX11.cpp over the precompiled DXBC (src/render/dx11/ao/); this file is the same algorithm
// driven natively: the same DXBC byte arrays (src/render/common/ao/shaders/, our own HLSL compiled
// offline by tools/ao_shaders.py: not game content) through the backend's one DXBC -> SPIR-V converter
// (shader_convert.h), the same passes in the same order with the same targets, formats, samplers,
// blend states and constants (ASSAODX11.cpp PrepareDepths / GenerateSSAO / Draw / UpdateConstants,
// with INTEL_SSAO_ENABLE_ADAPTIVE_QUALITY off as on DX11), and the dx11 pass around it
// (src/render/dx11/ao/ao_pass.cpp): the main depth resolved to a 1x R32F raw depth first
// (ao_depth_resolve.hlsl), the result multiplied into the main colour between the Opaque and Blended
// lists. Bodies: assao_pass.cpp. Nothing here names another backend.
// ============================================================================

namespace Video {
class Context;
}

namespace Graphics {
struct ExtAoFrame;
class RendererSceneEntry;
}  // namespace Graphics

namespace VideoVulkan {
namespace Assao {

// AoBackend::Supported: a live device with what the pass needs (device_objects.h Caps: fragment-stage
// storage writes, format-less storage images, the target formats) and every AO program converted. The
// conversion runs on the first call per device (main thread, Ext::AmbientOcclusion::Sync); a failure is
// logged once and answers false from then on.
bool Ready();

// Graphics::ExtAoAfterOpaque (renderer_ext_ao.h): the recording thread, inside the frame, between the
// main entry's Opaque and Blended lists. Leaves the engine's bound state to be rebound (draw.h DrawAoEnd).
void AfterOpaque(Video::Context& context, const Graphics::ExtAoFrame& frame, const Graphics::RendererSceneEntry& entry);

// DeviceData::Shutdown, the device idle: every object of the pass is destroyed; the next device starts
// over (Ready converts again).
void Shutdown();

}  // namespace Assao
}  // namespace VideoVulkan
