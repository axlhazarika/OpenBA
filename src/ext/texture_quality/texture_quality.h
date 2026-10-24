#pragma once
// src/ext/texture_quality/texture_quality.h -- ext.texture_quality (class A, next start, default OFF)
//
// NOT RETAIL (docs/extensions/handins/TEXQUALITY.md). Retail creates every file-seeded texture from
// all the mip levels its resource file holds. With the row on, a texture that qualifies (2D,
// mipmapped, at least 512 x 512, render/common/texture_quality/texture_quality.h has the rule) is
// created without its ext.texture_quality_drop largest levels (the key takes 0 = Full or 1 = Half; the
// code takes any drop N, Quarter was removed from the key by the user's ruling): a quarter of the memory, the dropped levels' bytes never decoded where the stream can skip them. The game's
// image (its size, flags, the save) is unchanged; only the GPU texture and its sampler differ.
//
// WHERE IT RUNS.
//   - The backend (render/common/texture_quality/texture_quality_backend.h): Boot asks Supported(); a
//     backend that cannot (Metal today) leaves the drop at 0.
//   - Video::ImageBuffer::_extLevelSource: with ext.create_on_demand on, its reader skips the
//     dropped levels without recording them (src/ext/create_on_demand/level_source.cpp); with it
//     off, ReadLevels below is the reader.
//   - ImageBufferData::Create (src/render/dx11/imagebufferdata.cpp): the descriptor, subresources and
//     sampler of a dropped texture.
// Never under the faithful profile (the row is off).

#include <cstdint>

#include "video/ImageBuffer.h"

namespace IO { class Serializer; }

namespace Ext { namespace TextureQuality {

// Boot only (registry_boot.cpp): after ext.create_on_demand's Boot (whose reader, when installed, does
// the drop itself) and before ext.stream_cache's (whose records are keyed on the drop).
void Boot();

// Video::ImageBuffer::_extLevelSource while the row is on and ext.create_on_demand is off. False (and
// nothing consumed) for an image that keeps its levels: retail's read follows.
bool ReadLevels(IO::Serializer& ser, Video::ImageBuffer& image, int32_t count, Video::ImageBuffer::LevelArray& levels);

// The dropped image's read, eager: the first `drop` level blobs have their size read as retail reads
// it and their bytes skipped in the stream (SkipIfWithin) or, where it cannot skip, read and freed;
// their arrays are left empty. The rest are read as retail reads them. Counts the image.
void ReadDropped(IO::Serializer& ser, int32_t count, Video::ImageBuffer::LevelArray& levels, int drop);

}}  // namespace Ext::TextureQuality
