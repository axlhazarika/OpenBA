// ============================================================================
// src/ext/texture_quality/texture_quality.cpp -- NOT RETAIL. ext.texture_quality
// ============================================================================
// Class A (docs/extensions/handins/TEXQUALITY.md); texture_quality.h has the contract.
// ============================================================================

#include "ext/texture_quality/texture_quality.h"

#include "create_on_demand/stream_locate.h"
#include "texture_quality/texture_quality.h"
#include "texture_quality/texture_quality_backend.h"
#include "ext/registry.h"
#include "io/Serializer.h"

#include <cstdio>

namespace Ext { namespace TextureQuality {

void ReadDropped(IO::Serializer& ser, int32_t count, Video::ImageBuffer::LevelArray& levels, int drop) {
    IO::Stream* const stream = ser._stream;
    Collection::Array<uint8_t>* const e = levels.GetData();
    int64_t skipped   = 0;
    int64_t discarded = 0;
    for (int32_t i = 0; i < drop && i < count; ++i) {
        // Retail's size read (RtVideo_SerializeBlobArray seeds it with the empty array's size).
        int32_t size = e[i]._size;
        ser.Serialize(size, nullptr);
        if (::CreateOnDemand::SkipIfWithin(stream, size)) {
            skipped += size;
            continue;
        }
        // A stream that cannot skip: the bytes the retail way, then given back.
        e[i].SetSize(size);
        ser.Serialize(size, e[i]._data, nullptr);
        discarded += e[i]._size;
        e[i].DestroyAndFree();
    }
    if (count > drop) RtVideo_SerializeBlobArray(ser, count - drop, e + drop);
    int64_t kept = 0;
    for (int32_t i = drop; i < count; ++i)
        kept += e[i]._size;
    ::TextureQuality::NoteDropped(false, skipped, discarded, kept);
}

bool ReadLevels(IO::Serializer& ser, Video::ImageBuffer& image, int32_t count, Video::ImageBuffer::LevelArray& levels) {
    // The images ImageBufferData::Create turns into a texture from their levels.
    if (count <= 0 || image._kind != 2 || image.Width() == 0 || image.Height() == 0) return false;
    const ::TextureQuality::Verdict v = ::TextureQuality::ClassifyImage(image, count);
    ::TextureQuality::NoteImage(image, count, v);
    if (v != ::TextureQuality::Verdict::Drop) return false;
    ReadDropped(ser, count, levels, ::TextureQuality::Drop());
    return true;
}

void Boot() {
    int drop = Ext().texture_quality ? Val().texture_quality_drop : 0;
    if (drop > 0 && !TextureQualityBackend::Supported()) {
        std::fprintf(stderr, "[TextureQuality] the renderer does not support dropped levels: every level kept\n");
        drop = 0;
    }
    ::TextureQuality::SetDrop(drop);
    const int active = ::TextureQuality::Drop();
    // ext.create_on_demand's reader, when installed, drops the levels itself.
    const bool ownReader = active > 0 && ::Video::ImageBuffer::_extLevelSource == nullptr;
    if (ownReader) ::Video::ImageBuffer::_extLevelSource = &ReadLevels;
    if (active > 0)
        std::fprintf(stderr,
                     "[TextureQuality] on: drop=%d, qualifying textures drop their %d largest level(s) (%s reader)\n",
                     active, active, ownReader ? "eager" : "create_on_demand's");
    else
        std::fprintf(stderr, "[TextureQuality] off: drop=0 (row %d, ext.texture_quality_drop=%d), every level kept\n",
                     Ext().texture_quality ? 1 : 0, static_cast<int>(Val().texture_quality_drop));
}

}}  // namespace Ext::TextureQuality
