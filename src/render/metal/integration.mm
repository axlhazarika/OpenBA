// ============================================================================
// src/render/metal/integration.mm -- the Metal backend's side of the backend-neutral seams
// ============================================================================
// NOT RETAIL. The bodies of the interface headers in src/render/common (AoBackend::, SmokeVsBackend::), the
// finish seam (Ext::RenderPipe::) and the AO-after-opaque seam (Graphics::ExtAoAfterOpaque),
// plus the Objective-C++ constructor the plain C++ entry points call
// (render_backend_metal.cpp). Nothing here names another backend's namespace.
// ============================================================================

#include "VideoMetalData.h"
#include "metal_entry.h"
#include "video/Context.h"
#include "video/Device.h"
#include "ao/ao_backend.h"
#include "better_trees/foliage_aa_backend.h"
#include "better_trees/billboard_shadow_backend.h"
#include "async_gpu_create/async_gpu_create_backend.h"
#include "compress_ao/compress_ao_backend.h"
#include "share_gpu_data/share_gpu_data_backend.h"
#include "create_on_demand/create_on_demand_backend.h"
#include "draw_batch/smoke_vs_backend.h"
#include "ext/render_pipe/finish_split.h"
#include "graphics/renderer_ext_ao.h"
#include "passes/assao_pass.h"
#include "passes/tree_passes.h"

namespace VideoMetal {

Video::DeviceData* NewDeviceData(Video::Device* device) {
    return new DeviceData(device);
}

}  // namespace VideoMetal

namespace AoBackend {

// Ambient occlusion runs only when a Metal device is live AND every ASSAO shader converted.
bool Supported() {
    if (!VideoMetal::DeviceLive()) {
        return false;
    }
    return VideoMetal::AssaoReady();
}

// The Metal depth is created readable already, so the request changes nothing.
void RequestReadableDepth(bool on) {
    (void)on;
}

bool DepthReadable() {
    return VideoMetal::DepthReadable();
}

}  // namespace AoBackend

// ext.better_trees: the extension's requests go to the Metal tree passes (passes/tree_passes.mm),
// which rewrite the retail cutout and billboard programs from the user's own bytecode. Metal
// supports every mode the interface carries, so there is no "not supported" answer to give.
namespace FaaBackend {

void Request(int mode, float fillK, float fillFloor, const BbsRewrite::Params* dither) {
    VideoMetal::TreePasses::SetFoliage(mode, fillK, fillFloor, dither);
}

}  // namespace FaaBackend

namespace BbsBackend {

void Request(bool on) {
    VideoMetal::TreePasses::SetBillboardShadow(on);
}

}  // namespace BbsBackend

// ext.async_gpu_create: the Metal backend has no worker path (its resources are created where the
// loader makes them), so the request is ignored and the answer is "not supported".
namespace AsyncGpuCreateBackend {

void Request(bool on) {
    (void)on;
}

bool Supported() {
    return false;
}

}  // namespace AsyncGpuCreateBackend

// ext.compress_ao: the Metal backend does not create BC4 textures from encoded levels yet (its
// texture create takes the R8 levels as they are), so the request is ignored, the AO maps stay R8,
// the derived cache is never asked, and the answer is "not supported".
namespace CompressAoBackend {

void Request(bool on, Ext::DerivedCache::Store* cache) {
    (void)on;
    (void)cache;
}

bool Supported() {
    return false;
}

}  // namespace CompressAoBackend

// ext.share_gpu_data: the Metal backend has no create-or-share path yet (its buffer and texture
// creates make one object each), so the request is ignored and the answer is "not supported".
namespace ShareGpuDataBackend {

void Request(bool on) {
    (void)on;
}

bool Supported() {
    return false;
}

}  // namespace ShareGpuDataBackend

// ext.create_on_demand: the Metal backend has no deferred create yet (its buffer and texture creates
// read their bytes at load), so the request is ignored, nothing is ever recorded or prefetched, and
// the answer is "not supported".
namespace CreateOnDemandBackend {

void Request(bool on) {
    (void)on;
}

bool Supported() {
    return false;
}

void Prefetch(const void* companion) {
    (void)companion;
}

}  // namespace CreateOnDemandBackend

// ext.draw_batch, Idea B (merged smoke runs): the extension needs a rewritten particle vertex shader
// that writes an emitter slot into each vertex. Metal has no such rewrite, so it answers "not
// supported" through the interface: the stage stays off, Rewritten() is false for every program,
// and the extension draws the faithful path.
namespace SmokeVsBackend {

void Request(bool on) {
    (void)on;
}

bool Rewritten(const Video::VertexProgram* vp) {
    (void)vp;
    return false;
}

}  // namespace SmokeVsBackend

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

namespace Graphics {

void ExtAoAfterOpaque(Video::Context& context, const ExtAoFrame& frame, const RendererSceneEntry& entry) {
    VideoMetal::Assao::AfterOpaque(context, frame, entry);
}

}  // namespace Graphics
