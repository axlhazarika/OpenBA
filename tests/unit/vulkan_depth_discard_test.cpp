// ============================================================================
// tests/unit/vulkan_depth_discard_test.cpp -- src/render/vulkan/depth_discard.h: THE DEPTH DISCARD's
// prediction by frame-structure signature (P8)
// ============================================================================
// BUILT ONLY WITH THE VULKAN BACKEND (BANISHED_RENDER_BACKEND=vulkan), as every tests/unit/vulkan_*
// test. Pure logic: frames are scripted as sequences of structural events.
//
// The cases are the sessions P7 measured: the menu's one main-depth pass, then the game's frame
// (shadow, reflection, main, frame copy, main). P7's index rule armed on the menu and missed on the
// game's first frame (one wrong frame, then off for the session); the signature rule must change
// structure with no miss and arm again on the game's frame.
// ============================================================================
#include "unit.h"

#include "render/vulkan/depth_discard.h"

#include <vector>

using VideoVulkan::DepthDiscardPredictor;

namespace {

// Event tokens (the backend folds image-state addresses; any distinct values do here).
constexpr uint64_t kShadow = 11;
constexpr uint64_t kReflection = 12;
constexpr uint64_t kMain = 13;
constexpr uint64_t kCopy = 14;
constexpr uint64_t kMinimap = 15;

struct Event {
    uint64_t token;
    bool mainDepth;
    bool loads;
};

struct FrameResult {
    int discards = 0;
    int misses = 0;
    int lastDiscardedIndex = -1;  // the main-depth pass index that discarded (-1: none)
};

FrameResult RunFrame(DepthDiscardPredictor& p, const std::vector<Event>& events) {
    FrameResult r;
    p.BeginFrame();
    int index = 0;
    for (const Event& e : events) {
        p.Note(e.token);
        if (!e.mainDepth) continue;
        bool missed = false;
        if (p.MainDepthPass(e.loads, true, &missed)) {
            ++r.discards;
            r.lastDiscardedIndex = index;
        }
        if (missed) ++r.misses;
        ++index;
    }
    p.EndFrame();
    return r;
}

const std::vector<Event> kMenuFrame = { { kMain, true, false } };
const std::vector<Event> kGameFrame = {
    { kShadow, false, false }, { kReflection, false, false }, { kMain, true, false },
    { kCopy, false, false },   { kMain, true, true },
};
const std::vector<Event> kGameMinimapFrame = {
    { kMinimap, false, false }, { kShadow, false, false }, { kReflection, false, false },
    { kMain, true, false },     { kCopy, false, false },   { kMain, true, true },
};

}  // namespace

UNIT_TEST(vulkan_depth_discard_arms_after_stable_frames) {
    DepthDiscardPredictor p;
    for (uint32_t i = 0; i < DepthDiscardPredictor::kStableFrames; ++i)
        CHECK_EQ(RunFrame(p, kMenuFrame).discards, 0);
    const FrameResult r = RunFrame(p, kMenuFrame);
    CHECK_EQ(r.discards, 1);
    CHECK_EQ(r.misses, 0);
}

UNIT_TEST(vulkan_depth_discard_menu_to_game_no_miss_and_rearms) {
    DepthDiscardPredictor p;
    for (int i = 0; i < 600; ++i)
        RunFrame(p, kMenuFrame);
    CHECK_EQ(RunFrame(p, kMenuFrame).discards, 1);
    // The game's first frame: P7's index rule discarded pass 0 here and pass 1 loaded it.
    const FrameResult first = RunFrame(p, kGameFrame);
    CHECK_EQ(first.discards, 0);
    CHECK_EQ(first.misses, 0);
    for (uint32_t i = 1; i < DepthDiscardPredictor::kStableFrames; ++i) {
        const FrameResult r = RunFrame(p, kGameFrame);
        CHECK_EQ(r.discards, 0);
        CHECK_EQ(r.misses, 0);
    }
    const FrameResult armed = RunFrame(p, kGameFrame);
    CHECK_EQ(armed.discards, 1);
    CHECK_EQ(armed.lastDiscardedIndex, 1);   // only the last main-depth pass
    CHECK_EQ(armed.misses, 0);
    CHECK(!p.Off());
    CHECK_EQ(p.Mispredicts(), 0u);
}

UNIT_TEST(vulkan_depth_discard_occasional_prefix_change_keeps_armed) {
    DepthDiscardPredictor p;
    for (int i = 0; i < 300; ++i)
        RunFrame(p, kGameFrame);
    // A minimap capture ahead of the main passes now and then: that frame's signatures are new (no
    // discard there), the usual frame stays armed.
    for (int round = 0; round < 5; ++round) {
        const FrameResult m = RunFrame(p, kGameMinimapFrame);
        CHECK_EQ(m.discards, 0);
        CHECK_EQ(m.misses, 0);
        for (int i = 0; i < 10; ++i)
            CHECK_EQ(RunFrame(p, kGameFrame).discards, 1);
    }
    CHECK_EQ(p.Mispredicts(), 0u);
}

UNIT_TEST(vulkan_depth_discard_appended_pass_is_one_miss_then_off) {
    DepthDiscardPredictor p;
    for (int i = 0; i < 300; ++i)
        RunFrame(p, kMenuFrame);
    // The prefix is unchanged and a pass is appended after it: the one case a signature cannot see.
    const std::vector<Event> appended = { { kMain, true, false }, { kCopy, false, false }, { kMain, true, true } };
    const FrameResult r = RunFrame(p, appended);
    CHECK_EQ(r.discards, 1);
    CHECK_EQ(r.misses, 1);
    CHECK(p.Off());
    // Off for the session: no second wrong frame, whatever the structure.
    for (int i = 0; i < 500; ++i) {
        CHECK_EQ(RunFrame(p, appended).discards, 0);
        CHECK_EQ(RunFrame(p, kMenuFrame).discards, 0);
    }
    CHECK_EQ(p.Mispredicts(), 1u);
}

UNIT_TEST(vulkan_depth_discard_non_terminal_sighting_resets_evidence) {
    DepthDiscardPredictor p;
    const std::vector<Event> twoPasses = { { kMain, true, false }, { kCopy, false, false }, { kMain, true, true } };
    // The one-pass prefix is seen non-terminal every other frame: it never gathers kStableFrames
    // sightings in a row, so it never discards (and nothing misses).
    for (int i = 0; i < 1000; ++i) {
        const FrameResult a = RunFrame(p, kMenuFrame);
        const FrameResult b = RunFrame(p, twoPasses);
        CHECK_EQ(a.discards, 0);
        CHECK_EQ(b.misses, 0);
    }
    CHECK(!p.Off());
}

UNIT_TEST(vulkan_depth_discard_structure_changed_disarms) {
    DepthDiscardPredictor p;
    for (int i = 0; i < 300; ++i)
        RunFrame(p, kGameFrame);
    CHECK_EQ(RunFrame(p, kGameFrame).discards, 1);
    p.StructureChanged();
    CHECK_EQ(RunFrame(p, kGameFrame).discards, 0);
}

UNIT_TEST(vulkan_depth_discard_not_allowed_never_discards) {
    DepthDiscardPredictor p;
    for (int i = 0; i < 300; ++i)
        RunFrame(p, kMenuFrame);
    p.BeginFrame();
    p.Note(kMain);
    bool missed = false;
    CHECK(!p.MainDepthPass(false, false, &missed));
    CHECK(!missed);
    p.EndFrame();
}
