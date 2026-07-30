from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .room_data import (
    apply_room_patch,
    find_cast_file,
    find_missing_assets,
    load_patch,
    load_room_yaml,
    save_room_yaml,
)
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
    serve_parser.add_argument(
        "--room",
        help="Optional room YAML or filename to open at startup. Omit to pick one in the UI.",
    )
    serve_paths = serve_parser.add_mutually_exclusive_group()
    serve_paths.add_argument(
        "--data-path",
        help="Game data directory. Rooms default to <data-path>/rooms.",
    )
    serve_paths.add_argument(
        "--base-path",
        help="Deprecated compatibility form: room directory used by older commands.",
    )
    serve_parser.add_argument(
        "--rooms-dir",
        default="rooms",
        help="Room directory, relative to --data-path unless absolute (default: rooms).",
    )
    serve_parser.add_argument("--host", default="127.0.0.1", help="Host to bind the web server to.")
    serve_parser.add_argument("--port", type=int, default=8000, help="Port to bind the web server to.")

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.command == "serve":
        raw_room = Path(args.room) if args.room else None
        if args.data_path:
            data_path = Path(args.data_path).resolve()
            requested_rooms = Path(args.rooms_dir)
            rooms_path = (
                requested_rooms.resolve()
                if requested_rooms.is_absolute()
                else (data_path / requested_rooms).resolve()
            )
            if raw_room:
                room_path = (
                    raw_room.resolve()
                    if raw_room.is_absolute() or raw_room.exists()
                    else (rooms_path / raw_room).resolve()
                )
            else:
                room_path = None
        elif args.base_path:
            rooms_path = Path(args.base_path).resolve()
            cast_path = find_cast_file(None, rooms_path)
            data_path = (
                cast_path.parent.resolve()
                if cast_path
                else (rooms_path.parent if rooms_path.name == "rooms" else rooms_path)
            )
            room_path = (
                raw_room.resolve()
                if raw_room and (raw_room.is_absolute() or raw_room.exists())
                else ((rooms_path / raw_room).resolve() if raw_room else None)
            )
        elif raw_room:
            room_path = raw_room.resolve()
            rooms_path = room_path.parent
            cast_path = find_cast_file(room_path, rooms_path)
            data_path = (
                cast_path.parent.resolve()
                if cast_path
                else (rooms_path.parent if rooms_path.name == "rooms" else rooms_path)
            )
        else:
            data_path = Path.cwd().resolve()
            rooms_path = (
                (data_path / args.rooms_dir).resolve()
                if (data_path / args.rooms_dir).is_dir()
                else data_path
            )
            room_path = None

        if not data_path.is_dir():
            raise SystemExit(f"room_editor: data directory not found: {data_path}")
        if not rooms_path.is_dir():
            raise SystemExit(f"room_editor: rooms directory not found: {rooms_path}")
        if room_path and not room_path.is_file():
            raise SystemExit(f"room_editor: room file not found: {room_path}")

        try:
            rooms_path.relative_to(data_path)
            if room_path:
                room_path.relative_to(rooms_path)
        except ValueError:
            raise SystemExit(
                "room_editor: rooms and room paths must stay inside --data-path"
            )

        run_server(
            room_path=room_path,
            rooms_path=rooms_path,
            data_path=data_path,
            host=args.host,
            port=args.port,
        )
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
