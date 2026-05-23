#!/usr/bin/env python3
"""Merge multiple packed atlas images+YAML into a single atlas PNG+YAML.

Usage examples:
  merge_atlases.py \
    --atlas data/avatar/body.png data/avatar/atlas_body.yml --namespace body \
    --atlas data/avatar/head.png data/avatar/atlas_head.yml --namespace head \
    --out-image data/avatar/merged.png --out-yaml data/avatar/atlas_merged.yml

This script purposely avoids external YAML dependencies and parses the
simple atlas YAML format produced by `pack_spritesheet.py`.
"""
from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path
import re
import sys

import cv2
import numpy as np


@dataclass(frozen=True)
class Box:
    x: int
    y: int
    w: int
    h: int


@dataclass
class Sprite:
    id: str
    source_rect: Box
    pixels: np.ndarray
    anchors: dict[str, tuple[int, int]] | None = None
    packed_rect: Box | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Merge multiple atlas PNG+YAML pairs")
    parser.add_argument(
        "--atlas",
        nargs=2,
        action="append",
        metavar=("IMAGE", "YAML"),
        required=True,
        help="Pair: <packed-image.png> <atlas.yml>. Repeat for each atlas to merge.",
    )
    parser.add_argument(
        "--namespace",
        action="append",
        default=[],
        help="Optional namespace to prefix sprite ids for the corresponding --atlas pair",
    )
    parser.add_argument("--out-image", type=Path, required=True, help="Merged output PNG")
    parser.add_argument("--out-yaml", type=Path, required=True, help="Merged output YAML")
    parser.add_argument("--max-width", type=int, default=1024, help="Max atlas width")
    parser.add_argument("--padding", type=int, default=2, help="Padding between sprites")
    return parser.parse_args()


def unquote_yaml(value: str) -> str:
    v = value.strip()
    if v.startswith('"') and v.endswith('"'):
        v = v[1:-1]
        v = v.replace('\\"', '"').replace('\\\\', '\\')
    return v


def parse_atlas_yaml(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    sprites: list[dict] = []
    cur: dict | None = None
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.lstrip()
        if stripped.startswith("- id:"):
            if cur:
                sprites.append(cur)
            m = re.match(r"- id:\s*(.*)", stripped)
            cur = {"id": unquote_yaml(m.group(1).strip()) if m else ""}
            i += 1
            continue
        if cur is not None:
            # rect, source_rect, anchors
            if stripped.startswith("rect:"):
                rect = {}
                key_indent = len(line) - len(stripped)
                i += 1
                while i < len(lines):
                    next_line = lines[i]
                    indent = len(next_line) - len(next_line.lstrip(" "))
                    if indent <= key_indent or next_line.lstrip().startswith("- id:"):
                        break
                    line2 = next_line.lstrip()
                    m = re.match(r"(\w+):\s*(\d+)", line2)
                    if m:
                        rect[m.group(1)] = int(m.group(2))
                    i += 1
                cur["rect"] = rect
                continue
            if stripped.startswith("source_rect:"):
                srect = {}
                key_indent = len(line) - len(stripped)
                i += 1
                while i < len(lines):
                    next_line = lines[i]
                    indent = len(next_line) - len(next_line.lstrip(" "))
                    if indent <= key_indent or next_line.lstrip().startswith("- id:"):
                        break
                    line2 = next_line.lstrip()
                    m = re.match(r"(\w+):\s*(\d+)", line2)
                    if m:
                        srect[m.group(1)] = int(m.group(2))
                    i += 1
                cur["source_rect"] = srect
                continue
            if stripped.startswith("anchors:"):
                anchors: dict[str, dict[str, int]] = {}
                key_indent = len(line) - len(stripped)
                i += 1
                while i < len(lines):
                    next_line = lines[i]
                    indent = len(next_line) - len(next_line.lstrip(" "))
                    if indent <= key_indent or next_line.lstrip().startswith("- id:"):
                        break
                    line2 = next_line.lstrip()
                    m = re.match(r"([\w\-\_\"][^:]*):\s*$", line2)
                    if m:
                        name = m.group(1).strip()
                        name = unquote_yaml(name)
                        coords: dict[str, int] = {}
                        child_indent = len(next_line) - len(line2)
                        i += 1
                        while i < len(lines):
                            child_line = lines[i]
                            child_line_indent = len(child_line) - len(child_line.lstrip(" "))
                            if child_line_indent <= child_indent or child_line.lstrip().startswith("- id:"):
                                break
                            line3 = child_line.lstrip()
                            m2 = re.match(r"(x|y):\s*(\d+)", line3)
                            if m2:
                                coords[m2.group(1)] = int(m2.group(2))
                            i += 1
                        anchors[name] = coords
                        continue
                    i += 1
                cur["anchors"] = {k: (v.get("x", 0), v.get("y", 0)) for k, v in anchors.items()}
                continue
        i += 1
    if cur:
        sprites.append(cur)
    return sprites


def next_power_of_two(value: int) -> int:
    if value <= 1:
        return 1
    return 2 ** math.ceil(math.log2(value))


def pack_sprites(sprites: list[Sprite], max_width: int, padding: int) -> tuple[int, int]:
    if not sprites:
        return 1, 1
    pack_order = sorted(sprites, key=lambda s: (s.pixels.shape[0], s.pixels.shape[1]), reverse=True)
    x = padding
    y = padding
    shelf_h = 0
    used_w = 0
    for sprite in pack_order:
        h, w = sprite.pixels.shape[0], sprite.pixels.shape[1]
        if w + padding * 2 > max_width:
            raise RuntimeError("Sprite is too wide for max-width")
        if x + w + padding > max_width and x > padding:
            x = padding
            y += shelf_h + padding
            shelf_h = 0
        sprite.packed_rect = Box(x, y, w, h)
        x += w + padding
        shelf_h = max(shelf_h, h)
        used_w = max(used_w, x)
    atlas_w = min(max_width, next_power_of_two(max(used_w + padding, 1)))
    atlas_h = next_power_of_two(y + shelf_h + padding)
    return atlas_w, atlas_h


def quote_yaml(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def quote_yaml_key(value: str) -> str:
    if value.replace("_", "").replace("-", "").isalnum() and value:
        return value
    return quote_yaml(value)


def render_yaml(image_path: str, atlas_size: tuple[int, int], sprites: list[Sprite]) -> str:
    atlas_w, atlas_h = atlas_size
    lines = [f"image: {quote_yaml(image_path)}", "size:", f"  width: {atlas_w}", f"  height: {atlas_h}", "sprites:"]
    for sprite in sorted(sprites, key=lambda s: s.id):
        if sprite.packed_rect is None:
            raise RuntimeError(f"Sprite not packed: {sprite.id}")
        rect = sprite.packed_rect
        src = sprite.source_rect
        lines.extend([
            f"  - id: {quote_yaml(sprite.id)}",
            "    rect:",
            f"      x: {rect.x}",
            f"      y: {rect.y}",
            f"      width: {rect.w}",
            f"      height: {rect.h}",
            "    source_rect:",
            f"      x: {src.x}",
            f"      y: {src.y}",
            f"      width: {src.w}",
            f"      height: {src.h}",
        ])
        if sprite.anchors:
            lines.append("    anchors:")
            for name, (ax, ay) in sprite.anchors.items():
                lines.extend([f"      {quote_yaml_key(name)}:", f"        x: {ax}", f"        y: {ay}"])
    return "\n".join(lines) + "\n"


def main() -> int:
    args = parse_args()
    namespaces = args.namespace or []
    if namespaces and len(namespaces) != len(args.atlas):
        print("Number of --namespace entries must match number of --atlas pairs", file=sys.stderr)
        return 2

    sprites: list[Sprite] = []
    for idx, (image_path, yaml_path) in enumerate(args.atlas):
        imgp = Path(image_path)
        ymlp = Path(yaml_path)
        if not imgp.exists() or not ymlp.exists():
            print(f"Missing file: {imgp} or {ymlp}", file=sys.stderr)
            return 3
        image = cv2.imread(str(imgp), cv2.IMREAD_UNCHANGED)
        if image is None:
            print(f"Could not read image: {imgp}", file=sys.stderr)
            return 4
        entries = parse_atlas_yaml(ymlp)
        ns = namespaces[idx] if idx < len(namespaces) else None
        for ent in entries:
            sid = ent.get("id", "")
            if ns:
                sid = f"{ns}_{sid}"
            srect = ent.get("source_rect") or ent.get("rect")
            src_box = Box(int(srect.get("x", 0)), int(srect.get("y", 0)), int(srect.get("width", 0)), int(srect.get("height", 0)))
            # Extract the pixel region from the packed image using rect (packed rect)
            packed = ent.get("rect")
            if not packed:
                print(f"Atlas entry missing rect for sprite {sid}", file=sys.stderr)
                return 5
            px = int(packed.get("x", 0))
            py = int(packed.get("y", 0))
            pw = int(packed.get("width", 0))
            ph = int(packed.get("height", 0))
            pixels = image[py : py + ph, px : px + pw].copy()
            anchors = ent.get("anchors")
            sprites.append(Sprite(sid, src_box, pixels, anchors))

    atlas_size = pack_sprites(sprites, args.max_width, args.padding)
    atlas_w, atlas_h = atlas_size
    atlas = np.zeros((atlas_h, atlas_w, 4), dtype=np.uint8)
    for s in sprites:
        if s.packed_rect is None:
            raise RuntimeError(f"Sprite not packed: {s.id}")
        dst = s.packed_rect
        h, w = s.pixels.shape[0], s.pixels.shape[1]
        atlas[dst.y : dst.y + h, dst.x : dst.x + w] = s.pixels

    args.out_image.parent.mkdir(parents=True, exist_ok=True)
    args.out_yaml.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(args.out_image), atlas)
    yaml_text = render_yaml(args.out_image.name, atlas_size, sprites)
    args.out_yaml.write_text(yaml_text, encoding="utf-8")
    print(f"Wrote: {args.out_image} ({atlas_w}x{atlas_h})")
    print(f"YAML: {args.out_yaml}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
