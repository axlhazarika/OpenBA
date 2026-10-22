// src/ext/item_priority/item_priority_town.cpp -- NOT RETAIL. ext.item_priority (class C): the game side. The
// factory walk that finds the materials, the path an item is listed by, and the once-per-frame BeginFrame.
// See item_priority.h and docs/extensions/handins/ITEMPRIO.md.
#include "ext/item_priority/item_priority.h"

#include "Component/RawMaterialDescription.h"
#include "ext/registry.h"   // Ext::Ext().item_priority (the game's locked value), Ext::LockEpoch
#include "object/External.h"
#include "object/TypeInfo.h"
#include "system/Filename.h"

namespace Ext {
namespace ItemPriority {

std::string TemplatePath(const void* external) {
    if (external == nullptr) {
        return std::string();
    }
    const System::Filename path = static_cast<const ::Object::External*>(external)->GetPath();
    return path._data != nullptr ? std::string(path._data) : std::string();
}

namespace {

// Every loaded RawMaterialDescription with its path. The factory walk is the one the game uses to enumerate
// what a package loaded (ExternalFactory::FindNext).
std::vector<Candidate> AllMaterials() {
    std::vector<Candidate> out;
    ::Object::ExternalFactory* factory = ::Object::ExternalFactory::_singleton;
    if (factory == nullptr) {
        return out;
    }
    const uint32_t tid = RawMaterialDescription::TypeId();
    const ::Object::External* prev = nullptr;
    int guard = 0;
    while ((prev = factory->FindNext(tid, prev)) != nullptr && guard++ < 20000) {
        if (!::Object::IsDerivedFromNullSafe(prev, tid)) {
            continue;
        }
        Candidate c;
        c.material = static_cast<const RawMaterialDescription*>(prev);   // the pointer the seams compare
        c.path = TemplatePath(prev);
        if (!c.path.empty()) {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

void BeginFrame() {
    const uint32_t epoch = LockEpoch();
    if (NeedsResolve(epoch)) {
        Resolve(epoch, Current().Empty() ? std::vector<Candidate>() : AllMaterials());
    }
    SetRowActive(Ext::Ext().item_priority);   // class C: the game's locked value
}

}  // namespace ItemPriority
}  // namespace Ext
