from __future__ import annotations

import json
import mimetypes
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from .room_data import apply_room_patch, list_assets, load_room_yaml, save_room_yaml

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8000


class RoomEditorHandler(BaseHTTPRequestHandler):
    server_version = "RoomEditorHTTP/1.0"
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/room":
            self.handle_api_room()
            return
        if parsed.path == "/api/info":
            self.handle_api_info()
            return
        if parsed.path == "/api/assets":
            self.handle_api_assets(parsed.query)
            return
        if parsed.path == "/api/save_asset":
            self.send_json({"ok": False, "error": "Use POST for /api/save_asset"}, status=405)
            return
        if parsed.path.startswith("/assets/"):
            self.serve_asset(parsed.path)
            return
        if parsed.path.startswith("/api/"):
            self.send_json({"ok": False, "error": f"Unknown endpoint: {parsed.path}"}, status=404)
            return
        self.serve_static(parsed.path)

    def _read_body(self) -> bytes:
        # Always drain the request body so a persistent (keep-alive) connection
        # never desyncs, even when the route is unknown.
        length = int(self.headers.get("Content-Length", "0") or 0)
        return self.rfile.read(length) if length > 0 else b""

    def do_POST(self) -> None:
        body = self._read_body()
        if self.path == "/api/save":
            self.handle_api_save(body)
            return
        if self.path == "/api/save_asset":
            self.handle_api_save_asset(body)
            return
        # Answer API routes with JSON so the client never has to parse an HTML
        # error page (which surfaces as "Unexpected token '<'" on res.json()).
        self.send_json({"ok": False, "error": f"Unknown endpoint: {self.path}"}, status=404)

    def handle_api_room(self) -> None:
        room = load_room_yaml(self.server.room_path)
        self.send_json(room)

    def handle_api_info(self) -> None:
        self.send_json(
            {
                "room_path": str(self.server.room_path),
                "base_path": str(self.server.base_path),
                "host": self.server.server_address[0],
                "port": self.server.server_address[1],
            }
        )

    def handle_api_assets(self, query: str) -> None:
        params = urllib.parse.parse_qs(query)
        prefix = params.get("prefix", [None])[0]
        assets = list_assets(self.server.base_path, prefix)
        self.send_json({"assets": assets})

    def handle_api_save(self, body: bytes) -> None:
        try:
            payload = json.loads(body.decode("utf-8"))
            patch = payload.get("patch")
            if not isinstance(patch, dict):
                raise ValueError("payload.patch must be a mapping")
            room = load_room_yaml(self.server.room_path)
            apply_room_patch(room, patch)
            save_room_yaml(self.server.room_path, room)
            self.send_json({"ok": True})
        except Exception as exc:
            self.send_json({"ok": False, "error": str(exc)}, status=400)

    def serve_asset(self, path: str) -> None:
        asset_name = urllib.parse.unquote(path[len("/assets/"):])
        asset_path = (self.server.base_path / asset_name).resolve()
        try:
            base = self.server.base_path.resolve()
            if not str(asset_path).startswith(str(base)):
                raise FileNotFoundError
            if not asset_path.exists() or not asset_path.is_file():
                raise FileNotFoundError
        except FileNotFoundError:
            self.send_error(404, "Asset not found")
            return

        content_type, _ = mimetypes.guess_type(asset_path.name)
        content_type = content_type or "application/octet-stream"
        with asset_path.open("rb") as handle:
            body = handle.read()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def serve_static(self, path: str) -> None:
        if path in ["", "/"]:
            path = "/index.html"
        static_path = self.server.static_dir.joinpath(path.lstrip("/"))
        try:
            static_path = static_path.resolve()
            if not str(static_path).startswith(str(self.server.static_dir.resolve())):
                raise FileNotFoundError
            if not static_path.exists() or not static_path.is_file():
                raise FileNotFoundError
        except FileNotFoundError:
            self.send_error(404, "File not found")
            return

        content_type, _ = mimetypes.guess_type(static_path.name)
        content_type = content_type or "application/octet-stream"
        with static_path.open("rb") as handle:
            body = handle.read()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, payload: Any, status: int = 200) -> None:
        body = json.dumps(payload, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: object) -> None:
        print(f"[room_editor] {self.address_string()} {format % args}")

    def handle_api_save_asset(self, body: bytes) -> None:
        # Accept JSON: { filename: str, data: base64 }
        import base64

        try:
            payload = json.loads(body.decode("utf-8"))
            filename = payload.get("filename")
            data = payload.get("data")
            if not filename or not isinstance(filename, str):
                raise ValueError("filename must be provided")
            if not data or not isinstance(data, str):
                raise ValueError("data must be base64 string")

            # prevent path escaping
            asset_path = (self.server.base_path / filename).resolve()
            base = self.server.base_path.resolve()
            if not str(asset_path).startswith(str(base)):
                raise FileNotFoundError("Invalid filename")

            asset_path.parent.mkdir(parents=True, exist_ok=True)
            blob = base64.b64decode(data)
            with asset_path.open("wb") as handle:
                handle.write(blob)

            self.send_json({"ok": True, "path": str(asset_path.relative_to(base))})
        except Exception as exc:
            self.send_json({"ok": False, "error": str(exc)}, status=400)


class RoomEditorServer(ThreadingHTTPServer):
    def __init__(
        self,
        server_address: tuple[str, int],
        RequestHandlerClass,
        room_path: Path,
        base_path: Path,
        static_dir: Path,
    ) -> None:
        super().__init__(server_address, RequestHandlerClass)
        self.room_path = room_path
        self.base_path = base_path
        self.static_dir = static_dir


def run_server(room_path: Path, base_path: Path, host: str, port: int) -> None:
    static_dir = Path(__file__).resolve().parent / "static"
    server = RoomEditorServer((host, port), RoomEditorHandler, room_path, base_path, static_dir)
    print(f"Serving room editor at http://{host}:{port}/")
    print(f"Editing room file: {room_path}")
    print(f"Asset base path: {base_path}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping room editor.")
        server.server_close()
