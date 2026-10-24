// ============================================================================
// src/render/vulkan/companions.cpp -- the resource companions (P0: stubs that draw nothing)
// ============================================================================
// NOT RETAIL. The buffer, image, program, layout and state companions of
// VideoVulkanData.h. In P0 (docs/vulkan/PLAN.md §5) every one of them accepts what the
// engine hands it and keeps nothing on the GPU: Create answers true so the loader and the
// menu run, Bind does nothing, Read answers false (the cursor copy and screenshots see
// "not readable"), Lock hands out host memory because the engine writes through the
// pointer. P2 replaces these bodies with the real ones (VMA buffers, images, samplers,
// SPIR-V programs, pipeline-key state); the slot roles are the DX11 files' (buffers.cpp,
// imagebufferdata.cpp, shaderprograms.cpp, vertexlayoutdata.cpp, renderstatedata.cpp).
//
// One body is NOT a stub: the buffers' Serialize. It is part of the resource stream format
// (a mesh's vertex and index bytes sit inline in the .rsc after `dynamic` and `size`), so a
// slot that did not consume them desynchronised every later read (the first run of this
// backend sat in GraphicsMesh::PlatformSerialize reading garbage sizes until the watchdog
// fired). The bytes are read into the companion's host copy, exactly as the DX11 and Metal
// load arms seed their GPU buffer; the save arm writes that same copy back.
// ============================================================================

#include "VideoVulkanData.h"

#include "io/Serializer.h"
#include "video/Buffers.h"
#include "video/ConstantBuffer.h"   // ConstantBuffer::_uploadIndex

#include <cstring>

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

// ---- the programs ----------------------------------------------------------------------------

bool VertexProgramData::PlatformCreate() {
    return true;   // P1: convert the owner's DXBC blobs to SPIR-V here
}

void VertexProgramData::Bind(void* context, int permutation) {
    (void)context;
    (void)permutation;
}

bool PixelProgramData::PlatformCreate() {
    return true;
}

void PixelProgramData::Bind(void* context, int permutation) {
    (void)context;
    (void)permutation;
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
