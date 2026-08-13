from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List, Optional

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None


def require_yaml() -> None:
    if yaml is None:
        raise RuntimeError(
            "PyYAML is required to run closeup_editor. Install it with `pip install pyyaml`."
        )


def load_closeup_yaml(path: Path) -> Dict[str, Any]:
    require_yaml()
    with path.open("r", encoding="utf-8") as handle:
        content = yaml.safe_load(handle)
    return content if isinstance(content, dict) else {}


_DUMPER = None


def _is_point(value: Any) -> bool:
    return isinstance(value, dict) and set(value.keys()) == {"x", "y"}


def _is_inline_map(value: Any) -> bool:
    # Keep the small scalar maps compact: `{x, y}` points and `{r, g, b[, a]}`
    # colours render inline so a hand-authored file stays close to its original
    # shape after a round-trip.
    if not isinstance(value, dict):
        return False
    keys = set(value.keys())
    return keys == {"x", "y"} or keys in ({"r", "g", "b"}, {"r", "g", "b", "a"})


def _dumper():
    # Same compact style as the room editor: small scalar maps render inline and any
    # list made entirely of `{x, y}` points (a polygon) is emitted inline, so a
    # hotspot `area` reads on one line. Built lazily because PyYAML may be absent at
    # import time.
    global _DUMPER
    if _DUMPER is None:

        class CloseUpDumper(yaml.SafeDumper):
            pass

        def represent_dict(dumper, data):
            return dumper.represent_mapping(
                "tag:yaml.org,2002:map", data, flow_style=_is_inline_map(data)
            )

        def represent_list(dumper, data):
            polygon = bool(data) and all(_is_point(item) for item in data)
            scalar_list = bool(data) and all(
                isinstance(item, (str, int, float, bool)) or item is None for item in data
            )
            return dumper.represent_sequence(
                "tag:yaml.org,2002:seq", data, flow_style=polygon or scalar_list
            )

        CloseUpDumper.add_representer(dict, represent_dict)
        CloseUpDumper.add_representer(list, represent_list)
        _DUMPER = CloseUpDumper
    return _DUMPER


def save_closeup_yaml(path: Path, data: Dict[str, Any]) -> None:
    require_yaml()
    with path.open("w", encoding="utf-8") as handle:
        # width=inf keeps each polygon on a single line.
        yaml.dump(
            data,
            handle,
            Dumper=_dumper(),
            sort_keys=False,
            allow_unicode=True,
            width=float("inf"),
        )


def _round_point(p: Any) -> Dict[str, int]:
    if not isinstance(p, dict) or "x" not in p or "y" not in p:
        raise ValueError("polygon vertex must be a {x, y} mapping")
    return {"x": int(round(float(p["x"]))), "y": int(round(float(p["y"])))}


def normalize_hotspots(hotspots: Any) -> Dict[str, Any]:
    """Validate + canonicalize an incoming hotspots map (id -> {name?, area}).

    Coordinates are rounded to ints (close-up space is whole pixels). Raises
    ``ValueError`` on a malformed shape so the client gets a clear error.
    """
    if not isinstance(hotspots, dict):
        raise ValueError("hotspots must be a mapping of id -> hotspot")
    result: Dict[str, Any] = {}
    for hid, spec in hotspots.items():
        if not isinstance(hid, str) or not hid:
            raise ValueError("hotspot id must be a non-empty string")
        if not isinstance(spec, dict):
            raise ValueError(f"hotspot '{hid}' must be a mapping")
        area = spec.get("area")
        if not isinstance(area, list) or len(area) < 3:
            raise ValueError(f"hotspot '{hid}': area must be a polygon of >= 3 points")
        entry: Dict[str, Any] = {}
        name = spec.get("name")
        if isinstance(name, str) and name:
            entry["name"] = name
        entry["area"] = [_round_point(p) for p in area]
        result[hid] = entry
    return result


def apply_hotspots(data: Dict[str, Any], hotspots: Any) -> Dict[str, Any]:
    """Replace the close-up's `hotspots` map with a normalized one."""
    if not isinstance(data, dict):
        raise ValueError("close-up data must be a mapping")
    data["hotspots"] = normalize_hotspots(hotspots)
    return data


def document_kind(data: Dict[str, Any]) -> str:
    """Return the polygon collection used by a supported editor document."""
    return "template" if isinstance(data.get("slots"), dict) else "closeup"


def editable_areas(data: Dict[str, Any]) -> Dict[str, Any]:
    """Return the collection exposed to the editor UI.

    Close-ups expose their full hotspot entries because ``name`` is editable.
    Case-resolution templates expose geometry-only slot entries; semantic fields
    such as ``accepts`` and ``solution`` are deliberately kept server-side.
    """
    key = "slots" if document_kind(data) == "template" else "hotspots"
    collection = data.get(key, {})
    if not isinstance(collection, dict):
        return {}
    result: Dict[str, Any] = {}
    for item_id, spec in collection.items():
        if not isinstance(item_id, str) or not isinstance(spec, dict):
            continue
        entry: Dict[str, Any] = {"area": spec.get("area")}
        if key == "hotspots" and isinstance(spec.get("name"), str):
            entry["name"] = spec["name"]
        result[item_id] = entry
    return result


def apply_editable_areas(data: Dict[str, Any], areas: Any) -> Dict[str, Any]:
    """Apply editor geometry to a close-up or case-resolution template.

    Template slot ids and membership are fixed in the geometry editor. This
    prevents a polygon edit from silently dropping the slot's ``accepts`` and
    ``solution`` fields or creating an unusable slot with no semantics.
    """
    if document_kind(data) == "closeup":
        return apply_hotspots(data, areas)
    if not isinstance(areas, dict):
        raise ValueError("slots must be a mapping of id -> slot")

    slots = data.get("slots")
    if not isinstance(slots, dict):
        raise ValueError("template slots must be a mapping")
    expected = set(slots)
    received = set(areas)
    if received != expected:
        missing = sorted(expected - received)
        extra = sorted(received - expected)
        details = []
        if missing:
            details.append(f"missing: {', '.join(missing)}")
        if extra:
            details.append(f"unknown: {', '.join(extra)}")
        raise ValueError(
            "template slot ids cannot be added or removed (" + "; ".join(details) + ")"
        )

    normalized = normalize_hotspots(areas)
    for slot_id, slot in slots.items():
        if not isinstance(slot, dict):
            raise ValueError(f"slot '{slot_id}' must be a mapping")
        slot["area"] = normalized[slot_id]["area"]
    return data


def background_asset_name(data: Dict[str, Any], path: Path, base_path: Path) -> Optional[str]:
    """Return the background path expected by the ``/assets`` endpoint.

    Like the engine, relative paths resolve beside the YAML. A leading slash is
    resources-root-relative and therefore resolves below ``base_path``.
    """
    background = data.get("background")
    if not isinstance(background, str) or not background:
        return None
    candidate = (
        base_path / background.lstrip("/")
        if background.startswith("/")
        else path.parent / background
    )
    try:
        return str(candidate.resolve().relative_to(base_path.resolve())).replace("\\", "/")
    except ValueError:
        return background


def resolve_within(base_path: Path, name: str) -> Path:
    """Resolve ``name`` under ``base_path`` and confirm it stays inside it.

    Mirrors the room editor: compares resolved components so siblings and ``..``
    cannot escape. Raises ``ValueError`` on escape.
    """
    base = base_path.resolve()
    target = (base_path / name).resolve()
    try:
        target.relative_to(base)
    except ValueError as exc:
        raise ValueError(f"Path escapes base directory: {name}") from exc
    return target


def list_closeups(base_path: Path) -> List[str]:
    """Supported YAML documents anywhere under ``base_path``.

    A mapping with a string ``background`` is a close-up or case template for the
    purposes of this editor. Version-2 manifests let the owning scene supply the
    id, so the data file need not repeat it.
    """
    if not base_path.exists() or not base_path.is_dir():
        return []
    results: List[str] = []
    for candidate in sorted(base_path.rglob("*")):
        if not candidate.is_file() or candidate.suffix not in {".yaml", ".yml"}:
            continue
        try:
            content = load_closeup_yaml(candidate)
        except Exception:
            continue
        if isinstance(content, dict) and isinstance(content.get("background"), str):
            results.append(str(candidate.relative_to(base_path)).replace("\\", "/"))
    return results
