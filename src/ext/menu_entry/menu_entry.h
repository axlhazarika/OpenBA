#pragma once
// ============================================================================
// src/ext/menu_entry/menu_entry.h -- ext.menu_entry (class A, live at each menu entry)
// ============================================================================
// NOT RETAIL (docs/extensions/handins/MENUENTRY.md, issue #13). Entering a menu loop from any
// other loop (Engine::Loader::Load, the boot's menu, every return from a game or a save) rescans
// the mods (Modification::RefreshList: every .pkm's header, icon, preview and path set, then the
// collision pass over every pair) and marks the mod list dirty, so the next loop switch remounts
// every package (Modification::SetupFileSystem). Retail does both every time. With the row on:
//   (a) the scan takes the record of a .pkm whose name, size and last write time are those of the
//       last scan from that scan, and the collision pass's answer when the same files were found
//       in the same order; the enabled flags and the order still come from settings.cfg through
//       retail's own lines;
//   (b) the remount is skipped when what it would mount equals what the last completed remount
//       mounted (fingerprint.h); the order stamp and `_dirty = 0` still run.
// The debug exe never skips: it runs the remount behind every skip it would make and compares
// (the shadow, MENUENTRY.md section 3). Never under the faithful profile: the seams stay null.

namespace Ext {
namespace MenuEntry {

// Boot only (registry_boot.cpp): installs the IO::Modification / IO::FileSystem seams (null under
// the faithful profile). The hooks read the row live.
void Sync();

}  // namespace MenuEntry
}  // namespace Ext
