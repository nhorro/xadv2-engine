# Scaffolder

Creates games, disposable prototypes, and in-engine experiments from template
directories. It can also add common authoring recipes to an existing project.

```bash
# New command form
python -m tools.scaffolder new prototype panel_lab --title "Panel Lab"
python -m tools.scaffolder new game my_game --title "My Game"
python -m tools.scaffolder new experiment shaders_lab --title "Shader Lab"

# Add to a project; the project is discovered from any child directory
python -m tools.scaffolder add room courtyard --project ../games/panel_lab
python -m tools.scaffolder add room courtyard --project ../games/panel_lab --dry-run
python -m tools.scaffolder add script-scene arcade --project ../games/panel_lab

# Existing command form remains supported
python -m tools.scaffolder --type experiment --short-name shaders_lab --title "Shader Lab"
python -m tools.scaffolder --list
```

Running `python -m tools.scaffolder` without arguments keeps the interactive
workflow. It asks for:

1. **Type** — `prototype`, `experiment`, `game`, or `other`.
   * `prototype` lands under `games/<short_name>/` as a minimal standalone project.
   * `experiment` lands under `experiments/<short_name>/` as part of the engine build.
   * `game` lands under `games/<short_name>/` as a complete standalone game shell.
   * `other` asks for an output directory and which template to use.
2. **Short name** — `[a-z][a-z0-9_-]*`. Used for the directory, binary, and manifest id.
3. **Title** — any UTF-8 display title.

For in-engine experiments, the scaffold prints the `add_subdirectory` line for
the parent CMake file. Standalone games and prototypes print their configure,
build, and run commands.

## Project templates

| Template | What it generates | Default destination |
|---|---|---|
| `prototype` | One direct-entry `RoomScene`, placeholder avatar, no title, inventory, intro, or save/load flow. A disposable standalone playground. | `../games/<short_name>/` |
| `experiment` | The same small playground, but joined to the engine's own build. | `experiments/<short_name>/` |
| `game` | Title, settings, save/load pickers, intro cutscene, and starting room. | `../games/<short_name>/` |

Each directory under `templates/` is self-contained. Text files (`.cpp`,
`.yaml`, `.lua`, `CMakeLists.txt`, and similar) receive `{{placeholder}}`
substitution. Binary files are copied verbatim. Placeholders work in relative
file names as well as file contents.

Built-in project placeholders:

| Placeholder | Meaning |
|---|---|
| `{{short_name}}` | OS-friendly project id. |
| `{{title}}` | Display title. |
| `{{base}}` | Parent directory name. |

Unknown placeholders and unsafe generated paths are hard errors.

## Additive recipes

Recipes live under `recipes/` and render into an existing project. The
scaffolder locates the nearest ancestor containing `data/game.yaml`, checks all
destination paths before writing, and refuses to overwrite existing files.

The `room` recipe creates:

```text
data/rooms/<room_id>.yaml   static room data and starter geometry
data/rooms/<room_id>.lua    lifecycle and hotspot behaviour skeleton
```

Rooms are discovered by `RoomScene` from its configured `rooms` directory, so
the recipe does not rewrite the manifest. Link the new room from Lua with
`change_room("<room_id>", "player_start")`.

The `script-scene` recipe creates a component-shaped scene YAML and a Lua
lifecycle/input/update sidecar under `data/scenes/<scene_id>/`, then appends a
`type: ScriptScene` descriptor to `data/game.yaml`. The manifest is edited as
text so existing comments and formatting are preserved. `--dry-run` includes the
manifest in its plan without writing it.

## What the scaffolder does not do

- It does not create a git repository or remote.
- It does not generate art.
- It does not build a pak archive; use `tools/pack/pack.py`.
- Additive recipes never overwrite outputs or duplicate a scene id. Edit or
  remove existing generated content explicitly if you intend to replace it.
