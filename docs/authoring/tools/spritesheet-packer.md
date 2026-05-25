# Spritesheet packer

Takes a transparent (but messy) generated spritesheet, detects each sprite,
crops it, and produces a **packed PNG atlas + a YAML** describing every sprite's
rectangle, source rectangle, and optional anchor points — ready for the engine's
animation definitions.

!!! note "Canonical reference"
    The full option reference, names-file format, traversal order, and
    troubleshooting live in
    [`tools/spritesheet_packer/README.md`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/spritesheet_packer).

## Requirements

```bash
pip install opencv-python numpy
```

The input image must be a PNG (or other OpenCV-readable format) **with an alpha
channel** — run it through the [Transparentizer](transparentizer.md) first if needed.

## Basic usage

```bash
python3 spritesheet_packer_with_names.py input_spritesheet.png \
  --out-image atlas.png \
  --out-yaml atlas.yml
```

Sprites are auto-named `frame_000`, `frame_001`, … unless you supply a names file.

## Recommended workflow

1. Run once with `--frames-dir debug_frames` to dump each detected crop.
2. Inspect the crops; tune detection until each crop is exactly one logical frame:
   `--alpha-threshold`, `--merge-kernel`, `--row-tolerance`, `--min-area`.
3. Write a `names.txt` matching the **source traversal order** (rows top-to-bottom,
   left-to-right) — e.g. a 4×5 character sheet:
   ```text
   sf wf0 wf1 wf2 wf3
   sr wr0 wr1 wr2 wr3
   sb wb0 wb1 wb2 wb3
   sl wl0 wl1 wl2 wl3
   ```
4. Re-run with `--names-file names.txt` and add anchors, e.g. `--add-anchor feet 0.5 1.0`.
5. Use the generated YAML in the engine animation definitions.

## Key options

| Option | Purpose |
|--------|---------|
| `--out-image` / `--out-yaml` | Output atlas PNG and metadata YAML (required). |
| `--frames-dir` | Dump each detected crop as a PNG (for tuning). |
| `--names-file` | Explicit sprite ids in traversal order (recommended). |
| `--prefix` / `--start-index` | Auto-naming controls. |
| `--alpha-threshold` | Alpha above which a pixel is "visible". |
| `--merge-kernel` | Join nearby parts of one sprite (raise to merge, lower to split). |
| `--row-tolerance` | Y tolerance for grouping boxes into rows. |
| `--min-area` | Ignore components smaller than this (filters noise). |
| `--max-width` / `--padding` | Atlas shelf width and gap between sprites. |
| `--add-anchor NAME X Y` | Add a relative anchor (0..1) to every sprite. |

!!! warning "Atlas order ≠ visual order"
    Packing sorts sprites by size for compactness, so the atlas won't preserve the
    source layout. The **YAML is the authoritative mapping** from each sprite id to
    its packed rectangle.

## Example output

```yaml
image: "atlas.png"
size: { width: 1024, height: 512 }
sprites:
  - id: "sf"
    rect:        { x: 2,  y: 0, width: 109, height: 285 }
    source_rect: { x: 26, y: 0, width: 109, height: 285 }
    anchors:
      feet: { x: 54, y: 285 }
```

---

> **TODO (skeleton):** document the merge script for combining multiple packed
> atlases, and cross-link to the gfx animation format in
> [06 — Data formats](../../sources/design/06-data-formats.md).
