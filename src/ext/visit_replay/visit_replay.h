#pragma once
// src/ext/visit_replay/visit_replay.h -- ext.visit_replay (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md, issue #13). Every loader bracket gives the
// loop a fresh usage bit, and ExternalFactory::Add's first touch of each row under that bit runs the
// reference visit (the object serialized into a VisitorStream, each ExternalPtr handed back to Add):
// under the seven-part MegaMod each of a boot's five later brackets re-walks ~375k rows for 0.23 to
// 0.6 s. With the row on, the rows one visit hands to Add's hit arm are recorded when it runs, and
// the next first touch of that row hands the same rows to the same arm from the record instead of
// serializing the object again. The bits set, the rows visited and their order are the visit's own.
// Never under the faithful profile: the seam stays null.

namespace Ext { namespace VisitReplay {

// Boot only (registry_boot.cpp): installs Object::ExternalFactory::_extVisitReplay (null under the
// faithful profile or with the row off) and the `[VisitReplay]` exit line.
void Sync();

}}  // namespace Ext::VisitReplay
