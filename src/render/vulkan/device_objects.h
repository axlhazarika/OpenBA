#pragma once
// ============================================================================
// src/render/vulkan/device_objects.h -- device-owned objects the companions create through device.cpp
// ============================================================================
// NOT RETAIL. The logical device is a file-static of device.cpp; the resource companions
// (companions.cpp) make their Vulkan objects through these calls instead of reaching for it. Each
// object made here is tracked, and DeviceData::Shutdown destroys whatever is still alive before
// vkDestroyDevice (a child must die before its device), so a companion released after the device
// went away (or across a device rebuild) has nothing left to free: its destroy call is then a no-op.
// Bodies: device.cpp.
// ============================================================================
#include "vk_api.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VideoVulkan {

// A shader module over `spirv`. VK_NULL_HANDLE with a message in *why when there is no live device
// or vkCreateShaderModule fails. Any thread (the loader creates programs too).
VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv, std::string* why);
// Destroys a module CreateShaderModule made; a no-op for VK_NULL_HANDLE and for a module the device's
// Shutdown already destroyed.
void DestroyShaderModule(VkShaderModule module);

}  // namespace VideoVulkan
