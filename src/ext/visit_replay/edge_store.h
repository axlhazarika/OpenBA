#pragma once
// ============================================================================
// src/ext/visit_replay/edge_store.h -- ext.visit_replay's per-row lists
//   (NOT RETAIL, docs/extensions/handins/VISITREPLAY.md)
// ============================================================================
// For each row of ExternalFactory::_resources whose first-touch visit was
// recorded: the rows that visit handed to Add's hit arm, in order (an "edge"
// is the row pointer with bit 0 set when the call came through Add's descent).
//
// A flat open-addressing table keyed by the row pointer (linear probing,
// backward-shift deletion, as collection/ptr_index.h) and one pool of edges.
// A list's edges are `Pool()[offset .. offset + count)`; the pool only grows
// between Compact / Clear, which move or drop it.
//
// A freed row: its own list goes at once (Erase); whether a surviving list
// still names it is settled once per sweep (Settle scans the live lists), so a
// sweep that frees a row's children before the row itself costs nothing.
//
// Standard C++ only (the unit tests link it on its own). Not thread-safe: the
// caller holds the resource-load lock.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Ext {
namespace VisitReplay {

class EdgeStore {
public:
    EdgeStore() = default;
    ~EdgeStore() { delete[] _slots; }
    EdgeStore(const EdgeStore&) = delete;
    EdgeStore& operator=(const EdgeStore&) = delete;

    // `row`'s list as pool offset and count; false = no list.
    bool Find(const void* row, uint32_t* offset, uint32_t* count) {
        Settle();
        const Slot* s = Lookup(row);
        if (s == nullptr)
            return false;
        *offset = s->offset;
        *count = s->count;
        return true;
    }
    const uintptr_t* Pool() const { return _pool.data(); }

    // Appends `row`'s list to `out` and returns true, or returns false (no list).
    bool Get(const void* row, std::vector<uintptr_t>& out) {
        uint32_t off = 0;
        uint32_t cnt = 0;
        if (!Find(row, &off, &cnt))
            return false;
        out.insert(out.end(), _pool.begin() + off, _pool.begin() + off + cnt);
        return true;
    }

    // Records `row`'s list (replacing any earlier one).
    void Put(const void* row, const uintptr_t* edges, size_t count) {
        Settle();
        Remove(row);
        if (_pool.size() + count > UINT32_MAX)
            Clear();
        Slot* s = Insert(row);
        s->offset = static_cast<uint32_t>(_pool.size());
        s->count = static_cast<uint32_t>(count);
        _pool.insert(_pool.end(), edges, edges + count);
        _live += count;
        if (_pool.size() > (1u << 20) && _pool.size() > 2 * _live)
            Compact();
    }

    // `row`'s own list goes (the row stays: lists naming it are unaffected).
    void Forget(const void* row) { Remove(row); }

    // `row` is about to be freed: its own list goes now, and the next Settle
    // checks that no surviving list names it.
    void Erase(const void* row) {
        Remove(row);
        _pending.push_back(row);
    }

    // After a sweep, before the store is read or written again: a freed row that
    // a surviving list still names empties the store (that list would hand a
    // freed row, or another row at its address, to Add). True when it did.
    bool Settle() {
        if (_pending.empty())
            return false;
        const std::unordered_set<const void*> freed(_pending.begin(), _pending.end());
        _pending.clear();
        for (size_t i = 0; i < _cap; ++i) {
            const Slot& s = _slots[i];
            if (s.key == nullptr)
                continue;
            for (uint32_t j = 0; j < s.count; ++j) {
                if (freed.count(RowOf(_pool[s.offset + j])) != 0) {
                    Clear();
                    return true;
                }
            }
        }
        return false;
    }
    bool HasPending() const { return !_pending.empty(); }

    void Clear() {
        for (size_t i = 0; i < _cap; ++i)
            _slots[i] = Slot{};
        _used = 0;
        _pool.clear();
        _pending.clear();
        _live = 0;
    }

    size_t Lists() const { return _used; }
    size_t Edges() const { return _live; }
    // How many edges of the live lists name `row` (a scan: tests and diagnostics).
    size_t Named(const void* row) const {
        size_t n = 0;
        for (size_t i = 0; i < _cap; ++i) {
            const Slot& s = _slots[i];
            for (uint32_t j = 0; s.key != nullptr && j < s.count; ++j)
                n += RowOf(_pool[s.offset + j]) == row ? 1 : 0;
        }
        return n;
    }

    static const void* RowOf(uintptr_t edge) {
        return reinterpret_cast<const void*>(edge & ~uintptr_t(1));   // CAST-OK: tagged row pointer
    }
    static bool ViaAdd(uintptr_t edge) { return (edge & 1) != 0; }
    static uintptr_t MakeEdge(const void* row, bool viaAdd) {
        return reinterpret_cast<uintptr_t>(row) | (viaAdd ? 1u : 0u);   // CAST-OK: tagged row pointer
    }

private:
    struct Slot {
        const void* key = nullptr;
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    static uint64_t Hash(const void* key) {
        uint64_t h = reinterpret_cast<uintptr_t>(key);   // CAST-OK: the address is the key
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ull;
        return h ^ (h >> 33);
    }
    size_t Home(const void* key) const { return static_cast<size_t>(Hash(key)) & (_cap - 1); }

    const Slot* Lookup(const void* key) const {
        if (_cap == 0)
            return nullptr;
        for (size_t i = Home(key); _slots[i].key != nullptr; i = (i + 1) & (_cap - 1)) {
            if (_slots[i].key == key)
                return &_slots[i];
        }
        return nullptr;
    }

    // A fresh slot for `key` (absent: the caller removed it first).
    Slot* Insert(const void* key) {
        if ((_used + 1) * 2 > _cap)
            Grow();
        size_t i = Home(key);
        while (_slots[i].key != nullptr)
            i = (i + 1) & (_cap - 1);
        _slots[i] = Slot{};
        _slots[i].key = key;
        ++_used;
        return &_slots[i];
    }

    void Remove(const void* key) {
        if (_cap == 0)
            return;
        size_t i = Home(key);
        while (_slots[i].key != key) {
            if (_slots[i].key == nullptr)
                return;
            i = (i + 1) & (_cap - 1);
        }
        _live -= _slots[i].count;
        size_t hole = i;
        for (size_t j = (hole + 1) & (_cap - 1); _slots[j].key != nullptr; j = (j + 1) & (_cap - 1)) {
            const size_t home = Home(_slots[j].key);
            if (((j - home) & (_cap - 1)) >= ((j - hole) & (_cap - 1))) {
                _slots[hole] = _slots[j];
                hole = j;
            }
        }
        _slots[hole] = Slot{};
        --_used;
    }

    void Grow() {
        const size_t oldCap = _cap;
        Slot* old = _slots;
        _cap = oldCap == 0 ? 4096 : oldCap * 2;
        _slots = new Slot[_cap];
        for (size_t i = 0; i < oldCap; ++i) {
            if (old[i].key == nullptr)
                continue;
            size_t j = Home(old[i].key);
            while (_slots[j].key != nullptr)
                j = (j + 1) & (_cap - 1);
            _slots[j] = old[i];
        }
        delete[] old;
    }

    // Rewrites the pool with the live lists only.
    void Compact() {
        std::vector<uintptr_t> pool;
        pool.reserve(_live);
        for (size_t i = 0; i < _cap; ++i) {
            Slot& r = _slots[i];
            if (r.key == nullptr)
                continue;
            const uint32_t off = static_cast<uint32_t>(pool.size());
            pool.insert(pool.end(), _pool.begin() + r.offset, _pool.begin() + r.offset + r.count);
            r.offset = off;
        }
        _pool.swap(pool);
    }

    Slot* _slots = nullptr;
    size_t _cap = 0;
    size_t _used = 0;
    std::vector<uintptr_t> _pool;
    std::vector<const void*> _pending;   // erased since the last Settle
    size_t _live = 0;
};

}  // namespace VisitReplay
}  // namespace Ext
