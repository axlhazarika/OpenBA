OpenBA for Linux (x86-64)
=========================

OpenBA is a standalone replacement for Banished's executable. It ships no game data:
it reads the WinData folder of your own Banished install (GOG or Steam, the Windows
build of the game; Banished 1.0.7).

Install
-------
1. Unpack this archive into one folder, for example ~/Banished.
2. Give that folder a WinData folder with the game's packages: copy the retail
   install's WinData there, or link it:
       ln -s "/path/to/Banished/WinData" ~/Banished/WinData
   Mods (*.pkm) go in the same WinData folder, as on Windows.
3. Run ./OpenBA.sh from that folder.

What is in the folder
---------------------
OpenBA               the game (renders with Vulkan: needs a Vulkan 1.3 driver and the
                     system's Vulkan loader, libvulkan.so.1)
OpenBA.sh            the launcher: sets the library path
libSDL3.so.0         SDL3, built from the pinned submodule (zlib licence)
FrancophilSans*.ttf  the UI typeface, loaded by the exe when it is not installed
                     (FrancophilSans_README.txt has its terms)
README.md, LICENSE, THIRD_PARTY_NOTICES.md

Logs
----
standalone_debug.log / standalone_error.log / standalone_crash.log are written to the
folder the game runs from. Settings and saves are under ~/.local/share/Banished.
Please report problems at https://github.com/fran-cap/OpenBA/issues with the logs attached.
