// ============================================================================
// src/ext/stream_cache/stream_record.cpp -- ext.stream_cache: records and bundles
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). The contract is in the header.
// ============================================================================
#include "ext/stream_cache/stream_record.h"

#include <cstring>

namespace Ext {
namespace StreamCache {

namespace {

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    uint8_t b[4];
    std::memcpy(b, &x, 4);
    v.insert(v.end(), b, b + 4);
}

void PutU64(std::vector<uint8_t>& v, uint64_t x) {
    uint8_t b[8];
    std::memcpy(b, &x, 8);
    v.insert(v.end(), b, b + 8);
}

uint32_t GetU32(const uint8_t* p) {
    uint32_t x;
    std::memcpy(&x, p, 4);
    return x;
}

uint64_t GetU64(const uint8_t* p) {
    uint64_t x;
    std::memcpy(&x, p, 8);
    return x;
}

}  // namespace

bool SameEntry(const EntryId& a, const EntryId& b) {
    return a.offset == b.offset && a.size == b.size && a.blockCount == b.blockCount && a.dirHash == b.dirHash;
}

// ---- RecordView ------------------------------------------------------------------------------

bool RecordView::Read(uint32_t pos, uint32_t n, uint8_t* dst) const {
    uint32_t at = 0;   // the segment's first byte in `bytes`
    for (uint32_t i = 0; i < segCount; ++i) {
        const Segment s = segs[i];
        if (pos >= s.pos && pos - s.pos <= s.len && n <= s.len - (pos - s.pos)) {
            std::memcpy(dst, bytes + at + (pos - s.pos), n);
            return true;
        }
        at += s.len;
    }
    return false;
}

// ---- Recorder --------------------------------------------------------------------------------

void Recorder::Begin(const EntryId& id, uint32_t total) {
    _id       = id;
    _total    = total;
    _consumed = 0;
    _last     = 0;
    _active   = true;
    _bad      = false;
    _overflow = false;
    _segs.clear();
    _bytes.clear();
}

void Recorder::Note(uint32_t pos, uint32_t n, const uint8_t* bytes) {
    if (!_active || _bad || _overflow || n == 0) return;
    if (pos < _last || pos + n < pos) {
        _bad = true;   // a read behind the last one: the view's forward layout cannot hold it
        return;
    }
    if (_consumed + n > kRecordMax) {
        _overflow = true;
        _bytes.clear();
        _segs.clear();
        return;
    }
    if (!_segs.empty() && _segs.back().pos + _segs.back().len == pos) {
        _segs.back().len += n;
    } else {
        _segs.push_back(Segment{pos, n});
    }
    _bytes.insert(_bytes.end(), bytes, bytes + n);
    _consumed += n;
    _last = pos + n;
}

void Recorder::Fail() {
    _bad = true;
}

bool Recorder::Finish(std::vector<uint8_t>& chunk) {
    const bool ok = Storable() && !_segs.empty();
    if (ok) {
        PutU64(chunk, _id.offset);
        PutU32(chunk, _id.size);
        PutU32(chunk, _id.blockCount);
        PutU64(chunk, _id.dirHash);
        PutU32(chunk, _total);
        PutU32(chunk, static_cast<uint32_t>(_segs.size()));
        for (const Segment& s : _segs) {
            PutU32(chunk, s.pos);
            PutU32(chunk, s.len);
        }
        chunk.insert(chunk.end(), _bytes.begin(), _bytes.end());
        while (chunk.size() % 8 != 0) chunk.push_back(0);
    }
    _active = false;
    _segs.clear();
    _bytes.clear();
    return ok;
}

// ---- chunks ----------------------------------------------------------------------------------

void BeginChunk(std::vector<uint8_t>& chunk) {
    chunk.clear();
    PutU32(chunk, kChunkMagic);
    PutU32(chunk, kChunkVersion);
    PutU32(chunk, 0);
    PutU32(chunk, 0);
}

void EndChunk(std::vector<uint8_t>& chunk, uint32_t count) {
    if (chunk.size() < kChunkHeader) return;
    std::memcpy(chunk.data() + 8, &count, 4);
}

uint32_t ChunkRecordCount(const std::vector<uint8_t>& chunk) {
    if (chunk.size() < kChunkHeader) return 0;
    if (GetU32(chunk.data()) != kChunkMagic || GetU32(chunk.data() + 4) != kChunkVersion) return 0;
    return GetU32(chunk.data() + 8);
}

// ---- Bundle ----------------------------------------------------------------------------------

bool Bundle::AddChunk(std::vector<uint8_t> chunk) {
    const uint32_t count = ChunkRecordCount(chunk);
    if (count == 0) return false;
    if (chunk.size() % 8 != 0) return false;
    std::vector<std::pair<uint64_t, RecordView>> parsed;
    parsed.reserve(count);
    const uint8_t* const base = chunk.data();
    const size_t size = chunk.size();
    size_t at = kChunkHeader;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + kRecordHeader > size) return false;
        RecordView v;
        v.id.offset     = GetU64(base + at);
        v.id.size       = GetU32(base + at + 8);
        v.id.blockCount = GetU32(base + at + 12);
        v.id.dirHash    = GetU64(base + at + 16);
        v.total         = GetU32(base + at + 24);
        v.segCount      = GetU32(base + at + 28);
        at += kRecordHeader;
        if (v.segCount == 0 || v.segCount > kRecordMax) return false;
        if (at + static_cast<size_t>(v.segCount) * 8 > size) return false;
        v.segs = reinterpret_cast<const Segment*>(base + at);   // CAST-OK: byte_io (8-byte aligned)
        at += static_cast<size_t>(v.segCount) * 8;
        uint64_t total = 0;
        uint64_t last  = 0;
        for (uint32_t s = 0; s < v.segCount; ++s) {
            const Segment seg = v.segs[s];
            if (seg.len == 0 || (s > 0 && seg.pos < last) || static_cast<uint64_t>(seg.pos) + seg.len > v.total) return false;
            last = static_cast<uint64_t>(seg.pos) + seg.len;
            total += seg.len;
        }
        if (total > kRecordMax) return false;
        v.byteCount = static_cast<uint32_t>(total);
        v.bytes = base + at;
        at += total;
        at = (at + 7) & ~static_cast<size_t>(7);
        if (at > size) return false;
        parsed.emplace_back(v.id.offset, v);
    }
    if (at != size) return false;
    _bytes += size;
    _chunks.push_back(std::move(chunk));
    for (auto& p : parsed) {
        _index[p.first] = p.second;
    }
    return true;
}

const RecordView* Bundle::Find(const EntryId& id) const {
    const auto it = _index.find(id.offset);
    if (it == _index.end()) return nullptr;
    if (!SameEntry(it->second.id, id)) return nullptr;
    return &it->second;
}

}  // namespace StreamCache
}  // namespace Ext
