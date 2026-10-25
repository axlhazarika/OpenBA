#pragma once
// ============================================================================
// src/render/vulkan/boot_device_lost/boot_device_lost.h -- fix.boot_device_lost, the Vulkan side
// ============================================================================
// NOT RETAIL (P4). The DX11 row recovers a swap-chain reset that failed on a REMOVED device: retail
// enters its device-lost recovery only from Present, so a reset that keeps failing on a dead device
// spins the frame loop (docs/retail_bugs.md section 10). The Vulkan twin: the swap-chain rebuild of a
// settings change, a resize or an out-of-date swap chain (device.cpp BeginCommands) fails with
// VK_ERROR_DEVICE_LOST; with the row on, that frame takes the device-lost recovery Finish runs (the
// route a lost submit / acquire / present takes); off (and under the faithful profile), the rebuild is
// retried every frame as the reset is on DX11. Body: boot_device_lost.cpp.
// ============================================================================

namespace VideoVulkan {
namespace BootDeviceLost {

// The row's live bool (null before the registry boots: off). One relaxed load.
bool On();

}  // namespace BootDeviceLost
}  // namespace VideoVulkan
