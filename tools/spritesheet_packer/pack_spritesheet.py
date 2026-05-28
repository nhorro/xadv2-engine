#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
import re
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np


@dataclass(frozen=True)
class Box:
    x: int
    y: int
    w: int
    h: int

    @property
    def area(self) -> int:
        return self.w * self.h


@dataclass
class Sprite:
    id: str
    source_rect: Box
    packed_rect: Box | None = None
    anchors: dict[str, tuple[int, int]] | None = None


@dataclass(frozen=True)
class DetectedBlob:
    """One connected component picked up by `extract_blobs`. `kept` is False when
    the blob fell below `--min-area` and was excluded from the packed output —
    visualizing it in the debug image is what makes lost sprites obvious."""

    box: Box
    area: int
    kept: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract visible sprites from a transparent source image, pack them "
            "into a compact atlas, and write YAML rect metadata."
        )
    )
    parser.add_argument("input", type=Path, help="Transparent source spritesheet")
    parser.add_argument("--out-image", type=Path, required=True, help="Output packed PNG")
    parser.add_argument("--out-yaml", type=Path, required=True, help="Output atlas YAML")
    parser.add_argument(
        "--frames-dir",
        type=Path,
        default=None,
        help="Optional directory for individual cropped frame PNGs",
    )
    parser.add_argument(
        "--prefix",
        default="frame",
        help="Sprite id prefix. IDs are '<prefix>_000', '<prefix>_001', ...",
    )
    parser.add_argument(
        "--start-index",
        type=int,
        default=0,
        help="Starting numeric index for generated sprite IDs",
    )
    parser.add_argument(
        "--names-file",
        type=Path,
        default=None,
        help=(
            "Optional text file with sprite IDs in source traversal order. "
            "Whitespace-separated names are accepted; blank lines and text after # are ignored. "
            "When provided, the number of names must match the number of detected sprites."
        ),
    )
    parser.add_argument(
        "--max-width",
        type=int,
        default=1024,
        help="Maximum atlas width before starting a new shelf",
    )
    parser.add_argument(
        "--padding",
        type=int,
        default=2,
        help="Transparent pixels between packed sprites",
    )
    parser.add_argument(
        "--alpha-threshold",
        type=int,
        default=10,
        help="Pixels with alpha above this value are considered visible",
    )
    parser.add_argument(
        "--merge-kernel",
        type=int,
        default=7,
        help="Morphological close kernel size for joining parts of one sprite",
    )
    parser.add_argument(
        "--row-tolerance",
        type=int,
        default=40,
        help="Y tolerance used to sort source sprites into approximate rows",
    )
    parser.add_argument(
        "--min-area",
        type=int,
        default=200,
        help="Ignore connected components smaller than this pixel area",
    )
    parser.add_argument(
        "--yaml-image-path",
        default=None,
        help="Image path to store inside YAML. Defaults to --out-image name.",
    )
    parser.add_argument(
        "--debug-image",
        type=Path,
        default=None,
        help=(
            "Optional PNG showing what the packer sees: the source sheet with "
            "kept detection rects (green, numbered), dropped components below "
            "--min-area (red), and zones where --merge-kernel bridged separate "
            "components (orange tint). Useful when the detected count doesn't "
            "match the expected count."
        ),
    )
    parser.add_argument(
        "--add-anchor",
        nargs=3,
        action="append",
        metavar=("NAME", "X_FACTOR", "Y_FACTOR"),
        default=[],
        help=(
            "Add an anchor to every sprite using relative coordinates. "
            "Example: --add-anchor walking_pivot 0.5 1.0"
        ),
    )
    return parser.parse_args()


def load_image(path: Path) -> np.ndarray:
    image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if image is None:
        raise RuntimeError(f"Could not read image: {path}")
    if len(image.shape) != 3 or image.shape[2] != 4:
        raise RuntimeError("Input image must have an alpha channel")
    return image


def extract_blobs(
    image: np.ndarray,
    alpha_threshold: int,
    merge_kernel: int,
    min_area: int,
) -> tuple[np.ndarray, np.ndarray, list[DetectedBlob]]:
    """Run the threshold + close + connectedComponents pipeline and report ALL
    components — both kept and discarded — alongside the pre- and post-close
    masks. Splitting it out from `detect_boxes` is what lets the debug image
    show why a sprite count came out wrong."""
    alpha = image[:, :, 3]
    mask_pre = (alpha > alpha_threshold).astype(np.uint8) * 255
    mask_post = mask_pre
    if merge_kernel > 1:
        kernel = cv2.getStructuringElement(
            cv2.MORPH_RECT, (merge_kernel, merge_kernel)
        )
        mask_post = cv2.morphologyEx(mask_pre, cv2.MORPH_CLOSE, kernel)

    num_labels, _, stats, _ = cv2.connectedComponentsWithStats(mask_post)
    blobs: list[DetectedBlob] = []
    for label in range(1, num_labels):
        x, y, w, h, area = stats[label]
        blobs.append(
            DetectedBlob(
                box=Box(int(x), int(y), int(w), int(h)),
                area=int(area),
                kept=int(area) >= min_area,
            )
        )
    return mask_pre, mask_post, blobs


def sort_boxes_in_traversal_order(boxes: list[Box], row_tolerance: int) -> list[Box]:
    """Top-to-bottom, then left-to-right within each row (with a per-row Y
    tolerance so a slight vertical wobble doesn't break the sort)."""
    boxes = sorted(boxes, key=lambda box: (box.y, box.x))
    rows: list[list[Box]] = []
    for box in boxes:
        for row in rows:
            avg_y = sum(item.y for item in row) / len(row)
            if abs(box.y - avg_y) < row_tolerance:
                row.append(box)
                break
        else:
            rows.append([box])
    for row in rows:
        row.sort(key=lambda box: box.x)
    rows.sort(key=lambda row: sum(box.y for box in row) / len(row))
    return [box for row in rows for box in row]


def detect_boxes(
    image: np.ndarray,
    alpha_threshold: int,
    merge_kernel: int,
    min_area: int,
    row_tolerance: int,
) -> list[Box]:
    _, _, blobs = extract_blobs(image, alpha_threshold, merge_kernel, min_area)
    kept = [b.box for b in blobs if b.kept]
    return sort_boxes_in_traversal_order(kept, row_tolerance)


def next_power_of_two(value: int) -> int:
    if value <= 1:
        return 1
    return 2 ** math.ceil(math.log2(value))


def pack_sprites(sprites: list[Sprite], max_width: int, padding: int) -> tuple[int, int]:
    if not sprites:
        return 1, 1
    if max_width <= padding * 2:
        raise RuntimeError("--max-width must be greater than twice --padding")

    pack_order = sorted(
        sprites,
        key=lambda sprite: (sprite.source_rect.h, sprite.source_rect.w),
        reverse=True,
    )

    x = padding
    y = padding
    shelf_h = 0
    used_w = 0

    for sprite in pack_order:
        box = sprite.source_rect
        if box.w + padding * 2 > max_width:
            raise RuntimeError(
                f"Sprite {sprite.id} is {box.w}px wide and does not fit in "
                f"--max-width {max_width} with {padding}px padding"
            )
        if x + box.w + padding > max_width and x > padding:
            x = padding
            y += shelf_h + padding
            shelf_h = 0

        sprite.packed_rect = Box(x, y, box.w, box.h)
        x += box.w + padding
        shelf_h = max(shelf_h, box.h)
        used_w = max(used_w, x)

    atlas_w = min(max_width, next_power_of_two(max(used_w + padding, 1)))
    atlas_h = next_power_of_two(y + shelf_h + padding)
    return atlas_w, atlas_h


def write_outputs(
    image: np.ndarray,
    sprites: list[Sprite],
    atlas_size: tuple[int, int],
    out_image: Path,
    out_yaml: Path,
    frames_dir: Path | None,
    yaml_image_path: str,
) -> None:
    atlas_w, atlas_h = atlas_size
    atlas = np.zeros((atlas_h, atlas_w, 4), dtype=np.uint8)

    if frames_dir:
        frames_dir.mkdir(parents=True, exist_ok=True)

    for sprite in sprites:
        if sprite.packed_rect is None:
            raise RuntimeError(f"Sprite was not packed: {sprite.id}")
        src = sprite.source_rect
        dst = sprite.packed_rect
        pixels = image[src.y : src.y + src.h, src.x : src.x + src.w]
        atlas[dst.y : dst.y + dst.h, dst.x : dst.x + dst.w] = pixels
        if frames_dir:
            cv2.imwrite(str(frames_dir / f"{sprite.id}.png"), pixels)

    out_image.parent.mkdir(parents=True, exist_ok=True)
    out_yaml.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out_image), atlas)
    out_yaml.write_text(render_yaml(yaml_image_path, atlas_size, sprites), encoding="utf-8")


def render_yaml(
    image_path: str, atlas_size: tuple[int, int], sprites: list[Sprite]
) -> str:
    atlas_w, atlas_h = atlas_size
    lines = [
        f"image: {quote_yaml(image_path)}",
        "size:",
        f"  width: {atlas_w}",
        f"  height: {atlas_h}",
        "sprites:",
    ]
    for sprite in sprites:
        if sprite.packed_rect is None:
            raise RuntimeError(f"Sprite was not packed: {sprite.id}")
        rect = sprite.packed_rect
        source = sprite.source_rect
        lines.extend(
            [
                f"  - id: {quote_yaml(sprite.id)}",
                "    rect:",
                f"      x: {rect.x}",
                f"      y: {rect.y}",
                f"      width: {rect.w}",
                f"      height: {rect.h}",
                "    source_rect:",
                f"      x: {source.x}",
                f"      y: {source.y}",
                f"      width: {source.w}",
                f"      height: {source.h}",
            ]
        )
        if sprite.anchors:
            lines.append("    anchors:")
            for name, point in sprite.anchors.items():
                x, y = point
                lines.extend(
                    [
                        f"      {quote_yaml_key(name)}:",
                        f"        x: {x}",
                        f"        y: {y}",
                    ]
                )
    return "\n".join(lines) + "\n"


def render_debug_image(
    image: np.ndarray,
    mask_pre: np.ndarray,
    mask_post: np.ndarray,
    blobs: list[DetectedBlob],
    sorted_boxes: list[Box],
    alpha_threshold: int,
    merge_kernel: int,
    min_area: int,
    row_tolerance: int,
) -> np.ndarray:
    """One PNG that explains what the packer saw.

    Failure modes the colours map to:
      * **green numbered rects** — kept components, in traversal order. A rect
        spanning two visibly-separate sprites means the close kernel bridged
        them (lower `--merge-kernel` or remove the bridging artifact).
      * **red rects** — components below `--min-area`. A red rect over a sprite
        you expected to detect means `--min-area` is too high (or
        `--alpha-threshold` chopped its tail off).
      * **orange tint** — pixels added by the morphology close
        (`mask_post & ~mask_pre`). When an orange filament joins two sprites,
        that's the smoking gun for the "two-detected-as-one" failure mode.
    """
    h, w = image.shape[:2]

    # Composite RGBA over a light-grey background so transparent regions are
    # visible, then dim slightly so overlay strokes pop.
    bgr = image[..., :3].astype(np.float32)
    a = image[..., 3:4].astype(np.float32) / 255.0
    bg = np.full_like(bgr, 220.0)  # near-white grey, BGR=GRY
    out_f = bgr * a + bg * (1.0 - a)
    out_f = out_f * 0.7 + 255.0 * 0.3
    out = np.clip(out_f, 0, 255).astype(np.uint8)

    # Tint pixels added by the close (where two components were potentially
    # bridged). Vivid orange in BGR.
    added = (mask_post > 0) & (mask_pre == 0)
    if added.any():
        tint = np.array([40, 130, 240], dtype=np.float32)  # BGR orange
        out_f = out.astype(np.float32)
        out_f[added] = 0.6 * out_f[added] + 0.4 * tint
        out = np.clip(out_f, 0, 255).astype(np.uint8)

    # Box → traversal index lookup (kept boxes only).
    index_of = {(b.x, b.y, b.w, b.h): i for i, b in enumerate(sorted_boxes)}

    # Render dropped first so kept rects + labels sit on top.
    for blob in blobs:
        if blob.kept:
            continue
        b = blob.box
        cv2.rectangle(out, (b.x, b.y), (b.x + b.w, b.y + b.h), (60, 60, 220), 1)
        label = f"dropped a={blob.area}"
        cv2.putText(
            out,
            label,
            (b.x + 2, b.y + b.h - 4),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.35,
            (60, 60, 220),
            1,
            cv2.LINE_AA,
        )

    for blob in blobs:
        if not blob.kept:
            continue
        b = blob.box
        cv2.rectangle(out, (b.x, b.y), (b.x + b.w, b.y + b.h), (0, 180, 0), 2)
        idx = index_of.get((b.x, b.y, b.w, b.h), -1)
        label = f"#{idx} a={blob.area}" if idx >= 0 else f"a={blob.area}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.4, 1)
        tag_y0 = max(0, b.y - th - 6)
        cv2.rectangle(
            out,
            (b.x, tag_y0),
            (b.x + tw + 6, tag_y0 + th + 6),
            (0, 180, 0),
            -1,
        )
        cv2.putText(
            out,
            label,
            (b.x + 3, tag_y0 + th + 2),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.4,
            (0, 0, 0),
            1,
            cv2.LINE_AA,
        )

    # Legend strip across the top.
    kept_count = sum(1 for b in blobs if b.kept)
    dropped_count = sum(1 for b in blobs if not b.kept)
    legend_lines = [
        f"kept N={kept_count}   dropped N={dropped_count}   "
        f"alpha_threshold={alpha_threshold}   merge_kernel={merge_kernel}   "
        f"min_area={min_area}   row_tolerance={row_tolerance}",
        "green=kept(#idx, area)   red=dropped(<min_area)   orange=bridged_by_close",
    ]
    line_h = 18
    legend_h = line_h * len(legend_lines) + 8
    legend = np.full((legend_h, w, 3), 245, dtype=np.uint8)
    for i, text in enumerate(legend_lines):
        cv2.putText(
            legend,
            text,
            (10, line_h * (i + 1) - 4),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.45,
            (40, 40, 40),
            1,
            cv2.LINE_AA,
        )
    return np.concatenate([legend, out], axis=0)


def quote_yaml(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def quote_yaml_key(value: str) -> str:
    if value.replace("_", "").replace("-", "").isalnum() and value:
        return value
    return quote_yaml(value)



def parse_names_file(path: Path) -> list[str]:
    """Read sprite IDs from a text file.

    Format:
      - Names are whitespace-separated.
      - Blank lines are ignored.
      - Text after '#' is treated as a comment.

    Example:
        sf wf0 wf1 wf2 wf3
        sr wr0 wr1 wr2 wr3
    """
    names: list[str] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        for name in line.split():
            if not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
                raise RuntimeError(
                    f"Invalid sprite id {name!r} at {path}:{line_number}. "
                    "Use only letters, numbers, underscore, dash, or dot."
                )
            names.append(name)

    duplicated = sorted({name for name in names if names.count(name) > 1})
    if duplicated:
        raise RuntimeError(
            "Duplicated sprite ids in --names-file: " + ", ".join(duplicated)
        )
    return names


def make_sprites(
    boxes: list[Box], prefix: str, start_index: int, names_file: Path | None
) -> list[Sprite]:
    if names_file is None:
        return [
            Sprite(f"{prefix}_{start_index + index:03d}", box)
            for index, box in enumerate(boxes)
        ]

    names = parse_names_file(names_file)
    if len(names) != len(boxes):
        raise RuntimeError(
            f"--names-file contains {len(names)} names, but {len(boxes)} sprites were detected. "
            "The names file must contain exactly one name per detected sprite, in traversal order."
        )
    return [Sprite(name, box) for name, box in zip(names, boxes)]


def parse_anchor_specs(specs: list[list[str]]) -> list[tuple[str, float, float]]:
    anchors: list[tuple[str, float, float]] = []
    for name, x_text, y_text in specs:
        try:
            x_factor = float(x_text)
            y_factor = float(y_text)
        except ValueError as exc:
            raise RuntimeError(
                f"Anchor {name} factors must be numeric, got {x_text!r} {y_text!r}"
            ) from exc
        if not 0.0 <= x_factor <= 1.0 or not 0.0 <= y_factor <= 1.0:
            raise RuntimeError(
                f"Anchor {name} factors must be between 0.0 and 1.0"
            )
        anchors.append((name, x_factor, y_factor))
    return anchors


def apply_anchors(
    sprites: list[Sprite], anchor_specs: list[tuple[str, float, float]]
) -> None:
    if not anchor_specs:
        return
    for sprite in sprites:
        anchors: dict[str, tuple[int, int]] = {}
        for name, x_factor, y_factor in anchor_specs:
            anchors[name] = (
                round(sprite.source_rect.w * x_factor),
                round(sprite.source_rect.h * y_factor),
            )
        sprite.anchors = anchors


def main() -> int:
    args = parse_args()
    anchor_specs = parse_anchor_specs(args.add_anchor)
    image = load_image(args.input)

    # Run extraction first so the debug image (when requested) can show every
    # component the packer considered, kept or not — even if the kept list ends
    # up empty.
    mask_pre, mask_post, blobs = extract_blobs(
        image=image,
        alpha_threshold=args.alpha_threshold,
        merge_kernel=args.merge_kernel,
        min_area=args.min_area,
    )
    kept_boxes = [b.box for b in blobs if b.kept]
    boxes = sort_boxes_in_traversal_order(kept_boxes, args.row_tolerance)

    if args.debug_image is not None:
        debug = render_debug_image(
            image=image,
            mask_pre=mask_pre,
            mask_post=mask_post,
            blobs=blobs,
            sorted_boxes=boxes,
            alpha_threshold=args.alpha_threshold,
            merge_kernel=args.merge_kernel,
            min_area=args.min_area,
            row_tolerance=args.row_tolerance,
        )
        args.debug_image.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(args.debug_image), debug)
        print(f"Debug image: {args.debug_image}")

    if not boxes:
        raise RuntimeError("No sprites detected")

    sprites = make_sprites(
        boxes=boxes,
        prefix=args.prefix,
        start_index=args.start_index,
        names_file=args.names_file,
    )
    apply_anchors(sprites, anchor_specs)
    atlas_size = pack_sprites(sprites, args.max_width, args.padding)
    yaml_image_path = args.yaml_image_path or args.out_image.name
    write_outputs(
        image=image,
        sprites=sprites,
        atlas_size=atlas_size,
        out_image=args.out_image,
        out_yaml=args.out_yaml,
        frames_dir=args.frames_dir,
        yaml_image_path=yaml_image_path,
    )

    print(f"Detected sprites: {len(sprites)}")
    print(f"Atlas size: {atlas_size[0]}x{atlas_size[1]}")
    print(f"Image: {args.out_image}")
    print(f"YAML: {args.out_yaml}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())