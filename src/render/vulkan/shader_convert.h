#pragma once
// ============================================================================
// src/render/vulkan/shader_convert.h -- DXBC to SPIR-V, the binding layout, the SPIR-V cache entry
// ============================================================================
// NOT RETAIL. docs/vulkan/PLAN.md §4.2 (translation) and §4.3 (binding model). Every retail program
// is SM4 DXBC (feature level 10_0); the backend converts each permutation blob at load with
// dxbc-spirv (third_party/dxbc-spirv, MIT, statically linked) and keeps the result in the per-user
// derived-data cache (src/ext/derived_cache). Nothing here ships a shader: the DXBC comes from the
// user's packages at run time, the SPIR-V derived from it stays in the user's cache folder.
//
// No Vulkan header and no device: the unit test (tests/unit/vulkan_shader_convert_test.cpp) and the
// headless check (tools/vulkan/shader_check.cpp) drive the same code as the program companions.
//
// THE PIPELINE (dxbc-spirv's tools/dxbc_compiler.cpp and DXVK's dxvk_shader_ir.cpp are the model):
//   dxbc::Converter -> IR, ir::legalizeIr with DXVK's CompileOptions, then ir::LowerIoPass (pixel
//   stage: unwritten render-target components defined as zero, DXVK's resolveUnwrittenPsOutputs;
//   with a previous stage given: the semantic VS -> PS linkage, below), then spirv::SpirvBuilder with
//   the ResourceMapping of the layout below.
//
// VS -> PS LINKAGE. dxbc-spirv numbers interpolants by D3D register (o3 is location 3, v3 location
// 3), while D3D links the two stages by SEMANTIC. A pair whose registers differ for the same
// semantic would read the wrong interpolant without any error. ShaderReflection::outputs of a vertex
// program and ::inputs of a pixel program carry the semantics and locations, so P2's pipeline
// builder can test a pair (StagesCompatible) and, when it fails, convert the pixel program again
// with ConvertOptions::previousStage set: that runs DXVK's resolveSemanticIo + resolveMismatchedIo
// (inputs moved to the location the vertex program writes, missing ones read as zero).
//
// THE BINDING LAYOUT (PLAN.md §4.3). One pipeline layout for the whole game: descriptor set 0 is
// the vertex stage, set 1 the pixel stage; inside a set the bindings are in D3D register order by
// class, constant buffers b0..b(N-1) first, then textures t0..t(M-1), then samplers s0..s(K-1):
//   binding = b                        (constant buffer register b)
//   binding = N + t                    (texture register t)
//   binding = N + M + s                (sampler register s)
// N / M / K were read from the retail programs by tools/vulkan/shader_check.cpp (its summary line
// prints the highest register each class uses over every vertex and pixel permutation of the
// install: 153 programs, 465 permutations, 2026-10-24): b0..b4, t0..t6 and s0..s6 are used. The
// limits below are those rounded up with headroom for the AO / ext programs of P4 (N = 8, M = 16,
// K = 16: 40 bindings per set), and stay under the D3D10 maxima (14 / 128 / 16): a register past a
// limit fails the conversion with a message naming it, it is never silently remapped.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace DerivedCache {
struct Key;
class Store;
}  // namespace DerivedCache
}  // namespace Ext

namespace VideoVulkan {
namespace Shaders {

enum class Stage : uint32_t {
    Vertex = 0,
    Pixel  = 1,
};

const char* StageName(Stage stage);

// ---- the binding layout ----------------------------------------------------------------------
constexpr uint32_t kSetVertex = 0;
constexpr uint32_t kSetPixel  = 1;
constexpr uint32_t kMaxConstantBuffers = 8;   // N: b0..b7
constexpr uint32_t kMaxTextures        = 16;  // M: t0..t15
constexpr uint32_t kMaxSamplers        = 16;  // K: s0..s15
constexpr uint32_t kBindingsPerSet     = kMaxConstantBuffers + kMaxTextures + kMaxSamplers;

constexpr uint32_t SetOfStage(Stage stage) {
    return stage == Stage::Vertex ? kSetVertex : kSetPixel;
}
constexpr uint32_t BindingOfConstantBuffer(uint32_t reg) {
    return reg;
}
constexpr uint32_t BindingOfTexture(uint32_t reg) {
    return kMaxConstantBuffers + reg;
}
constexpr uint32_t BindingOfSampler(uint32_t reg) {
    return kMaxConstantBuffers + kMaxTextures + reg;
}

// Bumped whenever the layout above, the compile options or the IO passes change: it is part of the
// cache key, so every SPIR-V module made under the old rules misses and is converted again.
constexpr uint32_t kMappingVersion = 1;

// ---- reflection --------------------------------------------------------------------------------
// One interpolant, vertex input or render-target output of a program.
struct IoElement {
    std::string semantic;        // "TEXCOORD", "POSITION", "SV_Target", ... (empty for a built-in)
    uint32_t    semanticIndex = 0;
    uint32_t    location      = 0;     // the SPIR-V Location (non-built-in) or the dxbc-spirv BuiltIn
    uint8_t     componentMask = 0;     // xyzw bits as declared
    uint8_t     builtIn       = kNoBuiltIn;   // dxbc-spirv ir::BuiltIn, kNoBuiltIn for a plain one
    static constexpr uint8_t kNoBuiltIn = 0xFF;
};

struct ShaderReflection {
    Stage    stage = Stage::Vertex;
    uint32_t constantBufferMask = 0;   // bit r: b<r> is read
    uint32_t textureMask        = 0;   // bit r: t<r> is read
    uint32_t samplerMask        = 0;   // bit r: s<r> is read
    // Vertex: the vertex input semantics (P2's vertex layouts bind by these) and the interpolants it
    // writes. Pixel: the interpolants it reads and the render targets it writes.
    std::vector<IoElement> inputs;
    std::vector<IoElement> outputs;
};

// The set / binding a register of `stage` lands on under the layout above.
struct DescriptorSlot {
    uint32_t set     = 0;
    uint32_t binding = 0;
};
DescriptorSlot ConstantBufferSlot(Stage stage, uint32_t reg);
DescriptorSlot TextureSlot(Stage stage, uint32_t reg);
DescriptorSlot SamplerSlot(Stage stage, uint32_t reg);

// ---- conversion --------------------------------------------------------------------------------
struct ConvertOptions {
    // Pixel stage only: the vertex program it is linked with. Null = unlinked (the D3D register
    // numbering, which is what every retail pair needs when StagesCompatible holds).
    const ShaderReflection* previousStage = nullptr;
};

// The program type a DXBC container's SHDR / SHEX chunk declares. False (with *why) when the blob is
// not a DXBC container, has no shader chunk, or is a stage this backend does not run (GS / HS / DS /
// CS: the retail path has none).
bool StageOfDxbc(const uint8_t* dxbc, size_t size, Stage* stage, std::string* why);

// Converts one DXBC blob of `stage`. On success *spirv holds the module (SPIR-V 1.6) and *refl its
// reflection. False with a plain message in *why on any failure: not DXBC, a different stage, a
// conversion error dxbc-spirv logged, or a register outside the binding layout.
bool ConvertDxbc(const uint8_t* dxbc, size_t size, Stage stage, std::vector<uint32_t>* spirv,
                 ShaderReflection* refl, std::string* why, const ConvertOptions& options = ConvertOptions());

// True when every interpolant `ps` reads is written by `vs` at the same location and component
// (built-ins aside), matched by semantic. False = the pair needs the linked conversion above.
bool StagesCompatible(const ShaderReflection& vs, const ShaderReflection& ps);

// A structural check of a SPIR-V module: the magic, a known version, a non-zero id bound, schema 0,
// and an instruction stream that ends exactly at the end of the module (no zero word count, no
// instruction running past it). The fallback when no spirv-val is installed.
bool CheckSpirvStructure(const std::vector<uint32_t>& spirv, std::string* why);

// The dxbc-spirv commit the backend was built with (from the submodule, at configure time).
const char* CompilerCommit();

// ---- the cache entry ---------------------------------------------------------------------------
// The derived-data cache key of a conversion: producer 'VKSV', version kMappingVersion, shape =
// {stage, compiler fingerprint low / high (the dxbc-spirv commit), link fingerprint (0 unlinked)},
// the DXBC byte count and its Hash64.
Ext::DerivedCache::Key CacheKey(const uint8_t* dxbc, size_t size, Stage stage,
                                const ConvertOptions& options = ConvertOptions());

// The payload stored under that key: the reflection, then the SPIR-V words (little-endian).
std::vector<uint8_t> EncodeEntry(const std::vector<uint32_t>& spirv, const ShaderReflection& refl);
// False when the payload is truncated, from another layout version, or holds no valid module.
bool DecodeEntry(const std::vector<uint8_t>& payload, std::vector<uint32_t>* spirv, ShaderReflection* refl);

// The program companions' entry (shader_cache.cpp): the cached entry when `store` has one, else a
// conversion stored for the next start. `store` may be null (no cache: the faithful profile, an
// unknown documents folder). *hit says which path served the call.
bool LoadOrConvert(Ext::DerivedCache::Store* store, const uint8_t* dxbc, size_t size, Stage stage,
                   std::vector<uint32_t>* spirv, ShaderReflection* refl, bool* hit, std::string* why,
                   const ConvertOptions& options = ConvertOptions());

}  // namespace Shaders
}  // namespace VideoVulkan
