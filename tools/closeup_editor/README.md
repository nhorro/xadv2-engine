# Close-up and case-template polygon editor

A small offline web tool for drawing the hotspot polygons of a **close-up**
(`closeups/<id>.yml`). Close-ups are simpler than rooms — just a full-screen
background plus named polygon hotspots — so this is a focused, standalone tool
rather than a mode of the room editor. It also edits the polygon areas of
case-resolution template `slots:` such as
`data/cases/last_afternoon/template_a.yaml`.

For close-ups it edits only the `hotspots:` map. Behavior (what a hotspot *does* on click) lives
in the close-up's Lua sidecar (`closeups/<id>.lua`); see
[05 §Close-up scripts](../../docs/development/design/05-scripting-api.md).
For case templates it edits existing slot areas while preserving `accepts`,
`solution`, and all other slot metadata. Slot ids cannot be added, removed, or
renamed in this geometry editor.

## Requirements

- Python 3.10+
- PyYAML (`pip install pyyaml`)

## Run

```bash
# From the repo root. Relative background paths resolve beside the close-up YAML.
# The game data directory is inferred as the root for paths beginning with `/`.
python -m tools.closeup_editor serve \
  --closeup examples/05_closeup/data/closeups/painting/closeup.yml

# Case-resolution template (background is resolved beside the YAML):
python -m tools.closeup_editor serve \
  --closeup ../games/fuera-de-cuadro/data/cases/last_afternoon/template_a.yaml

# Then open http://127.0.0.1:8001/
```

Options: `--base-path <dir>` (override the asset base), `--resolution 1280x720`
(the virtual resolution the polygons are authored in — defaults to 1280×720),
`--host`, `--port`. Omit `--closeup` to pick one from a dropdown.
Nested close-up directories are supported: `background: background.png` resolves
next to that close-up's YAML, matching the engine. A leading slash such as
`background: /backgrounds/shared.png` resolves from the asset base instead.

## Editing

- **New hotspot** → click points on the image; double-click (or Enter) to close
  the polygon (≥ 3 points), then type its id.
- Click a polygon (or a list row) to **select** it; **drag** a vertex to move it.
- **Shift-click** an edge to insert a vertex; **right-click** a vertex to delete it.
- Edit the selected hotspot's `id` / `name` in the sidebar, then **Apply**.
- **Save** writes the `hotspots:` map or template slot geometry back to the YAML.

For a template, the canvas height comes from `canvas_height`; existing slots can
be selected and reshaped with the same vertex controls. Their semantic fields are
not exposed by this editor and remain unchanged.

The polygon coordinates are in the close-up's virtual-resolution space (the canvas
*is* that space). Other YAML fields (`version`, `id`, `background`,
`background_color`) are preserved across a save, **but comments are not** (PyYAML
re-emits the file) — keep comments minimal in files you edit here, or re-add them.

## Headless edit

```bash
python -m tools.closeup_editor edit \
  --closeup closeups/lab_skull.yml --hotspots hotspots.json
```

where `hotspots.json` is `{ "<id>": { "name": "...", "area": [ {"x":..,"y":..}, ... ] } }`.
For a template, the JSON must contain every existing slot id and its `area`;
slot metadata is retained from the YAML.

## Tests

```bash
cd tools && python -m unittest closeup_editor.test_closeup_editor
```
