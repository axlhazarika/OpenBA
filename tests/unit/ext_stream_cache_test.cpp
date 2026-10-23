// ============================================================================
// tests/unit/ext_stream_cache_test.cpp -- ext.stream_cache's records and bundles (STREAMCACHE)
// ============================================================================
// Ext::StreamCache (src/ext/stream_cache/stream_record.*) over synthetic streams:
//   * a recording of forward reads merges adjacent reads, keeps gaps as separate segments, and the
//     record answers exactly the recorded ranges (a read across a gap or past a segment fails);
//   * a read behind the last one, a failed read, a failed load or more than kRecordMax bytes drop
//     the record;
//   * a chunk of several records round-trips through a Bundle, is found by its full identity only,
//     and a malformed chunk (bad magic, truncated, segments out of order) is refused whole;
//   * a whole-stream record is one segment covering the total.
// ============================================================================
#include "unit.h"

#include "ext/stream_cache/stream_record.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace Ext::StreamCache;

std::vector<uint8_t> Pattern(uint32_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7);
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

UNIT_TEST(ext_stream_cache_record_dropped_when_not_storable) {
    const std::vector<uint8_t> plain = Pattern(20000, 9);
    std::vector<uint8_t> chunk;
    BeginChunk(chunk);
    const size_t header = chunk.size();
    Recorder r;
    // backwards
    CHECK(!RecordReads(r, Id(1), plain, {{0, 4}, {8, 4}, {4, 4}}, chunk));
    CHECK_EQ(chunk.size(), header);
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
    r.Note(kRecordMax, 1, plain.data() + kRecordMax);
    CHECK(r.Overflowed());
    CHECK(!r.Finish(chunk));
    CHECK_EQ(chunk.size(), header);
    // nothing read
    r.Begin(Id(4), 20000);
    CHECK(!r.Finish(chunk));
    // exactly the limit, as one read
    CHECK(RecordReads(r, Id(5), plain, {{0, kRecordMax}}, chunk));
    CHECK(chunk.size() > header);
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
