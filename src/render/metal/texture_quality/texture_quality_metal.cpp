// ============================================================================
// src/render/metal/texture_quality/texture_quality_metal.cpp -- ext.texture_quality, the Metal answer
// (docs/extensions/handins/TEXQUALITY.md)
// ============================================================================
// NOT RETAIL. The drop itself is in ImageBufferData::Create (backend.mm, its EXT SEAM lines): the
// descriptor at the first kept level's size with that many fewer mips, the uploads planned from the first
// kept level, and the sampler's lodMaxClamp lowered by the same count (MakeSampler reads the mip count).
// The deferred creates (ext.create_on_demand's records) receive the already-dropped shape and the kept
// levels only, and ext.compress_ao / ext.share_gpu_data see the smaller texture as any other.
// ============================================================================

#include "texture_quality/texture_quality_backend.h"

namespace TextureQualityBackend {

bool Supported() {
    return true;
}

}  // namespace TextureQualityBackend
