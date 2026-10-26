// ============================================================================
// tests/unit/vulkan_ao_convert_test.cpp -- ext.ambient_occlusion's programs through the Vulkan
// converter (src/render/vulkan/shader_convert.{h,cpp}, P4)
// ============================================================================
// BUILT ONLY WITH THE VULKAN BACKEND (BANISHED_RENDER_BACKEND=vulkan): CMakeLists.txt filters
// tests/unit/vulkan_*_test.cpp out of every other configuration. Runs on any host (no device).
//
// The bytes are OUR OWN: ASSAO's HLSL compiled by tools/ao_shaders.py into
// src/render/common/ao/shaders/*.h (the arrays the DX11 backend creates its shaders from). The Vulkan
// backend converts the same arrays at load (src/render/vulkan/ao/assao_pass.cpp); this test checks the
// conversion and the one binding the AO programs add to the layout: PSPrepareDepthsAndNormals writes
// its normals through the typed UAV u4, which maps to BindingOfUav(4) of the pixel set.
// ============================================================================

#include "unit.h"

#include "render/vulkan/shader_convert.h"

#include "ao/shaders/PSApply.h"
#include "ao/shaders/PSGenerateQ2.h"
#include "ao/shaders/PSPrepareDepthsAndNormals.h"
#include "ao/shaders/PSResolveDepthMS.h"
#include "ao/shaders/VSFullscreen.h"

#include <map>
#include <string>
#include <vector>

using namespace VideoVulkan::Shaders;

namespace {

bool Convert(const uint8_t* blob, size_t size, Stage stage, ShaderReflection* refl) {
    std::vector<uint32_t> spirv;
    std::string why;
    const bool ok = ConvertDxbc(blob, size, stage, &spirv, refl, &why);
    if (!ok) std::fprintf(stderr, "    convert failed: %s\n", why.c_str());
    return ok && !spirv.empty() && spirv[0] == 0x07230203u;   // the SPIR-V magic
}

}  // namespace

UNIT_TEST(vulkan_ao_uav_binding_formula) {
    CHECK_EQ(BindingOfUav(0), kBindingsPerSet);
    CHECK_EQ(BindingOfUav(4), kBindingsPerSet + 4);
}

UNIT_TEST(vulkan_ao_normals_uav) {
    ShaderReflection refl;
    CHECK(Convert(g_ao_PSPrepareDepthsAndNormals, sizeof(g_ao_PSPrepareDepthsAndNormals), Stage::Pixel, &refl));
    CHECK_EQ(refl.uavMask, 1u << 4);
    CHECK((refl.textureMask & 1u) != 0);
}

UNIT_TEST(vulkan_ao_programs_convert) {
    ShaderReflection refl;
    CHECK(Convert(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), Stage::Vertex, &refl));
    CHECK_EQ(refl.uavMask, 0u);
    CHECK(Convert(g_ao_PSResolveDepthMS, sizeof(g_ao_PSResolveDepthMS), Stage::Pixel, &refl));
    CHECK(Convert(g_ao_PSGenerateQ2, sizeof(g_ao_PSGenerateQ2), Stage::Pixel, &refl));
    CHECK_EQ(refl.uavMask, 0u);
    CHECK(Convert(g_ao_PSApply, sizeof(g_ao_PSApply), Stage::Pixel, &refl));
    // A program fed to the wrong stage is refused, not converted.
    CHECK(!Convert(g_ao_PSApply, sizeof(g_ao_PSApply), Stage::Vertex, &refl));
}

// P8 round 2 (src/render/vulkan/spirv_legalize.h): the Adreno 830 driver refused VSFullscreen's
// OpBitFieldInsert (vkCreateGraphicsPipelines VK_ERROR_UNKNOWN, "Shader compilation failed", so no AO
// on the S25). The converted VSFullscreen holds no OpBitFieldInsert: the shifts and masks instead.
UNIT_TEST(vulkan_ao_bitfield_insert_lowered) {
    std::vector<uint32_t> vs;
    ShaderReflection refl;
    std::string why;
    CHECK(ConvertDxbc(g_ao_VSFullscreen, sizeof(g_ao_VSFullscreen), Stage::Vertex, &vs, &refl, &why));
    std::map<uint32_t, size_t> ops;
    for (size_t at = 5; at < vs.size();) {
        const uint32_t words = vs[at] >> 16;
        if (words == 0) break;
        ++ops[vs[at] & 0xFFFF];
        at += words;
    }
    CHECK_EQ(ops[201], (size_t)0);    // OpBitFieldInsert
    CHECK(ops[196] >= 1);             // OpShiftLeftLogical
    CHECK(ops[197] >= 1);             // OpBitwiseOr
    CHECK(CheckSpirvStructure(vs, &why));
}
