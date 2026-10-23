#pragma once
// ============================================================================
// src/ext/stream_cache/stream_record.h -- ext.stream_cache: the record of what a load consumed
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). Standard C++ only (no engine
// header): tests/unit/ext_stream_cache_test.cpp drives it with synthetic streams.
//
// A compiled resource in a package is one compressed stream (payload.h has the layout). Loading it
// consumes a few ranges of the DECOMPRESSED stream: all of it for a small description, the header
// and the inter-level size fields for a texture or a mesh whose payloads ext.create_on_demand skips.
// A Record is those ranges, in read order, with their bytes: the loader can read the same resource
// again from it instead of opening the compressed stream (the 32 KB window, the block directory,
// the LZF decode of the header block and of every block a skipped payload ends in).
//
// RECORD LAYOUT (little-endian, 8-byte aligned, as a Bundle chunk holds it):
//   +0x00  u64  entry offset in the package      } the entry's identity (EntryId): the record is
//   +0x08  u32  entry size                       } used only when all four match the entry about
//   +0x0c  u32  block count                      } to be read
//   +0x10  u64  Hash64 of the block directory    }
//   +0x18  u32  decompressed total (what CompressedStream::GetSize answers for the whole stream)
//   +0x1c  u32  segment count
//   +0x20  segment count x { u32 pos, u32 len }   in increasing pos, disjoint
//   then   the segments' bytes back to back, padded with zeros to a multiple of 8
//
// A CHUNK is a 16-byte header { 'STRC', version, record count, 0 } and that many records. A
// package's bundle is one or more chunks (one per load bracket that recorded something).
//
// WHAT IS RECORDED. Reads are noted in order; adjacent reads merge into one segment. A stream is
// stored only when every read succeeded, the reads never went backwards and the bytes consumed are
// at most kRecordMax: the per-type load table (STREAMCACHE.md section 1) puts the description-class
// streams at 0.2..2 KB and the consumed part of a texture or mesh stream under 2 KB, while CPU
// geometry (picking and edge meshes, 30..50 KB, fully read) and anything else large stays on the
// compressed path. The limit is a constant, not a setting: it is a property of the content.
// ============================================================================
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace StreamCache {

constexpr uint32_t kRecordMax     = 8192;        // bytes a record may hold (see above)
constexpr uint32_t kChunkMagic    = 0x43525453;  // 'STRC'
constexpr uint32_t kChunkVersion  = 1;
constexpr uint32_t kChunkHeader   = 16;
constexpr uint32_t kRecordHeader  = 32;

struct Segment {
    uint32_t pos;
    uint32_t len;
};

struct EntryId {
    uint64_t offset     = 0;
    uint32_t size       = 0;
    uint32_t blockCount = 0;
    uint64_t dirHash    = 0;
};

bool SameEntry(const EntryId& a, const EntryId& b);

// A record inside a chunk's bytes (the chunk outlives the view).
struct RecordView {
    EntryId        id;
    uint32_t       total    = 0;
    uint32_t       segCount = 0;
    const Segment* segs     = nullptr;   // segCount entries
    const uint8_t* bytes    = nullptr;   // the segments' bytes, back to back
    uint32_t       byteCount = 0;        // sum of the segments' len

    // Copy [pos, pos + n) of the decompressed stream out of the record; false when the range is
    // not inside one recorded segment (the consumer asked for bytes the recording never read).
    bool Read(uint32_t pos, uint32_t n, uint8_t* dst) const;
};

// One load's reads, in order.
class Recorder {
public:
    void Begin(const EntryId& id, uint32_t total);
    // A successful read of n bytes at pos (n == 0 is ignored).
    void Note(uint32_t pos, uint32_t n, const uint8_t* bytes);
    // A failed read, or a load that did not produce its object: the record is dropped.
    void Fail();
    bool Active() const { return _active; }
    bool Storable() const { return _active && !_bad && !_overflow; }
    uint32_t Consumed() const { return _consumed; }
    bool Overflowed() const { return _overflow; }
    bool Bad() const { return _bad; }
    // Appends the record to `chunk` (a chunk body under construction) when it is storable. False
    // otherwise (nothing appended). Ends the recording either way.
    bool Finish(std::vector<uint8_t>& chunk);

private:
    EntryId  _id;
    uint32_t _total    = 0;
    uint32_t _consumed = 0;
    uint32_t _last     = 0;   // end of the last noted read
    bool     _active   = false;
    bool     _bad      = false;
    bool     _overflow = false;
    std::vector<Segment> _segs;
    std::vector<uint8_t> _bytes;
};

// Chunk framing.
void     BeginChunk(std::vector<uint8_t>& chunk);              // writes the 16-byte header (count 0)
void     EndChunk(std::vector<uint8_t>& chunk, uint32_t count); // stamps the record count
uint32_t ChunkRecordCount(const std::vector<uint8_t>& chunk);  // 0 for a malformed header

// A package's records: the chunks' bytes and an index by entry offset.
class Bundle {
public:
    // Parses `chunk` (taken by move) and indexes its records; false (nothing kept) when the chunk is
    // malformed: a bad header or version, a record past the end, segments out of order.
    bool AddChunk(std::vector<uint8_t> chunk);
    // The record for `id`, or null.
    const RecordView* Find(const EntryId& id) const;
    size_t Records() const { return _index.size(); }
    size_t Chunks() const { return _chunks.size(); }
    size_t Bytes() const { return _bytes; }

private:
    std::vector<std::vector<uint8_t>>             _chunks;
    std::unordered_map<uint64_t, RecordView>      _index;   // by entry offset
    size_t                                        _bytes = 0;
};

}  // namespace StreamCache
}  // namespace Ext
