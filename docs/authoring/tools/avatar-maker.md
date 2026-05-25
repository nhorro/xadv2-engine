# Avatar maker

!!! warning "Experimental / questionable"
    This was an exploratory editor for **composite** avatars (body + head + props
    assembled per frame). Its usefulness and design are under review and may
    change or be removed. Use the [Spritesheet packer](spritesheet-packer.md) for
    standard single-image avatars.

A web-based composer that builds avatar animation frames from a merged atlas and
saves them as YAML, plus a small **anchor editor** for placing named pivot points.

!!! note "Canonical reference"
    [`tools/avatarmaker/README.md`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/avatarmaker)
    and the step-by-step
    [`HOWTO.md`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/avatarmaker/HOWTO.md).

## Requirements

```bash
python3 -m pip install PyYAML
```

## Composer

```bash
python3 avatar_composer.py \
  --atlas data/test_output/avatar/atlas_merged.yml \
  --animation data/test_output/avatar/avatar_animation.yml
```

In the browser editor you can pick sprites from the atlas palette, place body /
head / props onto a frame, drag layers to position them, create frame ids like
`walking_front_0`, and save/reload the animation YAML.

## Anchor editor

```bash
python3 anchor_editor.py --atlas data/test_output/atlas.yml
```

Open the printed local URL and click on a frame to add named anchors (e.g.
`walking_pivot`, `neck_pivot`, `left_hand_pivot`), stored relative to each
sprite's top-left corner.

## Naming convention (from the HOWTO)

Composite frame ids encode part + direction + pose + sequence number:

- **Part** — `b` body, `h` head
- **Direction** — `u` up, `r` right, `l` left, `d` down
- **Pose** — `w` walking, `s` standing
- Sequence — `0`, `1`, `2`, …

Examples: `bus0` (body, up, standing), `brw0`/`brw1` (body, right, walking frames).

---

> **TODO (skeleton):** decide the future of this tool (keep / redesign / remove)
> and update this page accordingly.
