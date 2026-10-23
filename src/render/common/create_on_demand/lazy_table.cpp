// ============================================================================
// src/render/common/create_on_demand/lazy_table.cpp -- ext.create_on_demand's table of deferred
// GPU creates (docs/extensions/handins/CREATEONDEMAND.md). The contract is in lazy_table.h.
// ============================================================================
// NOT RETAIL.
// ============================================================================

#include "create_on_demand/lazy_table.h"

#include <chrono>

namespace CreateOnDemand {

namespace {

uint64_t NowNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

}  // namespace

LazyTable::LazyTable(int workers) : _workers(workers < 0 ? 0 : workers) {}

LazyTable::~LazyTable() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _workCv.notify_all();
    for (std::thread& t : _threads) {
        t.join();
    }
}

void LazyTable::Add(const void* obj, std::unique_ptr<Record> record) {
    std::unique_lock<std::mutex> lock(_mutex);
    // A running record is never replaced under its thread (the caller's Forget already waited).
    _doneCv.wait(lock, [&] {
        auto it = _entries.find(obj);
        return it == _entries.end() || it->second.state != State::Running;
    });
    const uint64_t bytes = record->Bytes();
    Entry& e = _entries[obj];
    if (e.record != nullptr) {
        // The caller Forgot obj first; a second record for a live one replaces it unrun.
        _stats.pendingBytes -= e.record->Bytes();
        --_stats.pending;
        ++_stats.forgotten;
    } else {
        _count.fetch_add(1, std::memory_order_release);
    }
    e.record = std::move(record);
    e.state  = State::Idle;
    ++_stats.recorded;
    _stats.recordedBytes += bytes;
    ++_stats.pending;
    _stats.pendingBytes += bytes;
}

void LazyTable::RunClaimed(std::unique_lock<std::mutex>& lock, const void* obj, bool onUse) {
    Record* const record = _entries.find(obj)->second.record.get();
    ++_running;
    lock.unlock();
    const uint64_t t0 = NowNs();
    const Outcome outcome = record->Materialize();
    const uint64_t dt = NowNs() - t0;
    lock.lock();
    --_running;
    _stats.materialNs += dt;
    if (outcome == Outcome::Retry) {
        // Kept, unpublished: the next use (or prefetch) after the device is back makes it.
        ++_stats.retried;
        _entries.find(obj)->second.state = State::Idle;
        _doneCv.notify_all();
        return;
    }
    const bool ok = outcome == Outcome::Made;
    record->Publish();
    const uint64_t bytes = record->Bytes();
    _entries.erase(obj);
    _count.fetch_sub(1, std::memory_order_release);
    --_stats.pending;
    _stats.pendingBytes -= bytes;
    if (!ok) ++_stats.failed;
    if (onUse) {
        ++_stats.onUse;
        _stats.onUseNs += dt;
        if (dt > _stats.worstOnUseNs) _stats.worstOnUseNs = dt;
    } else {
        ++_stats.prefetched;
    }
    if (ok) _stats.materialBytes += bytes;
    _doneCv.notify_all();
}

void LazyTable::EnsureSlow(const void* obj) {
    std::unique_lock<std::mutex> lock(_mutex);
    bool waited = false;
    for (;;) {
        auto it = _entries.find(obj);
        if (it == _entries.end()) return;
        if (it->second.state == State::Running) {
            if (!waited) ++_stats.waited;
            waited = true;
            _doneCv.wait(lock);
            continue;
        }
        if (_paused) return;   // no device: the use goes without, as on a failed create
        it->second.state = State::Running;
        RunClaimed(lock, obj, true);
        return;
    }
}

void LazyTable::ForgetSlow(const void* obj) {
    std::unique_lock<std::mutex> lock(_mutex);
    bool waited = false;
    for (;;) {
        auto it = _entries.find(obj);
        if (it == _entries.end()) return;
        if (it->second.state == State::Running) {
            if (!waited) ++_stats.waited;
            waited = true;
            _doneCv.wait(lock);
            continue;
        }
        _stats.pendingBytes -= it->second.record->Bytes();
        --_stats.pending;
        ++_stats.forgotten;
        _entries.erase(it);   // a queued one is skipped by the worker that pops it
        _count.fetch_sub(1, std::memory_order_release);
        return;
    }
}

void LazyTable::Prefetch(const void* obj) {
    if (_count.load(std::memory_order_acquire) == 0 || _workers == 0) return;
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _entries.find(obj);
    if (it == _entries.end() || it->second.state != State::Idle || _stop) return;
    it->second.state = State::Queued;
    _queue.push_back(obj);
    if (_threads.empty()) {
        for (int i = 0; i < _workers; ++i) {
            _threads.emplace_back([this] { WorkerMain(); });
        }
    }
    _workCv.notify_one();
}

void LazyTable::WorkerMain() {
    std::unique_lock<std::mutex> lock(_mutex);
    for (;;) {
        _workCv.wait(lock, [this] { return _stop || (!_paused && !_queue.empty()); });
        if (_stop) return;
        const void* const obj = _queue.front();
        _queue.pop_front();
        auto it = _entries.find(obj);
        // Gone (forgotten, or materialized by a use), or a later record at a reused address.
        if (it == _entries.end() || it->second.state != State::Queued) {
            if (_queue.empty()) _doneCv.notify_all();
            continue;
        }
        it->second.state = State::Running;
        RunClaimed(lock, obj, false);
    }
}

void LazyTable::Pause() {
    std::unique_lock<std::mutex> lock(_mutex);
    _paused = true;
    _doneCv.wait(lock, [this] { return _running == 0; });
}

void LazyTable::Resume() {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _paused = false;
    }
    _workCv.notify_all();
    _doneCv.notify_all();
}

void LazyTable::DrainQueue() {
    std::unique_lock<std::mutex> lock(_mutex);
    _doneCv.wait(lock, [this] { return _queue.empty() && _running == 0; });
}

LazyTable::Stats LazyTable::GetStats() {
    std::lock_guard<std::mutex> lock(_mutex);
    return _stats;
}

}  // namespace CreateOnDemand
