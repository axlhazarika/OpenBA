// ============================================================================
// src/render/vulkan/device.cpp -- the Vulkan device, swap chain and frame loop
// ============================================================================
// NOT RETAIL. The DeviceData and ContextData of VideoVulkanData.h, the adapter
// enumeration (Enumerate) and the main-target attach slots. The shape follows the Metal
// backend (src/render/metal/backend.mm) and the slot roles the DX11 files
// (src/render/dx11/devicedata.cpp, contextdata.cpp); docs/vulkan/PLAN.md §4 is the design.
//
// P0 (PLAN.md §5): instance, physical device, logical device, SDL surface, swap chain and a
// three-deep frame loop. P2: a frame is BeginCommands (wait for the slot, collect the retired
// objects, acquire an image, open the command buffer, reset the slot's ring and pools: draw.h),
// the draws (draw.cpp records them into the lazily-begun dynamic-rendering pass), then Finish (end
// the pass, submit the upload batch and the frame, present). The resource files are resources.h
// (memory, submission, retire queue), pipelines.h and draw.h; this file starts and stops them.
//
// Threads: BeginCommands / Clear / Finish run on whichever thread drives the frame (the
// main thread, or ext.render_pipe's render thread); the device lock (Video::Device::Lock,
// recursive) is taken in BeginCommands and dropped in Finish before the present, the DX11
// order. CreateDevice / Shutdown run on the main thread inside Engine::Startup / shutdown.
//
// Vulkan floor (PLAN.md §4.1): 1.3 core with dynamicRendering and synchronization2, plus
// VK_KHR_swapchain. Nothing else is required; what is merely used when present is probed
// here and recorded in `caps`.
// ============================================================================

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "draw.h"
#include "pipelines.h"
#include "resources.h"
#include "vulkan_entry.h"

#include "app/options.h"
#include "boot_window.h"   // BootWindow::ArmDeferredShow (src/app): the deferred first show
#include "stall_debug.h"   // StallDebug::Mark (src/diag): the boot timeline
#include "platform/common/platform_window.h"         // Platform::Window::ShowDefault
#include "platform/common/platform_window_hooks.h"   // Platform::Window::QueryScreenForModes
#include "display_geometry.h"                        // Platform::Window::BuildResolutionList
#include "platform/sdl/window_sdl_internal.h"         // Platform::Window::Sdl::GameWindow
#include "system/Error.h"
#include "system/MemoryAllocator.h"
#include "video/Device.h"
#include "video/Context.h"
#include "video/ImageBuffer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

namespace VideoVulkan {

namespace {

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;              // signalled when this slot's submit completed
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
};

VkInstance instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
std::vector<VkPhysicalDevice> physicalDevices;   // the adapters Enumerate listed, in order
VkPhysicalDevice physical = VK_NULL_HANDLE;
uint32_t graphicsFamily = 0;
VkDevice device = VK_NULL_HANDLE;
VkQueue queue = VK_NULL_HANDLE;
VkSurfaceKHR surface = VK_NULL_HANDLE;
VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkFormat swapFormat = VK_FORMAT_UNDEFINED;
VkExtent2D swapExtent{};
std::vector<VkImage> swapImages;
std::vector<VkImageView> swapViews;
std::vector<VkSemaphore> renderFinished;   // one per swap-chain image
Frame frames[kFramesInFlight];
unsigned frameSlot = 0;
uint32_t imageIndex = 0;
bool imageAcquired = false;
bool swapchainDirty = false;   // rebuild before the next acquire
Caps caps;
VkPhysicalDeviceProperties deviceProperties{};
int lastFullscreen = -1;
uint64_t frameCount = 0;
bool resourcesUp = false;   // ResourcesStartup / PipelinesStartup / DrawStartup ran
// The shader modules CreateShaderModule made and nothing destroyed yet (device_objects.h); Shutdown
// destroys the rest before the device. Its own lock: programs are created on the loader thread too.
std::mutex moduleLock;
std::unordered_set<VkShaderModule> liveModules;

// The SDL window the surface is made on. Device::windowHandle is the native handle (the HWND on
// Windows, retail's field); the SDL window that owns it is the one window_sdl.cpp created.
SDL_Window* GameWindow() {
    return Platform::Window::Sdl::GameWindow();
}

bool Fail(const char* message) {
    std::fprintf(stderr, "[Vulkan] %s\n", message);
    System::Error::SetText(message, false);
    return false;
}

bool Fail(const std::string& message) {
    return Fail(message.c_str());
}

const char* DeviceTypeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT types,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)types;
    (void)user;
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "warning";
    std::fprintf(stderr, "[Vulkan] validation %s: %s\n", level, data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

// The one instance of the process: made by the first Enumerate (the adapter list needs the
// physical devices) and kept until exit. Shutdown destroys the device, not the instance, so a
// device rebuild (Options Apply through Device::PlatformStartup's fallback) reuses it.
bool EnsureInstance(std::string* why) {
    if (instance) return true;
    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        *why = std::string("SDL could not load the Vulkan loader: ") + SDL_GetError();
        return false;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!LoadGlobalFunctions(gipa, why)) return false;

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&loaderVersion);
    if (loaderVersion < VK_API_VERSION_1_3) {
        *why = "the Vulkan loader is older than 1.3";
        return false;
    }

    Uint32 sdlCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
    if (!sdlExtensions) {
        *why = std::string("SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError();
        return false;
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlCount);
    std::vector<const char*> layers;

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
    for (const auto& e : available) {
        if (std::strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) caps.debugUtils = true;
    }
#if defined(_DEBUG)
    // The debug flavour validates when the layer is installed (the Vulkan SDK on a dev box,
    // the layer pushed into the app on Android); the other flavours never look for it.
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layerProps(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layerProps.data());
    for (const auto& l : layerProps) {
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) caps.validation = true;
    }
    if (caps.validation) layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
    if (caps.debugUtils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo app = Make<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "OpenBA";
    app.applicationVersion = 1;
    app.pEngineName = "OpenBA";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci = Make<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ci, nullptr, &instance);
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreateInstance failed: ") + ResultName(r);
        instance = VK_NULL_HANDLE;
        return false;
    }
    if (!LoadInstanceFunctions(instance, why)) return false;
    if (caps.debugUtils && vkCreateDebugUtilsMessengerEXT) {
        auto mi = Make<VkDebugUtilsMessengerCreateInfoEXT>(
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mi.pfnUserCallback = DebugMessage;
        vkCreateDebugUtilsMessengerEXT(instance, &mi, nullptr, &messenger);
    }

    // The adapters: every physical device with a graphics queue, in enumeration order.
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> all(count);
    vkEnumeratePhysicalDevices(instance, &count, all.data());
    physicalDevices.clear();
    for (VkPhysicalDevice pd : all) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> q(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, q.data());
        bool graphics = false;
        for (const auto& f : q) {
            if (f.queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = true;
        }
        if (graphics) physicalDevices.push_back(pd);
    }
    if (physicalDevices.empty()) {
        *why = "no Vulkan device with a graphics queue was found";
        return false;
    }
    std::fprintf(stderr, "[Vulkan] instance: loader %u.%u.%u, %zu adapter(s)%s%s\n",
                 VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion),
                 VK_API_VERSION_PATCH(loaderVersion), physicalDevices.size(),
                 caps.validation ? ", validation on" : "", caps.debugUtils ? ", debug utils" : "");
    return true;
}

bool HasDeviceExtension(VkPhysicalDevice pd, const char* name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> props(count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, props.data());
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName, name) == 0) return true;
    }
    return false;
}

// ---- the swap chain ---------------------------------------------------------------------------

void DestroySwapchainObjects() {
    for (VkImageView v : swapViews) vkDestroyImageView(device, v, nullptr);
    swapViews.clear();
    for (VkSemaphore s : renderFinished) vkDestroySemaphore(device, s, nullptr);
    renderFinished.clear();
    swapImages.clear();
}

bool CreateSwapchain(SDL_Window* window, std::string* why) {
    VkSurfaceCapabilitiesKHR surfaceCaps{};
    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &surfaceCaps);
    if (r != VK_SUCCESS) {
        *why = std::string("vkGetPhysicalDeviceSurfaceCapabilitiesKHR: ") + ResultName(r);
        return false;
    }

    // Retail's back buffer is BGRA8 UNORM; RGBA8 when a surface has no BGRA (Android).
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    if (!formats.empty()) chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    if (chosen.format != VK_FORMAT_B8G8R8A8_UNORM) {
        for (const auto& f : formats) {
            if (f.format == VK_FORMAT_R8G8B8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f;
                break;
            }
        }
    }

    // VSync on: FIFO (always available). Off: mailbox, then immediate, then FIFO anyway.
    const bool vsync = Video::Configuration::_singleton ? Video::Configuration::_singleton->vsync != 0 : true;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync || Opt().stress) {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, modes.data());
        for (VkPresentModeKHR m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) presentMode = m;
        }
        if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
            for (VkPresentModeKHR m : modes) {
                if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) presentMode = m;
            }
        }
    }

    VkExtent2D extent = surfaceCaps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        extent.width = std::clamp(static_cast<uint32_t>(std::max(w, 1)), surfaceCaps.minImageExtent.width,
                                  surfaceCaps.maxImageExtent.width);
        extent.height = std::clamp(static_cast<uint32_t>(std::max(h, 1)), surfaceCaps.minImageExtent.height,
                                   surfaceCaps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        *why = "the window has no pixels (minimised)";
        return false;
    }

    uint32_t imageCount = std::max(kFramesInFlight, surfaceCaps.minImageCount);
    if (surfaceCaps.maxImageCount != 0) imageCount = std::min(imageCount, surfaceCaps.maxImageCount);

    VkSwapchainCreateInfoKHR ci = Make<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    ci.surface = surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                          ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                          : surfaceCaps.currentTransform;
    ci.compositeAlpha = (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain;
    VkSwapchainKHR created = VK_NULL_HANDLE;
    r = vkCreateSwapchainKHR(device, &ci, nullptr, &created);
    if (r != VK_SUCCESS) {
        *why = std::string("vkCreateSwapchainKHR: ") + ResultName(r);
        return false;
    }
    DestroySwapchainObjects();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = created;
    swapFormat = chosen.format;
    swapExtent = extent;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    swapImages.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapImages.data());
    swapViews.resize(count);
    renderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vi = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        vi.image = swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFormat;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(device, &vi, nullptr, &swapViews[i]);
        VkSemaphoreCreateInfo si = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        vkCreateSemaphore(device, &si, nullptr, &renderFinished[i]);
    }
    swapchainDirty = false;
    imageAcquired = false;
    DrawTargetsChanged();
    std::fprintf(stderr, "[Vulkan] swap chain %ux%u, %u images, format %d, present mode %d\n", extent.width,
                 extent.height, count, static_cast<int>(swapFormat), static_cast<int>(presentMode));
    return true;
}

// Options Apply (resolution / fullscreen) and a user drag-resize raise settingsChanged. The DX11
// backend answers with Dx11_DeviceData_Resize: Device::AdjustDisplay sizes the real window to the
// engine's back buffer, then the buffers are rebuilt. Here (as on Metal): size the window if the
// engine's size differs from it or fullscreen flipped, publish the REAL pixel size as the back
// buffer, then rebuild the swap chain and every dynamic target.
void ApplyEngineSettingsChange(Video::Device* dev) {
    auto w = GameWindow();
    if (!w || (SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED)) return;
    auto cfg = Video::Configuration::_singleton;
    const int fs = cfg && cfg->fullscreen ? 1 : 0;
    int cw = 0;
    int ch = 0;
    SDL_GetWindowSizeInPixels(w, &cw, &ch);
    const bool fsFlip = lastFullscreen >= 0 && lastFullscreen != fs;
    lastFullscreen = fs;
    if (fsFlip || dev->backBufferWidth != cw || dev->backBufferHeight != ch) {
        const int wantW = dev->backBufferWidth;
        const int wantH = dev->backBufferHeight;
        dev->AdjustDisplay();
        SDL_SyncWindow(w);
        SDL_GetWindowSizeInPixels(w, &cw, &ch);
        dev->backBufferWidth = cw;
        dev->backBufferHeight = ch;
        if (cfg && !fs) {
            cfg->windowWidth = cw;
            cfg->windowHeight = ch;
        }
        std::fprintf(stderr, "[Vulkan] resize: wanted %dx%d px fullscreen=%d -> window %dx%d px\n", wantW, wantH,
                     fs, cw, ch);
    }
    if (cw > 0 && ch > 0 && device) {
        vkDeviceWaitIdle(device);
        // Apply also changes shadowScalar without changing the window: recreate every dynamic
        // image / render buffer so the engine's extents agree with the new back buffer.
        dev->ReleaseDynamic();
        dev->RecreateDynamic();
        std::string why;
        if (!CreateSwapchain(w, &why)) {
            std::fprintf(stderr, "[Vulkan] swap chain rebuild failed: %s\n", why.c_str());
            swapchainDirty = true;
        }
    }
}

void DestroyFrames() {
    for (Frame& f : frames) {
        if (f.done) vkDestroyFence(device, f.done, nullptr);
        if (f.imageAvailable) vkDestroySemaphore(device, f.imageAvailable, nullptr);
        if (f.pool) vkDestroyCommandPool(device, f.pool, nullptr);
        f = Frame{};
    }
}

bool CreateFrames(std::string* why) {
    for (Frame& f : frames) {
        auto pi = Make<VkCommandPoolCreateInfo>(
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = graphicsFamily;
        if (vkCreateCommandPool(device, &pi, nullptr, &f.pool) != VK_SUCCESS) {
            *why = "vkCreateCommandPool failed";
            return false;
        }
        auto ai = Make<VkCommandBufferAllocateInfo>(
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &ai, &f.commands) != VK_SUCCESS) {
            *why = "vkAllocateCommandBuffers failed";
            return false;
        }
        VkFenceCreateInfo fi = Make<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device, &fi, nullptr, &f.done);
        VkSemaphoreCreateInfo si = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        vkCreateSemaphore(device, &si, nullptr, &f.imageAvailable);
    }
    return true;
}

}  // namespace

// ---- the entry points (vulkan_entry.h) ----------------------------------------------------------

::Video::DeviceData* NewDeviceData(::Video::Device* dev) {
    return new DeviceData(dev);
}

bool DeviceLive() {
    return device != VK_NULL_HANDLE;
}

// ---- the live device (device_objects.h) ---------------------------------------------------------

VkDevice LiveDevice() {
    return device;
}

VkPhysicalDevice LivePhysical() {
    return physical;
}

VkQueue LiveQueue() {
    return queue;
}

uint32_t LiveGraphicsFamily() {
    return graphicsFamily;
}

const VkPhysicalDeviceProperties& LiveProperties() {
    return deviceProperties;
}

const Caps& LiveCaps() {
    return caps;
}

VkInstance LiveInstance() {
    return instance;
}

VkFormat DepthStencilFormat() {
    return caps.d24s8 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkFormat SwapchainFormat() {
    return swapFormat;
}

// ---- device objects (device_objects.h) ----------------------------------------------------------

VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv, std::string* why) {
    std::lock_guard<std::mutex> guard(moduleLock);
    if (device == VK_NULL_HANDLE) {
        if (why) *why = "no live device";
        return VK_NULL_HANDLE;
    }
    auto ci = Make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    ci.codeSize = spirv.size() * sizeof(uint32_t);
    ci.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult r = vkCreateShaderModule(device, &ci, nullptr, &module);
    if (r != VK_SUCCESS) {
        if (why) *why = std::string("vkCreateShaderModule: ") + ResultName(r);
        return VK_NULL_HANDLE;
    }
    liveModules.insert(module);
    return module;
}

void DestroyShaderModule(VkShaderModule module) {
    if (module == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> guard(moduleLock);
    if (liveModules.erase(module) == 0 || device == VK_NULL_HANDLE) return;
    vkDestroyShaderModule(device, module, nullptr);
}

// One GraphicsInterface with one Adapter per physical device (dxgiAdapterIndex = its index in
// physicalDevices; CreateDevice reads it back from Device::dxgiAdapterIndex). The mode list is
// the usable screen's, as on Metal: there is no DXGI output to ask.
bool Enumerate(Video::Device* dev, const char* name) {
    using D = Video::Device;
    using A = System::MemoryAllocator;
    std::string why;
    if (!EnsureInstance(&why)) {
        std::fprintf(stderr, "[Vulkan] no adapters listed: %s\n", why.c_str());
        return false;
    }
    auto old = dev->interfaceData;
    const int n = dev->interfaceCount;
    auto list = static_cast<D::GraphicsInterface*>(A::MemAlloc(sizeof(D::GraphicsInterface) * (n + 1), 4));
    for (int i = 0; i < n; ++i) {
        new (list + i) D::GraphicsInterface(old[i]);
        old[i].~GraphicsInterface();
    }
    A::MemFree(old);
    new (list + n) D::GraphicsInterface();
    auto& g = list[n];
    g.name.Set(name);
    const int adapterCount = static_cast<int>(physicalDevices.size());
    g.adapterData = static_cast<D::Adapter*>(A::MemAlloc(sizeof(D::Adapter) * adapterCount, 4));
    g.adapterCount = g.adapterCapacity = adapterCount;

    int mw = 0;
    int mh = 0;
    int nw = 0;
    int nh = 0;
    Platform::Window::QueryScreenForModes(&mw, &mh, &nw, &nh);
    int32_t sizes[32][2];
    const int modeCount = Platform::Window::BuildResolutionList(mw, mh, nw, nh, sizes, 32);
    const D::RefreshRate rate{ 60, 1 };

    for (int i = 0; i < adapterCount; ++i) {
        auto& a = *new (g.adapterData + i) D::Adapter();
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevices[i], &props);
        a.name.Set(props.deviceName);
        a.dxgiAdapterIndex = i;
        a.dxgiOutputIndex = 0;
        a.maxFilterLevel = std::min(16, static_cast<int>(props.limits.maxSamplerAnisotropy));   // dx11: 16
        // Array<bool>: multisampleData[k] is "k+1 samples works" for k = 0..7.
        a.multisampleData = static_cast<uint8_t*>(A::MemAlloc(8, 4));
        std::memset(a.multisampleData, 0, 8);
        a.multisampleData[0] = 1;
        a.multisampleCount = a.multisampleCap = 8;
        const VkSampleCountFlags samples =
            props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
        for (unsigned s : { 2u, 4u, 8u }) {
            if (samples & static_cast<VkSampleCountFlags>(s)) a.multisampleData[s - 1] = 1;
        }
        for (int m = 0; m < modeCount; ++m) {
            D::DisplayMode mode{};
            mode.width = sizes[m][0];
            mode.height = sizes[m][1];
            a.FindOrAddMode(mode, rate);
        }
    }
    dev->interfaceData = list;
    dev->interfaceCount = dev->interfaceCapacity = n + 1;
    return true;
}

// ---- DeviceData ---------------------------------------------------------------------------------

bool DeviceData::EnumerateInterfaces() {
    return Enumerate(_owner, "Vulkan");
}

bool DeviceData::CreateDevice() {
    ::Video::Device* dev = _owner;
    StallDebug::Mark("  Vulkan CreateDevice: entry");
    std::string why;
    if (!EnsureInstance(&why)) return Fail("Vulkan: " + why);

    // 1 -- the adapter the configuration picked (Adapter::dxgiAdapterIndex, copied to the device).
    int adapterIndex = dev->dxgiAdapterIndex;
    if (adapterIndex < 0 || adapterIndex >= static_cast<int>(physicalDevices.size())) adapterIndex = 0;
    physical = physicalDevices[static_cast<size_t>(adapterIndex)];
    vkGetPhysicalDeviceProperties(physical, &deviceProperties);
    caps.apiVersion = deviceProperties.apiVersion;
    if (deviceProperties.apiVersion < VK_API_VERSION_1_3) {
        return Fail(std::string("Vulkan: ") + deviceProperties.deviceName + " reports Vulkan " +
                    std::to_string(VK_API_VERSION_MAJOR(deviceProperties.apiVersion)) + "." +
                    std::to_string(VK_API_VERSION_MINOR(deviceProperties.apiVersion)) +
                    "; OpenBA's Vulkan renderer needs 1.3");
    }

    // 2 -- the floor: dynamic rendering and synchronization2 (1.3 core features, still optional bits).
    auto have13 = Make<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    auto have12 = Make<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    have13.pNext = &have12;
    auto have2 = Make<VkPhysicalDeviceFeatures2>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    have2.pNext = &have13;
    vkGetPhysicalDeviceFeatures2(physical, &have2);
    if (!have13.dynamicRendering || !have13.synchronization2) {
        return Fail("Vulkan: the driver lacks dynamicRendering or synchronization2 (Vulkan 1.3 core)");
    }
    if (!have12.timelineSemaphore) {
        return Fail("Vulkan: the driver lacks timelineSemaphore (Vulkan 1.2 core)");
    }
    if (!HasDeviceExtension(physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return Fail("Vulkan: the device has no VK_KHR_swapchain");
    }
    caps.pushDescriptor = HasDeviceExtension(physical, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    caps.vertexInputDynamicState =
        HasDeviceExtension(physical, VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
    caps.extendedDynamicState2 = HasDeviceExtension(physical, VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
    caps.loadStoreOpNone = HasDeviceExtension(physical, VK_KHR_LOAD_STORE_OP_NONE_EXTENSION_NAME) ||
                           HasDeviceExtension(physical, VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME);
    {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
        caps.d24s8 = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    }
    caps.colorSamples = deviceProperties.limits.framebufferColorSampleCounts;
    caps.samplerAnisotropy = have2.features.samplerAnisotropy != 0;
    caps.fillModeNonSolid = have2.features.fillModeNonSolid != 0;
    caps.textureCompressionBC = have2.features.textureCompressionBC != 0;

    // 3 -- the graphics queue family (it must also present to the surface, checked after the surface).
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qCount, families.data());
    graphicsFamily = qCount;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
            break;
        }
    }
    if (graphicsFamily == qCount) return Fail("Vulkan: no graphics queue family");
    StallDebug::Mark("  Vulkan adapter + features");

    // 4 / 5 -- the back buffer from the selected mode, then the windowed override (dx11 steps 4-5).
    ::Video::Configuration* cfg = Video::Configuration::_singleton;
    if (dev->modeData && dev->displayModeIndex >= 0 && dev->displayModeIndex < dev->modeCount) {
        dev->backBufferWidth = dev->modeData[dev->displayModeIndex].width;
        dev->backBufferHeight = dev->modeData[dev->displayModeIndex].height;
    }
    if (cfg && cfg->fullscreen == 0) {
        if (cfg->windowWidth >= 0) dev->backBufferWidth = cfg->windowWidth;
        if (cfg->windowHeight >= 0) dev->backBufferHeight = cfg->windowHeight;
    }
    if (Opt().background) {
        dev->backBufferWidth = 640;
        dev->backBufferHeight = 360;
    }
    auto w = GameWindow();
    if (!w) return Fail("Vulkan: no game window");

    // 6 -- the deferred first show (src/app/boot_window.cpp) or the shipped inline ShowWindow.
    BootWindow::ArmDeferredShow(cfg, dev->windowHandle);
    if (!cfg || cfg->field42 == 0) {
        Platform::Window::ShowDefault(dev->windowHandle);
        StallDebug::Mark("  Vulkan ShowWindow  <== WINDOW NOW VISIBLE");
    }
    // 7 -- no DXGI output: the monitor stays unset and AdjustDisplay pins fullscreen to the origin.
    dev->monitor = nullptr;
    // 8 -- settle the window to the back buffer, then publish the real pixel size.
    dev->AdjustDisplay();
    SDL_SyncWindow(w);
    SDL_GetWindowSizeInPixels(w, &dev->backBufferWidth, &dev->backBufferHeight);
    lastFullscreen = cfg && cfg->fullscreen ? 1 : 0;
    StallDebug::Mark("  Vulkan AdjustDisplay (window sized)");

    // 9 -- the surface, and the queue's right to present to it.
    if (!SDL_Vulkan_CreateSurface(w, instance, nullptr, &surface)) {
        surface = VK_NULL_HANDLE;
        return Fail(std::string("Vulkan: SDL_Vulkan_CreateSurface: ") + SDL_GetError());
    }
    VkBool32 presentable = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical, graphicsFamily, surface, &presentable);
    if (!presentable) return Fail("Vulkan: the graphics queue cannot present to the window");

    // 10 -- the device.
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = Make<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qi.queueFamilyIndex = graphicsFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    std::vector<const char*> extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (caps.pushDescriptor) extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (caps.vertexInputDynamicState) extensions.push_back(VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME);
    if (caps.extendedDynamicState2) extensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
    auto want13 = Make<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    want13.dynamicRendering = VK_TRUE;
    want13.synchronization2 = VK_TRUE;
    auto want12 = Make<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    want12.timelineSemaphore = have12.timelineSemaphore;
    want12.bufferDeviceAddress = have12.bufferDeviceAddress;
    want12.vulkanMemoryModel = have12.vulkanMemoryModel;
    want12.vulkanMemoryModelDeviceScope = have12.vulkanMemoryModelDeviceScope;
    want13.pNext = &want12;
    auto wantVids = Make<VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT);
    wantVids.vertexInputDynamicState = VK_TRUE;
    auto wantEds2 = Make<VkPhysicalDeviceExtendedDynamicState2FeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT);
    wantEds2.extendedDynamicState2 = VK_TRUE;
    void** tail = &want12.pNext;
    if (caps.vertexInputDynamicState) {
        *tail = &wantVids;
        tail = &wantVids.pNext;
    }
    if (caps.extendedDynamicState2) {
        *tail = &wantEds2;
        tail = &wantEds2.pNext;
    }
    auto want2 = Make<VkPhysicalDeviceFeatures2>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
    want2.pNext = &want13;
    want2.features.samplerAnisotropy = have2.features.samplerAnisotropy;
    want2.features.textureCompressionBC = have2.features.textureCompressionBC;
    want2.features.fillModeNonSolid = have2.features.fillModeNonSolid;
    want2.features.independentBlend = have2.features.independentBlend;
    want2.features.depthClamp = have2.features.depthClamp;
    want2.features.shaderClipDistance = have2.features.shaderClipDistance;
    VkDeviceCreateInfo di = Make<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    di.pNext = &want2;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    di.ppEnabledExtensionNames = extensions.data();
    VkResult r = vkCreateDevice(physical, &di, nullptr, &device);
    if (r != VK_SUCCESS) {
        device = VK_NULL_HANDLE;
        return Fail(std::string("Vulkan: vkCreateDevice: ") + ResultName(r));
    }
    if (!LoadDeviceFunctions(device, &why)) return Fail("Vulkan: " + why);
    vkGetDeviceQueue(device, graphicsFamily, 0, &queue);
    StallDebug::Mark("  Vulkan vkCreateDevice");

    // 11 -- the frames and the swap chain.
    if (!CreateFrames(&why)) return Fail("Vulkan: " + why);
    if (!CreateSwapchain(w, &why)) return Fail("Vulkan: " + why);
    frameSlot = 0;
    frameCount = 0;
    dev->settingsChanged = 0;
    StallDebug::Mark("  Vulkan swap chain");

    // 12 -- the resource layer (P2): memory and submission, the pipeline layout and cache, the draw state.
    if (!ResourcesStartup(&why)) return Fail("Vulkan: " + why);
    resourcesUp = true;
    if (!PipelinesStartup(&why)) return Fail("Vulkan: " + why);
    if (!DrawStartup(&why)) return Fail("Vulkan: " + why);
    StallDebug::Mark("  Vulkan resources");

    std::fprintf(stderr,
                 "[Vulkan] device %s (%s) api %u.%u.%u driver 0x%x created; push_descriptor=%d "
                 "vertex_input_dynamic=%d eds2=%d load_store_none=%d d24s8=%d validation=%d\n",
                 deviceProperties.deviceName, DeviceTypeName(deviceProperties.deviceType),
                 VK_API_VERSION_MAJOR(caps.apiVersion), VK_API_VERSION_MINOR(caps.apiVersion),
                 VK_API_VERSION_PATCH(caps.apiVersion), deviceProperties.driverVersion, caps.pushDescriptor,
                 caps.vertexInputDynamicState, caps.extendedDynamicState2, caps.loadStoreOpNone, caps.d24s8,
                 caps.validation);
    return true;
}

void DeviceData::Shutdown() {
    if (device) {
        vkDeviceWaitIdle(device);
        if (resourcesUp) {
            // The draw state, the pipelines (their cache is written here), then everything the
            // companions still hold (resources.h); the shader modules after them, below.
            DrawShutdown();
            PipelinesShutdown();
            ResourcesShutdown();
            resourcesUp = false;
        }
        DestroySwapchainObjects();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
        DestroyFrames();
        {
            // Programs still alive at shutdown: their modules die with the device (device_objects.h).
            std::lock_guard<std::mutex> guard(moduleLock);
            if (!liveModules.empty()) {
                std::fprintf(stderr, "[Vulkan] shutdown: %zu shader module(s) still held, destroyed with the device\n",
                             liveModules.size());
            }
            for (VkShaderModule module : liveModules)
                vkDestroyShaderModule(device, module, nullptr);
            liveModules.clear();
        }
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        queue = VK_NULL_HANDLE;
    }
    if (surface) {
        SDL_Vulkan_DestroySurface(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    physical = VK_NULL_HANDLE;
    imageAcquired = false;
    swapchainDirty = false;
    // The instance (and its messenger) stay for the process: Enumerate's adapter list and a
    // device rebuild reuse them.
}

// The loader bracket (dx11 devicedata_stubs.cpp Slot4): retail's body is empty; the extensions'
// load-end summaries hook here on the other backends (none is supported yet). At a load's end the
// backend's own program summary (companions.cpp) is logged.
void DeviceData::Slot4(bool loading) {
    if (!loading) LogProgramSummary();
}

float DeviceData::Slot5() {
    return 0.0f;
}

bool DeviceData::Slot6() {
    return false;
}

bool DeviceData::Slot7() {
    return true;
}

// Retail slot 8 (dx11 devicedata.cpp): the game cursor from a 32x32 ImageBuffer. It needs the
// texture read-back (ImageBufferData::Read), which is P3 with the render targets; until then the
// cursor stays the platform's. The Metal body is the template.
void DeviceData::SetImageBuffer(Video::ImageBuffer* imageBuffer, int hotX, int hotY) {
    (void)imageBuffer;
    (void)hotX;
    (void)hotY;
}

Video::ContextData* DeviceData::CreateContextData(Video::Context* owner) {
    return new ContextData(owner);
}
Video::ImageBufferData* DeviceData::CreateImageBufferData(Video::ImageBuffer* owner) {
    return new ImageBufferData(owner);
}
Video::ConstantBufferData* DeviceData::CreateConstantBufferData(Video::ConstantBuffer* owner) {
    return new ConstantBufferData(owner);
}
Video::IndexBufferData* DeviceData::CreateIndexBufferData(Video::IndexBuffer* owner) {
    return new IndexBufferData(owner);
}
Video::VertexBufferData* DeviceData::CreateVertexBufferData(Video::VertexBuffer* owner) {
    return new VertexBufferData(owner);
}
Video::VertexLayoutData* DeviceData::CreateVertexLayoutData(Video::VertexLayout* owner) {
    return new VertexLayoutData(owner);
}
Video::VertexProgramData* DeviceData::CreateVertexProgramData(Video::VertexProgram* owner) {
    return new VertexProgramData(owner);
}
Video::PixelProgramData* DeviceData::CreatePixelProgramData(Video::PixelProgram* owner) {
    return new PixelProgramData(owner);
}
Video::RenderStateData* DeviceData::CreateRenderStateData(Video::RenderState* owner) {
    return new RenderStateData(owner);
}
Video::VideoProgramData* DeviceData::CreateVideoProgramData(Video::VideoProgram* owner) {
    return new VideoProgramData(owner);
}
Video::RenderBufferData* DeviceData::CreateRenderBufferData(Video::RenderBuffer* owner) {
    return new RenderBufferData(owner);
}

// ---- ContextData: the frame loop ----------------------------------------------------------------

// Slot 1 (dx11 contextdata.cpp 0x180001650). A settings change is answered and the frame refused,
// as retail does (Dx11_DeviceData_Resize, then `return false`); the next frame begins normally.
bool ContextData::BeginCommands() {
    ::Video::Context* const ctx = _owner;
    ::Video::Device* const dev = ctx->_device;
    if (!device) return false;

    ctx->_trianglesSubmitted = 0;
    ctx->_verticesSubmitted = 0;
    ctx->_drawsSubmitted = 0;
    ctx->_objectsSubmitted = 0;

    if (dev->settingsChanged != 0) {
        dev->Lock();
        ApplyEngineSettingsChange(dev);
        dev->settingsChanged = 0;
        dev->Unlock();
        return false;
    }
    auto w = GameWindow();
    if (swapchainDirty) {
        vkDeviceWaitIdle(device);
        std::string why;
        if (!CreateSwapchain(w, &why)) {
            return false;   // minimised, or the surface is gone: try again next frame
        }
    }

    Frame& f = frames[frameSlot];
    vkWaitForFences(device, 1, &f.done, VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE,
                                       &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        swapchainDirty = true;
        return false;
    }
    if (r == VK_SUBOPTIMAL_KHR) {
        swapchainDirty = true;   // present this frame, rebuild before the next
    } else if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkAcquireNextImageKHR: %s\n", ResultName(r));
        return false;
    }
    imageAcquired = true;
    vkResetFences(device, 1, &f.done);
    vkResetCommandPool(device, f.pool, 0);
    VkCommandBufferBeginInfo bi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.commands, &bi);

    dev->Lock();
    // The slot's previous frame completed (the fence above): its ring, pools and the objects
    // released before it can go.
    CollectRetired();
    FrameTarget target;
    target.cb = f.commands;
    target.slot = frameSlot;
    target.image = swapImages[imageIndex];
    target.view = swapViews[imageIndex];
    target.extent = swapExtent;
    DrawBeginFrame(target);
    ctx->SetRenderBuffer(ctx->_defaultRenderBuffer);
    Video::Viewport vp{};
    vp.x = 0;
    vp.y = 0;
    vp.width = dev->backBufferWidth;
    vp.height = dev->backBufferHeight;
    ctx->SetViewport(vp);
    return true;
}

void ContextData::EndCommands() {
}

// Slot 3: end the pass and move the image to PRESENT_SRC (draw.cpp), drop the device lock, submit the
// upload batch and then the frame (resources.h: one queue, the timeline), present.
void ContextData::Finish() {
    ::Video::Device* const dev = _owner->_device;
    if (!device || !imageAcquired) {
        dev->Unlock();
        return;
    }
    Frame& f = frames[frameSlot];
    DrawEndFrame();
    vkEndCommandBuffer(f.commands);
    const bool logFrame = frameCount == 0 || (frameCount + 1) % 300 == 0;
    if (logFrame) DrawLogFrame(frameCount + 1, _owner);

    // The DX11 order: the device lock goes before the present, so a loader thread linking a new
    // resource never waits out the presentation.
    dev->Unlock();

    FlushUploads();
    VkSemaphoreSubmitInfo wait = Make<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    wait.semaphore = f.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const uint64_t serial = SubmitCommands(f.commands, &wait, renderFinished[imageIndex], f.done);
    NoteFrameSubmitted(serial);

    VkPresentInfoKHR pi = Make<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;
    VkResult r = PresentQueue(pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        swapchainDirty = true;
    } else if (r != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] vkQueuePresentKHR: %s\n", ResultName(r));
    }
    imageAcquired = false;
    frameSlot = (frameSlot + 1) % kFramesInFlight;
    ++frameCount;
}

// ---- the main targets ---------------------------------------------------------------------------
// RenderBuffer::Create on the default render buffer calls these on its colour and depth images:
// the companion publishes the back-buffer shape through the owner's fields (dx11
// imagebufferdata.cpp AttachColorBuffer / AttachDepthBuffer: format 5 = BGRA8, 7 = D24S8; kind
// 0 = render target, 1 = depth) and marks itself a main target: the draws aim at the swap-chain
// image and the backend's depth image (draw.cpp), and a Bind of either as a texture reads the
// placeholder.

void ImageBufferData::AttachColorBuffer() {
    mainTarget = MainTarget::Color;
    auto dev = Video::Device::_singleton;
    _owner->_width = dev->backBufferWidth;
    _owner->_height = dev->backBufferHeight;
    _owner->_dimension = 0;
    _owner->_kind = 0;
    _owner->_format = 5;
    _owner->_flags = 0x10;
}

void ImageBufferData::AttachDepthBuffer() {
    mainTarget = MainTarget::Depth;
    if (_owner->Width() <= 0 || _owner->Height() <= 0) {
        auto dev = Video::Device::_singleton;
        _owner->_width = dev->backBufferWidth;
        _owner->_height = dev->backBufferHeight;
    }
    _owner->_dimension = 0;
    _owner->_kind = 1;
    _owner->_format = 7;
}

// dx11: OMSetRenderTargets(colour, depth). Here the render buffer becomes the draw target (draw.h:
// P2 draws only into the main one).
void RenderBufferData::Bind(void* context) {
    (void)context;
    DrawSetRenderBuffer(_owner);
}

// dx11: ResolveSubresource from `other`'s colour into this one's. P3 (render targets).
void RenderBufferData::Resolve(void* context, Video::RenderBuffer* other, bool flag) {
    (void)context;
    (void)other;
    (void)flag;
    static bool logged = false;
    if (!logged) {
        logged = true;
        std::fprintf(stderr, "[Vulkan] RenderBuffer::Resolve is P3: skipped\n");
    }
}

}  // namespace VideoVulkan
