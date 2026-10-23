#pragma once
// ============================================================================
// src/ext/stream_cache/segment_stream.h -- ext.stream_cache: a resource read from its record
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). An IO::Stream the serializer
// reads a compiled resource from when the stream cache holds the resource's record
// (stream_record.h): positions are the DECOMPRESSED stream's, as CompressedStream's are, so every
// consumer -- the serializer, ext.create_on_demand's Locate / Skip -- sees the coordinates it saw
// over the compressed stream. A Read outside the recorded segments fails (false, nothing copied)
// and marks the stream: the load that followed a stale record is reported, never silently kept.
//
// The stream carries the package handle and the entry offset the record was made from, which is
// what a Locator needs (create_on_demand's payloads are re-read from the mapped package exactly as
// they are when the compressed stream is read).
// ============================================================================
#include <cstdint>

#include "io/Stream.h"
#include "platform_file.h"
#include "ext/stream_cache/stream_record.h"

namespace Ext {
namespace StreamCache {

class SegmentStream final : public IO::Stream {
public:
    SegmentStream(const RecordView* record, Platform::FileHandle package, int64_t entry);
    ~SegmentStream() override;
    SegmentStream(const SegmentStream&) = delete;
    SegmentStream& operator=(const SegmentStream&) = delete;

    bool     Write(int n, const void* src) override;
    bool     Read(int n, void* dst) override;
    bool     SetCurrentPos(int pos) override;
    uint32_t GetCurrentPos() const override;
    uint32_t GetSize() const override;

    // Past a payload the renderer reads later (CreateOnDemand::Skip's shape).
    void Skip(int32_t len) { _pos += len; }

    const RecordView*    Record() const { return _record; }
    Platform::FileHandle Package() const { return _package; }
    int64_t              Entry() const { return _entry; }
    uint32_t             Consumed() const { return _consumed; }
    bool                 Failed() const { return _failed; }

private:
    const RecordView*    _record;
    Platform::FileHandle _package;
    int64_t              _entry;
    int32_t              _pos      = 0;
    uint32_t             _consumed = 0;
    bool                 _failed   = false;
};

}  // namespace StreamCache
}  // namespace Ext
