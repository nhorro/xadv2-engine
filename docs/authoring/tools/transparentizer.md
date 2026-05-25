# Transparentizer

Replaces a chroma-key / solid-colour background with a real alpha channel and
crops the image to its visible content. Diffusion-model output is rarely truly
transparent (and "transparent" backgrounds come back noisy), so this threshold-based
tool produces clean alpha — rudimentary, but enough for now.

!!! note "Canonical reference"
    Source and the latest options:
    [`tools/transparentizer/`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/transparentizer).

## Requirements

```bash
pip install pillow numpy   # see the tool source for exact deps
```

## Basic usage

```bash
python3 -m tools.transparentizer input.png output.png --key 255,0,255
```

`input`/`output` can be single PNGs or directories.

## Key options

| Option | Purpose |
|--------|---------|
| `--key R,G,B` or `--key auto` | Background colour to key out (or auto-detect). |
| `--low N` | RGB distance below which a pixel becomes fully transparent. |
| `--high N` | RGB distance above which a pixel stays fully opaque. |
| `--no-despill` | Disable halo / colour-spill correction. |
| `--no-crop` | Keep the canvas; do not crop to visible content. |
| `--min-alpha N` | Minimum alpha for a pixel to count as part of the object. |
| `--margin N` | Optional margin around the cropped object. |

## Typical workflow

1. Run with `--key` set to the generated background colour (or `auto`).
2. Tune `--low` / `--high` until edges are clean and there's no halo.
3. Feed the result into the [Spritesheet packer](spritesheet-packer.md) if it's a
   sprite sheet, or use it directly as a background layer.

---

> **TODO (skeleton):** add before/after example images and a recommended preset
> per image source (ChatGPT vs other models).
