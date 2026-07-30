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
  --data-path examples/02_scumm_inventory/data \
  --room yard.yaml
```

Or start without a file and pick from a folder via the **Room file** dropdown:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --data-path examples/02_scumm_inventory/data
```

Rooms default to `<data-path>/rooms`. Use `--rooms-dir` to override that
directory; it may be relative to the data directory or an absolute path inside
it. Room backgrounds and objects may live elsewhere under the data directory.
Useful extras: `--host` / `--port` (bind address).

## What you can do

- Load, edit, and save room YAML (layers + geometry only; rest preserved).
- Draw and edit the **walkable** polygon, named **points** (e.g. `player_start`),
  and **zones** (room-transition polygons).
- Move, resize, and depth-sort **background layers** — in *layers* mode, drag a
  layer's corner handles to resize (aspect-locked, base-anchored so furniture stays
  grounded). The Background panel exposes numeric `scale`/`z` and a **z = base**
  button that sorts a layer by its floor line (handy for occluders).
- Add editor-only **PC/NPC previews** from `cast.yaml`, move or scale their
  first/default animation frame, and copy their floor-pivot position and scale.
  Previews depth-sort against furniture and are never saved into room YAML.
- Browse object and background assets throughout the game data directory while
  keeping their YAML paths relative to the room.
- Scroll beyond the room background on every side. The workspace follows the
  farthest editable entity with extra margin, allowing negative coordinates and
  off-screen staging points or paths.

## Headless patching

Apply a YAML/JSON patch without the UI:

```bash
PYTHONPATH=tools python3 -m tools.room_editor edit \
  --room examples/02_scumm_inventory/data/rooms/yard.yaml \
  --patch patch.yaml \
  --base-path examples/02_scumm_inventory/data
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

- [Point & click concepts § rooms, layers, zones](../../development/design/04-point-and-click-concepts.md)
- [Data formats](../data-formats.md)

---

> **TODO (skeleton):** add a screenshot of the editor and a step-by-step "author
> your first room" walkthrough.
