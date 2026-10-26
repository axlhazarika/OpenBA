// ============================================================================
// src/render/vulkan/spirv_legalize.cpp -- lowering of OpBitFieldInsert (spirv_legalize.h)
// ============================================================================
// NOT RETAIL. A word-level SPIR-V pass:
//   1. index the 32-bit integer scalar types and the 32-bit OpConstant values;
//   2. replace each OpBitFieldInsert on such a type with constant offset and count by
//      OpShiftLeftLogical / OpBitwiseAnd / OpBitwiseAnd / OpBitwiseOr (the last keeps the result id);
//   3. declare the two mask constants of each before the first function.
// ============================================================================
#include "spirv_legalize.h"

#include <cstddef>
#include <map>
#include <utility>

namespace VideoVulkan {
namespace Shaders {

namespace {

constexpr uint32_t kOpTypeInt = 21;
constexpr uint32_t kOpConstant = 43;
constexpr uint32_t kOpFunction = 54;
constexpr uint32_t kOpShiftLeftLogical = 196;
constexpr uint32_t kOpBitwiseOr = 197;
constexpr uint32_t kOpBitwiseAnd = 199;
constexpr uint32_t kOpBitFieldInsert = 201;

uint32_t Head(uint32_t op, uint32_t words) {
    return (words << 16) | op;
}

void SetWhy(std::string* why, const std::string& text) {
    if (why) *why = text;
}

}  // namespace

bool LowerBitFieldInsert(std::vector<uint32_t>* spirv, uint32_t* changed, std::string* why) {
    *changed = 0;
    const std::vector<uint32_t>& m = *spirv;
    if (m.size() < 5) {
        SetWhy(why, "legalize: module too short");
        return false;
    }
    uint32_t bound = m[3];

    // ---- 1. index ----
    std::map<uint32_t, uint32_t> intWidth;    // OpTypeInt id -> width
    std::map<uint32_t, uint32_t> constants;   // OpConstant id (one value word) -> value
    size_t firstFunction = m.size();
    bool any = false;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        if (words == 0 || at + words > m.size()) {
            SetWhy(why, "legalize: malformed instruction stream");
            return false;
        }
        if (op == kOpTypeInt && words == 4) intWidth[m[at + 1]] = m[at + 2];
        if (op == kOpConstant && words == 4) constants[m[at + 2]] = m[at + 3];
        if (op == kOpFunction && firstFunction == m.size()) firstFunction = at;
        if (op == kOpBitFieldInsert) any = true;
        at += words;
    }
    if (!any || firstFunction == m.size()) return true;

    // ---- 2. rewrite ----
    std::vector<uint32_t> out(m.begin(), m.begin() + 5);
    out.reserve(m.size() + 64);
    std::vector<uint32_t> newConstants;
    size_t constantsAt = 0;
    for (size_t at = 5; at < m.size();) {
        const uint32_t words = m[at] >> 16;
        const uint32_t op = m[at] & 0xFFFFu;
        const uint32_t* w = &m[at];
        if (at == firstFunction) constantsAt = out.size();
        // OpBitFieldInsert: result type, result, base, insert, offset, count.
        const bool lower = op == kOpBitFieldInsert && words == 7 && intWidth.count(w[1]) != 0 &&
                           intWidth[w[1]] == 32 && constants.count(w[5]) != 0 && constants.count(w[6]) != 0 &&
                           constants[w[5]] < 32 && constants[w[6]] <= 32 - constants[w[5]];
        if (lower) {
            const uint32_t offset = constants[w[5]];
            const uint32_t count = constants[w[6]];
            const uint32_t mask = (count == 32 ? 0xFFFFFFFFu : ((1u << count) - 1u)) << offset;
            const uint32_t type = w[1];
            const uint32_t maskId = bound++;
            const uint32_t notMaskId = bound++;
            newConstants.insert(newConstants.end(), { Head(kOpConstant, 4), type, maskId, mask });
            newConstants.insert(newConstants.end(), { Head(kOpConstant, 4), type, notMaskId, ~mask });
            const uint32_t shifted = bound++;
            const uint32_t inserted = bound++;
            const uint32_t kept = bound++;
            out.insert(out.end(), { Head(kOpShiftLeftLogical, 5), type, shifted, w[4], w[5] });
            out.insert(out.end(), { Head(kOpBitwiseAnd, 5), type, inserted, shifted, maskId });
            out.insert(out.end(), { Head(kOpBitwiseAnd, 5), type, kept, w[3], notMaskId });
            out.insert(out.end(), { Head(kOpBitwiseOr, 5), type, w[2], inserted, kept });
            ++*changed;
        } else {
            out.insert(out.end(), w, w + words);
        }
        at += words;
    }
    if (*changed == 0) return true;
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(constantsAt), newConstants.begin(), newConstants.end());
    out[3] = bound;
    *spirv = std::move(out);
    return true;
}

}  // namespace Shaders
}  // namespace VideoVulkan
