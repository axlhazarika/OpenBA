// ============================================================================
// src/ext/fps_cap/fps_cap.cpp -- NOT RETAIL. ext.fps_cap (fps_cap.h has the contract)
// ============================================================================

#include "ext/fps_cap/fps_cap.h"

#include "ext/registry.h"
#include "frame_limiter.h"   // src/app: FrameLimiter::SetSettingsCap

namespace Ext {
namespace FpsCap {

void Sync() {
    const int32_t rate = Ext().fps_cap ? Val().fps_cap_rate : 0;
    FrameLimiter::SetSettingsCap(static_cast<double>(rate));
}

}  // namespace FpsCap
}  // namespace Ext
