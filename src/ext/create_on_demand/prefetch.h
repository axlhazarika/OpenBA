#pragma once
// src/ext/create_on_demand/prefetch.h -- ext.create_on_demand's mesh prefetch (NOT RETAIL)
//
// Graphics::MeshInstance::_extMeshAssigned / _extMaterialAssigned, Graphics::ParticleInstance::_extParticleAssigned,
// UI::SpriteSheet::_extLoaded and WorldGeometry::_extMaterialTaken while the row is on (prefetch.cpp
// has the rules, each seam's header its contract).

namespace Graphics { class MaterialInstance; class MeshInstance; class Particle; }
namespace UI { class SpriteSheet; }

namespace Ext { namespace CreateOnDemand {

void PrefetchMesh(const Graphics::MeshInstance& instance);
void PrefetchParticle(const Graphics::Particle& particle);
void PrefetchMaterial(const Graphics::MaterialInstance* material);
void KeepSpriteSheet(const UI::SpriteSheet& sheet);

}}  // namespace Ext::CreateOnDemand
