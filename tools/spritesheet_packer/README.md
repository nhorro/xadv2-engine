Spritesheet packet
==================

Image generators such as ChatGPT generate images that are not transparent, do not have sprites properly arranged in a fixed cell size, have artifacts, redundancies, etc. 
This tool is to extract each sprite to a separate frame and create a new image that contains all script and a YAML with the rects and anchor points.

There are two python scripts:

- A script to pack a spritesheet and generate `.png` + `.yml`
- A script to merge packed spritesheets and atlases into a single one.

## Requirements

```bash
pip install opencv-python numpy
```

The input image must be a PNG or another image format readable by OpenCV with an alpha channel.

## Basic usage

```bash
python3 spritesheet_packer_with_names.py input_spritesheet.png \
  --out-image atlas.png \
  --out-yaml atlas.yml
```

By default, detected sprites are named:

```text
frame_000
frame_001
frame_002
...
```

The generated YAML contains:

- `image`: atlas image path.
- `size`: atlas dimensions.
- `sprites`: one entry per detected sprite.
- `rect`: packed atlas rectangle.
- `source_rect`: original rectangle in the source spritesheet.
- `anchors`: optional anchor points, when requested.

## Command-line options

### Positional argument

```text
input
```

Transparent source spritesheet. The image must include an alpha channel.

### Required outputs

```text
--out-image PATH
```

Output packed PNG atlas.

```text
--out-yaml PATH
```

Output YAML metadata file.

### Optional frame export

```text
--frames-dir PATH
```

Optional directory where each detected and cropped frame will be written as an individual PNG.

This is useful for debugging detection, checking frame boundaries, and validating that no sprite parts were merged incorrectly.

### Automatic naming

```text
--prefix TEXT
```

Prefix used for automatically generated sprite IDs.

Default:

```text
frame
```

Generated IDs have the form:

```text
<prefix>_000
<prefix>_001
<prefix>_002
```

```text
--start-index N
```

Starting numeric index for automatic sprite IDs.

Default:

```text
0
```

Example:

```bash
python3 spritesheet_packer_with_names.py input.png \
  --out-image atlas.png \
  --out-yaml atlas.yml \
  --prefix julia \
  --start-index 10
```

This produces IDs such as:

```text
julia_010
julia_011
julia_012
```

### Suggested names file

```text
--names-file PATH
```

Optional text file with explicit sprite IDs.

When this option is provided, automatic naming is not used. The file must contain exactly one name per detected sprite, in the same order used by the source traversal algorithm.

This is the recommended option when the spritesheet has a semantic layout, for example:

```text
sf wf0 wf1 wf2 wf3
sr wr0 wr1 wr2 wr3
sb wb0 wb1 wb2 wb3
sl wl0 wl1 wl2 wl3
```

Where, for example:

- `sf`: standing front.
- `wf0`..`wf3`: walking front frames.
- `sr`: standing right.
- `wr0`..`wr3`: walking right frames.
- `sb`: standing back.
- `wb0`..`wb3`: walking back frames.
- `sl`: standing left.
- `wl0`..`wl3`: walking left frames.

The tool validates that:

- All names use only letters, numbers, `_`, `-`, or `.`.
- Names are unique.
- The number of names matches the number of detected sprites.

### Atlas packing

```text
--max-width N
```

Maximum atlas width before starting a new shelf.

Default:

```text
1024
```

If a sprite is wider than `--max-width` minus padding, the tool fails instead of silently corrupting the atlas.

```text
--padding N
```

Transparent pixels between packed sprites.

Default:

```text
2
```

Padding reduces texture bleeding when sprites are rendered with filtering or subpixel coordinates.

### Sprite detection

```text
--alpha-threshold N
```

Pixels with alpha above this value are considered visible.

Default:

```text
10
```

Increase this value if the source image contains almost-transparent noise. Decrease it if valid sprite pixels are very faint.

```text
--merge-kernel N
```

Morphological close kernel size used to join nearby visible parts into a single sprite component.

Default:

```text
7
```

A larger value helps join disconnected parts of the same sprite, such as hair, hands, props, or antialiased outlines. However, if sprites are close together, a large value can incorrectly merge different frames.

Use `--frames-dir` to inspect the result when tuning this value.

```text
--row-tolerance N
```

Y-coordinate tolerance used to group detected boxes into approximate source rows.

Default:

```text
40
```

Increase it if frames in the same row have noticeably different top `y` coordinates. Decrease it if different rows are being grouped together.

```text
--min-area N
```

Ignore connected components smaller than this area in pixels.

Default:

```text
200
```

This filters out tiny alpha artifacts or noise.

### YAML image path

```text
--yaml-image-path TEXT
```

Image path stored inside the YAML file.

By default, the tool stores the output image file name, not the full path.

Example:

```bash
python3 spritesheet_packer_with_names.py input.png \
  --out-image build/atlas.png \
  --out-yaml build/atlas.yml \
  --yaml-image-path assets/characters/julia/atlas.png
```

### Anchors

```text
--add-anchor NAME X_FACTOR Y_FACTOR
```

Add an anchor to every sprite using relative coordinates.

`X_FACTOR` and `Y_FACTOR` must be between `0.0` and `1.0`.

Examples:

```bash
--add-anchor feet 0.5 1.0
```

Adds an anchor at the horizontal center and bottom of each source frame.

```bash
--add-anchor center 0.5 0.5 --add-anchor feet 0.5 1.0
```

Adds both a center anchor and a feet anchor.

Anchor coordinates are computed relative to each sprite crop, not relative to the final atlas.

## Names file format

The names file is a plain text file.

Rules:

- Names are separated by whitespace.
- Blank lines are ignored.
- Text after `#` is treated as a comment.
- Each name must match this character set:

```text
A-Z a-z 0-9 _ - .
```

Example:

```text
# Front row
sf wf0 wf1 wf2 wf3

# Right row
sr wr0 wr1 wr2 wr3

# Back row
sb wb0 wb1 wb2 wb3

# Left row
sl wl0 wl1 wl2 wl3
```

Equivalent one-name-per-line format:

```text
sf
wf0
wf1
wf2
wf3
sr
wr0
wr1
wr2
wr3
sb
wb0
wb1
wb2
wb3
sl
wl0
wl1
wl2
wl3
```

## Source traversal order

The names in `--names-file` must follow the source traversal order, not the final packed atlas order.

The algorithm traverses the source image as follows:

1. Builds a binary mask from the alpha channel.
2. Optionally applies morphological closing using `--merge-kernel`.
3. Finds connected components in the mask.
4. Removes components smaller than `--min-area`.
5. Sorts the detected boxes by `(y, x)`.
6. Groups boxes into approximate rows using `--row-tolerance`.
7. Sorts each row from left to right.
8. Sorts rows from top to bottom by average `y`.
9. Flattens the rows into a single list.

In practical terms, write names in this order:

```text
first row, left to right
second row, left to right
third row, left to right
...
```

For the example character layout:

```text
Standing front | Walking front 0 | Walking front 1 | Walking front 2 | Walking front 3
Standing right | Walking right 0 | Walking right 1 | Walking right 2 | Walking right 3
Standing back  | Walking back 0  | Walking back 1  | Walking back 2  | Walking back 3
Standing left  | Walking left 0  | Walking left 1  | Walking left 2  | Walking left 3
```

The matching names file would be:

```text
sf wf0 wf1 wf2 wf3
sr wr0 wr1 wr2 wr3
sb wb0 wb1 wb2 wb3
sl wl0 wl1 wl2 wl3
```

Important: the final atlas may not preserve this visual order. The packing step sorts sprites by size to make the atlas more compact. The YAML is the authoritative mapping between each sprite ID and its packed atlas rectangle.

## Example output YAML

```yaml
image: "atlas.png"
size:
  width: 1024
  height: 512
sprites:
  - id: "sf"
    rect:
      x: 2
      y: 2
      width: 109
      height: 285
    source_rect:
      x: 26
      y: 0
      width: 109
      height: 285
    anchors:
      feet:
        x: 54
        y: 285
```

## Recommended workflow

1. Run the tool once with automatic names and `--frames-dir`.
2. Check the generated individual frame PNGs.
3. Tune `--alpha-threshold`, `--merge-kernel`, `--row-tolerance`, and `--min-area` until each crop corresponds to exactly one logical frame.
4. Create a `names.txt` file matching the detected order.
5. Run the tool again with `--names-file`.
6. Use the generated YAML in the engine animation definitions.

Example:

```bash
python3 spritesheet_packer_with_names.py julia.png \
  --out-image julia_atlas.png \
  --out-yaml julia_atlas.yml \
  --frames-dir debug_frames \
  --names-file julia_names.txt \
  --add-anchor feet 0.5 1.0
```

## Common issues

### The number of names does not match the number of detected sprites

The tool fails with an error if `--names-file` contains a different number of names than the number of detected sprites.

This usually means one of the following:

- Two neighboring sprites were merged.
- One sprite was split into multiple components.
- A small artifact was detected as a sprite.
- A valid sprite was ignored because its visible area was below `--min-area`.

Use `--frames-dir` to inspect what was actually detected.

### Two frames are detected as one sprite

Try lowering `--merge-kernel`.

### One frame is split into multiple sprites

Try increasing `--merge-kernel`.

### Noise is detected as a sprite

Try increasing `--alpha-threshold` or `--min-area`.

### Rows are detected in the wrong order

Try adjusting `--row-tolerance`.

The algorithm is heuristic. It works best when frames are separated clearly and arranged in visually consistent rows.


