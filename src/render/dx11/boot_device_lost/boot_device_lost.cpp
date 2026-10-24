// ============================================================================
// src/render/dx11/boot_device_lost/boot_device_lost.cpp -- fix.boot_device_lost, the DX11 side
// ============================================================================
// NOT RETAIL. boot_device_lost.h has the contract.
// ============================================================================

#include "boot_device_lost/boot_device_lost.h"
#include "boot_device_lost/boot_device_lost_backend.h"

namespace VideoDX11 {

std::atomic<const bool*> g_extBootDeviceLost{nullptr};

}  // namespace VideoDX11

namespace BootDeviceLostBackend {

void SetFixFlag(const bool* live) {
    VideoDX11::g_extBootDeviceLost.store(live, std::memory_order_relaxed);
}

}  // namespace BootDeviceLostBackend
