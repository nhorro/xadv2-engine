const state = {
  room: null,
  info: null,
  mode: 'walkable',
  selectedEntity: null,
  selectedVertex: null,
  selectedPoint: null,
  addVertexMode: false,
  deleteVertexMode: false,
  dragTarget: null,
  dragOffset: null,
  imageCache: new Map(),
  viewScale: 1, // world->canvas scale from the last draw(); used to map clicks back to world
};

const canvas = document.getElementById('room-canvas');
const ctx = canvas.getContext('2d');
const modeSelect = document.getElementById('mode-select');
const entitySelect = document.getElementById('entity-select');
const layersList = document.getElementById('layers-list');
const roomInfo = document.getElementById('room-info');
const assetBase = document.getElementById('asset-base');
const vertexList = document.getElementById('vertex-list');
const status = document.getElementById('status');
const reloadButton = document.getElementById('reload-button');
const saveButton = document.getElementById('save-button');
const addEntityButton = document.getElementById('add-entity');
const removeEntityButton = document.getElementById('remove-entity');
const addVertexButton = document.getElementById('add-vertex');
const deleteVertexButton = document.getElementById('delete-vertex');
const addPointButton = document.getElementById('add-point');
const deletePointButton = document.getElementById('delete-point');
const snapshotRegionButton = document.getElementById('snapshot-region');
const snapshotSource = document.getElementById('snapshot-source');

const modeOptions = ['walkable', 'zones', 'regions', 'hotspots', 'points', 'layers', 'preview'];
const entityPrefix = {
  zones: 'zone',
  regions: 'region',
  hotspots: 'hotspot',
  points: 'point',
};
let selectedLayerInputX = null;
let selectedLayerInputY = null;

function setStatus(message, isError = false) {
  status.textContent = message;
  status.style.color = isError ? '#fca5a5' : '#a5b4fc';
}

function fetchJson(url) {
  return fetch(url).then((res) => {
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
    return res.json();
  });
}

async function loadInfo() {
  state.info = await fetchJson('/api/info');
  assetBase.textContent = state.info.base_path;
}

async function loadRoom() {
  state.room = await fetchJson('/api/room');
  updateRoomInfo();
  state.selectedEntity = null;
  state.selectedPoint = null;
  state.addVertexMode = false;
  state.deleteVertexMode = false;
  state.selectedVertex = null;
  updateUI();
  draw();
}

function updateModeOptions() {
  modeSelect.innerHTML = modeOptions.map((mode) => `<option value="${mode}">${mode}</option>`).join('');
  modeSelect.value = state.mode;
}

function getEntities() {
  if (!state.room) return [];
  if (state.mode === 'walkable') return [{ id: 'walkable', label: 'walkable' }];
  if (state.mode === 'points') return Object.keys(state.room.points || {}).map((id) => ({ id, label: id }));
  if (state.mode === 'zones') return (state.room.zones || []).map((zone) => ({ id: zone.id, label: zone.id }));
  if (state.mode === 'regions') return Object.keys(state.room.regions || {}).map((id) => ({ id, label: id }));
  if (state.mode === 'hotspots') return Object.keys(state.room.hotspots || {}).map((id) => ({ id, label: id }));
  return [];
}

function updateEntityOptions() {
  const entities = getEntities();
  entitySelect.innerHTML = entities.map((item) => `<option value="${item.id}">${item.label}</option>`).join('');
  if (!entities.length) {
    state.selectedEntity = null;
    entitySelect.value = '';
  } else if (!entities.some((item) => item.id === state.selectedEntity)) {
    state.selectedEntity = entities[0].id;
    entitySelect.value = state.selectedEntity;
  } else {
    entitySelect.value = state.selectedEntity;
  }
}

// Layer origin uses the engine YAML form `origin: {x, y}` (absent = world origin
// (0,0)). Reads also tolerate a legacy flat x/y so older files still load.
function layerOrigin(layer) {
  return {
    x: layer.origin?.x ?? layer.x ?? 0,
    y: layer.origin?.y ?? layer.y ?? 0,
  };
}

// Write the engine format; omit `origin` at (0,0) so a full-room background stays
// clean, and drop any legacy flat fields so a save never carries both.
function setLayerOrigin(layer, x, y) {
  delete layer.x;
  delete layer.y;
  if (x === 0 && y === 0) {
    delete layer.origin;
  } else {
    layer.origin = { x, y };
  }
}

function updateLayersList() {
  const background = state.room.background || {};
  const layers = Array.isArray(background.layers) ? background.layers : [];
  layersList.innerHTML = layers.map((layer) => {
    const { x, y } = layerOrigin(layer);
    return `
      <div class="layer-item" data-layer-id="${layer.id || ''}">
        <strong>${layer.id || 'unnamed'}</strong>
        <div>${layer.image || ''}</div>
        <div>z: ${layer.z ?? ''} interactive: ${layer.interactive ?? false}</div>
        <div>pos: ${x}, ${y}</div>
      </div>
    `;
  }).join('');
}

function selectLayer(layerId) {
  state.selectedLayerId = layerId;
  // highlight in DOM
  for (const el of Array.from(layersList.children)) {
    if (el.getAttribute('data-layer-id') === layerId) el.classList.add('selected');
    else el.classList.remove('selected');
  }
  updateLayerInfoUI();
  updateSnapshotSource();
}

function updateLayerInfoUI() {
  const layerX = document.getElementById('layer-x');
  const layerY = document.getElementById('layer-y');
  selectedLayerInputX = layerX;
  selectedLayerInputY = layerY;
  if (!state.room || !state.selectedLayerId) {
    layerX.value = '';
    layerY.value = '';
    return;
  }
  const layers = state.room.background?.layers || [];
  const layer = layers.find((l) => l.id === state.selectedLayerId);
  if (!layer) {
    layerX.value = '';
    layerY.value = '';
    return;
  }
  const origin = layerOrigin(layer);
  layerX.value = origin.x;
  layerY.value = origin.y;
}

function applyLayerPosition() {
  if (!state.room || !state.selectedLayerId) return;
  const layers = state.room.background?.layers || [];
  const layer = layers.find((l) => l.id === state.selectedLayerId);
  if (!layer) return;
  const nx = parseInt(selectedLayerInputX.value || '0', 10);
  const ny = parseInt(selectedLayerInputY.value || '0', 10);
  setLayerOrigin(layer, nx, ny);
  updateLayersList();
  draw();
}

function resetLayerPosition() {
  if (!state.room || !state.selectedLayerId) return;
  const layers = state.room.background?.layers || [];
  const layer = layers.find((l) => l.id === state.selectedLayerId);
  if (!layer) return;
  setLayerOrigin(layer, 0, 0);
  updateLayersList();
  updateLayerInfoUI();
  draw();
}

function updateUI() {
  updateModeOptions();
  updateEntityOptions();
  updateLayersList();
  updateVertexList();
  updateSnapshotSource();
  draw();
}

function updateSnapshotSource() {
  if (!snapshotSource) return;
  const id = state.selectedLayerId;
  snapshotSource.textContent = id
    ? `Snapshot source: layer "${id}"`
    : 'Snapshot source: select a layer in Background';
}

function updateVertexList() {
  if (!state.room) {
    vertexList.innerHTML = '';
    return;
  }

  const polygon = getRoomPolygon();
  if (state.mode === 'points') {
    const points = state.room.points || {};
    vertexList.innerHTML = Object.entries(points)
      .map(([id, point]) => `<div class="vertex-item${id === state.selectedPoint ? ' selected' : ''}"><span>${id}</span><span>${point.x}, ${point.y}</span></div>`)
      .join('') || '<div class="vertex-item">No points defined.</div>';
    return;
  }

  if (!Array.isArray(polygon) || polygon.length === 0) {
    vertexList.innerHTML = '<div class="vertex-item">No vertices defined for the selected entity.</div>';
    return;
  }

  vertexList.innerHTML = polygon
    .map((vertex, index) => {
      const selected = index === state.selectedVertex ? ' selected' : '';
      return `<div class="vertex-item${selected}"><span>#${index + 1}</span><span>${vertex.x}, ${vertex.y}</span></div>`;
    })
    .join('');
}

function getRoomPolygon() {
  if (!state.room) return [];
  if (state.mode === 'walkable') return state.room.walkable || [];
  if (!state.selectedEntity) return [];
  if (state.mode === 'zones') {
    return (state.room.zones || []).find((zone) => zone.id === state.selectedEntity)?.polygon || [];
  }
  if (state.mode === 'regions') {
    return (state.room.regions || {})[state.selectedEntity]?.area || [];
  }
  if (state.mode === 'hotspots') {
    return (state.room.hotspots || {})[state.selectedEntity]?.area || [];
  }
  return [];
}

function setRoomPolygon(polygon) {
  if (!state.room) return;
  if (state.mode === 'walkable') {
    state.room.walkable = polygon;
    return;
  }
  if (!state.selectedEntity) return;
  if (state.mode === 'zones') {
    const zone = (state.room.zones || []).find((zone) => zone.id === state.selectedEntity);
    if (zone) zone.polygon = polygon;
  }
  if (state.mode === 'regions') {
    const region = (state.room.regions || {})[state.selectedEntity];
    if (region) region.area = polygon;
  }
  if (state.mode === 'hotspots') {
    const hotspot = (state.room.hotspots || {})[state.selectedEntity];
    if (hotspot) hotspot.area = polygon;
  }
}

function getPoint() {
  if (!state.room || !state.selectedPoint) return null;
  return state.room.points?.[state.selectedPoint] || null;
}

function setPoint(point) {
  if (!state.room || !state.selectedPoint) return;
  state.room.points[state.selectedPoint] = point;
}

function pointNear(point, target, threshold = 12) {
  return Math.hypot(point.x - target.x, point.y - target.y) <= threshold;
}

function resizeCanvas() {
  const rect = canvas.parentElement.getBoundingClientRect();
  canvas.width = rect.width * devicePixelRatio;
  canvas.height = rect.height * devicePixelRatio;
  canvas.style.width = `${rect.width}px`;
  canvas.style.height = `${rect.height}px`;
  ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
}

// Room world bounds, mirroring the engine's compute_room_bounds: the union of
// every layer's rect [origin, origin + native image size), anchored at (0,0).
// Origin is the layer's x/y (also accepts an `origin:{x,y}`), default (0,0). The
// format has no authored `size`; until images load we fall back so the canvas is
// still usable (img.onload re-runs draw(), which recomputes once sizes are known).
function computeRoomSize() {
  const layers = Array.isArray(state.room?.background?.layers) ? state.room.background.layers : [];
  let width = 0;
  let height = 0;
  for (const layer of layers) {
    if (!layer.image) continue;
    const img = state.imageCache.get(layer.image);
    if (!img || !img.complete || !img.naturalWidth) continue;
    const { x: ox, y: oy } = layerOrigin(layer);
    width = Math.max(width, ox + img.naturalWidth);
    height = Math.max(height, oy + img.naturalHeight);
  }
  if (width <= 0 || height <= 0) {
    return { width: state.room?.size?.width || 800, height: state.room?.size?.height || 600 };
  }
  return { width, height };
}

function updateRoomInfo() {
  if (!state.room) return;
  const { width, height } = computeRoomSize();
  roomInfo.textContent = `${state.room.id || '[unknown]'} · ${width}x${height}`;
}

function draw() {
  if (!state.room) return;
  resizeCanvas();
  const { width, height } = computeRoomSize();
  updateRoomInfo();
  const scale = Math.min(canvas.width / devicePixelRatio / width, canvas.height / devicePixelRatio / height, 1);
  state.viewScale = scale;

  ctx.save();
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.scale(scale, scale);

  const bg = state.room.background?.color || { r: 0, g: 0, b: 0 };
  ctx.fillStyle = `rgba(${bg.r || 0}, ${bg.g || 0}, ${bg.b || 0}, ${bg.a ?? 255} / 255)`;
  ctx.fillRect(0, 0, width, height);

  drawLayers();
  drawPolygons();
  drawTempRegion();
  drawPoints();
  drawSelectedHandles();

  ctx.restore();
}

function drawTempRegion() {
  if (state.mode !== 'regions' || !state.tempRegion) return;
  const { start, current } = state.tempRegion;
  const x = Math.min(start.x, current.x);
  const y = Math.min(start.y, current.y);
  const w = Math.abs(current.x - start.x);
  const h = Math.abs(current.y - start.y);
  ctx.save();
  ctx.strokeStyle = 'rgba(124, 58, 237, 0.9)';
  ctx.fillStyle = 'rgba(192, 132, 252, 0.2)';
  ctx.lineWidth = 2;
  ctx.setLineDash([6, 4]);
  ctx.fillRect(x, y, w, h);
  ctx.strokeRect(x, y, w, h);
  ctx.restore();
}

function drawLayers() {
  const layers = Array.isArray(state.room.background?.layers) ? state.room.background.layers : [];
  layers.forEach((layer) => {
    if (!layer.image) return;
    let img = state.imageCache.get(layer.image);
    if (!img) {
      img = new Image();
      img.src = `/assets/${encodeURIComponent(layer.image)}`;
      img.onload = () => {
        state.imageCache.set(layer.image, img);
        draw();
      };
      img.onerror = () => {
        setStatus(`Unable to load layer ${layer.image}`, true);
      };
      state.imageCache.set(layer.image, img);
    }
    if (img.complete && img.naturalWidth !== 0) {
      const { x: ox, y: oy } = layerOrigin(layer);
      ctx.drawImage(img, ox, oy);
    }
  });
}

function polygonForMode() {
  if (state.mode === 'walkable') {
    return [{ id: 'walkable', polygon: state.room.walkable || [] }];
  }
  if (state.mode === 'zones') return (state.room.zones || []).map((zone) => ({ id: zone.id, polygon: zone.polygon }));
  if (state.mode === 'regions') return Object.entries(state.room.regions || {}).map(([id, region]) => ({ id, polygon: region.area }));
  if (state.mode === 'hotspots') return Object.entries(state.room.hotspots || {}).map(([id, hotspot]) => ({ id, polygon: hotspot.area }));
  return [];
}

function drawPolygons() {
  const colors = {
    walkable: 'rgba(34, 197, 94, 0.25)',
    zones: 'rgba(96, 165, 250, 0.25)',
    regions: 'rgba(192, 132, 252, 0.25)',
    hotspots: 'rgba(251, 146, 60, 0.25)',
  };
  const outlines = {
    walkable: '#15803d',
    zones: '#1d4ed8',
    regions: '#7c3aed',
    hotspots: '#ea580c',
  };

  if (state.mode === 'points' || state.mode === 'preview') return;
  const polygons = polygonForMode();
  polygons.forEach((item) => {
    if (!Array.isArray(item.polygon) || item.polygon.length === 0) return;
    ctx.beginPath();
    item.polygon.forEach((vertex, index) => {
      if (index === 0) ctx.moveTo(vertex.x, vertex.y);
      else ctx.lineTo(vertex.x, vertex.y);
    });
    if (item.polygon.length > 2) ctx.closePath();
    ctx.fillStyle = colors[state.mode];
    ctx.strokeStyle = outlines[state.mode];
    ctx.lineWidth = 2;
    ctx.fill();
    ctx.stroke();
  });
}

function drawPoints() {
  if (state.mode === 'preview') return;
  const points = state.room.points || {};
  Object.entries(points).forEach(([id, point]) => {
    ctx.fillStyle = id === state.selectedPoint ? '#ef4444' : '#16a34a';
    ctx.strokeStyle = 'white';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(point.x, point.y, 8, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = 'white';
    ctx.font = '12px sans-serif';
    ctx.fillText(id, point.x + 12, point.y - 10);
  });
}

function drawSelectedHandles() {
  if (state.mode === 'points' || state.mode === 'preview') return;
  const polygon = getRoomPolygon();
  if (!Array.isArray(polygon)) return;
  polygon.forEach((vertex, index) => {
    ctx.fillStyle = index === state.selectedVertex ? '#ef4444' : '#ffffff';
    ctx.strokeStyle = '#000000';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(vertex.x, vertex.y, 6, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  });
}

function getCanvasPosition(evt) {
  const rect = canvas.getBoundingClientRect();
  const scale = state.viewScale || 1;
  const x = (evt.clientX - rect.left) * (canvas.width / rect.width) / devicePixelRatio / scale;
  const y = (evt.clientY - rect.top) * (canvas.height / rect.height) / devicePixelRatio / scale;
  return { x, y };
}

function findVertexAt(point) {
  const polygon = getRoomPolygon();
  if (!Array.isArray(polygon)) return null;
  return polygon.findIndex((vertex) => pointNear(vertex, point, 10));
}

function findPointAt(point) {
  if (!state.room) return null;
  return Object.entries(state.room.points || {}).find(([, pt]) => pointNear(pt, point, 10));
}

function distanceToSegment(point, a, b) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  if (dx === 0 && dy === 0) {
    return Math.hypot(point.x - a.x, point.y - a.y);
  }
  const t = ((point.x - a.x) * dx + (point.y - a.y) * dy) / (dx * dx + dy * dy);
  const clamped = Math.max(0, Math.min(1, t));
  const closestX = a.x + clamped * dx;
  const closestY = a.y + clamped * dy;
  return Math.hypot(point.x - closestX, point.y - closestY);
}

function insertVertexIntoPolygon(point, polygon) {
  if (!Array.isArray(polygon) || polygon.length < 2) {
    polygon.push(point);
    return polygon.length - 1;
  }
  let bestIndex = 0;
  let bestDistance = Infinity;
  for (let i = 0; i < polygon.length; i += 1) {
    const next = polygon[(i + 1) % polygon.length];
    const distance = distanceToSegment(point, polygon[i], next);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i + 1;
    }
  }
  polygon.splice(bestIndex, 0, point);
  return bestIndex;
}

// Regions are axis-aligned rectangles stored as 4 corners. Dragging one corner
// moves the two adjacent corners along their shared edges so the shape stays a
// rectangle (the diagonally-opposite corner is the fixed anchor).
function moveRectangleCorner(area, index, nx, ny) {
  if (!Array.isArray(area) || area.length !== 4) {
    area[index] = { x: nx, y: ny };
    return;
  }
  const old = area[index];
  for (const n of [(index + 1) % 4, (index + 3) % 4]) {
    if (area[n].x === old.x) area[n] = { x: nx, y: area[n].y };
    if (area[n].y === old.y) area[n] = { x: area[n].x, y: ny };
  }
  area[index] = { x: nx, y: ny };
}

function pointInPolygon(pt, poly) {
  if (!Array.isArray(poly) || poly.length === 0) return false;
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const xi = poly[i].x, yi = poly[i].y;
    const xj = poly[j].x, yj = poly[j].y;
    const intersect = ((yi > pt.y) !== (yj > pt.y)) && (pt.x < (xj - xi) * (pt.y - yi) / (yj - yi + 0.0) + xi);
    if (intersect) inside = !inside;
  }
  return inside;
}

function handlePointerDown(evt) {
  if (!state.room) return;
  const pos = getCanvasPosition(evt);

  // Regions: click+drag to create rectangle, or select an existing region
  if (state.mode === 'regions') {
    const regions = state.room.regions || {};
    // Prefer vertex selection on existing regions first so clicking an existing corner does not start a new rectangle.
    for (const [rid, region] of Object.entries(regions)) {
      const area = region?.area || [];
      if (!Array.isArray(area)) continue;
      const vertexIndex = area.findIndex((vertex) => pointNear(vertex, pos, 10));
      if (vertexIndex !== -1) {
        const vertex = area[vertexIndex];
        state.selectedEntity = rid;
        state.selectedVertex = vertexIndex;
        state.dragTarget = { type: 'vertex', index: vertexIndex };
        state.dragOffset = { x: pos.x - vertex.x, y: pos.y - vertex.y };
        updateUI();
        return;
      }
    }

    for (const [rid, region] of Object.entries(regions)) {
      const area = region?.area || [];
      if (pointInPolygon(pos, area)) {
        state.selectedEntity = rid;
        state.selectedVertex = null;
        updateUI();
        return;
      }
    }

    // not over an existing region: begin rectangle creation
    state.dragTarget = { type: 'region-create', start: { x: pos.x, y: pos.y } };
    state.tempRegion = { start: { x: pos.x, y: pos.y }, current: { x: pos.x, y: pos.y } };
    setStatus('Drag to define region rectangle');
    draw();
    return;
  }

  // Layer hit-test only in 'layers' mode, so a click over the (room-sized)
  // background doesn't swallow point/vertex selection in the other modes.
  if (state.mode === 'layers') {
    const layers = (state.room.background?.layers || []).slice().reverse();
    for (const layer of layers) {
      if (!layer.image) continue;
      const img = state.imageCache.get(layer.image);
      if (!img || !img.complete) continue;
      const { x: ox, y: oy } = layerOrigin(layer);
      if (pos.x >= ox && pos.x <= ox + img.naturalWidth && pos.y >= oy && pos.y <= oy + img.naturalHeight) {
        selectLayer(layer.id);
        state.dragTarget = { type: 'layer', id: layer.id };
        state.dragOffset = { x: pos.x - ox, y: pos.y - oy };
        updateUI();
        return;
      }
    }
  }

  if (state.mode === 'points') {
    const found = findPointAt(pos);
    if (found) {
      const [id] = found;
      state.selectedPoint = id;
      state.dragTarget = { type: 'point', id };
      updateUI();
      return;
    }
    return;
  }

  if (state.addVertexMode) {
    const polygon = getRoomPolygon();
    if (Array.isArray(polygon)) {
      const insertedIndex = insertVertexIntoPolygon({ x: Math.round(pos.x), y: Math.round(pos.y) }, polygon);
      setRoomPolygon(polygon);
      state.addVertexMode = false;
      state.selectedVertex = insertedIndex;
      setStatus('Vertex added.');
      updateUI();
      return;
    }
  }

  const vertexIndex = findVertexAt(pos);
  if (vertexIndex !== -1) {
    if (state.deleteVertexMode) {
      const polygon = getRoomPolygon();
      polygon.splice(vertexIndex, 1);
      setRoomPolygon(polygon);
      state.deleteVertexMode = false;
      deleteVertexButton.textContent = 'Delete vertex';
      setStatus('Vertex deleted.');
      updateUI();
      return;
    }
    state.selectedVertex = vertexIndex;
    state.dragTarget = { type: 'vertex', index: vertexIndex };
    state.dragOffset = { x: pos.x - getRoomPolygon()[vertexIndex].x, y: pos.y - getRoomPolygon()[vertexIndex].y };
    updateUI();
    return;
  }

  if (!state.selectedEntity) {
    const entities = getEntities();
    if (entities.length > 0) {
      state.selectedEntity = entities[0].id;
      updateUI();
    }
  }
  // Only insert vertices for non-rectangle polygons
  if (state.mode !== 'regions') {
    const polygon = getRoomPolygon();
    if (Array.isArray(polygon) && polygon.length > 0) {
      const insertedIndex = insertVertexIntoPolygon({ x: Math.round(pos.x), y: Math.round(pos.y) }, polygon);
      setRoomPolygon(polygon);
      state.selectedVertex = insertedIndex;
      setStatus('Vertex inserted into polygon.');
      updateUI();
    }
  }
}

function handlePointerMove(evt) {
  if (!state.room || !state.dragTarget) return;
  const pos = getCanvasPosition(evt);
  if (state.dragTarget.type === 'point') {
    setPoint({ x: Math.round(pos.x), y: Math.round(pos.y) });
    draw();
    return;
  }
  if (state.dragTarget.type === 'vertex') {
    const polygon = getRoomPolygon();
    if (!Array.isArray(polygon) || state.dragTarget.index == null) return;
    const nx = Math.round(pos.x - state.dragOffset.x);
    const ny = Math.round(pos.y - state.dragOffset.y);
    if (state.mode === 'regions') {
      moveRectangleCorner(polygon, state.dragTarget.index, nx, ny);
    } else {
      polygon[state.dragTarget.index] = { x: nx, y: ny };
    }
    setRoomPolygon(polygon);
    draw();
  }
  if (state.dragTarget.type === 'layer') {
    const layers = state.room.background?.layers || [];
    const layer = layers.find((l) => l.id === state.dragTarget.id);
    if (!layer) return;
    setLayerOrigin(layer,
                   Math.round(pos.x - state.dragOffset.x),
                   Math.round(pos.y - state.dragOffset.y));
    updateLayersList();
    updateLayerInfoUI();
    draw();
    return;
  }
  if (state.dragTarget.type === 'region-create') {
    state.tempRegion = state.tempRegion || { start: state.dragTarget.start, current: { x: pos.x, y: pos.y } };
    state.tempRegion.current = { x: pos.x, y: pos.y };
    draw();
    return;
  }
}

function handlePointerUp() {
  if (state.dragTarget && state.dragTarget.type === 'layer') {
    // keep selection, but clear drag state
    state.dragTarget = null;
    state.dragOffset = null;
    setStatus('Layer moved.');
    return;
  }
  if (state.dragTarget && state.dragTarget.type === 'region-create') {
    // finalize region rectangle
    const temp = state.tempRegion;
    if (temp) {
      const sx = Math.min(temp.start.x, temp.current.x);
      const sy = Math.min(temp.start.y, temp.current.y);
      const ex = Math.max(temp.start.x, temp.current.x);
      const ey = Math.max(temp.start.y, temp.current.y);
      const w = ex - sx;
      const h = ey - sy;
      if (w >= 1 && h >= 1) {
        const id = prompt('Region id:');
        if (id) {
          state.room.regions = state.room.regions || {};
          state.room.regions[id] = { area: [ { x: sx, y: sy }, { x: ex, y: sy }, { x: ex, y: ey }, { x: sx, y: ey } ], z: 0, states: {}, initial: '' };
          state.selectedEntity = id;
          setStatus('Region created.');
          updateUI();
        }
      }
    }
    state.tempRegion = null;
    state.dragTarget = null;
    return;
  }
  state.dragTarget = null;
  state.dragOffset = null;
}

function changeMode(evt) {
  state.mode = evt.target.value;
  state.selectedEntity = null;
  state.selectedPoint = null;
  state.selectedVertex = null;
  updateUI();
}

function changeEntity(evt) {
  state.selectedEntity = evt.target.value;
  state.selectedPoint = state.mode === 'points' ? evt.target.value : null;
  state.selectedVertex = null;
  updateUI();
}

function addEntity() {
  if (!state.room) return;
  const mode = state.mode;
  if (mode === 'points') {
    const id = prompt('New point ID:');
    if (!id) return;
    state.room.points = state.room.points || {};
    if (state.room.points[id]) {
      alert('A point with that id already exists.');
      return;
    }
    state.room.points[id] = { x: 100, y: 100 };
    state.selectedPoint = id;
    state.selectedEntity = id;
  } else if (mode === 'walkable') {
    alert('Walkable is a single polygon and cannot be added.');
    return;
  } else {
    const id = prompt(`New ${mode.slice(0, -1)} id:`);
    if (!id) return;
    if (mode === 'zones') {
      state.room.zones = state.room.zones || [];
      if (state.room.zones.some((zone) => zone.id === id)) {
        alert('A zone with that id already exists.');
        return;
      }
      state.room.zones.push({ id, polygon: [] });
    } else if (mode === 'regions') {
      state.room.regions = state.room.regions || {};
      if (state.room.regions[id]) {
        alert('A region with that id already exists.');
        return;
      }
      state.room.regions[id] = {
        area: [{ x: 100, y: 100 }, { x: 300, y: 100 }, { x: 300, y: 250 }, { x: 100, y: 250 }],
        z: 0,
        states: {},
        initial: '',
      };
    } else if (mode === 'hotspots') {
      state.room.hotspots = state.room.hotspots || {};
      if (state.room.hotspots[id]) {
        alert('A hotspot with that id already exists.');
        return;
      }
      state.room.hotspots[id] = { name: id, area: [], affordances: ['look_at'] };
    }
    state.selectedEntity = id;
  }
  updateUI();
}

function removeEntity() {
  if (!state.room || !state.selectedEntity) return;
  const mode = state.mode;
  if (mode === 'points') {
    delete state.room.points[state.selectedEntity];
    state.selectedPoint = null;
    state.selectedEntity = null;
  } else if (mode === 'walkable') {
    alert('Walkable cannot be deleted.');
    return;
  } else if (mode === 'zones') {
    state.room.zones = (state.room.zones || []).filter((zone) => zone.id !== state.selectedEntity);
    state.selectedEntity = null;
  } else if (mode === 'regions') {
    delete state.room.regions[state.selectedEntity];
    state.selectedEntity = null;
  } else if (mode === 'hotspots') {
    delete state.room.hotspots[state.selectedEntity];
    state.selectedEntity = null;
  }
  updateUI();
}

function toggleAddVertex() {
  if (state.mode === 'regions') {
    setStatus('Regions are rectangles; vertices cannot be added.', true);
    return;
  }
  state.addVertexMode = !state.addVertexMode;
  addVertexButton.textContent = state.addVertexMode ? 'Cancel add' : 'Add vertex';
  deleteVertexButton.textContent = 'Delete vertex';
  state.deleteVertexMode = false;
  setStatus(state.addVertexMode ? 'Click canvas to place a new vertex.' : '');
}

function toggleDeleteVertex() {
  if (state.mode === 'regions') {
    setStatus('Regions are rectangles; vertices cannot be deleted.', true);
    return;
  }
  state.deleteVertexMode = !state.deleteVertexMode;
  deleteVertexButton.textContent = state.deleteVertexMode ? 'Cancel delete' : 'Delete vertex';
  addVertexButton.textContent = 'Add vertex';
  state.addVertexMode = false;
  setStatus(state.deleteVertexMode ? 'Click a vertex to delete it.' : '');
}

function deleteVertex() {
  if (!state.room || !state.selectedEntity) return;
  if (state.mode === 'regions') {
    setStatus('Regions are rectangles; vertices cannot be deleted.', true);
    return;
  }
  const polygon = getRoomPolygon();
  if (!Array.isArray(polygon) || state.selectedVertex == null) return;
  polygon.splice(state.selectedVertex, 1);
  setRoomPolygon(polygon);
  state.selectedVertex = null;
  updateUI();
}

function addPoint() {
  if (!state.room) return;
  const id = prompt('New point ID:');
  if (!id) return;
  state.room.points = state.room.points || {};
  if (state.room.points[id]) {
    alert('A point with that id already exists.');
    return;
  }
  state.room.points[id] = { x: 100, y: 100 };
  state.selectedPoint = id;
  state.selectedEntity = id;
  updateUI();
}

function deletePoint() {
  if (!state.room || !state.selectedPoint) return;
  delete state.room.points[state.selectedPoint];
  state.selectedPoint = null;
  state.selectedEntity = null;
  updateUI();
}

async function saveRoom() {
  if (!state.room) return;
  const patch = {
    background: state.room.background || {},
    geometry: {
      walkable: state.room.walkable || [],
      obstacles: state.room.obstacles || [],
      points: state.room.points || {},
      zones: state.room.zones || [],
      regions: state.room.regions || {},
      hotspots: state.room.hotspots || {},
    },
  };
  try {
    const res = await fetch('/api/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ patch }),
    });
    const result = await res.json();
    if (!result.ok) {
      throw new Error(result.error || 'Save failed');
    }
    setStatus('Saved successfully.');
  } catch (err) {
    setStatus(err.message, true);
  }
}

reloadButton.addEventListener('click', async () => {
  setStatus('Reloading...');
  await loadRoom();
  setStatus('Room reloaded.');
});
saveButton.addEventListener('click', saveRoom);
// layer controls
document.addEventListener('click', (evt) => {
  const target = evt.target;
  const layerEl = target.closest && target.closest('.layer-item');
  if (layerEl && layersList.contains(layerEl)) {
    const id = layerEl.getAttribute('data-layer-id');
    if (id) selectLayer(id);
  }
});

document.getElementById('layer-apply').addEventListener('click', applyLayerPosition);
document.getElementById('layer-reset').addEventListener('click', resetLayerPosition);
snapshotRegionButton.addEventListener('click', snapshotRegion);
modeSelect.addEventListener('change', changeMode);
entitySelect.addEventListener('change', changeEntity);
addEntityButton.addEventListener('click', addEntity);
removeEntityButton.addEventListener('click', removeEntity);
addVertexButton.addEventListener('click', toggleAddVertex);
deleteVertexButton.addEventListener('click', toggleDeleteVertex);
addPointButton.addEventListener('click', addPoint);
deletePointButton.addEventListener('click', deletePoint);
canvas.addEventListener('pointerdown', handlePointerDown);
canvas.addEventListener('pointermove', (evt) => {
  if (evt.buttons === 1) handlePointerMove(evt);
});
canvas.addEventListener('pointerup', handlePointerUp);
window.addEventListener('resize', draw);

async function saveAsset(filename, blob) {
  // read blob as base64
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        const base64 = reader.result.split(',')[1];
        const res = await fetch('/api/save_asset', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ filename, data: base64 }),
        });
        const j = await res.json();
        if (!res.ok) throw new Error(j.error || 'save failed');
        resolve(j);
      } catch (err) {
        reject(err);
      }
    };
    reader.onerror = () => reject(new Error('Failed reading blob'));
    reader.readAsDataURL(blob);
  });
}

function boundingBoxForRegion(region) {
  if (!region || !Array.isArray(region.area) || region.area.length === 0) return null;
  const xs = region.area.map((p) => p.x);
  const ys = region.area.map((p) => p.y);
  const minX = Math.min(...xs);
  const minY = Math.min(...ys);
  const maxX = Math.max(...xs);
  const maxY = Math.max(...ys);
  return { x: minX, y: minY, w: maxX - minX, h: maxY - minY };
}

async function snapshotRegion() {
  if (!state.room || state.mode !== 'regions' || !state.selectedEntity) {
    alert('Select a region first');
    return;
  }
  const region = (state.room.regions || {})[state.selectedEntity];
  if (!region) { alert('Region not found'); return; }
  const bbox = boundingBoxForRegion(region);
  if (!bbox || bbox.w === 0 || bbox.h === 0) { alert('Region has no area'); return; }

  // Snapshot the *selected* background layer only, preserving its alpha. Region
  // state images are transparent overlays drawn at the region's top-left
  // (see lib/src/pnc/room_renderer.cpp), so we must not flatten the base in.
  const layers = Array.isArray(state.room.background?.layers) ? state.room.background.layers : [];
  const layer = layers.find((l) => l.id === state.selectedLayerId);
  if (!layer) { alert('Select a source background layer first (click one in the Background panel).'); return; }
  const img = state.imageCache.get(layer.image);
  if (!img || !img.complete || !img.naturalWidth) { alert(`Layer image not loaded: ${layer.image}`); return; }

  // A region must have at least one named state, with `initial` pointing at one
  // of them (see docs/sources/design/06-data-formats.md). Capture the snapshot
  // into a named state rather than a placeholder key.
  const stateName = prompt('State name for this snapshot:', region.initial || 'default');
  if (!stateName) return;

  // Draw only the selected layer into a region-sized canvas, offset so the
  // region's top-left maps to (0,0). The canvas starts transparent, so pixels
  // the layer doesn't cover (and the layer's own transparent pixels) stay clear.
  const crop = document.createElement('canvas');
  crop.width = Math.max(1, bbox.w);
  crop.height = Math.max(1, bbox.h);
  const cctx = crop.getContext('2d');
  const { x: ox, y: oy } = layerOrigin(layer);
  cctx.drawImage(img, ox - bbox.x, oy - bbox.y);

  // convert to blob
  const blob = await new Promise((res) => crop.toBlob(res, 'image/png'));
  if (!blob) { alert('Failed to capture image'); return; }

  const filename = `regions/${encodeURIComponent(state.room.id || 'room')}_${encodeURIComponent(state.selectedEntity)}_${encodeURIComponent(stateName)}.png`;
  try {
    await saveAsset(filename, blob);
    // attach the saved filename to the named region state and ensure `initial`
    // names a real state.
    state.room.regions = state.room.regions || {};
    const target = state.room.regions[state.selectedEntity];
    target.states = target.states || {};
    target.states[stateName] = filename;
    if (!target.initial) target.initial = stateName;
    // persist entire regions mapping via /api/save
    const patch = { geometry: { regions: state.room.regions } };
    const res = await fetch('/api/save', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ patch }) });
    const jr = await res.json();
    if (!jr.ok) throw new Error(jr.error || 'save failed');
    setStatus(`Region snapshot saved to state "${stateName}".`);
    updateUI();
  } catch (err) {
    setStatus(err.message || String(err), true);
  }
}

updateModeOptions();
loadInfo().then(loadRoom).catch((err) => setStatus(err.message, true));
