#pragma once
// ============================================================================
// src/render/vulkan/compress_ao/compress_ao.h -- ext.compress_ao, the Vulkan side
// (docs/extensions/handins/COMPRESSAO.md, issue #13)
// ============================================================================
// NOT RETAIL (P4). seeded_create.cpp MakeTexture asks Requested() and, for a 2D R8 texture seeded from a
// resource file (kind 2, every level of the chain present, the top level a multiple of 4 each way), has
// CompressAo::Encode (src/render/common/compress_ao/bc4_chain.h) turn its levels into BC4: the image is
// made VK_FORMAT_BC4_UNORM_BLOCK and each level uploaded as its block rows. Video::ImageBuffer::_format
// stays 4 (R8); a BC4 image refuses ImageBufferData::Update, as the Metal one does (it does not hold the
// rows the engine would write; nothing writes these textures). Body: compress_ao.cpp.
// ============================================================================

namespace VideoVulkan {
namespace CompressAo {

// The row's boot value (CompressAoBackend::Request), false when the device cannot sample BC4. One
// relaxed load.
bool Requested();

}  // namespace CompressAo
}  // namespace VideoVulkan
