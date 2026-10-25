// ============================================================================
// src/render/vulkan/companions.cpp -- the resource companions: buffers, images, programs, layouts, states
// ============================================================================
// NOT RETAIL. The buffer, image, program, layout and state companions of VideoVulkanData.h. The slot
// roles are the DX11 files' (src/render/dx11/buffers.cpp, imagebufferdata.cpp, shaderprograms.cpp,
// vertexlayoutdata.cpp, renderstatedata.cpp); the GPU objects come from resources.h, the pipeline
// pieces from pipelines.h, and every Bind records into the bound state of draw.h.
//
// P2 (docs/vulkan/PLAN.md §5): everything here is real. P3: a render-buffer image gets an attachment
// view and layout tracking (draw.h TARGETS / LAYOUTS), and ImageBufferData::Read reads back through
// draw.cpp (DrawReadImage).
//
// The buffers' Serialize is part of the resource stream format (a mesh's vertex and index bytes sit
// inline in the .rsc after `dynamic` and `size`): the load arm reads them and creates the buffer from
// them, exactly as the DX11 and Metal load arms do; the save arm reads the GPU buffer back.
// ============================================================================

#include "VideoVulkanData.h"
#include "device_objects.h"
#include "draw.h"

#include "ext/derived_cache/derived_cache.h"   // the SPIR-V cache's store (OpenShared)
#include "io/Serializer.h"
#include "video/Buffers.h"
#include "video/ConstantBuffer.h"   // ConstantBuffer::_uploadIndex
#include "video/Device.h"           // Video::Configuration::_singleton, Device::maxAnisotropy
#include "video/ImageBuffer.h"
#include "video/RenderState.h"
#include "video/ShaderProgram.h"    // ShaderProgram::_byteCode, the permutation blobs
#include "video/VertexLayout.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace VideoVulkan {

namespace {

std::atomic<uint32_t> g_logOnce{0};

void LogOnce(uint32_t bit, const char* message) {
    if (g_logOnce.fetch_or(bit, std::memory_order_relaxed) & bit) return;
    std::fprintf(stderr, "[Vulkan] %s\n", message);
}

// ---- buffers (dx11 buffers.cpp GpuBuffer_* / BufferData_*) ----

void ReleaseStore(BufferStore& store) {
    RetireBuffer(&store.gpu);
    for (BufferStore::Version& v : store.versions)
        RetireBuffer(&v.buffer);
    store.versions.clear();
    store.current = -1;
    store.writeCursor = 0;
}

// Slot 1 of both buffer kinds: owner->size / dynamic are stored first, as retail does; a zero size
// makes nothing. A dynamic buffer starts with one host-visible version (seeded from `data`), a static
// one is device-local and filled through the upload batch.
template <class Data>
bool CreateBuffer(Data* self, uint32_t sizeBytes, uint8_t dynamicByte, const void* data, VkBufferUsageFlags usage) {
    auto* owner = self->_owner;
    owner->size = static_cast<int32_t>(sizeBytes);
    owner->dynamic = dynamicByte;
    BufferStore& store = self->store;
    ReleaseStore(store);
    store.dynamic = dynamicByte != 0;
    if (owner->size == 0) return true;
    std::string why;
    if (store.dynamic) {
        BufferStore::Version v;
        if (!CreateHostBuffer(sizeBytes, usage, &v.buffer, &why)) {
            std::fprintf(stderr, "[Vulkan] dynamic buffer of %u bytes: %s\n", sizeBytes, why.c_str());
            return false;
        }
        if (data) std::memcpy(v.buffer.mapped, data, sizeBytes);
        store.versions.push_back(v);
        store.current = 0;
        return true;
    }
    if (!CreateStaticBuffer(sizeBytes, usage, data, &store.gpu, &why)) {
        std::fprintf(stderr, "[Vulkan] buffer of %u bytes: %s\n", sizeBytes, why.c_str());
        return false;
    }
    return true;
}

// Slot 2: D3D11 UpdateSubresource over [offset, offset + size). A static buffer takes it through the
// upload batch (resources.h: it lands before the frame being recorded); a dynamic one is written in
// place (D3D11 never updates a DYNAMIC buffer this way; nothing in the game does).
template <class Data>
bool SetBufferData(Data* self, int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    if (self->_owner->size == 0) return true;
    if (sizeBytes <= 0 || offsetBytes < 0 || !src) return true;
    BufferStore& store = self->store;
    if (store.dynamic) {
        if (store.current >= 0 && offsetBytes + sizeBytes <= self->_owner->size) {
            std::memcpy(store.versions[store.current].buffer.mapped + offsetBytes, src,
                        static_cast<size_t>(sizeBytes));
        }
        return true;
    }
    UploadBuffer(store.gpu, static_cast<VkDeviceSize>(offsetBytes), src, static_cast<VkDeviceSize>(sizeBytes));
    return true;
}

// Slot 3: dx11's Lock. Past the end of the buffer it is a MAP_WRITE_DISCARD (cursor back to 0, and
// here a fresh version: the one the frames in flight read stays untouched); otherwise
// MAP_WRITE_NO_OVERWRITE at the cursor. The cursor advances by the size rounded up to even. A
// non-dynamic buffer cannot be mapped (D3D11 fails the Map): null, as retail gets.
template <class Data>
void* LockBuffer(Data* self, int32_t sizeBytes, int32_t* outByteOffset) {
    BufferStore& store = self->store;
    const int32_t size = self->_owner->size;
    if (!store.dynamic || store.current < 0) return nullptr;
    if (store.writeCursor + sizeBytes > size) {
        const uint64_t recording = FramesSubmitted();
        store.versions[store.current].busyUntilFrame = recording;
        const uint64_t completed = FramesCompleted();
        int next = -1;
        for (size_t i = 0; i < store.versions.size(); ++i) {
            if (static_cast<int>(i) != store.current && store.versions[i].busyUntilFrame < completed) {
                next = static_cast<int>(i);
                break;
            }
        }
        if (next < 0) {
            BufferStore::Version v;
            std::string why;
            if (!CreateHostBuffer(static_cast<VkDeviceSize>(size), Data::kUsage, &v.buffer, &why)) {
                std::fprintf(stderr, "[Vulkan] dynamic buffer version of %d bytes: %s\n", size, why.c_str());
                return nullptr;
            }
            store.versions.push_back(v);
            next = static_cast<int>(store.versions.size()) - 1;
        }
        store.current = next;
        store.writeCursor = 0;
    }
    *outByteOffset = store.writeCursor;
    uint8_t* p = store.versions[store.current].buffer.mapped + store.writeCursor;
    store.writeCursor += ((sizeBytes + 1) / 2) * 2;
    return p;
}

template <class Data>
void SerializeBuffer(Data* self, IO::Serializer* serializer, VkBufferUsageFlags usage) {
    if (!serializer->GetEncoding().IsBinary()) return;
    auto* owner = self->_owner;
    const bool loading = static_cast<int>(serializer->GetDirection()) == 0;
    serializer->Serialize(owner->dynamic, nullptr);
    serializer->Serialize(owner->size, nullptr);
    if (owner->size <= 0) return;
    std::vector<uint8_t> bytes(static_cast<size_t>(owner->size), 0);
    if (loading) {
        serializer->SerializeBytes(owner->size, bytes.data(), nullptr);
        CreateBuffer(self, static_cast<uint32_t>(owner->size), owner->dynamic, bytes.data(), usage);
        return;
    }
    const BufferStore& store = self->store;
    if (store.dynamic && store.current >= 0) {
        std::memcpy(bytes.data(), store.versions[store.current].buffer.mapped, bytes.size());
    } else if (!ReadBuffer(store.gpu, bytes.data(), bytes.size())) {
        LogOnce(1, "a buffer save read back nothing (the buffer is gone): zeros written");
    }
    serializer->SerializeBytes(owner->size, bytes.data(), nullptr);
}

// ---- images (dx11 imagebufferdata.cpp) ----

// dx11's kFormatToDxgi, as Vulkan formats: BC1, BC3, BC3, A8, R8, BGRA8, R32F, D24S8 (or D32S8).
VkFormat ImageFormat(int format) {
    switch (format) {
    case 0: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case 1: return VK_FORMAT_BC3_UNORM_BLOCK;
    case 2: return VK_FORMAT_BC3_UNORM_BLOCK;
    case 3: return VK_FORMAT_R8_UNORM;   // A8_UNORM: R8 with an alpha swizzle (no core A8)
    case 4: return VK_FORMAT_R8_UNORM;
    case 5: return VK_FORMAT_B8G8R8A8_UNORM;
    case 6: return VK_FORMAT_R32_SFLOAT;
    case 7: return DepthStencilFormat();
    default: return VK_FORMAT_UNDEFINED;
    }
}

// Bytes per texel (per 4x4 block for the compressed formats): dx11's kFormatBytes.
uint32_t FormatBytes(int format) {
    static const uint32_t kBytes[8] = { 8, 16, 16, 1, 1, 4, 4, 4 };
    return (format >= 0 && format < 8) ? kBytes[format] : 4;
}

// dx11's SamplerDescFor: the engine's filter index into a three-entry table patched by the
// configuration's texture filter and the comparison flag, wrap / clamp addressing, MaxLOD the mip count.
SamplerDesc SamplerFor(const Video::ImageBuffer* ib, bool point) {
    static const uint8_t kAddress[2] = { VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE };
    int aniso = Video::Configuration::_singleton ? Video::Configuration::_singleton->textureFilter : 0;
    const bool comparison = ((ib->_flags >> 6) & 1) != 0;
    // filter entry: 0 point, 1 min/mag linear mip point, 2 anisotropic (as patched below).
    enum Kind { Point, LinearMipPoint, LinearAll, Anisotropic, CompareLinear };
    Kind table[3] = { Point, LinearMipPoint, Anisotropic };
    if (comparison) {
        table[1] = CompareLinear;
        aniso = 0;
    } else if (aniso == -1) {
        table[2] = LinearMipPoint;
        aniso = 0;
    } else if (aniso == 0) {
        table[2] = LinearAll;
        aniso = 0;
    } else if (aniso < 0) {
        aniso = 0;
    }
    const Video::Device* dev = Video::Device::_singleton;
    if (dev && aniso > dev->maxAnisotropy) aniso = dev->maxAnisotropy;
    const int filter = std::clamp(static_cast<int>(ib->_filter), 0, 2);
    const Kind kind = point ? Point : table[filter];
    SamplerDesc d;
    d.magLinear = kind != Point;
    d.minLinear = kind != Point;
    d.mipLinear = kind == LinearAll || kind == Anisotropic || kind == CompareLinear;
    d.compare = kind == CompareLinear;
    d.anisotropy = kind == Anisotropic ? static_cast<uint8_t>(std::max(aniso, 0)) : 0;
    d.addressU = kAddress[ib->_addressU & 1];
    d.addressV = kAddress[ib->_addressV & 1];
    d.addressW = kAddress[ib->_addressW & 1];
    d.maxLod = (ib->_flags & 1) ? static_cast<float>(ib->GetLevelCount()) - 1.0f : 0.0f;
    return d;
}

// ---- render states (dx11 renderstatedata.cpp's tables, as Vulkan values) ----
const uint8_t kBlendFactor[10] = {
    VK_BLEND_FACTOR_ZERO,                 // 0 D3D11_BLEND_ZERO
    VK_BLEND_FACTOR_ONE,                  // 1 D3D11_BLEND_ONE
    VK_BLEND_FACTOR_SRC_COLOR,            // 2 D3D11_BLEND_SRC_COLOR
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,  // 3 D3D11_BLEND_INV_SRC_COLOR
    VK_BLEND_FACTOR_SRC_ALPHA,            // 4 D3D11_BLEND_SRC_ALPHA
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,  // 5 D3D11_BLEND_INV_SRC_ALPHA
    VK_BLEND_FACTOR_DST_COLOR,            // 6 D3D11_BLEND_DEST_COLOR
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,  // 7 D3D11_BLEND_INV_DEST_COLOR
    VK_BLEND_FACTOR_DST_ALPHA,            // 8 D3D11_BLEND_DEST_ALPHA
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,  // 9 D3D11_BLEND_INV_DEST_ALPHA
};
const uint8_t kBlendOp[3] = { VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT };
const uint8_t kCompare[8] = {
    VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_NOT_EQUAL,
};
// D3D11 INCR / DECR wrap; INCR_SAT / DECR_SAT clamp.
const uint8_t kStencilOp[8] = {
    VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP,
    VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INVERT,
};
const uint8_t kCull[3] = { VK_CULL_MODE_FRONT_BIT, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_NONE };
const uint8_t kFill[2] = { VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_LINE };

template <size_t N>
uint8_t Pick(const uint8_t (&table)[N], int index, uint8_t fallback) {
    return (index >= 0 && static_cast<size_t>(index) < N) ? table[index] : fallback;
}

}  // namespace

// ---- ImageBufferData (AttachColorBuffer / AttachDepthBuffer are in device.cpp: they alias the
// main targets) ---------------------------------------------------------------------------

// The attachment view of a render-buffer image (retired with it: the frame being recorded may use it).
static void RetireAttachmentView(VkImageView* view) {
    if (!*view) return;
    VkImageView v = *view;
    *view = VK_NULL_HANDLE;
    Retire([v]() {
        if (LiveDevice()) vkDestroyImageView(LiveDevice(), v, nullptr);
    });
}

ImageBufferData::~ImageBufferData() {
    DrawForget(this);
    DrawForgetImage(this);
    RetireAttachmentView(&attachmentView);
    RetireImage(&image);
}

// Slot 3 (dx11 Create). Same early-out (a non-generated, non-rendered owner with no extent makes
// nothing), same shape: 2D or a cube (6 layers), one mip or the full chain (dx11 MipLevels =
// ~flags & 1, 0 meaning the full chain), the level arrays face-major as D3D11's subresource order.
// A render-buffer kind gets attachment usage (draw.h TARGETS renders into it); every image is created
// readable: uploaded, or cleared (colour 0, depth 1) when it has no levels.
bool ImageBufferData::Create(const Collection::Array<Collection::Array<uint8_t>>* levels) {
    Video::ImageBuffer* const ib = _owner;
    const int width = ib->Width();
    const int height = ib->Height();
    if (!ib->IsGenerated() && !ib->IsRendered() && (width == 0 || height == 0)) return true;
    DrawForgetImage(this);
    RetireAttachmentView(&attachmentView);
    RetireImage(&image);
    state = ImageState{};
    const int format = static_cast<int>(ib->_format);
    const VkFormat vkFormat = ImageFormat(format);
    if (vkFormat == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "[Vulkan] image %p: format %d is not one retail uses\n", static_cast<void*>(ib), format);
        return false;
    }
    cube = ib->_dimension != 0;
    ImageDesc desc;
    desc.width = static_cast<uint32_t>(std::max(width, 1));
    desc.height = static_cast<uint32_t>(std::max(height, 1));
    desc.mips = ib->HasMipMaps() ? static_cast<uint32_t>(ib->GetLevelCount()) : 1;
    desc.layers = cube ? 6 : 1;
    desc.cube = cube;
    desc.format = vkFormat;
    if (format == 3) {
        desc.swizzle = { VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO,
                         VK_COMPONENT_SWIZZLE_R };
    }
    if (ib->IsRenderBuffer()) {
        desc.usage = ib->IsDepthBuffer() ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                         : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    std::string why;
    if (!CreateImage(desc, &image, &why)) {
        std::fprintf(stderr, "[Vulkan] image %dx%d format %d: %s\n", width, height, format, why.c_str());
        return false;
    }
    // P3: every image is tracked from here (ClearImage / UploadImage below leave it SHADER_READ_ONLY in
    // the upload batch, which runs before any frame that uses it). A 2D render-buffer image also gets
    // dx11's RTV / DSV: the attachment view over mip 0 and every aspect, no swizzle (a cube gets none,
    // as dx11 makes none).
    state.image = image.image;
    state.aspect = image.aspect;
    state.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (ib->IsRenderBuffer() && !cube) {
        VkImageViewCreateInfo vi = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        vi.image = image.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = image.format;
        vi.subresourceRange = { image.aspect, 0, 1, 0, 1 };
        const VkResult r = vkCreateImageView(LiveDevice(), &vi, nullptr, &attachmentView);
        if (r != VK_SUCCESS) {
            std::fprintf(stderr, "[Vulkan] attachment view %dx%d format %d: %s\n", width, height, format,
                         ResultName(r));
            attachmentView = VK_NULL_HANDLE;
        }
    }
    sampler = GetSampler(SamplerFor(ib, false));
    pointSampler = GetSampler(SamplerFor(ib, true));
    if (!levels || levels->GetSize() == 0) {
        ClearImage(image);
        return true;
    }
    // The levels into one staging blob, each region at a 16-byte boundary (a texel / block multiple).
    const bool compressed = ib->IsCompressed();
    const uint32_t texelBytes = FormatBytes(format);
    std::vector<ImageRegion> regions;
    size_t total = 0;
    const int count = levels->GetSize();
    for (int i = 0; i < count; ++i)
        total += (static_cast<size_t>(levels->GetData()[i].GetSize()) + 15) / 16 * 16;
    std::vector<uint8_t> blob(total);
    size_t at = 0;
    bool shortLevel = false;
    for (int i = 0; i < count; ++i) {
        const uint32_t mip = static_cast<uint32_t>(i) % desc.mips;
        const uint32_t layer = static_cast<uint32_t>(i) / desc.mips;
        if (layer >= desc.layers) break;
        const uint32_t w = std::max(desc.width >> mip, 1u);
        const uint32_t h = std::max(desc.height >> mip, 1u);
        const size_t need = compressed ? static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * texelBytes
                                       : static_cast<size_t>(w) * h * texelBytes;
        const Collection::Array<uint8_t>& level = levels->GetData()[i];
        const size_t have = static_cast<size_t>(level.GetSize());
        if (have < need) {
            shortLevel = true;
            continue;
        }
        std::memcpy(blob.data() + at, level.GetData(), need);
        ImageRegion r;
        r.offset = at;
        r.mip = mip;
        r.layer = layer;
        r.width = w;
        r.height = h;
        regions.push_back(r);
        at += (need + 15) / 16 * 16;
    }
    if (shortLevel) LogOnce(2, "an image level holds fewer bytes than its extent needs: that level is left cleared");
    // A partial set of levels: the rest is cleared first, so every subresource is defined.
    if (regions.size() < static_cast<size_t>(desc.mips) * desc.layers) ClearImage(image);
    UploadImage(image, regions.data(), regions.size(), blob.data(), at);
    return true;
}

// Slot 4: the pixel stage's texture and sampler at `slot` (dx11 PSSetSamplers + PSSetShaderResources).
void ImageBufferData::Bind(void* context, unsigned slot) {
    (void)context;   // never read by retail either
    DrawSetTexture(slot, this);
}

// Slot 5: the dynamic textures' refresh (dx11: Map DISCARD for a dynamic owner, UpdateSubresource
// otherwise; both replace mip 0 from `data`, `width * 4` bytes a row).
bool ImageBufferData::Update(void* data, int width, int height) {
    if (!image.image || !data || width <= 0 || height <= 0) return true;
    const uint32_t w = std::min(static_cast<uint32_t>(width), image.width);
    const uint32_t h = std::min(static_cast<uint32_t>(height), image.height);
    std::vector<uint8_t> rows(static_cast<size_t>(w) * h * 4);
    const auto* src = static_cast<const uint8_t*>(data);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rows.data() + static_cast<size_t>(y) * w * 4, src + static_cast<size_t>(y) * width * 4, w * 4);
    ImageRegion r;
    r.width = w;
    r.height = h;
    UploadImage(image, &r, 1, rows.data(), rows.size());
    return true;
}

// Slot 6 (dx11 0x180002ef0: a staging copy, Map READ_WRITE, rows `width * 4` bytes apart). Under the
// device lock (the Metal backend's rule): a caller outside the frame waits for the recording thread's
// frame to end; the recording thread itself (minimap, save thumbnail, screenshot) reads inside it.
bool ImageBufferData::Read(void* dst, int width, int height) {
    Video::Device* const dev = Video::Device::_singleton;
    dev->Lock();
    const bool ok = DrawReadImage(this, dst, width, height);
    dev->Unlock();
    return ok;
}

// ---- IndexBufferData -----------------------------------------------------------------------

IndexBufferData::~IndexBufferData() {
    DrawForget(this);
    ReleaseStore(store);
}

bool IndexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    return CreateBuffer(this, sizeBytes, dynamic, data, kUsage);
}

bool IndexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    return SetBufferData(this, sizeBytes, offsetBytes, src);
}

void* IndexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    return LockBuffer(this, sizeBytes, outByteOffset);
}

// Host-coherent memory: nothing to flush.
void IndexBufferData::Unlock() {
}

// dx11: IASetIndexBuffer(buffer, R16_UINT, 0).
void IndexBufferData::Bind(void* context) {
    (void)context;
    DrawSetIndexBuffer(this);
}

void IndexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer, kUsage);
}

// ---- VertexBufferData ----------------------------------------------------------------------

VertexBufferData::~VertexBufferData() {
    DrawForget(this);
    ReleaseStore(store);
}

bool VertexBufferData::Create(uint32_t sizeBytes, uint8_t dynamic, const void* data) {
    return CreateBuffer(this, sizeBytes, dynamic, data, kUsage);
}

bool VertexBufferData::SetData(int32_t sizeBytes, int32_t offsetBytes, const void* src) {
    return SetBufferData(this, sizeBytes, offsetBytes, src);
}

void* VertexBufferData::Lock(int32_t sizeBytes, int32_t* outByteOffset) {
    return LockBuffer(this, sizeBytes, outByteOffset);
}

void VertexBufferData::Unlock() {
}

// dx11: IASetVertexBuffers(startSlot, 1, buffer, stride, offset).
void VertexBufferData::Bind(void* context, uint32_t startSlot, uint32_t offsetBytes, uint32_t strideBytes) {
    (void)context;
    DrawSetVertexStream(startSlot, this, offsetBytes, strideBytes);
}

void VertexBufferData::Serialize(IO::Serializer* serializer) {
    SerializeBuffer(this, serializer, kUsage);
}

// ---- ConstantBufferData --------------------------------------------------------------------

ConstantBufferData::~ConstantBufferData() {
    DrawForget(this);
}

// Slot 1 (dx11): lay the constants out back to back (each entry's offset is the running total of the
// sizes before it; the engine reads those offsets). The storage is the frame's ring (draw.cpp).
bool ConstantBufferData::Create() {
    Video::ConstantBuffer* owner = _owner;
    int32_t running = 0;
    for (int i = 0; i < owner->_constantCount; ++i) {
        Video::ConstantBuffer::ConstantInfo& entry = owner->_constantData[i];
        entry.offset = running;
        running += entry.size;
    }
    return true;
}

// Slot 2 (dx11): UpdateSubresource of the whole bound buffer from `srcData`: register count * 16
// bytes, or for a banked buffer the rung the size picks (bankStride * (size / bankStride)). Those
// bytes are kept and copied into the frame's ring at the draw that reads them.
void ConstantBufferData::Bind(void* context, const void* srcData, int32_t first, int32_t sizeBytes) {
    (void)context;
    (void)first;   // never read by retail
    const Video::ConstantBuffer* owner = _owner;
    int32_t bytes = owner->_registerCount << 4;
    if (owner->_banked && owner->_bankStride > 0) bytes = owner->_bankStride * (sizeBytes / owner->_bankStride);
    if (bytes > static_cast<int32_t>(kConstantBufferRange)) {
        LogOnce(4, "a constant buffer larger than the descriptor range is cut to it (pipelines.h kConstantBufferRange)");
        bytes = static_cast<int32_t>(kConstantBufferRange);
    }
    if (bytes < 0) bytes = 0;
    const auto* src = static_cast<const uint8_t*>(srcData);
    shadow.assign(src, src + bytes);
    ++generation;
    if (owner->_uploadIndex >= 0) {
        DrawSetConstants(this, owner->_bindToVertex != 0, static_cast<uint32_t>(owner->_uploadIndex));
    }
}

int32_t ConstantBufferData::SlotIndexPlusOne() {
    return _owner->_uploadIndex + 1;
}

// ---- RenderBufferData (Bind / Resolve are in device.cpp with the targets) -------------------

// Retail's destructor is empty (the views belong to the images). Here the bound target names the
// owner, so a render buffer released while bound stops being the target (draw.cpp).
RenderBufferData::~RenderBufferData() {
    DrawForget(_owner);
}

bool RenderBufferData::Slot1() {
    return true;
}

// ---- VertexLayoutData ----------------------------------------------------------------------

VertexLayoutData::~VertexLayoutData() {
    DrawForget(this);
}

void VertexLayoutData::Bind(void* context) {
    (void)context;
    DrawSetLayout(this);
}

// dx11: one input element per engine element, element i as (TEXCOORD, i), format from the table,
// stream and offset from the element; nothing is made without a signature. Here the list is
// interned for the pipeline key; the locations come from the vertex shader's reflection when a
// pipeline is built (pipelines.cpp BuildVertexInput).
void VertexLayoutData::PlatformCreate() {
    const Video::VertexLayout* owner = _owner;
    layoutHash = 0;
    streamMask = 0;
    if (owner->_signature.GetSize() == 0) return;
    int count = owner->_elements.GetSize();
    if (count > 64) {
        std::fprintf(stderr, "[Vulkan] vertex layout: %d elements exceeds the original's 64; clamping\n", count);
        count = 64;
    }
    std::vector<LayoutElement> elements(static_cast<size_t>(count));
    const Video::VertexLayout::Element* elems = owner->_elements.GetData();
    for (int i = 0; i < count; ++i) {
        elements[i].format = static_cast<uint8_t>(elems[i].format);
        elements[i].stream = static_cast<uint8_t>(elems[i].stream);
        elements[i].offset = static_cast<uint16_t>(elems[i].offset);
        if (elems[i].stream >= 0 && elems[i].stream < 16) streamMask |= 1u << elems[i].stream;
    }
    layoutHash = InternLayout(elements.data(), elements.size());
}

// ---- the programs (P1) -------------------------------------------------------------------------
// The slot roles are src/render/dx11/shaderprograms.cpp's: PlatformCreate makes one shader per
// permutation blob the owner has (Video::ShaderProgram::_byteCode, at most four), SKIPPING the
// permutations past the owner's count rather than stopping, and returns false on the first failure
// with the shaders already made left in place (the destructor releases them). Bind keeps the DX11
// fallback: the requested permutation when it was built, else permutation 0, with no bounds check
// on the request (retail has none; a clamp would hide a caller bug).
//
// A blob goes through the shader registry (pipelines.h) first: a blob another program already
// registered is shared, not converted again. Otherwise Shaders::LoadOrConvert (shader_convert.h): the
// user's derived-data cache (<Documents>\Banished\Cache; none under the faithful profile, which reads
// and writes no cache: every boot then converts), else dxbc-spirv, stored for the next start.

namespace {

std::atomic<unsigned> g_programModules{0};    // modules made since the last summary
std::atomic<unsigned> g_programCacheHits{0};
std::atomic<unsigned> g_programShared{0};
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
        const uint64_t hash = ShaderHash(bytes, size, stage);
        ReleaseShader(self->shaders[i]);   // a second PlatformCreate replaces, it does not leak
        self->shaders[i] = AcquireShader(hash);
        if (self->shaders[i]) {
            g_programShared.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        std::vector<uint32_t> spirv;
        Shaders::ShaderReflection reflection;
        bool hit = false;
        std::string why;
        if (!Shaders::LoadOrConvert(cache, bytes, size, stage, &spirv, &reflection, &hit, &why)) {
            g_programFailures.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[Vulkan] %s program %p permutation %d (%zu bytes): %s\n", Shaders::StageName(stage),
                         static_cast<const void*>(owner), i, size, why.c_str());
            return false;
        }
        VkShaderModule module = CreateShaderModule(spirv, &why);
        if (module == VK_NULL_HANDLE) {
            g_programFailures.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "[Vulkan] %s program %p permutation %d: %s\n", Shaders::StageName(stage),
                         static_cast<const void*>(owner), i, why.c_str());
            return false;
        }
        self->shaders[i] = AddShader(hash, stage, module, reflection, bytes, size);
        g_programModules.fetch_add(1, std::memory_order_relaxed);
        if (hit) g_programCacheHits.fetch_add(1, std::memory_order_relaxed);
        if (g_programLogLines.fetch_add(1, std::memory_order_relaxed) < 4) {
            std::fprintf(stderr, "[Vulkan] %s program permutation %d: %s, %zu SPIR-V words, cb 0x%x tex 0x%x smp 0x%x\n",
                         Shaders::StageName(stage), i, hit ? "cache" : "converted", spirv.size(),
                         reflection.constantBufferMask, reflection.textureMask, reflection.samplerMask);
        }
    }
    return true;
}

template <class Data>
void BindProgram(Data* self, int permutation) {
    self->boundPermutation = self->shaders[permutation] != nullptr ? permutation : 0;
}

template <class Data>
void DestroyProgram(Data* self) {
    DrawForget(self);
    for (int i = 0; i < kPermutationCount; ++i) {
        ReleaseShader(self->shaders[i]);
        self->shaders[i] = nullptr;
    }
}

}  // namespace

void LogProgramSummary() {
    const unsigned made = g_programModules.exchange(0, std::memory_order_relaxed);
    const unsigned hits = g_programCacheHits.exchange(0, std::memory_order_relaxed);
    const unsigned shared = g_programShared.exchange(0, std::memory_order_relaxed);
    const unsigned failed = g_programFailures.exchange(0, std::memory_order_relaxed);
    if (made == 0 && failed == 0 && shared == 0) return;
    std::fprintf(stderr,
                 "[Vulkan] programs: %u shader module(s), %u from the cache, %u converted, %u shared, %u failed\n",
                 made, hits, made - hits, shared, failed);
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
    DrawSetVertexProgram(this);
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
    DrawSetPixelProgram(this);
}

// ---- RenderStateData -----------------------------------------------------------------------

RenderStateData::~RenderStateData() {
    DrawForget(this);
}

// dx11 Create: the blend (one render target; colour and alpha from the same factors), rasterizer
// and depth-stencil descriptions from the engine's state, through renderstatedata.cpp's tables.
// D3D11 REJECTS a blend description whose alpha factors end in _COLOR (CreateBlendState fails, so
// retail binds a NULL blend state: blending off, every channel written); that outcome is kept.
bool RenderStateData::Create() {
    const Video::RenderState* rs = _owner;
    StateBlock s;
    const int src = rs->_alphaBlend.srcBlend;
    const int dst = rs->_alphaBlend.dstBlend;
    const auto colorFactor = [](int f) {
        return f == 2 || f == 3 || f == 6 || f == 7;
    };
    if (colorFactor(src) || colorFactor(dst)) {
        LogOnce(8, "a render state blends alpha with a _COLOR factor: D3D11 rejects it, so it draws unblended "
                   "as on DX11");
        s.blendEnable = 0;
        s.srcBlend = VK_BLEND_FACTOR_ONE;
        s.dstBlend = VK_BLEND_FACTOR_ZERO;
        s.blendOp = VK_BLEND_OP_ADD;
        s.writeMask = 0xF;
    } else {
        s.blendEnable = rs->_alphaBlend.enabled != 0;
        s.srcBlend = Pick(kBlendFactor, src, VK_BLEND_FACTOR_ONE);
        s.dstBlend = Pick(kBlendFactor, dst, VK_BLEND_FACTOR_ZERO);
        s.blendOp = Pick(kBlendOp, rs->_alphaBlend.blendOp, VK_BLEND_OP_ADD);
        s.writeMask = static_cast<uint8_t>(static_cast<uint8_t>(rs->_alphaBlend.writeMask) & 0x0Fu);
    }
    s.polygonMode = Pick(kFill, rs->_fillMode, VK_POLYGON_MODE_FILL);
    s.cullMode = Pick(kCull, rs->_cullMode, VK_CULL_MODE_BACK_BIT);
    s.depthBias = static_cast<int32_t>(rs->_depth.bias * 16777215.0f);
    s.slopeBias = rs->_depth.slopeBias * 4096.0f;
    s.depthTest = rs->_depth.test != 0;
    s.depthWrite = rs->_depth.write != 0;
    s.depthFunc = Pick(kCompare, rs->_depth.func, VK_COMPARE_OP_LESS_OR_EQUAL);
    s.stencilEnable = rs->_stencil.enabled != 0;
    s.frontFail = Pick(kStencilOp, rs->_stencil.failOp, VK_STENCIL_OP_KEEP);
    s.frontDepthFail = Pick(kStencilOp, rs->_stencil.depthFailOp, VK_STENCIL_OP_KEEP);
    s.frontPass = Pick(kStencilOp, rs->_stencil.passOp, VK_STENCIL_OP_KEEP);
    s.frontFunc = Pick(kCompare, rs->_stencil.func, VK_COMPARE_OP_ALWAYS);
    if (!rs->_stencil.twoSided) {
        s.backFail = VK_STENCIL_OP_KEEP;
        s.backDepthFail = VK_STENCIL_OP_KEEP;
        s.backPass = VK_STENCIL_OP_KEEP;
        s.backFunc = VK_COMPARE_OP_ALWAYS;
    } else {
        s.backFail = Pick(kStencilOp, rs->_stencil.backFailOp, VK_STENCIL_OP_KEEP);
        s.backDepthFail = Pick(kStencilOp, rs->_stencil.backDepthFailOp, VK_STENCIL_OP_KEEP);
        s.backPass = Pick(kStencilOp, rs->_stencil.backPassOp, VK_STENCIL_OP_KEEP);
        s.backFunc = Pick(kCompare, rs->_stencil.func, VK_COMPARE_OP_ALWAYS);
    }
    block = s;
    stencilReference = rs->_stencil.reference;
    scissorEnable = rs->_flag70 != 0;
    return true;
}

// dx11 Bind: OMSetBlendState + OMSetDepthStencilState(reference) + RSSetState.
void RenderStateData::Bind(void* context) {
    (void)context;
    DrawSetRenderState(this);
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
