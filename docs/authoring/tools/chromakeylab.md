# Chroma Key Lab

Removes a chroma-key / solid-colour background from a generated image and
produces a clean RGBA, cropped to the silhouette. Generated artwork (ChatGPT
and other diffusion models) rarely has true transparency, and "transparent"
backgrounds come back noisy, so the lab gives you a tunable pipeline rather
than a one-shot script.

!!! note "Canonical reference"
    Source, the latest options, and the full per-step parameter table:
    [`tools/chromakeylab/`](https://github.com/nhorro/xadv2-engine/tree/develop/tools/chromakeylab).

The lab has two deliverables that share the same algorithm:

- A **Jupyter notebook** (`chroma_key_lab.ipynb`) for step-by-step inspection.
- An **interactive OpenCV tuner** (`chroma_key_tuner.py`) with sliders for
  fast iteration on a single specimen.

## When to use which

| Use the **notebook** to… | Use the **tuner** to… |
|--------------------------|------------------------|
| Diagnose a failure mode (halo, eaten edges, punched holes) | Find the right `RGB thresh` / `Hue tol` / morphology on a known-good specimen in seconds |
| See the intermediate masks and the residual heatmap | Save a one-off output PNG with `S` |
| Tune `KEY_RGB_OVERRIDE`, `RESIDUAL_DIST_TOL`, etc. once before batching | Compare borders-on/off live |

## Requirements

```bash
pip install opencv-python numpy matplotlib  # add jupyter for the notebook
```

## Pipeline

The same eight steps run in both the notebook and the tuner:

1. **Auto-detect the chroma key** from the four corner patches (median per
   channel). Manual override available in both tools.
2. **Distance to key** in RGB and HSV (combined with a high-saturation /
   high-value gate so dark and grey pixels stay foreground).
3. **Border flood-fill**: keep only the chroma-like components that touch a
   border row or column. Interior chroma-coloured regions (a magenta dress
   on a magenta background, foliage that shares hue with a green key)
   survive.
4. **Mask cleanup** (open / close / dilate / erode).
5. **Soft alpha** from the distance transform inside the foreground mask.
6. **Despill** — chroma-direction-aware. Pulls the key's dominant channels
   toward its suppressed channel(s) at edge pixels (magenta / cyan /
   yellow handled symmetrically with R / G / B; near-grey keys are a
   no-op).
7. **Residual QA**: distance-to-key on the silhouette edge band
   (`0 < α < 1`), before / after despill, with a heatmap of remaining halo
   pixels. Optional `RESIDUAL_FIX` zeros alpha where residuals are
   detected.
8. **Crop to content** so the output PNG is no larger than the silhouette.

## Notebook usage

Drop your image somewhere visible to the notebook (default
`data/<set>/<name>.png`) and edit the first cell:

```python
IMAGE_PATH = Path("data/julia/julia.png")
# Auto-detect; set to e.g. np.array([255, 0, 255], dtype=np.uint8) to override
KEY_RGB_OVERRIDE = None
```

Then run the cells top to bottom. Each tunable knob sits in the cell that
uses it; the failure-modes table at the end of the notebook maps each
symptom back to the parameter that controls it.

## Tuner usage

```bash
python tools/chromakeylab/chroma_key_tuner.py your_layer.png \
    --output extracted.png
# explicit chroma override:
python tools/chromakeylab/chroma_key_tuner.py your_layer.png --key 255 0 255
```

Sliders cover every knob; press `S` to save (cropped automatically), `Q` /
`Esc` to quit, and toggle `Border only` to compare the legacy "kill every
chroma-like pixel" behaviour against the border-flood-fill default.

When `--key` is omitted the tuner prints the auto-detected chroma at
startup — sanity-check it before tweaking.

## Typical workflow

1. Run the tuner on a representative specimen and dial in the sliders.
2. Re-run the notebook with the same parameter values; look at the
   residual heatmap and decide whether to enable `RESIDUAL_FIX` for the
   batch.
3. Use the cropped output as a background layer, or feed it into the
   [Spritesheet packer](spritesheet-packer.md) if it's a sprite sheet.
