// ============================================================================
// tests/unit/ext_menu_entry_test.cpp -- ext.menu_entry's remount fingerprint
// ============================================================================
// src/ext/menu_entry/fingerprint.{h,cpp} on a synthetic install: act 2's user pack order
// (enabled mods, last to first), and the fingerprint equal / not equal for every change that
// must make the remount run (reorder, enable toggle, size, write time, an added enabled .pkm,
// an added .pkg, a gate, a row) and for the one that need not (an added .pkm left disabled:
// act 2 never mounts it). Each fingerprint is taken the way the extension takes it: the stamps
// of the files a remount would mount, through a stat callback over a fake directory.
// ============================================================================
#include "unit.h"
#include "ext/menu_entry/fingerprint.h"

#include <map>
#include <string>
#include <vector>

namespace {

using namespace Ext::MenuEntry;

struct Disk {
    std::map<std::string, FileStamp> files;

    void Put(const std::string& name, int64_t size, uint64_t t) {
        FileStamp s;
        s.name = name;
        s.size = size;
        s.writeTime = t;
        files[name] = s;
    }
};

FileStamp StatDisk(void* ctx, const std::string& path) {
    const Disk* d = static_cast<const Disk*>(ctx);
    auto it = d->files.find(path);
    if (it != d->files.end())
        return it->second;
    FileStamp s;
    s.name = path;
    return s;
}

struct Install {
    Disk disk;
    std::vector<std::string> base;
    std::vector<ModView> mods;
    std::vector<MountedPack> mounted;
    uint8_t gate = 1;
    bool largePackages = true;
    bool packageCache = true;
};

Fingerprint Take(Install& in) {
    Fingerprint f;
    f.base = Stamps(in.base, &StatDisk, &in.disk);
    f.user = Stamps(UserPackOrder(in.mods), &StatDisk, &in.disk);
    f.mounted = in.mounted;
    f.existsInPackfileGate = in.gate;
    f.platformExistsGate = 1;
    f.largePackages = in.largePackages;
    f.packageCache = in.packageCache;
    return f;
}

Install Sample() {
    Install in;
    in.disk.Put("WinData/Resource.pkg", 1000, 11);
    in.disk.Put("WinData/Audio.pkg", 2000, 12);
    in.disk.Put("WinData/MegaMod1.pkm", 3000, 13);
    in.disk.Put("WinData/MegaMod2.pkm", 4000, 14);
    in.disk.Put("WinData/Other.pkm", 5000, 15);
    in.base = { "WinData/Audio.pkg", "WinData/Resource.pkg" };
    in.mods = { { "WinData/MegaMod1.pkm", true },
                { "WinData/MegaMod2.pkm", true },
                { "WinData/Other.pkm", false } };
    MountedPack p;
    p.object = 0x1000;
    p.name = "Resource";
    p.entries = 10;
    in.mounted.push_back(p);
    return in;
}

}  // namespace

UNIT_TEST(ext_menu_entry_user_order_is_enabled_last_to_first) {
    const std::vector<ModView> mods = { { "a", true }, { "b", false }, { "c", true }, { "d", true } };
    const std::vector<std::string> want = { "d", "c", "a" };
    CHECK(UserPackOrder(mods) == want);
    CHECK(UserPackOrder({}).empty());
}

UNIT_TEST(ext_menu_entry_same_install_same_fingerprint) {
    Install a = Sample();
    Install b = Sample();
    CHECK(Take(a) == Take(b));
    CHECK(FirstDifference(Take(a), Take(b)) == nullptr);
}

UNIT_TEST(ext_menu_entry_reorder_changes_it) {
    Install a = Sample();
    Install b = Sample();
    std::swap(b.mods[0], b.mods[1]);
    CHECK(Take(a) != Take(b));
    CHECK_STREQ(FirstDifference(Take(a), Take(b)), "user");
}

UNIT_TEST(ext_menu_entry_enable_toggle_changes_it) {
    Install a = Sample();
    Install b = Sample();
    b.mods[2].enabled = true;
    CHECK(Take(a) != Take(b));
    Install c = Sample();
    c.mods[0].enabled = false;
    CHECK(Take(a) != Take(c));
}

UNIT_TEST(ext_menu_entry_size_or_write_time_changes_it) {
    Install a = Sample();
    Install b = Sample();
    b.disk.Put("WinData/MegaMod2.pkm", 4001, 14);
    CHECK(Take(a) != Take(b));
    Install c = Sample();
    c.disk.Put("WinData/MegaMod2.pkm", 4000, 99);
    CHECK(Take(a) != Take(c));
    Install d = Sample();
    d.disk.Put("WinData/Resource.pkg", 1000, 98);   // a base package rebuilt in place
    CHECK_STREQ(FirstDifference(Take(a), Take(d)), "base");
    // A disabled mod's file is not mounted: its stamp is not in the fingerprint.
    Install e = Sample();
    e.disk.Put("WinData/Other.pkm", 1, 1);
    CHECK(Take(a) == Take(e));
}

UNIT_TEST(ext_menu_entry_added_files) {
    Install a = Sample();
    // An added .pkm the settings enable (ModEnabled_<stem>=true) is mounted: not equal.
    Install b = Sample();
    b.disk.Put("WinData/New.pkm", 10, 20);
    b.mods.push_back({ "WinData/New.pkm", true });
    CHECK(Take(a) != Take(b));
    // An added .pkm left disabled (a new file has no ModEnabled_ key) mounts nothing: equal, and
    // the skip is exact (act 2 appends enabled mods only).
    Install c = Sample();
    c.disk.Put("WinData/New.pkm", 10, 20);
    c.mods.push_back({ "WinData/New.pkm", false });
    CHECK(Take(a) == Take(c));
    // An added .pkg is mounted by LoadPackages' loop 1 whatever the mods say.
    Install d = Sample();
    d.disk.Put("WinData/Extra.pkg", 10, 20);
    d.base.push_back("WinData/Extra.pkg");
    CHECK_STREQ(FirstDifference(Take(a), Take(d)), "base");
    // A removed (unreadable) mod file.
    Install e = Sample();
    e.disk.files.erase("WinData/MegaMod1.pkm");
    CHECK(Take(a) != Take(e));
}

UNIT_TEST(ext_menu_entry_mounted_gates_and_rows_change_it) {
    Install a = Sample();
    Install b = Sample();
    b.mounted[0].object = 0x2000;   // the packages were mounted again elsewhere
    CHECK_STREQ(FirstDifference(Take(a), Take(b)), "mounted");
    Install c = Sample();
    c.gate = 0;
    CHECK_STREQ(FirstDifference(Take(a), Take(c)), "gates");
    Install d = Sample();
    d.largePackages = false;
    CHECK_STREQ(FirstDifference(Take(a), Take(d)), "rows");
    Install e = Sample();
    e.packageCache = false;
    CHECK_STREQ(FirstDifference(Take(a), Take(e)), "rows");
}
