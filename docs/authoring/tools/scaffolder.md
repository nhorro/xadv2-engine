# Scaffolder

Bootstraps a new game or experiment from a template directory so you don't
have to copy an example by hand and rename half its
contents. Issue #134.

!!! note "Canonical reference"
    Source, template list, and the placeholder format:
    [`tools/scaffolder/`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/scaffolder).

## Run

```bash
python -m tools.scaffolder                         # interactive
python -m tools.scaffolder --list                  # available templates
python -m tools.scaffolder \
    --type experiment \
    --short-name shaders_lab \
    --title "Laboratorio de shaders"
```

Three questions, then a directory drops at `experiments/<short_name>/` or
`games/<short_name>/`:

1. **Type** — `experiment` lands under `experiments/`, `game` under
   `games/`, `other` asks where (and which template to use).
2. **Short name** — `[a-z][a-z0-9_-]*`. Used for the directory, the
   `pac_<short_name>` binary, and the manifest `id` field.
3. **Title** — any UTF-8. Drops into the README, the manifest, the intro
   cutscene's first slide.

The tool prints the `add_subdirectory(<short_name>)` line to paste into
the parent `CMakeLists.txt`; once that's done a fresh `cmake --build`
produces a runnable binary.

## Built-in templates

| Template | What it generates | Default destination |
|----------|-------------------|---------------------|
| `experiment` | One `RoomScene` (navy fill, placeholder blob avatar), no title, no inventory. The smallest possible playground for shader / mechanic exploration. | `experiments/<short_name>/` |
| `game` | Title screen + settings + save/load picker + manual-mode intro cutscene + a starting room. A real game loop on day one. | `games/<short_name>/` |

Both ship a 32×48 placeholder avatar, the Departure Mono font, and a
fully-populated Spanish strings file.

## Add a new template

Drop a directory under `tools/scaffolder/templates/`. The scaffolder picks
it up on the next run and lists it via `--list`. Text files (`.cpp`,
`.yaml`, `.lua`, `CMakeLists.txt`, …) get placeholder substitution; binary
files are copied verbatim, so a real font or sample PNG can sit alongside
the template's source.

Placeholders available inside any text file:

| Placeholder | Meaning |
|-------------|---------|
| `{{short_name}}` | OS-friendly id. |
| `{{title}}` | Display title. |
| `{{base}}` | Parent directory name (`experiments`, `games`, …). |

An unknown placeholder is a hard error — better a loud `SystemExit` than a
silent typo in the generated tree.

## What it deliberately doesn't do

- Edit the parent `CMakeLists.txt` automatically. The hint at the end of a
  successful run tells you what to paste.
- Generate art. The placeholder blob is intentionally ugly so it's obvious
  it has to be replaced.
- Wire a pak archive. Use [the resource packer](../../development/design/02-architecture-overview.md#resource-source) for that.
