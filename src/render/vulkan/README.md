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
| `resources.h`, `resources.cpp` | GPU memory (VMA buffers and images, multisampled and attachment-only ones for the main targets), submission (one queue, the timeline semaphore), the upload batch, the frame-indexed retire queue, shared samplers, the placeholders, the read-back buffer, and `ImageState` / `TransitionImage`: per-image layout tracking with sync2 barriers. |
| `pipelines.h`, `pipelines.cpp` | The pipeline layout, the shader registry, vertex layouts, the render-state block, the pipeline key and cache, the persistent `VkPipelineCache` and the warm list with its worker. |
| `draw.h`, `draw.cpp` | The bound state, the render targets (the main targets: swap-chain image, MSAA colour + depth; every render buffer's own images), the lazily-begun pass with its clears as load ops, layout transitions at pass boundaries, Resolve (the frame copy) and the image read-back, the constant ring, descriptor sets, and the ContextData draw slots (Clear, the Bind* slots, DrawIndexed, the dynamic-vertex slots). |
| `vk_api.h`, `vk_api.cpp` | The entry points the backend calls, as function pointers resolved at run time from SDL's `vkGetInstanceProcAddr` (nothing links the loader). Three X-lists: global, instance, device. |
| `VideoVulkanData.h` | The twelve companion subclasses of `src/engine/video/CompanionData.h`. |
| `device.cpp` | Instance, physical device, logical device, SDL surface, swap chain, the frame loop (`BeginCommands` / `Finish`, and `SubmitFrameSoFar` for a mid-frame read-back), the multisample count (dx11 ResolveMultisampleMode), the adapter enumeration, the main-target attach slots, `RenderBufferData::Bind` / `Resolve`, the game cursor (`SetImageBuffer`), the start and stop of the resource files. |
| `companions.cpp` | Buffers (static device-local, dynamic renamed on DISCARD), images (every retail format, mips, cubes; a render-buffer image's attachment view and layout state), the image read-back slot, constant buffers, programs (through the shader registry), vertex layouts, render states. |
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
main one (shadow map, water reflection, frame copy) were skipped in P2.
P3 (render targets, done): every render buffer renders into its own colour / depth images (the 1264x672
reflection, the depth-only shadow map, the minimap capture), the frame copy's Resolve copies the main
colour, `ImageBufferData::Read` reads back (the game cursor, the minimap, `TakeScreenshot`'s BMP), the
Options multisample count drives the main targets (resolve attachments on desktop; the tiler path
`VK_EXT_multisampled_render_to_single_sampled` where the device has it), and a settings change or a
window resize remakes the main targets. A new game boots, plays and quits through the pause menu on
the debug and perf flavours; an in-game RenderDoc capture under `/profile:faithful` at a 60 fps cap
has the DX11 exe's draw count per target. The divergences left are in `docs/known_port_issues.md`
("Vulkan backend"). Next: P4 ext seams and the parity table.

P4 (ext seams): every backend-neutral seam answers "supported" (`docs/vulkan/PARITY.md` has the table
beside DX11 and the retail features). `ext.ambient_occlusion` runs ASSAO from the committed DXBC through
the same converter (`ao/assao_pass.cpp`; typed UAVs map to binding 40 + u, `kMappingVersion` 3) over a
main depth that gets a sampled view on request. foliage_aa, billboard_shadow and draw_batch's smoke VS
convert their rewritten DXBC into second registry entries (`shader_rewrites.cpp`; foliage_aa's A2C clone
is two bits of the pipeline key's render state). async_gpu_create, share_gpu_data, create_on_demand,
compress_ao and texture_quality hook the companions as on DX11 (`seeded_create.h` is the load's one
create: BC4, then create-or-share; seeded static buffers are immutable, as D3D11 makes them).
`VK_ERROR_DEVICE_LOST` from submit, wait, acquire or present begins an empty frame whose Finish runs
dx11's RecoverLostDevice sequence (OnVideoError, device rebuilt, OnVideoRestart); `ext.render_pipe`
splits it into `PresentFrame` and `RecoverLostDevice`, and `fix.boot_device_lost` sends a swap-chain
rebuild that fails on a lost device into the same recovery. `BANISHED_FORCE_DEVICE_LOST` injects the loss
on this backend too. Next: P5 (Linux).

## Validation on the Windows bed
The debug flavour enables `VK_LAYER_KHRONOS_validation` when the loader lists it. With the LunarG Vulkan
SDK installed, its layer is registered with the loader and used as is (no `VK_ADD_LAYER_PATH`; the
SDK's `Bin\spirv-val.exe` serves `vulkan-shader-check --spirv-val`). With no Vulkan SDK
installed, a layer built from Khronos's source with the MinGW toolchain works: build
`KhronosGroup/Vulkan-ValidationLayers` (tag matching the loader) with `scripts/update_deps.py` and
CMake + Ninja in a SHORT path (the dependency builds exceed MAX_PATH under the session scratchpad),
link it with `-DCMAKE_MODULE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"` (the loader does
not find MinGW's runtime DLLs beside the layer), then run with `VK_ADD_LAYER_PATH=<build>\layers`;
`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1` adds synchronization validation. Messages land in
`standalone_error.log` as `[Vulkan] validation ...`.

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
