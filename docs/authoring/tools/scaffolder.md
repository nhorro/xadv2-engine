# Scaffolder

Bootstraps a new game or experiment from a template directory so you don't have
to copy an example by hand and rename half its contents. Issue #134.

!!! note "Canonical reference"
    Source, template list, and the placeholder format:
    [`tools/scaffolder/`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/scaffolder).

## Run

```bash
python -m tools.scaffolder                         # interactive
python -m tools.scaffolder --list                  # available templates
python -m tools.scaffolder \
    --type game \
    --short-name mygame \
    --title "My Game"
```

Three questions:

1. **Type** — `game` or `experiment` (`other` asks where, and which template).
2. **Short name** — `[a-z][a-z0-9_-]*`. Used for the directory, the binary, and
   the manifest `id`.
3. **Title** — any UTF-8. Drops into the README, the manifest, and the intro
   cutscene's first slide.

## The two templates land in different worlds

| Template | What it generates | Default destination |
|---|---|---|
| `game` | A **standalone project**, with its own `CMakeLists.txt`, README, `.gitignore`, `run.sh` and `vcpkg.json`. Title screen + settings + save/load picker + intro cutscene + a starting room: a real game loop on day one. | `../games/<short_name>` — the workspace's games directory, alongside the engine checkout |
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

Both templates ship a placeholder avatar, the Departure Mono font, and a
fully-populated Spanish strings file.

## Add a new template

Drop a directory under `tools/scaffolder/templates/`. The scaffolder picks it up
on the next run and lists it via `--list`. Text files (`.cpp`, `.yaml`, `.lua`,
`CMakeLists.txt`, …) get placeholder substitution; binary files are copied
verbatim, so a real font or sample PNG can sit alongside the template's source.

Placeholders available inside any text file:

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
