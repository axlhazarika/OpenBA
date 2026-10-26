// src/render/vulkan/depth_discard.h -- THE DEPTH DISCARD's prediction (draw.cpp), with no Vulkan header
// (tests/unit/vulkan_depth_discard_test.cpp).
//
// NOT RETAIL (a tiler bandwidth saving, docs/android/PERF_P7.md §2 row 2, P8 re-arm:
// docs/android/P8_VULKAN.md). Nothing reads the main depth after the frame's last pass on it, so that
// pass may store DONT_CARE. The store op is fixed when the pass begins, before it is known to be the
// last, so the last one is PREDICTED.
//
// P7 predicted it by index (the pass whose index equals the last frame's count, less one) and turned
// the discard off for the session at the first miss; a session whose frame structure changed after
// the prediction armed (the menu's one pass, then the game's three) lost it at the game's first frame.
// P8 predicts it by SIGNATURE: every structural event of the frame (a pass begun, with its attachments;
// a copy; a read-back) is folded into a running hash, and a main-depth pass is predicted last only when
// its signature (the frame so far, this pass included) has ended the frame kStableFrames times, and has
// never been followed by another main-depth pass since. A structure change before the pass (the game's
// shadow and reflection passes ahead of the main one) changes the signature: the new structure starts
// unarmed and arms on its own evidence, with no wrong frame. A signature that is seen non-terminal (a
// main-depth pass came after it) has its evidence reset. What the signature cannot see is a pass
// appended AFTER a pass whose prefix is unchanged; that is the one miss left, and it is caught where
// it matters: a main-depth pass that loads discarded contents clears instead (one frame drawn against
// an empty depth) and the discard is off for the rest of the session, so no second wrong frame.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace VideoVulkan {

class DepthDiscardPredictor {
public:
    static constexpr uint32_t kStableFrames = 120;     // terminal sightings before a signature arms
    static constexpr size_t kMaxSignatures = 64;       // the table is cleared past this (a churning frame)

    // A new frame: the signature restarts.
    void BeginFrame() {
        sig_ = kSeed;
        frameSigs_.clear();
    }

    // A structural event of the frame (a pass begun on its attachments, a copy, a read-back).
    void Note(uint64_t token) {
        sig_ ^= token + 0x9E3779B97F4A7C15ull + (sig_ << 6) + (sig_ >> 2);
        sig_ *= 1099511628211ull;
    }

    // A main-depth pass begins (after its Note). `loads`: the pass loads depth or stencil. `allowed`:
    // the caller permits a discard now (no readable depth requested). Returns true when this pass is to
    // store DONT_CARE; `*missed` is true when the pass loads contents a discard threw away (the caller
    // clears instead).
    bool MainDepthPass(bool loads, bool allowed, bool* missed) {
        *missed = false;
        if (lost_ && loads) {
            *missed = true;
            off_ = true;
            ++mispredicts_;
        }
        lost_ = false;
        frameSigs_.push_back(sig_);
        if (!allowed || off_) return false;
        const auto it = terminal_.find(sig_);
        if (it == terminal_.end() || it->second < kStableFrames) return false;
        lost_ = true;
        return true;
    }

    // The frame ended: its last main-depth signature gains a terminal sighting, the earlier ones lose
    // their evidence.
    void EndFrame() {
        if (frameSigs_.empty()) return;
        for (size_t i = 0; i + 1 < frameSigs_.size(); ++i)
            terminal_[frameSigs_[i]] = 0;
        uint32_t& n = terminal_[frameSigs_.back()];
        if (n < kStableFrames) ++n;
        if (terminal_.size() > kMaxSignatures) terminal_.clear();
    }

    // A known structure change (the main targets remade): every signature starts again.
    void StructureChanged() {
        terminal_.clear();
    }

    bool Off() const { return off_; }
    uint32_t Mispredicts() const { return mispredicts_; }

private:
    static constexpr uint64_t kSeed = 1469598103934665603ull;
    uint64_t sig_ = kSeed;
    std::vector<uint64_t> frameSigs_;                  // this frame's main-depth pass signatures, in order
    std::unordered_map<uint64_t, uint32_t> terminal_;  // signature -> frames it ended (reset when not last)
    bool lost_ = false;                                // the main depth's contents were discarded
    bool off_ = false;                                 // a prediction missed: off for the session
    uint32_t mispredicts_ = 0;
};

}  // namespace VideoVulkan
