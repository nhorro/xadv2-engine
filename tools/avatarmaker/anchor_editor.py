#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import mimetypes
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

try:
    import yaml
except ImportError as exc:
    raise SystemExit("Missing dependency: PyYAML. Install it with: python3 -m pip install PyYAML") from exc


HTML = r"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Avatar Anchor Editor</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #151515;
      color: #f2f2f2;
    }
    * { box-sizing: border-box; }
    body { margin: 0; height: 100vh; overflow: hidden; }
    button, input, select {
      font: inherit;
      color: #f2f2f2;
      background: #242424;
      border: 1px solid #555;
      border-radius: 4px;
      padding: 6px 8px;
    }
    button { cursor: pointer; }
    button:hover { background: #333; }
    button.primary { background: #255a35; border-color: #4fa66b; }
    button.danger { background: #5a2525; border-color: #a64f4f; }
    .app {
      display: grid;
      grid-template-columns: 230px 1fr 320px;
      height: 100vh;
      min-width: 920px;
    }
    .panel {
      border-right: 1px solid #333;
      background: #1d1d1d;
      min-height: 0;
      display: flex;
      flex-direction: column;
    }
    .right { border-right: 0; border-left: 1px solid #333; }
    .toolbar {
      display: flex;
      gap: 8px;
      align-items: center;
      padding: 10px;
      border-bottom: 1px solid #333;
      background: #191919;
    }
    .toolbar input { width: 100%; }
    .toolbar .toggle {
      display: flex;
      gap: 6px;
      align-items: center;
      color: #cfcfcf;
      white-space: nowrap;
      cursor: pointer;
    }
    .toolbar .toggle input {
      width: auto;
      margin: 0;
      padding: 0;
      accent-color: #a8a8a8;
    }
    .frames {
      overflow: auto;
      padding: 8px;
      display: grid;
      gap: 4px;
    }
    .frame-button {
      text-align: left;
      border: 1px solid transparent;
      background: transparent;
      padding: 7px 8px;
      border-radius: 4px;
    }
    .frame-button:hover { background: #292929; }
    .frame-button.selected {
      background: #26384f;
      border-color: #5f8bc2;
    }
    .viewer {
      min-width: 0;
      min-height: 0;
      display: grid;
      grid-template-rows: auto 1fr;
      background: #101010;
    }
    .canvas-wrap {
      min-width: 0;
      min-height: 0;
      overflow: auto;
      display: grid;
      place-items: center;
      background:
        linear-gradient(45deg, #252525 25%, transparent 25%),
        linear-gradient(-45deg, #252525 25%, transparent 25%),
        linear-gradient(45deg, transparent 75%, #252525 75%),
        linear-gradient(-45deg, transparent 75%, #252525 75%);
      background-size: 24px 24px;
      background-position: 0 0, 0 12px, 12px -12px, -12px 0;
    }
    canvas {
      image-rendering: pixelated;
      background: rgba(0, 0, 0, 0.2);
      outline: 1px solid #666;
    }
    .meta, .anchors, .status {
      padding: 10px;
      border-bottom: 1px solid #333;
    }
    .meta div { margin: 4px 0; color: #cfcfcf; }
    .anchors {
      overflow: auto;
      flex: 1;
      min-height: 0;
    }
    .anchor-row {
      display: grid;
      grid-template-columns: 1fr auto auto;
      gap: 6px;
      align-items: center;
      margin-bottom: 6px;
      padding: 6px;
      border: 1px solid #383838;
      border-radius: 4px;
      background: #222;
    }
    .anchor-row.selected { border-color: #e0c452; background: #302d1d; }
    .anchor-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .coord { color: #bdbdbd; font-variant-numeric: tabular-nums; }
    .hint { color: #aaa; font-size: 13px; line-height: 1.35; }
    .status {
      min-height: 42px;
      color: #98d08f;
      border-bottom: 0;
    }
    .error { color: #ff8d8d; }
  </style>
</head>
<body>
  <main class="app">
    <aside class="panel">
      <div class="toolbar">
        <input id="filter" placeholder="Filter frames">
      </div>
      <div id="frames" class="frames"></div>
    </aside>

    <section class="viewer">
      <div class="toolbar">
        <strong id="frame-title">No frame</strong>
        <label>Zoom</label>
        <select id="zoom">
          <option value="1">1x</option>
          <option value="2" selected>2x</option>
          <option value="3">3x</option>
          <option value="4">4x</option>
          <option value="6">6x</option>
          <option value="8">8x</option>
        </select>
        <label class="toggle" title="Overlay the selected anchor from other frames matching the frame filter">
          <input id="ghost-pivots" type="checkbox">
          Ghost pivots
        </label>
      </div>
      <div class="canvas-wrap">
        <canvas id="canvas" width="1" height="1"></canvas>
      </div>
    </section>

    <aside class="panel right">
      <div class="toolbar">
        <input id="anchor-name" placeholder="anchor name">
        <button id="add-anchor">Add</button>
      </div>
      <div class="meta" id="meta"></div>
      <div class="anchors" id="anchors"></div>
      <div class="toolbar">
        <button id="save" class="primary">Save atlas</button>
      </div>
      <div id="status" class="status"></div>
    </aside>
  </main>

  <script>
    const state = {
      atlas: null,
      image: new Image(),
      selectedIndex: 0,
      selectedAnchor: "",
      zoom: 2,
      ghostPivots: false,
      dirty: false,
    };

    const framesEl = document.getElementById("frames");
    const anchorsEl = document.getElementById("anchors");
    const canvas = document.getElementById("canvas");
    const ctx = canvas.getContext("2d");
    const statusEl = document.getElementById("status");

    function currentSprite() {
      return state.atlas.sprites[state.selectedIndex];
    }

    function setStatus(text, isError = false) {
      statusEl.textContent = text;
      statusEl.className = isError ? "status error" : "status";
    }

    function rectText(rect) {
      return `${rect.x}, ${rect.y}, ${rect.width}x${rect.height}`;
    }

    function frameFilter() {
      return document.getElementById("filter").value.trim().toLowerCase();
    }

    function matchesFrameFilter(sprite, filter = frameFilter()) {
      return !filter || sprite.id.toLowerCase().includes(filter);
    }

    function renderFrameList() {
      const filter = frameFilter();
      framesEl.replaceChildren();
      state.atlas.sprites.forEach((sprite, index) => {
        if (!matchesFrameFilter(sprite, filter)) return;
        const button = document.createElement("button");
        button.className = "frame-button" + (index === state.selectedIndex ? " selected" : "");
        button.textContent = sprite.id;
        button.onclick = () => {
          state.selectedIndex = index;
          const names = Object.keys(sprite.anchors || {});
          if (!state.selectedAnchor) state.selectedAnchor = names[0] || "";
          document.getElementById("anchor-name").value = state.selectedAnchor;
          renderAll();
        };
        framesEl.appendChild(button);
      });
    }

    function renderMeta() {
      const sprite = currentSprite();
      document.getElementById("frame-title").textContent = sprite.id;
      document.getElementById("meta").innerHTML = `
        <div><strong>Atlas rect</strong></div>
        <div>${rectText(sprite.rect)}</div>
        <div><strong>Source rect</strong></div>
        <div>${rectText(sprite.source_rect)}</div>
        <p class="hint">Select or add an anchor name, then click the frame. Coordinates are stored relative to this frame rect.</p>
        <p class="hint">The Ghost pivots option overlays the selected anchor from other frames matching the frame filter, so related animation frames can be aligned.</p>
      `;
    }

    function renderAnchors() {
      const sprite = currentSprite();
      sprite.anchors ||= {};
      anchorsEl.replaceChildren();
      const names = Object.keys(sprite.anchors).sort();
      if (!names.length) {
        const empty = document.createElement("p");
        empty.className = "hint";
        empty.textContent = "No anchors in this frame.";
        anchorsEl.appendChild(empty);
        return;
      }
      for (const name of names) {
        const point = sprite.anchors[name];
        const row = document.createElement("div");
        row.className = "anchor-row" + (name === state.selectedAnchor ? " selected" : "");
        const label = document.createElement("button");
        label.className = "anchor-name";
        label.textContent = name;
        label.onclick = () => {
          state.selectedAnchor = name;
          document.getElementById("anchor-name").value = name;
          renderAll();
        };
        const coord = document.createElement("span");
        coord.className = "coord";
        coord.textContent = `${point.x}, ${point.y}`;
        const del = document.createElement("button");
        del.className = "danger";
        del.textContent = "Del";
        del.onclick = () => {
          delete sprite.anchors[name];
          if (state.selectedAnchor === name) state.selectedAnchor = "";
          state.dirty = true;
          renderAll();
        };
        row.append(label, coord, del);
        anchorsEl.appendChild(row);
      }
    }

    function renderCanvas() {
      const sprite = currentSprite();
      const rect = sprite.rect;
      const zoom = state.zoom;
      canvas.width = Math.max(1, rect.width * zoom);
      canvas.height = Math.max(1, rect.height * zoom);
      ctx.imageSmoothingEnabled = false;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.drawImage(
        state.image,
        rect.x, rect.y, rect.width, rect.height,
        0, 0, rect.width * zoom, rect.height * zoom
      );

      if (state.ghostPivots && state.selectedAnchor) {
        const filter = frameFilter();
        const groups = new Map();
        state.atlas.sprites.forEach((otherSprite, index) => {
          if (index === state.selectedIndex || !matchesFrameFilter(otherSprite, filter)) return;
          const point = otherSprite.anchors?.[state.selectedAnchor];
          if (!point || !Number.isFinite(point.x) || !Number.isFinite(point.y)) return;
          const key = `${point.x},${point.y}`;
          if (!groups.has(key)) groups.set(key, { point, frameIds: [] });
          groups.get(key).frameIds.push(otherSprite.id);
        });

        for (const { point, frameIds } of groups.values()) {
          const x = point.x * zoom;
          const y = point.y * zoom;
          const label = frameIds.length <= 3 ? frameIds.join(", ") : `${frameIds.length} frames`;
          ctx.save();
          ctx.strokeStyle = "rgba(205, 205, 205, 0.52)";
          ctx.fillStyle = "rgba(220, 220, 220, 0.66)";
          ctx.lineWidth = 1;
          ctx.setLineDash([3, 3]);
          ctx.beginPath();
          ctx.moveTo(x - 9, y);
          ctx.lineTo(x + 9, y);
          ctx.moveTo(x, y - 9);
          ctx.lineTo(x, y + 9);
          ctx.stroke();
          ctx.beginPath();
          ctx.arc(x, y, 5, 0, Math.PI * 2);
          ctx.stroke();
          ctx.setLineDash([]);
          ctx.font = "11px sans-serif";
          ctx.fillText(label, x + 7, y + 14);
          ctx.restore();
        }
      }

      const anchors = sprite.anchors || {};
      for (const [name, point] of Object.entries(anchors)) {
        const x = point.x * zoom;
        const y = point.y * zoom;
        ctx.save();
        ctx.strokeStyle = name === state.selectedAnchor ? "#ffe66d" : "#00e5ff";
        ctx.fillStyle = ctx.strokeStyle;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x - 7, y);
        ctx.lineTo(x + 7, y);
        ctx.moveTo(x, y - 7);
        ctx.lineTo(x, y + 7);
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(x, y, 3, 0, Math.PI * 2);
        ctx.fill();
        ctx.font = "12px sans-serif";
        ctx.fillText(name, x + 6, y - 6);
        ctx.restore();
      }
    }

    function renderAll() {
      renderFrameList();
      renderMeta();
      renderAnchors();
      renderCanvas();
    }

    function addAnchorName() {
      const input = document.getElementById("anchor-name");
      const name = input.value.trim();
      if (!name) {
        setStatus("Anchor name is required.", true);
        return;
      }
      const sprite = currentSprite();
      sprite.anchors ||= {};
      if (!sprite.anchors[name]) {
        sprite.anchors[name] = {
          x: Math.floor(sprite.rect.width / 2),
          y: Math.floor(sprite.rect.height / 2),
        };
        state.dirty = true;
      }
      state.selectedAnchor = name;
      setStatus(`Selected anchor: ${name}`);
      renderAll();
    }

    canvas.addEventListener("click", (event) => {
      const nameInput = document.getElementById("anchor-name").value.trim();
      if (nameInput && nameInput !== state.selectedAnchor) state.selectedAnchor = nameInput;
      if (!state.selectedAnchor) {
        setStatus("Select or add an anchor first.", true);
        return;
      }
      const sprite = currentSprite();
      const bounds = canvas.getBoundingClientRect();
      const x = Math.round((event.clientX - bounds.left) / state.zoom);
      const y = Math.round((event.clientY - bounds.top) / state.zoom);
      sprite.anchors ||= {};
      sprite.anchors[state.selectedAnchor] = {
        x: Math.max(0, Math.min(sprite.rect.width, x)),
        y: Math.max(0, Math.min(sprite.rect.height, y)),
      };
      state.dirty = true;
      setStatus(`Set ${state.selectedAnchor} on ${sprite.id}.`);
      renderAll();
    });

    document.getElementById("add-anchor").onclick = addAnchorName;
    document.getElementById("anchor-name").addEventListener("keydown", (event) => {
      if (event.key === "Enter") addAnchorName();
    });
    document.getElementById("filter").oninput = () => {
      renderFrameList();
      renderCanvas();
    };
    document.getElementById("zoom").onchange = (event) => {
      state.zoom = Number(event.target.value);
      renderCanvas();
    };
    document.getElementById("ghost-pivots").onchange = (event) => {
      state.ghostPivots = event.target.checked;
      renderCanvas();
    };
    document.getElementById("save").onclick = async () => {
      const response = await fetch("/api/save", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ sprites: state.atlas.sprites }),
      });
      const result = await response.json();
      if (!response.ok) {
        setStatus(result.error || "Save failed.", true);
        return;
      }
      state.dirty = false;
      setStatus(result.message);
    };

    window.addEventListener("beforeunload", (event) => {
      if (!state.dirty) return;
      event.preventDefault();
      event.returnValue = "";
    });

    async function init() {
      const response = await fetch("/api/state");
      state.atlas = await response.json();
      state.atlas.sprites.forEach((sprite) => { sprite.anchors ||= {}; });
      const names = Object.keys(currentSprite().anchors);
      state.selectedAnchor = names[0] || "";
      document.getElementById("anchor-name").value = state.selectedAnchor;
      state.image.onload = renderAll;
      state.image.src = "/api/image";
    }

    init().catch((error) => setStatus(String(error), true));
  </script>
</body>
</html>
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Local web editor for sprite atlas anchors."
    )
    parser.add_argument("--atlas", type=Path, required=True, help="Atlas YAML to edit")
    parser.add_argument(
        "--image",
        type=Path,
        default=None,
        help="Packed atlas PNG. Defaults to the YAML 'image' path resolved beside --atlas.",
    )
    parser.add_argument("--host", default="127.0.0.1", help="Bind host")
    parser.add_argument("--port", type=int, default=8765, help="Bind port")
    parser.add_argument(
        "--no-browser",
        action="store_true",
        help="Do not open the default browser automatically",
    )
    return parser.parse_args()


class AnchorEditorState:
    def __init__(self, atlas_path: Path, image_path: Path | None):
        self.atlas_path = atlas_path.resolve()
        self.data = self.load_yaml()
        self.image_path = (image_path.resolve() if image_path else self.resolve_image_path())

    def load_yaml(self) -> dict:
        if not self.atlas_path.exists():
            raise RuntimeError(f"Atlas YAML does not exist: {self.atlas_path}")
        with self.atlas_path.open("r", encoding="utf-8") as handle:
            data = yaml.safe_load(handle)
        if not isinstance(data, dict):
            raise RuntimeError("Atlas YAML must contain a mapping at the top level")
        sprites = data.get("sprites")
        if not isinstance(sprites, list):
            raise RuntimeError("Atlas YAML must contain a 'sprites' list")
        for sprite in sprites:
            if not isinstance(sprite, dict) or "id" not in sprite or "rect" not in sprite:
                raise RuntimeError("Each sprite must contain at least 'id' and 'rect'")
            anchors = sprite.get("anchors")
            if anchors is None:
                sprite["anchors"] = {}
            elif not isinstance(anchors, dict):
                raise RuntimeError(f"Sprite {sprite.get('id')} has invalid anchors")
        return data

    def resolve_image_path(self) -> Path:
        image = self.data.get("image")
        if not isinstance(image, str) or not image:
            raise RuntimeError("Atlas YAML must contain an 'image' path or --image must be provided")
        path = Path(image)
        if not path.is_absolute():
            path = self.atlas_path.parent / path
        if not path.exists():
            raise RuntimeError(f"Atlas image does not exist: {path}")
        return path.resolve()

    def public_state(self) -> dict:
        return self.data

    def save_anchors(self, sprites_payload: list[dict]) -> None:
        incoming = {item.get("id"): item for item in sprites_payload if isinstance(item, dict)}
        for sprite in self.data["sprites"]:
            payload = incoming.get(sprite["id"])
            if not payload:
                continue
            anchors = payload.get("anchors") or {}
            clean_anchors: dict[str, dict[str, int]] = {}
            for name, point in anchors.items():
                if not isinstance(name, str) or not isinstance(point, dict):
                    continue
                try:
                    clean_anchors[name] = {"x": int(point["x"]), "y": int(point["y"])}
                except (KeyError, TypeError, ValueError):
                    continue
            if clean_anchors:
                sprite["anchors"] = clean_anchors
            else:
                sprite.pop("anchors", None)
        with self.atlas_path.open("w", encoding="utf-8") as handle:
            yaml.safe_dump(
                self.data,
                handle,
                sort_keys=False,
                allow_unicode=True,
                default_flow_style=False,
            )


def make_handler(state: AnchorEditorState) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path == "/":
                self.write_bytes(HTML.encode("utf-8"), "text/html; charset=utf-8")
            elif path == "/api/state":
                self.write_json(state.public_state())
            elif path == "/api/image":
                content_type = mimetypes.guess_type(state.image_path.name)[0] or "image/png"
                self.write_bytes(state.image_path.read_bytes(), content_type)
            else:
                self.send_error(404)

        def do_POST(self) -> None:
            path = urlparse(self.path).path
            if path != "/api/save":
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                payload = json.loads(self.rfile.read(length).decode("utf-8"))
                sprites = payload.get("sprites")
                if not isinstance(sprites, list):
                    raise RuntimeError("Payload must contain a sprites list")
                state.save_anchors(sprites)
                self.write_json({"message": f"Saved {state.atlas_path}"})
            except Exception as exc:  # noqa: BLE001 - endpoint should report the error to the UI
                self.write_json({"error": str(exc)}, status=400)

        def write_json(self, payload: object, status: int = 200) -> None:
            self.write_bytes(
                json.dumps(payload).encode("utf-8"),
                "application/json; charset=utf-8",
                status,
            )

        def write_bytes(self, payload: bytes, content_type: str, status: int = 200) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, format: str, *args: object) -> None:
            sys.stderr.write("%s - %s\n" % (self.address_string(), format % args))

    return Handler


def main() -> int:
    args = parse_args()
    state = AnchorEditorState(args.atlas, args.image)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    url = f"http://{args.host}:{args.port}/"
    print(f"Anchor editor: {url}")
    print(f"Atlas: {state.atlas_path}")
    print(f"Image: {state.image_path}")
    if not args.no_browser:
        threading.Timer(0.2, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
