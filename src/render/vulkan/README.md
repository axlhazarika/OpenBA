# src/render/vulkan/ — the Vulkan renderer backend

NOT RETAIL. The third backend behind `src/render/common/README.md`'s seam, picked with
`BANISHED_RENDER_BACKEND=vulkan`. Targets: Linux (replacing the dxvk-native route) and Android
(Adreno 8xx); Windows is the development bed. The design and the phase plan are
`docs/vulkan/PLAN.md`; this file is the map of the directory.

| File | What |
|---|---|
| `backend.cmake` | The CMake fragment: sources, the Vulkan-Headers include (`-isystem`), ready flag. |
| `vk_api.h`, `vk_api.cpp` | The entry points the backend calls, as function pointers resolved at run time from SDL's `vkGetInstanceProcAddr` (nothing links the loader). Three X-lists: global, instance, device. |
| `VideoVulkanData.h` | The twelve companion subclasses of `src/engine/video/CompanionData.h`. |
| `device.cpp` | Instance, physical device, logical device, SDL surface, swap chain, the frame loop (`BeginCommands` / `Clear` / `Finish`), the adapter enumeration, the main-target attach slots. |
| `companions.cpp` | Buffers, images, programs, layouts, states. **P0: stubs** that accept and draw nothing; each says what replaces it. |
| `integration.cpp` | The backend-neutral seams (`AoBackend::`, `FaaBackend::`, … , `Ext::RenderPipe::`, `Graphics::ExtAoAfterOpaque`): every answer is "not supported" until P4. |
| `render_backend_vulkan.cpp` | `Render::` entry points and the compatibility-slot placeholder. |
| `vulkan_entry.h` | What the plain entry file needs from `device.cpp` without a Vulkan header. |

## Where it stands
P0 (scaffold): the device comes up, the swap chain presents the engine's clear colour at the
engine's back-buffer size, resizes and fullscreen flips rebuild it, and the debug flavour runs
the validation layer when one is installed. No draw reaches the GPU. Next: P1 shaders
(`dxbc-spirv`), P2 resources and draws, P3 render targets (`docs/vulkan/PLAN.md` §5).

## Build and run (Windows dev bed)
```bash
BANISHED_RENDER_BACKEND=vulkan tools/build.sh run-menu      # build/cmake-debug-vulkan/OpenBA.exe
```
`tools/build.sh` keys the build directory on the backend, so the DX11 build is untouched. The
exe lands in the same game directory as the DX11 one (`BANISHED_DIR`), so one or the other is
deployed at a time. Log lines are tagged `[Vulkan]` in `standalone_error.log`.

## Rules of the directory
- Vulkan 1.3 core + `VK_KHR_swapchain` is the floor; anything else is probed into `caps` and
  used only when present (PLAN.md §4.1). The reference device is a Galaxy S25 (PLAN.md §2.1).
- No game content: shaders are converted from the user's packages at load and cached under the
  user's profile (PLAN.md §4.2).
- Nothing outside this directory names `VideoVulkan::`; a feature the backend cannot do answers
  "not supported" through the seam header, never a silent stub.
- One statement per line (`.claude/rules/style.md`).
