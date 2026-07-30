from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .closeup_data import apply_editable_areas, load_closeup_yaml, save_closeup_yaml
from .server import run_server


def _parse_resolution(text: str) -> tuple[int, int]:
    try:
        w, h = text.lower().split("x")
        return int(w), int(h)
    except Exception as exc:  # noqa: BLE001
        raise argparse.ArgumentTypeError("resolution must look like 1280x720") from exc


def _default_base_path(closeup_path: Path) -> Path:
    # Find the resource root for conventional <data>/closeups/... and
    # <data>/cases/... layouts. This also supports arbitrarily nested close-up
    # directories and leading-slash, resources-root-relative asset paths.
    for parent in closeup_path.parents:
        if parent.name in {"closeups", "cases"}:
            return parent.parent
    return closeup_path.parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Polygon editor for close-ups and case-resolution templates."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    serve = sub.add_parser("serve", help="Start the web-based close-up editor.")
    serve.add_argument("--closeup", help="Close-up YAML to open at startup. Omit to pick in the UI.")
    serve.add_argument(
        "--base-path",
        help="Base directory for the background image and the close-up list. Defaults to the "
        "data directory inferred from the close-up path.",
    )
    serve.add_argument(
        "--resolution",
        type=_parse_resolution,
        default=(1280, 720),
        help="Virtual resolution the hotspot polygons are authored in (default 1280x720).",
    )
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8001)

    edit = sub.add_parser("edit", help="Write an areas map (JSON file) into a supported YAML.")
    edit.add_argument("--closeup", required=True)
    edit.add_argument("--hotspots", required=True, help="JSON file: { id: { name?, area: [...] } }")

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.command == "serve":
        closeup_path = Path(args.closeup) if args.closeup else None
        if args.base_path:
            base_path = Path(args.base_path)
        elif closeup_path:
            base_path = _default_base_path(closeup_path)
        else:
            base_path = Path.cwd()
        width, height = args.resolution
        run_server(closeup_path, base_path, args.host, args.port, width, height)
        return 0

    if args.command == "edit":
        import json

        closeup_path = Path(args.closeup)
        hotspots = json.loads(Path(args.hotspots).read_text(encoding="utf-8"))
        data = load_closeup_yaml(closeup_path)
        apply_editable_areas(data, hotspots)
        save_closeup_yaml(closeup_path, data)
        print(f"Saved editor document {closeup_path}")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
