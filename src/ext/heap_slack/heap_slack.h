#pragma once
// src/ext/heap_slack/heap_slack.h -- ext.heap_slack (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/HEAPSLACK.md). Retail's allocator decommits the top of a
// range every time a free coalesces to the range's end (ReleaseToPage, 0x1800c7c80) and commits
// it again at the next carve past the mark (CommitToPage, 0x1800c7bd0, and Allocate's inlined
// copy). With large mods that is hundreds of thousands of VirtualFree / VirtualAlloc pairs over
// the same addresses during a boot, each recommitted page then faulted in and zeroed by the kernel.
//
// With the row on, the allocator still keeps retail's committed mark exactly (MemoryRange::
// committed, every byte of the heap's own bookkeeping unchanged); this extension keeps a second,
// real mark per range and leaves pages committed above retail's mark while the total of such
// slack stays under kBudgetBytes. When retail would commit pages that are really still committed,
// the extension zeroes exactly those pages, so every allocation sees the bytes retail's would
// (a recommitted page reads zero: docs/extensions/handins/XFILEAHEAD.md section 3). When retail
// commits past the real mark the remainder is committed by the platform, as retail does.
//
// WHERE IT RUNS. The engine seam System::MemoryAllocator::_extCommitSeam (MemoryAllocator.h;
// null by default, DESIGN.md section 3 hook rule 5), set once by Sync at boot through
// RtSystemAllocatorSetCommitSeam. Every call arrives with the allocator lock held.

#include <cstdint>

namespace rt { namespace system { struct MemoryAllocatorCommitSeam; } }

namespace Ext { namespace HeapSlack {

// The most bytes, over every range, left committed above retail's marks.
constexpr uint64_t kBudgetBytes = 64ull << 20;

// What retail would have asked the platform for, what reached it, and what was zeroed instead.
struct Stats {
    uint64_t retailCommits;
    uint64_t retailCommitBytes;
    uint64_t retailDecommits;
    uint64_t retailDecommitBytes;
    uint64_t commits;
    uint64_t commitBytes;
    uint64_t decommits;
    uint64_t decommitBytes;
    uint64_t zeroedBytes;
    uint64_t zeroNs;
    uint64_t commitNs;
    uint64_t decommitNs;
    uint64_t peakSlack;
    uint64_t untracked;   // calls on a range past the table (passed through as retail's)
};

// The seam follows Ext().heap_slack (boot only: a toggle takes effect at the next start). Null
// under the faithful profile or with the row off. With the row on it also prints one
// [HeapSlack] line of Stats at exit.
void Sync();

// The seam and its state, for tests/unit/ext_heap_slack_test.cpp: the seam object, a reset of
// every mark and counter with a given budget, and the counters.
const rt::system::MemoryAllocatorCommitSeam* Seam();
void Reset(uint64_t budgetBytes);
Stats Read();

}}  // namespace Ext::HeapSlack
