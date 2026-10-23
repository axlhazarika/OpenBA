#pragma once
// src/ext/create_on_demand/prefetch.h -- ext.create_on_demand's mesh prefetch (NOT RETAIL)
//
// Graphics::MeshInstance::_extMeshAssigned while the row is on (prefetch.cpp has the rules, Mesh.h
// the seam's contract).

namespace Graphics { class MeshInstance; }

namespace Ext { namespace CreateOnDemand {

void PrefetchMesh(const Graphics::MeshInstance& instance);

}}  // namespace Ext::CreateOnDemand
