# Close-up hotspot editor

A small offline web tool for drawing the hotspot polygons of a **close-up**
(`closeups/<id>.yml`). Close-ups are simpler than rooms — just a full-screen
background plus named polygon hotspots — so this is a focused, standalone tool
rather than a mode of the room editor.

It edits only the `hotspots:` map. Behavior (what a hotspot *does* on click) lives
in the close-up's Lua sidecar (`closeups/<id>.lua`); see
[05 §Close-up scripts](../../docs/development/design/05-scripting-api.md).

## Requirements

- Python 3.10+
- PyYAML (`pip install pyyaml`)

## Run

```bash
# From the repo root. Point it at a close-up YAML; the asset base path is inferred
# as the game data directory (so `background: closeups/skull.png` resolves).
python -m tools.closeup_editor serve \
  --closeup examples/05_closeup/data/closeups/painting/closeup.yml

# Then open http://127.0.0.1:8001/
```

Options: `--base-path <dir>` (override the asset base), `--resolution 1280x720`
(the virtual resolution the polygons are authored in — defaults to 1280×720),
`--host`, `--port`. Omit `--closeup` to pick one from a dropdown.

## Editing

- **New hotspot** → click points on the image; double-click (or Enter) to close
  the polygon (≥ 3 points), then type its id.
- Click a polygon (or a list row) to **select** it; **drag** a vertex to move it.
- **Shift-click** an edge to insert a vertex; **right-click** a vertex to delete it.
- Edit the selected hotspot's `id` / `name` in the sidebar, then **Apply**.
- **Save** writes the `hotspots:` map back to the YAML.

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

## Tests

```bash
cd tools && python -m unittest closeup_editor.test_closeup_editor
```
