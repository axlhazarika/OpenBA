// ============================================================================
// src/platform/posix/settings_posix.cpp -- the settings seam, POSIX (Phase D.4)
// ============================================================================
// The POSIX half of src/platform/common/platform_settings.h.
//
// DocumentsDir: the per-user data directory, `<home>/.local/share` (the XDG data
// home's default), with <home> from the password database (getpwuid_r on the real
// uid), NEVER from an environment variable (CLAUDE.md, D4_RULES C11). GetSaveDirectory
// then appends "\Banished\Save\" as on Windows.
//
// ReadWholeFile / WriteFileAtomic: the same contract as the Windows bodies, written
// directly over POSIX calls (the paths are already UTF-8 here). ALLOCATOR-FREE:
// std::string and the C library only, because src/app/main.cpp's harness guard runs
// them before the engine allocator exists (platform_settings.h).
//
// ReadLegacySettings: there is no legacy store on POSIX; always false.
// ============================================================================

#include "platform_settings.h"
#include "platform_file.h"   // EnsureDirChain: WriteFileAtomic's parent directories
#include "posix_path.h"      // Posix::Utf8ToUtf16

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <vector>

#include "system/String.h"

namespace {

bool HomeDir(std::string& out) {
    struct passwd pw;
    struct passwd* result = nullptr;
    std::vector<char> buf(16384);
    if (getpwuid_r(getuid(), &pw, buf.data(), buf.size(), &result) != 0 || result == nullptr)
        return false;
    if (pw.pw_dir == nullptr || pw.pw_dir[0] == 0)
        return false;
    out = pw.pw_dir;
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return true;
}

std::string NativeSlashes(const char* s) {
    std::string out = s;
    for (char& c : out) if (c == '\\') c = '/';
    return out;
}

bool WriteAll(int fd, const char* data, size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, data, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (w == 0) return false;
        data += w;
        n -= static_cast<size_t>(w);
    }
    return true;
}

}  // namespace

namespace Platform {

bool DocumentsDir(std::string& out) {
    std::string home;
    if (!HomeDir(home))
        return false;
    out = home + "/.local/share";
    return true;
}

bool DocumentsDir(System::String& out) {
    std::string dir;
    if (!DocumentsDir(dir))
        return false;
    out = System::String(dir.c_str());
    return true;
}

bool ReadWholeFile(const char* pathUtf8, std::string& out) {
    if (pathUtf8 == nullptr || pathUtf8[0] == 0)
        return false;
    const std::string path = NativeSlashes(pathUtf8);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    std::string data;
    char chunk[16384];
    bool ok = true;
    for (;;) {
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (n == 0) break;
        data.append(chunk, static_cast<size_t>(n));
    }
    ::close(fd);
    if (!ok)
        return false;
    out.swap(data);
    return true;
}

bool WriteFileAtomic(const char* pathUtf8, const char* data, size_t n) {
    if (pathUtf8 == nullptr || pathUtf8[0] == 0)
        return false;
    const std::string path = NativeSlashes(pathUtf8);
    const std::string tmp = path + ".tmp";

    const size_t cut = path.find_last_of('/');
    if (cut != std::string::npos && cut > 0)
        Platform::EnsureDirChain(Posix::Utf8ToUtf16(path.c_str(), cut).c_str());   // platform_file.h

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0)
        return false;
    bool ok = WriteAll(fd, data, n);
    if (ok && ::fsync(fd) != 0)
        ok = false;
    if (::close(fd) != 0)
        ok = false;
    if (ok && ::rename(tmp.c_str(), path.c_str()) != 0)
        ok = false;
    if (!ok)
        ::unlink(tmp.c_str());
    return ok;
}

bool ReadLegacySettings(void (*)(void* ctx, const char* key, const char* value), void*) {
    return false;
}

}  // namespace Platform
