// ============================================================================
// src/platform/common/dir_chain.cpp -- Platform::EnsureDirChain (NOT RETAIL)
// ============================================================================
// platform_file.h has the contract. One body for every platform: it stands on DirCreate and
// PathAttributes only, whose twins (file_win.cpp, file_posix.cpp, file_darwin.cpp) do the OS work
// and the separator conversion. Lifted from the settings twins' private CreateDirectoryChain
// (settings_win.cpp, settings_posix.cpp, settings_darwin.cpp), which now call it.
// ============================================================================

#include "platform_file.h"

#include <string>

namespace Platform {

namespace {
bool IsSeparator(PathChar c) {
    return c == '\\' || c == '/';
}
}  // namespace

bool EnsureDirChain(const PathChar* dir) {
    if (dir == nullptr || dir[0] == 0) return false;
    const std::u16string path(dir);
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && !IsSeparator(path[i])) continue;
        const std::u16string prefix = path.substr(0, i);
        // A drive ("C:") is not a directory to create; a UNC host or an existing directory fails
        // harmlessly.
        if (prefix.size() == 2 && prefix[1] == ':') continue;
        DirCreate(prefix.c_str());
    }
    const uint32_t attrs = PathAttributes(dir);
    return attrs != kInvalidPathAttributes && (attrs & kFileAttributeDirectory) != 0;
}

}  // namespace Platform
