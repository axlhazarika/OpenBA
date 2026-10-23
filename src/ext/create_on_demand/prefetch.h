#pragma once
// src/ext/create_on_demand/prefetch.h -- ext.create_on_demand's mesh prefetch (NOT RETAIL)
//
// Graphics::MeshInstance::_extMeshAssigned and UI::SpriteSheet::_extLoaded while the row is on
// (prefetch.cpp has the rules, Mesh.h and SpriteSheet.h the seams' contracts).

namespace Graphics { class MeshInstance; }
namespace UI { class SpriteSheet; }

namespace Ext { namespace CreateOnDemand {

void PrefetchMesh(const Graphics::MeshInstance& instance);
void KeepSpriteSheet(const UI::SpriteSheet& sheet);

}}  // namespace Ext::CreateOnDemand
