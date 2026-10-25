// ============================================================================
// src/render/vulkan/draw.cpp -- the frame's pass, the bound state, descriptors, draws; ContextData slots
// ============================================================================
// NOT RETAIL. draw.h has the model. The ContextData slots here keep the DX11 slot bodies' order and
// arithmetic (src/render/dx11/contextdata.cpp: the counters, the dirty bits, the dynamic-vertex
// capacity tests and their reproduced retail quirks), with the D3D11 calls replaced by recording into
// the bound state and issuing the Vulkan draw.
// ============================================================================

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "draw.h"
#include "pipelines.h"
#include "resources.h"

#include "video/Buffers.h"
#include "video/Context.h"
#include "video/Device.h"
#include "video/ImageBuffer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VideoVulkan {

namespace {

constexpr uint32_t kStreams = 16;
constexpr uint32_t kTextureSlots = 16;
constexpr uint32_t kCbSlots = Shaders::kMaxConstantBuffers;
constexpr VkDeviceSize kRingChunk = 1ull << 20;
constexpr uint32_t kSetsPerPool = 256;
// The DX11 topology cache values (contextdata.cpp): 0 = none set yet, 4 = TRIANGLELIST, 2 = LINELIST.
constexpr int kTopologyNone = 0;
constexpr int kTopologyTriangles = 4;
constexpr int kTopologyLines = 2;

struct StreamBinding {
    const VertexBufferData* vb = nullptr;
    uint32_t offset = 0;
    uint32_t stride = 0;
};

struct CbBinding {
    const ConstantBufferData* cb = nullptr;
    uint32_t generation = 0;     // the cb's generation the ring copy holds
    uint64_t frame = ~0ull;      // the frame the ring copy was made in
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
};

// What the engine bound (draw.h: persists like a D3D11 context's state).
struct Bound {
    StreamBinding streams[kStreams];
    const IndexBufferData* index = nullptr;
    const VertexLayoutData* layout = nullptr;
    const VertexProgramData* vs = nullptr;
    const PixelProgramData* ps = nullptr;
    const RenderStateData* state = nullptr;
    const ImageBufferData* textures[kTextureSlots] = {};
    CbBinding cbs[2][kCbSlots];
    Video::Viewport viewport{};
    Video::Viewport scissor{};
    int topology = kTopologyNone;
    const Video::RenderBuffer* target = nullptr;
};
Bound g_bound;

// The descriptor contents of one stage's set (hashed bytes: no padding).
struct SetKey {
    uint32_t stage = 0;
    uint32_t cbMask = 0;
    uint32_t texMask = 0;
    uint32_t smpMask = 0;
    VkBuffer cb[kCbSlots] = {};
    VkDeviceSize cbOffset[kCbSlots] = {};   // static (non-dynamic) bindings only
    VkImageView view[kTextureSlots] = {};
    VkSampler sampler[kTextureSlots] = {};
};

struct SlotResources {
    std::vector<GpuBuffer> ring;   // constant-buffer chunks
    size_t chunk = 0;
    VkDeviceSize offset = 0;
    std::vector<VkDescriptorPool> pools;
    size_t pool = 0;
    std::unordered_map<uint64_t, std::vector<std::pair<SetKey, VkDescriptorSet>>> sets;
};
SlotResources g_slots[kFramesInFlight];

// The attachments of a pass, resolved from a render buffer when the pass begins (draw.h TARGETS).
struct Target {
    bool main = false;                     // the colour (or, with no colour, the depth) is a main target
    ImageState* color = nullptr;
    VkImageView colorView = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    ImageState* resolve = nullptr;         // the swap-chain image, resolve-attachment MSAA only
    VkImageView resolveView = VK_NULL_HANDLE;
    ImageState* depth = nullptr;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    uint32_t samples = 1;
    bool msrtss = false;
    VkExtent2D extent{};
};

// The frame being recorded, and what its command buffer has bound (reset with each frame).
struct Frame {
    FrameTarget target;
    bool active = false;
    bool passActive = false;
    bool mainDrawn = false;       // a pass ran on the main colour this frame
    Target pass;                  // the attachments of the active (or last) pass
    uint64_t index = 0;           // the frame index (FramesSubmitted at BeginCommands)
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool viewportSet = false;
    VkViewport viewport{};
    bool scissorSet = false;
    VkRect2D scissor{};
    int stencilRef = -1;
    VkBuffer vbuffers[kStreams] = {};
    VkDeviceSize voffsets[kStreams] = {};
    VkDeviceSize vstrides[kStreams] = {};
    bool vbuffersSet = false;
    VkBuffer ibuffer = VK_NULL_HANDLE;
    VkDescriptorSet sets[2] = {};
    uint32_t dynOffsets[2][kCbSlots] = {};
    bool setsBound = false;
    // The last resolved pipeline key (most draws repeat it).
    PipelineKey lastKey{};
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    bool lastValid = false;
    // Counters for the frame line.
    uint32_t passes = 0;
    uint32_t setsAllocated = 0;
    uint32_t targetPasses = 0;    // passes on render buffers other than the main one
    uint32_t resolves = 0;
    uint32_t reads = 0;
    uint32_t failedDraws = 0;
    VkDeviceSize ringBytes = 0;
};
Frame g_frame;

struct PendingClear {
    bool color = false;
    bool depth = false;
    bool stencil = false;
    float rgba[4] = {};
    float depthValue = 1.0f;
    uint32_t stencilValue = 0;
};
PendingClear g_clear;

// The backend's main targets (draw.h TARGETS), sized to the swap chain and shared by the frames in
// flight (submission order plus the pass barriers order them).
struct MainTargets {
    uint32_t samples = 0;
    bool msrtss = false;
    VkExtent2D extent{};
    GpuImage depth;               // attachment-only: `view` covers depth and stencil
    ImageState depthState;
    GpuImage color;               // MSAA only: the multisampled colour, or the tiler-MSAA colour
    ImageState colorState;
};
MainTargets g_main;
ImageState g_swap;                // the acquired swap-chain image, this frame
bool g_targetsChanged = false;
// The companion images this frame moved out of SHADER_READ_ONLY (the frame's end moves them back).
std::vector<ImageState*> g_touched;
std::vector<VkImageMemoryBarrier2> g_barriers;
// Texture slots of the draw being issued that sample an attachment of the bound render buffer.
uint32_t g_hazardSlots = 0;
// A one-shot command pool for a read-back outside a frame (under the device lock).
VkCommandPool g_readPool = VK_NULL_HANDLE;

std::unordered_set<const void*> g_loggedTargets;
uint32_t g_logOnce = 0;

uint64_t HashKey(const SetKey& k) {
    uint64_t h = 1469598103934665603ull;
    const auto* p = reinterpret_cast<const uint8_t*>(&k);   // CAST-OK: hashing a POD's bytes
    for (size_t i = 0; i < sizeof(SetKey); ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

void LogOnce(uint32_t bit, const char* message) {
    if (g_logOnce & bit) return;
    g_logOnce |= bit;
    std::fprintf(stderr, "[Vulkan] %s\n", message);
}

const ImageBufferData* Companion(const Video::ImageBuffer* ib) {
    return ib ? static_cast<const ImageBufferData*>(ib->_data) : nullptr;
}

ImageBufferData* MutableCompanion(const Video::ImageBuffer* ib) {
    return ib ? static_cast<ImageBufferData*>(ib->_data) : nullptr;
}

void Touch(ImageState* state) {
    if (std::find(g_touched.begin(), g_touched.end(), state) == g_touched.end()) g_touched.push_back(state);
}

// The command buffer has no state of its own yet (a new frame, or SubmitFrameSoFar's continuation).
void ResetCommandState() {
    g_frame.pipeline = VK_NULL_HANDLE;
    g_frame.viewportSet = false;
    g_frame.scissorSet = false;
    g_frame.stencilRef = -1;
    g_frame.vbuffersSet = false;
    g_frame.ibuffer = VK_NULL_HANDLE;
    g_frame.setsBound = false;
}

// ---- the main targets ----

void DestroyMainTargets() {
    GpuImage depth = g_main.depth;
    GpuImage color = g_main.color;
    g_main = MainTargets{};
    if (depth.image) RetireImage(&depth);
    if (color.image) RetireImage(&color);
}

bool MakeMainImage(VkFormat format, VkImageUsageFlags usage, uint32_t samples, bool msrtss, VkExtent2D extent,
                   GpuImage* image, ImageState* state) {
    ImageDesc d;
    d.width = extent.width;
    d.height = extent.height;
    d.format = format;
    d.usage = usage;
    d.attachmentOnly = true;
    d.samples = static_cast<VkSampleCountFlagBits>(msrtss ? 1u : samples);
    d.flags = msrtss ? VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT : 0;
    std::string why;
    if (!CreateImage(d, image, &why)) {
        std::fprintf(stderr, "[Vulkan] main target %ux%u format %d x%u: %s\n", extent.width, extent.height,
                     static_cast<int>(format), samples, why.c_str());
        return false;
    }
    state->image = image->image;
    state->aspect = image->aspect;
    state->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

bool EnsureMainTargets() {
    const VkExtent2D extent = g_frame.target.extent;
    const uint32_t samples = MainSampleCount();
    const bool msrtss = samples > 1 && LiveCaps().msrtss;
    if (g_main.depth.image && !g_targetsChanged && g_main.extent.width == extent.width &&
        g_main.extent.height == extent.height && g_main.samples == samples && g_main.msrtss == msrtss) {
        return true;
    }
    g_targetsChanged = false;
    DestroyMainTargets();
    g_main.samples = samples;
    g_main.msrtss = msrtss;
    g_main.extent = extent;
    if (!MakeMainImage(DepthStencilFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, samples, msrtss, extent,
                       &g_main.depth, &g_main.depthState)) {
        return false;
    }
    if (samples > 1) {
        const VkImageUsageFlags usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (msrtss ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        if (!MakeMainImage(SwapchainFormat(), usage, samples, msrtss, extent, &g_main.color, &g_main.colorState)) {
            return false;
        }
    }
    std::fprintf(stderr, "[Vulkan] main targets %ux%u, multisample %ux (%s)\n", extent.width, extent.height, samples,
                 samples == 1 ? "swap-chain image" : (msrtss ? "tiler msrtss" : "resolve attachments"));
    return true;
}

// The main colour as a Resolve / read source: where the frame so far is single-sampled.
ImageState* MainColorSource() {
    if (g_main.samples > 1 && g_main.msrtss) return &g_main.colorState;
    return &g_swap;
}

void MainColorInto(Target* t) {
    t->main = true;
    t->samples = g_main.samples;
    t->extent = g_frame.target.extent;
    t->colorFormat = SwapchainFormat();
    if (g_main.samples == 1) {
        t->color = &g_swap;
        t->colorView = g_frame.target.view;
    } else if (g_main.msrtss) {
        t->color = &g_main.colorState;
        t->colorView = g_main.color.view;
        t->msrtss = true;
    } else {
        t->color = &g_main.colorState;
        t->colorView = g_main.color.view;
        t->resolve = &g_swap;
        t->resolveView = g_frame.target.view;
    }
}

void MainDepthInto(Target* t) {
    t->depth = &g_main.depthState;
    t->depthView = g_main.depth.view;
    t->depthFormat = g_main.depth.format;
}

void MainTarget(Target* t) {
    *t = Target{};
    MainColorInto(t);
    MainDepthInto(t);
}

// The attachments of `rb` (draw.h TARGETS). False when it has none this backend can render to.
bool ResolveTarget(const Video::RenderBuffer* rb, Target* t) {
    *t = Target{};
    if (!rb) return false;
    if (!EnsureMainTargets()) return false;
    const ImageBufferData* c = Companion(rb->colorBuffer);
    const ImageBufferData* d = Companion(rb->depthBuffer);
    bool ownDepthMismatch = false;
    if (c && c->mainTarget == ImageBufferData::MainTarget::Color) {
        MainColorInto(t);
    } else if (c && c->attachmentView) {
        t->color = const_cast<ImageState*>(&c->state);   // CAST-OK: the bound state holds const companions
        t->colorView = c->attachmentView;
        t->colorFormat = c->image.format;
        t->extent = { c->image.width, c->image.height };
    }
    if (d && d->mainTarget != ImageBufferData::MainTarget::None) {
        if (!t->color) {
            t->main = true;
            t->samples = g_main.samples;
            t->msrtss = g_main.samples > 1 && g_main.msrtss;
            t->extent = g_frame.target.extent;
        }
        if (t->main) {
            MainDepthInto(t);
        } else if (g_main.samples == 1) {
            MainDepthInto(t);
            t->extent = { std::min(t->extent.width, g_main.extent.width),
                          std::min(t->extent.height, g_main.extent.height) };
        } else {
            ownDepthMismatch = true;
        }
    } else if (d && d->attachmentView) {
        if (!t->color || !t->main || g_main.samples == 1) {
            t->depth = const_cast<ImageState*>(&d->state);   // CAST-OK: as above
            t->depthView = d->attachmentView;
            t->depthFormat = d->image.format;
            if (t->color) {
                t->extent = { std::min(t->extent.width, d->image.width), std::min(t->extent.height, d->image.height) };
            } else {
                t->extent = { d->image.width, d->image.height };
            }
        } else {
            ownDepthMismatch = true;
        }
    }
    // NOT RETAIL, a divergence of this backend: D3D11 would refuse a multisampled main colour with a
    // single-sampled depth (or the reverse) at the draw; here the depth is left out of the pass.
    if (ownDepthMismatch) LogOnce(4, "a render buffer pairs the multisampled main colour with a single-sampled depth: "
                                     "the depth is left out");
    if (g_loggedTargets.insert(rb).second) {
        std::fprintf(stderr, "[Vulkan] render target %p: %ux%u colour %s (format %d) depth %s (format %d) x%u\n",
                     static_cast<const void*>(rb), t->extent.width, t->extent.height,
                     t->color ? (t->main ? "main" : "own") : "none", static_cast<int>(t->colorFormat),
                     t->depth ? (t->depth == &g_main.depthState ? "main" : "own") : "none",
                     static_cast<int>(t->depthFormat), t->samples);
    }
    return (t->color || t->depth) && t->extent.width > 0 && t->extent.height > 0;
}

// ---- the pass ----

void EndPass() {
    if (!g_frame.passActive) return;
    vkCmdEndRendering(g_frame.target.cb);
    g_frame.passActive = false;
}

bool BeginPass(const Target& t) {
    VkCommandBuffer cb = g_frame.target.cb;
    const VkImageLayout colorWas = t.color ? t.color->layout : VK_IMAGE_LAYOUT_UNDEFINED;
    const VkImageLayout depthWas = t.depth ? t.depth->layout : VK_IMAGE_LAYOUT_UNDEFINED;
    if (t.color) TransitionImage(*t.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &g_barriers);
    if (t.resolve) TransitionImage(*t.resolve, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, &g_barriers);
    if (t.depth) TransitionImage(*t.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, &g_barriers);
    FlushBarriers(cb, &g_barriers);
    if (!t.main) {
        if (t.color) Touch(t.color);
        if (t.depth && t.depth != &g_main.depthState) Touch(t.depth);
    }

    VkRenderingAttachmentInfo color = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = t.colorView;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Nothing to load from an image with no contents yet (fresh, or the acquired swap-chain image).
    color.loadOp = g_clear.color ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                 : (colorWas == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                                          : VK_ATTACHMENT_LOAD_OP_LOAD);
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    for (int i = 0; i < 4; ++i)
        color.clearValue.color.float32[i] = g_clear.rgba[i];
    if (t.resolve) {
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = t.resolveView;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo depth = Make<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    depth.imageView = t.depthView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    // A fresh depth image has no contents to load: it starts at the far plane, stencil 0.
    const bool depthFresh = depthWas == VK_IMAGE_LAYOUT_UNDEFINED;
    depth.loadOp = (g_clear.depth || depthFresh) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil.depth = g_clear.depth ? g_clear.depthValue : 1.0f;
    depth.clearValue.depthStencil.stencil = g_clear.stencil ? g_clear.stencilValue : 0;
    VkRenderingAttachmentInfo stencil = depth;
    stencil.loadOp = (g_clear.stencil || depthFresh) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    const bool hasStencil = t.depth && (t.depth->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;

    auto msrtss = Make<VkMultisampledRenderToSingleSampledInfoEXT>(
        VK_STRUCTURE_TYPE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT);
    msrtss.multisampledRenderToSingleSampledEnable = VK_TRUE;
    msrtss.rasterizationSamples = static_cast<VkSampleCountFlagBits>(t.samples);
    VkRenderingInfo ri = Make<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.pNext = t.msrtss ? &msrtss : nullptr;
    ri.renderArea = { { 0, 0 }, t.extent };
    ri.layerCount = 1;
    ri.colorAttachmentCount = t.color ? 1 : 0;
    ri.pColorAttachments = t.color ? &color : nullptr;
    ri.pDepthAttachment = t.depth ? &depth : nullptr;
    ri.pStencilAttachment = hasStencil ? &stencil : nullptr;
    vkCmdBeginRendering(cb, &ri);
    g_frame.passActive = true;
    g_frame.pass = t;
    g_clear = PendingClear{};
    ++g_frame.passes;
    if (t.main && t.color) g_frame.mainDrawn = true;
    if (!t.main) ++g_frame.targetPasses;
    // A new pass: the scissor default is its own extent.
    g_frame.scissorSet = false;
    return true;
}

bool EnsurePass() {
    if (g_frame.passActive) return true;
    Target t;
    if (!ResolveTarget(g_bound.target, &t)) {
        g_clear = PendingClear{};
        return false;
    }
    return BeginPass(t);
}

// A clear recorded for the bound target with no draw after it: run it now as an empty pass (D3D11
// clears at once). Called before the target changes, a copy, a read-back and the frame's end.
void FlushPendingClear() {
    if (!g_frame.active) return;
    if (!g_clear.color && !g_clear.depth && !g_clear.stencil) return;
    EndPass();
    if (EnsurePass()) EndPass();
}

// The textures the draw being issued samples are moved to SHADER_READ_ONLY before its pass begins (a
// pass in progress ends for it). One that is an attachment of the bound render buffer reads the
// placeholder instead (g_hazardSlots).
void PrepareTextures(const ShaderEntry* ps) {
    g_hazardSlots = 0;
    if (!ps) return;
    const uint32_t mask = ps->reflection.textureMask;
    const Video::RenderBuffer* rb = g_bound.target;
    const ImageBufferData* attachedColor = rb ? Companion(rb->colorBuffer) : nullptr;
    const ImageBufferData* attachedDepth = rb ? Companion(rb->depthBuffer) : nullptr;
    for (uint32_t t = 0; t < kTextureSlots; ++t) {
        if (!((mask >> t) & 1)) continue;
        const ImageBufferData* img = g_bound.textures[t];
        if (!img || img->mainTarget != ImageBufferData::MainTarget::None || !img->state.image) continue;
        if (img->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
        if (img == attachedColor || img == attachedDepth) {
            g_hazardSlots |= 1u << t;
            continue;
        }
        EndPass();
        auto* state = const_cast<ImageState*>(&img->state);   // CAST-OK: the bound state holds const companions
        TransitionImage(*state, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &g_barriers);
        Touch(state);
    }
    FlushBarriers(g_frame.target.cb, &g_barriers);
}

// ---- the constant ring ----

// Room for `size` bytes in the slot's ring, at an offset from which a whole descriptor range fits.
bool RingAlloc(VkDeviceSize size, VkBuffer* buffer, VkDeviceSize* offset, uint8_t** mapped) {
    SlotResources& s = g_slots[g_frame.target.slot];
    const VkDeviceSize align = std::max<VkDeviceSize>(LiveProperties().limits.minUniformBufferOffsetAlignment, 256);
    const VkDeviceSize need = std::max<VkDeviceSize>(size, kConstantBufferRange);
    for (;;) {
        if (s.chunk < s.ring.size()) {
            const VkDeviceSize at = (s.offset + align - 1) / align * align;
            if (at + need <= s.ring[s.chunk].size) {
                *buffer = s.ring[s.chunk].buffer;
                *offset = at;
                *mapped = s.ring[s.chunk].mapped + at;
                s.offset = at + size;
                g_frame.ringBytes += size;
                return true;
            }
            ++s.chunk;
            s.offset = 0;
            continue;
        }
        GpuBuffer chunk;
        std::string why;
        if (!CreateHostBuffer(std::max(kRingChunk, need), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &chunk, &why)) {
            std::fprintf(stderr, "[Vulkan] constant ring chunk: %s\n", why.c_str());
            return false;
        }
        s.ring.push_back(chunk);
    }
}

// The ring copy of a stage's constant buffer for this draw (made when the cb was bound again or the
// copy is from an earlier frame).
void ResolveConstants(CbBinding& c) {
    if (!c.cb || c.cb->shadow.empty()) {
        c.buffer = ZeroBuffer().buffer;
        c.offset = 0;
        return;
    }
    if (c.frame == g_frame.index && c.generation == c.cb->generation && c.buffer) return;
    uint8_t* p = nullptr;
    if (!RingAlloc(c.cb->shadow.size(), &c.buffer, &c.offset, &p)) {
        c.buffer = ZeroBuffer().buffer;
        c.offset = 0;
        return;
    }
    std::memcpy(p, c.cb->shadow.data(), c.cb->shadow.size());
    c.frame = g_frame.index;
    c.generation = c.cb->generation;
}

// ---- descriptor sets ----

VkDescriptorPool NewPool() {
    const uint32_t dyn = DynamicConstantBuffers();
    VkDescriptorPoolSize sizes[4];
    uint32_t count = 0;
    if (dyn) sizes[count++] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kSetsPerPool * dyn };
    if (kCbSlots > dyn) sizes[count++] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSetsPerPool * (kCbSlots - dyn) };
    sizes[count++] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kSetsPerPool * kTextureSlots };
    sizes[count++] = { VK_DESCRIPTOR_TYPE_SAMPLER, kSetsPerPool * kTextureSlots };
    auto pi = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pi.maxSets = kSetsPerPool;
    pi.poolSizeCount = count;
    pi.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    const VkResult r = vkCreateDescriptorPool(LiveDevice(), &pi, nullptr, &pool);
    if (r != VK_SUCCESS) std::fprintf(stderr, "[Vulkan] vkCreateDescriptorPool: %s\n", ResultName(r));
    return pool;
}

VkDescriptorSet AllocateSet(Shaders::Stage stage) {
    SlotResources& s = g_slots[g_frame.target.slot];
    const VkDescriptorSetLayout layout = StageSetLayout(stage);
    for (;;) {
        if (s.pool >= s.pools.size()) {
            VkDescriptorPool pool = NewPool();
            if (!pool) return VK_NULL_HANDLE;
            s.pools.push_back(pool);
        }
        auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        ai.descriptorPool = s.pools[s.pool];
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult r = vkAllocateDescriptorSets(LiveDevice(), &ai, &set);
        if (r == VK_SUCCESS) {
            ++g_frame.setsAllocated;
            return set;
        }
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) {
            std::fprintf(stderr, "[Vulkan] vkAllocateDescriptorSets: %s\n", ResultName(r));
            return VK_NULL_HANDLE;
        }
        ++s.pool;
    }
}

// The texture a stage's slot t samples, as a view of the type the shader declares (a placeholder when
// nothing usable is bound: never a null descriptor).
VkImageView TextureView(const ImageBufferData* image, bool cube, bool hazard) {
    if (!hazard && image && image->image.view && image->mainTarget == ImageBufferData::MainTarget::None &&
        image->cube == cube) {
        return image->image.view;
    }
    return cube ? WhiteCubeView() : WhiteImageView();
}

VkSampler TextureSampler(const ImageBufferData* image) {
    if (!image || !image->sampler) return DefaultSampler();
    // EXT SEAM (NOT RETAIL): ext.point_sampling's switch (dx11 imagebufferdata.cpp Bind), never on a
    // comparison sampler.
    if (Video::ImageBufferData::_extPointSampling && ((image->_owner->_flags >> 6) & 1) == 0 && image->pointSampler) {
        return image->pointSampler;
    }
    return image->sampler;
}

// Builds (or reuses) the set of `stage` for the bound resources the shader reads, and fills its
// dynamic offsets.
VkDescriptorSet StageSet(Shaders::Stage stage, const Shaders::ShaderReflection* refl, uint32_t* dynOffsets) {
    const uint32_t s = Shaders::SetOfStage(stage);
    const uint32_t dyn = DynamicConstantBuffers();
    SetKey key;
    key.stage = s;
    key.cbMask = refl ? refl->constantBufferMask : 0;
    key.texMask = refl ? refl->textureMask : 0;
    key.smpMask = refl ? refl->samplerMask : 0;
    for (uint32_t b = 0; b < kCbSlots; ++b) {
        const bool used = (key.cbMask >> b) & 1;
        dynOffsets[b] = 0;
        if (!used && b >= dyn) continue;
        CbBinding& c = g_bound.cbs[s][b];
        if (used) {
            ResolveConstants(c);
        }
        const VkBuffer buffer = used ? c.buffer : ZeroBuffer().buffer;
        const VkDeviceSize offset = used ? c.offset : 0;
        key.cb[b] = buffer;
        if (b < dyn) {
            dynOffsets[b] = static_cast<uint32_t>(offset);
        } else {
            key.cbOffset[b] = offset;
        }
    }
    // The vertex stage's texture slots are never bound by the DX11 backend (ImageBufferData::Bind
    // binds the pixel stage): they read the placeholder here.
    const bool pixel = stage == Shaders::Stage::Pixel;
    for (uint32_t t = 0; t < kTextureSlots; ++t) {
        if ((key.texMask >> t) & 1) {
            const bool cube = refl && ((refl->textureCubeMask >> t) & 1);
            key.view[t] = TextureView(pixel ? g_bound.textures[t] : nullptr, cube, pixel && ((g_hazardSlots >> t) & 1));
        }
        if ((key.smpMask >> t) & 1) key.sampler[t] = TextureSampler(pixel ? g_bound.textures[t] : nullptr);
    }

    SlotResources& slot = g_slots[g_frame.target.slot];
    const uint64_t hash = HashKey(key);
    auto& bucket = slot.sets[hash];
    for (const auto& entry : bucket) {
        if (std::memcmp(&entry.first, &key, sizeof(SetKey)) == 0) return entry.second;
    }
    VkDescriptorSet set = AllocateSet(stage);
    if (!set) return VK_NULL_HANDLE;
    VkDescriptorBufferInfo buffers[kCbSlots];
    VkDescriptorImageInfo images[kTextureSlots];
    VkDescriptorImageInfo samplers[kTextureSlots];
    VkWriteDescriptorSet writes[Shaders::kBindingsPerSet];
    uint32_t n = 0;
    for (uint32_t b = 0; b < kCbSlots; ++b) {
        if (!key.cb[b]) continue;
        buffers[b] = { key.cb[b], key.cbOffset[b], kConstantBufferRange };
        writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        writes[n].dstSet = set;
        writes[n].dstBinding = Shaders::BindingOfConstantBuffer(b);
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = b < dyn ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[n].pBufferInfo = &buffers[b];
        ++n;
    }
    for (uint32_t t = 0; t < kTextureSlots; ++t) {
        if (key.view[t]) {
            images[t] = { VK_NULL_HANDLE, key.view[t], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            writes[n].dstSet = set;
            writes[n].dstBinding = Shaders::BindingOfTexture(t);
            writes[n].descriptorCount = 1;
            writes[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[n].pImageInfo = &images[t];
            ++n;
        }
        if (key.sampler[t]) {
            samplers[t] = { key.sampler[t], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED };
            writes[n] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            writes[n].dstSet = set;
            writes[n].dstBinding = Shaders::BindingOfSampler(t);
            writes[n].descriptorCount = 1;
            writes[n].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[n].pImageInfo = &samplers[t];
            ++n;
        }
    }
    vkUpdateDescriptorSets(LiveDevice(), n, writes, 0, nullptr);
    bucket.emplace_back(key, set);
    return set;
}

// ---- vertex input ----

VkBuffer BufferOf(const VertexBufferData* vb) {
    return vb ? vb->store.Buffer() : VK_NULL_HANDLE;
}

void BindStreams(VkCommandBuffer cb) {
    const uint32_t mask = g_bound.layout ? g_bound.layout->streamMask : 0;
    if (mask == 0) return;
    VkBuffer buffers[kStreams];
    VkDeviceSize offsets[kStreams];
    VkDeviceSize strides[kStreams];
    uint32_t last = 0;
    for (uint32_t i = 0; i < kStreams; ++i) {
        buffers[i] = ZeroBuffer().buffer;
        offsets[i] = 0;
        strides[i] = 0;
        if (!((mask >> i) & 1)) continue;
        last = i + 1;
        const StreamBinding& s = g_bound.streams[i];
        const VkBuffer b = BufferOf(s.vb);
        if (b) {
            buffers[i] = b;
            offsets[i] = s.offset;
            strides[i] = s.stride;
        }
    }
    if (g_frame.vbuffersSet && std::memcmp(buffers, g_frame.vbuffers, sizeof(VkBuffer) * last) == 0 &&
        std::memcmp(offsets, g_frame.voffsets, sizeof(VkDeviceSize) * last) == 0 &&
        std::memcmp(strides, g_frame.vstrides, sizeof(VkDeviceSize) * last) == 0) {
        return;
    }
    vkCmdBindVertexBuffers2(cb, 0, last, buffers, offsets, nullptr, strides);
    std::memcpy(g_frame.vbuffers, buffers, sizeof(buffers));
    std::memcpy(g_frame.voffsets, offsets, sizeof(offsets));
    std::memcpy(g_frame.vstrides, strides, sizeof(strides));
    g_frame.vbuffersSet = true;
}

// The render-state block D3D11 uses with no state bound (the default blend / depth / raster states).
StateBlock DefaultState() {
    StateBlock s;
    s.writeMask = 0xF;
    s.depthTest = 1;
    s.depthWrite = 1;
    s.depthFunc = VK_COMPARE_OP_LESS;
    s.frontFunc = VK_COMPARE_OP_ALWAYS;
    s.backFunc = VK_COMPARE_OP_ALWAYS;
    s.cullMode = VK_CULL_MODE_BACK_BIT;
    s.polygonMode = VK_POLYGON_MODE_FILL;
    return s;
}

// The one draw path of every slot below: resolve the bound state, then record the draw.
void IssueDraw(bool indexed, uint32_t count, uint32_t instances, uint32_t first, int32_t baseVertex) {
    if (!g_frame.active || count == 0 || instances == 0) return;
    // D3D11 draws nothing with no topology set (UNDEFINED) or an empty viewport.
    if (g_bound.topology == kTopologyNone || g_bound.viewport.width <= 0 || g_bound.viewport.height <= 0) {
        ++g_frame.failedDraws;
        return;
    }
    const VertexProgramData* vsp = g_bound.vs;
    const ShaderEntry* vs = vsp ? vsp->shaders[vsp->boundPermutation] : nullptr;
    const PixelProgramData* psp = g_bound.ps;
    const ShaderEntry* ps = psp ? psp->shaders[psp->boundPermutation] : nullptr;
    if (!vs) {
        LogOnce(1, "a draw with no vertex program bound is skipped");
        ++g_frame.failedDraws;
        return;
    }
    PrepareTextures(ps);
    if (!EnsurePass()) {
        ++g_frame.failedDraws;
        return;
    }
    VkCommandBuffer cb = g_frame.target.cb;

    PipelineKey key;
    key.vs = vs->hash;
    key.ps = ps ? ps->hash : 0;
    key.layout = g_bound.layout ? g_bound.layout->layoutHash : 0;
    key.state = g_bound.state ? g_bound.state->block : DefaultState();
    key.colorFormat = static_cast<uint32_t>(g_frame.pass.colorFormat);
    key.depthFormat = static_cast<uint32_t>(g_frame.pass.depthFormat);
    key.topology = static_cast<uint8_t>(g_bound.topology == kTopologyLines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                           : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    key.samples = static_cast<uint8_t>(g_frame.pass.samples);
    key.msrtss = g_frame.pass.msrtss ? 1 : 0;
    key.linked = g_frame.lastValid ? g_frame.lastKey.linked : 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (g_frame.lastValid && std::memcmp(&key, &g_frame.lastKey, sizeof(PipelineKey)) == 0) {
        pipeline = g_frame.lastPipeline;
    } else {
        pipeline = ResolvePipeline(key, vs, ps);
        g_frame.lastKey = key;
        g_frame.lastPipeline = pipeline;
        g_frame.lastValid = true;
    }
    if (!pipeline) {
        ++g_frame.failedDraws;
        return;
    }
    if (pipeline != g_frame.pipeline) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        g_frame.pipeline = pipeline;
    }

    // Viewport (Y flipped by a negative height, PLAN.md §4.3) and scissor; a render state without
    // ScissorEnable scissors to the whole target.
    const Video::Viewport& v = g_bound.viewport;
    VkViewport vp{ static_cast<float>(v.x), static_cast<float>(v.y + v.height), static_cast<float>(v.width),
                   -static_cast<float>(v.height), 0.0f, 1.0f };
    if (!g_frame.viewportSet || std::memcmp(&vp, &g_frame.viewport, sizeof(vp)) != 0) {
        vkCmdSetViewport(cb, 0, 1, &vp);
        g_frame.viewport = vp;
        g_frame.viewportSet = true;
    }
    VkRect2D sc{ { 0, 0 }, g_frame.pass.extent };
    if (g_bound.state && g_bound.state->scissorEnable) {
        const Video::Viewport& s = g_bound.scissor;
        const int32_t x0 = std::max(s.x, 0);
        const int32_t y0 = std::max(s.y, 0);
        const int32_t x1 = std::max(s.x + s.width, x0);
        const int32_t y1 = std::max(s.y + s.height, y0);
        sc = { { x0, y0 }, { static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0) } };
    }
    if (!g_frame.scissorSet || std::memcmp(&sc, &g_frame.scissor, sizeof(sc)) != 0) {
        vkCmdSetScissor(cb, 0, 1, &sc);
        g_frame.scissor = sc;
        g_frame.scissorSet = true;
    }
    const int ref = g_bound.state ? g_bound.state->stencilReference : 0;
    if (ref != g_frame.stencilRef) {
        vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, static_cast<uint32_t>(ref));
        g_frame.stencilRef = ref;
    }

    // Descriptors.
    uint32_t offsets[2][kCbSlots];
    VkDescriptorSet sets[2];
    sets[0] = StageSet(Shaders::Stage::Vertex, &vs->reflection, offsets[0]);
    sets[1] = StageSet(Shaders::Stage::Pixel, ps ? &ps->reflection : nullptr, offsets[1]);
    if (!sets[0] || !sets[1]) {
        ++g_frame.failedDraws;
        return;
    }
    const uint32_t dyn = DynamicConstantBuffers();
    if (!g_frame.setsBound || sets[0] != g_frame.sets[0] || sets[1] != g_frame.sets[1] ||
        std::memcmp(offsets[0], g_frame.dynOffsets[0], dyn * sizeof(uint32_t)) != 0 ||
        std::memcmp(offsets[1], g_frame.dynOffsets[1], dyn * sizeof(uint32_t)) != 0) {
        uint32_t packed[2 * kCbSlots];
        std::memcpy(packed, offsets[0], dyn * sizeof(uint32_t));
        std::memcpy(packed + dyn, offsets[1], dyn * sizeof(uint32_t));
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, GamePipelineLayout(), 0, 2, sets, 2 * dyn,
                                packed);
        g_frame.sets[0] = sets[0];
        g_frame.sets[1] = sets[1];
        std::memcpy(g_frame.dynOffsets, offsets, sizeof(offsets));
        g_frame.setsBound = true;
    }

    BindStreams(cb);
    if (indexed) {
        const IndexBufferData* ib = g_bound.index;
        VkBuffer buffer = ib ? ib->store.Buffer() : VK_NULL_HANDLE;
        if (!buffer) buffer = ZeroBuffer().buffer;   // D3D11 reads zeros from a null index buffer
        if (buffer != g_frame.ibuffer) {
            vkCmdBindIndexBuffer(cb, buffer, 0, VK_INDEX_TYPE_UINT16);
            g_frame.ibuffer = buffer;
        }
        vkCmdDrawIndexed(cb, count, instances, first, baseVertex, 0);
    } else {
        vkCmdDraw(cb, count, instances, first, 0);
    }
}

}  // namespace

// ---- lifecycle ------------------------------------------------------------------------------------

bool DrawStartup(std::string* why) {
    g_bound = Bound{};
    g_frame = Frame{};
    g_clear = PendingClear{};
    g_loggedTargets.clear();
    g_touched.clear();
    g_logOnce = 0;
    auto pi = Make<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = LiveGraphicsFamily();
    if (vkCreateCommandPool(LiveDevice(), &pi, nullptr, &g_readPool) != VK_SUCCESS) {
        *why = "vkCreateCommandPool (read-back) failed";
        return false;
    }
    return true;
}

void DrawShutdown() {
    for (SlotResources& s : g_slots) {
        for (GpuBuffer& b : s.ring)
            DestroyBufferNow(&b);
        s.ring.clear();
        for (VkDescriptorPool p : s.pools)
            vkDestroyDescriptorPool(LiveDevice(), p, nullptr);
        s.pools.clear();
        s.sets.clear();
        s.chunk = 0;
        s.offset = 0;
        s.pool = 0;
    }
    DestroyImageNow(&g_main.depth);
    DestroyImageNow(&g_main.color);
    g_main = MainTargets{};
    if (g_readPool) vkDestroyCommandPool(LiveDevice(), g_readPool, nullptr);
    g_readPool = VK_NULL_HANDLE;
    g_touched.clear();
    g_bound = Bound{};
    g_frame = Frame{};
}

// ---- the frame --------------------------------------------------------------------------------------

void DrawBeginFrame(const FrameTarget& target) {
    SlotResources& s = g_slots[target.slot];
    s.chunk = 0;
    s.offset = 0;
    for (VkDescriptorPool p : s.pools)
        vkResetDescriptorPool(LiveDevice(), p, 0);
    s.pool = 0;
    s.sets.clear();
    const PipelineKey lastKey = g_frame.lastKey;
    const VkPipeline lastPipeline = g_frame.lastPipeline;
    const bool lastValid = g_frame.lastValid;
    g_frame = Frame{};
    g_frame.target = target;
    g_frame.active = true;
    g_frame.index = FramesSubmitted();
    g_frame.lastKey = lastKey;
    g_frame.lastPipeline = lastPipeline;
    g_frame.lastValid = lastValid;
    g_clear = PendingClear{};
    g_swap.image = target.image;
    g_swap.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    g_swap.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    g_touched.clear();
}

// Copies (or blits, where the formats or sizes differ) mip 0 of `src` into `dst`; both already moved
// to TRANSFER_SRC / TRANSFER_DST.
void CopyColor(VkCommandBuffer cb, VkImage src, VkFormat srcFormat, VkExtent2D srcExtent, VkImage dst,
               VkFormat dstFormat, VkExtent2D dstExtent) {
    if (srcFormat == dstFormat && srcExtent.width == dstExtent.width && srcExtent.height == dstExtent.height) {
        VkImageCopy c{};
        c.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        c.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        c.extent = { srcExtent.width, srcExtent.height, 1 };
        vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        return;
    }
    VkImageBlit b{};
    b.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    b.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    b.srcOffsets[1] = { static_cast<int32_t>(srcExtent.width), static_cast<int32_t>(srcExtent.height), 1 };
    b.dstOffsets[1] = { static_cast<int32_t>(dstExtent.width), static_cast<int32_t>(dstExtent.height), 1 };
    vkCmdBlitImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                   VK_FILTER_NEAREST);
}

void DrawEndFrame() {
    if (!g_frame.active) return;
    FlushPendingClear();
    EndPass();
    VkCommandBuffer cb = g_frame.target.cb;
    // A frame that drew nothing on the main colour still presents a defined image: black.
    if (!g_frame.mainDrawn && EnsureMainTargets()) {
        Target t;
        MainTarget(&t);
        g_clear.color = true;
        BeginPass(t);
        EndPass();
    }
    // Tiler MSAA: the frame is in the backend's colour image; the swap-chain image gets a copy.
    if (g_main.samples > 1 && g_main.msrtss && g_main.color.image) {
        TransitionImage(g_main.colorState, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &g_barriers);
        TransitionImage(g_swap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &g_barriers);
        FlushBarriers(cb, &g_barriers);
        CopyColor(cb, g_main.color.image, g_main.color.format, g_main.extent, g_swap.image, SwapchainFormat(),
                  g_frame.target.extent);
    }
    TransitionImage(g_swap, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &g_barriers);
    // Every companion image this frame used goes back to SHADER_READ_ONLY (resources.h ImageState).
    for (ImageState* s : g_touched)
        TransitionImage(*s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &g_barriers);
    g_touched.clear();
    FlushBarriers(cb, &g_barriers);
    g_frame.active = false;
}

void DrawLogFrame(uint64_t frame, const Video::Context* context) {
    uint32_t built = 0;
    uint32_t warmed = 0;
    uint32_t failed = 0;
    TakePipelineStats(&built, &warmed, &failed);
    uint64_t uploadBytes = 0;
    uint32_t batches = 0;
    TakeUploadStats(&uploadBytes, &batches);
    std::fprintf(stderr,
                 "[Vulkan] frame=%llu draws=%d triangles=%d vertices=%d objects=%d | passes=%u (other targets %u) "
                 "resolves=%u reads=%u sets=%u ring=%lluB failed=%u | pipelines +%u built +%u warmed "
                 "%u failed | uploads %llu KB in %u batch(es)\n",
                 static_cast<unsigned long long>(frame), context->_drawsSubmitted, context->_trianglesSubmitted,
                 context->_verticesSubmitted, context->_objectsSubmitted, g_frame.passes, g_frame.targetPasses,
                 g_frame.resolves, g_frame.reads, g_frame.setsAllocated,
                 static_cast<unsigned long long>(g_frame.ringBytes), g_frame.failedDraws, built, warmed, failed, static_cast<unsigned long long>(uploadBytes >> 10),
                 batches);
}

void DrawTargetsChanged() {
    g_targetsChanged = true;
}

// ---- the companions' Bind slots ----------------------------------------------------------------------

void DrawSetVertexStream(uint32_t slot, const VertexBufferData* vb, uint32_t offset, uint32_t stride) {
    if (slot >= kStreams) return;
    g_bound.streams[slot] = { vb, offset, stride };
}

void DrawSetIndexBuffer(const IndexBufferData* ib) {
    g_bound.index = ib;
}

void DrawSetLayout(const VertexLayoutData* layout) {
    g_bound.layout = layout;
}

void DrawSetVertexProgram(const VertexProgramData* program) {
    g_bound.vs = program;
}

void DrawSetPixelProgram(const PixelProgramData* program) {
    g_bound.ps = program;
}

void DrawSetRenderState(const RenderStateData* state) {
    g_bound.state = state;
}

void DrawSetTexture(uint32_t slot, const ImageBufferData* image) {
    if (slot < kTextureSlots) g_bound.textures[slot] = image;
}

void DrawSetConstants(const ConstantBufferData* cb, bool vertexStage, uint32_t slot) {
    if (slot >= kCbSlots) {
        LogOnce(2, "a constant buffer bound past b7 is ignored (shader_convert.h binding layout)");
        return;
    }
    CbBinding& c = g_bound.cbs[vertexStage ? Shaders::kSetVertex : Shaders::kSetPixel][slot];
    c.cb = cb;
    c.frame = ~0ull;   // copied into the ring at the next draw that reads it
}

void DrawSetRenderBuffer(const Video::RenderBuffer* rb) {
    if (rb == g_bound.target) return;
    // The old target's pending clear lands before the switch; the next draw begins a pass on `rb`.
    FlushPendingClear();
    EndPass();
    g_bound.target = rb;
}

void DrawForget(const void* companion) {
    for (StreamBinding& s : g_bound.streams) {
        if (s.vb == companion) s = StreamBinding{};
    }
    if (g_bound.index == companion) g_bound.index = nullptr;
    if (g_bound.layout == companion) g_bound.layout = nullptr;
    if (g_bound.vs == companion) g_bound.vs = nullptr;
    if (g_bound.ps == companion) g_bound.ps = nullptr;
    if (g_bound.state == companion) g_bound.state = nullptr;
    for (const ImageBufferData*& t : g_bound.textures) {
        if (t == companion) t = nullptr;
    }
    for (auto& stage : g_bound.cbs) {
        for (CbBinding& c : stage) {
            if (c.cb == companion) c = CbBinding{};
        }
    }
    // P3: a render buffer going away while bound, or an image the active pass renders into.
    if (g_bound.target == companion) {
        EndPass();
        g_bound.target = nullptr;
        g_clear = PendingClear{};
    }
}

void DrawForgetImage(const ImageBufferData* image) {
    const ImageState* state = &image->state;
    if (g_frame.passActive && (g_frame.pass.color == state || g_frame.pass.depth == state)) EndPass();
    auto it = std::find(g_touched.begin(), g_touched.end(), state);
    if (it != g_touched.end()) g_touched.erase(it);
}

// ---- P3: copies and read-backs ------------------------------------------------------------------------

void DrawResolve(const Video::RenderBuffer* dst, const Video::RenderBuffer* src) {
    if (!g_frame.active || !dst || !src) {
        LogOnce(8, "RenderBuffer::Resolve outside a frame is not recorded");
        return;
    }
    ImageBufferData* to = MutableCompanion(dst->colorBuffer);
    const ImageBufferData* from = Companion(src->colorBuffer);
    if (!to || !from || !to->state.image || to->mainTarget != ImageBufferData::MainTarget::None) {
        LogOnce(16, "RenderBuffer::Resolve into a buffer with no colour image of its own is skipped");
        return;
    }
    FlushPendingClear();
    EndPass();
    if (!EnsureMainTargets()) return;
    VkCommandBuffer cb = g_frame.target.cb;
    ImageState* source = nullptr;
    VkFormat sourceFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D sourceExtent{};
    if (from->mainTarget == ImageBufferData::MainTarget::Color) {
        source = MainColorSource();
        sourceFormat = SwapchainFormat();
        sourceExtent = g_frame.target.extent;
    } else if (from->state.image) {
        source = const_cast<ImageState*>(&from->state);   // CAST-OK: the source is only read
        sourceFormat = from->image.format;
        sourceExtent = { from->image.width, from->image.height };
        Touch(source);
    } else {
        return;
    }
    TransitionImage(*source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &g_barriers);
    TransitionImage(to->state, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &g_barriers);
    Touch(&to->state);
    FlushBarriers(cb, &g_barriers);
    CopyColor(cb, source->image, sourceFormat, sourceExtent, to->image.image, to->image.format,
              { to->image.width, to->image.height });
    ++g_frame.resolves;
}

bool DrawReadImage(ImageBufferData* image, void* dst, int width, int height) {
    const int format = static_cast<int>(image->_owner->_format);
    // The Metal backend's rule: no compressed or depth read-back (retail's staging copy is BGRA8, which
    // D3D11 refuses from those formats too), and nothing past the image.
    if (format < 3 || format == 7 || !image->image.image || image->mainTarget != ImageBufferData::MainTarget::None ||
        !dst || width <= 0 || height <= 0 || static_cast<uint32_t>(width) > image->image.width ||
        static_cast<uint32_t>(height) > image->image.height) {
        return false;
    }
    const uint32_t texel = (format == 3 || format == 4) ? 1u : 4u;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * texel;
    GpuBuffer readback;
    std::string why;
    if (!CreateReadbackBuffer(bytes, &readback, &why)) {
        std::fprintf(stderr, "[Vulkan] read-back buffer: %s\n", why.c_str());
        return false;
    }
    VkBufferImageCopy region{};
    region.bufferRowLength = static_cast<uint32_t>(width);
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    auto toHost = Make<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    toHost.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo hostDep = Make<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    hostDep.memoryBarrierCount = 1;
    hostDep.pMemoryBarriers = &toHost;
    if (g_frame.active) {
        // The recording thread, inside a frame: the image holds what this frame drew into it so far.
        FlushPendingClear();
        EndPass();
        VkCommandBuffer cb = g_frame.target.cb;
        TransitionImage(image->state, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &g_barriers);
        Touch(&image->state);
        FlushBarriers(cb, &g_barriers);
        vkCmdCopyImageToBuffer(cb, image->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                               &region);
        vkCmdPipelineBarrier2(cb, &hostDep);
        g_frame.target.cb = SubmitFrameSoFar();
        ResetCommandState();
        ++g_frame.reads;
    } else {
        // Between frames (the device lock is held): a command buffer of its own, after the upload batch.
        VkCommandBuffer cb = VK_NULL_HANDLE;
        auto ai = Make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = g_readPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(LiveDevice(), &ai, &cb) != VK_SUCCESS) {
            DestroyBufferNow(&readback);
            return false;
        }
        auto bi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        const VkImageLayout was = image->state.layout;
        TransitionImage(image->state, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, &g_barriers);
        FlushBarriers(cb, &g_barriers);
        vkCmdCopyImageToBuffer(cb, image->image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1,
                               &region);
        vkCmdPipelineBarrier2(cb, &hostDep);
        if (was != VK_IMAGE_LAYOUT_UNDEFINED) TransitionImage(image->state, was, &g_barriers);
        FlushBarriers(cb, &g_barriers);
        vkEndCommandBuffer(cb);
        FlushUploads();
        WaitSerial(SubmitCommands(cb, nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE));
        vkFreeCommandBuffers(LiveDevice(), g_readPool, 1, &cb);
    }
    InvalidateBuffer(readback);
    std::memcpy(dst, readback.mapped, static_cast<size_t>(bytes));
    DestroyBufferNow(&readback);
    return true;
}

// ---- ContextData: the slots that record work (dx11 contextdata.cpp slots 4-19) ------------------------

// Slot 4. The DX11 order: flush the lazily-bound render buffer / viewport / scissor first (a clear
// targets what is bound), clear the three dirty bits in one AND, then the clears; here a clear is
// recorded as the next pass's LOAD_OP_CLEAR (draw.h). The colour word is 0xAARRGGBB, each component
// DIVIDED by 255 as retail does.
void ContextData::Clear(unsigned flags, unsigned color, float depth, int stencil) {
    if (static_cast<uint8_t>(_owner->_dirty) & 0x01) ContextData::BindRenderBuffer();
    if (_owner->_dirty & 0x200) ContextData::BindViewport();
    if (_owner->_dirty & 0x400) ContextData::BindScissor();
    _owner->_dirty &= 0xfffff9feu;
    if (!g_frame.active) return;
    if ((flags & 7) == 0) return;
    EndPass();   // never a mid-pass clear: the next draw begins a pass that clears
    if (flags & 1) {
        g_clear.color = true;
        g_clear.rgba[0] = static_cast<float>((color >> 16) & 0xff) / 255.0f;
        g_clear.rgba[1] = static_cast<float>((color >> 8) & 0xff) / 255.0f;
        g_clear.rgba[2] = static_cast<float>(color & 0xff) / 255.0f;
        g_clear.rgba[3] = static_cast<float>(color >> 24) / 255.0f;
    }
    if (flags & 2) {
        g_clear.depth = true;
        g_clear.depthValue = depth;
    }
    if (flags & 4) {
        g_clear.stencil = true;
        g_clear.stencilValue = static_cast<uint8_t>(stencil);   // retail reads the stencil as a byte
    }
}

// Slot 5: unbind any texture slot that holds the render buffer's colour or depth image, flush the
// texture bindings, then bind the render buffer (RenderBuffer::Bind -> RenderBufferData::Bind).
void ContextData::BindRenderBuffer() {
    for (int i = 0; i < static_cast<int>(kTextureSlots); ++i) {
        ::Video::Context* const ctx = _owner;
        const ::Video::ImageBuffer* bound = ctx->_imageBuffers[i];
        const ::Video::RenderBuffer* rb = ctx->_renderBuffer;
        if (bound == rb->colorBuffer || bound == rb->depthBuffer) ctx->SetImageBuffer(nullptr, i);
    }
    if (static_cast<uint8_t>(_owner->_dirty) & 0x02) {
        _owner->BindTextures();
        _owner->_dirty &= 0xfffffffdu;
    }
    _owner->_renderBuffer->Bind(*_owner);
}

// Slots 6 / 7: the viewport and scissor as the engine holds them, applied at the next draw.
void ContextData::BindViewport() {
    g_bound.viewport = _owner->_viewport;
}

void ContextData::BindScissor() {
    g_bound.scissor = _owner->_scissor;
}

// Slot 8: the two constant buffers embedded in the Context (transforms gated on dirty bit 0x4000,
// bones on the live count), exactly as dx11.
void ContextData::BindTransforms() {
    ::Video::Context* ctx = _owner;
    if (ctx->_dirty & 0x4000) ctx->_transformConstants.Bind(*ctx, ctx->_transforms, 0, ctx->_transformCount << 6);
    ctx = _owner;
    if (ctx->_unknown0024 > 0) ctx->_boneConstants.Bind(*ctx, ctx->_boneAndProjectionStaging, 0, 0x0cc0);
}

// Slot 9 (pixel stage only, as retail).
void ContextData::BindTextureNone(int slot) {
    DrawSetTexture(static_cast<uint32_t>(slot), nullptr);
}

// Slot 10.
void ContextData::BindIndexNone() {
    DrawSetIndexBuffer(nullptr);
}

// Slot 11.
void ContextData::BindVertexNone(int slot) {
    DrawSetVertexStream(static_cast<uint32_t>(slot), nullptr, 0, 0);
}

// Slot 12. Retail forces TRIANGLELIST whatever the PrimitiveType (a shipped bug, reproduced); the
// counters use the truncating `indexCount / 3`.
void ContextData::DrawIndexed(int primitiveType, int baseVertex, int vertexCount, int startIndex, int indexCount,
                              int instanceCount) {
    (void)primitiveType;
    _owner->_trianglesSubmitted += (indexCount / 3) * instanceCount;
    _owner->_verticesSubmitted += vertexCount * instanceCount;
    _owner->_drawsSubmitted += 1;
    _owner->_objectsSubmitted += instanceCount;
    _owner->BindResources();
    g_bound.topology = kTopologyTriangles;
    IssueDraw(true, static_cast<uint32_t>(indexCount), static_cast<uint32_t>(instanceCount),
              static_cast<uint32_t>(startIndex), baseVertex);
}

// Slot 13: retail's literal 52.
int ContextData::GetMaxDynamicInstances(int a, int b, int c) {
    (void)a;
    (void)b;
    (void)c;
    return 0x34;
}

// Slot 14: sub-allocate out of the Context's shared dynamic vertex buffer (strict `<` capacity test).
bool ContextData::BeginVertices(int vertexCount, int vertexStride) {
    ::Video::Context* ctx = _owner;
    if (ctx->_dynamicVertexBuffer->size <= vertexCount * vertexStride) return false;
    _owner->_dynamicVertexStride = vertexStride;
    _owner->_unknown2104 = vertexCount;
    ctx = _owner;
    void* mapped = ctx->_dynamicVertexBuffer->Lock(vertexCount * vertexStride, ctx->_unknown2100);
    _owner->_dynamicVertexCursor = static_cast<uint8_t*>(mapped);   // CAST-OK: gpu_map (the Lock result)
    _owner->_unknown2108 = _owner->_dynamicVertexCursor;
    return _owner->_dynamicVertexCursor != nullptr;
}

// The topology filter of slots 15 / 17 / 19: 0 -> triangles, 2 -> lines, anything else keeps the last.
static void ApplyPrimitiveType(int primitiveType) {
    if (primitiveType == 0) g_bound.topology = kTopologyTriangles;
    if (primitiveType == 2) g_bound.topology = kTopologyLines;
}

// Slot 15.
void ContextData::EndVertices(int primitiveType) {
    _owner->_dynamicVertexCursor = nullptr;
    _owner->_dynamicVertexBuffer->Unlock();
    ::Video::Context* ctx = _owner;
    ctx->SetVertexBuffer(ctx->_dynamicVertexBuffer, 0, ctx->_unknown2100, ctx->_dynamicVertexStride);
    ctx = _owner;
    const int vertexCount = ctx->_unknown2104;
    ctx->_trianglesSubmitted += vertexCount / 3;
    _owner->_verticesSubmitted += _owner->_unknown2104;
    _owner->_drawsSubmitted += 1;
    _owner->_objectsSubmitted += 1;
    _owner->BindResources();
    ApplyPrimitiveType(primitiveType);
    IssueDraw(false, static_cast<uint32_t>(_owner->_unknown2104), 1, 0, 0);
}

// Slot 16: slot 18 with one instance.
bool ContextData::BeginIndexedVertices(int vertexCount, int vertexStride, int indexCount) {
    return ContextData::BeginInstancedIndexedVertices(vertexCount, vertexStride, indexCount, 1);
}

// Slot 17: slot 19's body (retail's jmp).
void ContextData::EndIndexedVertices(int primitiveType) {
    ContextData::EndInstancedIndexedVertices(primitiveType);
}

// Slot 18. The index capacity test compares against the VERTEX buffer's size (retail's bug, kept).
bool ContextData::BeginInstancedIndexedVertices(int vertexCount, int vertexStride, int indexCount,
                                                int instanceCount) {
    ::Video::Context* ctx = _owner;
    const int32_t vertexCapacity = ctx->_dynamicVertexBuffer->size;
    if (vertexCapacity <= vertexCount * vertexStride) return false;
    if (indexCount * 2 >= vertexCapacity) return false;   // sic, as retail
    ctx->_dynamicVertexStride = vertexStride;
    _owner->_unknown2104 = vertexCount;
    _owner->_instanceCount = instanceCount;
    ctx = _owner;
    void* mappedVertices = ctx->_dynamicVertexBuffer->Lock(vertexCount * vertexStride, ctx->_unknown2100);
    _owner->_dynamicVertexCursor = static_cast<uint8_t*>(mappedVertices);   // CAST-OK: gpu_map
    _owner->_unknown2108 = _owner->_dynamicVertexCursor;
    if (_owner->_dynamicVertexCursor == nullptr) return false;
    _owner->_unknown2124 = indexCount;
    ctx = _owner;
    void* mappedIndices = ctx->_dynamicIndexBuffer->Lock(indexCount * 2, ctx->_unknown2120);
    _owner->_dynamicIndexCursor = static_cast<uint16_t*>(mappedIndices);   // CAST-OK: gpu_map
    _owner->_unknown2128 = _owner->_dynamicIndexCursor;
    return _owner->_dynamicIndexCursor != nullptr;
}

// Slot 19. StartIndexLocation is the byte offset >> 1 through retail's sign-extend-then-logical-shift.
void ContextData::EndInstancedIndexedVertices(int primitiveType) {
    _owner->_dynamicVertexCursor = nullptr;
    _owner->_dynamicVertexBuffer->Unlock();
    _owner->_dynamicIndexCursor = nullptr;
    _owner->_dynamicIndexBuffer->Unlock();
    ::Video::Context* ctx = _owner;
    ctx->SetVertexBuffer(ctx->_dynamicVertexBuffer, 0, ctx->_unknown2100, ctx->_dynamicVertexStride);
    ctx = _owner;
    ::Video::IndexBuffer* const dynIb = ctx->_dynamicIndexBuffer;
    const int32_t dynOffset = ctx->_unknown2120;
    if (dynIb != ctx->_indexBuffer || dynOffset != ctx->_indexOffset) {
        ctx->_dirty |= 0x10u;
        ctx->_indexBuffer = dynIb;
        ctx->_indexOffset = dynOffset;
    }
    ctx = _owner;
    const int indexCount = ctx->_unknown2124;
    const int instanceCount = ctx->_instanceCount;
    ctx->_trianglesSubmitted += (indexCount / 3) * instanceCount;
    ctx = _owner;
    ctx->_verticesSubmitted += ctx->_instanceCount * ctx->_unknown2104;
    _owner->_drawsSubmitted += 1;
    _owner->_objectsSubmitted += _owner->_instanceCount;
    _owner->BindResources();
    ApplyPrimitiveType(primitiveType);
    ctx = _owner;
    const uint32_t startIndex =
        static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(ctx->_unknown2120)) >> 1);
    IssueDraw(true, static_cast<uint32_t>(ctx->_unknown2124), static_cast<uint32_t>(ctx->_instanceCount), startIndex,
              0);
}

}  // namespace VideoVulkan
