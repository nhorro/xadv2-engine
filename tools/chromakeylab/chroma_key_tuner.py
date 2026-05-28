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
    p.add_argument(
        "--key",
        nargs=3,
        type=int,
        default=None,
        metavar=("R", "G", "B"),
        help="chroma key RGB; auto-detected from image borders when omitted",
    )
    p.add_argument(
        "--patch",
        type=int,
        default=20,
        help="corner patch size for auto chroma detection (pixels)",
    )
    p.add_argument("--output", type=Path, default=None)
    p.add_argument("--scale", type=float, default=0.75, help="preview scale")
    return p.parse_args()


def estimate_key_color(rgb: np.ndarray, patch: int = 20) -> np.ndarray:
    """Median chroma color sampled from the four corner patches (mirrors the notebook)."""
    h, w = rgb.shape[:2]
    patch = max(1, min(patch, h // 2, w // 2))
    patches = [
        rgb[:patch, :patch],
        rgb[:patch, w - patch:],
        rgb[h - patch:, :patch],
        rgb[h - patch:, w - patch:],
    ]
    samples = np.concatenate([p.reshape(-1, 3) for p in patches], axis=0)
    return np.median(samples, axis=0).astype(np.uint8)


def background_connected_to_border(bg_mask: np.ndarray) -> np.ndarray:
    """Connected components of `bg_mask` that touch a border row or column."""
    num, labels = cv2.connectedComponents(bg_mask.astype(np.uint8), connectivity=4)
    if num <= 1:
        return np.zeros_like(bg_mask, dtype=bool)
    border_labels = set()
    border_labels.update(np.unique(labels[0]).tolist())
    border_labels.update(np.unique(labels[-1]).tolist())
    border_labels.update(np.unique(labels[:, 0]).tolist())
    border_labels.update(np.unique(labels[:, -1]).tolist())
    border_labels.discard(0)
    return np.isin(labels, list(border_labels))


def despill_for_key(
    rgb_linear: np.ndarray,
    alpha: np.ndarray,
    key_rgb: np.ndarray,
    strength: float,
) -> np.ndarray:
    """Chroma-direction-aware despill at the silhouette edge band.

    Channels of `key_rgb` with value >= 50% of the key's max are the
    "dominant" ones; the rest are "suppressed". An edge pixel whose
    dominant-channel minimum exceeds its suppressed-channel maximum carries
    chroma spill in that direction, which we shrink. A near-grey/near-black
    key has no chroma direction and we return the input unchanged.
    """
    out = rgb_linear.copy()
    if strength <= 0.0:
        return out
    edge = (alpha > 0.0) & (alpha < 1.0)
    if not edge.any():
        return out
    key = key_rgb.astype(np.float32) / 255.0
    max_k = float(key.max())
    if max_k < 1e-3:
        return out
    dom_mask = key >= 0.5 * max_k
    dom_chans = [c for c in range(3) if dom_mask[c]]
    sup_chans = [c for c in range(3) if not dom_mask[c]]
    if not dom_chans or not sup_chans:
        return out
    dom_min = rgb_linear[..., dom_chans].min(axis=-1)
    sup_max = rgb_linear[..., sup_chans].max(axis=-1)
    excess = np.maximum(0.0, dom_min - sup_max)
    correction = strength * excess * edge.astype(np.float32)
    for c in dom_chans:
        out[..., c] = np.clip(out[..., c] - correction, 0.0, 1.0)
    for c in sup_chans:
        out[..., c] = np.clip(out[..., c] + 0.25 * correction, 0.0, 1.0)
    return out


def crop_to_content(rgba: np.ndarray, alpha_threshold: int = 5, padding: int = 0) -> np.ndarray:
    """Crop to the bounding box of pixels with alpha > threshold (+ padding)."""
    alpha = rgba[..., 3]
    mask = alpha > alpha_threshold
    if not np.any(mask):
        return rgba
    ys, xs = np.where(mask)
    y0 = max(int(ys.min()) - padding, 0)
    y1 = min(int(ys.max()) + padding + 1, rgba.shape[0])
    x0 = max(int(xs.min()) - padding, 0)
    x1 = min(int(xs.max()) + padding + 1, rgba.shape[1])
    return rgba[y0:y1, x0:x1]


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
    cv2.createTrackbar("Border only", name, 1, 1, lambda _: None)  # preserve interior chroma
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
    border_only: bool,
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
    # Keep only background reachable from the image border, so interior pixels
    # that share the chroma colour stay foreground (notebook §"Keep only
    # background connected to the image border").
    if border_only:
        bg = background_connected_to_border(bg)
    bg_clean = morph_mask(bg, open_k, close_k, dilate_k, erode_k)
    fg_clean = ~bg_clean

    dist_inside = cv2.distanceTransform(fg_clean.astype(np.uint8), cv2.DIST_L2, 3)
    feather_px = max(feather_px, 0.1)
    alpha = np.clip(dist_inside / feather_px, 0, 1)
    alpha[bg_clean] = 0.0

    rgb = img.astype(np.float32) / 255.0
    rgb2 = despill_for_key(rgb, alpha, key_rgb, despill_strength)

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
    if args.key is None:
        key_rgb = estimate_key_color(img, patch=args.patch)
        print(f"auto chroma RGB: {tuple(int(x) for x in key_rgb)}")
    else:
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
        border_only = cv2.getTrackbarPos("Border only", controls) > 0
        view = cv2.getTrackbarPos("View", controls)

        rgba, debug = make_output(
            img, key_rgb, rgb_thresh, hue_tol, min_sat, min_val,
            open_k, close_k, dilate_k, erode_k, feather_px, despill, border_only
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
            cropped = crop_to_content(latest_rgba)
            cv2.imwrite(str(output), cv2.cvtColor(cropped, cv2.COLOR_RGBA2BGRA))
            h0, w0 = latest_rgba.shape[:2]
            h1, w1 = cropped.shape[:2]
            print(f"Saved {output}  ({w0}x{h0} -> {w1}x{h1})")

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
