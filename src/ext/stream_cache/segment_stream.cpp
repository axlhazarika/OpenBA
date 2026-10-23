// ============================================================================
// src/ext/stream_cache/segment_stream.cpp -- ext.stream_cache: the record-backed stream
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). The contract is in the header.
// ============================================================================
#include "ext/stream_cache/segment_stream.h"

#include "io/Serializer.h"
#include "system/string_internal.h"   // RtSystem::RtMemAlloc / RtMemFree: the encoding, as every stream makes it

namespace Ext {
namespace StreamCache {

SegmentStream::SegmentStream(const RecordView* record, Platform::FileHandle package, int64_t entry)
    : IO::Stream(), _record(record), _package(package), _entry(entry) {
    // What every reading stream's constructor does (MemoryStream, CompressedStream): a
    // BinaryEncoding of its own, freed by ~Stream.
    void* encMem = RtSystem::RtMemAlloc(8, 4);
    IO::Encoding* enc = nullptr;
    if (encMem) enc = ::new (encMem) IO::BinaryEncoding();
    if (_encoding) RtSystem::RtMemFree(_encoding);
    _encoding  = enc;
    _direction = IO::kReading;
}

SegmentStream::~SegmentStream() = default;

bool SegmentStream::Write(int n, const void* src) {
    (void)n;
    (void)src;
    return false;
}

bool SegmentStream::Read(int n, void* dst) {
    if (n < 0) return false;
    if (n == 0) return true;
    const bool ok = _record->Read(static_cast<uint32_t>(_pos), static_cast<uint32_t>(n), static_cast<uint8_t*>(dst));
    if (!ok) {
        _failed = true;
        return false;
    }
    _pos += n;
    _consumed += static_cast<uint32_t>(n);
    return true;
}

bool SegmentStream::SetCurrentPos(int pos) {
    _pos = pos;
    return true;
}

uint32_t SegmentStream::GetCurrentPos() const {
    return static_cast<uint32_t>(_pos);
}

uint32_t SegmentStream::GetSize() const {
    return _record->total;
}

}  // namespace StreamCache
}  // namespace Ext
