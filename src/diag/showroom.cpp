// src/diag/showroom.cpp
// BANISHED_SHOWROOM — the building showroom (see showroom.h). Harness only, not retail.
#include "showroom.h"
#include "options.h"
#include "io/FileSystem.h"
#include "system/Filename.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Frames (GameUpdateLoop::Update calls) the world gets before the first shot: the fade-in
// and any texture created on first bind have landed by then.
constexpr int kSettleFrames = 300;
// Frames between pinning the camera on a building and requesting its shot.
constexpr int kFrameFrames = 60;
// Frames to wait for the retail grab to write its .bmp before giving up on a shot.
constexpr int kShotTimeoutFrames = 300;
// Frames between the .bmp appearing and moving it (lets the writer finish).
constexpr int kMoveDelayFrames = 5;
// Fixed heading for every shot, degrees.
constexpr float kYaw = 45.0f;
constexpr const char* kOutDir = "showroom";

struct Entry {
    std::string path;   // "Template\X.rsc"
    std::string name;   // "X"
    bool placed = false;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float dist = 40.0f;
};

enum class Phase { Settle, Frame, AwaitFile, Done };

struct State {
    bool loaded = false;
    std::vector<Entry> entries;
    Phase phase = Phase::Settle;
    int frame = 0;          // Tick calls so far
    int phaseStart = 0;     // frame the current phase began
    int current = -1;       // entry being framed / shot
    int shots = 0;
    int fileSeenAt = -1;
    std::string pendingShot;   // the ScreenshotN.bmp the retail grab is about to write
};

State& S() {
    static State s;
    return s;
}

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void Load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    const char* file = Opt().showroom;
    if (!file || !file[0]) return;
    FILE* f = std::fopen(file, "r");
    if (!f) {
        std::fprintf(stderr, "[Showroom] cannot open list %s\n", file);
        return;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string text = line;
        const size_t hash = text.find('#');
        if (hash != std::string::npos) text.resize(hash);
        // A tab-separated list (the ranking TSV) is accepted: its first column is the path.
        const size_t tab = text.find('\t');
        if (tab != std::string::npos) text.resize(tab);
        text = Trim(text);
        if (text.empty()) continue;
        for (char& c : text) {
            if (c == '/') c = '\\';
        }
        Entry e;
        e.path = text;
        const size_t slash = text.rfind('\\');
        std::string stem = slash == std::string::npos ? text : text.substr(slash + 1);
        const size_t dot = stem.rfind('.');
        if (dot != std::string::npos) stem.resize(dot);
        e.name = stem;
        s.entries.push_back(e);
    }
    std::fclose(f);
    std::fprintf(stderr, "[Showroom] list %s: %zu templates\n", file, s.entries.size());
}

// The file the retail grab will write next: <save dir>Screenshots\Screenshot<N>.bmp for
// the first N that does not exist (GameUpdateLoop::TakeScreenshot's own probe).
std::string NextScreenshotPath() {
    const IO::FileSystem* fs = IO::FileSystem::_singleton;
    if (!fs) return std::string();
    System::Filename saveDir(fs->GetSaveDirectory());
    const char* dir = saveDir.AsString()._data;
    if (!dir) return std::string();
    for (int n = 0; n < 100000; ++n) {
        char path[1024];
        std::snprintf(path, sizeof(path), "%sScreenshots\\Screenshot%d.bmp", dir, n);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return path;
    }
    return std::string();
}

void MoveShot(const std::string& from, int index) {
    const Entry& e = S().entries[index];
    std::error_code ec;
    std::filesystem::create_directories(kOutDir, ec);
    char to[512];
    std::snprintf(to, sizeof(to), "%s\\%02d_%s.bmp", kOutDir, index, e.name.c_str());
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::remove(from, ec);
    std::fprintf(stderr, "[Showroom] shot %d %s -> %s%s\n", index, e.name.c_str(), to,
                 ec ? " (MOVE FAILED)" : "");
}

int NextPlaced(int after) {
    const State& s = S();
    for (int i = after + 1; i < (int)s.entries.size(); ++i) {
        if (s.entries[i].placed) return i;
    }
    return -1;
}

void Enter(Phase phase) {
    State& s = S();
    s.phase = phase;
    s.phaseStart = s.frame;
}

void Finish() {
    State& s = S();
    int placed = 0;
    for (const Entry& e : s.entries) {
        if (e.placed) ++placed;
    }
    std::fprintf(stderr, "[Showroom] DONE placed=%d of %zu, shots=%d\n", placed, s.entries.size(), s.shots);
    std::fflush(stderr);
    Enter(Phase::Done);
}

}  // namespace

extern "C" bool Showroom_Enabled() {
    Load();
    return !S().entries.empty();
}

extern "C" int Showroom_Count() {
    Load();
    return (int)S().entries.size();
}

extern "C" const char* Showroom_Path(int index) {
    return S().entries[index].path.c_str();
}

extern "C" const char* Showroom_Name(int index) {
    return S().entries[index].name.c_str();
}

extern "C" void Showroom_NotePlaced(int index, bool placed, int tileX, int tileY, int w, int h, int mapDim,
                                    const char* why) {
    Entry& e = S().entries[index];
    e.placed = placed;
    if (!placed) {
        std::fprintf(stderr, "[Showroom] NOT placed %d %s: %s\n", index, e.path.c_str(), why ? why : "?");
        return;
    }
    // Footprint centre in world units: world = cell - mapDim/2 (skill camera-pin).
    const float half = (float)(mapDim >> 1);
    e.worldX = (float)tileX + (float)w * 0.5f - half;
    e.worldY = (float)tileY + (float)h * 0.5f - half;
    const int size = w > h ? w : h;
    e.dist = 14.0f + 2.5f * (float)size;
    if (e.dist < 28.0f) e.dist = 28.0f;
    if (e.dist > 90.0f) e.dist = 90.0f;
    std::fprintf(stderr, "[Showroom] placed %d %s tile=(%d,%d) fp=%dx%d world=(%.1f,%.1f) dist=%.0f\n", index,
                 e.path.c_str(), tileX, tileY, w, h, e.worldX, e.worldY, e.dist);
}

extern "C" bool Showroom_WantsPause() {
    return Showroom_Enabled() && S().phase != Phase::Done;
}

extern "C" bool Showroom_Tick() {
    if (!Showroom_Enabled()) return false;
    State& s = S();
    ++s.frame;
    const int inPhase = s.frame - s.phaseStart;
    switch (s.phase) {
    case Phase::Settle:
        if (inPhase < kSettleFrames) return false;
        s.current = NextPlaced(-1);
        if (s.current < 0) {
            Finish();
            return false;
        }
        Enter(Phase::Frame);
        return false;
    case Phase::Frame:
        if (inPhase < kFrameFrames) return false;
        s.pendingShot = NextScreenshotPath();
        s.fileSeenAt = -1;
        Enter(Phase::AwaitFile);
        return true;
    case Phase::AwaitFile: {
        std::error_code ec;
        if (s.fileSeenAt < 0 && std::filesystem::exists(s.pendingShot, ec)) s.fileSeenAt = s.frame;
        if (s.fileSeenAt >= 0 && s.frame - s.fileSeenAt >= kMoveDelayFrames) {
            MoveShot(s.pendingShot, s.current);
            ++s.shots;
        } else if (inPhase < kShotTimeoutFrames) {
            return false;
        } else {
            std::fprintf(stderr, "[Showroom] shot %d %s: no %s after %d frames\n", s.current,
                         s.entries[s.current].name.c_str(), s.pendingShot.c_str(), kShotTimeoutFrames);
        }
        s.current = NextPlaced(s.current);
        if (s.current < 0) {
            Finish();
            return false;
        }
        Enter(Phase::Frame);
        return false;
    }
    case Phase::Done:
        return false;
    }
    return false;
}

extern "C" bool Showroom_CameraFocus(float* x, float* y, float* dist, float* yaw) {
    if (!Showroom_Enabled()) return false;
    const State& s = S();
    if (s.current < 0 || s.phase == Phase::Done) return false;
    const Entry& e = s.entries[s.current];
    *x = e.worldX;
    *y = e.worldY;
    *dist = e.dist;
    *yaw = kYaw;
    return true;
}
