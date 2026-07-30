# Close-up hotspot editor

A small web-based editor for the hotspot polygons of a **close-up**
(`closeups/<id>.yml`). Close-ups are a simpler thing than rooms — a full-screen
background plus a few named polygon hotspots — so this is a focused standalone
tool, not a mode of the [room editor](room-editor.md).

The same editor supports case-resolution templates containing `slots:`. In
template mode it edits existing slot polygons while preserving `accepts`,
`solution`, and other slot metadata.

It edits only the `hotspots:` map (each hotspot's polygon `area` and hover `name`).
What a hotspot *does* on click lives in the close-up's Lua sidecar
(`closeups/<id>.lua`) — see
[Scripting API § Close-up scripts](../../development/design/05-scripting-api.md).

!!! note "Canonical reference"
    Full usage: [`tools/closeup_editor/README.md`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/closeup_editor).

## Requirements

- Python 3.10+
- `PyYAML` (`pip install pyyaml`)

## Start the editor

```bash
python3 -m tools.closeup_editor serve \
  --closeup examples/05_closeup/data/closeups/painting/closeup.yml
# then open http://127.0.0.1:8001/

# Or edit a case-resolution template:
python3 -m tools.closeup_editor serve \
  --closeup ../games/fuera-de-cuadro/data/cases/last_afternoon/template_a.yaml
```

Relative backgrounds resolve beside each close-up YAML, including YAML files in
nested directories. A leading slash makes a path relative to the inferred game
**data** directory; override that resource root with `--base-path`. Hotspot
coordinates are authored in the game's virtual resolution — pass `--resolution
1280x720` if your game differs from the default.

## Editing

- **New hotspot** → click points on the image; double-click (or **Enter**) to
  close the polygon (≥ 3 points), then type its id.
- Click a polygon (or list row) to **select**; **drag** a vertex to move it.
- **Shift-click** an edge to insert a vertex; **right-click** a vertex to delete it.
- Edit the selected hotspot's `id` / `name`, then **Apply**.
- **Save** writes the `hotspots:` map back to the YAML.

For templates, the canvas uses `canvas_height`; adding, removing, and renaming
slots is disabled because those operations require semantic fields that the
geometry editor does not author.

Other YAML fields (`version`, `id`, `background`, `background_color`) survive a
save, but **comments do not** (the file is re-emitted by PyYAML).

## See also

- [Point & click concepts § CloseUp / Examine](../../development/design/04-point-and-click-concepts.md)
- [Scripting API § Close-up scripts](../../development/design/05-scripting-api.md)
- [Data formats § Close-up](../../development/design/06-data-formats.md)
