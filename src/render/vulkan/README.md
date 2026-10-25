# src/render/vulkan/ — the Vulkan renderer backend

NOT RETAIL. The third backend behind `src/render/common/README.md`'s seam, picked with
`BANISHED_RENDER_BACKEND=vulkan`. Targets: Linux (replacing the dxvk-native route) and Android
(Adreno 8xx); Windows is the development bed. The design and the phase plan are
`docs/vulkan/PLAN.md`; this file is the map of the directory.

| File | What |
|---|---|
| `backend.cmake` | The CMake fragment: sources, the Vulkan-Headers include (`-isystem`), ready flag; the `dxbc_spv` static library (dxbc-spirv + SPIRV-Headers submodules); the unit additions; the `vulkan-shader-check` target. |
| `shader_convert.h`, `shader_convert.cpp` | DXBC to SPIR-V through dxbc-spirv, the binding layout (set 0 vertex, set 1 pixel; constant buffers, then textures, then samplers), the reflection, the VS -> PS semantic link, the cache key and payload. No Vulkan header. |
| `shader_cache.cpp` | `LoadOrConvert`: the SPIR-V cache over the per-user derived-data cache (`src/ext/derived_cache`). |
| `device_objects.h` | What the other files read from the device file: the live device, queue, properties, `Caps`, the depth and swap-chain formats; shader modules (tracked, destroyed at Shutdown). |
| `vk_mem.h`, `vma_impl.cpp` | VMA (`third_party/VulkanMemoryAllocator`), configured once (no static or dynamic function fetching, API 1.3); the implementation is compiled in `vma_impl.cpp`. |
| `resources.h`, `resources.cpp` | GPU memory (VMA buffers and images), submission (one queue, the timeline semaphore), the upload batch, the frame-indexed retire queue, shared samplers, the placeholders. |
| `pipelines.h`, `pipelines.cpp` | The pipeline layout, the shader registry, vertex layouts, the render-state block, the pipeline key and cache, the persistent `VkPipelineCache` and the warm list with its worker. |
| `draw.h`, `draw.cpp` | The bound state, the lazily-begun pass, the constant ring, descriptor sets, and the ContextData draw slots (Clear, the Bind* slots, DrawIndexed, the dynamic-vertex slots). |
| `vk_api.h`, `vk_api.cpp` | The entry points the backend calls, as function pointers resolved at run time from SDL's `vkGetInstanceProcAddr` (nothing links the loader). Three X-lists: global, instance, device. |
| `VideoVulkanData.h` | The twelve companion subclasses of `src/engine/video/CompanionData.h`. |
| `device.cpp` | Instance, physical device, logical device, SDL surface, swap chain, the frame loop (`BeginCommands` / `Finish`), the adapter enumeration, the main-target attach slots, the start and stop of the resource files. |
| `companions.cpp` | Buffers (static device-local, dynamic renamed on DISCARD), images (every retail format, mips, cubes), constant buffers, programs (through the shader registry), vertex layouts, render states. |
| `integration.cpp` | The backend-neutral seams (`AoBackend::`, `FaaBackend::`, … , `Ext::RenderPipe::`, `Graphics::ExtAoAfterOpaque`): every answer is "not supported" until P4. |
| `render_backend_vulkan.cpp` | `Render::` entry points and the compatibility-slot placeholder. |
| `vulkan_entry.h` | What the plain entry file needs from `device.cpp` without a Vulkan header. |

## Where it stands
P0 (scaffold): the device comes up, the swap chain presents the engine's clear colour at the
engine's back-buffer size, resizes and fullscreen flips rebuild it, and the debug flavour runs
the validation layer when one is installed.
P1 (shaders, done): every vertex and pixel program permutation is converted from the user's
DXBC to SPIR-V with dxbc-spirv and made a shader module; the SPIR-V is kept in the per-user
derived-data cache (`<Documents>\Banished\Cache`, none under the faithful profile). All 465
retail permutations convert and pass `spirv-val` (`vulkan-shader-check`, below).
P2 (resources and draws, done): buffers, textures, samplers, vertex layouts, render states, pipelines
(persistent cache plus a warm list) and descriptors are real, and the main menu renders into the
swap-chain image with the backend's depth image. A RenderDoc capture of the menu under
`/profile:faithful` has the same main-target draws as the DX11 exe's. Render buffers other than the
main one (shadow map, water reflection, frame copy) are skipped and counted, and `ImageBufferData::Read`
answers false. Next: P3 render targets (`docs/vulkan/PLAN.md` §5).

## Checking the shaders
```bash
cmake --build build/cmake-debug-vulkan -t vulkan-shader-check
build/cmake-debug-vulkan/vulkan-shader-check.exe "C:/GOG Games/Banished"   # [--spirv-val <exe>]
```
It reads `WinData/*.pkg` of the install named (parked mods in subfolders stay out), converts every
permutation, round-trips each cache payload and validates each module with `spirv-val` from the
Vulkan SDK or SPIRV-Tools when one is found (a structural check otherwise), then prints the table
and exits non-zero on any failure. It writes nothing but one temporary `.spv` in the system temp
folder.

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
