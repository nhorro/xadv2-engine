# {{title}}

A point-and-click adventure built on the
[Extraordinary Adventures Engine v2](https://github.com/nhorro/xadv2-engine).

This repository is **just the game**: YAML for what exists, Lua for what happens,
and the art. The engine is a library it links against.

## Build

You need an engine checkout (or an installed engine) plus its dependencies:
SFML 2.6, Lua 5.4, yaml-cpp.

```bash
# 1. source mode — recommended while the engine is still moving, because you can
#    fix an engine bug in place and rebuild
cmake -S . -B build -DXADV2_ENGINE_DIR=~/workspace/xadv2-engine
cmake --build build -j"$(nproc)"

# 2. installed mode — once the engine is stable
#    (in the engine: cmake --install <build> --prefix ~/.local)
cmake -S . -B build -DCMAKE_PREFIX_PATH=~/.local
cmake --build build -j"$(nproc)"
```

## Run

```bash
./run.sh                                  # from the repo root
./build/{{short_name}} --frames 5 --shot out.png   # headless smoke + screenshot
```

The manifest paths are relative to the working directory, so run from the repo
root (or pass an absolute manifest path).

## Authoring tools

The room editor, close-up editor, spritesheet packer and resource packer live in
the engine repo. Point them at this game's data:

```bash
export XADV2_ENGINE=~/workspace/xadv2-engine

# trace a room's walkable area, hotspots and objects in the browser
(cd $XADV2_ENGINE && python -m tools.room_editor serve \
    --room ~/games/{{short_name}}/data/rooms/lab.yaml \
    --base-path ~/games/{{short_name}}/data)

# pack the game into a single archive for shipping
python $XADV2_ENGINE/tools/pack/pack.py data build/resources.pak
```

## Layout

```
main.cpp             the entry point (one line — the engine does the rest)
data/
├── game.yaml        the manifest: scenes, resolution, languages, settings
├── cast.yaml        characters and their appearances
├── rooms/           <id>.yaml (geometry, hotspots) + <id>.lua (behaviour)
├── scripts/game.lua game-wide verb fallbacks
├── cutscenes/       slide-based cutscenes
├── characters/      spritesheets + animation rigs
├── strings/         localized UI text (the engine emits no hardcoded strings)
└── fonts/
```

## Where to look things up

* **The Lua API** and **every YAML field**: the engine's `docs/development/design/`
  (05-scripting-api, 06-data-formats).
* **How to do X**: the engine's `examples/` — one small game per feature (rooms,
  SCUMM verbs + inventory, dialog trees, cutscenes, close-ups, custom C++ scenes).
