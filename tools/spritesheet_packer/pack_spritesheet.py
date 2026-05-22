#!/usr/bin/env python3
from __future__ import annotations

import argparse
import math
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


def detect_boxes(
    image: np.ndarray,
    alpha_threshold: int,
    merge_kernel: int,
    min_area: int,
    row_tolerance: int,
) -> list[Box]:
    alpha = image[:, :, 3]
    mask = (alpha > alpha_threshold).astype(np.uint8) * 255

    if merge_kernel > 1:
        kernel = cv2.getStructuringElement(
            cv2.MORPH_RECT, (merge_kernel, merge_kernel)
        )
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

    num_labels, _, stats, _ = cv2.connectedComponentsWithStats(mask)
    boxes: list[Box] = []
    for label in range(1, num_labels):
        x, y, w, h, area = stats[label]
        if area >= min_area:
            boxes.append(Box(int(x), int(y), int(w), int(h)))

    boxes.sort(key=lambda box: (box.y, box.x))

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
    for sprite in sorted(sprites, key=lambda item: item.id):
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


def quote_yaml(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def quote_yaml_key(value: str) -> str:
    if value.replace("_", "").replace("-", "").isalnum() and value:
        return value
    return quote_yaml(value)


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
    boxes = detect_boxes(
        image=image,
        alpha_threshold=args.alpha_threshold,
        merge_kernel=args.merge_kernel,
        min_area=args.min_area,
        row_tolerance=args.row_tolerance,
    )
    if not boxes:
        raise RuntimeError("No sprites detected")

    sprites = [
        Sprite(f"{args.prefix}_{args.start_index + index:03d}", box)
        for index, box in enumerate(boxes)
    ]
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
