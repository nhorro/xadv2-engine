from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None


def require_yaml() -> None:
    if yaml is None:
        raise RuntimeError(
            "PyYAML is required to run room_editor. Install it with `pip install pyyaml`."
        )


def load_room_yaml(path: Path) -> Dict[str, Any]:
    require_yaml()
    with path.open("r", encoding="utf-8") as handle:
        content = yaml.safe_load(handle)
    return content if isinstance(content, dict) else {}


_ROOM_DUMPER = None


def _is_point(value: Any) -> bool:
    return isinstance(value, dict) and set(value.keys()) == {"x", "y"}


def _room_dumper():
    # Built lazily (and cached) because PyYAML may be absent at import time. The
    # customisation keeps the point-heavy data compact: `{x, y}` mappings and any
    # list made entirely of them (a polygon) are emitted inline (flow style), so a
    # whole polygon reads as `walkable: [{x: 0, y: 0}, ...]` on one line instead of
    # three lines per vertex. Everything else stays block style.
    global _ROOM_DUMPER
    if _ROOM_DUMPER is None:

        class RoomDumper(yaml.SafeDumper):
            pass

        def represent_dict(dumper, data):
            return dumper.represent_mapping(
                "tag:yaml.org,2002:map", data, flow_style=_is_point(data)
            )

        def represent_list(dumper, data):
            polygon = bool(data) and all(_is_point(item) for item in data)
            return dumper.represent_sequence("tag:yaml.org,2002:seq", data, flow_style=polygon)

        RoomDumper.add_representer(dict, represent_dict)
        RoomDumper.add_representer(list, represent_list)
        _ROOM_DUMPER = RoomDumper
    return _ROOM_DUMPER


def save_room_yaml(path: Path, room: Dict[str, Any]) -> None:
    require_yaml()
    with path.open("w", encoding="utf-8") as handle:
        # width=inf keeps each polygon on a single line (PyYAML would otherwise wrap
        # a long flow sequence at ~80 columns).
        yaml.dump(
            room,
            handle,
            Dumper=_room_dumper(),
            sort_keys=False,
            allow_unicode=True,
            width=float("inf"),
        )


def load_patch(path: Path) -> Dict[str, Any]:
    require_yaml()
    with path.open("r", encoding="utf-8") as handle:
        patch = yaml.safe_load(handle)
    if patch is None:
        return {}
    if not isinstance(patch, dict):
        raise ValueError("Patch file must contain a mapping at the top level.")
    return patch


def deep_merge_dict(destination: Dict[str, Any], source: Dict[str, Any]) -> None:
    for key, value in source.items():
        if (
            isinstance(value, dict)
            and isinstance(destination.get(key), dict)
        ):
            deep_merge_dict(destination[key], value)
        else:
            destination[key] = value


def apply_room_patch(room: Dict[str, Any], patch: Dict[str, Any]) -> Dict[str, Any]:
    if not isinstance(room, dict):
        raise ValueError("Room data must be a mapping.")
    if not isinstance(patch, dict):
        raise ValueError("Patch data must be a mapping.")

    if "background" in patch:
        background = room.setdefault("background", {})
        if not isinstance(background, dict):
            background = {}
            room["background"] = background
        if not isinstance(patch["background"], dict):
            raise ValueError("background patch must be a mapping.")
        deep_merge_dict(background, patch["background"])

    geometry = patch.get("geometry")
    if geometry is not None:
        if not isinstance(geometry, dict):
            raise ValueError("geometry patch must be a mapping.")
        if "walkable" in geometry:
            room["walkable"] = geometry["walkable"]
        if "obstacles" in geometry:
            room["obstacles"] = geometry["obstacles"]
        if "points" in geometry:
            room["points"] = geometry["points"]
        if "zones" in geometry:
            room["zones"] = geometry["zones"]
        if "regions" in geometry:
            room["regions"] = geometry["regions"]
        if "hotspots" in geometry:
            room["hotspots"] = geometry["hotspots"]
        if "objects" in geometry:
            room["objects"] = geometry["objects"]

    return room


def collect_asset_paths(room: Dict[str, Any]) -> List[str]:
    paths: List[str] = []

    background = room.get("background")
    if isinstance(background, dict):
        for layer in background.get("layers", []) or []:
            if isinstance(layer, dict) and isinstance(layer.get("image"), str):
                paths.append(layer["image"])

    regions = room.get("regions")
    if isinstance(regions, dict):
        for region in regions.values():
            if isinstance(region, dict):
                states = region.get("states")
                if isinstance(states, dict):
                    for value in states.values():
                        if isinstance(value, str):
                            paths.append(value)

    objects = room.get("objects")
    if isinstance(objects, dict):
        for obj in objects.values():
            if isinstance(obj, dict) and isinstance(obj.get("sprite"), str):
                paths.append(obj["sprite"])

    return paths


def find_missing_assets(room: Dict[str, Any], base_path: Path) -> List[str]:
    missing = []
    for logical_path in collect_asset_paths(room):
        resolved = base_path / logical_path
        if not resolved.exists():
            missing.append(logical_path)
    return missing


def resolve_within(base_path: Path, name: str) -> Path:
    """Resolve ``name`` against ``base_path`` and confirm it stays inside it.

    A plain ``str.startswith`` check is not enough: it lets a sibling like
    ``rooms_other`` past a ``rooms`` base, and ``..`` segments can climb out. This
    compares resolved path components instead. Raises ``ValueError`` on escape.
    """
    base = base_path.resolve()
    target = (base_path / name).resolve()
    try:
        target.relative_to(base)
    except ValueError as exc:
        raise ValueError(f"Path escapes base directory: {name}") from exc
    return target


def list_rooms(base_path: Path) -> List[str]:
    """Room YAML files directly under ``base_path`` (the rooms/asset folder).

    A file counts as a room if it parses to a mapping carrying ``background`` or
    ``walkable`` — enough to skip the manifest/cast YAMLs that may sit alongside.
    Returns bare filenames so the UI can open them relative to ``base_path``.
    """
    if not base_path.exists() or not base_path.is_dir():
        return []
    candidates = sorted(
        p for p in base_path.iterdir() if p.is_file() and p.suffix in {".yaml", ".yml"}
    )
    results: List[str] = []
    for candidate in candidates:
        try:
            content = load_room_yaml(candidate)
        except Exception:
            continue
        if isinstance(content, dict) and ("background" in content or "walkable" in content):
            results.append(candidate.name)
    return results


def list_assets(
    data_path: Path,
    prefix: Optional[str] = None,
    relative_to: Optional[Path] = None,
) -> List[str]:
    """List data-tree assets using paths valid from the active room directory."""

    if not data_path.exists() or not data_path.is_dir():
        return []
    data_root = data_path.resolve()
    prefix_path = (data_path / prefix).resolve() if prefix else None
    if prefix_path:
        try:
            prefix_path.relative_to(data_root)
        except ValueError:
            return []
    reference = (relative_to or data_path).resolve()
    results: List[str] = []
    for candidate in sorted(data_path.rglob("*")):
        if not candidate.is_file():
            continue
        resolved = candidate.resolve()
        try:
            resolved.relative_to(data_root)
        except ValueError:
            continue
        if prefix_path:
            try:
                resolved.relative_to(prefix_path)
            except ValueError:
                continue
        results.append(os.path.relpath(resolved, reference).replace("\\", "/"))
    return results


def resolve_asset_within(data_path: Path, room_dir: Path, logical: str) -> Path:
    """Resolve a room-relative asset while preventing escape from game data."""

    data_root = data_path.resolve()
    target = (room_dir / logical).resolve()
    try:
        target.relative_to(data_root)
    except ValueError as exc:
        raise ValueError(f"Asset escapes data directory: {logical}") from exc
    return target


def find_cast_file(room_path: Optional[Path], base_path: Path) -> Optional[Path]:
    """Find the nearest cast file above the room folder.

    Room images are normally relative to ``data/rooms`` while cast resources are
    relative to ``data``. This also supports the deprecated room-directory launch
    form by discovering the surrounding data root.
    """

    starts = []
    if room_path:
        starts.append(room_path.parent.resolve())
    starts.append(base_path.resolve())
    visited = set()
    for start in starts:
        for directory in (start, *start.parents):
            if directory in visited:
                continue
            visited.add(directory)
            for name in ("cast.yaml", "cast.yml"):
                candidate = directory / name
                if candidate.is_file():
                    return candidate
    return None


def _resolve_project_asset(asset_root: Path, parent: Path, logical: str) -> Tuple[Path, str]:
    target = (parent / logical).resolve()
    try:
        relative = target.relative_to(asset_root.resolve())
    except ValueError as exc:
        raise ValueError(f"Asset escapes cast data root: {logical}") from exc
    return target, relative.as_posix()


def _default_preview_sequence(sequences: Dict[str, Any]) -> Optional[str]:
    for name in ("stand_down", "stand", "idle_down", "idle"):
        if name in sequences:
            return name
    return next(iter(sequences), None)


def load_avatar_catalog(cast_path: Path) -> Dict[str, Any]:
    """Resolve cast characters to first-frame browser preview metadata.

    The result contains no image bytes. It describes the atlas crop, pivot and
    sequence mirror flag; the HTTP server serves the referenced atlas from the
    cast file's directory (the game's data root).
    """

    asset_root = cast_path.parent.resolve()
    cast = load_room_yaml(cast_path)
    appearances = cast.get("appearances") or {}
    characters = cast.get("characters") or {}
    result: List[Dict[str, Any]] = []
    errors: List[str] = []

    if not isinstance(appearances, dict) or not isinstance(characters, dict):
        return {
            "cast_path": str(cast_path),
            "asset_root": str(asset_root),
            "characters": [],
            "errors": ["cast appearances and characters must be mappings"],
        }

    for character_id, character in characters.items():
        if not isinstance(character, dict):
            continue
        appearance_id = character.get("appearance")
        appearance = appearances.get(appearance_id)
        if not isinstance(appearance, dict) or appearance.get("type") != "animated_sprite":
            continue
        animation_logical = appearance.get("sprite")
        if not isinstance(animation_logical, str) or not animation_logical:
            continue

        try:
            animation_path, _ = _resolve_project_asset(
                asset_root, asset_root, animation_logical
            )
            animation = load_room_yaml(animation_path)
            sheet_logical = animation.get("spritesheet")
            if not isinstance(sheet_logical, str) or not sheet_logical:
                raise ValueError("animation has no spritesheet")
            sheet_path, _ = _resolve_project_asset(
                asset_root, animation_path.parent, sheet_logical
            )
            sheet = load_room_yaml(sheet_path)
            image_logical = sheet.get("image")
            if not isinstance(image_logical, str) or not image_logical:
                raise ValueError("spritesheet has no image")
            image_path, image_asset = _resolve_project_asset(
                asset_root, sheet_path.parent, image_logical
            )
            if not image_path.is_file():
                raise FileNotFoundError(f"atlas image not found: {image_asset}")

            frames_by_id = {
                frame.get("id"): frame
                for frame in (sheet.get("sprites") or [])
                if isinstance(frame, dict) and isinstance(frame.get("id"), str)
            }
            raw_sequences = animation.get("sequences") or {}
            if not isinstance(raw_sequences, dict):
                raise ValueError("animation sequences must be a mapping")
            pivot_name = animation.get("pivot")
            sequence_previews: Dict[str, Any] = {}
            for sequence_name, sequence in raw_sequences.items():
                if not isinstance(sequence, dict):
                    continue
                refs = sequence.get("frames") or []
                if not isinstance(refs, list) or not refs or not isinstance(refs[0], dict):
                    continue
                frame_id = refs[0].get("sprite")
                frame = frames_by_id.get(frame_id)
                if not isinstance(frame, dict) or not isinstance(frame.get("rect"), dict):
                    continue
                rect = frame["rect"]
                anchors = frame.get("anchors") or {}
                pivot = anchors.get(pivot_name, {"x": 0, "y": 0})
                sequence_previews[str(sequence_name)] = {
                    "frame": str(frame_id),
                    "rect": {
                        "x": int(rect.get("x", 0)),
                        "y": int(rect.get("y", 0)),
                        "width": int(rect.get("width", 0)),
                        "height": int(rect.get("height", 0)),
                    },
                    "pivot": {
                        "x": float(pivot.get("x", 0)),
                        "y": float(pivot.get("y", 0)),
                    },
                    "h_mirror": bool(sequence.get("h_mirror", False)),
                }
            default_sequence = _default_preview_sequence(sequence_previews)
            if not default_sequence:
                raise ValueError("animation has no previewable sequence frames")

            result.append(
                {
                    "id": str(character_id),
                    "name": str(character.get("name") or character_id),
                    "appearance": str(appearance_id),
                    "image": image_asset,
                    "default_sequence": default_sequence,
                    "sequences": sequence_previews,
                }
            )
        except Exception as exc:
            errors.append(f"{character_id}: {exc}")

    return {
        "cast_path": str(cast_path),
        "asset_root": str(asset_root),
        "characters": result,
        "errors": errors,
    }
