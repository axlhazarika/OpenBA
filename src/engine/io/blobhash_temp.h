#pragma once
// ============================================================================
// TEMP INSTRUMENTS -- never merge (issue #13 per-blob hash proof)
// ============================================================================
// Every External::Load gets a hash of every value its BinaryReader handed the
// object: each scalar read, each array read (hashed once, as a whole, after the
// array slot returns -- the scalar hook is suppressed inside an array slot so
// the per-element and the batched bodies fold the same chunks), each
// SerializeBytes. At the end of the load one line `path<TAB>hash<TAB>bytes` is
// appended to blobhash_<pid>.log in the working directory (the game dir).
// Nested loads keep their own hash (saved and restored around the inner load).
// Diff two runs' logs after `sort`: the External set and every payload must be
// identical.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace BlobHash {

struct State {
    uint64_t h;
    uint64_t n;
    int32_t depth;
};

inline thread_local State t_state{0x9E3779B97F4A7C15ull, 0, 0};

inline void Fold(const void* p, uint64_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    uint64_t h = t_state.h;
    uint64_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        std::memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) {
        h = (h ^ b[i]) * 0x100000001B3ull;
    }
    h ^= n;   // the chunk length
    h *= 0x9E3779B97F4A7C15ull;
    t_state.h = h;
    t_state.n += n;
}

inline void Scalar(const void* p, uint64_t n) {
    if (t_state.depth == 0)
        Fold(p, n);
}

struct ArrayScope {
    const void* p;
    uint64_t n;
    ArrayScope(const void* p_, int count, uint64_t elem)
        : p(p_), n(count > 0 ? static_cast<uint64_t>(count) * elem : 0) {
        ++t_state.depth;
    }
    ~ArrayScope() {
        --t_state.depth;
        if (t_state.depth == 0)
            Fold(p, n);
    }
};

inline std::mutex& FileLock() {
    static std::mutex m;
    return m;
}

inline FILE* File() {
    static FILE* f = nullptr;
    if (f == nullptr) {
        char name[64];
        std::snprintf(name, sizeof name, "blobhash.log");
        f = std::fopen(name, "w");
    }
    return f;
}

struct LoadScope {
    State saved;
    const char* path;
    explicit LoadScope(const char* p) : saved(t_state), path(p) {
        t_state = State{0x9E3779B97F4A7C15ull, 0, 0};
    }
    ~LoadScope() {
        {
            std::lock_guard<std::mutex> g(FileLock());
            FILE* f = File();
            if (f != nullptr) {
                std::fprintf(f, "%s\t%016llx\t%llu\n", path ? path : "(null)",
                             static_cast<unsigned long long>(t_state.h),
                             static_cast<unsigned long long>(t_state.n));
                std::fflush(f);
            }
        }
        t_state = saved;
    }
};

}  // namespace BlobHash
