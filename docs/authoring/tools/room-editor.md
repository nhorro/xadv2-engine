# Room editor

A minimal web-based editor for the xadv2-engine room YAML format. It edits the
**background layers** and **geometry** sections (walkable area, named points,
zones) and preserves every other room field intact.

!!! note "Canonical reference"
    Full usage and patch format:
    [`tools/room_editor/README.md`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/room_editor).

## Requirements

- Python 3.9+
- `PyYAML` (`pip install pyyaml`)

## Start the editor

Open a specific room in the browser:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --room games/themummy/data/rooms/hall.yaml
```

Or start without a file and pick from a folder via the **Room file** dropdown:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --base-path games/themummy/data/rooms
```

Useful extras: `--base-path` (override asset root for validation),
`--host` / `--port` (bind address).

## What you can do

- Load, edit, and save room YAML (layers + geometry only; rest preserved).
- Draw and edit the **walkable** polygon, named **points** (e.g. `player_start`),
  and **zones** (room-transition polygons).
- Move, resize, and depth-sort **background layers** — in *layers* mode, drag a
  layer's corner handles to resize (aspect-locked, base-anchored so furniture stays
  grounded). The Background panel exposes numeric `scale`/`z` and a **z = base**
  button that sorts a layer by its floor line (handy for occluders).
- Validate assets against a base asset directory.

## Headless patching

Apply a YAML/JSON patch without the UI:

```bash
PYTHONPATH=tools python3 -m tools.room_editor edit \
  --room games/themummy/data/rooms/hall.yaml \
  --patch patch.yaml \
  --base-path games/themummy/data
```

```yaml
geometry:
  walkable:
    - {x: 40, y: 600}
    - {x: 1560, y: 600}
    - {x: 1560, y: 700}
    - {x: 40, y: 700}
  points:
    player_start: {x: 120, y: 650}
  zones:
    - id: to_hall
      polygon:
        - {x: 1560, y: 600}
        - {x: 1600, y: 520}
        - {x: 1600, y: 720}
        - {x: 1560, y: 720}
```

## See also

- [Point & click concepts § rooms, layers, zones](../../sources/design/04-point-and-click-concepts.md)
- [Data formats](../data-formats.md)

---

> **TODO (skeleton):** add a screenshot of the editor and a step-by-step "author
> your first room" walkthrough.
