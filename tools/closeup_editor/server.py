from __future__ import annotations

import json
import mimetypes
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional

from .closeup_data import (
    apply_hotspots,
    list_closeups,
    load_closeup_yaml,
    resolve_within,
    save_closeup_yaml,
)

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8001


class CloseUpEditorHandler(BaseHTTPRequestHandler):
    server_version = "CloseUpEditorHTTP/1.0"
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/api/closeup":
            self.handle_api_closeup()
            return
        if parsed.path == "/api/info":
            self.handle_api_info()
            return
        if parsed.path == "/api/closeups":
            self.handle_api_closeups()
            return
        if parsed.path.startswith("/assets/"):
            self.serve_asset(parsed.path)
            return
        if parsed.path.startswith("/api/"):
            self.send_json({"ok": False, "error": f"Unknown endpoint: {parsed.path}"}, status=404)
            return
        self.serve_static(parsed.path)

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0") or 0)
        return self.rfile.read(length) if length > 0 else b""

    def do_POST(self) -> None:
        body = self._read_body()
        if self.path == "/api/save":
            self.handle_api_save(body)
            return
        if self.path == "/api/open":
            self.handle_api_open(body)
            return
        self.send_json({"ok": False, "error": f"Unknown endpoint: {self.path}"}, status=404)

    def handle_api_closeup(self) -> None:
        if not self.server.closeup_path:
            self.send_json({})
            return
        self.send_json(load_closeup_yaml(self.server.closeup_path))

    def handle_api_info(self) -> None:
        self.send_json(
            {
                "closeup_path": str(self.server.closeup_path) if self.server.closeup_path else None,
                "closeup": self.server.closeup_path.name if self.server.closeup_path else None,
                "base_path": str(self.server.base_path),
                "resolution": {"width": self.server.width, "height": self.server.height},
                "host": self.server.server_address[0],
                "port": self.server.server_address[1],
            }
        )

    def handle_api_closeups(self) -> None:
        items = list_closeups(self.server.base_path)
        current = None
        if self.server.closeup_path:
            try:
                current = str(
                    self.server.closeup_path.resolve().relative_to(self.server.base_path.resolve())
                ).replace("\\", "/")
            except ValueError:
                current = self.server.closeup_path.name
        self.send_json({"closeups": items, "current": current})

    def handle_api_open(self, body: bytes) -> None:
        try:
            payload = json.loads(body.decode("utf-8"))
            name = payload.get("closeup")
            if not name or not isinstance(name, str):
                raise ValueError("closeup name must be provided")
            target = resolve_within(self.server.base_path, name)
            if not target.exists() or not target.is_file():
                raise FileNotFoundError(f"Close-up file not found: {name}")
            self.server.closeup_path = target
            self.send_json({"ok": True, "closeup": target.name})
        except Exception as exc:
            self.send_json({"ok": False, "error": str(exc)}, status=400)

    def handle_api_save(self, body: bytes) -> None:
        try:
            if not self.server.closeup_path:
                raise ValueError("No close-up loaded; open one first.")
            payload = json.loads(body.decode("utf-8"))
            hotspots = payload.get("hotspots")
            data = load_closeup_yaml(self.server.closeup_path)
            apply_hotspots(data, hotspots)
            save_closeup_yaml(self.server.closeup_path, data)
            self.send_json({"ok": True})
        except Exception as exc:
            self.send_json({"ok": False, "error": str(exc)}, status=400)

    def serve_asset(self, path: str) -> None:
        asset_name = urllib.parse.unquote(path[len("/assets/"):])
        try:
            asset_path = resolve_within(self.server.base_path, asset_name)
            if not asset_path.exists() or not asset_path.is_file():
                raise FileNotFoundError
        except (FileNotFoundError, ValueError):
            self.send_error(404, "Asset not found")
            return
        content_type, _ = mimetypes.guess_type(asset_path.name)
        content_type = content_type or "application/octet-stream"
        body = asset_path.read_bytes()
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
        body = static_path.read_bytes()
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
        print(f"[closeup_editor] {self.address_string()} {format % args}")


class CloseUpEditorServer(ThreadingHTTPServer):
    def __init__(
        self,
        server_address: tuple[str, int],
        RequestHandlerClass,
        closeup_path: Optional[Path],
        base_path: Path,
        static_dir: Path,
        width: int,
        height: int,
    ) -> None:
        super().__init__(server_address, RequestHandlerClass)
        self.closeup_path = closeup_path
        self.base_path = base_path
        self.static_dir = static_dir
        self.width = width
        self.height = height


def run_server(
    closeup_path: Optional[Path],
    base_path: Path,
    host: str,
    port: int,
    width: int,
    height: int,
) -> None:
    static_dir = Path(__file__).resolve().parent / "static"
    server = CloseUpEditorServer(
        (host, port), CloseUpEditorHandler, closeup_path, base_path, static_dir, width, height
    )
    print(f"Serving close-up editor at http://{host}:{port}/")
    print(f"Editing close-up file: {closeup_path if closeup_path else '(none — pick one in the UI)'}")
    print(f"Asset base path: {base_path}  (virtual resolution {width}x{height})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping close-up editor.")
        server.server_close()
