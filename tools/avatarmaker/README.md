Avatar composer
===============

Web based tool to compose avatar animation frames from a merged atlas and save them as YAML:

```bash
python3 avatar_composer.py \
  --atlas data/test_output/avatar/atlas_merged.yml \
  --animation data/test_output/avatar/avatar_animation.yml
```

This opens a browser editor where you can:
- choose sprites from the atlas palette
- place body, head, and props onto a frame
- drag layers to position them
- create frame ids like `walking_front_0`
- save and reload the animation YAML later

Then continue using:

```bash
python3 avatar_composer.py \
  --atlas data/test_output/avatar/atlas_merged.yml \
  --animation data/test_output/avatar/avatar_animation.yml
```

## Anchor editor

Run the local graphical editor:

```bash
python3 anchor_editor.py --atlas data/test_output/atlas.yml
```

Then open the printed local URL in a browser. The editor lets you inspect each
frame and add named anchors such as `walking_pivot`, `neck_pivot`, or
`left_hand_pivot`.

Enable **Ghost pivots** to overlay the selected anchor from other frames as
translucent gray reference markers. The frame filter controls which frames count
as related: for example, filter by `wf` before enabling the overlay to compare
only the forward-walking cycle. Frames that share the same anchor coordinates
are grouped into a single ghost marker.

Clicking on the frame stores anchor coordinates relative to the top-left corner
of that sprite rect:

```yaml
  - id: "frame_039"
    rect:
      x: 110
      y: 365
      width: 106
      height: 117
    source_rect:
      x: 910
      y: 1213
      width: 106
      height: 117
    anchors:
      walking_pivot:
        x: 52
        y: 108
      neck_pivot:
        x: 53
        y: 44
```

Dependencies:

```bash
python3 -m pip install PyYAML
```
