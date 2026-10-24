// ============================================================================
// src/render/vulkan/integration.cpp -- the Vulkan backend's side of the backend-neutral seams
// ============================================================================
// NOT RETAIL. The bodies of the interface headers in src/render/common and the two seams that
// live with their callers (Ext::RenderPipe::, Graphics::ExtAoAfterOpaque). Every answer in P0
// is "not supported": the extension keeps its faithful path and nothing is silently stubbed
// (AGENTS.md §4). Each body names the phase that gives it a real answer (docs/vulkan/PLAN.md
// §5 P4). Nothing here names another backend's namespace.
// ============================================================================

#include "VideoVulkanData.h"
#include "vulkan_entry.h"

#include "video/Context.h"
#include "video/Device.h"
#include "ao/ao_backend.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/billboard_shadow_backend.h"
#include "async_gpu_create/async_gpu_create_backend.h"
#include "draw_batch/smoke_vs_backend.h"
#include "share_gpu_data/share_gpu_data_backend.h"
#include "create_on_demand/create_on_demand_backend.h"
#include "compress_ao/compress_ao_backend.h"
#include "texture_quality/texture_quality_backend.h"
#include "boot_device_lost/boot_device_lost_backend.h"
#include "ext/render_pipe/finish_split.h"
#include "graphics/renderer_ext_ao.h"

// ext.ambient_occlusion (P4: the ASSAO shaders through the same DXBC -> SPIR-V path, a readable
// depth attachment). Until then: not supported, and the depth request changes nothing.
namespace AoBackend {

bool Supported() {
    return false;
}

void RequestReadableDepth(bool on) {
    (void)on;
}

bool DepthReadable() {
    return false;
}

}  // namespace AoBackend

// ext.better_trees (P4): the rewrites edit the DXBC before conversion, so the request is kept
// and applied when the programs are converted. P0 has no programs; the request is ignored.
namespace FaaBackend {

void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither) {
    (void)mode;
    (void)fillK;
    (void)fillFloor;
    (void)dither;
}

#if BANISHED_TWEAK_OVERLAY
TweakStats Stats() {
    return TweakStats{};
}
#endif

}  // namespace FaaBackend

namespace BbsBackend {

void Request(bool on) {
    (void)on;
}

#if BANISHED_TWEAK_OVERLAY
void RequestParams(const BbsRewrite::Params& p) {
    (void)p;
}

BbsRewrite::Params AppliedParams() {
    return BbsRewrite::Params{};
}
#endif

}  // namespace BbsBackend

// ext.async_gpu_create (P4): no worker path yet.
namespace AsyncGpuCreateBackend {

void Request(bool on) {
    (void)on;
}

bool Supported() {
    return false;
}

}  // namespace AsyncGpuCreateBackend

// ext.draw_batch, Idea B (merged smoke runs): needs the rewritten particle vertex shader; the
// extension draws the faithful path while Rewritten() is false for every program.
namespace SmokeVsBackend {

void Request(bool on) {
    (void)on;
}

bool Rewritten(const Video::VertexProgram* vp) {
    (void)vp;
    return false;
}

}  // namespace SmokeVsBackend

// ext.share_gpu_data (P4): no GPU objects to share yet.
namespace ShareGpuDataBackend {

void Request(bool on) {
    (void)on;
}

bool Supported() {
    return false;
}

}  // namespace ShareGpuDataBackend

// ext.create_on_demand (P4): nothing is deferred; every object is "made" when the engine asks.
namespace CreateOnDemandBackend {

void Request(bool on, int idleMinutes) {
    (void)on;
    (void)idleMinutes;
}

bool Supported() {
    return false;
}

void Prefetch(const void* companion) {
    (void)companion;
}

void Keep(const void* companion) {
    (void)companion;
}

bool VertexBufferMade(const void* companion) {
    (void)companion;
    return true;
}

bool ImageMade(const void* companion) {
    (void)companion;
    return true;
}

}  // namespace CreateOnDemandBackend

// ext.compress_ao (P4): the BC4 re-encode of the AO maps needs the texture upload path.
namespace CompressAoBackend {

void Request(bool on, Ext::DerivedCache::Store* cache) {
    (void)on;
    (void)cache;
}

bool Supported() {
    return false;
}

}  // namespace CompressAoBackend

// ext.texture_quality (P4): the level drop needs the texture upload path.
namespace TextureQualityBackend {

bool Supported() {
    return false;
}

}  // namespace TextureQualityBackend

// fix.boot_device_lost: the row's backend half recovers a swap-chain reset that failed on a
// REMOVED DXGI device. VK_ERROR_DEVICE_LOST has its own route (PLAN.md §4.5, P7); the flag is
// accepted and has nothing to act on here.
namespace BootDeviceLostBackend {

void SetFixFlag(const bool* live) {
    (void)live;
}

}  // namespace BootDeviceLostBackend

// ext.render_pipe: the render thread presents through Finish; nothing to recover on this
// backend's present path.
namespace Ext {
namespace RenderPipe {

int32_t FinishPresent(Video::Context& ctx) {
    ctx._contextData->Finish();
    return 0;
}

void FinishRecover(Video::Device& device, int32_t state) {
    (void)device;
    (void)state;
}

}  // namespace RenderPipe
}  // namespace Ext

// ext.ambient_occlusion's pass hook: nothing runs while AoBackend::Supported() is false.
namespace Graphics {

void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame, const RendererSceneEntry& entry) {
    (void)context;
    (void)frame;
    (void)entry;
}

}  // namespace Graphics
