// src/ext/heap_slack/heap_slack.cpp -- ext.heap_slack (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/HEAPSLACK.md). heap_slack.h has the contract.
//
// Two marks per range, as byte offsets from its base:
//   faithful  retail's MemoryRange::committed, kept by the allocator exactly as retail does;
//             the seam receives it as `have` (commit) or `have` -> `need` (decommit)
//   real      what the platform really has committed; never below faithful
// Pages in [faithful, real) are the slack: committed, but uncommitted in retail. Retail's next
// commit over them would hand out kernel-zeroed pages, so the seam zeroes them before the
// allocator hands them out. Nothing below the faithful mark is ever touched.

#include "ext/heap_slack/heap_slack.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ext/registry.h"
#include "system/MemoryAllocator.h"

namespace Ext { namespace HeapSlack {

namespace {

using rt::system::RtSysMemPlatform;

struct RangeMark {
    uint8_t* base;
    uint64_t real;
};

// Retail adds a range only when every free block is too small; a MegaMod boot reaches a few
// dozen. A range past the table is passed through untouched (its real mark is its faithful one).
constexpr int kMaxRanges = 256;

RangeMark g_marks[kMaxRanges];
int       g_markCount = 0;
int       g_lastHit = 0;
uint64_t  g_budget = kBudgetBytes;
uint64_t  g_slack = 0;   // sum over the table of (real - faithful)
uint64_t  g_page = 0;
Stats     g_stats = {};

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

// The mark of the range at `base`. A range seen for the first time has never had slack (the seam
// is the only thing that makes any), so its real mark is its faithful one.
RangeMark* Find(uint8_t* base, uint64_t faithful) {
    if (g_lastHit < g_markCount && g_marks[g_lastHit].base == base) return &g_marks[g_lastHit];
    for (int i = 0; i < g_markCount; ++i) {
        if (g_marks[i].base == base) {
            g_lastHit = i;
            return &g_marks[i];
        }
    }
    if (g_markCount == kMaxRanges) return nullptr;
    RangeMark* m = &g_marks[g_markCount];
    m->base = base;
    m->real = faithful;
    g_lastHit = g_markCount;
    ++g_markCount;
    return m;
}

void PlatformCommit(const RtSysMemPlatform& plat, uint8_t* at, uint64_t bytes) {
    const uint64_t t0 = NowNs();
    plat.commit(at, bytes);
    g_stats.commitNs += NowNs() - t0;
    ++g_stats.commits;
    g_stats.commitBytes += bytes;
}

void PlatformDecommit(const RtSysMemPlatform& plat, uint8_t* at, uint64_t bytes) {
    const uint64_t t0 = NowNs();
    plat.decommit(at, bytes);
    g_stats.decommitNs += NowNs() - t0;
    ++g_stats.decommits;
    g_stats.decommitBytes += bytes;
}

// Retail: plat.commit(base + have, need - have). Pages of that span still really committed are
// zeroed (retail's would arrive zero from the kernel); the rest is committed by the platform.
void Commit(const RtSysMemPlatform& plat, uint8_t* base, uint64_t have, uint64_t need) {
    ++g_stats.retailCommits;
    g_stats.retailCommitBytes += need - have;
    RangeMark* m = Find(base, have);
    if (m == nullptr) {
        ++g_stats.untracked;
        PlatformCommit(plat, base + have, need - have);
        return;
    }
    uint64_t real = m->real;
    const uint64_t zeroTo = real < need ? real : need;
    if (zeroTo > have) {
        const uint64_t t0 = NowNs();
        std::memset(base + have, 0, static_cast<size_t>(zeroTo - have));
        g_stats.zeroNs += NowNs() - t0;
        g_stats.zeroedBytes += zeroTo - have;
    }
    if (need > real) {
        PlatformCommit(plat, base + real, need - real);
        real = need;
    }
    g_slack -= m->real - have;
    g_slack += real - need;
    m->real = real;
}

// Retail: plat.decommit(base + need, have - need). The pages stay committed while the total slack
// fits the budget; past it, this range keeps what still fits and the rest is decommitted.
void Decommit(const RtSysMemPlatform& plat, uint8_t* base, uint64_t need, uint64_t have) {
    ++g_stats.retailDecommits;
    g_stats.retailDecommitBytes += have - need;
    RangeMark* m = Find(base, have);
    if (m == nullptr) {
        ++g_stats.untracked;
        PlatformDecommit(plat, base + need, have - need);
        return;
    }
    uint64_t real = m->real;
    g_slack -= real - have;
    const uint64_t others = g_slack;
    if (others + (real - need) > g_budget) {
        if (g_page == 0) g_page = plat.pageSize();
        uint64_t keep = g_budget > others ? g_budget - others : 0;
        keep -= keep % g_page;
        const uint64_t newReal = need + keep;   // < real: real - need exceeded keep
        PlatformDecommit(plat, base + newReal, real - newReal);
        real = newReal;
    }
    g_slack += real - need;
    if (g_slack > g_stats.peakSlack) g_stats.peakSlack = g_slack;
    m->real = real;
}

const rt::system::MemoryAllocatorCommitSeam kSeam = { &Commit, &Decommit };

unsigned long long U(uint64_t v) {
    return static_cast<unsigned long long>(v);
}

// One line at exit, plain integers (the perf build keeps stderr).
void Dump() {
    const Stats s = g_stats;
    std::fprintf(stderr,
                 "[HeapSlack] retail commits=%llu bytes=%llu decommits=%llu bytes=%llu | platform commits=%llu "
                 "bytes=%llu decommits=%llu bytes=%llu | zeroed=%llu zero_us=%llu commit_us=%llu "
                 "decommit_us=%llu peak_slack=%llu untracked=%llu ranges=%d\n",
                 U(s.retailCommits), U(s.retailCommitBytes), U(s.retailDecommits), U(s.retailDecommitBytes),
                 U(s.commits), U(s.commitBytes), U(s.decommits), U(s.decommitBytes), U(s.zeroedBytes),
                 U(s.zeroNs / 1000), U(s.commitNs / 1000), U(s.decommitNs / 1000), U(s.peakSlack),
                 U(s.untracked), g_markCount);
    std::fflush(stderr);
}

}  // namespace

const rt::system::MemoryAllocatorCommitSeam* Seam() {
    return &kSeam;
}

void Reset(uint64_t budgetBytes) {
    g_markCount = 0;
    g_lastHit = 0;
    g_budget = budgetBytes;
    g_slack = 0;
    g_stats = {};
}

Stats Read() {
    return g_stats;
}

void Sync() {
    if (!Ext().heap_slack) return;
    RtSystemAllocatorSetCommitSeam(&kSeam);
    std::atexit(&Dump);
    std::fprintf(stderr, "[HeapSlack] on: budget %llu MB above retail's committed marks\n", U(g_budget >> 20));
}

}}  // namespace Ext::HeapSlack
