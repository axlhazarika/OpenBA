// ============================================================================
// src/platform/android/settings_android.cpp -- the settings seam (Android)
// ============================================================================
// The Android twin of src/platform/posix/settings_posix.cpp (src/platform/android/README.md):
// it replaces that file whole on Android. One body differs:
//
// DocumentsDir: <data>/Documents, the app's data folder (android_paths.h) plus Documents, so
// GetSaveDirectory's appended "Banished/Save/" lands at
// /sdcard/Android/data/<package>/files/Documents/Banished/Save (PLAN.md §1). An app process has
// a uid but no home directory in the password database, and no getenv is read here either.
//
// ReadWholeFile / WriteFileAtomic / ReadLegacySettings are the POSIX bodies (their banner in
// settings_posix.cpp holds).
// ============================================================================

#include "platform_settings.h"
#include "platform_file.h"   // EnsureDirChain: WriteFileAtomic's parent directories
#include "platform/posix/posix_path.h"      // Posix::Utf8ToUtf16
#include "android_paths.h"   // Platform::Android::DataDir

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <string>


#include "system/String.h"

namespace {

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
    const std::string& data = Android::DataDir();
    if (data.empty())
        return false;
    out = data + "/Documents";
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
