#pragma once
// ============================================================================
// src/render/dx11/compress_ao/bc4_create.h -- ext.compress_ao, the DX11 side
// (docs/extensions/handins/COMPRESSAO.md, issue #13)
// ============================================================================
// NOT RETAIL. ImageBufferData::Create (imagebufferdata.cpp, its EXT SEAM) asks Requested() and, for
// a mipmapped-or-not 2D R8_UNORM texture seeded from a resource file (usage 2, every level present,
// the top level a multiple of 4 each way), has CompressAo::Encode (render/common/compress_ao/
// bc4_chain.h) turn its levels into BC4: the descriptor's format becomes DXGI_FORMAT_BC4_UNORM, each
// subresource points at its encoded level with the block-row pitch, and the default SRV follows the
// texture's format. Video::ImageBuffer::_format stays 4 (R8): the engine never reads these textures
// back (Lock / Read / Update callers are generated BGRA images). ext.async_gpu_create's deferred
// create copies the encoded levels instead of the R8 ones.
// ============================================================================

namespace VideoDX11 {
namespace CompressAo {

// The row's boot value (CompressAoBackend::Request). One relaxed load.
bool Requested();

}  // namespace CompressAo
}  // namespace VideoDX11
