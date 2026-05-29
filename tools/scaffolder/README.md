# scaffolder

Bootstraps a new game or experiment from a template directory. Issue #134.

```bash
python -m tools.scaffolder                                     # interactive
python -m tools.scaffolder --list                              # available templates
python -m tools.scaffolder \
    --type experiment \
    --short-name shaders_lab \
    --title "Laboratorio de shaders"
```

Three questions:

1. **Type** — `experiment`, `game`, or `other`.
   * `experiment` lands under `experiments/<short_name>/`.
   * `game` lands under `games/<short_name>/`.
   * `other` asks for an output directory and which template to use.
2. **Short name** — `[a-z][a-z0-9_-]*` (lowercase ASCII, digits, underscore,
   dash). Used as the directory name, the CMake target (`pac_<short_name>`),
   and the manifest `id` (R1: must match `[a-z0-9_-]+`).
3. **Title** — any UTF-8 string. Used in the README, the manifest, the intro
   cutscene's title slide, etc.

When the scaffold finishes it prints the line to add to the parent
`CMakeLists.txt` (`add_subdirectory(<short_name>)`), so the build picks up
the new target.

## Templates

Each directory under `templates/` is a self-contained template. The scaffolder
walks it and copies every file into the target; **text files** (whitelist:
`.cpp`, `.hpp`, `.yaml`, `.lua`, `.md`, `.sh`, `CMakeLists.txt`, `.gitignore`,
...) have placeholders substituted, **binary files** are copied verbatim so a
real font or PNG drops in cleanly.

Placeholder set (used inside any text file):

| Placeholder | Meaning |
|-------------|---------|
| `{{short_name}}` | OS-friendly id (the answer to question 2). |
| `{{title}}` | Display title (the answer to question 3). |
| `{{base}}` | Parent directory name (`experiments`, `games`, or the first component of an `--output` override). |

An unknown placeholder is a hard error — keeps typos from silently producing
junk.

### Built-in templates

| Template | What it generates | Default destination |
|----------|-------------------|---------------------|
| `experiment` | One `RoomScene` (navy fill, placeholder blob avatar), no title, no inventory. The smallest possible playground for shader / mechanic exploration. | `experiments/<short_name>/` |
| `game` | Title screen + settings + save/load picker + manual-mode intro cutscene + a starting room. A real game loop on day one. | `games/<short_name>/` |

Both ship a 32×48 placeholder avatar, the Departure Mono font, and the
required Spanish strings.

### Adding a template

Drop a new directory under `templates/`. The scaffolder picks it up
automatically and lists it in `--list`. Files use the same placeholder
syntax; for binary files just commit them.

A template with no obvious base directory (something other than `experiment`
or `game`) is selectable via `--type other`; the user is asked for the
output path in that case.

## What the scaffolder does **not** do

- It doesn't edit the parent `CMakeLists.txt` automatically. The hint at the
  end of a successful run tells you what to paste.
- It doesn't drop the binary or assets into a pak archive — use
  `tools/pack/pack.py` for that.
- It doesn't generate art. The placeholder blob is intentionally ugly so
  it's obvious you should swap it.
