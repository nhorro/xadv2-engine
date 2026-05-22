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
  <title>Avatar Composer</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #151515;
      color: #f2f2f2;
    }
    * { box-sizing: border-box; }
    body { margin: 0; height: 100vh; overflow: hidden; }
    button, input, select, textarea {
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
      grid-template-columns: 240px 1fr 300px;
      height: 100vh;
      min-width: 980px;
    }
    .panel {
      border-right: 1px solid #333;
      background: #1d1d1d;
      display: flex;
      flex-direction: column;
      min-height: 0;
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
    .toolbar input, .toolbar select { width: 100%; }
    .list {
      overflow: auto;
      padding: 8px;
      display: grid;
      gap: 4px;
    }
    .item-button {
      text-align: left;
      border: 1px solid transparent;
      background: transparent;
      padding: 8px;
      border-radius: 4px;
    }
    .item-button:hover { background: #292929; }
    .item-button.selected { background: #26384f; border-color: #5f8bc2; }
    .viewer {
      min-width: 0;
      min-height: 0;
      display: grid;
      grid-template-rows: auto 1fr auto;
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
    .details, .status { padding: 10px; }
    .details { overflow: auto; flex: 1; }
    .details div { margin: 6px 0; }
    .layer-row {
      display: grid;
      grid-template-columns: 1fr auto auto;
      gap: 6px;
      align-items: center;
      padding: 8px;
      border: 1px solid #383838;
      border-radius: 4px;
      background: #222;
    }
    .layer-row.selected { border-color: #e0c452; background: #302d1d; }
    .hint { color: #aaa; font-size: 13px; line-height: 1.35; }
    .status { min-height: 44px; color: #98d08f; border-top: 1px solid #333; }
    .field-row { display: grid; grid-template-columns: 1fr auto; gap: 8px; margin-top: 8px; }
    textarea { width: 100%; height: 100px; resize: vertical; background: #181818; }
  </style>
</head>
<body>
  <main class="app">
    <aside class="panel">
      <div class="toolbar">
        <input id="sprite-filter" placeholder="Filter sprites">
      </div>
      <div id="sprites" class="list"></div>
    </aside>

    <section class="viewer">
      <div class="toolbar">
        <strong id="canvas-title">Avatar Composer</strong>
        <label>Zoom</label>
        <select id="zoom">
          <option value="1">1x</option>
          <option value="2" selected>2x</option>
          <option value="3">3x</option>
          <option value="4">4x</option>
        </select>
      </div>
      <div class="canvas-wrap">
        <canvas id="canvas" width="1" height="1"></canvas>
      </div>
      <div class="toolbar">
        <button id="place-sprite" class="primary">Place selected sprite</button>
        <button id="delete-layer" class="danger">Delete layer</button>
        <button id="clear-frame">Clear frame</button>
      </div>
    </section>

    <aside class="panel right">
      <div class="toolbar">
        <input id="new-frame-id" placeholder="Frame id">
        <button id="add-frame" class="primary">Add frame</button>
      </div>
      <div class="list" id="frames"></div>
      <div class="details">
        <div><strong>Selected frame:</strong> <span id="frame-name">None</span></div>
        <div><strong>Selected sprite:</strong> <span id="selected-sprite">None</span></div>
        <div><strong>Selected layer:</strong> <span id="selected-layer">None</span></div>
        <div class="hint">Click a sprite to select it, click the canvas to place it, or drag an existing layer.</div>
        <div class="field-row">
          <input id="layer-x" placeholder="x" type="number">
          <input id="layer-y" placeholder="y" type="number">
        </div>
        <div style="margin-top:8px;">
          <button id="update-position" class="primary">Update position</button>
        </div>
      </div>
      <div class="details" style="border-top:1px solid #333;">
        <strong>Layers</strong>
        <div id="layers"></div>
      </div>
      <div class="toolbar">
        <button id="save" class="primary">Save animation</button>
      </div>
      <div id="status" class="status"></div>
    </aside>
  </main>

  <script>
    const state = {
      atlas: null,
      animation: null,
      image: new Image(),
      selectedSpriteId: null,
      selectedFrameIndex: 0,
      selectedLayerIndex: null,
      dragging: false,
      dragOffset: { x: 0, y: 0 },
      panning: false,
      panOffset: { x: 0, y: 0 },
      panStart: { x: 0, y: 0 },
      zoom: 2,
      dirty: false,
    };

    const spritesEl = document.getElementById('sprites');
    const framesEl = document.getElementById('frames');
    const layersEl = document.getElementById('layers');
    const canvas = document.getElementById('canvas');
    const ctx = canvas.getContext('2d');
    const statusEl = document.getElementById('status');

    function currentFrame() {
      return state.animation.frames[state.selectedFrameIndex];
    }

    function selectedLayer() {
      const frame = currentFrame();
      if (!frame || state.selectedLayerIndex === null) return null;
      return frame.layers[state.selectedLayerIndex] || null;
    }

    function setStatus(text, error = false) {
      statusEl.textContent = text;
      statusEl.className = error ? 'status error' : 'status';
    }

    function renderSprites() {
      const filter = document.getElementById('sprite-filter').value.toLowerCase();
      spritesEl.replaceChildren();
      state.atlas.sprites.forEach((sprite) => {
        if (filter && !sprite.id.toLowerCase().includes(filter)) return;
        const btn = document.createElement('button');
        btn.className = 'item-button' + (sprite.id === state.selectedSpriteId ? ' selected' : '');
        btn.style.display = 'flex';
        btn.style.flexDirection = 'column';
        btn.style.alignItems = 'center';
        btn.style.gap = '4px';
        btn.onclick = () => {
          state.selectedSpriteId = sprite.id;
          renderAll();
        };
        const label = document.createElement('span');
        label.textContent = sprite.id;
        label.style.fontSize = '12px';
        const previewCanvas = document.createElement('canvas');
        const rect = sprite.rect;
        const w = rect.width || 1;
        const h = rect.height || 1;
        const maxPreviewSize = 48;
        const scale = Math.min(maxPreviewSize / w, maxPreviewSize / h);
        previewCanvas.width = Math.max(20, Math.ceil(w * scale));
        previewCanvas.height = Math.max(20, Math.ceil(h * scale));
        previewCanvas.style.background = 'rgba(0, 0, 0, 0.3)';
        previewCanvas.style.border = '1px solid #555';
        previewCanvas.style.imageRendering = 'pixelated';
        const previewCtx = previewCanvas.getContext('2d');
        previewCtx.drawImage(
          state.image,
          rect.x, rect.y, w, h,
          0, 0, w * scale, h * scale
        );
        btn.append(previewCanvas, label);
        spritesEl.append(btn);
      });
    }

    function renderFrames() {
      framesEl.replaceChildren();
      state.animation.frames.forEach((frame, index) => {
        const btn = document.createElement('button');
        btn.className = 'item-button' + (index === state.selectedFrameIndex ? ' selected' : '');
        btn.style.display = 'flex';
        btn.style.flexDirection = 'column';
        btn.style.alignItems = 'center';
        btn.style.gap = '4px';
        const label = document.createElement('span');
        label.textContent = frame.id;
        const thumbCanvas = document.createElement('canvas');
        thumbCanvas.width = 60;
        thumbCanvas.height = 60;
        thumbCanvas.style.background = 'rgba(0, 0, 0, 0.3)';
        thumbCanvas.style.border = '1px solid #555';
        thumbCanvas.style.imageRendering = 'pixelated';
        const thumbCtx = thumbCanvas.getContext('2d');
        if (frame.layers && frame.layers.length > 0) {
          // render frame thumbnail
          thumbCtx.fillStyle = '#0a0a0a';
          thumbCtx.fillRect(0, 0, 60, 60);
          // compute frame bbox to fit in thumb
          let minX = null, minY = null, maxX = null, maxY = null;
          frame.layers.forEach((layer) => {
            const sprite = state.atlas.sprites.find((s) => s.id === layer.sprite_id);
            if (!sprite) return;
            const w = sprite.rect.width;
            const h = sprite.rect.height;
            const rx = layer.x + w;
            const ry = layer.y + h;
            minX = minX === null ? layer.x : Math.min(minX, layer.x);
            minY = minY === null ? layer.y : Math.min(minY, layer.y);
            maxX = maxX === null ? rx : Math.max(maxX, rx);
            maxY = maxY === null ? ry : Math.max(maxY, ry);
          });
          if (minX !== null) {
            const boxW = maxX - minX;
            const boxH = maxY - minY;
            const scale = Math.min(55 / boxW, 55 / boxH, 1);
            const offsetX = (60 - boxW * scale) / 2 - minX * scale;
            const offsetY = (60 - boxH * scale) / 2 - minY * scale;
            frame.layers.forEach((layer) => {
              const sprite = state.atlas.sprites.find((s) => s.id === layer.sprite_id);
              if (!sprite) return;
              const rect = sprite.rect;
              thumbCtx.drawImage(
                state.image,
                rect.x, rect.y, rect.width, rect.height,
                offsetX + layer.x * scale, offsetY + layer.y * scale,
                rect.width * scale, rect.height * scale
              );
            });
          }
        }
        btn.append(label, thumbCanvas);
        btn.onclick = () => {
          state.selectedFrameIndex = index;
          state.selectedLayerIndex = null;
          renderAll();
        };
        framesEl.append(btn);
      });
    }

    function renderLayers() {
      layersEl.replaceChildren();
      const frame = currentFrame();
      frame.layers.forEach((layer, index) => {
        const row = document.createElement('div');
        row.className = 'layer-row' + (index === state.selectedLayerIndex ? ' selected' : '');
        row.onclick = () => {
          state.selectedLayerIndex = index;
          const sprite = state.atlas.sprites.find((sprite) => sprite.id === layer.sprite_id);
          if (sprite) state.selectedSpriteId = sprite.id;
          renderAll();
        };
        const label = document.createElement('span');
        label.textContent = `${layer.sprite_id} (${layer.x}, ${layer.y})`;
        const orderBtn = document.createElement('button');
        orderBtn.textContent = '↑';
        orderBtn.onclick = (event) => {
          event.stopPropagation();
          if (index > 0) {
            frame.layers.splice(index - 1, 0, frame.layers.splice(index, 1)[0]);
            state.selectedLayerIndex = index - 1;
            state.dirty = true;
            renderAll();
          }
        };
        const delBtn = document.createElement('button');
        delBtn.className = 'danger';
        delBtn.textContent = 'Del';
        delBtn.onclick = (event) => {
          event.stopPropagation();
          frame.layers.splice(index, 1);
          if (state.selectedLayerIndex === index) state.selectedLayerIndex = null;
          state.dirty = true;
          renderAll();
        };
        row.append(label, orderBtn, delBtn);
        layersEl.append(row);
      });
      if (!frame.layers.length) {
        const hint = document.createElement('p');
        hint.className = 'hint';
        hint.textContent = 'No layers yet. Select a sprite and click the canvas to place it.';
        layersEl.append(hint);
      }
    }

    function renderCanvas() {
      const frame = currentFrame();
      const canvasW = (state.animation.canvas && state.animation.canvas.width) ? state.animation.canvas.width : 1024;
      const canvasH = (state.animation.canvas && state.animation.canvas.height) ? state.animation.canvas.height : 1024;
      canvas.width = Math.max(1, Math.ceil(canvasW * state.zoom));
      canvas.height = Math.max(1, Math.ceil(canvasH * state.zoom));
      ctx.imageSmoothingEnabled = false;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.fillStyle = '#101010';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.strokeStyle = '#3a3a3a';
      ctx.lineWidth = 1;
      for (let x = 0; x < canvas.width; x += 40 * state.zoom) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, canvas.height);
        ctx.stroke();
      }
      for (let y = 0; y < canvas.height; y += 40 * state.zoom) {
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(canvas.width, y);
        ctx.stroke();
      }
      frame.layers.forEach((layer, index) => {
        const sprite = state.atlas.sprites.find((sprite) => sprite.id === layer.sprite_id);
        if (!sprite) return;
        const rect = sprite.rect;
        const drawX = Math.round((layer.x + state.panOffset.x) * state.zoom);
        const drawY = Math.round((layer.y + state.panOffset.y) * state.zoom);
        ctx.drawImage(
          state.image,
          rect.x, rect.y, rect.width, rect.height,
          drawX, drawY,
          rect.width * state.zoom, rect.height * state.zoom
        );
        if (index === state.selectedLayerIndex) {
          ctx.save();
          ctx.strokeStyle = '#ffe66d';
          ctx.lineWidth = 2;
          ctx.strokeRect(drawX, drawY, rect.width * state.zoom, rect.height * state.zoom);
          ctx.restore();
        }
      });
      // compute and draw bounding box for all layers
      let minX = null, minY = null, maxX = null, maxY = null;
      frame.layers.forEach((layer) => {
        const sprite = state.atlas.sprites.find((sprite) => sprite.id === layer.sprite_id);
        if (!sprite) return;
        const w = sprite.rect.width;
        const h = sprite.rect.height;
        const rx = layer.x + w;
        const ry = layer.y + h;
        minX = minX === null ? layer.x : Math.min(minX, layer.x);
        minY = minY === null ? layer.y : Math.min(minY, layer.y);
        maxX = maxX === null ? rx : Math.max(maxX, rx);
        maxY = maxY === null ? ry : Math.max(maxY, ry);
      });
      if (minX !== null) {
        ctx.save();
        ctx.strokeStyle = '#4fa66b';
        ctx.lineWidth = 2;
        ctx.setLineDash([4, 4]);
        const bboxX = Math.round((minX + state.panOffset.x) * state.zoom);
        const bboxY = Math.round((minY + state.panOffset.y) * state.zoom);
        const bboxW = Math.round((maxX - minX) * state.zoom);
        const bboxH = Math.round((maxY - minY) * state.zoom);
        ctx.strokeRect(bboxX, bboxY, bboxW, bboxH);
        ctx.restore();
      }
      // store last canvas size for coordinate conversions
      frame._lastCanvas = { width: canvasW, height: canvasH };
    }

    function renderMeta() {
      document.getElementById('frame-name').textContent = currentFrame()?.id || 'None';
      document.getElementById('selected-sprite').textContent = state.selectedSpriteId || 'None';
      const layer = selectedLayer();
      document.getElementById('selected-layer').textContent = layer ? layer.sprite_id : 'None';
      document.getElementById('layer-x').value = layer ? layer.x : '';
      document.getElementById('layer-y').value = layer ? layer.y : '';
      // compute bounding box for info display
      const frame = currentFrame();
      let minX = null, minY = null, maxX = null, maxY = null;
      frame.layers.forEach((l) => {
        const sprite = state.atlas.sprites.find((s) => s.id === l.sprite_id);
        if (!sprite) return;
        const w = sprite.rect.width;
        const h = sprite.rect.height;
        const rx = l.x + w;
        const ry = l.y + h;
        minX = minX === null ? l.x : Math.min(minX, l.x);
        minY = minY === null ? l.y : Math.min(minY, l.y);
        maxX = maxX === null ? rx : Math.max(maxX, rx);
        maxY = maxY === null ? ry : Math.max(maxY, ry);
      });
      const bboxInfo = minX !== null ? `${Math.round(maxX - minX)} x ${Math.round(maxY - minY)}` : 'N/A';
      const bboxOriginInfo = minX !== null ? `(${Math.round(minX)}, ${Math.round(minY)})` : 'N/A';
      document.getElementById('frame-name').textContent = (currentFrame()?.id || 'None') + ' — bbox: ' + bboxInfo + ' at ' + bboxOriginInfo;
    }

    function renderAll() {
      renderSprites();
      renderFrames();
      renderLayers();
      renderMeta();
      renderCanvas();
    }

    function placeSprite(x, y) {
      if (!state.selectedSpriteId) {
        setStatus('Select a sprite first.', true);
        return;
      }
      const sprite = state.atlas.sprites.find((item) => item.id === state.selectedSpriteId);
      if (!sprite) {
        setStatus('Selected sprite not found.', true);
        return;
      }
      const frame = currentFrame();
      // x,y are absolute editor canvas coordinates; store layer coordinates as absolute
      const lx = Math.round(x - sprite.rect.width / 2);
      const ly = Math.round(y - sprite.rect.height / 2);
      const layer = { sprite_id: sprite.id, x: lx, y: ly };
      frame.layers.push(layer);
      state.selectedLayerIndex = frame.layers.length - 1;
      state.selectedSpriteId = null;
      state.dirty = true;
      renderAll();
      setStatus(`Placed ${sprite.id}.`);
    }

    function hitLayer(mx, my) {
      const frame = currentFrame();
      for (let i = frame.layers.length - 1; i >= 0; i -= 1) {
        const layer = frame.layers[i];
        const sprite = state.atlas.sprites.find((item) => item.id === layer.sprite_id);
        if (!sprite) continue;
        const absX = layer.x;
        const absY = layer.y;
        if (
          mx >= absX && mx <= absX + sprite.rect.width &&
          my >= absY && my <= absY + sprite.rect.height
        ) {
          return i;
        }
      }
      return null;
    }

    function canvasToFrameCoords(event) {
      const rectBox = canvas.getBoundingClientRect();
      const canvasX = (event.clientX - rectBox.left) / state.zoom - state.panOffset.x;
      const canvasY = (event.clientY - rectBox.top) / state.zoom - state.panOffset.y;
      const absX = Math.round(canvasX);
      const absY = Math.round(canvasY);
      return { x: absX, y: absY };
    }

    canvas.addEventListener('mousedown', (event) => {
      if (event.button === 1) {
        // middle mouse button: start panning
        state.panning = true;
        state.panStart.x = event.clientX;
        state.panStart.y = event.clientY;
        return;
      }
      const coords = canvasToFrameCoords(event);
      const hit = hitLayer(coords.x, coords.y);
      if (hit !== null) {
        state.selectedLayerIndex = hit;
        const layer = currentFrame().layers[hit];
        state.dragging = true;
        state.dragOffset.x = coords.x - layer.x;
        state.dragOffset.y = coords.y - layer.y;
        renderAll();
        return;
      }
      if (state.selectedSpriteId) {
        placeSprite(coords.x, coords.y);
      }
    });

    window.addEventListener('mousemove', (event) => {
      if (state.panning) {
        const deltaX = (event.clientX - state.panStart.x) / state.zoom;
        const deltaY = (event.clientY - state.panStart.y) / state.zoom;
        state.panOffset.x += deltaX;
        state.panOffset.y += deltaY;
        state.panStart.x = event.clientX;
        state.panStart.y = event.clientY;
        renderCanvas();
        return;
      }
      if (!state.dragging) return;
      const coords = canvasToFrameCoords(event);
      const layer = selectedLayer();
      if (!layer) return;
      const absX = coords.x - state.dragOffset.x;
      const absY = coords.y - state.dragOffset.y;
      layer.x = Math.round(absX);
      layer.y = Math.round(absY);
      state.dirty = true;
      renderAll();
    });

    window.addEventListener('mouseup', () => {
      state.dragging = false;
      state.panning = false;
    });

    document.getElementById('sprite-filter').addEventListener('input', renderSprites);
    document.getElementById('zoom').addEventListener('change', (event) => {
      state.zoom = Number(event.target.value);
      renderCanvas();
    });
    document.getElementById('place-sprite').onclick = () => {
      if (!state.selectedSpriteId) {
        setStatus('Select a sprite first.', true);
        return;
      }
      setStatus('Click the canvas to place the selected sprite.');
    };
    document.getElementById('delete-layer').onclick = () => {
      const idx = state.selectedLayerIndex;
      if (idx === null) return;
      currentFrame().layers.splice(idx, 1);
      state.selectedLayerIndex = null;
      state.dirty = true;
      renderAll();
    };
    document.getElementById('clear-frame').onclick = () => {
      currentFrame().layers = [];
      state.selectedLayerIndex = null;
      state.dirty = true;
      renderAll();
    };
    document.getElementById('add-frame').onclick = () => {
      const input = document.getElementById('new-frame-id');
      const id = input.value.trim();
      if (!id) {
        setStatus('Frame id is required.', true);
        return;
      }
      if (state.animation.frames.some((frame) => frame.id === id)) {
        setStatus('Frame id already exists.', true);
        return;
      }
      const frame = { id, rect: { x: 0, y: 0, width: 512, height: 512 }, layers: [] };
      state.animation.frames.push(frame);
      state.selectedFrameIndex = state.animation.frames.length - 1;
      state.selectedLayerIndex = null;
      input.value = '';
      state.dirty = true;
      renderAll();
    };
    document.getElementById('update-position').onclick = () => {
      const layer = selectedLayer();
      if (!layer) {
        setStatus('Select a layer first.', true);
        return;
      }
      const x = Number(document.getElementById('layer-x').value);
      const y = Number(document.getElementById('layer-y').value);
      if (Number.isNaN(x) || Number.isNaN(y)) {
        setStatus('Provide valid coordinates.', true);
        return;
      }
      layer.x = x;
      layer.y = y;
      state.dirty = true;
      renderAll();
      setStatus('Layer position updated.');
    };
    document.getElementById('save').onclick = async () => {
      const response = await fetch('/api/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ animation: state.animation }),
      });
      const result = await response.json();
      if (!response.ok) {
        setStatus(result.error || 'Save failed.', true);
        return;
      }
      state.dirty = false;
      setStatus(result.message);
    };

    window.addEventListener('beforeunload', (event) => {
      if (!state.dirty) return;
      event.preventDefault();
      event.returnValue = '';
    });

    async function init() {
      const response = await fetch('/api/state');
      if (!response.ok) throw new Error('Unable to load state');
      const data = await response.json();
      state.atlas = data.atlas;
      state.animation = data.animation;
      state.animation.frames ||= [];
      if (!state.animation.frames.length) {
        state.animation.frames.push({ id: 'frame_000', rect: { x: 0, y: 0, width: 512, height: 512 }, layers: [] });
      }
      state.image.onload = renderAll;
      state.image.src = '/api/image';
    }

    init().catch((error) => setStatus(String(error), true));
  </script>
</body>
</html>
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Local web editor for composing avatar animation frames."
    )
    parser.add_argument("--atlas", type=Path, required=True, help="Atlas YAML to load")
    parser.add_argument(
        "--image",
        type=Path,
        default=None,
        help="Packed atlas PNG. Defaults to the YAML 'image' path resolved beside --atlas.",
    )
    parser.add_argument(
        "--animation",
        type=Path,
        default=None,
        help="Animation YAML file to load/save. Defaults to atlas directory/avatar_animation.yml.",
    )
    parser.add_argument("--host", default="127.0.0.1", help="Bind host")
    parser.add_argument("--port", type=int, default=8766, help="Bind port")
    parser.add_argument(
        "--no-browser",
        action="store_true",
        help="Do not open the default browser automatically",
    )
    return parser.parse_args()


class AvatarComposerState:
    def __init__(self, atlas_path: Path, image_path: Path | None, animation_path: Path | None):
        self.atlas_path = atlas_path.resolve()
        self.atlas = self.load_atlas_yaml()
        self.image_path = (image_path.resolve() if image_path else self.resolve_image_path())
        self.animation_path = self.resolve_animation_path(animation_path)
        self.animation = self.load_animation_yaml()

    def load_atlas_yaml(self) -> dict:
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
        return data

    def resolve_image_path(self) -> Path:
        image = self.atlas.get("image")
        if not isinstance(image, str) or not image:
            raise RuntimeError("Atlas YAML must contain an 'image' path or --image must be provided")
        path = Path(image)
        if not path.is_absolute():
            path = self.atlas_path.parent / path
        if not path.exists():
            raise RuntimeError(f"Atlas image does not exist: {path}")
        return path.resolve()

    def resolve_animation_path(self, animation_path: Path | None) -> Path:
        if animation_path:
            return animation_path.resolve()
        default_name = "avatar_animation.yml"
        return self.atlas_path.parent / default_name

    def load_animation_yaml(self) -> dict:
        if not self.animation_path.exists():
            return {
                "atlas": self.atlas_path.name,
                "image": self.image_path.name,
                "canvas": {"width": 1024, "height": 1024},
                "frames": [],
            }
        with self.animation_path.open("r", encoding="utf-8") as handle:
            data = yaml.safe_load(handle)
        if not isinstance(data, dict):
            raise RuntimeError("Animation YAML must contain a mapping at the top level")
        frames = data.get("frames")
        if frames is None:
            data["frames"] = []
        elif not isinstance(frames, list):
            raise RuntimeError("Animation YAML must contain a 'frames' list")
        canvas = data.get("canvas")
        if canvas is None or not isinstance(canvas, dict):
            data["canvas"] = {"width": 1024, "height": 1024}
        return data

    def public_state(self) -> dict:
        return {"atlas": self.atlas, "animation": self.animation}

    def save_animation(self, payload: dict) -> None:
      if not isinstance(payload, dict):
        raise RuntimeError("Payload must be a mapping")
      animation = payload.get("animation")
      if not isinstance(animation, dict):
        raise RuntimeError("Payload must contain an animation mapping")
      frames = animation.get("frames")
      if not isinstance(frames, list):
        raise RuntimeError("Animation payload must contain a frames list")
      canvas = animation.get("canvas")
      if not isinstance(canvas, dict):
        raise RuntimeError("Animation payload must contain canvas dimensions")

      clean_frames = []
      for frame in frames:
        if not isinstance(frame, dict) or "id" not in frame:
          continue
        frame_id = str(frame["id"])
        raw_layers = [l for l in (frame.get("layers") or []) if isinstance(l, dict)]

        # compute minimal bounding box for this frame using absolute layer positions
        min_x = None
        min_y = None
        max_x = None
        max_y = None
        processed_layers = []

        # In the editor we store layer coordinates as absolute editor canvas positions

        for layer in raw_layers:
          sprite_id = layer.get("sprite_id")
          lx = layer.get("x")
          ly = layer.get("y")
          if not isinstance(sprite_id, str) or not isinstance(lx, int) or not isinstance(ly, int):
            continue
          # absolute layer position (editor coords)
          abs_x = int(lx)
          abs_y = int(ly)

          # find sprite rect in atlas
          sprite = None
          for s in self.atlas.get("sprites", []):
            if s.get("id") == sprite_id:
              sprite = s
              break
          if sprite is None:
            continue
          rect = sprite.get("rect") or {}
          w = int(rect.get("width", 0))
          h = int(rect.get("height", 0))
          # update bounding box (use absolute layer x,y as top-left of sprite)
          lx_abs = abs_x
          ly_abs = abs_y
          rx = lx_abs + w
          ry = ly_abs + h
          min_x = lx_abs if min_x is None else min(min_x, lx_abs)
          min_y = ly_abs if min_y is None else min(min_y, ly_abs)
          max_x = rx if max_x is None else max(max_x, rx)
          max_y = ry if max_y is None else max(max_y, ry)
          processed_layers.append((layer, sprite, w, h, abs_x, abs_y))

        if min_x is None:
          # empty frame
          frame_rect = {"x": 0, "y": 0, "width": 0, "height": 0}
        else:
          frame_rect = {"x": int(min_x), "y": int(min_y), "width": int(max_x - min_x), "height": int(max_y - min_y)}

        layers_out = []
        for (layer, sprite, w, h, abs_x, abs_y) in processed_layers:
          sprite_id = layer.get("sprite_id")
          rel_x = abs_x - frame_rect["x"]
          rel_y = abs_y - frame_rect["y"]
          out_layer = {"sprite_id": sprite_id, "x": int(rel_x), "y": int(rel_y)}

          # translate sprite anchors into frame coordinate system
          anchors = sprite.get("anchors") or {}
          if isinstance(anchors, dict) and anchors:
            out_anchors: dict[str, dict[str, int]] = {}
            for name, point in anchors.items():
              try:
                if isinstance(point, dict):
                  ax = int(point.get("x", 0))
                  ay = int(point.get("y", 0))
                else:
                  ax = int(point[0])
                  ay = int(point[1])
              except Exception:
                continue
              frame_ax = ax + abs_x - frame_rect["x"]
              frame_ay = ay + abs_y - frame_rect["y"]
              out_anchors[name] = {"x": int(frame_ax), "y": int(frame_ay)}
            if out_anchors:
              out_layer["anchors"] = out_anchors

          layers_out.append(out_layer)

        clean_frames.append({"id": frame_id, "rect": frame_rect, "layers": layers_out})

      clean_animation = {
        "atlas": animation.get("atlas") or self.atlas_path.name,
        "image": animation.get("image") or self.image_path.name,
        "canvas": animation.get("canvas") or self.animation.get("canvas") or {"width": 1024, "height": 1024},
        "frames": clean_frames,
      }

      with self.animation_path.open("w", encoding="utf-8") as handle:
        yaml.safe_dump(clean_animation, handle, sort_keys=False, allow_unicode=True, default_flow_style=False)


def make_handler(state: AvatarComposerState) -> type[BaseHTTPRequestHandler]:
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
                state.save_animation(payload)
                self.write_json({"message": f"Saved {state.animation_path}"})
            except Exception as exc:
                self.write_json({"error": str(exc)}, status=400)

        def write_json(self, payload: object, status: int = 200) -> None:
            self.write_bytes(json.dumps(payload).encode("utf-8"), "application/json; charset=utf-8", status)

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
    state = AvatarComposerState(args.atlas, args.image, args.animation)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    url = f"http://{args.host}:{args.port}/"
    print(f"Avatar composer: {url}")
    print(f"Atlas YAML: {state.atlas_path}")
    print(f"Atlas image: {state.image_path}")
    print(f"Animation YAML: {state.animation_path}")
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
