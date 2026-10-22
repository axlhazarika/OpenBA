#pragma once
// TEMP INSTRUMENTS — never merge. Per-kind D3D11 Create* counts, sizes and times.
#include <cstdint>
namespace VideoDX11 {
namespace TempCreateStats {
enum Kind { kVbStatic, kVbDynamic, kVbDefault, kIbStatic, kIbDynamic, kIbDefault, kCb,
            kTex, kTexDefault, kSrv, kRtvDsv, kSampler, kKindCount };
uint64_t Now();
void Add(Kind k, uint64_t bytes, uint64_t t0);
void MaybeDump(bool force);
}  // namespace TempCreateStats
}  // namespace VideoDX11
