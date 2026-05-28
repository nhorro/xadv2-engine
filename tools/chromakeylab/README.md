# Chroma Key Lab

Files included:

- `chroma_key_lab.ipynb`: notebook for step-by-step inspection.
- `chroma_key_tuner.py`: interactive OpenCV trackbar application.

Both share the same algorithm — the notebook is the design surface, the
tuner is the fast iteration loop on a single specimen.

## Pipeline

1. **Auto-detect the chroma key** from the four corner patches (median per
   channel). Manual override available in both tools.
2. **Distance to key** in RGB and HSV (combined with a high-saturation /
   high-value gate so dark and grey pixels stay foreground).
3. **Border flood-fill**: keep only the chroma-like components that touch a
   border row or column. Interior chroma-coloured regions (a magenta dress
   on a magenta background, foliage on a green key) survive.
4. **Mask cleanup** (open / close / dilate / erode).
5. **Soft alpha** from the distance transform inside the foreground mask.
6. **Despill** — chroma-direction-aware. Pulls the key's dominant channels
   toward its suppressed channel(s) at edge pixels (magenta/cyan/yellow
   handled symmetrically with R/G/B; near-grey keys are a no-op).
7. **Residual QA**: distance-to-key on the silhouette edge band
   (`0 < α < 1`), before / after despill, with a heatmap of remaining halo
   pixels. Optional `RESIDUAL_FIX` zeros alpha where residuals are detected.
8. **Crop to content** so the output PNG is no larger than the silhouette.

## Notebook usage

Place or copy your image somewhere visible to the notebook (default path
is `data/julia/julia.png`) and edit:

```python
IMAGE_PATH = Path("data/julia/julia.png")
# Auto-detect; set to e.g. np.array([255, 0, 255], dtype=np.uint8) to override
KEY_RGB_OVERRIDE = None
```

Then run the cells top to bottom. Each tunable parameter sits in the cell it
affects:

| Cell | Knob | Purpose |
|------|------|---------|
| Estimate chroma | `KEY_RGB_OVERRIDE`, `PATCH` | manual override / corner sample size |
| Baseline RGB | `RGB_THRESHOLD` | RGB distance under which a pixel counts as chroma |
| HSV keying | `HUE_TOL`, `MIN_S`, `MIN_V` | hue tolerance + saturation/value gate |
| Mask cleanup | `OPEN_K` … `ERODE_K` | morphology kernel sizes (0 disables) |
| Soft alpha | `FEATHER_PX` | width of the alpha transition band |
| Despill | `DESPILL_STRENGTH` | magenta excess removed at edge pixels (0..1) |
| Residual QA | `RESIDUAL_DIST_TOL`, `RESIDUAL_FIX` | residual-detection threshold; toggle hard-zero |
| Crop | `CROP_ALPHA_THRESHOLD`, `CROP_PADDING` | bbox sensitivity + margin |

## Interactive tuner

```bash
pip install opencv-python numpy
python chroma_key_tuner.py your_layer.png --output extracted.png
# explicit override:
python chroma_key_tuner.py your_layer.png --key 255 0 255
```

Controls:
- `RGB thresh`: distance to chroma in RGB space.
- `Hue tol`: hue tolerance around the chroma in OpenCV HSV.
- `Min sat`, `Min val`: avoid selecting low-saturation / dark pixels.
- `Open k`, `Close k`: mask cleanup.
- `Dilate k`: expands background mask; removes halos but can eat edges.
- `Erode k`: shrinks background mask; preserves edge but may leave halo.
- `Feather x10`: soft alpha width in pixels.
- `Despill x100`: chroma fringe reduction. Adapts to whatever `--key` (or
  the auto-detected key) selected, so green / cyan / yellow / magenta all
  work without code changes; a near-grey key is a clean no-op and the
  residual QA cell + `Dilate k` are the tools to reach for there.
- `Border only`: keep only chroma reachable from the image border (off
  reverts to the legacy "every chroma-like pixel" behaviour).
- `View`: switch preview / debug view.

Keys:
- `S`: save the current RGBA, automatically cropped to the silhouette bbox.
- `Q` or `Esc`: quit.

When `--key` is omitted (recommended) the tuner prints the auto-detected
chroma at startup so you can confirm it before tweaking the sliders.
