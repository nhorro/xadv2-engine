# Scaffolder

Creates a game, a disposable standalone prototype, or an in-engine experiment
from a template directory. It can also add common authoring recipes to an
existing project.

!!! note "Canonical reference"
    Source, template list, and the placeholder format:
    [`tools/scaffolder/`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/scaffolder).

## Run

```bash
python -m tools.scaffolder new prototype panel_lab --title "Panel Lab"
python -m tools.scaffolder new game mygame --title "My Game"
python -m tools.scaffolder add room courtyard --project ../games/panel_lab
python -m tools.scaffolder add room courtyard --project ../games/panel_lab --dry-run
python -m tools.scaffolder add script-scene arcade --project ../games/panel_lab
```

The original interactive and flag-based forms remain available:

```bash
python -m tools.scaffolder
python -m tools.scaffolder --list
python -m tools.scaffolder --type game --short-name mygame --title "My Game"
```

Three questions:

1. **Type** — `game`, `prototype`, or `experiment` (`other` asks where, and which template).
2. **Short name** — `[a-z][a-z0-9_-]*`. Used for the directory, the binary, and
   the manifest `id`.
3. **Title** — any UTF-8. Drops into the README, the manifest, and the intro
   cutscene's first slide.

## Project templates

| Template | What it generates | Default destination |
|---|---|---|
| `game` | A **standalone project**, with its own `CMakeLists.txt`, README, `.gitignore`, `run.sh` and `vcpkg.json`. Title screen + settings + save/load picker + intro cutscene + a starting room: a real game loop on day one. | `../games/<short_name>` — the workspace's games directory, alongside the engine checkout |
| `prototype` | A **minimal standalone project** that enters one room immediately, with placeholder art and no title, cutscene, inventory, or save/load flow. Intended for disposable UI, art, shader, and interaction experiments. | `../games/<short_name>` |
| `experiment` | One `RoomScene` (flat fill, placeholder avatar), no title, no inventory: the smallest playground for exploring a shader or a mechanic. Part of the engine's own build. | `experiments/<short_name>/` |

!!! important "A game is not part of the engine repo"
    It has its own repository and links the engine as a library. That is why the
    `game` template writes a full project and the scaffolder's closing hint is
    `git init` + `cmake -DXADV2_ENGINE_DIR=…`, not `add_subdirectory`. See
    [Building a game](../building-a-game.md).

    An **experiment** *is* part of the engine repo, so there the hint is still the
    `add_subdirectory(<short_name>)` line to paste into `experiments/CMakeLists.txt`.

After scaffolding a game:

```bash
cd ../games/mygame
git init && git add -A && git commit -m "initial scaffold"
cmake -S . -B build -DXADV2_ENGINE_DIR=~/workspace/point-and-click-game/xadv2-engine
cmake --build build -j"$(nproc)"
./run.sh
```

All templates ship a placeholder avatar, the Departure Mono font, and a
fully-populated Spanish strings file.

## Add a room

The room recipe accepts a project root or any directory inside it. The nearest
ancestor containing `data/game.yaml` is treated as the project:

```bash
python -m tools.scaffolder add room courtyard --project ../games/panel_lab
```

It creates `data/rooms/courtyard.yaml` and `data/rooms/courtyard.lua`. All
collisions are checked before writing and existing files are never replaced.
Use `--dry-run` to list the paths without changing the project.

`RoomScene` discovers files in its `rooms` directory, so no manifest rewrite is
needed. Enter the room from Lua with:

```lua
change_room("courtyard", "player_start")
```

## Add a scriptable scene

```bash
python -m tools.scaffolder add script-scene arcade --project ../games/panel_lab
```

This creates `data/scenes/arcade/scene.yaml` for component-shaped sprite and
animation entities, plus `scene.lua` for lifecycle, normalized input, and the
fixed-step update. It also appends the `type: ScriptScene` descriptor to
`data/game.yaml` while preserving the file's comments and formatting.

The operation refuses existing output files and an existing scene id. With
`--dry-run`, it prints both generated files and `data/game.yaml (modify)` without
writing anything. Continue with the [Scriptable scenes guide](../script-scenes.md).

## Add a new template

Drop a directory under `tools/scaffolder/templates/`. The scaffolder picks it up
on the next run and lists it via `--list`. Text files (`.cpp`, `.yaml`, `.lua`,
`CMakeLists.txt`, …) get placeholder substitution; binary files are copied
verbatim, so a real font or sample PNG can sit alongside the template's source.

Placeholders are available inside text files and relative template paths:

| Placeholder | Meaning |
|-------------|---------|
| `{{short_name}}` | OS-friendly id. |
| `{{title}}` | Display title. |
| `{{base}}` | Parent directory name. |

An unknown placeholder is a hard error — better a loud `SystemExit` than a silent
typo in the generated tree.

## What it deliberately doesn't do

- Create the game's git repository or its GitHub remote. It prints the commands.
- Generate art. The placeholder avatar is intentionally ugly so it is obvious it
  has to be replaced.
- Wire a pak archive. Use [the resource packer](../../development/design/02-architecture-overview.md#resource-source)
  for that.
