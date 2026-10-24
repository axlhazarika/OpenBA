// ============================================================================
// src/render/metal/texture_quality/texture_quality_metal.cpp -- ext.texture_quality, the Metal answer
// (docs/extensions/handins/TEXQUALITY.md)
// ============================================================================
// NOT RETAIL. Not supported yet: the seeded upload (backend.mm PlanUploads) and the deferred
// create's shape (shape.levelCount = GetLevelCount()) still plan every level of the file, so the
// extension keeps the drop at 0 on this backend and the level readers keep every level. Supporting
// it means planning the uploads from the first kept level (texture_quality.h DropFor) at
// (width >> drop) x (height >> drop) with levelCount - drop levels, and the sampler's
// lodMaxClamp lowered by the same count.
// ============================================================================

#include "texture_quality/texture_quality_backend.h"

namespace TextureQualityBackend {

bool Supported() {
    return false;
}

}  // namespace TextureQualityBackend
