// ============================================================================
// tests/unit/ext_stream_cache_test.cpp -- ext.stream_cache's records, bundles and record stream
// ============================================================================
// Ext::StreamCache (src/ext/stream_cache/) over synthetic streams:
//   * a recording merges adjacent and overlapping reads (the overlap must agree), keeps gaps as
//     separate segments, and the record answers exactly the recorded ranges (a read across a gap
//     or past a segment fails);
//   * a failed read, a failed load, an empty recording, an overlap mismatch or more than kRecordMax
//     bytes drop the record; a recording seeded from a record widens it;
//   * a chunk of several records round-trips through a Bundle, is found by its full identity only,
//     a later chunk's record replaces an earlier one, and a malformed chunk is refused whole;
//   * a SegmentStream serves the record's bytes in the decompressed stream's coordinates, and a
//     read outside the record falls back to the engine's own CompressedStream over the same entry
//     (positioned where the read was) for the rest of the load; without an opener it fails.
// ============================================================================
#include "unit.h"

#include "ext/stream_cache/segment_stream.h"
#include "ext/stream_cache/stream_record.h"
#include "io/Stream.h"
#include "system/Compression.h"
#include "system/string_internal.h"   // RtSystem::RtMemFree: the table Compress allocated

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace Ext::StreamCache;

constexpr int kBlockBytes = 0x8000;

std::vector<uint8_t> Pattern(uint32_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7 + (i >> 9));
    return v;
}

EntryId Id(uint64_t offset) {
    EntryId id;
    id.offset     = offset;
    id.size       = 1000;
    id.blockCount = 3;
    id.dirHash    = 0x1234567890abcdefull ^ offset;
    return id;
}

// A recorder fed the reads [a,b) of `plain` in the given order; the record appended to `chunk`.
bool RecordReads(Recorder& r, const EntryId& id, const std::vector<uint8_t>& plain,
                 const std::vector<std::pair<uint32_t, uint32_t>>& reads, std::vector<uint8_t>& chunk) {
    r.Begin(id, static_cast<uint32_t>(plain.size()));
    for (const auto& rd : reads) r.Note(rd.first, rd.second, plain.data() + rd.first);
    return r.Finish(chunk);
}

void PutI32(std::vector<uint8_t>& v, size_t at, int32_t x) {
    std::memcpy(v.data() + at, &x, 4);
}

// A compressed entry as the packer writes it: the block count, the directory, the blocks.
std::vector<uint8_t> BuildEntry(const std::vector<uint8_t>& plain) {
    const int n = static_cast<int>((plain.size() + kBlockBytes - 1) / kBlockBytes);
    std::vector<uint8_t> out(4 + static_cast<size_t>(n) * 8);
    PutI32(out, 0, n);
    System::Compression comp;
    std::vector<uint8_t> packed(kBlockBytes + 64);
    for (int b = 0; b < n; ++b) {
        const size_t from = static_cast<size_t>(b) * kBlockBytes;
        const uint32_t len = static_cast<uint32_t>(std::min<size_t>(kBlockBytes, plain.size() - from));
        const int csize = comp.Compress(plain.data() + from, len, packed.data(), len);
        const uint32_t offset = static_cast<uint32_t>(out.size());
        uint16_t c16 = 0;
        if (csize > 0 && csize < static_cast<int>(len)) {
            c16 = static_cast<uint16_t>(csize);
            out.insert(out.end(), packed.begin(), packed.begin() + csize);
        } else {
            out.insert(out.end(), plain.begin() + static_cast<std::ptrdiff_t>(from),
                       plain.begin() + static_cast<std::ptrdiff_t>(from + len));
        }
        const uint16_t u16 = static_cast<uint16_t>(len);
        std::memcpy(out.data() + 4 + 8 * b, &offset, 4);
        std::memcpy(out.data() + 4 + 8 * b + 4, &c16, 2);
        std::memcpy(out.data() + 4 + 8 * b + 6, &u16, 2);
    }
    if (comp._table != nullptr) ::RtSystem::RtMemFree(comp._table);
    comp._table = nullptr;
    return out;
}

// An entry's bytes served as FileStream serves a packed entry.
struct EntryBytes final : IO::Stream {
    const uint8_t* base = nullptr;
    int32_t size = 0;
    int32_t pos  = 0;
    bool Write(int, const void*) override { return false; }
    bool Read(int n, void* dst) override {
        if (n < 0 || pos + n > size) return false;
        std::memcpy(dst, base + pos, static_cast<size_t>(n));
        pos += n;
        return true;
    }
    bool SetCurrentPos(int p) override {
        pos = p;
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(size); }
};

// The engine's reader over one entry: a CompressedMemoryStream in the state CompressedStream::Open
// leaves, with External::Load's one window.
struct EntryReader {
    EntryBytes src;
    std::vector<RtIo::CompressedStreamHeader> headers;
    std::vector<RtIo::CompressedStreamWindow> windows;
    std::vector<uint8_t> buffer;
    alignas(IO::CompressedMemoryStream) unsigned char mem[sizeof(IO::CompressedMemoryStream)];
    IO::CompressedMemoryStream* cs = nullptr;
    int opened = 0;

    void Open(const std::vector<uint8_t>& entry) {
        ++opened;
        src.base = entry.data();
        src.size = static_cast<int32_t>(entry.size());
        src.pos  = 0;
        int32_t n = 0;
        std::memcpy(&n, entry.data(), 4);
        headers.resize(static_cast<size_t>(n));
        std::memcpy(headers.data(), entry.data() + 4, static_cast<size_t>(n) * 8);
        windows.assign(1, RtIo::CompressedStreamWindow{});
        buffer.assign(kBlockBytes, 0);
        windows[0].useCount = 0;
        windows[0].start    = -1;
        windows[0].end      = -1;
        windows[0]._pad0c   = 0;
        windows[0].buffer   = buffer.data();
        cs = ::new (static_cast<void*>(mem)) IO::CompressedMemoryStream();
        cs->_direction    = 0;
        cs->_position     = 0;
        cs->_activeWindow = 0;
        cs->_source       = &src;
        cs->_windowData   = windows.data();
        cs->_windowSize   = 1;
        cs->_windowCap    = 1;
        cs->_headerData   = headers.data();
        cs->_headerSize   = n;
        cs->_headerCap    = n;
    }
    ~EntryReader() {
        if (cs == nullptr) return;
        cs->_windowData = nullptr;
        cs->_headerData = nullptr;
        cs->_windowSize = 0;
        cs->_headerSize = 0;
        cs->_source     = nullptr;
        cs->IO::CompressedMemoryStream::~CompressedMemoryStream();
    }
};

struct FallbackContext {
    EntryReader reader;
    const std::vector<uint8_t>* entry = nullptr;
};

IO::CompressedStream* OpenReader(SegmentStream& self, void* context) {
    (void)self;
    FallbackContext* const c = static_cast<FallbackContext*>(context);
    c->reader.Open(*c->entry);
    return c->reader.cs;
}

}  // namespace

UNIT_TEST(ext_stream_cache_record_merges_and_reads) {
    const std::vector<uint8_t> plain = Pattern(100000, 3);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    // header 0..12 in three reads, then a gap (a skipped payload), a 4-byte size field, a gap, a tail.
    CHECK(RecordReads(r, Id(4096), plain, {{0, 4}, {4, 4}, {8, 4}, {70000, 4}, {99990, 10}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    CHECK_EQ(b.Records(), static_cast<size_t>(1));
    const RecordView* v = b.Find(Id(4096));
    CHECK(v != nullptr);
    CHECK_EQ(v->segCount, 3u);
    CHECK_EQ(v->segs[0].pos, 0u);
    CHECK_EQ(v->segs[0].len, 12u);
    CHECK_EQ(v->segs[1].pos, 70000u);
    CHECK_EQ(v->segs[2].len, 10u);
    CHECK_EQ(v->total, 100000u);
    CHECK_EQ(v->byteCount, 26u);
    uint8_t out[16];
    CHECK(v->Read(0, 12, out));
    CHECK(std::memcmp(out, plain.data(), 12) == 0);
    CHECK(v->Read(6, 6, out));
    CHECK(std::memcmp(out, plain.data() + 6, 6) == 0);
    CHECK(v->Read(70000, 4, out));
    CHECK(std::memcmp(out, plain.data() + 70000, 4) == 0);
    CHECK(v->Read(99995, 5, out));
    CHECK(std::memcmp(out, plain.data() + 99995, 5) == 0);
    CHECK(v->Read(99990, 0, out));
    CHECK(!v->Read(10, 4, out));       // past the first segment
    CHECK(!v->Read(12, 1, out));       // inside the gap
    CHECK(!v->Read(69999, 2, out));    // across a gap's end
    CHECK(!v->Read(99995, 6, out));    // past the total
}

UNIT_TEST(ext_stream_cache_record_overlaps_merge_or_drop) {
    const std::vector<uint8_t> plain = Pattern(20000, 9);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    // out of order, overlapping and touching reads: one segment 0..40 and one 100..120
    CHECK(RecordReads(r, Id(1), plain, {{8, 8}, {0, 10}, {30, 10}, {16, 20}, {100, 10}, {110, 10}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* v = b.Find(Id(1));
    CHECK(v != nullptr);
    CHECK_EQ(v->segCount, 2u);
    CHECK_EQ(v->segs[0].pos, 0u);
    CHECK_EQ(v->segs[0].len, 40u);
    CHECK_EQ(v->segs[1].pos, 100u);
    CHECK_EQ(v->segs[1].len, 20u);
    CHECK_EQ(v->byteCount, 60u);
    std::vector<uint8_t> out(40);
    CHECK(v->Read(0, 40, out.data()));
    CHECK(std::memcmp(out.data(), plain.data(), 40) == 0);
    // an overlap whose bytes disagree drops the record
    std::vector<uint8_t> other = plain;
    other[5] ^= 0xff;
    r.Begin(Id(2), 20000);
    r.Note(0, 10, plain.data());
    r.Note(4, 10, other.data() + 4);
    CHECK(r.Bad());
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(!r.Finish(chunk2));
    CHECK_EQ(chunk2.size(), static_cast<size_t>(kChunkHeader));
}

UNIT_TEST(ext_stream_cache_record_dropped_when_not_storable) {
    const std::vector<uint8_t> plain = Pattern(20000, 9);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    const size_t header = chunk.size();
    Recorder r;
    // a failed read
    r.Begin(Id(2), 20000);
    r.Note(0, 4, plain.data());
    r.Fail();
    CHECK(!r.Finish(chunk));
    CHECK_EQ(chunk.size(), header);
    // over the limit
    r.Begin(Id(3), 20000);
    r.Note(0, kRecordMax, plain.data());
    CHECK(r.Storable());
    r.Note(kRecordMax + 8, 1, plain.data() + kRecordMax + 8);
    CHECK(r.Overflowed());
    CHECK(!r.Finish(chunk));
    CHECK_EQ(chunk.size(), header);
    // over the limit by widening
    r.Begin(Id(3), 20000);
    r.Note(0, kRecordMax, plain.data());
    r.Note(kRecordMax - 4, 8, plain.data() + kRecordMax - 4);
    CHECK(r.Overflowed());
    // nothing read
    r.Begin(Id(4), 20000);
    CHECK(!r.Finish(chunk));
    // exactly the limit, as one read
    CHECK(RecordReads(r, Id(5), plain, {{0, kRecordMax}}, chunk));
    CHECK(chunk.size() > header);
}

UNIT_TEST(ext_stream_cache_seeded_recording_widens_a_record) {
    const std::vector<uint8_t> plain = Pattern(50000, 2);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    CHECK(RecordReads(r, Id(7), plain, {{0, 16}, {40000, 8}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* old = b.Find(Id(7));
    CHECK(old != nullptr);
    // the next build reads the header, 8 more bytes after it, a new field at 20000 and the tail
    r.Seed(*old);
    CHECK_EQ(r.Consumed(), 24u);
    r.Note(0, 16, plain.data());
    r.Note(16, 8, plain.data() + 16);
    r.Note(20000, 4, plain.data() + 20000);
    r.Note(40000, 8, plain.data() + 40000);
    r.Note(40008, 4, plain.data() + 40008);
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(r.Finish(chunk2));
    EndChunk(chunk2, 1);
    CHECK(b.AddChunk(chunk2));
    CHECK_EQ(b.Records(), static_cast<size_t>(1));   // replaced, not added
    const RecordView* wide = b.Find(Id(7));
    CHECK(wide != nullptr);
    CHECK_EQ(wide->segCount, 3u);
    CHECK_EQ(wide->segs[0].len, 24u);
    CHECK_EQ(wide->segs[1].pos, 20000u);
    CHECK_EQ(wide->segs[2].pos, 40000u);
    CHECK_EQ(wide->segs[2].len, 12u);
    CHECK_EQ(wide->byteCount, 40u);
    uint8_t out[24];
    CHECK(wide->Read(0, 24, out));
    CHECK(std::memcmp(out, plain.data(), 24) == 0);
    CHECK(wide->Read(40000, 12, out));
    CHECK(std::memcmp(out, plain.data() + 40000, 12) == 0);
}

UNIT_TEST(ext_stream_cache_bundle_round_trip_and_identity) {
    const std::vector<uint8_t> plain = Pattern(5000, 1);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    uint32_t count = 0;
    for (uint64_t off = 100; off < 100 + 50 * 64; off += 64) {
        // a whole stream: one read of everything
        CHECK(RecordReads(r, Id(off), plain, {{0, 5000}}, chunk));
        ++count;
    }
    CHECK(RecordReads(r, Id(9999), plain, {{0, 16}, {4000, 8}}, chunk));
    ++count;
    EndChunk(chunk, count);
    CHECK_EQ(ChunkRecordCount(chunk), count);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    CHECK_EQ(b.Records(), static_cast<size_t>(count));
    CHECK_EQ(b.Chunks(), static_cast<size_t>(1));
    const RecordView* whole = b.Find(Id(100 + 64 * 7));
    CHECK(whole != nullptr);
    CHECK_EQ(whole->segCount, 1u);
    CHECK_EQ(whole->byteCount, whole->total);
    std::vector<uint8_t> out(5000);
    CHECK(whole->Read(0, 5000, out.data()));
    CHECK(out == plain);
    // the identity is all four fields
    EntryId other = Id(100);
    other.size = 1001;
    CHECK(b.Find(other) == nullptr);
    other = Id(100);
    other.dirHash ^= 1;
    CHECK(b.Find(other) == nullptr);
    other = Id(100);
    other.blockCount = 4;
    CHECK(b.Find(other) == nullptr);
    CHECK(b.Find(Id(101)) == nullptr);
    CHECK(b.Find(Id(100)) != nullptr);
    // a second chunk adds records and the first stays
    std::vector<uint8_t> chunk2;
    BeginChunk(chunk2);
    CHECK(RecordReads(r, Id(777), plain, {{0, 100}}, chunk2));
    EndChunk(chunk2, 1);
    CHECK(b.AddChunk(chunk2));
    CHECK_EQ(b.Records(), static_cast<size_t>(count + 1));
    CHECK(b.Find(Id(777)) != nullptr);
    CHECK(b.Find(Id(9999)) != nullptr);
    b.Clear();
    CHECK_EQ(b.Records(), static_cast<size_t>(0));
    CHECK_EQ(b.Bytes(), static_cast<size_t>(0));
    CHECK(b.Find(Id(777)) == nullptr);
}

UNIT_TEST(ext_stream_cache_bundle_refuses_malformed_chunks) {
    const std::vector<uint8_t> plain = Pattern(3000, 5);
    std::vector<uint8_t> good;
    BeginChunk(good);
    Recorder r;
    CHECK(RecordReads(r, Id(1), plain, {{0, 8}, {2000, 8}}, good));
    CHECK(RecordReads(r, Id(2), plain, {{0, 3000}}, good));
    EndChunk(good, 2);
    Bundle b;
    // bad magic
    std::vector<uint8_t> bad = good;
    bad[0] ^= 0xff;
    CHECK(!b.AddChunk(bad));
    // a count past the end
    bad = good;
    bad[8] = 9;
    CHECK(!b.AddChunk(bad));
    // truncated
    bad = good;
    bad.resize(bad.size() - 8);
    CHECK(!b.AddChunk(bad));
    // segments out of order: swap the first record's two segments
    bad = good;
    {
        uint8_t tmp[8];
        std::memcpy(tmp, bad.data() + kChunkHeader + kRecordHeader, 8);
        std::memcpy(bad.data() + kChunkHeader + kRecordHeader, bad.data() + kChunkHeader + kRecordHeader + 8, 8);
        std::memcpy(bad.data() + kChunkHeader + kRecordHeader + 8, tmp, 8);
    }
    CHECK(!b.AddChunk(bad));
    // empty and header-only
    CHECK(!b.AddChunk(std::vector<uint8_t>()));
    std::vector<uint8_t> empty;
    BeginChunk(empty);
    EndChunk(empty, 0);
    CHECK(!b.AddChunk(empty));
    CHECK_EQ(b.Records(), static_cast<size_t>(0));
    // the good one still parses after all that
    CHECK(b.AddChunk(good));
    CHECK_EQ(b.Records(), static_cast<size_t>(2));
}

UNIT_TEST(ext_stream_cache_segment_stream_serves_then_falls_back) {
    const std::vector<uint8_t> plain = Pattern(70000, 11);   // three blocks, the last short
    const std::vector<uint8_t> entry = BuildEntry(plain);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    Recorder r;
    CHECK(RecordReads(r, Id(5), plain, {{0, 100}, {65000, 100}}, chunk));
    EndChunk(chunk, 1);
    Bundle b;
    CHECK(b.AddChunk(chunk));
    const RecordView* rec = b.Find(Id(5));
    CHECK(rec != nullptr);

    FallbackContext ctx;
    ctx.entry = &entry;
    SegmentStream ss(rec, nullptr, 5, &OpenReader, &ctx);
    CHECK_EQ(ss.GetDirection(), IO::kReading);
    CHECK_EQ(ss.GetSize(), 70000u);
    std::vector<uint8_t> out(200);
    // the header from the record, in two reads
    CHECK(ss.Read(50, out.data()));
    CHECK(ss.Read(50, out.data() + 50));
    CHECK(std::memcmp(out.data(), plain.data(), 100) == 0);
    CHECK_EQ(ss.GetCurrentPos(), 100u);
    // a skipped payload, then the tail field from the record
    ss.Skip(64900);
    CHECK_EQ(ss.GetCurrentPos(), 65000u);
    CHECK(ss.Read(100, out.data()));
    CHECK(std::memcmp(out.data(), plain.data() + 65000, 100) == 0);
    CHECK(!ss.Extended());
    CHECK_EQ(ss.Consumed(), 200u);
    CHECK_EQ(ctx.reader.opened, 0);
    // a read the record never saw: the engine's stream takes over at that position
    CHECK(ss.Read(100, out.data()));
    CHECK(ss.Extended());
    CHECK_EQ(ctx.reader.opened, 1);
    CHECK(std::memcmp(out.data(), plain.data() + 65100, 100) == 0);
    CHECK_EQ(ss.GetCurrentPos(), 65200u);
    // and serves everything from then on: a read across the block boundary at 65536, a skip, a read
    // (SetCurrentPos / GetSize forward to the engine stream, which the unit build stubs: not called here)
    std::vector<uint8_t> big(400);
    CHECK(ss.Read(400, big.data()));
    CHECK(std::memcmp(big.data(), plain.data() + 65200, 400) == 0);
    CHECK_EQ(ss.GetCurrentPos(), 65600u);
    ss.Skip(100);
    CHECK_EQ(ss.GetCurrentPos(), 65700u);
    CHECK(ss.Read(4, out.data()));
    CHECK(std::memcmp(out.data(), plain.data() + 65700, 4) == 0);
    CHECK(!ss.Failed());
    CHECK_EQ(ss.Consumed(), 200u);   // the record's share only

    // no opener: the read outside the record fails and the stream is marked
    SegmentStream bare(rec, nullptr, 5, nullptr, nullptr);
    CHECK(bare.Read(100, out.data()));
    CHECK(!bare.Read(100, out.data()));
    CHECK(bare.Failed());
    CHECK(!bare.Extended());
}
