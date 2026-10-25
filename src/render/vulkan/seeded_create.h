#pragma once
// ============================================================================
// src/render/vulkan/seeded_create.h -- the Vulkan backend's creates of data-seeded buffers and textures
// ============================================================================
// NOT RETAIL (P4). One body for the three places that make a seeded object: the load (the buffers'
// Create and Serialize, ImageBufferData::Create in companions.cpp), ext.async_gpu_create's workers
// (async_gpu_create/async_create.cpp) and ext.create_on_demand's first-use create
// (create_on_demand/lazy_create.cpp), so all of them run the same chain in the same order, the DX11 and
// Metal order: ext.compress_ao's BC4 conversion, then ext.share_gpu_data's create-or-share, else the
// plain create. Any thread: VMA and the upload batch are thread-safe (resources.h), and the share table
// takes its own lock. Bodies: seeded_create.cpp.
// ============================================================================
#include "resources.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace VideoVulkan {

// What ImageBufferData::Create decided about the image, kept by value so a deferred create does not
// read the engine image again.
struct TextureShape {
    uint32_t width = 1;          // the image made (ext.texture_quality's drop applied)
    uint32_t height = 1;
    uint32_t mips = 1;           // the mips made (the drop applied)
    uint32_t layers = 1;         // 6 for a cube
    bool cube = false;
    int format = 0;              // the engine format (0..7)
    int kind = 0;                // 2: seeded from a resource file
    int levelCount = 1;          // the image's own GetLevelCount (before the drop)
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    VkComponentMapping swizzle{ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    VkImageUsageFlags usage = 0; // attachment usage of a render-buffer image (ImageDesc::usage)
};

// One level blob as the engine hands it to Create (after the dropped ones: level i is mip i % mips of
// layer i / mips).
struct SourceLevel {
    const void* data;
    size_t size;
};

// The outcome of MakeTexture beside the image.
struct TextureMade {
    bool shared = false;         // the image is in ext.share_gpu_data's table: never written, dropped there
    bool bc4 = false;            // ext.compress_ao made it BC4 (the engine still says R8)
    bool shortLevel = false;     // a level held fewer bytes than its extent: that level is left cleared
};

// The image of `shape` filled from `levels`. `seeded` (kind 2 with levels): ext.compress_ao's conversion
// may make it BC4, and ext.share_gpu_data may hand out an equal image made before. False (logged) when
// the image could not be made.
bool MakeTexture(const TextureShape& shape, const SourceLevel* levels, int count, bool seeded, GpuImage* out,
                 TextureMade* made);

// A static (device-local) vertex / index buffer of `size` bytes filled from `data` (null: zeros), through
// ext.share_gpu_data's create-or-share when `data` is given. *shared: the buffer is in the table.
bool MakeStaticBuffer(bool index, const void* data, uint32_t size, VkBufferUsageFlags usage, GpuBuffer* out,
                      bool* shared, std::string* why);

// A companion lets go of its object (a destructor, a re-create, an eviction): a shared one drops its
// use (the last user retires it), anything else is retired. The object and the flag are emptied.
void ReleaseBuffer(GpuBuffer* buffer, bool* shared);
void ReleaseImage(GpuImage* image, bool* shared);

}  // namespace VideoVulkan
