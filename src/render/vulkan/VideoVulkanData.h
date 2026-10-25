#pragma once
// ============================================================================
// src/render/vulkan/VideoVulkanData.h -- the Vulkan backend's twelve companion classes
// ============================================================================
// NOT RETAIL. The Vulkan subclasses of the runtime's abstract Video::*Data bases
// (src/engine/video/CompanionData.h): the eleven objects Video::Device::Create*Data hands
// out plus the DeviceData itself. Same shape as src/render/metal/VideoMetalData.h; the
// bodies are device.cpp (DeviceData, ContextData, the render-target attach slots) and
// companions.cpp (buffers, images, programs, layouts, states).
//
// P0 (docs/vulkan/PLAN.md §5): the device, swap chain and frame loop are real. P1: the two
// program classes convert their DXBC to SPIR-V shader modules. Every other resource class is
// still a stub that accepts what the engine hands it and draws nothing; each stub says so in
// its body. No retail layout constraints apply to a backend subclass's own
// members (the base pins the two words every companion starts with).
// ============================================================================

#include "vk_api.h"
#include "shader_convert.h"
#include "video/CompanionData.h"

#include <cstdint>
#include <vector>

namespace VideoVulkan {

class DeviceData final : public Video::DeviceData {
public:
    explicit DeviceData(Owner* o) : Video::DeviceData(o) {}
    ~DeviceData() override { Shutdown(); }
    bool EnumerateInterfaces() override;
    bool CreateDevice() override;
    void Shutdown() override;
    void Slot4(bool loading) override;
    float Slot5() override;
    bool Slot6() override;
    bool Slot7() override;
    void SetImageBuffer(Video::ImageBuffer* imageBuffer, int hotX, int hotY) override;
    Video::ContextData* CreateContextData(Video::Context* owner) override;
    Video::ImageBufferData* CreateImageBufferData(Video::ImageBuffer* owner) override;
    Video::ConstantBufferData* CreateConstantBufferData(Video::ConstantBuffer* owner) override;
    Video::IndexBufferData* CreateIndexBufferData(Video::IndexBuffer* owner) override;
    Video::VertexBufferData* CreateVertexBufferData(Video::VertexBuffer* owner) override;
    Video::VertexLayoutData* CreateVertexLayoutData(Video::VertexLayout* owner) override;
    Video::VertexProgramData* CreateVertexProgramData(Video::VertexProgram* owner) override;
    Video::PixelProgramData* CreatePixelProgramData(Video::PixelProgram* owner) override;
    Video::RenderStateData* CreateRenderStateData(Video::RenderState* owner) override;
    Video::VideoProgramData* CreateVideoProgramData(Video::VideoProgram* owner) override;
    Video::RenderBufferData* CreateRenderBufferData(Video::RenderBuffer* owner) override;
};

class ContextData final : public Video::ContextData {
public:
    explicit ContextData(Owner* o) : Video::ContextData(o) {}
    ~ContextData() override = default;
    bool BeginCommands() override;
    void EndCommands() override;
    void Finish() override;
    void Clear(unsigned flags, unsigned color, float depth, int stencil) override;
    void BindRenderBuffer() override;
    void BindViewport() override;
    void BindScissor() override;
    void BindTransforms() override;
    void BindTextureNone(int slot) override;
    void BindIndexNone() override;
    void BindVertexNone(int slot) override;
    void DrawIndexed(int primitiveType, int baseVertex, int vertexCount, int startIndex, int indexCount,
                     int instanceCount) override;
    int GetMaxDynamicInstances(int a, int b, int c) override;
    bool BeginVertices(int vertexCount, int vertexStride) override;
    void EndVertices(int primitiveType) override;
    bool BeginIndexedVertices(int vertexCount, int vertexStride, int indexCount) override;
    void EndIndexedVertices(int primitiveType) override;
    bool BeginInstancedIndexedVertices(int vertexCount, int vertexStride, int indexCount,
                                       int instanceCount) override;
    void EndInstancedIndexedVertices(int primitiveType) override;
};

class ImageBufferData final : public Video::ImageBufferData {
public:
    explicit ImageBufferData(Owner* o) : Video::ImageBufferData(o) {}
    ~ImageBufferData() override = default;
    void AttachColorBuffer() override;
    void AttachDepthBuffer() override;
    bool Create(const Collection::Array<Collection::Array<uint8_t>>* levels) override;
    void Bind(void* context, unsigned slot) override;
    bool Update(void* data, int width, int height) override;
    bool Read(void* dst, int width, int height) override;
};

class IndexBufferData final : public Video::IndexBufferData {
public:
    explicit IndexBufferData(Owner* o) : Video::IndexBufferData(o) {}
    ~IndexBufferData() override = default;
    bool Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;
    bool SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override;
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;
    void Unlock() override;
    void Bind(void* context) override;
    void Serialize(IO::Serializer* serializer) override;
    // P0: the engine writes through Lock's pointer, so a stub still has to hand out memory.
    std::vector<uint8_t> scratch;
};

class VertexBufferData final : public Video::VertexBufferData {
public:
    explicit VertexBufferData(Owner* o) : Video::VertexBufferData(o) {}
    ~VertexBufferData() override = default;
    bool Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) override;
    bool SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) override;
    void* Lock(int32_t sizeBytes, int32_t* outByteOffset) override;
    void Unlock() override;
    void Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) override;
    void Serialize(IO::Serializer* serializer) override;
    std::vector<uint8_t> scratch;
};

class ConstantBufferData final : public Video::ConstantBufferData {
public:
    explicit ConstantBufferData(Owner* o) : Video::ConstantBufferData(o) {}
    ~ConstantBufferData() override = default;
    bool Create() override;
    void Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) override;
    int32_t SlotIndexPlusOne() override;
};

class RenderBufferData final : public Video::RenderBufferData {
public:
    explicit RenderBufferData(Owner* o) : Video::RenderBufferData(o) {}
    ~RenderBufferData() override = default;
    bool Slot1() override;
    void Bind(void* context) override;
    void Resolve(void* context, Video::RenderBuffer* other, bool flag) override;
};

class VertexLayoutData final : public Video::VertexLayoutData {
public:
    explicit VertexLayoutData(Owner* o) : Video::VertexLayoutData(o) {}
    ~VertexLayoutData() override = default;
    void Bind(void* context) override;
    void PlatformCreate() override;
};

// The programs (P1): the owner's DXBC permutation blobs (Video::ShaderProgram::_byteCode, up to four,
// the DX11 `variants` rule of src/render/dx11/shaderprograms.cpp) converted to SPIR-V
// (shader_convert.h, through the derived-data cache), one VkShaderModule per permutation the
// program has, with its reflection. Bind records the permutation P2's pipeline key reads.
constexpr int kPermutationCount = 4;

// One log line with the modules made, served from the cache and failed since the last call
// (nothing when none); the loader bracket's end calls it (DeviceData::Slot4). companions.cpp.
void LogProgramSummary();

class VertexProgramData final : public Video::VertexProgramData {
public:
    explicit VertexProgramData(Owner* o) : Video::VertexProgramData(o) {}
    ~VertexProgramData() override;
    bool PlatformCreate() override;
    void Bind(void* context, int permutation) override;
    VkShaderModule modules[kPermutationCount] = {};
    Shaders::ShaderReflection reflection[kPermutationCount];
    int boundPermutation = 0;   // the last Bind's choice: the request when built, else 0
};

class PixelProgramData final : public Video::PixelProgramData {
public:
    explicit PixelProgramData(Owner* o) : Video::PixelProgramData(o) {}
    ~PixelProgramData() override;
    bool PlatformCreate() override;
    void Bind(void* context, int permutation) override;
    VkShaderModule modules[kPermutationCount] = {};
    Shaders::ShaderReflection reflection[kPermutationCount];
    int boundPermutation = 0;
};

class RenderStateData final : public Video::RenderStateData {
public:
    explicit RenderStateData(Owner* o) : Video::RenderStateData(o) {}
    ~RenderStateData() override = default;
    bool Create() override;
    void Bind(void* context) override;
};

class VideoProgramData final : public Video::VideoProgramData {
public:
    explicit VideoProgramData(Owner* o) : Video::VideoProgramData(o) {}
    ~VideoProgramData() override = default;
    bool Slot1() override;
    void Bind(void* context, int detail) override;
};

}  // namespace VideoVulkan
