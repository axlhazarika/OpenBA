// ============================================================================
// src/render/vulkan/shader_cache.cpp -- the SPIR-V cache over the derived-data cache
// ============================================================================
// NOT RETAIL. shader_convert.h (LoadOrConvert) has the contract; the key and the payload format are
// shader_convert.cpp's (CacheKey, EncodeEntry, DecodeEntry). The store is the exe's shared one
// (Ext::DerivedCache::OpenShared, <Documents>\Banished\Cache: the user's folder, never the game
// directory or the tree) or, in the unit test, a store over a temp folder. A payload that does not
// decode (another build's layout, a damaged entry the store's own hash did not catch) is a miss and
// is converted again; the store keeps only a key's last record, so the fresh entry replaces it.
// ============================================================================
#include "shader_convert.h"

#include "ext/derived_cache/derived_cache.h"

#include <utility>

namespace VideoVulkan {
namespace Shaders {

bool LoadOrConvert(Ext::DerivedCache::Store* store, const uint8_t* dxbc, size_t size, Stage stage,
                   std::vector<uint32_t>* spirv, ShaderReflection* refl, bool* hit, std::string* why,
                   const ConvertOptions& options) {
    if (hit) *hit = false;
    Ext::DerivedCache::Key key;
    if (store != nullptr) {
        key = CacheKey(dxbc, size, stage, options);
        std::vector<uint8_t> payload;
        if (store->Find(key, payload) && DecodeEntry(payload, spirv, refl) && refl->stage == stage) {
            if (hit) *hit = true;
            return true;
        }
    }
    if (!ConvertDxbc(dxbc, size, stage, spirv, refl, why, options)) return false;
    if (store != nullptr) store->Put(key, EncodeEntry(*spirv, *refl));
    return true;
}

}  // namespace Shaders
}  // namespace VideoVulkan
