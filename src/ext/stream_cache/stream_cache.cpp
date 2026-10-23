// ============================================================================
// src/ext/stream_cache/stream_cache.cpp -- ext.stream_cache (class A, next start)
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). The contract is in the header.
//
// THREADS. Loads run on the loader thread and, between brackets, on the main thread; a load nests
// (a reference inside a resource loads the referenced resource before the outer one continues), so
// each thread keeps a stack of the loads it is inside, and the read tap attributes a read to the
// stream on top of that stack. The package table, the bundles and the pending records are under one
// mutex (a lookup per load; nothing heavy under it).
// ============================================================================
#include "ext/stream_cache/stream_cache.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ext/stream_cache/segment_stream.h"
#include "ext/stream_cache/stream_record.h"
#include "ext/derived_cache/derived_cache.h"
#include "ext/derived_cache/hash64.h"
#include "ext/registry.h"
#include "create_on_demand/payload.h"
#include "create_on_demand/stream_locate.h"
#include "engine/UpdateLoop.h"
#include "io/FileStream.h"
#include "io/FileSystem.h"
#include "io/PackageFile.h"
#include "io/Stream.h"
#include "object/External.h"
#include "system/string_internal.h"   // RtSystem::RtMemAlloc: the stream's storage, freed by IO::Stream::operator delete
#include "openba_build_info.h"   // OPENBA_GIT_HASH: a record is only as good as the serializer that made it

namespace Ext {
namespace StreamCache {

namespace {

constexpr uint32_t kProducer = DerivedCache::MakeProducer('S', 'T', 'R', 'M');
constexpr uint32_t kVersion  = 1;   // the record / chunk format and what a load consumes

// One package (one identity), across remounts.
struct Package {
    uint64_t identity  = 0;
    uint32_t crc       = 0;
    uint32_t fileCount = 0;
    uint32_t chunks    = 0;   // stored chunks: loaded at first use + put this run
    Bundle   bundle;
    std::vector<uint8_t> pending;        // the chunk under construction (this bracket's records)
    uint32_t pendingCount = 0;
};

struct Slot {
    const IO::PackageFile* pkg = nullptr;   // the FileSystem's PackageFile at that pack index
    Package* package = nullptr;
};

struct Stats {
    uint64_t hitsWhole = 0, hitsElided = 0, misses = 0, loose = 0;
    uint64_t recorded = 0, recordedBytes = 0;
    uint64_t tooBig = 0, backwards = 0, readFailed = 0, loadFailed = 0;
    uint64_t servedFailed = 0;    // a read outside the record (a stale record): reported
    uint64_t chunksPut = 0, chunkBytesPut = 0;
};

DerivedCache::Store* g_store = nullptr;
std::mutex g_mu;
std::unordered_map<uint64_t, std::unique_ptr<Package>> g_packages;   // by identity
std::vector<Slot> g_slots;                                            // by pack index
Stats g_stats;
uint32_t g_buildHash = 0;
bool g_payloadsSkipped = false;   // ext.create_on_demand on: what a load consumes depends on it

// A load this thread is inside.
struct Active {
    IO::CompressedFileStream* cfs = nullptr;
    SegmentStream*            stream = nullptr;   // serving from a record
    Package*                  package = nullptr;
    Recorder                  recorder;           // recording the retail read
};

thread_local std::vector<Active> t_stack;

DerivedCache::Key ChunkKey(const Package& p, uint32_t chunk) {
    DerivedCache::Key k;
    k.producer   = kProducer;
    k.version    = kVersion;
    k.shape[0]   = p.crc;
    k.shape[1]   = g_payloadsSkipped ? 1u : 0u;
    k.shape[2]   = g_buildHash;
    k.shape[3]   = chunk;
    k.sourceSize = p.fileCount;
    k.sourceHash = p.identity;
    return k;
}

// The package's identity: its header fields, its name and its whole entry table (offset and size of
// every entry), so a rebuilt package is another identity even at the same path.
uint64_t Identity(const IO::PackageFile& pkg) {
    struct Head {
        uint32_t crc, version, userVersion, fileCount;
    } head{pkg._crc, static_cast<uint32_t>(pkg._version), static_cast<uint32_t>(pkg._userVersion),
           static_cast<uint32_t>(pkg._entries._size)};
    uint64_t h = DerivedCache::Hash64(&head, sizeof(head));
    if (pkg._name._data != nullptr) h = DerivedCache::Hash64(pkg._name._data, std::strlen(pkg._name._data), h);
    if (pkg._entries._data != nullptr && pkg._entries._size > 0) {
        // Offset and size of every entry (not the entry's name, a String pointer per run).
        std::vector<uint32_t> table(static_cast<size_t>(pkg._entries._size) * 2);
        for (int32_t i = 0; i < pkg._entries._size; ++i) {
            table[static_cast<size_t>(i) * 2]     = pkg._entries._data[i].offset;
            table[static_cast<size_t>(i) * 2 + 1] = pkg._entries._data[i].size;
        }
        h = DerivedCache::Hash64(table.data(), table.size() * sizeof(uint32_t), h);
    }
    return h;
}

// Under g_mu. The Package of pack index `index`, made (and its stored chunks loaded) on first use.
Package* PackageAt(uint32_t index) {
    IO::FileSystem* const sys = IO::FileSystem::_singleton;
    if (index >= static_cast<uint32_t>(sys->_packfiles._size)) return nullptr;
    const IO::PackageFile* const pkg = sys->_packfiles[index];
    if (pkg == nullptr) return nullptr;
    if (g_slots.size() <= index) g_slots.resize(index + 1);
    Slot& slot = g_slots[index];
    if (slot.pkg == pkg && slot.package != nullptr) return slot.package;
    const uint64_t id = Identity(*pkg);
    std::unique_ptr<Package>& p = g_packages[id];
    if (p == nullptr) {
        p.reset(new Package());
        p->identity  = id;
        p->crc       = pkg->_crc;
        p->fileCount = static_cast<uint32_t>(pkg->_entries._size);
        std::vector<uint8_t> chunk;
        while (g_store->Find(ChunkKey(*p, p->chunks), chunk)) {
            if (!p->bundle.AddChunk(std::move(chunk))) {
                std::fprintf(stderr, "[StreamCache] package %08x chunk %u malformed: ignored\n", p->crc, p->chunks);
            }
            chunk.clear();
            ++p->chunks;
        }
    }
    slot.pkg     = pkg;
    slot.package = p.get();
    return p.get();
}

IO::Stream* LoadBegin(IO::CompressedFileStream& cfs) {
    t_stack.emplace_back();
    Active& a = t_stack.back();
    a.cfs = &cfs;
    const RtIO::FileInfo& fi = cfs._file._file;
    IO::FileSystem* const sys = IO::FileSystem::_singleton;
    Platform::FileHandle handle = nullptr;
    int64_t entry = 0;
    if (!sys->PackedEntryOrigin(fi, &handle, &entry) || fi.size < 4) {
        ++g_stats.loose;
        return nullptr;
    }
    const uint8_t* const head = sys->PackedMapSpan(fi, 0, 4);
    if (head == nullptr) {
        ++g_stats.loose;
        return nullptr;
    }
    int32_t n = 0;
    std::memcpy(&n, head, 4);
    if (n <= 0 || static_cast<int64_t>(n) * 8 + 4 > fi.size) {
        ++g_stats.loose;
        return nullptr;
    }
    const uint8_t* const dir = sys->PackedMapSpan(fi, 4, n * 8);
    if (dir == nullptr) {
        ++g_stats.loose;
        return nullptr;
    }
    EntryId id;
    id.offset     = static_cast<uint64_t>(entry);
    id.size       = static_cast<uint32_t>(fi.size);
    id.blockCount = static_cast<uint32_t>(n);
    id.dirHash    = DerivedCache::Hash64(dir, static_cast<size_t>(n) * 8);
    uint16_t lastUncompressed = 0;
    std::memcpy(&lastUncompressed, dir + static_cast<size_t>(n - 1) * 8 + 6, 2);
    const uint32_t total = static_cast<uint32_t>(n - 1) * ::CreateOnDemand::kBlockBytes + lastUncompressed;

    std::lock_guard<std::mutex> lk(g_mu);
    Package* const pkg = PackageAt(static_cast<uint32_t>(fi.handle) >> 24);
    if (pkg == nullptr) {
        ++g_stats.loose;
        return nullptr;
    }
    a.package = pkg;
    const RecordView* const rec = pkg->bundle.Find(id);
    if (rec != nullptr) {
        if (rec->segCount == 1 && rec->byteCount == rec->total) ++g_stats.hitsWhole;
        else ++g_stats.hitsElided;
        // From the engine allocator: IO::Stream's class operator delete frees there
        // (root_operator_delete.cpp), as every stream the engine makes.
        void* const mem = RtSystem::RtMemAlloc(sizeof(SegmentStream), 8);
        a.stream = ::new (mem) SegmentStream(rec, handle, entry);
        return a.stream;
    }
    ++g_stats.misses;
    a.recorder.Begin(id, total);
    return nullptr;
}

void ReadTap(IO::CompressedStream& cs, int32_t pos, int32_t n, const void* dst, bool ok) {
    if (t_stack.empty()) return;
    Active& a = t_stack.back();
    if (static_cast<IO::CompressedStream*>(a.cfs) != &cs || !a.recorder.Active()) return;
    if (!ok || pos < 0 || n < 0) {
        a.recorder.Fail();
        return;
    }
    a.recorder.Note(static_cast<uint32_t>(pos), static_cast<uint32_t>(n), static_cast<const uint8_t*>(dst));
}

void LoadEnd(IO::CompressedFileStream& cfs, IO::Stream* alt, const Object::External* result) {
    if (t_stack.empty() || t_stack.back().cfs != &cfs) return;   // not a pair this extension opened
    Active a = std::move(t_stack.back());
    t_stack.pop_back();
    if (a.stream != nullptr) {
        if (a.stream->Failed() || result == nullptr) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_stats.servedFailed == 0) {
                std::fprintf(stderr, "[StreamCache] a record did not match what the load read (package %08x, "
                             "entry %lld): the record is stale\n",
                             a.package->crc, static_cast<long long>(a.stream->Entry()));
            }
            ++g_stats.servedFailed;
        }
        delete a.stream;
        (void)alt;
        return;
    }
    if (!a.recorder.Active()) return;
    if (result == nullptr) a.recorder.Fail();
    std::lock_guard<std::mutex> lk(g_mu);
    if (a.recorder.Overflowed()) {
        ++g_stats.tooBig;
    } else if (a.recorder.Bad()) {
        if (result == nullptr) ++g_stats.loadFailed;
        else ++g_stats.readFailed;
    }
    Package& p = *a.package;
    if (p.pendingCount == 0) BeginChunk(p.pending);
    const uint32_t consumed = a.recorder.Consumed();
    if (a.recorder.Finish(p.pending)) {
        ++p.pendingCount;
        ++g_stats.recorded;
        g_stats.recordedBytes += consumed;
    }
}

void LoadDone() {
    std::lock_guard<std::mutex> lk(g_mu);
    size_t bundleBytes = 0;
    size_t records = 0;
    for (auto& kv : g_packages) {
        Package& p = *kv.second;
        if (p.pendingCount > 0) {
            EndChunk(p.pending, p.pendingCount);
            std::vector<uint8_t> copy = p.pending;
            g_store->Put(ChunkKey(p, p.chunks), std::move(copy));
            ++g_stats.chunksPut;
            g_stats.chunkBytesPut += p.pending.size();
            if (!p.bundle.AddChunk(std::move(p.pending))) {
                std::fprintf(stderr, "[StreamCache] package %08x: the chunk just made does not parse\n", p.crc);
            }
            p.pending = std::vector<uint8_t>();
            p.pendingCount = 0;
            ++p.chunks;
        }
        bundleBytes += p.bundle.Bytes();
        records += p.bundle.Records();
    }
    g_store->RequestFlush();
    std::fprintf(stderr, "[StreamCache] load end (run total): hits %llu (whole %llu, elided %llu), misses %llu, "
                 "not packed %llu; recorded %llu (%.1f MB; too big %llu, backwards %llu, read failed %llu, "
                 "load failed %llu); stale %llu; packages %zu, records %zu (%.1f MB in memory), chunks put %llu "
                 "(%.1f MB)\n",
                 (unsigned long long)(g_stats.hitsWhole + g_stats.hitsElided), (unsigned long long)g_stats.hitsWhole,
                 (unsigned long long)g_stats.hitsElided, (unsigned long long)g_stats.misses,
                 (unsigned long long)g_stats.loose, (unsigned long long)g_stats.recorded,
                 g_stats.recordedBytes / 1048576.0, (unsigned long long)g_stats.tooBig,
                 (unsigned long long)g_stats.backwards, (unsigned long long)g_stats.readFailed,
                 (unsigned long long)g_stats.loadFailed, (unsigned long long)g_stats.servedFailed, g_packages.size(),
                 records, bundleBytes / 1048576.0, (unsigned long long)g_stats.chunksPut,
                 g_stats.chunkBytesPut / 1048576.0);
}

// ext.create_on_demand's Locate / Skip over a record stream: the same Locator the compressed stream
// gives (the package mapping, the entry, the decompressed position), checked against the mapped
// entry's own block directory.
bool AltLocate(IO::Stream* stream, int32_t len, ::CreateOnDemand::Locator* out) {
    SegmentStream* const ss = dynamic_cast<SegmentStream*>(stream);
    if (ss == nullptr || len < 0) return false;
    ::CreateOnDemand::PackageMap* const map = ::CreateOnDemand::Maps().Acquire(ss->Package());
    if (map == nullptr) return false;
    ::CreateOnDemand::Locator loc;
    loc.map   = map;
    loc.entry = static_cast<uint64_t>(ss->Entry());
    loc.size  = ss->Record()->id.size;
    loc.pos   = static_cast<int32_t>(ss->GetCurrentPos());
    loc.len   = len;
    if (loc.entry + loc.size > map->map.size) return false;
    const uint8_t* const bytes = map->map.data + loc.entry;
    if (!::CreateOnDemand::RangeInStream(bytes, loc.size, loc.pos, loc.len)) return false;
    *out = std::move(loc);
    return true;
}

bool AltSkip(IO::Stream* stream, int32_t len) {
    SegmentStream* const ss = dynamic_cast<SegmentStream*>(stream);
    if (ss == nullptr) return false;
    ss->Skip(len);
    return true;
}

}  // namespace

void Boot() {
    if (!Ext().stream_cache) return;
    g_store = DerivedCache::OpenShared();
    if (g_store == nullptr) {
        std::fprintf(stderr, "[StreamCache] no derived cache (faithful profile, no documents folder, or another "
                     "instance holds it): off for this run\n");
        return;
    }
    g_buildHash       = static_cast<uint32_t>(DerivedCache::Hash64(OPENBA_GIT_HASH, std::strlen(OPENBA_GIT_HASH)));
    g_payloadsSkipped = Ext().create_on_demand;
    ::Object::ExternalFactory::_extLoadBegin = &LoadBegin;
    ::Object::ExternalFactory::_extLoadEnd   = &LoadEnd;
    ::IO::CompressedStream::_extReadTap      = &ReadTap;
    ::Engine::LoaderLoop::_extLoadDone       = &LoadDone;
    ::CreateOnDemand::g_altLocate            = &AltLocate;
    ::CreateOnDemand::g_altSkip              = &AltSkip;
}

}  // namespace StreamCache
}  // namespace Ext
