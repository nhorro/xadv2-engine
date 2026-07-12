# Examples

Six small games, each showing **one** thing. They are the engine's worked
documentation: if an example breaks, CI fails, so what you read here is what the
engine actually does today.

None of them is a game worth playing. That is deliberate — a real game is a
distraction from the mechanic being demonstrated, and it is also the reason the
engine repo no longer ships one. **Your game lives in its own repository and
links the engine as a library**; see
[docs/authoring/building-a-game.md](../docs/authoring/building-a-game.md).

| Example | Shows | Read |
|---|---|---|
| [01_hello_room](01_hello_room/) | A room, a walkable floor, click-to-move, hotspots that answer | `data/rooms/study.{yaml,lua}` |
| [02_scumm_inventory](02_scumm_inventory/) | The verb grid, carrying things, "use X with Y", a second room | `data/inventory.lua`, `data/rooms/workshop.lua` |
| [03_dialog_npc](03_dialog_npc/) | An NPC, a hotspot bound to it, a branching dialog tree | `data/dialogs/curator.lua` |
| [04_cutscene](04_cutscene/) | Title screen, intro cutscene with fades, a mid-game cutscene | `data/cutscenes/intro.yaml` |
| [05_closeup](05_closeup/) | Examining something up close; layer shaders; a custom cursor | `data/closeups/painting/` |
| [06_cpp_scene](06_cpp_scene/) | A game adding a **scene type of its own, in C++** | `src/field_notes.cpp` |

Examples 01–05 contain **no C++ beyond a four-line `main`** — all behaviour is
YAML + Lua. That is the point: a standard game needs no engine code. 06 exists to
document the escape hatch for when it does.

## Running

From the repo root, after `cmake --build build`:

```bash
./run-game.sh                  # 01_hello_room
./run-game.sh 03_dialog_npc    # any example by directory name

# or directly, which is what the smoke tests do:
./build/examples/05_closeup/pac_example_05_closeup --frames 60 --shot out.png
```

In-game, with `development.edit_mode` on (all examples): **F1** walkable area,
**F2** hotspot polygons, **F3** anchors/z-order, **F4** state HUD, **F5** reload
the room's YAML + Lua without restarting.

## Assets

Every example is **self-contained**: copy `examples/<name>/data` anywhere and it
runs, because a manifest has exactly one resource root and cannot reach outside
it. The shared kit (one character rig, one font, flat backgrounds, the stock
shader library, cursor art — ~390 KB) is therefore duplicated into each one.

`_assets/` is the single source of truth. Edit there, then:

```bash
python3 examples/tools/sync_assets.py           # copy the kit into every example
python3 examples/tools/sync_assets.py --check   # fail if any copy is stale
python3 examples/tools/make_backgrounds.py      # regenerate the placeholder art
```

The backgrounds are flat rectangles drawn by a script, not painted art — a room
is geometry and behaviour, and the engine does not care that the wall is a
rectangle. Swap in real art and nothing else changes.
