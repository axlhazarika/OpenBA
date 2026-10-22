// TEMP INSTRUMENTS — never merge.
#include "temp_create_stats.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
namespace VideoDX11 {
namespace TempCreateStats {
namespace {
const char* kNames[kKindCount] = {"VB.immut", "VB.dyn", "VB.default", "IB.immut", "IB.dyn", "IB.default",
                                  "CB", "Tex.immut", "Tex.other", "SRV", "RTV/DSV", "Sampler"};
std::atomic<uint64_t> g_count[kKindCount];
std::atomic<uint64_t> g_bytes[kKindCount];
std::atomic<uint64_t> g_ticks[kKindCount];
std::atomic<uint64_t> g_hist[kKindCount][32];
std::atomic<uint64_t> g_total;
uint64_t g_lastDumpTotal = 0;
uint64_t g_lastDumpTick  = 0;
double Freq() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart);
}
}  // namespace
uint64_t Now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<uint64_t>(c.QuadPart);
}
void Add(Kind k, uint64_t bytes, uint64_t t0) {
    const uint64_t dt = Now() - t0;
    g_count[k].fetch_add(1, std::memory_order_relaxed);
    g_bytes[k].fetch_add(bytes, std::memory_order_relaxed);
    g_ticks[k].fetch_add(dt, std::memory_order_relaxed);
    int b = 0;
    while (b < 31 && (1ull << (b + 1)) <= bytes) ++b;
    g_hist[k][b].fetch_add(1, std::memory_order_relaxed);
    g_total.fetch_add(1, std::memory_order_relaxed);
}
void MaybeDump(bool force) {
    const uint64_t now = Now();
    static const double freq = Freq();
    const uint64_t total = g_total.load();
    if (!force && (total == g_lastDumpTotal || (now - g_lastDumpTick) < static_cast<uint64_t>(freq * 2)))
        return;
    g_lastDumpTotal = total;
    g_lastDumpTick  = now;
    std::fprintf(stderr, "[CreateStats] total=%llu\n", static_cast<unsigned long long>(total));
    for (int k = 0; k < kKindCount; ++k) {
        const uint64_t n = g_count[k].load();
        if (!n) continue;
        std::fprintf(stderr, "[CreateStats] %-10s n=%8llu bytes=%12llu ms=%9.1f us/each=%7.2f hist(log2):",
                     kNames[k], static_cast<unsigned long long>(n),
                     static_cast<unsigned long long>(g_bytes[k].load()),
                     g_ticks[k].load() * 1000.0 / freq, g_ticks[k].load() * 1e6 / freq / n);
        for (int b = 0; b < 32; ++b) {
            const uint64_t h = g_hist[k][b].load();
            if (h) std::fprintf(stderr, " %d:%llu", b, static_cast<unsigned long long>(h));
        }
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}
}  // namespace TempCreateStats
}  // namespace VideoDX11
