#!/usr/bin/env bash
# tools/linux_gamedir.sh -- assemble (or refresh) a Linux game directory (Phase D.4 LINUX-1).
#
# usage (inside WSL; strip the checkout's CRs as for linux_run.sh):
#   bash <(tr -d '\r' < tools/linux_gamedir.sh) <build-dir> <gamedir> <retail-dir>
#     <build-dir>   a CMake build directory holding OpenBA (the Vulkan backend, the Linux
#                   default) and SDL's libSDL3.so.0, e.g. build/cmake-debug
#     <gamedir>     the directory to fill, e.g. ~/banished_linux (created if absent)
#     <retail-dir>  the retail install, e.g. "/mnt/c/GOG Games/Banished" -- READ ONLY: only
#                   its WinData is LINKED into <gamedir>; nothing is written there.
#
# What the exe reads beside itself (IO::FileSystem::GetRootDirectory = the exe's directory,
# /proc/self/exe): WinData/*.pkg (and *.pkm mods), an optional cmd.txt. It writes its logs
# and boot state to the working directory (linux_run.sh runs it from <gamedir>), its settings
# and saves under ~/.local/share/Banished (Platform::DocumentsDir).
#
# The exe is COPIED, not linked: /proc/self/exe resolves a symlink to the build directory,
# which would make that the root. libSDL3.so.0 (the same submodule, built shared) goes beside
# it (RPATH $ORIGIN; linux_run.sh also sets LD_LIBRARY_PATH). Nothing else: the Vulkan loader
# (libvulkan.so.1) and its driver are the system's.
set -eu
B="${1:?build-dir}"; G="${2:?gamedir}"; R="${3:?retail-dir}"
mkdir -p "$G"
cp -f "$B/OpenBA" "$G/OpenBA"
SDLSO="$(find "$B" -name 'libSDL3.so.0' | head -1)"
[ -n "$SDLSO" ] || { echo "no libSDL3.so.0 under $B"; exit 1; }
cp -fL "$SDLSO" "$G/libSDL3.so.0"
[ -e "$G/WinData" ] || ln -s "$R/WinData" "$G/WinData"
echo "game dir $G: exe $(md5sum "$G/OpenBA" | cut -c1-8), SDL $(md5sum "$G/libSDL3.so.0" | cut -c1-8), WinData -> $(readlink "$G/WinData")"
