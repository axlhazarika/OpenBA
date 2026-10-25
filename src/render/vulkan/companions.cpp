// ============================================================================
// src/render/vulkan/companions.cpp -- the resource companions (P1: programs real, the rest stubs)
// ============================================================================
// NOT RETAIL. The buffer, image, program, layout and state companions of
// VideoVulkanData.h. The programs are real since P1 (docs/vulkan/PLAN.md §5): their DXBC
// permutations become SPIR-V shader modules (the section at "the programs" below). Every
// other companion is still the P0 stub that accepts what the engine hands it and keeps
// nothing on the GPU: Create answers true so the loader and the menu run, Bind does nothing,
// Read answers false (the cursor copy and screenshots see "not readable"), Lock hands out
// host memory because the engine writes through the pointer. P2 replaces those bodies with
// the real ones (VMA buffers, images, samplers, pipeline-key state); the slot roles are the
// DX11 files' (buffers.cpp, imagebufferdata.cpp, shaderprograms.cpp, vertexlayoutdata.cpp,
// renderstatedata.cpp).
//
// One body is NOT a stub: the buffers' Serialize. It is part of the resource stream format
// (a mesh's vertex and index bytes sit inline in the .rsc after `dynamic` and `size`), so a
// slot that did not consume them desynchronised every later read (the first run of this
// backend sat in GraphicsMesh::PlatformSerialize reading garbage sizes until the watchdog
// fired). The bytes are read into the companion's host copy, exactly as the DX11 and Metal
// load arms seed their GPU buffer; the save arm writes that same copy back.
// ============================================================================

#include "VideoVulkanData.h"
#include "device_objects.h"

#include "ext/derived_cache/derived_cache.h"   // the SPIR-V cache's store (OpenShared)
#include "io/Serializer.h"
#include "video/Buffers.h"
#include "video/ConstantBuffer.h"   // ConstantBuffer::_uploadIndex
#include "video/ShaderProgram.h"    // ShaderProgram::_byteCode, the permutation blobs

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace VideoVulkan {

namespace {

// The shared Serialize of both buffer kinds (dx11 buffers.cpp GpuBuffer_Serialize, Metal
// SerializeBuffer). Order matters: the dynamic flag first, then the size, then the payload.
template <class Data>
void SerializeBuffer(Data* self, IO::Serializer* serializer) {
    if (!serializer->GetEncoding().IsBinary()) return;
    auto* owner = self->_owner;
    const bool loading = static_cast<int>(serializer->GetDirection()) == 0;
    serializer->Serialize(owner->dynamic, nullptr);
    serializer->Serialize(owner->size, nullptr);
    if (owner->size <= 0) return;
    if (loading) {
        self->scratch.assign(static_cast<size_t>(owner->size), 0);
        serializer->SerializeBytes(owner->size, self->scratch.data(), nullptr);
        self->Create(static_cast<uint32_t>(owner->size), owner->dynamic, self->scratch.data());
        return;
    }
    if (self->scratch.size() < static_cast<size_t>(owner->size)) {
        self->scratch.resize(static_cast<size_t>(owner->size));
    }
    serializer->SerializeBytes(owner->size, self->scratch.data(), nullptr);
}

template <class Data>
bool CreateBuffer(Data* self, uint32_t sizeBytes, const void* data) {
    if (data == self->scratch.data() && !self->scratch.empty()) {
        return true;   // Serialize seeded the copy already
    }
    self->scratch.assign(sizeBytes, 0);
    if (data && sizeBytes) std::memcpy(self->scratch.data(), data, sizeBytes);
    return true;
}

template <class Data>
bool SetBufferData(Data* self, int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    if (sizeBytes <= 0 || offsetBytes < 0 || !src) return true;
    const size_t end = static_cast<size_t>(offsetBytes) + static_cast<size_t>(sizeBytes);
    if (self->scratch.size() < end) self->scratch.resize(end);
    std::memcpy(self->scratch.data() + offsetBytes, src, static_cast<size_t>(sizeBytes));
    return true;
}

template <class Data>
void* LockBuffer(Data* self, int32_t sizeBytes, int32_t* outByteOffset) {
    if (self->scratch.size() < static_cast<size_t>(sizeBytes)) {
        self->scratch.resize(static_cast<size_t>(sizeBytes));
    }
    if (outByteOffset) *outByteOffset = 0;
    return self->scratch.data();
}

}  // namespace

// ---- ImageBufferData (AttachColorBuffer / AttachDepthBuffer are in device.cpp: they alias the
// main targets) ---------------------------------------------------------------------------

bool ImageBufferData::Create(const Collection::Array<Collection::Array<uint8_t>>* levels) {
    (void)levels;   // P0: no texture is made
    return true;
}

void ImageBufferData::Bind(void* context, unsigned slot) {
    (void)context;
    (void)slot;
}

bool ImageBufferData::Update(void* data, int width, int height) {
    (void)data;
    (void)width;
    (void)height;
    return true;
}

bool ImageBufferData::Read(void* dst, int width, int height) {
    (void)dst;
    (void)width;
    (void)height;
    return false;   // nothing to read back yet
}

// ---- IndexBufferData -----------------------------------------------------------------------

bool IndexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    (void)dynamic;
    return CreateBuffer(this, sizeBytes, data);
}

bool IndexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    return SetBufferData(this, sizeBytes, offsetBytes, src);
}

void* IndexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    return LockBuffer(this, sizeBytes, outByteOffset);
}

void IndexBufferData::Unlock() {
}

void IndexBufferData::Bind(void* context) {
    (void)context;
}

void IndexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer);
}

// ---- VertexBufferData ----------------------------------------------------------------------

bool VertexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    (void)dynamic;
    return CreateBuffer(this, sizeBytes, data);
}

bool VertexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    return SetBufferData(this, sizeBytes, offsetBytes, src);
}

void* VertexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    return LockBuffer(this, sizeBytes, outByteOffset);
}

void VertexBufferData::Unlock() {
}

void VertexBufferData::Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) {
    (void)context;
    (void)startSlot;
    (void)offsetBytes;
    (void)strideBytes;
}

void VertexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer);
}

// ---- ConstantBufferData --------------------------------------------------------------------

bool ConstantBufferData::Create() {
    return true;
}

void ConstantBufferData::Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) {
    (void)context;
    (void)srcData;
    (void)first;
    (void)sizeBytes;
}

int32_t ConstantBufferData::SlotIndexPlusOne() {
    return _owner->_uploadIndex + 1;
}

// ---- RenderBufferData (Bind / Resolve are in device.cpp with the targets) -------------------

bool RenderBufferData::Slot1() {
    return true;
}

// ---- VertexLayoutData ----------------------------------------------------------------------

void VertexLayoutData::Bind(void* context) {
    (void)context;
}

void VertexLayoutData::PlatformCreate() {
}

// ---- the programs (P1) -------------------------------------------------------------------------
// The slot roles are src/render/dx11/shaderprograms.cpp's: PlatformCreate makes one shader per
// permutation blob the owner has (Video::ShaderProgram::_byteCode, at most four), SKIPPING the
// permutations past the owner's count rather than stopping, and returns false on the first failure
// with the modules already made left in place (the destructor frees them). Bind keeps the DX11
// fallback: the requested permutation when it was built, else permutation 0, with no bounds check
// on the request (retail has none; a clamp would hide a caller bug). Nothing is bound on a command
// buffer yet: P2's pipeline key reads `boundPermutation`, `modules` and `reflection`.
//
// A blob goes through Shaders::LoadOrConvert (shader_convert.h): the user's derived-data cache
// first (<Documents>\Banished\Cache; none under the faithful profile, which reads and writes no
// cache: every boot then converts), else dxbc-spirv, stored for the next start.

namespace {

std::atomic<unsigned> g_programModules{0};    // modules made since the last summary
std::atomic<unsigned> g_programCacheHits{0};
std::atomic<unsigned> g_programFailures{0};
std::atomic<unsigned> g_programLogLines{0};

template <class Data>
bool CreateProgram(Data* self, Shaders::Stage stage) {
    const Video::ShaderProgram* owner = self->_owner;
    Ext::DerivedCache::Store* const cache = Ext::DerivedCache::OpenShared();
    for (int i = 0; i < kPermutationCount; ++i) {
        if (i >= owner->_byteCode.GetSize()) continue;
        const Collection::Array<uint8_t>& blob = owner->_byteCode.GetData()[i];
        // EXT SEAM (P4, NOT RETAIL): ext.better_trees' foliage_aa / billboard_shadow rewrites and
        // ext.draw_batch's particle VS rewrite edit these DXBC bytes on DX11
        // (src/ext/better_trees/foliage_aa_stage.h, billboard_shadow_stage.h,
        // src/ext/draw_batch/smoke_vs_stage.h). On Vulkan the rewritten copy is converted the
        // same way, through this call, into a second module per permutation; P4 wires the stages.
        const uint8_t* bytes = blob.GetData();
        const size_t size = static_cast<size_t>(blob.GetSize());
        std::vector<uint32_t> spirv;
        bool hit = false;
        std::string why;
        if (!Shaders::LoadOrConvert(cache, bytes, size, stage, &spirv, &self->reflection[i], &hit, &why)) {
            g_programFailures.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[Vulkan] %s program %p permutation %d (%zu bytes): %s\n", Shaders::StageName(stage),
                         static_cast<const void*>(owner), i, size, why.c_str());
            return false;
        }
        DestroyShaderModule(self->modules[i]);   // a second PlatformCreate replaces, it does not leak
        self->modules[i] = CreateShaderModule(spirv, &why);
        if (self->modules[i] == VK_NULL_HANDLE) {
            g_programFailures.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[Vulkan] %s program %p permutation %d: %s\n", Shaders::StageName(stage),
                         static_cast<const void*>(owner), i, why.c_str());
            return false;
        }
        g_programModules.fetch_add(1, std::memory_order_relaxed);
        if (hit) g_programCacheHits.fetch_add(1, std::memory_order_relaxed);
        if (g_programLogLines.fetch_add(1, std::memory_order_relaxed) < 4) {
            std::fprintf(stderr, "[Vulkan] %s program permutation %d: %s, %zu SPIR-V words, cb 0x%x tex 0x%x smp 0x%x\n",
                         Shaders::StageName(stage), i, hit ? "cache" : "converted", spirv.size(),
                         self->reflection[i].constantBufferMask, self->reflection[i].textureMask,
                         self->reflection[i].samplerMask);
        }
    }
    return true;
}

template <class Data>
void BindProgram(Data* self, int permutation) {
    self->boundPermutation = self->modules[permutation] != VK_NULL_HANDLE ? permutation : 0;
}

template <class Data>
void DestroyProgram(Data* self) {
    for (int i = 0; i < kPermutationCount; ++i)
        DestroyShaderModule(self->modules[i]);
}

}  // namespace

void LogProgramSummary() {
    const unsigned made = g_programModules.exchange(0, std::memory_order_relaxed);
    const unsigned hits = g_programCacheHits.exchange(0, std::memory_order_relaxed);
    const unsigned failed = g_programFailures.exchange(0, std::memory_order_relaxed);
    if (made == 0 && failed == 0) return;
    std::fprintf(stderr, "[Vulkan] programs: %u shader module(s), %u from the cache, %u converted, %u failed\n", made,
                 hits, made - hits, failed);
}

VertexProgramData::~VertexProgramData() {
    DestroyProgram(this);
}

bool VertexProgramData::PlatformCreate() {
    return CreateProgram(this, Shaders::Stage::Vertex);
}

void VertexProgramData::Bind(void* context, int permutation) {
    (void)context;   // the DX11 slot ignores it too (shaderprograms.cpp header)
    BindProgram(this, permutation);
}

PixelProgramData::~PixelProgramData() {
    DestroyProgram(this);
}

bool PixelProgramData::PlatformCreate() {
    return CreateProgram(this, Shaders::Stage::Pixel);
}

void PixelProgramData::Bind(void* context, int permutation) {
    (void)context;
    BindProgram(this, permutation);
}

// ---- RenderStateData -----------------------------------------------------------------------

bool RenderStateData::Create() {
    return true;
}

void RenderStateData::Bind(void* context) {
    (void)context;
}

// ---- VideoProgramData (dx11: Slot1 true, Bind inert) ---------------------------------------

bool VideoProgramData::Slot1() {
    return true;
}

void VideoProgramData::Bind(void* context, int detail) {
    (void)context;
    (void)detail;
}

}  // namespace VideoVulkan
