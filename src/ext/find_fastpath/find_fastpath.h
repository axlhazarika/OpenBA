#pragma once
// src/ext/find_fastpath/find_fastpath.h -- ext.find_fastpath (class A, next start)
//
// NOT RETAIL (docs/extensions/handins/VISITREPLAY.md, issue #13). ExternalFactory::FindOrLoad runs
// for every resource pointer a load reads (1.02M under the seven-part MegaMod boot, 0.18M reference
// ids on a vanilla save load): ExpandRelativePath, the private Find (two Filename copies, the
// identifier split, a case-folding descent) and Add (two more copies and a lookup), to land on a row
// the same request already named. With the row on, a request's characters (and, on a save load, a
// reference id) that already named a row go straight to FindOrLoad's hit arm on that row. Same rows,
// same bits, same visits. Never under the faithful profile: the seam stays null.

namespace Ext { namespace FindFastpath {

// Boot only (registry_boot.cpp): installs Object::ExternalFactory::_extFindFast (null under the
// faithful profile or with the row off) and the `[FindFastpath]` exit line.
void Sync();

}}  // namespace Ext::FindFastpath
