#pragma once
// ============================================================================
// src/engine/object/external_visit.h -- the resource-reference visit of ONE
//   ExternalPtr (block VISITPASS, NOT RETAIL in its shortcut, exact)
// ============================================================================
// `ExternalPtr<T>::Serialize` under a VisitorStream (Encoding kind 3) is
// retail's `FindOrLoad(cur->GetPath())` with a discarded `IsDerivedFrom`
// (external_ptr_serialize.cpp, mode 1). external_visit.cpp has that walk and,
// in front of it, the shortcut that reaches the same row of
// `ExternalFactory::_resources` from the object itself; its banner holds the
// exactness argument.
// ============================================================================

#include <cstdint>

namespace Object {
class External;
class BaseTypeInfo;
}

namespace RtObject {

// The visit of `cur` (non-null): what retail's mode-1 arm does, by the shortcut
// when it is known exact, by the path walk otherwise.
void VisitExternal(::Object::External* cur, const ::Object::BaseTypeInfo& typeInfo);

// The `[VisitPass]` exit line (installed with the `[ExtAdd]` one).
void DumpVisitCounters();

// NOT RETAIL, print-only (docs/extensions/handins/VISITREPLAY.md section 1): the
// ExternalPtr pointers per mode, printed in DumpVisitCounters' `[FindCensus]` line
// with the FindOrLoad / Find call counts.
struct PtrCensus {
    uint64_t visit;   // mode 1: a non-null pointer visited
    uint64_t read2;   // the load tail ran with reference mode on (save load)
    uint64_t read3;   // ... with reference mode off (a resource load)
};
extern PtrCensus g_ptrCensus;   // external_visit.cpp; external_ptr_serialize.cpp counts

}  // namespace RtObject
