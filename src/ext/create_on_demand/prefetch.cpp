// ============================================================================
// src/ext/create_on_demand/prefetch.cpp -- NOT RETAIL. ext.create_on_demand's mesh prefetch
// ============================================================================
// Class A (docs/extensions/handins/CREATEONDEMAND.md). Installed as Graphics::MeshInstance::
// _extMeshAssigned (Mesh.h has the seam's contract) while the row is on.
//
// A deferred object is made at its first use, on the thread that uses it (the render thread's
// bind): a building never seen before would make its buffers and textures inside the frame that
// first draws it. The world gives notice earlier: a mesh instance gets its mesh (SetMesh) when an
// entity is placed, loaded from a save, or changes its model, well before it is drawn. This queues
// the first-use creates of everything that instance draws with -- every detail level's four
// buffers, every material's textures -- for the backend's prefetch workers. Queueing an object that
// has no deferred create (already made, never deferred) is a lookup that finds nothing.
// ============================================================================

#include "ext/create_on_demand/prefetch.h"

#include "create_on_demand/create_on_demand_backend.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "video/Buffers.h"
#include "video/ImageBuffer.h"

namespace Ext { namespace CreateOnDemand {

void PrefetchMesh(const Graphics::MeshInstance& instance) {
    const Graphics::GraphicsMesh* const mesh = instance._mesh.Get();
    if (mesh != nullptr) {
        const Graphics::GraphicsMesh::DetailLevel* const levels = mesh->_detailLevels.GetData();
        for (int32_t i = 0; i < mesh->_detailLevels.GetSize(); ++i) {
            const Graphics::GraphicsMesh::DetailLevel& level = levels[i];
            CreateOnDemandBackend::Prefetch(level._vertexBuffer.platformData);
            CreateOnDemandBackend::Prefetch(level._indexBuffer.platformData);
            CreateOnDemandBackend::Prefetch(level._skinVertexBuffer.platformData);
            CreateOnDemandBackend::Prefetch(level._skinIndexBuffer.platformData);
        }
    }
    const Object::ExternalPtr<Graphics::MaterialInstance>* const materials = instance._materials.GetData();
    for (int32_t m = 0; m < instance._materials.GetSize(); ++m) {
        const Graphics::MaterialInstance* const material = materials[m].Get();
        if (material == nullptr) continue;
        const RtGraphics::MaterialTexture* const textures = material->_textures.GetData();
        for (int32_t t = 0; t < material->_textures.GetSize(); ++t) {
            const Video::ImageBuffer* const image = textures[t].image;
            if (image != nullptr) CreateOnDemandBackend::Prefetch(image->_data);
        }
    }
}

}}  // namespace Ext::CreateOnDemand
