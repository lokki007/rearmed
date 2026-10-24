# Building REARMED

REARMED is ioquake3 (engine) plus Quake III game code (cgame, game, ui) compiled to QVM bytecode and
packed with our own assets into a pk3. Everything here is GPL v2 (`ioq3/COPYING.txt`).

## What you need

- Windows 10/11 (the release targets win64; Linux and macOS builds of ioquake3 work but are untested here)
- MinGW-w64 GCC, CMake 3.25+ and Ninja on `PATH`
- Python 3.10+ (packing the pk3s and the release zip)
- Your own Quake III Arena install (to run and test; the build itself only reads its pak file names to
  make sure REARMED never ships an id Software file)

## Build

```bash
cmake -S ioq3 -B ioq3/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GAME_QVMS=ON -DBUILD_GAME_LIBRARIES=OFF -DBUILD_MISSIONPACK=OFF \
  -DUSE_INTERNAL_LIBS=ON -DUSE_RENDERER_DLOPEN=ON
cmake --build ioq3/build
```

This gives `ioq3/build/Release/ioquake3.exe`, `ioq3ded.exe`, the two renderer DLLs and
`baseq3/vm/{cgame,qagame,ui}.qvm`.

## Package

```bash
Q3_BASE_WIN="C:\path\to\Quake 3 Arena" python tools/package.py
```

writes `release/REARMED-<version>-win64.zip` (engine, the mod pk3, launcher and setup script, no id
data) and `release/REARMED-<version>-src.zip`. `REARMED_VERSION` is in `ioq3/code/game/bg_public.h`.

The maintainer's own loop is `tools/build.sh` (build + pk3s + deploy to a local test install); it expects
the portable toolchain paths in `tools/env.sh`, so adjust those for your machine.

## What is not in this repository

- No id Software or Team Arena data. The game reads it from the player's install, and `setup.ps1` builds
  the Team Arena weapon pack from the player's own copy (`ta_manifest.txt` lists the files).
- Not the remix map sources (`rm_camp`, `rm_lost`, `rm_yard`): they are decompiled from id's maps.
  `maps/remixes.py` and `tools/remix.py` rebuild them from your own install.
- No builds, test installs or player saves.
