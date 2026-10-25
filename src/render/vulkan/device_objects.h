#pragma once
// ============================================================================
// src/render/vulkan/device_objects.h -- what the rest of the backend reads from the device file
// ============================================================================
// NOT RETAIL. The logical device is a file-static of device.cpp; the resource files (resources.cpp,
// pipelines.cpp, draw.cpp, companions.cpp) reach it through the accessors below instead of a global.
// Each object made through this header is tracked, and DeviceData::Shutdown destroys whatever is
// still alive before vkDestroyDevice (a child must die before its device), so a companion released
// after the device went away (or across a device rebuild) has nothing left to free: its destroy
// call is then a no-op. Bodies: device.cpp.
// ============================================================================
#include "vk_api.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace VideoVulkan {

constexpr unsigned kFramesInFlight = 3;

// What the instance / device creation found out, for the log line and the later phases.
struct Caps {
    uint32_t apiVersion = 0;
    bool debugUtils = false;             // VK_EXT_debug_utils on the instance
    bool validation = false;             // VK_LAYER_KHRONOS_validation enabled
    bool pushDescriptor = false;         // VK_KHR_push_descriptor
    bool vertexInputDynamicState = false;// VK_EXT_vertex_input_dynamic_state
    bool extendedDynamicState2 = false;  // VK_EXT_extended_dynamic_state2 (EDS1 is 1.3 core)
    bool loadStoreOpNone = false;        // VK_KHR_load_store_op_none / VK_EXT_load_store_op_none
    bool d24s8 = false;                  // D24_UNORM_S8_UINT as a depth-stencil attachment
    bool samplerAnisotropy = false;      // the feature, enabled when present
    bool fillModeNonSolid = false;       // wireframe render states
    bool textureCompressionBC = false;   // BC1 / BC3 sampled (every desktop GPU, the S25)
    // VK_EXT_multisampled_render_to_single_sampled with its feature (the S25; not the desktop dev box):
    // the main target's MSAA is then the tiler's own, resolved on the tile (PLAN.md §4.4).
    bool msrtss = false;
    VkSampleCountFlags colorSamples = VK_SAMPLE_COUNT_1_BIT;
};

// ---- the live device (valid between CreateDevice and Shutdown) --------------------------------
VkDevice LiveDevice();
VkPhysicalDevice LivePhysical();
VkQueue LiveQueue();
uint32_t LiveGraphicsFamily();
const VkPhysicalDeviceProperties& LiveProperties();
const Caps& LiveCaps();
VkInstance LiveInstance();
// The depth-stencil format of every depth image this device makes: D24S8 where the device has it
// as an attachment (retail's choice), else D32S8 (PLAN.md §4.3).
VkFormat DepthStencilFormat();
// The swap chain's colour format (BGRA8 UNORM, RGBA8 where a surface has no BGRA).
VkFormat SwapchainFormat();
// The main target's sample count (P3): the configuration's multisample index resolved against the
// adapter's list as dx11 ResolveMultisampleMode does (index + 1, the index written back when lowered).
// Read again at CreateDevice and at every settings change.
uint32_t MainSampleCount();

// P3: a read-back in the middle of a frame (ImageBufferData::Read on the recording thread: the minimap
// capture, the save thumbnail, the screenshot) needs the frame's commands so far on the GPU. This ends
// the frame's command buffer, submits it (with the upload batch before it, and the acquire wait the
// first time), waits for it, and begins the next command buffer of the same frame, which it returns.
// The frame's final submit (Finish) then carries only the rest. Recording thread, inside a frame.
VkCommandBuffer SubmitFrameSoFar();

// A shader module over `spirv`. VK_NULL_HANDLE with a message in *why when there is no live device
// or vkCreateShaderModule fails. Any thread (the loader creates programs too).
VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv, std::string* why);
// Destroys a module CreateShaderModule made; a no-op for VK_NULL_HANDLE and for a module the device's
// Shutdown already destroyed.
void DestroyShaderModule(VkShaderModule module);

}  // namespace VideoVulkan
