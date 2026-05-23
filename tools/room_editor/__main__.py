from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .room_data import apply_room_patch, find_missing_assets, load_patch, load_room_yaml, save_room_yaml
from .server import run_server


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Room YAML editor for point-and-click rooms."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    edit_parser = subparsers.add_parser("edit", help="Apply a JSON/YAML patch to a room YAML file.")
    edit_parser.add_argument("--room", required=True, help="Path to the room YAML file.")
    edit_parser.add_argument("--patch", required=True, help="Path to a YAML/JSON patch file.")
    edit_parser.add_argument("--base-path", help="Optional base directory for asset validity checks.")

    serve_parser = subparsers.add_parser("serve", help="Start the web-based room editor in a browser.")
    serve_parser.add_argument("--room", help="Optional room YAML to open at startup. Omit to pick one in the UI.")
    serve_parser.add_argument("--base-path", help="Base directory for logical asset paths and the room list. Defaults to the room YAML directory, or the current directory when --room is omitted.")
    serve_parser.add_argument("--host", default="127.0.0.1", help="Host to bind the web server to.")
    serve_parser.add_argument("--port", type=int, default=8000, help="Port to bind the web server to.")

    gui_parser = subparsers.add_parser("gui", help="Start the web-based room editor in a browser. Alias for serve.")
    gui_parser.add_argument("--room", help="Optional room YAML to open at startup. Omit to pick one in the UI.")
    gui_parser.add_argument("--base-path", help="Base directory for logical asset paths and the room list. Defaults to the room YAML directory, or the current directory when --room is omitted.")
    gui_parser.add_argument("--host", default="127.0.0.1", help="Host to bind the web server to.")
    gui_parser.add_argument("--port", type=int, default=8000, help="Port to bind the web server to.")

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.command in {"serve", "gui"}:
        room_path = Path(args.room) if args.room else None
        if args.base_path:
            base_path = Path(args.base_path)
        elif room_path:
            base_path = room_path.parent
        else:
            base_path = Path.cwd()
        run_server(room_path=room_path, base_path=base_path, host=args.host, port=args.port)
        return 0

    if args.command == "edit":
        room_path = Path(args.room)
        patch_path = Path(args.patch)
        patch = load_patch(patch_path)
        room = load_room_yaml(room_path)
        apply_room_patch(room, patch)
        save_room_yaml(room_path, room)
        print(f"Saved room {room_path}")
        base_path = Path(args.base_path) if args.base_path else room_path.parent
        missing = find_missing_assets(room, base_path)
        if missing:
            print("[WARN] Missing asset files:")
            for relative in missing:
                print(f"  - {relative}")
        return 0

    return 1


if __name__ == "__main__":
    sys.exit(main())
