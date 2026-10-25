// ============================================================================
// src/render/vulkan/vma_impl.cpp -- the one translation unit that compiles VMA's implementation
// ============================================================================
// NOT RETAIL. vk_mem.h has the configuration; backend.cmake builds this file with -w (upstream's
// warnings are not ours to fix, as for dxbc-spirv and Dear ImGui).
// ============================================================================
#define VMA_IMPLEMENTATION
#include "vk_mem.h"
