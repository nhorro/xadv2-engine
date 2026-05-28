#!/usr/bin/env python3
"""
Interactive chroma-key tuner for generated 2D game-art layers.

Usage:
    python chroma_key_tuner.py input.png --key 255 0 255 --output extracted.png

Controls:
    - Trackbars adjust thresholds and cleanup.
    - Press S to save RGBA output.
    - Press Q or ESC to quit.

Requires:
    pip install opencv-python numpy
"""

from __future__ import annotations

import argparse
from pathlib import Path
import cv2
import numpy as np


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("image", type=Path)
    p.add_argument("--key", nargs=3, type=int, default=[255, 0, 255],
                   metavar=("R", "G", "B"), help="chroma key RGB color")
    p.add_argument("--output", type=Path, default=None)
    p.add_argument("--scale", type=float, default=0.75, help="preview scale")
    return p.parse_args()


def imread_rgb(path: Path) -> np.ndarray:
    bgr = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if bgr is None:
        raise FileNotFoundError(path)
    return cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)


def create_trackbar_window(name: str) -> None:
    cv2.namedWindow(name, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(name, 500, 420)
    cv2.createTrackbar("RGB thresh", name, 45, 255, lambda _: None)
    cv2.createTrackbar("Hue tol", name, 12, 90, lambda _: None)
    cv2.createTrackbar("Min sat", name, 80, 255, lambda _: None)
    cv2.createTrackbar("Min val", name, 80, 255, lambda _: None)
    cv2.createTrackbar("Open k", name, 0, 15, lambda _: None)
    cv2.createTrackbar("Close k", name, 0, 15, lambda _: None)
    cv2.createTrackbar("Dilate k", name, 1, 15, lambda _: None)
    cv2.createTrackbar("Erode k", name, 0, 15, lambda _: None)
    cv2.createTrackbar("Feather x10", name, 20, 100, lambda _: None)
    cv2.createTrackbar("Despill x100", name, 70, 100, lambda _: None)
    cv2.createTrackbar("View", name, 0, 5, lambda _: None)


def odd_or_zero(v: int) -> int:
    if v <= 0:
        return 0
    return v if v % 2 == 1 else v + 1


def morph_mask(mask: np.ndarray, open_k: int, close_k: int, dilate_k: int, erode_k: int) -> np.ndarray:
    m = mask.astype(np.uint8) * 255
    for op_name, ksize in [("open", open_k), ("close", close_k), ("dilate", dilate_k), ("erode", erode_k)]:
        ksize = odd_or_zero(ksize)
        if ksize <= 0:
            continue
        k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (ksize, ksize))
        if op_name == "open":
            m = cv2.morphologyEx(m, cv2.MORPH_OPEN, k)
        elif op_name == "close":
            m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, k)
        elif op_name == "dilate":
            m = cv2.dilate(m, k)
        elif op_name == "erode":
            m = cv2.erode(m, k)
    return m > 0


def composite_over(rgba: np.ndarray, bg_rgb: tuple[int, int, int]) -> np.ndarray:
    a = rgba[..., 3:4].astype(np.float32) / 255.0
    fg = rgba[..., :3].astype(np.float32)
    bg = np.zeros_like(fg) + np.array(bg_rgb, dtype=np.float32)
    return (fg * a + bg * (1 - a)).astype(np.uint8)


def make_output(
    img: np.ndarray,
    key_rgb: np.ndarray,
    rgb_thresh: int,
    hue_tol: int,
    min_sat: int,
    min_val: int,
    open_k: int,
    close_k: int,
    dilate_k: int,
    erode_k: int,
    feather_px: float,
    despill_strength: float,
) -> tuple[np.ndarray, dict[str, np.ndarray]]:
    img_f = img.astype(np.float32)
    key_f = key_rgb.astype(np.float32)
    rgb_dist = np.linalg.norm(img_f - key_f[None, None, :], axis=2)
    bg_rgb = rgb_dist < rgb_thresh

    hsv = cv2.cvtColor(img, cv2.COLOR_RGB2HSV)
    key_hsv = cv2.cvtColor(np.uint8([[key_rgb]]), cv2.COLOR_RGB2HSV)[0, 0]
    H, S, V = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    key_h = int(key_hsv[0])
    hue_delta = np.abs(H.astype(np.int16) - key_h)
    hue_dist = np.minimum(hue_delta, 180 - hue_delta)
    bg_hsv = (hue_dist <= hue_tol) & (S >= min_sat) & (V >= min_val)

    bg = bg_rgb | bg_hsv
    bg_clean = morph_mask(bg, open_k, close_k, dilate_k, erode_k)
    fg_clean = ~bg_clean

    dist_inside = cv2.distanceTransform(fg_clean.astype(np.uint8), cv2.DIST_L2, 3)
    feather_px = max(feather_px, 0.1)
    alpha = np.clip(dist_inside / feather_px, 0, 1)
    alpha[bg_clean] = 0.0

    rgb = img.astype(np.float32) / 255.0
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    magenta_excess = np.maximum(0, np.minimum(r, b) - g)
    edge = (alpha > 0.0) & (alpha < 1.0)
    correction = despill_strength * magenta_excess * edge

    rgb2 = rgb.copy()
    rgb2[..., 0] = np.clip(rgb2[..., 0] - correction, 0, 1)
    rgb2[..., 2] = np.clip(rgb2[..., 2] - correction, 0, 1)
    rgb2[..., 1] = np.clip(rgb2[..., 1] + correction * 0.25, 0, 1)

    rgba = np.dstack([(rgb2 * 255).astype(np.uint8), (alpha * 255).astype(np.uint8)])
    debug = {
        "rgb_dist": np.clip(rgb_dist, 0, 255).astype(np.uint8),
        "bg_rgb": (bg_rgb.astype(np.uint8) * 255),
        "bg_hsv": (bg_hsv.astype(np.uint8) * 255),
        "bg_clean": (bg_clean.astype(np.uint8) * 255),
        "alpha": (alpha * 255).astype(np.uint8),
    }
    return rgba, debug


def resize_preview(img: np.ndarray, scale: float) -> np.ndarray:
    if scale == 1.0:
        return img
    h, w = img.shape[:2]
    return cv2.resize(img, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)


def main() -> None:
    args = parse_args()
    img = imread_rgb(args.image)
    key_rgb = np.array(args.key, dtype=np.uint8)
    output = args.output or args.image.with_name(args.image.stem + "_extracted.png")

    controls = "controls"
    create_trackbar_window(controls)
    cv2.namedWindow("preview", cv2.WINDOW_NORMAL)

    latest_rgba = None

    while True:
        rgb_thresh = cv2.getTrackbarPos("RGB thresh", controls)
        hue_tol = cv2.getTrackbarPos("Hue tol", controls)
        min_sat = cv2.getTrackbarPos("Min sat", controls)
        min_val = cv2.getTrackbarPos("Min val", controls)
        open_k = cv2.getTrackbarPos("Open k", controls)
        close_k = cv2.getTrackbarPos("Close k", controls)
        dilate_k = cv2.getTrackbarPos("Dilate k", controls)
        erode_k = cv2.getTrackbarPos("Erode k", controls)
        feather_px = cv2.getTrackbarPos("Feather x10", controls) / 10.0
        despill = cv2.getTrackbarPos("Despill x100", controls) / 100.0
        view = cv2.getTrackbarPos("View", controls)

        rgba, debug = make_output(
            img, key_rgb, rgb_thresh, hue_tol, min_sat, min_val,
            open_k, close_k, dilate_k, erode_k, feather_px, despill
        )
        latest_rgba = rgba

        if view == 0:
            shown = composite_over(rgba, (40, 40, 40))
            title = "composite over dark"
        elif view == 1:
            shown = composite_over(rgba, (210, 210, 210))
            title = "composite over light"
        elif view == 2:
            shown = cv2.cvtColor(debug["bg_clean"], cv2.COLOR_GRAY2RGB)
            title = "clean background mask"
        elif view == 3:
            shown = cv2.cvtColor(debug["alpha"], cv2.COLOR_GRAY2RGB)
            title = "alpha"
        elif view == 4:
            shown = cv2.cvtColor(debug["bg_rgb"], cv2.COLOR_GRAY2RGB)
            title = "RGB background mask"
        else:
            shown = cv2.cvtColor(debug["bg_hsv"], cv2.COLOR_GRAY2RGB)
            title = "HSV background mask"

        shown_bgr = cv2.cvtColor(resize_preview(shown, args.scale), cv2.COLOR_RGB2BGR)
        cv2.imshow("preview", shown_bgr)
        cv2.setWindowTitle("preview", f"preview - {title}")

        k = cv2.waitKey(30) & 0xFF
        if k in (27, ord("q"), ord("Q")):
            break
        if k in (ord("s"), ord("S")) and latest_rgba is not None:
            cv2.imwrite(str(output), cv2.cvtColor(latest_rgba, cv2.COLOR_RGBA2BGRA))
            print(f"Saved {output}")

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
