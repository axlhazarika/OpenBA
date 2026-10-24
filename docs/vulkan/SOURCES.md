# Sources for docs/vulkan/PLAN.md §3 (collected 2026-10-24)

## 3.1 dxvk-native / DXVK
- https://github.com/misyltoad/dxvk-native (archived; adopters list)
- https://www.gamingonlinux.com/2022/11/half-life-2-portal-portal-2-left-4-dead-2-all-get-upgraded-with-dxvk-20-vulkan/
- https://developer.valvesoftware.com/wiki/DXVK (Source 2 = native Vulkan)
- https://github.com/doitsujin/dxvk/blob/master/README.md (DXVK Native, DXVK_WSI_DRIVER)
- https://github.com/doitsujin/dxvk/releases/tag/v3.0 , https://github.com/doitsujin/dxvk/wiki/Driver-support (Vulkan 1.4 floor, "no first-party support for Android")
- https://github.com/doitsujin/dxvk/issues/5423 (ARM64EC: not planned)
- https://github.com/momo5502/sogen/pull/1522 (stock Adreno lacks DXVK requirements)
- https://packages.debian.org/en/sid/libdxvk-native-dev (glibc arm64 only)
- https://9to5linux.com/wine-6-0-officially-released-with-vulkan-backend-for-wined3d-this-is-whats-new

## 3.2 Real backends
- Dolphin: https://github.com/dolphin-emu/dolphin/pull/6399 , https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/ , https://dolphin-emu.org/blog/6V
- PCSX2: https://github.com/PCSX2/pcsx2/pull/5224
- DuckStation: https://github.com/stenzek/duckstation
- Xenia: https://github.com/xenia-project/xenia/commits/master
- Godot: https://docs.godotengine.org/en/stable/engine_details/architecture/internal_rendering_architecture.html , https://godotengine.org/article/rendering-acyclic-graph/
- Croteam GDC 2016: http://media.steampowered.com/apps/valve/2016/Practical_Development_for_Vulkan.pdf
- id Tech 6: https://gpuopen.com/learn/vulkan-and-doom/
- Unreal PSO precaching: https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine

## 3.3 Shader translation
- vkd3d-shader API header: https://raw.githubusercontent.com/wine-mirror/wine/master/libs/vkd3d/include/vkd3d_shader.h
- dxbc-spirv: https://github.com/doitsujin/dxbc-spirv (LICENSE = MIT, meson.build: C++17, no deps)
- Old DXVK DXBC compiler extraction: https://codeberg.org/redstrate/dxbc
- HLSLcc: https://github.com/Unity-Technologies/HLSLcc
- SPIRV-Cross reflection: https://github.com/KhronosGroup/SPIRV-Cross/wiki/Reflection-API-user-guide

## 3.4 Abstraction layers
- SDL3 GPU: https://wiki.libsdl.org/SDL3/CategoryGPU , https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader , https://wiki.libsdl.org/SDL3/SDL_CreateGPUDevice
- NVRHI: https://github.com/NVIDIA-RTX/NVRHI · Diligent: https://github.com/DiligentGraphics/DiligentCore · bgfx: https://github.com/bkaradzic/bgfx

## 3.5 Tiler / Android
- Qualcomm game developer guide: https://docs.qualcomm.com/bundle/publicresource/topics/80-78185-2/mobile_best_practices.html
- Khronos Vulkan-Samples performance READMEs: render_passes, msaa, pipeline_cache, surface_rotation, descriptor_management, pipeline_barriers under https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/performance
- Android pre-rotation: https://developer.android.com/games/optimize/vulkan-prerotation
- Android Vulkan baseline: https://developer.android.com/games/develop/vulkan/native-engine-support
- SDL surface-lost fix: https://discourse.libsdl.org/t/sdl-gpu-vulkan-handle-vk-error-surface-lost-khr-in-acquire-path/67125
- libadrenotools (Turnip loading): https://github.com/eden-emulator/libadrenotools
- Adreno 830 stock driver report: https://vulkan.gpuinfo.org/displayreport.php?id=40499 (our own device read: scratchpad s25_vkjson.json, summarised in PLAN.md §2.1)

## 3.6 Android plumbing
- SDL3 Android README: https://raw.githubusercontent.com/libsdl-org/SDL/main/docs/README-android.md
- NDK downloads / releases: https://developer.android.com/ndk/downloads , https://github.com/android/ndk/releases
- 16 KB pages: https://developer.android.com/guide/practices/page-sizes
- Vulkan platform rules: https://source.android.com/docs/core/graphics/implement-vulkan , https://developer.android.com/games/develop/vulkan/overview
- Android Vulkan profiles: https://developer.android.com/ndk/guides/graphics/android-baseline-profile
- Validation layers on device: https://developer.android.com/ndk/guides/graphics/validation-layer
- Pipeline cache serialisation: https://zeux.io/2019/07/17/serializing-pipeline-cache/ , https://corp.roblox.com/newsroom/2021/02/creating-a-robust-pipeline-cache-with-vulkan
- RenderDoc Android: https://renderdoc.org/docs/how/how_android_capture.html
- Snapdragon Profiler: https://docs.qualcomm.com/bundle/publicresource/80-80022-19/topics/graphics_developer_tools.md
- DevilutionX data folder recipe: https://github.com/glebm/devilutionX/blob/master/docs/installing.md
- ScummVM SAF move: https://scummvm.org/news/20230303
- LMK guidance: https://developer.android.com/games/optimize/vitals/lmk
- ndk-stack: https://developer.android.com/ndk/guides/ndk-stack · HWASan: https://developer.android.com/ndk/guides/hwasan , https://developer.android.com/ndk/guides/wrap-script
- S25 Vulkan reports: https://fauxvkdb.canonspike.com/device/samsung-sm-s931b

## 4.2 Shader compiler comparison (clones read 2026-10-24)
- dxbc-spirv 687a301: `spirv/spirv_mapping.h` (ResourceMapping), `dxbc/dxbc_api.h`, `tools/dxbc_compiler.cpp`, `spirv/spirv_builder.cpp` (emitMemoryModel, emitSampledImage)
- DXVK master cbd2206: `src/dxvk/dxvk_shader_ir.cpp` (LowerIoPass sequence ~L2033-2086, CompileOptions ~L2230-2269); `src/d3d11/d3d11_context.cpp` (negative viewport height)
- DXVK v2.6.2 / v2.7.1 `src/dxbc` (binding formulas; 2.7 sampler heap in dxbc_compiler.cpp emitSamplerArray)
- Codeberg extraction: https://codeberg.org/redstrate/dxbc (2a43b89, frozen ~DXVK 2.3)
