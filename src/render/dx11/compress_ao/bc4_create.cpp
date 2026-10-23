// ============================================================================
// src/render/dx11/compress_ao/bc4_create.cpp -- ext.compress_ao, the DX11 side (NOT RETAIL)
// ============================================================================
// bc4_create.h has the contract; docs/extensions/handins/COMPRESSAO.md.
// ============================================================================
#include "compress_ao/bc4_create.h"

#include "compress_ao/bc4_chain.h"
#include "compress_ao/compress_ao_backend.h"

#include <atomic>
#include <cstdio>

namespace VideoDX11 {
namespace CompressAo {

namespace {
std::atomic<bool> g_requested{false};
}  // namespace

bool Requested() {
    return g_requested.load(std::memory_order_relaxed);
}

}  // namespace CompressAo
}  // namespace VideoDX11

// ---- the backend-neutral interface (compress_ao_backend.h) ----------------------------------------
namespace CompressAoBackend {

// BC4_UNORM is required of every D3D11 device (feature level 10_0 and up), so DX11 always supports it.
void Request(bool on, Ext::DerivedCache::Store* cache) {
    ::CompressAo::SetCache(on ? cache : nullptr);
    VideoDX11::CompressAo::g_requested.store(on, std::memory_order_relaxed);
    std::fprintf(stderr, "[CompressAo] %s (derived cache %s)\n", on ? "on: R8 AO maps load as BC4" : "off",
                 on && cache != nullptr ? "on" : "off");
}

bool Supported() {
    return true;
}

}  // namespace CompressAoBackend
