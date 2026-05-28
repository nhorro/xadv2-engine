# Chroma Key Lab

Files included:

- `chroma_key_lab.ipynb`: notebook for step-by-step inspection.
- `chroma_key_tuner.py`: interactive OpenCV trackbar application.

## Notebook usage

Place or copy your image in the same folder as `input.png`, or edit:

```python
IMAGE_PATH = Path("input.png")
```

Then run the notebook cells.

## Interactive app usage

```bash
pip install opencv-python numpy
python chroma_key_tuner.py your_layer.png --key 255 0 255 --output extracted.png
```

Controls:
- `RGB thresh`: distance to magenta in RGB space.
- `Hue tol`: hue tolerance around magenta in OpenCV HSV.
- `Min sat`, `Min val`: avoid selecting low-saturation/dark pixels.
- `Open k`, `Close k`: mask cleanup.
- `Dilate k`: expands background mask; removes halos but can eat edges.
- `Erode k`: shrinks background mask; preserves edge but may leave halo.
- `Feather x10`: soft alpha width in pixels.
- `Despill x100`: magenta fringe reduction.
- `View`: switch preview/debug view.

Keys:
- `S`: save RGBA output.
- `Q` or `Esc`: quit.

Recommended initial chroma key: `#FF00FF`, i.e. RGB `(255, 0, 255)`.
