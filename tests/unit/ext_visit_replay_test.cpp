// ============================================================================
// tests/unit/ext_visit_replay_test.cpp -- ext.visit_replay's record
//   (src/ext/visit_replay/edge_store.h, docs/extensions/handins/VISITREPLAY.md)
// ============================================================================
// The model: rows with fixed reference lists (the objects' pointers) and a
// usage mask; the arm ORs a bit in and, on a clear bit, visits the row (hands
// each reference to the arm). The faithful model always visits; the replay
// model records each visit's direct arm calls in an EdgeStore and, on the next
// first touch, hands the recorded rows to the arm instead. Under random bits,
// random pre-marked rows (the provider's own pin bit, a second root's pass) and
// random roots, the two models must leave every mask identical and make the
// same arm calls in the same order. Then the store's own rules: an erased row's
// list goes, a list naming a freed row empties the store at Settle, a sweep
// that frees parents and children together does not.
// ============================================================================
#include "unit.h"
#include "ext/visit_replay/edge_store.h"

#include <cstdint>
#include <random>
#include <vector>

using Ext::VisitReplay::EdgeStore;

namespace {

struct Model {
    std::vector<std::vector<int>> refs;   // row -> the rows its visit hands to the arm
    std::vector<uint32_t> mask;
    std::vector<int> calls;               // every arm call, in order
};

void ArmFaithful(Model& m, int row, uint32_t bit) {
    m.calls.push_back(row);
    const uint32_t before = m.mask[static_cast<size_t>(row)];
    m.mask[static_cast<size_t>(row)] = before | bit;
    if ((before & bit) != 0)
        return;
    for (int c : m.refs[static_cast<size_t>(row)])
        ArmFaithful(m, c, bit);
}

// Row addresses for the store: one byte each of a static array (8-aligned is
// not needed: bit 0 is the tag, so the model uses even offsets).
const void* Addr(int row) {
    alignas(8) static char base[1 << 16];
    return &base[static_cast<size_t>(row) * 2];
}

void ArmReplay(Model& m, EdgeStore& store, std::vector<uintptr_t>* recording, int row, uint32_t bit,
               uint64_t* replays) {
    m.calls.push_back(row);
    if (recording != nullptr)
        recording->push_back(EdgeStore::MakeEdge(Addr(row), false));
    const uint32_t before = m.mask[static_cast<size_t>(row)];
    m.mask[static_cast<size_t>(row)] = before | bit;
    if ((before & bit) != 0)
        return;
    std::vector<uintptr_t> list;
    if (store.Get(Addr(row), list)) {
        ++*replays;
        for (uintptr_t e : list) {
            const int c = static_cast<int>((static_cast<const char*>(EdgeStore::RowOf(e)) -
                                            static_cast<const char*>(Addr(0))) / 2);
            ArmReplay(m, store, nullptr, c, bit, replays);
        }
        return;
    }
    std::vector<uintptr_t> mine;
    for (int c : m.refs[static_cast<size_t>(row)])
        ArmReplay(m, store, &mine, c, bit, replays);
    store.Put(Addr(row), mine.data(), mine.size());
}

}  // namespace

UNIT_TEST(ext_visit_replay_replay_equals_visit_under_premarked_rows) {
    std::mt19937 rng(20261024);
    for (int round = 0; round < 40; ++round) {
        const int n = 200 + static_cast<int>(rng() % 800);
        Model faithful;
        faithful.refs.resize(static_cast<size_t>(n));
        for (int r = 0; r < n; ++r) {
            const int k = static_cast<int>(rng() % 5);
            for (int i = 0; i < k; ++i)
                faithful.refs[static_cast<size_t>(r)].push_back(static_cast<int>(rng() % static_cast<uint32_t>(n)));
        }
        faithful.mask.assign(static_cast<size_t>(n), 0);
        Model replay = faithful;
        EdgeStore store;
        uint64_t replays = 0;
        for (int pass = 0; pass < 12; ++pass) {
            const uint32_t bit = 1u << (pass % 6);
            // a new pass: the bit is clean everywhere (Clean cleared it)
            for (int r = 0; r < n; ++r) {
                faithful.mask[static_cast<size_t>(r)] &= ~bit;
                replay.mask[static_cast<size_t>(r)] &= ~bit;
            }
            // pre-marked rows: a pin or an earlier root left the bit on some rows
            const int pre = static_cast<int>(rng() % 20);
            for (int i = 0; i < pre; ++i) {
                const size_t r = rng() % static_cast<uint32_t>(n);
                faithful.mask[r] |= bit;
                replay.mask[r] |= bit;
            }
            const int roots = 1 + static_cast<int>(rng() % 4);
            for (int i = 0; i < roots; ++i) {
                const int root = static_cast<int>(rng() % static_cast<uint32_t>(n));
                ArmFaithful(faithful, root, bit);
                ArmReplay(replay, store, nullptr, root, bit, &replays);
            }
            CHECK(faithful.mask == replay.mask);
            CHECK(faithful.calls == replay.calls);
        }
        CHECK(replays > 0);
    }
}

UNIT_TEST(ext_visit_replay_store_erase_and_settle) {
    EdgeStore s;
    const uintptr_t ab[2] = {EdgeStore::MakeEdge(Addr(1), false), EdgeStore::MakeEdge(Addr(2), true)};
    s.Put(Addr(0), ab, 2);
    CHECK_EQ(s.Named(Addr(1)), size_t(1));
    CHECK_EQ(s.Named(Addr(2)), size_t(1));
    std::vector<uintptr_t> out;
    CHECK(s.Get(Addr(0), out));
    CHECK_EQ(out.size(), size_t(2));
    CHECK(EdgeStore::ViaAdd(out[1]));
    CHECK(!EdgeStore::ViaAdd(out[0]));
    CHECK(EdgeStore::RowOf(out[1]) == Addr(2));

    // a sweep freeing the child before its parent: settles clean, the store keeps others
    const uintptr_t c[1] = {EdgeStore::MakeEdge(Addr(5), false)};
    s.Put(Addr(4), c, 1);
    s.Erase(Addr(2));
    s.Erase(Addr(0));
    CHECK(!s.Settle());
    out.clear();
    CHECK(!s.Get(Addr(0), out));
    CHECK(s.Get(Addr(4), out));
    CHECK_EQ(s.Lists(), size_t(1));

    // a freed row a surviving list still names: the store empties
    s.Erase(Addr(5));
    CHECK(s.Settle());
    out.clear();
    CHECK(!s.Get(Addr(4), out));
    CHECK_EQ(s.Lists(), size_t(0));
    CHECK_EQ(s.Edges(), size_t(0));

    // a replaced list stops counting toward its old rows
    s.Put(Addr(0), ab, 2);
    const uintptr_t only3[1] = {EdgeStore::MakeEdge(Addr(3), false)};
    s.Put(Addr(0), only3, 1);
    CHECK_EQ(s.Named(Addr(1)), size_t(0));
    CHECK_EQ(s.Named(Addr(3)), size_t(1));
    CHECK_EQ(s.Edges(), size_t(1));
    // a self-reference is a reference like any other
    const uintptr_t self[1] = {EdgeStore::MakeEdge(Addr(7), false)};
    s.Put(Addr(7), self, 1);
    s.Erase(Addr(7));
    CHECK(!s.Settle());
}
