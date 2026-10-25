// ============================================================================
// src/ext/gather_threads/gather_threads.cpp -- ext.gather_threads: the helper pool
// ============================================================================
// NOT RETAIL. gather_threads.h has the model and the contract.
// ============================================================================
#include "ext/gather_threads/gather_threads.h"

#include "platform/common/platform_time.h"
#include "diag/diag_threads.h"   // DiagThreads::Register (block LOADSTAMP)

namespace Ext {
namespace GatherThreads {

namespace {

constexpr uint32_t kHelperStack = 0x40000;   // a CullStack is 10 KB; the rest is headroom
constexpr int64_t  kSpinMicros = 50;         // P7: an idle spin longer than this sleeps (gather_threads.h)
constexpr uint32_t kProbesPerClockRead = 64; // the clock is read once per this many idle probes

inline uint32_t TicketCount(uint64_t t) { return static_cast<uint32_t>(t >> 16) & 0xffffu; }
inline uint32_t TicketNext(uint64_t t)  { return static_cast<uint32_t>(t) & 0xffffu; }

}  // namespace

HelperPool::HelperPool(int helpers) {
    _helpers = helpers < 0 ? 0 : (helpers > Cpu::kMaxCullHelpers ? Cpu::kMaxCullHelpers : helpers);
    for (int i = 0; i < _helpers; ++i) {
        Helper& h = _h[i];
        h.pool   = this;
        h.worker = i + 1;
        h.thread = Platform::ThreadCreateSuspended(&HelperPool::ThreadMain, &h, kHelperStack);
        if (h.thread == nullptr) {   // no thread: run with the ones we have
            _helpers = i;
            break;
        }
        Platform::ThreadSetNormalPriority(h.thread);
        Platform::ThreadResume(h.thread);
    }
}

HelperPool::~HelperPool() {
    Park();
    {
        std::lock_guard<std::mutex> lk(_m);
        _quit = true;
    }
    _cv.notify_all();
    for (int i = 0; i < _helpers; ++i) {
        Platform::ThreadJoin(_h[i].thread);
        Platform::ThreadClose(_h[i].thread);
    }
}

unsigned int HelperPool::ThreadMain(void* arg) {
    Helper* h = static_cast<Helper*>(arg);   // the thread entry's void* context (Platform::ThreadEntry)  // CAST-OK: user_data
    DiagThreads::Register(DiagThreads::kCull);   // names this thread for the sampler
    h->pool->HelperLoop(h->worker);
    return 0;
}

void HelperPool::Wake() {
    if (_awake.load(std::memory_order_relaxed) || _helpers == 0) return;
    // `_awake` before the epoch: a helper that sees the new epoch (under the lock)
    // sees `_awake` true, so it cannot go back to sleep on this wake.
    _awake.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(_m);
        ++_epoch;
    }
    _cv.notify_all();
    ++_wakes;
}

void HelperPool::Park() {
    // Called between passes only (every task of the last pass has finished).
    _awake.store(false, std::memory_order_release);
}

void HelperPool::HelperLoop(int32_t worker) {
    uint64_t seen = 0;
    int64_t freq = 0;
    Platform::PerfFrequency(&freq);
    const int64_t spinTicks = freq / 1000000 * kSpinMicros;
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(_m);
            _cv.wait(lk, [&] { return _quit || _epoch != seen; });
            if (_quit) return;
            seen = _epoch;
        }
        // The frame: spin on the ticket until Park. A pause per probe, no syscall; P7: an idle spin
        // past kSpinMicros sleeps until the ticket changes (the next Publish), a Wake or quit.
        uint32_t idle = 0;
        int64_t idleSince = 0;
        while (_awake.load(std::memory_order_acquire)) {
            int32_t task;
            if (Claim(&task)) {
                _fn(_ctx, task, worker);   // read after the claim: this pass's
                _done.fetch_add(1, std::memory_order_release);
                idle = 0;
                continue;
            }
            Platform::CpuRelax();
            if (++idle % kProbesPerClockRead != 0) continue;
            int64_t now = 0;
            Platform::PerfCounter(&now);
            if (idle == kProbesPerClockRead) {
                idleSince = now;
                continue;
            }
            if (now - idleSince < spinTicks) continue;
            // A Publish that landed after the failed Claim has tasks left: claim them, do not sleep.
            const uint64_t ticket = _ticket.load(std::memory_order_acquire);
            if (TicketNext(ticket) < TicketCount(ticket)) {
                idle = 0;
                continue;
            }
            std::unique_lock<std::mutex> lk(_m);
            _sleepers.fetch_add(1, std::memory_order_seq_cst);
            _cv.wait(lk, [&] {
                return _quit || _epoch != seen || !_awake.load(std::memory_order_acquire) ||
                       _ticket.load(std::memory_order_seq_cst) != ticket;
            });
            _sleepers.fetch_sub(1, std::memory_order_relaxed);
            if (_quit) return;
            // Park does not notify, so a helper can sleep here through a Park and wake on the next
            // Wake's epoch: that is a new frame, taken from here. `seen` follows it, or every later
            // wait would return at once on the stale epoch and the helper would spin on the lock.
            seen = _epoch;
            idle = 0;
        }
    }
}

bool HelperPool::Claim(int32_t* task) {
    uint64_t t = _ticket.load(std::memory_order_acquire);
    for (;;) {
        const uint32_t next = TicketNext(t);
        if (next >= TicketCount(t)) return false;
        if (_ticket.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
            *task = static_cast<int32_t>(next);
            return true;
        }
    }
}

void HelperPool::Publish(TaskFn fn, void* ctx, int32_t count) {
    if (count < 0) count = 0;
    if (count > kMaxTasks) count = kMaxTasks;
    _fn    = fn;
    _ctx   = ctx;
    _count = count;
    ++_gen;
    _done.store(0, std::memory_order_relaxed);
    // Release: _fn / _ctx / _done and the caller's task data happen-before any claim.
    _ticket.store((static_cast<uint64_t>(_gen) << 32) | (static_cast<uint64_t>(count) << 16),
                  std::memory_order_release);
    // P7: a helper that slept on an idle spin is woken (the lock orders this with its predicate).
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (_sleepers.load(std::memory_order_seq_cst) != 0) {
        _m.lock();
        _m.unlock();
        _cv.notify_all();
    }
}

HelperPool::PassResult HelperPool::Finish() {
    PassResult r;
    r.tasks = _count;
    int32_t task;
    while (Claim(&task)) {
        _fn(_ctx, task, 0);
        ++r.callerTasks;
    }
    const int32_t helperTasks = _count - r.callerTasks;
    if (_done.load(std::memory_order_acquire) < helperTasks) {
        int64_t t0 = 0, t1 = 0;
        Platform::PerfCounter(&t0);
        while (_done.load(std::memory_order_acquire) < helperTasks) Platform::CpuRelax();
        Platform::PerfCounter(&t1);
        r.waitTicks = t1 - t0;
    }
    _count = 0;
    return r;
}

}  // namespace GatherThreads
}  // namespace Ext
