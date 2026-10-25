#pragma once
// ============================================================================
// src/platform/android/phone_seed.h -- the phone defaults' lines an existing settings.cfg lacks
// ============================================================================
// NOT RETAIL. Pure (tests/unit/phone_seed_test.cpp). app_android.cpp seeds a first run's
// settings.cfg with the phone defaults; a file written before a default existed (a P7 phone that
// never saw ext.fps_cap) gets the lines of the defaults whose KEY it does not have, appended. A key
// the file has keeps its value, whatever it is: the player's choice, or the game's own write-back.
// ============================================================================

#include <cstddef>
#include <string>

namespace Platform {
namespace Android {

// The "key=value\n" lines of `seed` whose key is not the key of any line of `existing` (a key is a
// line's text before its '='; '\r' is ignored). In `seed`'s order, each ending in '\n'.
inline std::string MissingSeedLines(const std::string& existing, const char* seed) {
    auto keyOf = [](const std::string& line) {
        const size_t eq = line.find('=');
        return eq == std::string::npos ? std::string() : line.substr(0, eq);
    };
    auto hasKey = [&](const std::string& key) {
        size_t start = 0;
        while (start < existing.size()) {
            size_t end = existing.find('\n', start);
            if (end == std::string::npos) end = existing.size();
            std::string line = existing.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (keyOf(line) == key) return true;
            start = end + 1;
        }
        return false;
    };
    std::string out;
    const std::string all(seed);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        const std::string line = all.substr(start, end - start);
        const std::string key = keyOf(line);
        if (!key.empty() && !hasKey(key)) {
            out += line;
            out += '\n';
        }
        start = end + 1;
    }
    return out;
}

}  // namespace Android
}  // namespace Platform
