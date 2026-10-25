// ============================================================================
// tests/unit/ext_fps_cap_test.cpp -- ext.fps_cap and the frame limiter's ceiling
// ============================================================================
// The registry row (class A, OFF by default, the phone's first-run lines, the faithful profile) and
// its discrete rate key, and src/app/frame_limiter_ceiling.h: which ceiling the menu and the game get
// from the harness flag, the settings cap and the menu's automatic ceiling. The pacing itself (a
// clock and a sleep) needs the running game.
// ============================================================================
#include "unit.h"
#include "app/frame_limiter_ceiling.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;

bool EntriesLookup(void* ctx, const char* key, std::string& value) {
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text) {
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

}  // namespace

UNIT_TEST(ext_fps_cap_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("fps_cap");
    CHECK(e != nullptr);
    if (e == nullptr) return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::A);
    CHECK(!e->defaultOn);
    CHECK(e->formatVersion == 0);
    CHECK(Ext::SettingsKey(*e) == "ext.fps_cap");
    CHECK(!ResolveText("").ext.fps_cap);                                   // a desktop: uncapped
    CHECK(ResolveText("ext.fps_cap=1\next.fps_cap_rate=60\n").ext.fps_cap);   // the phone's first run
    CHECK(!ResolveText("ext.fps_cap=1\nprofile=faithful\n").ext.fps_cap);
}

UNIT_TEST(ext_fps_cap_rate_key_is_discrete_with_60_default) {
    const Ext::ValueKey* k = Ext::FindValueKey("fps_cap_rate");
    CHECK(k != nullptr);
    if (k == nullptr) return;
    CHECK(std::string(k->owner) == "fps_cap");
    CHECK(k->defaultValue == 60);
    CHECK(Ext::Values().fps_cap_rate == 60);
    CHECK(ResolveText("").val.fps_cap_rate == 60);
    CHECK(ResolveText("ext.fps_cap_rate=30\n").val.fps_cap_rate == 30);
    CHECK(ResolveText("ext.fps_cap_rate=120\n").val.fps_cap_rate == 120);
    CHECK(Ext::ClampValue(*k, 90) == 90);
    CHECK(Ext::ClampValue(*k, 75) == 60);    // not one of the combo's rates: the default
    CHECK(Ext::ClampValue(*k, 500) == 60);
}

UNIT_TEST(frame_limiter_ceiling_game_is_uncapped_unless_asked) {
    using FrameLimiter::HarnessCap;
    using FrameLimiter::ResolveCeiling;
    // No flag, no settings cap: the menu gets its spin guard, the game nothing (retail's uncapped loop).
    CHECK(ResolveCeiling(HarnessCap::None, 0.0, 180.0, 0.0, true) == 180.0);
    CHECK(ResolveCeiling(HarnessCap::None, 0.0, 180.0, 0.0, false) == 0.0);
    // The settings cap paces both; in the menu the lower of it and the spin guard.
    CHECK(ResolveCeiling(HarnessCap::None, 0.0, 180.0, 60.0, true) == 60.0);
    CHECK(ResolveCeiling(HarnessCap::None, 0.0, 180.0, 60.0, false) == 60.0);
    CHECK(ResolveCeiling(HarnessCap::None, 0.0, 90.0, 120.0, true) == 90.0);
    CHECK(ResolveCeiling(HarnessCap::None, 0.0, 90.0, 120.0, false) == 120.0);
}

UNIT_TEST(frame_limiter_ceiling_harness_flag_wins) {
    using FrameLimiter::HarnessCap;
    using FrameLimiter::ResolveCeiling;
    // BANISHED_FPS_CAP=off (the stress and perf runs): nothing paced, whatever the settings say.
    CHECK(ResolveCeiling(HarnessCap::Off, 0.0, 180.0, 60.0, true) == 0.0);
    CHECK(ResolveCeiling(HarnessCap::Off, 0.0, 180.0, 60.0, false) == 0.0);
    // BANISHED_FPS_CAP=<n>: that ceiling in both loops, over the settings cap and the spin guard.
    CHECK(ResolveCeiling(HarnessCap::Fixed, 45.0, 180.0, 60.0, true) == 45.0);
    CHECK(ResolveCeiling(HarnessCap::Fixed, 45.0, 180.0, 60.0, false) == 45.0);
    CHECK(ResolveCeiling(HarnessCap::Fixed, 500.0, 180.0, 0.0, false) == 500.0);
}
