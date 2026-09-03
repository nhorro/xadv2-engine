Extraordinary Adventures Engine v2
==================================

A C++20/SFML engine for third-person, SCUMM-style point-and-click adventure games,
scripted in **Lua** and configured in **YAML**. A remake of
[Extraordinary Adventures](https://github.com/nhorro/ea-engine).

**This repository is the engine, not a game.** Games live in their own
repositories and link the engine as a library — see
[docs/authoring/building-a-game.md](docs/authoring/building-a-game.md):

~~~bash
# scaffold a standalone game (lands in ../games/, alongside the engine checkout)
python -m tools.scaffolder new game mygame --title "My Game"

# or make a disposable, direct-to-room prototype in ../games/
python -m tools.scaffolder new prototype ui_lab --title "UI Lab"

# add a generic YAML + Lua scene to that prototype
python -m tools.scaffolder add script-scene arcade --project ../games/ui_lab

cd ../games/mygame
cmake -S . -B build -DXADV2_ENGINE_DIR=~/workspace/point-and-click-game/xadv2-engine   # engine from source
cmake --build build -j"$(nproc)" && ./run.sh
~~~

Once the engine is stable enough for you, install it and link it instead:
`find_package(pac_engine CONFIG REQUIRED)` → `pac::engine`
(`-DPAC_BUILD_SHARED=ON` for a shared library).

Examples
--------

[`examples/`](examples/) holds seven small games, each showing exactly **one** thing:
a room and click-to-move, the SCUMM verb grid + inventory, NPCs and dialog trees,
title screens and cutscenes, close-ups, a game adding a scene type of its own in
C++, and a generic YAML + Lua `ScriptScene`. They are the engine's worked
documentation, and CI smoke-runs every one of them, so what they show is what the
engine actually does.

~~~bash
./run-game.sh                  # 01_hello_room
./run-game.sh 03_dialog_npc    # any example, by directory name
~~~

Examples 01–05 and 07 contain no C++ beyond a four-line `main`. That is the claim
the engine makes about itself, and the examples are there to keep it honest.

Build instructions
------------------

### Linux

The engine builds its pinned modified SFML source on every platform. On
Ubuntu/Debian, install its native window/audio dependencies together with the
engine's YAML and Lua dependencies:

~~~bash
sudo apt install build-essential cmake libx11-dev libxrandr-dev libxcursor-dev \
    libudev-dev libgl1-mesa-dev \
    libfreetype-dev libopenal-dev libflac-dev libvorbis-dev \
    libyaml-cpp-dev liblua5.4-dev
~~~

~~~bash
./build-linux.sh
~~~

Or directly: `cmake -S . -B build && cmake --build build -j"$(nproc)"`, then
`ctest --test-dir build` (add `-LE gui` to skip the windowed example smoke runs).

### Windows

Requires a [vcpkg](https://github.com/microsoft/vcpkg) checkout. Set `VCPKG_ROOT`
to it (the script also auto-detects a sibling `..\vcpkg`), then:

~~~bat
.\build-windows.bat            REM Debug; pass Release for an optimized build
~~~

This stamps the vcpkg baseline, vcpkg-installs yaml-cpp and Lua 5.4, fetches the
same pinned modified SFML source used on Linux/Android, and builds the engine +
examples (sol2 and doctest stay header-only). For the IDE
/ CMake preset flow that CI uses (`cmake --preset windows-msvc`), see the Windows
section of [CLAUDE.md](CLAUDE.md).

### Android (experimental)

The optional Linux-hosted Android backend can build an APK, run it in the local
emulator, or upload it to a USB/wireless-debugging phone. See
[android/README.md](android/README.md).

### Packaging check

~~~bash
./scripts/check-packaging.sh   # installs the engine, then builds an external game
                               # against it — both find_package and source mode
~~~

Docker
------

Containerized environments to build/run the engine and the authoring tools on
Linux without installing the toolchain locally. See [docker/README.md](docker/README.md).

~~~bash
docker compose run --rm engine-test   # build the engine + run the headless tests
docker compose up room-editor         # then open http://localhost:8000
docker compose run --rm engine        # run an example (X11 + audio, desktop host)
~~~

Documentation
-------------

~~~bash
python3 -m venv .venv && source .venv/bin/activate
pip install -r docs/requirements.txt
mkdocs serve -a localhost:8002 # Room/closeup editors use 8000/8001 by default
~~~

Start with the [as-built architecture tour](docs/development/tour/index.md).
The older [design folder](docs/development/design/) is frozen history.
Game authors use the [authoring API](docs/authoring/index.md) (Lua, YAML, tools).
