Spritesheet packet
==================

Image generators such as ChatGPT generate images that are not transparent, do not have sprites properly arranged in a fixed cell size, have artifacts, redundancies, etc. 
This tool is to extract each sprite to a separate frame and create a new image that contains all script and a YAML with the rects and anchor points.

There are two python scripts:

- A script to pack a spritesheet and generate `.png` + `.yml`
- A script to merge packed spritesheets and atlases into a single one.

## Packed atlas

Create a compact atlas and YAML metadata from a transparent source spritesheet:

```bash
python3 pack_spritesheet.py \
  --frames-dir data/test_output/frames \
  --out-image data/test_output/packed_spritesheet.png \
  --out-yaml data/test_output/atlas.yml \
  --add-anchor walking_pivot 0.5 1.0 \
  --add-anchor neck_pivot 0.5 0.3 \
  data/test_input/spritesheet_alpha.png
```

The generated YAML stores each sprite rectangle in atlas coordinates:

```yaml
image: "packed_spritesheet.png"
size:
  width: 1024
  height: 1024
sprites:
  - id: "frame_000"
    rect:
      x: 775
      y: 2
      width: 95
      height: 181
    source_rect:
      x: 174
      y: 34
      width: 95
      height: 181
    anchors:
      walking_pivot:
        x: 48
        y: 181
      neck_pivot:
        x: 48
        y: 54
```

Anchor factors are relative to each sprite rect. `0.0, 0.0` is the top-left
corner, and `1.0, 1.0` is the bottom-right edge.

## Merge packed atlases

Once you have multiple packed atlas PNG/YAML pairs, merge them into a single
atlas with `merge_atlases.py`.

```bash
python3 merge_atlases.py \
  --atlas data/test_output/avatar/body.png data/test_output/avatar/atlas_body.yml --namespace body \
  --atlas data/test_output/avatar/head.png data/test_output/avatar/atlas_head.yml --namespace head \
  --out-image data/test_output/avatar/merged.png \
  --out-yaml data/test_output/avatar/atlas_merged.yml \
  --max-width 1024 --padding 2
```

The merged output contains all source sprites and preserves per-sprite anchors.
The YAML is written in the same atlas format as `pack_spritesheet.py`.

Alternatively, run the bundled `pack.sh` in this folder to pack body and head
and then merge them into a single atlas:

```bash
./pack.sh
```

