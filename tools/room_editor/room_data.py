from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

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


def list_assets(base_path: Path, prefix: Optional[str] = None) -> List[str]:
    if not base_path.exists() or not base_path.is_dir():
        return []
    prefix_path = Path(prefix) if prefix else None
    results: List[str] = []
    for candidate in sorted(base_path.rglob("*")):
        if not candidate.is_file():
            continue
        if prefix_path:
            try:
                candidate.relative_to(base_path / prefix_path)
            except ValueError:
                continue
        results.append(str(candidate.relative_to(base_path)).replace("\\", "/"))
    return results
