#pragma once
// ============================================================================
// src/render/common/texture_quality/texture_quality.h -- ext.texture_quality: which textures lose
// their top mip levels, and by how many (docs/extensions/handins/TEXQUALITY.md)
// ============================================================================
// NOT RETAIL. Retail has no texture quality setting: every file-seeded texture is created with every
// level its resource file holds. With the row on, a texture that QUALIFIES is created without its
// `drop` largest levels: its top is level `drop` of the file, at (width >> drop) x (height >> drop),
// and its sampler's MaxLOD is `levels - 1 - drop`. The logical Video::ImageBuffer (width, height,
// flags, serialized at imagebuffer_vtable.cpp) is never touched, so saves and every game-side reader
// of the image's size see retail's values.
//
// THE RULE (no usage flag exists; at read time only the image's own fields are known):
//   usage 2 (a texture seeded from a resource file), 2D (cubes keep everything), mipmapped, the file
//   holds exactly GetLevelCount() levels, at least kMinLevels levels, both sides of the top level at
//   least kMinTop pixels, and both sides a multiple of (4 << drop), so every kept level is exactly the
//   D3D level of the smaller texture (BC rows stay whole blocks; compress_ao's BC4 needs % 4).
// Sprite sheets and fonts are not mipmapped (their pixel rects are 1:1 texels): the rule leaves them
// alone. Generated and rendered images are never usage 2.
//
// WHO ASKS. The engine's level reader (Video::ImageBuffer::_extLevelSource: ext.create_on_demand's,
// or ext.texture_quality's own when that row is off) skips the dropped levels' bytes; the backend's
// ImageBufferData::Create drops the same levels from its subresources and its sampler. Both call
// DropFor on the same fields, so they agree. The run's drop is fixed at boot (SetDrop, before the
// first load): the row and its value key take effect at the next start.
// ============================================================================

#include <cstdint>

namespace Video {
class ImageBuffer;
}

namespace TextureQuality {

constexpr int     kMaxDrop   = 2;
constexpr int32_t kMinLevels = 5;
constexpr int32_t kMinTop    = 512;

// Why an image keeps all its levels (or Drop: it qualifies).
enum class Verdict : uint8_t { Drop, Off, NotUsage2, Cube, NotMipmapped, LevelCount, FewLevels, Small, OddSize };
constexpr int kVerdictCount = 9;

inline const char* VerdictName(Verdict v) {
    switch (v) {
    case Verdict::Drop: return "dropped";
    case Verdict::Off: return "off";
    case Verdict::NotUsage2: return "not usage 2";
    case Verdict::Cube: return "cube";
    case Verdict::NotMipmapped: return "not mipmapped";
    case Verdict::LevelCount: return "level count";
    case Verdict::FewLevels: return "few levels";
    case Verdict::Small: return "small";
    case Verdict::OddSize: return "odd size";
    }
    return "?";
}

// The rule, on the image's fields (pure; the unit tests drive it). `drop` is the run's setting
// (0 = off), `levelCount` the image's GetLevelCount(), `fileLevels` the level blobs its file holds.
inline Verdict Classify(int drop, int32_t kind, int32_t dimension, uint32_t flags, int32_t width, int32_t height,
                        int32_t levelCount, int32_t fileLevels) {
    if (drop <= 0) return Verdict::Off;
    if (kind != 2) return Verdict::NotUsage2;
    if (dimension != 0) return Verdict::Cube;
    if ((flags & 1u) == 0) return Verdict::NotMipmapped;
    if (fileLevels != levelCount) return Verdict::LevelCount;
    if (levelCount < kMinLevels) return Verdict::FewLevels;
    if (width < kMinTop || height < kMinTop) return Verdict::Small;
    const int32_t unit = 4 << drop;
    if (width % unit != 0 || height % unit != 0) return Verdict::OddSize;
    return Verdict::Drop;
}

// The levels to drop: `drop` when the image qualifies, else 0.
inline int DropFor(int drop, int32_t kind, int32_t dimension, uint32_t flags, int32_t width, int32_t height,
                   int32_t levelCount, int32_t fileLevels) {
    const Verdict v = Classify(drop, kind, dimension, flags, width, height, levelCount, fileLevels);
    return v == Verdict::Drop ? drop : 0;
}

// The first `drop` of `count` elements removed, the rest moved down in order (a texture's
// subresources after the faithful loop built them for every level). Returns the new count.
template <typename T>
int DropLeading(T* data, int count, int drop) {
    if (drop <= 0 || data == nullptr) return count;
    if (drop >= count) return 0;
    for (int i = drop; i < count; ++i)
        data[i - drop] = data[i];
    return count - drop;
}

// ---- the run's setting (texture_quality.cpp) ------------------------------------------------------
// Boot only (Ext::TextureQuality::Boot), before the first load: 0..kMaxDrop, 0 = off (retail).
void SetDrop(int levels);
int  Drop();
// The rule with the run's setting over a live image whose file holds `fileLevels` level blobs.
int DropFor(const Video::ImageBuffer& image, int32_t fileLevels);
Verdict ClassifyImage(const Video::ImageBuffer& image, int32_t fileLevels);

// ---- the run's counters (the [TextureQuality] line) -----------------------------------------------
// The engine's level reader, once per usage-2 image it reads while the drop is on: the verdict (the
// first few images of each verdict, dropped ones included, are logged by name).
void NoteImage(const Video::ImageBuffer& image, int32_t fileLevels, Verdict v);
// A dropped image: the bytes of its dropped levels never decoded (`skipped`: a lazy texture's, or
// skipped in the stream) or read and thrown away (`discarded`: a stream that cannot skip), and the
// bytes of the levels it keeps.
void NoteDropped(bool lazy, int64_t skippedBytes, int64_t discardedBytes, int64_t keptBytes);
// The loader bracket's close: one line when anything changed since the last.
void PrintIfChanged(const char* when);

}  // namespace TextureQuality
