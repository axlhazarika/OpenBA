#pragma once
// ============================================================================
// src/render/vulkan/share_gpu_data/share_create.h -- ext.share_gpu_data, the Vulkan side
// (docs/extensions/handins/SHAREGPUDATA.md, issue #13)
// ============================================================================
// NOT RETAIL (P4). The Vulkan twin of src/render/dx11/share_gpu_data/share_create.h and the Metal one,
// over the same backend-neutral table (src/render/common/share_gpu_data/share_table.h). With the row on,
// the creates of objects the backend never writes after creation go through here:
//   - a static, data-seeded vertex / index buffer (D3D11 makes these IMMUTABLE; companions.cpp refuses
//     their SetData the same way), and
//   - a texture seeded from a resource file (kind 2: sampled only, every mip of every layer uploaded,
//     after ext.compress_ao's conversion);
// each keyed by the device, its descriptor (size and usage for a buffer; format, extent, mips, layers,
// cube and swizzle for an image) and the XXH64 of every seed byte (chained over the levels with each
// one's size), and an equal key hands out the object made first.
//
// HANDLES. Vulkan objects carry no reference count: the table's entry holds the object's handle as its
// key (handles[0]) and a heap copy of the whole GpuBuffer / GpuImage (handles[1]: allocation, view,
// extent) that an Acquire copies into the new user's companion. The table's "own reference" is that
// copy; the object itself is destroyed by its LAST user, which DropBuffer / DropImage tell (it then
// retires its own copy, seeded_create.h ReleaseBuffer / ReleaseImage): the frames in flight that read
// the object finish first (the retire queue). DeviceData::Shutdown clears the table (Clear); a user's
// later Drop finds nothing and its retire is the shutdown's no-op.
// ============================================================================
#include "resources.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace VideoVulkan {
namespace ShareGpuData {

// The row's boot value (ShareGpuDataBackend::Request). One relaxed load.
bool Requested();

// The bytes one image level is seeded with, as MakeTexture uploads them.
struct LevelBytes {
    const void* data;
    size_t size;
};

// A static buffer of `size` bytes seeded with `data`: an equal one is shared (*shared true, *out its
// copy), else `make` fills *out (false: the create failed) and the new buffer is inserted. The row off:
// `make` alone.
bool CreateBuffer(bool index, const void* data, size_t size, VkBufferUsageFlags usage,
                  const std::function<bool(GpuBuffer*)>& make, GpuBuffer* out, bool* shared);

// A kind-2 image of `desc` whose every subresource is `levels[0..count)`. `qualifies` false (a level
// missing or short): `make` alone, counted as refused. Otherwise as CreateBuffer.
bool CreateImage(const ImageDesc& desc, const LevelBytes* levels, int count, bool qualifies,
                 const std::function<bool(GpuImage*)>& make, GpuImage* out, bool* shared);

// Before a shared user lets go of its object: true when it was the last user (the caller retires it).
bool DropBuffer(const GpuBuffer& buffer);
bool DropImage(const GpuImage& image);

// DeviceData::Shutdown: the table forgets every object.
void Clear();

// The loader bracket's close (DeviceData::Slot4(false)): one summary line when anything was shared since
// the last.
void LoadEnd();

}  // namespace ShareGpuData
}  // namespace VideoVulkan
