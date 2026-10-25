#pragma once
// ============================================================================
// src/render/vulkan/draw.h -- the frame's pass, the bound state, descriptors and draws
// ============================================================================
// NOT RETAIL. docs/vulkan/PLAN.md §4.3 / §4.4. Bodies: draw.cpp, which also holds the ContextData
// slots that record work (Clear, the Bind* slots, DrawIndexed, the dynamic-vertex slots); the frame
// slots (BeginCommands / EndCommands / Finish) stay in device.cpp and call the frame hooks below.
//
// THE BOUND STATE mirrors a D3D11 immediate context: the engine's BindResources binds only what is
// dirty, so every binding below persists across draws and frames until the engine replaces it. The
// companions' Bind slots record into it (Draw* calls); a draw resolves it into a pipeline, two
// descriptor sets and the vertex / index buffers.
//
// THE PASS (§4.4): dynamic rendering on the swap-chain image plus the backend's depth image, begun
// lazily at the first draw; a Clear only records intent, so it becomes the pass's LOAD_OP_CLEAR (a
// Clear after draws ends the pass and the next draw begins a new one: no mid-pass clear is emitted).
// P2 renders only into the main render buffer: binding another one (shadow map, minimap, the
// reflection and frame-copy targets) ends the pass, and its clears and draws are skipped and counted
// (one log line per distinct buffer) until P3.
//
// CONSTANT BUFFERS: a per-frame-slot ring of host-visible chunks; each constant buffer's bytes are
// copied in at the first draw that reads them after a Bind (or in a new frame), at a 256-byte-or-
// device-aligned offset, and bound as a dynamic offset (pipelines.h). DESCRIPTOR SETS come from a
// per-frame-slot pool, reset when the slot comes round again, and are reused within the frame for
// the same contents.
// ============================================================================
#include "vk_api.h"

#include <cstdint>
#include <string>

namespace Video {
class Context;
class RenderBuffer;
}  // namespace Video

namespace VideoVulkan {

class VertexBufferData;
class IndexBufferData;
class VertexLayoutData;
class VertexProgramData;
class PixelProgramData;
class RenderStateData;
class ImageBufferData;
class ConstantBufferData;

// ---- lifecycle (device.cpp) ----------------------------------------------------------------------
bool DrawStartup(std::string* why);
void DrawShutdown();   // device idle

// ---- the frame (device.cpp) ------------------------------------------------------------------------
struct FrameTarget {
    VkCommandBuffer cb = VK_NULL_HANDLE;
    unsigned slot = 0;                  // the frame-in-flight slot (its fence has been waited for)
    VkImage image = VK_NULL_HANDLE;     // the acquired swap-chain image
    VkImageView view = VK_NULL_HANDLE;
    VkExtent2D extent{};
};
// BeginCommands, after the slot's fence wait and the acquire: resets the slot's ring and pools.
void DrawBeginFrame(const FrameTarget& target);
// Finish, before vkEndCommandBuffer: ends the pass (running one for a frame that drew nothing, so its
// clear lands), and moves the swap-chain image to PRESENT_SRC.
void DrawEndFrame();
// The periodic frame line: the engine's counters of this frame plus the backend's own.
void DrawLogFrame(uint64_t frame, const Video::Context* context);
// The swap chain was rebuilt or the device is going: the next pass remakes the depth image.
void DrawTargetsChanged();

// ---- the companions' Bind slots ----------------------------------------------------------------------
void DrawSetVertexStream(uint32_t slot, const VertexBufferData* vb, uint32_t offset, uint32_t stride);
void DrawSetIndexBuffer(const IndexBufferData* ib);
void DrawSetLayout(const VertexLayoutData* layout);
void DrawSetVertexProgram(const VertexProgramData* program);
void DrawSetPixelProgram(const PixelProgramData* program);
void DrawSetRenderState(const RenderStateData* state);
void DrawSetTexture(uint32_t slot, const ImageBufferData* image);
void DrawSetConstants(const ConstantBufferData* cb, bool vertexStage, uint32_t slot);
void DrawSetRenderBuffer(const Video::RenderBuffer* renderBuffer);
// A companion is being destroyed: every binding that names it is dropped (D3D11 holds a reference on
// what is bound; here the binding just falls back to the placeholder).
void DrawForget(const void* companion);

}  // namespace VideoVulkan
