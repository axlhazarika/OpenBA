# Vulkan backend parity with DX11

What the Vulkan backend (`src/render/vulkan/`, `BANISHED_RENDER_BACKEND=vulkan`) does for each retail
renderer feature and each backend-neutral extension seam, beside the DX11 backend (`src/render/dx11/`).
As of P4 (2026-10-24, branch `vulkan/p0-scaffold`). Divergences that are not "the same" are listed in
`docs/known_port_issues.md` ("Vulkan backend"). Plan: `docs/vulkan/PLAN.md`.

"Verified" means seen on the Windows bed (RTX 5080, driver 1.4.357, debug flavour, Khronos validation
layer loaded) in an autostarted new game that quits through the pause menu with exit 0.

## Retail features

| Feature | DX11 | Vulkan | Notes |
|---|---|---|---|
| MSAA | Multisampled main target, resolved once a frame | Multisampled main colour and depth, resolve attachment at the end of each main pass; `VK_EXT_multisampled_render_to_single_sampled` where present | Same picture. Resolve per pass costs bandwidth; the tiler path runs on the S25 (P7, P8; with AO at 2x in P8). Sample count follows dx11's ResolveMultisampleMode. Verified at 4x and 1x. |
| Fullscreen | DXGI swap chain, exclusive or borderless per settings | SDL window fullscreen; the swap chain is rebuilt on the flip | No exclusive-mode probe (`Render::BootedExclusiveFullscreen` answers "not probed"). Fullscreen boot verified in P3. |
| VSync | Present interval 1 / 0 | FIFO when on; MAILBOX, then IMMEDIATE, then FIFO when off | Re-read on every swap-chain rebuild. |
| Shadows | Depth-only shadow map render buffer | Same render buffer as a depth image, depth-only pass | P3 RenderDoc capture: shadow 104 draws against DX11's 105 (one culling-order draw). The pixel shaders write `SV_Target` into no attachment (a validation warning, as D3D11 binds a null target). |
| Reflections | Water reflection render buffer (1264x672) | Same render buffer, its own colour and depth images | P3 capture: 79 / 79 draws. |
| Screenshots | `TakeScreenshot` reads the frame copy into a BMP | Same route: `RenderBuffer::Resolve` copies the main colour, `ImageBufferData::Read` reads back | Verified: correct BMPs (the AO on / off pair of P4). |
| Save thumbnails | `FrameCopyImageBuffer::Generate` + `Read` | Same route as screenshots | Covered by the resave gate's save path and the autosaves of every gate run. |
| Cursor | `DeviceData::SetImageBuffer` reads the cursor texture back | Same, through `Platform::Cursor::SetFromTexels` | Verified in P3. |
| Resize | `Device::settingsChanged` -> swap-chain resize, dynamic resources rebuilt | Swap chain rebuilt, main targets remade, dynamic resources rebuilt | Three resizes (menu, in game) verified in P3. |
| Device lost | Present's `DXGI_ERROR_DEVICE_REMOVED/RESET` -> RecoverLostDevice | `VK_ERROR_DEVICE_LOST` from submit, wait, acquire or present -> the same RecoverLostDevice sequence | P4. A lost frame is begun with nothing recorded and Finish runs the recovery, so the loop never spins. Forced loss (`BANISHED_FORCE_DEVICE_LOST`) verified in game on both Finish paths. A loss during the boot's world build crashes in the loader on both backends (the retail window, deferred). |

## Extension seams (`src/render/common/*_backend.h` and the two caller-side seams)

| Feature | DX11 | Vulkan | Notes |
|---|---|---|---|
| `ext.ambient_occlusion` (AoBackend, `Graphics::ExtAoAfterOpaque`) | ASSAO from the committed DXBC (`src/render/common/ao/shaders/`), readable D24S8 depth | Supported. The same committed DXBC through the same dxbc-spirv converter as the game's programs (`ao/assao_pass.cpp`); the main depth gets a sampled depth-aspect view on request | Route below. Verified at 4x. On the tiler MSAA path (msrtss) the single-sampled depth (sample 0) is read through the 1x resolve; verified on the S25 at 2x (P8). |
| `ext.foliage_aa` (FaaBackend) | DXBC rewrite -> second pixel shader; A2C clone of a non-blending state | Supported. Same rewrite, converted into a second registry entry; the A2C clone is two bits of the pipeline key | `shader_rewrites.cpp`. Verified mode 2 at 4x (coverage + fill). |
| `ext.billboard_shadow` (BbsBackend) | DXBC rewrite of the receiver vertex program | Supported, as above | Verified. |
| `ext.draw_batch` smoke VS (SmokeVsBackend) | DXBC rewrite of the particle vertex program | Supported, as above; `Rewritten` checks every permutation | Verified. |
| `ext.async_gpu_create` (AsyncGpuCreateBackend) | Static buffers and kind-2 textures created on workers inside the loader bracket | Supported. Same rule; VMA and the upload batch are thread-safe | `async_gpu_create/`. Verified with `create_on_demand` off. |
| `ext.share_gpu_data` (ShareGpuDataBackend) | Equal immutable buffers / textures share one object | Supported, over the same `GpuShare::Table` | `share_gpu_data/`. Verified (hits logged). |
| `ext.create_on_demand` (CreateOnDemandBackend) | Static buffers / textures made at first use, evicted when idle | Supported, over the same `LazyTable`; the draw holds the use gate while it reads the bound objects | `create_on_demand/`. Verified (prefetch and on-use). |
| `ext.compress_ao` (CompressAoBackend) | R8 AO maps loaded as BC4 | Supported where BC4 is sampleable (every desktop GPU) | `compress_ao/`. Verified (cache hits). |
| `ext.texture_quality` (TextureQualityBackend) | Top levels dropped, sampler MaxLOD lowered | Supported. Same drop in `ImageBufferData::Create`, both samplers' maxLod lowered | Verified Half (drop 1), eager and lazy. |
| `fix.boot_device_lost` (BootDeviceLostBackend) | A swap-chain reset failing on a removed device enters the recovery | Supported. A swap-chain rebuild failing with `VK_ERROR_DEVICE_LOST` enters the recovery | `boot_device_lost/`. Off: retried every frame, as DX11. |
| `ext.render_pipe` finish split (`Ext::RenderPipe::FinishPresent / FinishRecover`) | Present on the render thread, recovery on the main thread | Supported. `PresentFrame` on the render thread, `RecoverLostDevice` on the main thread | Verified with a forced loss. |
| Tweak overlay (`BANISHED_TWEAK_OVERLAY`) | Dear ImGui over D3D11 | Not supported (the option is DX11-only in CMake) | Debug tool, not a game feature. |

## The AO route

The ASSAO programs are our own HLSL compiled by `tools/ao_shaders.py` into the DXBC arrays in
`src/render/common/ao/shaders/*.h`, the bytes the DX11 backend creates its shaders from. The Vulkan
backend converts those arrays at load with `Shaders::LoadOrConvert` (cached per user, as every game
program is): no second shader source, no SPIR-V in the tree, no new tool. The converter's layout grew
one class for them: a typed UAV `u<n>` maps to binding `BindingOfUav(n)` = 40 + n of its stage's set
(`kMappingVersion` 3), used by `PSPrepareDepthsAndNormals*` for the normals target (`u4`). The pass
(`ao/assao_pass.cpp`) mirrors upstream `ASSAODX11.cpp` step for step: depth resolve (MS or 1x) to R32F,
the four half-resolution depth mips and normals, the quality-2 generate passes with their blurs into a
four-layer RG8 array, then the apply pass multiplied into the main colour (blend ZERO / SRC_COLOR,
depth off). The constant block is `UpdateConstants` field for field. It needs `fragmentStoresAndAtomics`
and `shaderStorageImageWriteWithoutFormat` (enabled when present; `AoBackend::Supported` is false
without them) and the R16F / RG8 / R32F / RGBA8-storage formats the device log line reports as
`ao=a/b/c`.
