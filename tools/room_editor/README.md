# Room Editor

A minimal Python-based room YAML editor for the xadv2-engine room format.

## Features

- Load and save room YAML files.
- Edit background layers and geometry sections only.
- Preserve other room fields intact.
- Visual web-based editor for room geometry and points (runs in the browser).
- Move, resize, and depth-sort background layers (handy for placing furniture
  occluders). In **layers** mode, select a layer then drag its corner handles to
  resize — scaling is aspect-locked and keeps the layer's base (bottom-centre)
  fixed, so furniture stays grounded. The Background panel exposes the numeric
  `scale`/`z` and a "z = base" button that sorts the layer by its floor line.
- Asset validation against a base asset directory.

## Requirements

- Python 3.9+
- `PyYAML` (`pip install pyyaml`)

## Usage

Start the web-based room editor and open it in a browser:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve --room games/themummy/data/rooms/hall.yaml
```

`--room` is optional. Omit it and point `--base-path` at the rooms folder to start
without a file loaded, then choose one from the **Room file** dropdown (top-left)
and click **Open** — no restart needed to switch between rooms in that folder:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve --base-path games/themummy/data/rooms
```

(Optional) override the asset base path:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve --room games/themummy/data/rooms/hall.yaml --base-path games/themummy/data
```

(Optional) bind to a custom host or port:

```bash
PYTHONPATH=tools python3 -m tools.room_editor serve --room games/themummy/data/rooms/hall.yaml --host 0.0.0.0 --port 9000
```

Apply a patch from a YAML or JSON patch file:

```bash
PYTHONPATH=tools python -m tools.room_editor edit --room games/themummy/data/rooms/hall.yaml --patch patch.yaml --base-path games/themummy/data
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
