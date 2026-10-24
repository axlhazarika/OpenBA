#pragma once
// ============================================================================
// src/ext/menu_entry/fingerprint.h -- ext.menu_entry: what a package remount mounts
// ============================================================================
// NOT RETAIL (docs/extensions/handins/MENUENTRY.md). Standard C++ only (the unit tests link it).
//
// Modification::SetupFileSystem's acts 1-4 close every package, mount the stack again and reload
// the resources whose package id moved. What act 2 and LoadPackages mount is a function of:
//   * the *.pkg files of the pack directory, in FindFiles order (LoadPackages' loop 1; the pack
//     index is the position, a byte of every packed FileInfo);
//   * the user pack list act 2 builds: the ENABLED mods, last to first (UserPackOrder);
//   * the bytes of each of those files (stood for by name, size and last write time: a mounted
//     package is held open for the whole run, and a change on disk is seen only by a remount);
//   * the two FileSystem gates (+0x18 LoadPackages' early return and ExistsInPackfile, +0x19 the
//     loose-file probe of act 3);
//   * the two rows that change how a mount is built (fix.large_packages, ext.package_cache).
// With every input equal the package directory comes out the same tree with the same values,
// so every resource's package id is the one it has now: act 3's queue is empty and act 4 does
// nothing. `mounted` is the live _packfiles list itself (object, handle, name, entries, priority,
// CRC, override byte), taken at the end of the remount and compared with the live list at the
// next one: the packages still mounted are the ones that remount left.
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace MenuEntry {

// One file as the fingerprint sees it. `size` -1: the file could not be opened.
struct FileStamp {
    std::string name;
    int64_t     size = -1;
    uint64_t    writeTime = 0;   // FILETIME units on Windows; whatever the platform's stamp is elsewhere
};
bool operator==(const FileStamp& a, const FileStamp& b);
bool operator!=(const FileStamp& a, const FileStamp& b);

// One live package of FileSystem::_packfiles.
struct MountedPack {
    uint64_t    object = 0;     // the PackageFile's address
    uint64_t    handle = 0;     // its FileInfo handle word and OS handle, mixed
    std::string name;
    int32_t     entries = 0;
    int32_t     priority = 0;
    uint32_t    crc = 0;
    bool        overridable = false;
};
bool operator==(const MountedPack& a, const MountedPack& b);

struct Fingerprint {
    std::vector<FileStamp>   base;      // the pack directory's *.pkg, FindFiles order
    std::vector<FileStamp>   user;      // the user pack list, act 2's order
    std::vector<MountedPack> mounted;   // FileSystem::_packfiles
    uint8_t existsInPackfileGate = 0;
    uint8_t platformExistsGate = 0;
    bool    largePackages = false;
    bool    packageCache = false;
};
bool operator==(const Fingerprint& a, const Fingerprint& b);
bool operator!=(const Fingerprint& a, const Fingerprint& b);

// One mod as act 2 reads it.
struct ModView {
    std::string file;
    bool        enabled = false;
};

// Act 2's user pack list (0x1800aed49): the enabled mods' files, from the last mod to the first.
std::vector<std::string> UserPackOrder(const std::vector<ModView>& mods);

// The stamps of `paths`, in order, through `stat`.
using StatFn = FileStamp (*)(void* ctx, const std::string& path);
std::vector<FileStamp> Stamps(const std::vector<std::string>& paths, StatFn stat, void* ctx);

// The first field two fingerprints differ in ("base", "user", "mounted", "gates", "rows"), or
// null when they are equal. For the log line.
const char* FirstDifference(const Fingerprint& a, const Fingerprint& b);

// ---- the mod scan (RefreshList) -------------------------------------------------------------
// Loop 2's answer for a scan: for each mod, the indices of the mods it collides with, in the
// order the loop appends them. It depends on the found mods in their order and on each mod's
// path set; the path set is a function of the file, so the key is the found mods' stamps.
struct OverrideAnswer {
    std::vector<FileStamp>            key;
    std::vector<std::vector<int32_t>> collides;
};

}  // namespace MenuEntry
}  // namespace Ext
