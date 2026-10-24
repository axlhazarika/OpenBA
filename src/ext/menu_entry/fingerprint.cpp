// ============================================================================
// src/ext/menu_entry/fingerprint.cpp -- NOT RETAIL. ext.menu_entry's fingerprint (fingerprint.h)
// ============================================================================
#include "ext/menu_entry/fingerprint.h"

namespace Ext {
namespace MenuEntry {

bool operator==(const FileStamp& a, const FileStamp& b) {
    return a.name == b.name && a.size == b.size && a.writeTime == b.writeTime;
}

bool operator!=(const FileStamp& a, const FileStamp& b) {
    return !(a == b);
}

bool operator==(const MountedPack& a, const MountedPack& b) {
    return a.object == b.object && a.handle == b.handle && a.name == b.name && a.entries == b.entries &&
           a.priority == b.priority && a.crc == b.crc && a.overridable == b.overridable;
}

const char* FirstDifference(const Fingerprint& a, const Fingerprint& b) {
    if (a.base != b.base)
        return "base";
    if (a.user != b.user)
        return "user";
    if (!(a.mounted == b.mounted))
        return "mounted";
    if (a.existsInPackfileGate != b.existsInPackfileGate || a.platformExistsGate != b.platformExistsGate)
        return "gates";
    if (a.largePackages != b.largePackages || a.packageCache != b.packageCache)
        return "rows";
    return nullptr;
}

bool operator==(const Fingerprint& a, const Fingerprint& b) {
    return FirstDifference(a, b) == nullptr;
}

bool operator!=(const Fingerprint& a, const Fingerprint& b) {
    return !(a == b);
}

std::vector<std::string> UserPackOrder(const std::vector<ModView>& mods) {
    std::vector<std::string> out;
    for (size_t i = mods.size(); i > 0; --i) {
        if (mods[i - 1].enabled)
            out.push_back(mods[i - 1].file);
    }
    return out;
}

std::vector<FileStamp> Stamps(const std::vector<std::string>& paths, StatFn stat, void* ctx) {
    std::vector<FileStamp> out;
    out.reserve(paths.size());
    for (const std::string& p : paths)
        out.push_back(stat(ctx, p));
    return out;
}

}  // namespace MenuEntry
}  // namespace Ext
