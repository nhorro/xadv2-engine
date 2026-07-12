#!/usr/bin/env python3
"""Generate the examples' placeholder backgrounds.

The examples deliberately ship no real art: every background here is a handful
of flat rectangles drawn at build-your-own-adventure quality. That keeps the
engine repo small and makes the point that a room is geometry + behaviour, not
a painting. Swap these PNGs for real art and nothing else changes.

The rectangles below are also the source of truth for the hotspot polygons in
the rooms' YAML — run this with `--print-shapes` to get them.

    python3 examples/tools/make_backgrounds.py            # regenerate the PNGs
    python3 examples/tools/make_backgrounds.py --print-shapes

Requires Pillow.  Output: examples/_assets/backgrounds/
"""

import argparse
import pathlib

from PIL import Image, ImageDraw

# The scenery viewport: the virtual resolution (1280x720) minus the SCUMM panel
# (108px tall, anchored at y=612). A background of exactly this size fills the
# view with no camera scrolling — the simplest case for a first room.
W, H = 1280, 612
# Full-screen surfaces (close-ups, title art) use the whole virtual resolution.
FW, FH = 1280, 720

OUT = pathlib.Path(__file__).resolve().parent.parent / "_assets" / "backgrounds"

# Every interactable shape, in room coordinates. The rooms' hotspot polygons
# quote these numbers, so a shape moved here must be moved there too.
SHAPES = {
    "interior": {
        "floor_y": 430,
        "window": (140, 140, 320, 300),
        "poster": (760, 160, 900, 290),
        "bench": (420, 352, 660, 432),
        "door": (1000, 170, 1150, 432),
    },
    "yard": {
        "floor_y": 400,
        "gate": (90, 190, 250, 420),
        "crate": (560, 330, 700, 425),
        "lamp": (980, 120, 1020, 420),
    },
    "gallery": {
        "floor_y": 415,
        "painting": (450, 110, 830, 390),
        "bench": (520, 430, 760, 500),
    },
    "office": {
        "floor_y": 425,
        "desk": (380, 340, 700, 440),
        "board": (830, 130, 1120, 330),
    },
}


def rect(d, box, fill, outline=None, width=3):
    d.rectangle(box, fill=fill, outline=outline, width=width)


def room(floor_y, wall, floor):
    im = Image.new("RGB", (W, H), wall)
    d = ImageDraw.Draw(im)
    rect(d, (0, floor_y, W, H), floor)
    # Skirting line: the eye reads it as the wall/floor join, which is also the
    # top edge of the walkable polygon.
    d.line((0, floor_y, W, floor_y), fill="#15161f", width=4)
    return im, d


def interior():
    s = SHAPES["interior"]
    im, d = room(s["floor_y"], "#2b2f45", "#4a3b32")
    rect(d, s["window"], "#1d3a63", outline="#8d8f9e")           # night pane
    x0, y0, x1, y1 = s["window"]
    d.line(((x0 + x1) // 2, y0, (x0 + x1) // 2, y1), fill="#8d8f9e", width=4)
    d.line((x0, (y0 + y1) // 2, x1, (y0 + y1) // 2), fill="#8d8f9e", width=4)
    rect(d, s["poster"], "#7a4b52", outline="#c9b28a")           # poster
    rect(d, s["bench"], "#6b563f", outline="#3a2e22")            # bench
    rect(d, s["door"], "#5a4630", outline="#c9b28a")             # door
    d.ellipse((1130, 300, 1144, 314), fill="#e0c274")            # knob
    return im


def yard():
    s = SHAPES["yard"]
    im = Image.new("RGB", (W, H), "#1b2440")
    d = ImageDraw.Draw(im)
    for y in range(s["floor_y"]):                                # sky gradient
        t = y / s["floor_y"]
        d.line((0, y, W, y), fill=(int(27 + 30 * t), int(36 + 25 * t), int(64 + 20 * t)))
    rect(d, (0, s["floor_y"], W, H), "#3a4034")
    d.line((0, s["floor_y"], W, s["floor_y"]), fill="#15161f", width=4)
    rect(d, s["gate"], "#4d4a55", outline="#9aa0b0")             # gate
    rect(d, s["crate"], "#6b563f", outline="#3a2e22")            # crate
    rect(d, s["lamp"], "#4d4a55")                                # lamppost
    d.ellipse((960, 96, 1040, 160), fill="#e0c274")              # lamp glow
    return im


def gallery():
    s = SHAPES["gallery"]
    im, d = room(s["floor_y"], "#33304a", "#43394a")
    x0, y0, x1, y1 = s["painting"]
    rect(d, s["painting"], "#c9a227", outline="#7a5c12", width=6)    # gilt frame
    rect(d, (x0 + 18, y0 + 18, x1 - 18, y1 - 18), "#22304a")         # canvas
    d.ellipse((580, 190, 700, 330), fill="#3f5f7a")                  # a figure on it
    rect(d, s["bench"], "#6b563f", outline="#3a2e22")
    return im


def office():
    s = SHAPES["office"]
    im, d = room(s["floor_y"], "#26323a", "#4a4038")
    rect(d, s["desk"], "#6b563f", outline="#3a2e22")             # desk
    rect(d, s["board"], "#2f4a3a", outline="#c9b28a")            # pin board
    for i in range(3):                                            # pinned notes
        x = 860 + i * 90
        rect(d, (x, 170, x + 60, 230), "#d8d2c0")
    return im


def painting_closeup():
    """The gallery painting, filling the screen: three details to click."""
    im = Image.new("RGB", (FW, FH), "#141018")
    d = ImageDraw.Draw(im)
    rect(d, (120, 60, 1160, 660), "#c9a227", outline="#7a5c12", width=8)   # frame
    rect(d, (170, 110, 1110, 610), "#22304a")                              # canvas
    d.ellipse((520, 220, 760, 500), fill="#3f5f7a")                        # the figure
    d.ellipse((575, 275, 705, 405), fill="#5c7f9e")                        # its face
    d.line((880, 130, 940, 380, 900, 590), fill="#101820", width=7)        # the crack
    d.text((980, 560), "H. 1911", fill="#d8d2c0")                          # the signature
    return im


def title():
    im = Image.new("RGB", (FW, FH), "#12141f")
    d = ImageDraw.Draw(im)
    for y in range(FH):
        t = y / FH
        d.line((0, y, FW, y), fill=(int(18 + 40 * t), int(20 + 30 * t), int(31 + 45 * t)))
    d.line((0, 520, FW, 520), fill="#c9a227", width=3)
    return im


BACKGROUNDS = {
    "interior": interior,
    "yard": yard,
    "gallery": gallery,
    "office": office,
    "painting_closeup": painting_closeup,
    "title": title,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--print-shapes", action="store_true",
                    help="print the interactable rectangles (for the rooms' hotspot polygons)")
    args = ap.parse_args()

    if args.print_shapes:
        for room_id, shapes in SHAPES.items():
            print(f"{room_id}:")
            for name, box in shapes.items():
                print(f"  {name}: {box}")
        return

    OUT.mkdir(parents=True, exist_ok=True)
    for name, make in BACKGROUNDS.items():
        path = OUT / f"{name}.png"
        # Palette PNGs: these are 6-colour flats, so 8-bit costs nothing visually
        # and keeps each file in the low tens of KB.
        make().convert("P", palette=Image.ADAPTIVE, colors=32).save(path, optimize=True)
        print(f"{path.relative_to(OUT.parent.parent.parent)}  {path.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
