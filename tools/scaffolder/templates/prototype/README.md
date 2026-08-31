# {{title}}

`{{short_name}}` is a disposable, standalone prototype built on the
Extraordinary Adventures Engine v2. It starts directly in one playable room
with a placeholder character: no title screen, intro, inventory, or save/load
flow is wired.

The prototype lives outside the engine repository under `games/`, but builds
against the engine checkout. This keeps experimental game content separate
without hiding engine changes that the experiment is meant to exercise.

## Build and run

From this directory:

```bash
cmake -S . -B build -DXADV2_ENGINE_DIR=../../xadv2-engine
cmake --build build -j"$(nproc)"
./run.sh
```

For a short headless smoke run and screenshot:

```bash
./run.sh --frames 5 --shot out.png
```

## Add another room

Run the scaffolder from the engine checkout. `--project` may name the prototype
root or any path inside it:

```bash
cd ../../xadv2-engine
python -m tools.scaffolder add room second_room \
    --project ../games/{{short_name}}
```

The recipe creates `data/rooms/second_room.{yaml,lua}` without replacing any
existing file. Enter it from another room's Lua with:

```lua
change_room("second_room", "player_start")
```

Use `--dry-run` to inspect the planned paths first.

## Layout

```text
data/
├── game.yaml                 manifest; enters `room_view` directly
├── cast.yaml                 placeholder player definition
├── characters/blob/         placeholder sprite and animation
├── rooms/lab.yaml            starting room geometry
├── rooms/lab.lua             starting room behaviour
├── strings/es.yaml           required UI strings
└── fonts/                    bundled OFL development font
```

This project is intentionally small. If the idea becomes a real game, generate
the `game` template and move the proven content into it.
