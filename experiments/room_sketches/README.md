# room_sketches

A minimal sandbox for trying out **shaders, mechanics, and other room-level
ideas** in isolation from the actual game. Spins up a single `RoomScene`
pointed at an empty room with one placeholder character; no title, no intro,
no inventory — the launcher drops straight into the lab.

## Run

From the repo root:

```bash
cmake --build build -j"$(nproc)"
./build/experiments/room_sketches/pac_room_sketches
```

Headless smoke + screenshot:

```bash
./build/experiments/room_sketches/pac_room_sketches --frames 60 --shot out.png
```

Override the manifest to point at a different sandbox:

```bash
./build/experiments/room_sketches/pac_room_sketches path/to/other/game.yaml
```

Pak (issue #109) works the same as for themummy — drop `resources.pak` next
to the binary, or pass `--pak path/to/resources.pak`.

## Layout

```
data/
├── game.yaml                       # manifest (resolution, entry = room_view)
├── cast.yaml                       # one character: `player` -> blob_body
├── characters/blob/
│   ├── blob.png                    # 32x48 placeholder sprite
│   ├── blob.yml                    # atlas (single sprite, walking_pivot anchor)
│   └── blob.anim.yml               # one `stand` sequence
├── rooms/
│   ├── lab.yaml                    # navy room, whole-area walkable polygon
│   └── lab.lua                     # empty behavior; add hooks here
├── strings/es.yaml                 # required ui/verbs/connectors/defaults
└── fonts/DepartureMono-Regular.otf # bundled (OFL)
```

## Adding things

- **A shader**: drop a `.frag` under `data/shaders/`, wire it on a layer /
  region / object in `lab.yaml`. See [docs/sources/design/03-2d-game-concepts.md](../../docs/sources/design/03-2d-game-concepts.md) §Shaders.
- **A hotspot**: add a `hotspots:` entry in `lab.yaml`, then a verb handler
  in `lab.lua`. See [docs/sources/design/04-point-and-click-concepts.md](../../docs/sources/design/04-point-and-click-concepts.md).
- **A second character**: add a cast entry in `cast.yaml`, then place it as
  an NPC in `lab.yaml`'s `avatars:` list with `player: false`.
- **An image background**: add a `layers:` block under `background:` in
  `lab.yaml` and reference an image under `data/`.

Throwaway by design — branch off, hack, throw away. The lab is the canvas;
don't grow it beyond the scope of "one room, one character, lots of shaders".
