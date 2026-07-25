# Building a game outside the engine repo

**Your game lives in its own repository.** The engine is a library it links
against — it is not a place to put a game. This page is the whole workflow:
scaffold, build, author, ship.

The engine repo keeps only [`examples/`](https://github.com/nhorro/xadv2-engine/tree/main/examples):
six tiny games, one per feature, that exist to be read.

## The workspace

One directory holds the engine checkout and every game working copy side by side.
Each game is its **own git repository** — `games/` is just where the working
copies live; it is not a repo, and the engine knows nothing about it.

```
point-and-click-game/
├── xadv2-engine/          the engine (this repository)
│   ├── lib/  examples/  tools/  docs/
└── games/                 one working copy per game repository
    ├── ingreso-urgente/
    └── mygame/            the scaffolder puts new games here
```

That layout is why a game's build points at the engine with a relative path
(`-DXADV2_ENGINE_DIR=../../xadv2-engine`) and the tools are reachable at
`../../xadv2-engine`. Nothing depends on it — use `--output` and absolute paths if
you keep your games elsewhere.

## 1. Scaffold

From the engine checkout:

```bash
python -m tools.scaffolder --type game --short-name mygame --title "My Game"
```

It writes a **standalone project** into the workspace's games directory
(`../games/mygame`), never inside the engine — with a manifest, a starting room, a
placeholder character, a title screen and an intro cutscene already wired
together. Then:

```bash
cd ../games/mygame
git init && git add -A && git commit -m "initial scaffold"
```

## 2. Build: two modes

The generated `CMakeLists.txt` resolves the engine in one of two ways, and you
will use both at different stages.

### Source mode — while the engine is still moving

```bash
cmake -S . -B build -DXADV2_ENGINE_DIR=~/workspace/point-and-click-game/xadv2-engine
cmake --build build -j"$(nproc)"
./run.sh
```

The engine is compiled from source as part of your build. **This is the mode to
use today**: when you hit an engine bug (and you will), you fix it in the engine
checkout, rebuild your game, and keep working — no install step, no version
juggling. Your fix is then a normal PR against the engine.

The engine contributes *only its library* to your build: it detects that it is not
the top-level project and leaves its own examples, tests and experiments off.

### Installed mode — once the engine settles

```bash
# in the engine, once:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cmake --install build --prefix ~/.local

# in your game:
cmake -S . -B build -DCMAKE_PREFIX_PATH=~/.local
cmake --build build -j"$(nproc)"
```

Your `CMakeLists.txt` finds it with `find_package(pac_engine 0.9 CONFIG REQUIRED)`
and links `pac::engine`. Nothing else changes.

Add `-DPAC_BUILD_SHARED=ON` to the engine's configure to build
`libpac_engine.so` instead of a static archive. (Linux/macOS today; on Windows the
engine still needs a symbol-export header, and will tell you so rather than hand
you a DLL with no symbols in it.)

## 3. What your CMakeLists needs

For a game that is pure YAML + Lua — which is what a game should be:

```cmake
find_package(pac_engine 0.9 CONFIG REQUIRED)   # or add_subdirectory(${XADV2_ENGINE_DIR})
add_executable(mygame main.cpp)
target_link_libraries(mygame PRIVATE pac::engine)
```

and `main.cpp` in full:

```cpp
#include "engine/pnc/game_app.hpp"

int main(int argc, char** argv) {
    return pac::pnc::run_game_main(argc, argv, "data/game.yaml");
}
```

If your game adds a **scene type of its own** in C++ — a journal, a map, a
minigame; something whose *interaction model* the engine doesn't have — it also
links `pac::sol2`, which is what lets it install its own Lua functions:

```cmake
add_executable(mygame main.cpp src/journal.cpp)
target_link_libraries(mygame PRIVATE pac::engine pac::sol2)
```

The engine's [`examples/06_cpp_scene`](https://github.com/nhorro/xadv2-engine/tree/main/examples/06_cpp_scene)
is that pattern end to end. Reach for it rarely: a room, a dialog, a close-up, a
cutscene and an inventory are all *data*.

!!! warning "Your own third-party libraries are yours to find"
    The engine uses yaml-cpp internally, but as a **private** dependency — the
    imported target is not handed to you. If your C++ parses its own YAML, call
    `find_package(yaml-cpp REQUIRED)` and link it yourself. (Skipping this can
    still *link* on Linux, where the bare name resolves to a system library, and
    then fail on Windows with `cannot open input file 'yaml-cpp.lib'`.)

### Exported targets

| Target | Link it when |
|---|---|
| `pac::engine` | Always. SFML comes with it (it is in the public headers). |
| `pac::sol2` | Your game adds its own Lua functions / C++ scenes (`engine/core/scripting_sol.hpp`). |
| `pac::lua` | You need Lua's headers on their own. Rare. |
| `pac::sanitizers` | Never explicitly — `pac::engine` propagates it. If the engine was built with sanitizers, your game is too, because mixing sanitized and unsanitized objects is unsafe. |

## 4. The authoring tools stay in the engine repo

They are Python and they work on *any* game's data directory. Point them at yours:

```bash
export XADV2_ENGINE=~/workspace/point-and-click-game/xadv2-engine

# trace a room's walkable area, hotspots, objects — in the browser
(cd $XADV2_ENGINE && python -m tools.room_editor serve \
    --room   ~/workspace/point-and-click-game/games/mygame/data/rooms/lab.yaml \
    --base-path ~/workspace/point-and-click-game/games/mygame/data)

# trace a close-up's hotspots
(cd $XADV2_ENGINE && python -m tools.closeup_editor serve \
    --closeup ~/workspace/point-and-click-game/games/mygame/data/closeups/desk/closeup.yml \
    --base-path ~/workspace/point-and-click-game/games/mygame/data)

# pack the game's resources into one archive for shipping
python $XADV2_ENGINE/tools/pack/pack.py data build/resources.pak
```

The runtime auto-discovers a `resources.pak` next to the binary, so a packed build
needs no code change — `--pak` overrides the location.

## 5. Windows

The scaffold ships a `vcpkg.json`. Its two `overrides` are the engine's, and they
matter: the engine uses the **SFML 2.x** API (not SFML 3), and sol2 rejects **Lua
5.5**, which is vcpkg's current default.

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
& "$env:VCPKG_ROOT\vcpkg.exe" x-update-baseline --add-initial-baseline   # once per checkout
cmake -S . -B build -DXADV2_ENGINE_DIR=C:\path\to\xadv2-engine `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

## Where to look things up

* **Every Lua function**: [Lua API reference](lua-api.md).
* **Every YAML field**: [Data formats](data-formats.md).
* **How do I do X?**: the engine's `examples/` — one small game per feature.
* **How the room contents fit together**: [Scenery authoring](scenery.md).
