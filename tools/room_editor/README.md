# Room Editor

A minimal Python-based room YAML editor for the xadv2-engine room format.

## Features

- Load and save room YAML files.
- Edit background layers, geometry, objects, and dynamic lights.
- Preserve other room fields intact.
- Visual web-based editor for room geometry and points (runs in the browser).
- Create and edit omni and spot lights in **lights** mode. Drag a light's centre
  to move it, its diamond handle to change range, and a spotlight's blue/pink
  handles to set direction and cone angle. The inspector also supports static
  positions and `player`, `avatar:<id>`, or `object:<id>` attachments.
- Preserve lighting fields that are not directly visualised—including ambient
  light, modulation, normal maps, occluders, and projected shadows—when saving.
- Preview PC/NPC cast sprites using the first frame of their default sequence.
  Avatar previews can be moved and scaled, are depth-sorted with furniture, and
  show copyable pivot position/scale values. They are editor-only and are not
  written to room YAML.
- Move, resize, and depth-sort background layers (handy for placing furniture
  occluders). In **layers** mode, select a layer then drag its corner handles to
  resize — scaling is aspect-locked and keeps the layer's base (bottom-centre)
  fixed, so furniture stays grounded. The Background panel exposes the numeric
  `scale`/`z` and a "z = base" button that sorts the layer by its floor line.
- Discover room backgrounds and objects anywhere under the game data directory.
- Scroll beyond every side of the visible room to recover negative coordinates
  and author off-screen points, paths, objects, and staging positions. The
  workspace grows in chunks beyond the farthest editable entity.

## Requirements

- Python 3.9+
- `PyYAML` (`pip install pyyaml`)

## Usage

Start the web-based room editor and open it in a browser:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --data-path examples/02_scumm_inventory/data \
  --room yard.yaml
```

`--data-path` points at the game data directory. Rooms are read from its `rooms`
subdirectory by default, while room-relative references such as
`../objects/desk.png` can reach other assets inside the data tree. `--room` is
optional: omit it to choose from the **Room file** dropdown without restarting.

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --data-path examples/02_scumm_inventory/data
```

Override the room directory when a game does not use `<data>/rooms`:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --data-path examples/02_scumm_inventory/data \
  --rooms-dir scenes/rooms
```

An absolute `--rooms-dir` is also accepted, provided it remains inside
`--data-path`. The older `--base-path <rooms-directory>` form remains available
for compatibility.

Bind to a custom host or port:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve \
  --data-path examples/02_scumm_inventory/data \
  --room yard.yaml --host 0.0.0.0 --port 9000
```

Apply a patch from a YAML or JSON patch file:

```bash
PYTHONPATH=tools python -m tools.room_editor edit --room examples/02_scumm_inventory/data/rooms/yard.yaml --patch patch.yaml --base-path examples/02_scumm_inventory/data
```

Patch format example:

```yaml
background:
  layers:
    - id: sky
      image: backgrounds/study/sky.png
      z: 0
      interactive: false
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
