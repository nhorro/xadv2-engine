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
  viewOrigin: { x: 0, y: 0 },
  workspaceInitialized: false,
  assets: [], // data-tree assets expressed relative to the active room directory
  avatarCatalog: [],
  avatarCatalogErrors: [],
  avatarPreviews: [], // editor-only; deliberately excluded from saveRoom()
  nextAvatarPreviewId: 1,
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
const roomFileSelect = document.getElementById('room-file');
const openRoomButton = document.getElementById('open-room');
const addEntityButton = document.getElementById('add-entity');
const removeEntityButton = document.getElementById('remove-entity');
const addVertexButton = document.getElementById('add-vertex');
const deleteVertexButton = document.getElementById('delete-vertex');
const addPointButton = document.getElementById('add-point');
const deletePointButton = document.getElementById('delete-point');
const snapshotRegionButton = document.getElementById('snapshot-region');
const snapshotSource = document.getElementById('snapshot-source');
const hotspotProps = document.getElementById('hotspot-props');
const hotspotIdInput = document.getElementById('hotspot-id');
const hotspotRenameButton = document.getElementById('hotspot-rename');
const hotspotNameInput = document.getElementById('hotspot-name');
const hotspotAffordances = document.getElementById('hotspot-affordances');
const avatarsPanel = document.getElementById('avatars-panel');
const avatarCharacterSelect = document.getElementById('avatar-character');
const avatarsList = document.getElementById('avatars-list');
const avatarXInput = document.getElementById('avatar-x');
const avatarYInput = document.getElementById('avatar-y');
const avatarScaleInput = document.getElementById('avatar-scale');
const avatarValues = document.getElementById('avatar-values');

const WORKSPACE_MARGIN = 160;
const WORKSPACE_GROWTH_STEP = 256;
const MAX_CANVAS_DEVICE_DIMENSION = 8192;
let pendingPointerMove = null;
let pointerMoveFrame = null;

// The game's verb set, mirroring `verbs:` in strings/<lang>.yaml. Used to offer
// affordance checkboxes; any verb already on a hotspot is shown too (see
// affordanceVerbs), so a custom/unknown verb is never hidden or dropped.
const KNOWN_VERBS = ['look_at', 'talk_to', 'pick_up', 'use', 'give', 'open', 'close', 'push', 'pull'];

const modeOptions = ['walkable', 'obstacles', 'zones', 'regions', 'hotspots', 'points', 'layers', 'objects', 'avatars', 'preview'];
const entityPrefix = {
  zones: 'zone',
  regions: 'region',
  hotspots: 'hotspot',
  points: 'point',
};

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
  assetBase.textContent =
    `data: ${state.info.data_path}\nrooms: ${state.info.rooms_path}`;
}

async function loadAvatarCatalog() {
  try {
    const data = await fetchJson('/api/avatar-catalog');
    state.avatarCatalog = Array.isArray(data.characters) ? data.characters : [];
    state.avatarCatalogErrors = Array.isArray(data.errors) ? data.errors : [];
  } catch (err) {
    state.avatarCatalog = [];
    state.avatarCatalogErrors = [err.message];
  }
  populateAvatarCharacters();
}

async function loadRoom() {
  state.room = await fetchJson('/api/room');
  state.workspaceInitialized = false;
  updateRoomInfo();
  state.selectedEntity = null;
  state.selectedPoint = null;
  state.selectedLayerId = null;
  state.addVertexMode = false;
  state.deleteVertexMode = false;
  state.selectedVertex = null;
  seedAvatarPreviews();
  updateUI();
  draw();
  if (!state.room || !state.room.id) {
    setStatus('No room loaded — pick a room file and click Open.');
  }
}

// Populate the room-file dropdown from the configured rooms directory, keeping the
// currently-open file selected.
async function loadRooms() {
  try {
    const data = await fetchJson('/api/rooms');
    const rooms = data.rooms || [];
    if (!rooms.length) {
      roomFileSelect.innerHTML = '<option value="">(no rooms found)</option>';
      return;
    }
    roomFileSelect.innerHTML = rooms.map((r) => `<option value="${r}">${r}</option>`).join('');
    if (data.current) roomFileSelect.value = data.current;
  } catch (err) {
    setStatus(err.message, true);
  }
}

async function openSelectedRoom() {
  const name = roomFileSelect.value;
  if (!name) {
    setStatus('No room file selected.', true);
    return;
  }
  try {
    const res = await fetch('/api/open', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ room: name }),
    });
    const result = await res.json();
    if (!result.ok) throw new Error(result.error || 'Open failed');
    state.imageCache.clear();
    await loadAvatarCatalog();
    await loadRoom();
    await loadRooms();
    await loadAssets();
    setStatus(`Opened ${name}.`);
  } catch (err) {
    setStatus(err.message, true);
  }
}

function updateModeOptions() {
  modeSelect.innerHTML = modeOptions.map((mode) => `<option value="${mode}">${mode}</option>`).join('');
  modeSelect.value = state.mode;
}

function getEntities() {
  if (!state.room) return [];
  if (state.mode === 'walkable') return [{ id: 'walkable', label: 'walkable' }];
  // Obstacles are unnamed polygons; key each by its array index.
  if (state.mode === 'obstacles') return (state.room.obstacles || []).map((_, i) => ({ id: String(i), label: `obstacle ${i}` }));
  if (state.mode === 'points') return Object.keys(state.room.points || {}).map((id) => ({ id, label: id }));
  if (state.mode === 'zones') return (state.room.zones || []).map((zone) => ({ id: zone.id, label: zone.id }));
  if (state.mode === 'regions') return Object.keys(state.room.regions || {}).map((id) => ({ id, label: id }));
  if (state.mode === 'hotspots') return Object.keys(state.room.hotspots || {}).map((id) => ({ id, label: id }));
  if (state.mode === 'objects') return Object.keys(state.room.objects || {}).map((id) => ({ id, label: id }));
  if (state.mode === 'avatars') {
    return state.avatarPreviews.map((preview) => ({
      id: preview.uid,
      label: `${preview.role} · ${preview.characterId}`,
    }));
  }
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
  // Points track selection via selectedPoint (drawing, drag, delete); keep it in
  // lockstep with the entity selection so the dropdown, canvas clicks, Remove and
  // Delete point all act on the same point.
  if (state.mode === 'points') {
    state.selectedPoint = state.selectedEntity;
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

// Uniform render scale (engine `scale:`, aspect always preserved). Absent = 1.0
// (native size); we omit it on save when it rounds back to native.
function layerScale(layer) {
  const s = Number(layer.scale);
  return Number.isFinite(s) && s > 0 ? s : 1;
}

function setLayerScale(layer, scale) {
  const s = Math.max(0.05, scale);
  if (Math.abs(s - 1) < 1e-3) {
    delete layer.scale;
  } else {
    layer.scale = Math.round(s * 1000) / 1000;
  }
}

// Native (unscaled) image size from the cache, or null until the image loads.
function layerNativeSize(layer) {
  if (!layer || !layer.image) return null;
  const img = state.imageCache.get(layer.image);
  if (!img || !img.complete || !img.naturalWidth) return null;
  return { w: img.naturalWidth, h: img.naturalHeight };
}

// Drawn rect in world space: [origin, origin + native size × scale). Null until
// the image loads (we need the native size to know the on-screen extent).
function layerRect(layer) {
  const native = layerNativeSize(layer);
  if (!native) return null;
  const { x, y } = layerOrigin(layer);
  const s = layerScale(layer);
  return { x, y, w: native.w * s, h: native.h * s };
}

// The layer's base anchor: the bottom-centre of its drawn rect (its floor line).
function layerBase(layer) {
  const r = layerRect(layer);
  return r ? { x: r.x + r.w / 2, y: r.y + r.h } : { x: 0, y: 0 };
}

// Rescale about the base (bottom-centre) so a piece of furniture stays grounded:
// its floor line and horizontal centre hold while it grows/shrinks. Optionally
// pass a fixed base (world point) to hold during a drag.
function rescaleLayerAboutBase(layer, newScale, base) {
  const native = layerNativeSize(layer);
  if (!native) return;
  const s = Math.max(0.05, newScale);
  const anchor = base || layerBase(layer);
  const w = native.w * s;
  const h = native.h * s;
  setLayerScale(layer, s);
  setLayerOrigin(layer, Math.round(anchor.x - w / 2), Math.round(anchor.y - h));
}

function updateLayersList() {
  const background = state.room.background || {};
  const layers = Array.isArray(background.layers) ? background.layers : [];
  layersList.innerHTML = layers.map((layer) => {
    const { x, y } = layerOrigin(layer);
    const s = layerScale(layer);
    return `
      <div class="layer-item" data-layer-id="${layer.id || ''}">
        <strong>${layer.id || 'unnamed'}</strong>
        <div>${layer.image || ''}</div>
        <div>z: ${layer.z ?? ''} interactive: ${layer.interactive ?? false}</div>
        <div>pos: ${x}, ${y} · scale: ${s === 1 ? '1 (native)' : s}</div>
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
  const layerScaleInput = document.getElementById('layer-scale');
  const layerZ = document.getElementById('layer-z');
  const clear = () => {
    layerX.value = '';
    layerY.value = '';
    layerScaleInput.value = '';
    layerZ.value = '';
  };
  if (!state.room || !state.selectedLayerId) return clear();
  const layers = state.room.background?.layers || [];
  const layer = layers.find((l) => l.id === state.selectedLayerId);
  if (!layer) return clear();
  const origin = layerOrigin(layer);
  layerX.value = origin.x;
  layerY.value = origin.y;
  layerScaleInput.value = layerScale(layer);
  layerZ.value = layer.z ?? 0;
}

function selectedLayer() {
  if (!state.room || !state.selectedLayerId) return null;
  const layers = state.room.background?.layers || [];
  return layers.find((l) => l.id === state.selectedLayerId) || null;
}

function applyLayerPosition() {
  const layer = selectedLayer();
  if (!layer) return;
  const nx = parseInt(document.getElementById('layer-x').value || '0', 10);
  const ny = parseInt(document.getElementById('layer-y').value || '0', 10);
  setLayerOrigin(layer, nx, ny);
  updateLayersList();
  draw();
}

function resetLayerPosition() {
  const layer = selectedLayer();
  if (!layer) return;
  setLayerOrigin(layer, 0, 0);
  updateLayersList();
  updateLayerInfoUI();
  draw();
}

function applyLayerScale() {
  const layer = selectedLayer();
  if (!layer) return;
  if (!layerNativeSize(layer)) {
    setStatus('Layer image not loaded yet; cannot scale.', true);
    return;
  }
  const next = parseFloat(document.getElementById('layer-scale').value || '1');
  if (!Number.isFinite(next) || next <= 0) {
    setStatus('Scale must be a positive number.', true);
    return;
  }
  rescaleLayerAboutBase(layer, next);
  updateLayersList();
  updateLayerInfoUI();
  draw();
  setStatus(`Layer scaled to ${layerScale(layer)}× (base fixed).`);
}

function resetLayerSize() {
  const layer = selectedLayer();
  if (!layer) return;
  rescaleLayerAboutBase(layer, 1);
  updateLayersList();
  updateLayerInfoUI();
  draw();
  setStatus('Layer reset to native size.');
}

function applyLayerZ() {
  const layer = selectedLayer();
  if (!layer) return;
  const z = parseFloat(document.getElementById('layer-z').value || '0');
  if (!Number.isFinite(z)) {
    setStatus('z must be a number.', true);
    return;
  }
  layer.z = Math.round(z);
  updateLayersList();
  draw();
  setStatus(`Layer z set to ${layer.z}.`);
}

// Sort a furniture layer by its floor line: set z to the rect's bottom edge in
// world space — a fixed value (unlike an object's per-frame `z: auto`), which is
// exactly right for static furniture: the avatar passes behind it above the line,
// in front below it. Re-click after moving/resizing the layer to re-snap.
function setLayerZBase() {
  const layer = selectedLayer();
  if (!layer) return;
  const rect = layerRect(layer);
  if (!rect) {
    setStatus('Layer image not loaded yet; cannot derive base z.', true);
    return;
  }
  layer.z = Math.round(rect.y + rect.h);
  document.getElementById('layer-z').value = layer.z;
  updateLayersList();
  draw();
  setStatus(`Layer z set to its base (${layer.z}).`);
}

// --- Objects (#147): static room objects (sprite + position + scale/z/baseline).
// Mirrors the layers mode — drawn on the canvas, selected/dragged, edited via the
// Objects panel. `sprite` is the engine YAML key; `scale` (default 1) is uniform.
function objectsMap() {
  return (state.room && state.room.objects) || {};
}
function objectScale(obj) {
  const s = Number(obj.scale);
  return Number.isFinite(s) && s > 0 ? s : 1;
}
function objectNativeSize(obj) {
  if (!obj || !obj.sprite) return null;
  const img = state.imageCache.get(obj.sprite);
  if (!img || !img.complete || !img.naturalWidth) return null;
  return { w: img.naturalWidth, h: img.naturalHeight };
}
// Drawn rect in world space: [position, position + native size × scale). Falls
// back to a zero-size rect at the position until the sprite image loads.
function objectRect(obj) {
  const pos = obj.position || { x: 0, y: 0 };
  const native = objectNativeSize(obj);
  const s = objectScale(obj);
  if (!native) return { x: pos.x, y: pos.y, w: 0, h: 0 };
  return { x: pos.x, y: pos.y, w: native.w * s, h: native.h * s };
}
function selectedObject() {
  const objs = objectsMap();
  return state.selectedEntity && objs[state.selectedEntity] ? objs[state.selectedEntity] : null;
}
function selectObject(id) {
  state.selectedEntity = id;
  entitySelect.value = id;
  updateObjectsPanel();
  draw();
}

function updateObjectsList() {
  const list = document.getElementById('objects-list');
  if (!list) return;
  const objs = objectsMap();
  list.innerHTML = Object.entries(objs).map(([id, obj]) => {
    const pos = obj.position || { x: 0, y: 0 };
    const sel = id === state.selectedEntity ? ' selected' : '';
    return `<div class="layer-item${sel}" data-object-id="${id}">
        <strong>${id}</strong>
        <div>${obj.sprite || '(no sprite)'}</div>
        <div>pos: ${pos.x}, ${pos.y} · scale: ${objectScale(obj)}</div>
      </div>`;
  }).join('');
}

function populateObjectSprites(current) {
  const sel = document.getElementById('object-sprite');
  if (!sel) return;
  const imgs = (state.assets || []).filter((p) => /\.(png|jpg|jpeg)$/i.test(p));
  const opts = ['<option value="">(choose sprite)</option>'].concat(
    imgs.map((p) => `<option value="${p}"${p === current ? ' selected' : ''}>${p}</option>`)
  );
  // Keep the current value even if the asset scan missed it, so editing other
  // fields doesn't silently drop the sprite.
  if (current && !imgs.includes(current)) {
    opts.push(`<option value="${current}" selected>${current}</option>`);
  }
  sel.innerHTML = opts.join('');
}

function updateObjectsPanel() {
  const panel = document.getElementById('objects-panel');
  if (panel) panel.style.display = state.mode === 'objects' ? '' : 'none';
  updateObjectsList();
  if (state.mode !== 'objects') return;
  const obj = selectedObject();
  const setVal = (id, v) => {
    const el = document.getElementById(id);
    if (el) el.value = v;
  };
  populateObjectSprites(obj ? obj.sprite || '' : '');
  if (!obj) {
    ['object-x', 'object-y', 'object-scale', 'object-z', 'object-baseline'].forEach((id) => setVal(id, ''));
    return;
  }
  const pos = obj.position || { x: 0, y: 0 };
  setVal('object-x', pos.x);
  setVal('object-y', pos.y);
  setVal('object-scale', objectScale(obj));
  setVal('object-z', obj.z === undefined || obj.z === 'auto' ? '' : obj.z);
  setVal('object-baseline', obj.baseline === undefined ? '' : obj.baseline);
  const vis = document.getElementById('object-visible');
  if (vis) vis.checked = obj.visible !== false;
}

function applyObjectFields() {
  const obj = selectedObject();
  if (!obj) return;
  const val = (id) => {
    const el = document.getElementById(id);
    return el ? String(el.value).trim() : '';
  };
  obj.sprite = val('object-sprite');
  const x = parseInt(val('object-x') || '0', 10);
  const y = parseInt(val('object-y') || '0', 10);
  obj.position = { x: Number.isFinite(x) ? x : 0, y: Number.isFinite(y) ? y : 0 };
  const s = parseFloat(val('object-scale') || '1');
  if (Number.isFinite(s) && s > 0 && Math.abs(s - 1) > 1e-3) {
    obj.scale = Math.round(s * 1000) / 1000;
  } else {
    delete obj.scale; // 1.0 = native; omit it
  }
  const zRaw = val('object-z');
  if (zRaw === '' || zRaw.toLowerCase() === 'auto') {
    delete obj.z; // auto (sort by scaled bottom edge)
  } else {
    const z = parseFloat(zRaw);
    if (Number.isFinite(z)) obj.z = Math.round(z);
  }
  const bRaw = val('object-baseline');
  if (bRaw === '') {
    delete obj.baseline;
  } else {
    const b = parseFloat(bRaw);
    if (Number.isFinite(b)) obj.baseline = Math.round(b);
  }
  const vis = document.getElementById('object-visible');
  if (vis && !vis.checked) obj.visible = false;
  else delete obj.visible; // visible is the default; omit it
  updateObjectsList();
  draw();
  setStatus(`Object "${state.selectedEntity}" updated.`);
}

function drawObject(id, obj) {
  if (!obj.sprite || obj.visible === false) return;
  let img = state.imageCache.get(obj.sprite);
  if (!img) {
    img = new Image();
    img.src = `/assets/${encodeURIComponent(obj.sprite)}`;
    img.onload = () => {
      state.imageCache.set(obj.sprite, img);
      draw();
    };
    img.onerror = () => {
      /* missing sprite: skip silently in the editor */
    };
    state.imageCache.set(obj.sprite, img);
  }
  if (img.complete && img.naturalWidth !== 0) {
    const r = objectRect(obj);
    ctx.drawImage(img, r.x, r.y, r.w, r.h);
    if (state.mode === 'objects' && id === state.selectedEntity) {
      ctx.save();
      ctx.strokeStyle = 'rgba(165, 180, 252, 0.95)';
      ctx.lineWidth = 2;
      ctx.setLineDash([6, 4]);
      ctx.strokeRect(r.x, r.y, r.w, r.h);
      ctx.restore();
    }
  }
}

function drawObjects() {
  for (const [id, obj] of Object.entries(objectsMap())) {
    drawObject(id, obj);
  }
}

function objectDepth(obj) {
  if (Number.isFinite(Number(obj.baseline))) return Number(obj.baseline);
  if (obj.z !== undefined && obj.z !== 'auto' && Number.isFinite(Number(obj.z))) {
    return Number(obj.z);
  }
  const rect = objectRect(obj);
  return rect.y + rect.h;
}

function avatarImage(character) {
  const key = `avatar:${character.image}`;
  let img = state.imageCache.get(key);
  if (!img) {
    img = new Image();
    img.src = `/avatar-assets/${encodeURIComponent(character.image)}`;
    img.onload = () => {
      state.imageCache.set(key, img);
      draw();
    };
    img.onerror = () => {
      setStatus(`Unable to load avatar atlas ${character.image}`, true);
    };
    state.imageCache.set(key, img);
  }
  return img;
}

function drawAvatarPreview(preview) {
  const resolved = avatarPreviewFrame(preview);
  if (!resolved) return;
  const { character, sequence } = resolved;
  const img = avatarImage(character);
  if (!img.complete || img.naturalWidth === 0) return;
  const { rect, pivot, h_mirror: mirrored } = sequence;
  const scale = avatarPreviewScale(preview);
  const x = Number(preview.position?.x) || 0;
  const y = Number(preview.position?.y) || 0;

  ctx.save();
  ctx.translate(x, y);
  ctx.scale(mirrored ? -scale : scale, scale);
  ctx.drawImage(
    img,
    rect.x, rect.y, rect.width, rect.height,
    -pivot.x, -pivot.y, rect.width, rect.height
  );
  ctx.restore();
}

// In avatar/preview modes use the same floor-depth model as the engine, so an
// avatar can be judged against foreground furniture such as a desk.
function drawDepthSortedScene() {
  const entries = [];
  let order = 0;
  for (const layer of state.room.background?.layers || []) {
    if (!layer.image) continue;
    entries.push({
      depth: Number.isFinite(Number(layer.z)) ? Number(layer.z) : order,
      order: order++,
      draw: () => drawLayer(layer),
    });
  }
  for (const [id, obj] of Object.entries(objectsMap())) {
    if (!obj.sprite || obj.visible === false) continue;
    entries.push({
      depth: objectDepth(obj),
      order: order++,
      draw: () => drawObject(id, obj),
    });
  }
  for (const preview of state.avatarPreviews) {
    entries.push({
      depth: Number(preview.position?.y) || 0,
      order: order++,
      draw: () => drawAvatarPreview(preview),
    });
  }
  entries.sort((a, b) => a.depth - b.depth || a.order - b.order);
  entries.forEach((entry) => entry.draw());
}

// --- Avatar previews -------------------------------------------------------
// These are an authoring overlay only. Existing room placements seed the list,
// but preview position/scale never enter the save patch.
function catalogCharacter(id) {
  return state.avatarCatalog.find((character) => character.id === id) || null;
}

function perspectiveScaleAt(y) {
  const perspective = state.room?.perspective;
  const top = perspective?.top;
  const bottom = perspective?.bottom;
  if (!top || !bottom) return 1;
  const topY = Number(top.y);
  const bottomY = Number(bottom.y);
  const topScale = Number(top.scale);
  const bottomScale = Number(bottom.scale);
  if (![topY, bottomY, topScale, bottomScale].every(Number.isFinite)) return 1;
  if (bottomY === topY) return bottomScale;
  const t = Math.max(0, Math.min(1, (y - topY) / (bottomY - topY)));
  return topScale + (bottomScale - topScale) * t;
}

function avatarSequence(character, orientation) {
  if (!character) return null;
  const preferred = orientation ? `stand_${orientation}` : null;
  if (preferred && character.sequences?.[preferred]) return preferred;
  return character.default_sequence;
}

function seedAvatarPreviews() {
  state.avatarPreviews = [];
  state.nextAvatarPreviewId = 1;
  for (const placement of state.room?.avatars || []) {
    const character = catalogCharacter(placement.id);
    if (!character) continue;
    const point = state.room?.points?.[placement.start];
    const position = point
      ? { x: Number(point.x) || 0, y: Number(point.y) || 0 }
      : { x: 100, y: 100 };
    state.avatarPreviews.push({
      uid: `avatar-preview-${state.nextAvatarPreviewId++}`,
      characterId: placement.id,
      role: placement.player ? 'PC' : 'NPC',
      position,
      scale: Math.round(perspectiveScaleAt(position.y) * 1000) / 1000,
      sequence: avatarSequence(character, placement.orientation),
      sourcePoint: placement.start || null,
    });
  }
}

function populateAvatarCharacters() {
  if (!avatarCharacterSelect) return;
  if (!state.avatarCatalog.length) {
    const reason = state.avatarCatalogErrors[0] || 'No animated cast characters found.';
    avatarCharacterSelect.innerHTML = `<option value="">(${reason})</option>`;
    return;
  }
  avatarCharacterSelect.innerHTML = state.avatarCatalog
    .map((character) => `<option value="${character.id}">${character.name} · ${character.id}</option>`)
    .join('');
}

function selectedAvatarPreview() {
  if (state.mode !== 'avatars' || !state.selectedEntity) return null;
  return state.avatarPreviews.find((preview) => preview.uid === state.selectedEntity) || null;
}

function addAvatarPreview(role) {
  const characterId = avatarCharacterSelect.value;
  const character = catalogCharacter(characterId);
  if (!character) {
    setStatus('Choose an animated cast character first.', true);
    return;
  }
  const { width, height } = computeRoomSize();
  const position = { x: Math.round(width / 2), y: Math.round(height * 0.8) };
  const preview = {
    uid: `avatar-preview-${state.nextAvatarPreviewId++}`,
    characterId,
    role,
    position,
    scale: Math.round(perspectiveScaleAt(position.y) * 1000) / 1000,
    sequence: character.default_sequence,
    sourcePoint: null,
  };
  state.avatarPreviews.push(preview);
  state.mode = 'avatars';
  state.selectedEntity = preview.uid;
  updateUI();
  setStatus(`${role} preview added. Drag it by the body or resize handle.`);
}

function removeSelectedAvatarPreview() {
  const preview = selectedAvatarPreview();
  if (!preview) return;
  state.avatarPreviews = state.avatarPreviews.filter((item) => item.uid !== preview.uid);
  state.selectedEntity = null;
  updateUI();
  setStatus('Avatar preview removed (room YAML unchanged).');
}

function avatarPreviewFrame(preview) {
  const character = preview ? catalogCharacter(preview.characterId) : null;
  if (!character) return null;
  const sequence =
    character.sequences?.[preview.sequence] ||
    character.sequences?.[character.default_sequence];
  return sequence ? { character, sequence } : null;
}

function avatarPreviewScale(preview) {
  const scale = Number(preview?.scale);
  return Number.isFinite(scale) && scale > 0 ? scale : 1;
}

function avatarPreviewRect(preview) {
  const resolved = avatarPreviewFrame(preview);
  if (!resolved) return null;
  const { rect, pivot, h_mirror: mirrored } = resolved.sequence;
  const scale = avatarPreviewScale(preview);
  const x = Number(preview.position?.x) || 0;
  const y = Number(preview.position?.y) || 0;
  const left = mirrored
    ? x - (rect.width - pivot.x) * scale
    : x - pivot.x * scale;
  return {
    x: left,
    y: y - pivot.y * scale,
    w: rect.width * scale,
    h: rect.height * scale,
  };
}

function avatarResizeHandle(preview) {
  const rect = avatarPreviewRect(preview);
  return rect ? { x: rect.x + rect.w, y: rect.y } : null;
}

function drawAvatarHandles() {
  if (state.mode !== 'avatars') return;
  const preview = selectedAvatarPreview();
  const rect = preview ? avatarPreviewRect(preview) : null;
  if (!preview || !rect) return;
  const pivot = preview.position;
  const handle = avatarResizeHandle(preview);

  ctx.save();
  ctx.strokeStyle = 'rgba(245, 158, 11, 0.95)';
  ctx.fillStyle = '#f59e0b';
  ctx.lineWidth = 2;
  ctx.setLineDash([6, 4]);
  ctx.strokeRect(rect.x, rect.y, rect.w, rect.h);
  ctx.setLineDash([]);

  ctx.beginPath();
  ctx.arc(handle.x, handle.y, 8, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = '#422006';
  ctx.lineWidth = 1;
  ctx.stroke();

  // The room position is the animation pivot (normally Julia's feet), not the
  // top-left sprite corner. Mark it clearly because this is the value to copy.
  ctx.fillStyle = '#22d3ee';
  ctx.beginPath();
  ctx.moveTo(pivot.x, pivot.y - 9);
  ctx.lineTo(pivot.x - 7, pivot.y + 5);
  ctx.lineTo(pivot.x + 7, pivot.y + 5);
  ctx.closePath();
  ctx.fill();
  ctx.font = '13px sans-serif';
  ctx.fillStyle = '#ffffff';
  ctx.fillText(
    `${preview.role} · ${preview.characterId} · ${avatarPreviewScale(preview)}×`,
    rect.x,
    rect.y - 8
  );
  ctx.restore();
}

function updateAvatarPanel() {
  if (!avatarsPanel) return;
  avatarsPanel.style.display = state.mode === 'avatars' ? '' : 'none';
  if (state.mode !== 'avatars') return;

  avatarsList.innerHTML = state.avatarPreviews.map((preview) => {
    const selected = preview.uid === state.selectedEntity ? ' selected' : '';
    const point = preview.sourcePoint ? ` · point: ${preview.sourcePoint}` : '';
    return `<div class="avatar-item${selected}" data-avatar-id="${preview.uid}">
      <strong>${preview.role} · ${preview.characterId}</strong>
      <div>pos: ${preview.position.x}, ${preview.position.y} · scale: ${avatarPreviewScale(preview)}${point}</div>
    </div>`;
  }).join('');

  const preview = selectedAvatarPreview();
  const disabled = !preview;
  for (const element of [avatarXInput, avatarYInput, avatarScaleInput]) {
    element.disabled = disabled;
  }
  document.getElementById('avatar-apply').disabled = disabled;
  document.getElementById('avatar-copy').disabled = disabled;
  document.getElementById('avatar-remove').disabled = disabled;
  if (!preview) {
    avatarXInput.value = '';
    avatarYInput.value = '';
    avatarScaleInput.value = '';
    avatarValues.textContent = 'No avatar selected.';
    return;
  }
  avatarXInput.value = preview.position.x;
  avatarYInput.value = preview.position.y;
  avatarScaleInput.value = avatarPreviewScale(preview);
  avatarValues.textContent =
    `position: {x: ${preview.position.x}, y: ${preview.position.y}}\n` +
    `scale: ${avatarPreviewScale(preview)}`;
}

function applyAvatarFields() {
  const preview = selectedAvatarPreview();
  if (!preview) return;
  const x = Number(avatarXInput.value);
  const y = Number(avatarYInput.value);
  const scale = Number(avatarScaleInput.value);
  if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(scale) || scale <= 0) {
    setStatus('Avatar x, y, and scale must be valid numbers; scale must be positive.', true);
    return;
  }
  preview.position = { x: Math.round(x), y: Math.round(y) };
  preview.scale = Math.round(Math.max(0.05, scale) * 1000) / 1000;
  updateAvatarPanel();
  draw();
}

async function copyAvatarValues() {
  const preview = selectedAvatarPreview();
  if (!preview) return;
  const text =
    `position: {x: ${preview.position.x}, y: ${preview.position.y}}\n` +
    `scale: ${avatarPreviewScale(preview)}`;
  try {
    await navigator.clipboard.writeText(text);
    setStatus('Avatar position and scale copied.');
  } catch (_) {
    setStatus('Clipboard unavailable; select the values shown in the panel.', true);
  }
}

function updateUI() {
  updateModeOptions();
  updateEntityOptions();
  updateLayersList();
  updateObjectsPanel();
  updateAvatarPanel();
  updateVertexList();
  updateSnapshotSource();
  updateHotspotProps();
  draw();
}

function selectedHotspot() {
  if (state.mode !== 'hotspots' || !state.selectedEntity) return null;
  return (state.room?.hotspots || {})[state.selectedEntity] || null;
}

// Show the id/name editor only for a selected hotspot; ids are the ASCII keys,
// names are the in-game (e.g. Spanish) display text.
function updateHotspotProps() {
  const hotspot = selectedHotspot();
  if (!hotspot) {
    hotspotProps.style.display = 'none';
    return;
  }
  hotspotProps.style.display = '';
  hotspotIdInput.value = state.selectedEntity;
  hotspotNameInput.value = hotspot.name ?? '';
  renderAffordances(hotspot);
}

// The known verbs plus any extra already on this hotspot, in a stable order, so a
// verb the game added (or an older file) still gets a checkbox.
function affordanceVerbs(hotspot) {
  const verbs = [...KNOWN_VERBS];
  for (const v of hotspot.affordances || []) {
    if (!verbs.includes(v)) verbs.push(v);
  }
  return verbs;
}

function renderAffordances(hotspot) {
  const have = new Set(hotspot.affordances || []);
  hotspotAffordances.innerHTML = affordanceVerbs(hotspot)
    .map((v) => `<label style="white-space:nowrap"><input type="checkbox" value="${v}"${
      have.has(v) ? ' checked' : ''
    } /> ${v}</label>`)
    .join('');
}

// Rebuild the affordance list from the checkbox states in display order, so the
// saved order is stable regardless of the click sequence.
function applyAffordances() {
  const hotspot = selectedHotspot();
  if (!hotspot) return;
  const checked = new Set(
    Array.from(hotspotAffordances.querySelectorAll('input:checked')).map((el) => el.value)
  );
  hotspot.affordances = affordanceVerbs(hotspot).filter((v) => checked.has(v));
  setStatus(`'${state.selectedEntity}' affordances: ${hotspot.affordances.join(', ') || '(none)'}`);
}

function applyHotspotName() {
  const hotspot = selectedHotspot();
  if (!hotspot) return;
  hotspot.name = hotspotNameInput.value;
  setStatus(`Name set to "${hotspot.name}".`);
}

// Rename a hotspot's id (its map key), preserving key order. The display name and
// every other field carry over. Note: a Lua verb handler keyed by the old id must
// be renamed by hand — the editor only touches the room YAML.
function renameHotspot() {
  const oldId = state.selectedEntity;
  if (!oldId || state.mode !== 'hotspots') return;
  const newId = (hotspotIdInput.value || '').trim();
  if (!newId || newId === oldId) return;
  if (/\s/.test(newId)) {
    setStatus('Hotspot id cannot contain spaces (it is a key).', true);
    return;
  }
  const hotspots = state.room.hotspots || {};
  if (hotspots[newId]) {
    setStatus(`A hotspot '${newId}' already exists.`, true);
    return;
  }
  const rebuilt = {};
  for (const [key, value] of Object.entries(hotspots)) {
    rebuilt[key === oldId ? newId : key] = value;
  }
  state.room.hotspots = rebuilt;
  state.selectedEntity = newId;
  updateUI();
  setStatus(`Hotspot renamed '${oldId}' → '${newId}'. Update its Lua handler if any.`);
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
  if (state.mode === 'obstacles') {
    return (state.room.obstacles || [])[Number(state.selectedEntity)] || [];
  }
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
  if (state.mode === 'obstacles') {
    const obstacles = state.room.obstacles || (state.room.obstacles = []);
    obstacles[Number(state.selectedEntity)] = polygon;
  }
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

function resizeCanvas(cssWidth, cssHeight, origin, scale) {
  const panel = canvas.parentElement;
  const oldOrigin = state.viewOrigin;
  const w = Math.max(1, Math.round(cssWidth * devicePixelRatio));
  const h = Math.max(1, Math.round(cssHeight * devicePixelRatio));
  // Assigning canvas.width/height reallocates (and clears) the backing store, so
  // only do it on an actual size change — draw() runs this every frame, including
  // during a drag.
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
    canvas.style.width = `${cssWidth}px`;
    canvas.style.height = `${cssHeight}px`;
  }
  ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);

  // Start with world (0,0) at the viewport's top-left. If geometry later grows
  // farther left/up, compensate the scroll position so the visible world does
  // not jump while the user is dragging.
  if (!state.workspaceInitialized) {
    panel.scrollLeft = Math.max(0, -origin.x * scale);
    panel.scrollTop = Math.max(0, -origin.y * scale);
    state.workspaceInitialized = true;
  } else {
    panel.scrollLeft += (oldOrigin.x - origin.x) * scale;
    panel.scrollTop += (oldOrigin.y - origin.y) * scale;
  }
  state.viewOrigin = origin;
}

// Room world bounds, mirroring the engine's compute_room_bounds: the union of
// every layer's rect [origin, origin + native image size × scale), anchored at
// (0,0). Origin is the layer's x/y (also accepts an `origin:{x,y}`), default
// (0,0). Until images load we fall back to a default so the canvas is still
// usable (img.onload re-runs draw(), which recomputes once sizes are known).
function computeRoomSize() {
  const layers = Array.isArray(state.room?.background?.layers) ? state.room.background.layers : [];
  let width = 0;
  let height = 0;
  for (const layer of layers) {
    if (!layer.image) continue;
    const img = state.imageCache.get(layer.image);
    if (!img || !img.complete || !img.naturalWidth) continue;
    const { x: ox, y: oy } = layerOrigin(layer);
    const s = layerScale(layer);
    width = Math.max(width, ox + img.naturalWidth * s);
    height = Math.max(height, oy + img.naturalHeight * s);
  }
  if (width <= 0 || height <= 0) {
    return { width: 800, height: 600 }; // no art loaded yet
  }
  return { width, height };
}

function includePoint(bounds, point) {
  if (!point) return;
  const x = Number(point.x);
  const y = Number(point.y);
  if (!Number.isFinite(x) || !Number.isFinite(y)) return;
  bounds.minX = Math.min(bounds.minX, x);
  bounds.minY = Math.min(bounds.minY, y);
  bounds.maxX = Math.max(bounds.maxX, x);
  bounds.maxY = Math.max(bounds.maxY, y);
}

function includeRect(bounds, rect) {
  if (!rect) return;
  includePoint(bounds, { x: rect.x, y: rect.y });
  includePoint(bounds, { x: rect.x + rect.w, y: rect.y + rect.h });
}

function includePolygon(bounds, polygon) {
  if (!Array.isArray(polygon)) return;
  polygon.forEach((point) => includePoint(bounds, point));
}

// The workspace includes every editable entity, irrespective of the active
// mode, plus a stable margin on each side. This makes small negative coordinates
// recoverable and leaves room to author off-screen paths and staging positions.
function computeWorkspaceBounds(roomSize) {
  const bounds = { minX: 0, minY: 0, maxX: roomSize.width, maxY: roomSize.height };
  for (const layer of state.room?.background?.layers || []) includeRect(bounds, layerRect(layer));
  includePolygon(bounds, state.room?.walkable);
  for (const polygon of state.room?.obstacles || []) includePolygon(bounds, polygon);
  for (const zone of state.room?.zones || []) includePolygon(bounds, zone?.polygon);
  for (const region of Object.values(state.room?.regions || {})) includePolygon(bounds, region?.area);
  for (const hotspot of Object.values(state.room?.hotspots || {})) includePolygon(bounds, hotspot?.area);
  for (const point of Object.values(state.room?.points || {})) includePoint(bounds, point);
  for (const object of Object.values(objectsMap())) includeRect(bounds, objectRect(object));
  for (const preview of state.avatarPreviews) {
    includeRect(bounds, avatarPreviewRect(preview));
    includePoint(bounds, preview.position);
  }
  if (state.tempRegion) {
    includePoint(bounds, state.tempRegion.start);
    includePoint(bounds, state.tempRegion.current);
  }
  return {
    // Grow in chunks so dragging the outermost entity does not reallocate the
    // canvas backing store for every single pixel crossed.
    minX:
      Math.floor((bounds.minX - WORKSPACE_MARGIN) / WORKSPACE_GROWTH_STEP) *
      WORKSPACE_GROWTH_STEP,
    minY:
      Math.floor((bounds.minY - WORKSPACE_MARGIN) / WORKSPACE_GROWTH_STEP) *
      WORKSPACE_GROWTH_STEP,
    maxX:
      Math.ceil((bounds.maxX + WORKSPACE_MARGIN) / WORKSPACE_GROWTH_STEP) *
      WORKSPACE_GROWTH_STEP,
    maxY:
      Math.ceil((bounds.maxY + WORKSPACE_MARGIN) / WORKSPACE_GROWTH_STEP) *
      WORKSPACE_GROWTH_STEP,
  };
}

function updateRoomInfo() {
  if (!state.room) return;
  const { width, height } = computeRoomSize();
  roomInfo.textContent = `${state.room.id || '[unknown]'} · ${width}x${height}`;
}

function draw() {
  if (!state.room) return;
  const roomSize = computeRoomSize();
  const { width, height } = roomSize;
  const workspace = computeWorkspaceBounds(roomSize);
  const workspaceWidth = Math.max(1, workspace.maxX - workspace.minX);
  const workspaceHeight = Math.max(1, workspace.maxY - workspace.minY);
  const panel = canvas.parentElement;
  const panelStyle = getComputedStyle(panel);
  const availableWidth = Math.max(
    1,
    panel.clientWidth - parseFloat(panelStyle.paddingLeft) - parseFloat(panelStyle.paddingRight)
  );
  const availableHeight = Math.max(
    1,
    panel.clientHeight - parseFloat(panelStyle.paddingTop) - parseFloat(panelStyle.paddingBottom)
  );
  const fitScale = Math.min(availableWidth / width, availableHeight / height, 1);
  // Keep pathological far-off coordinates from asking Chrome for an enormous
  // backing store. The whole workspace remains reachable, just at a lower zoom.
  const scale = Math.min(
    fitScale,
    MAX_CANVAS_DEVICE_DIMENSION / devicePixelRatio / workspaceWidth,
    MAX_CANVAS_DEVICE_DIMENSION / devicePixelRatio / workspaceHeight
  );
  // Ensure there is enough scroll range to place world (0,0) at the viewport's
  // top-left even when the room's aspect ratio leaves spare space on one axis.
  const cssWidth = Math.max(
    workspaceWidth * scale,
    availableWidth + Math.max(0, -workspace.minX * scale)
  );
  const cssHeight = Math.max(
    workspaceHeight * scale,
    availableHeight + Math.max(0, -workspace.minY * scale)
  );
  resizeCanvas(cssWidth, cssHeight, { x: workspace.minX, y: workspace.minY }, scale);
  updateRoomInfo();
  state.viewScale = scale;

  ctx.save();
  ctx.clearRect(0, 0, cssWidth, cssHeight);
  ctx.scale(scale, scale);
  ctx.translate(-workspace.minX, -workspace.minY);

  const bg = state.room.background?.color || { r: 0, g: 0, b: 0 };
  ctx.fillStyle = `rgba(${bg.r || 0}, ${bg.g || 0}, ${bg.b || 0}, ${bg.a ?? 255} / 255)`;
  ctx.fillRect(0, 0, width, height);

  if (state.mode === 'avatars' || state.mode === 'preview') {
    drawDepthSortedScene();
  } else {
    drawLayers();
    drawObjects();
  }
  drawPolygons();
  drawTempRegion();
  drawPoints();
  drawSelectedHandles();
  drawLayerHandles();
  drawAvatarHandles();

  ctx.restore();
}

// The selected layer's drawn rect, its four corners, and its base (bottom-centre)
// anchor — all in world space. Null until the image (hence native size) loads.
function selectedLayerCorners() {
  const layer = selectedLayer();
  if (!layer) return null;
  const r = layerRect(layer);
  if (!r) return null;
  return {
    rect: r,
    tl: { x: r.x, y: r.y },
    tr: { x: r.x + r.w, y: r.y },
    bl: { x: r.x, y: r.y + r.h },
    br: { x: r.x + r.w, y: r.y + r.h },
    base: { x: r.x + r.w / 2, y: r.y + r.h },
  };
}

// In 'layers' mode, outline the selected layer, draw a resize handle at each
// corner, mark the base (bottom-centre) anchor that resizing holds fixed, and
// trace the layer's depth line.
function drawLayerHandles() {
  if (state.mode !== 'layers') return;
  const layer = selectedLayer();
  if (!layer) return;

  // Depth guide: a faint dotted horizontal line across the room at world-Y = z.
  // It shows where this layer sorts — an avatar/object whose pivot is below the
  // line draws in front of the layer, above it draws behind.
  const { width } = computeRoomSize();
  const zy = Number(layer.z) || 0;
  ctx.save();
  ctx.strokeStyle = 'rgba(45, 212, 191, 0.85)';
  ctx.fillStyle = 'rgba(45, 212, 191, 0.95)';
  ctx.lineWidth = 1;
  ctx.setLineDash([5, 5]);
  ctx.beginPath();
  ctx.moveTo(0, zy);
  ctx.lineTo(width, zy);
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.font = '12px sans-serif';
  ctx.fillText(`z ${Math.round(zy)}`, 6, zy - 4);
  ctx.restore();

  const c = selectedLayerCorners();
  if (!c) return;
  ctx.save();
  ctx.strokeStyle = 'rgba(165, 180, 252, 0.9)';
  ctx.lineWidth = 2;
  ctx.setLineDash([6, 4]);
  ctx.strokeRect(c.rect.x, c.rect.y, c.rect.w, c.rect.h);
  ctx.setLineDash([]);
  for (const key of ['tl', 'tr', 'bl', 'br']) {
    ctx.fillStyle = '#a5b4fc';
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(c[key].x, c[key].y, 8, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
  }
  // Base anchor marker (the floor line / pivot resizing keeps fixed).
  ctx.fillStyle = '#f59e0b';
  ctx.beginPath();
  ctx.moveTo(c.base.x, c.base.y - 10);
  ctx.lineTo(c.base.x - 7, c.base.y + 5);
  ctx.lineTo(c.base.x + 7, c.base.y + 5);
  ctx.closePath();
  ctx.fill();
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

function drawLayer(layer) {
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
    const s = layerScale(layer);
    ctx.drawImage(img, ox, oy, img.naturalWidth * s, img.naturalHeight * s);
  }
}

function drawLayers() {
  const layers = Array.isArray(state.room.background?.layers) ? state.room.background.layers : [];
  layers.forEach(drawLayer);
}

function polygonForMode() {
  if (state.mode === 'walkable') {
    return [{ id: 'walkable', polygon: state.room.walkable || [] }];
  }
  if (state.mode === 'obstacles') return (state.room.obstacles || []).map((poly, i) => ({ id: String(i), polygon: poly }));
  if (state.mode === 'zones') return (state.room.zones || []).map((zone) => ({ id: zone.id, polygon: zone.polygon }));
  if (state.mode === 'regions') return Object.entries(state.room.regions || {}).map(([id, region]) => ({ id, polygon: region.area }));
  if (state.mode === 'hotspots') return Object.entries(state.room.hotspots || {}).map(([id, hotspot]) => ({ id, polygon: hotspot.area }));
  return [];
}

function drawPolygons() {
  const colors = {
    walkable: 'rgba(34, 197, 94, 0.25)',
    obstacles: 'rgba(239, 68, 68, 0.28)',
    zones: 'rgba(96, 165, 250, 0.25)',
    regions: 'rgba(192, 132, 252, 0.25)',
    hotspots: 'rgba(251, 146, 60, 0.25)',
  };
  const outlines = {
    walkable: '#15803d',
    obstacles: '#b91c1c',
    zones: '#1d4ed8',
    regions: '#7c3aed',
    hotspots: '#ea580c',
  };

  if (state.mode === 'points' || state.mode === 'avatars' || state.mode === 'preview') return;
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
  if (state.mode === 'avatars' || state.mode === 'preview') return;
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
  if (state.mode === 'points' || state.mode === 'avatars' || state.mode === 'preview') return;
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
  const x = (evt.clientX - rect.left) / scale + state.viewOrigin.x;
  const y = (evt.clientY - rect.top) / scale + state.viewOrigin.y;
  return { x, y };
}

function findVertexAt(point) {
  const polygon = getRoomPolygon();
  if (!Array.isArray(polygon)) return -1;
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

  if (state.mode === 'avatars') {
    const selected = selectedAvatarPreview();
    const resizeHandle = selected ? avatarResizeHandle(selected) : null;
    if (selected && resizeHandle && pointNear(resizeHandle, pos, 14)) {
      const distance = Math.hypot(
        resizeHandle.x - selected.position.x,
        resizeHandle.y - selected.position.y
      );
      state.dragTarget = {
        type: 'avatar-resize',
        id: selected.uid,
        nativeDistance: distance / avatarPreviewScale(selected) || 1,
      };
      setStatus('Drag to scale the avatar about its pivot.');
      return;
    }

    // Hit-test in reverse depth order so overlapping characters select the one
    // that is visually in front.
    const previews = state.avatarPreviews
      .slice()
      .sort((a, b) => (Number(b.position?.y) || 0) - (Number(a.position?.y) || 0));
    for (const preview of previews) {
      const rect = avatarPreviewRect(preview);
      if (!rect) continue;
      if (pos.x >= rect.x && pos.x <= rect.x + rect.w &&
          pos.y >= rect.y && pos.y <= rect.y + rect.h) {
        state.selectedEntity = preview.uid;
        entitySelect.value = preview.uid;
        state.dragTarget = { type: 'avatar', id: preview.uid };
        state.dragOffset = {
          x: pos.x - preview.position.x,
          y: pos.y - preview.position.y,
        };
        updateUI();
        return;
      }
    }
    return;
  }

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
    // 1) A corner handle of the already-selected layer starts an aspect-locked
    //    resize about the base (bottom-centre stays fixed).
    const corners = selectedLayerCorners();
    if (corners) {
      for (const key of ['tl', 'tr', 'bl', 'br']) {
        if (!pointNear(corners[key], pos, 14)) continue;
        const native = layerNativeSize(selectedLayer());
        // The grabbed corner's offset from the base at scale 1, as a unit
        // direction + length. The drag projects the cursor onto this ray, so
        // moving perpendicular to it (e.g. dragging a bottom handle vertically)
        // doesn't change the scale.
        const dx = (key === 'tl' || key === 'bl') ? -native.w / 2 : native.w / 2;
        const dy = (key === 'tl' || key === 'tr') ? -native.h : 0;
        const cornerBaseDist = Math.hypot(dx, dy) || 1;
        state.dragTarget = {
          type: 'layer-resize',
          id: state.selectedLayerId,
          base: { x: corners.base.x, y: corners.base.y },
          dir: { x: dx / cornerBaseDist, y: dy / cornerBaseDist },
          cornerBaseDist,
        };
        setStatus('Drag to resize (aspect locked, base fixed).');
        return;
      }
    }

    // 2) Otherwise select / move a layer body (topmost first).
    const layers = (state.room.background?.layers || []).slice().reverse();
    for (const layer of layers) {
      const r = layerRect(layer);
      if (!r) continue;
      if (pos.x >= r.x && pos.x <= r.x + r.w && pos.y >= r.y && pos.y <= r.y + r.h) {
        selectLayer(layer.id);
        state.dragTarget = { type: 'layer', id: layer.id };
        state.dragOffset = { x: pos.x - r.x, y: pos.y - r.y };
        updateUI();
        return;
      }
    }
  }

  // Objects mode: click an object to select it, then drag its body to move it.
  if (state.mode === 'objects') {
    const entries = Object.entries(objectsMap()).reverse(); // topmost first
    for (const [id, obj] of entries) {
      const r = objectRect(obj);
      if (r.w <= 0 || r.h <= 0) continue;
      if (pos.x >= r.x && pos.x <= r.x + r.w && pos.y >= r.y && pos.y <= r.y + r.h) {
        selectObject(id);
        state.dragTarget = { type: 'object', id };
        state.dragOffset = { x: pos.x - r.x, y: pos.y - r.y };
        return;
      }
    }
    return; // empty space in objects mode: nothing to create here (use Add)
  }

  if (state.mode === 'points') {
    const found = findPointAt(pos);
    if (found) {
      const [id] = found;
      state.selectedPoint = id;
      state.selectedEntity = id; // keep the dropdown + Remove in sync with the click
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
  // Insert vertices for non-rectangle polygons. An empty polygon (e.g. a freshly
  // added hotspot) must accept its first point, so don't gate on length.
  if (state.mode !== 'regions') {
    const polygon = getRoomPolygon();
    if (Array.isArray(polygon)) {
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
    updateLayerInfoUI();
    draw();
    return;
  }
  if (state.dragTarget.type === 'layer-resize') {
    const layers = state.room.background?.layers || [];
    const layer = layers.find((l) => l.id === state.dragTarget.id);
    if (!layer) return;
    const { base, dir, cornerBaseDist } = state.dragTarget;
    const proj = (pos.x - base.x) * dir.x + (pos.y - base.y) * dir.y;
    rescaleLayerAboutBase(layer, proj / cornerBaseDist, base);
    updateLayerInfoUI();
    draw();
    return;
  }
  if (state.dragTarget.type === 'object') {
    const obj = objectsMap()[state.dragTarget.id];
    if (!obj) return;
    obj.position = {
      x: Math.round(pos.x - state.dragOffset.x),
      y: Math.round(pos.y - state.dragOffset.y),
    };
    updateObjectsPanel();
    draw();
    return;
  }
  if (state.dragTarget.type === 'avatar') {
    const preview = state.avatarPreviews.find(
      (item) => item.uid === state.dragTarget.id
    );
    if (!preview) return;
    preview.position = {
      x: Math.round(pos.x - state.dragOffset.x),
      y: Math.round(pos.y - state.dragOffset.y),
    };
    updateAvatarPanel();
    draw();
    return;
  }
  if (state.dragTarget.type === 'avatar-resize') {
    const preview = state.avatarPreviews.find(
      (item) => item.uid === state.dragTarget.id
    );
    if (!preview) return;
    const distance = Math.hypot(
      pos.x - preview.position.x,
      pos.y - preview.position.y
    );
    preview.scale = Math.round(
      Math.max(0.05, distance / state.dragTarget.nativeDistance) * 1000
    ) / 1000;
    updateAvatarPanel();
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

function queuePointerMove(evt) {
  // Retain only the latest coordinates. Raw pointer events can arrive much
  // faster than a full room redraw; one animation-frame callback bounds both
  // memory use and paint work while preserving the latest drag position.
  pendingPointerMove = { clientX: evt.clientX, clientY: evt.clientY };
  if (pointerMoveFrame !== null) return;
  pointerMoveFrame = requestAnimationFrame(() => {
    pointerMoveFrame = null;
    const latest = pendingPointerMove;
    pendingPointerMove = null;
    if (latest) handlePointerMove(latest);
  });
}

function flushPointerMove() {
  if (pointerMoveFrame !== null) {
    cancelAnimationFrame(pointerMoveFrame);
    pointerMoveFrame = null;
  }
  const latest = pendingPointerMove;
  pendingPointerMove = null;
  if (latest) handlePointerMove(latest);
}

function handlePointerUp() {
  if (state.dragTarget &&
      (state.dragTarget.type === 'avatar' ||
       state.dragTarget.type === 'avatar-resize')) {
    const resized = state.dragTarget.type === 'avatar-resize';
    state.dragTarget = null;
    state.dragOffset = null;
    updateAvatarPanel();
    setStatus(resized ? 'Avatar preview scaled.' : 'Avatar preview moved.');
    return;
  }
  if (state.dragTarget && (state.dragTarget.type === 'layer' || state.dragTarget.type === 'layer-resize')) {
    // keep selection, but clear drag state. The layer list (pos/scale text) is
    // refreshed here rather than on every pointermove to keep dragging smooth.
    const resized = state.dragTarget.type === 'layer-resize';
    state.dragTarget = null;
    state.dragOffset = null;
    updateLayersList();
    setStatus(resized ? 'Layer resized.' : 'Layer moved.');
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
  if (mode === 'avatars') {
    addAvatarPreview('NPC');
    return;
  } else if (mode === 'points') {
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
  } else if (mode === 'obstacles') {
    // Obstacles are unnamed; add an empty polygon and select it by index.
    state.room.obstacles = state.room.obstacles || [];
    state.room.obstacles.push([]);
    state.selectedEntity = String(state.room.obstacles.length - 1);
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
    } else if (mode === 'objects') {
      state.room.objects = state.room.objects || {};
      if (state.room.objects[id]) {
        alert('An object with that id already exists.');
        return;
      }
      // New object: pick a sprite from the Objects panel; seed at room centre.
      state.room.objects[id] = { sprite: '', position: { x: 200, y: 200 } };
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
  } else if (mode === 'obstacles') {
    (state.room.obstacles || []).splice(Number(state.selectedEntity), 1);
    state.selectedEntity = null;
  } else if (mode === 'zones') {
    state.room.zones = (state.room.zones || []).filter((zone) => zone.id !== state.selectedEntity);
    state.selectedEntity = null;
  } else if (mode === 'regions') {
    delete state.room.regions[state.selectedEntity];
    state.selectedEntity = null;
  } else if (mode === 'hotspots') {
    delete state.room.hotspots[state.selectedEntity];
    state.selectedEntity = null;
  } else if (mode === 'objects') {
    delete (state.room.objects || {})[state.selectedEntity];
    state.selectedEntity = null;
  } else if (mode === 'avatars') {
    removeSelectedAvatarPreview();
    return;
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
      objects: state.room.objects || {},
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
  state.imageCache.clear(); // re-fetch images in case the art changed on disk
  await loadAvatarCatalog();
  await loadRoom();
  await loadRooms();
  await loadAssets();
  setStatus('Room reloaded.');
});
saveButton.addEventListener('click', saveRoom);
openRoomButton.addEventListener('click', openSelectedRoom);
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
document.getElementById('layer-scale').addEventListener('change', applyLayerScale);
document.getElementById('layer-reset-size').addEventListener('click', resetLayerSize);
document.getElementById('layer-z').addEventListener('change', applyLayerZ);
document.getElementById('layer-z-base').addEventListener('click', setLayerZBase);
// object controls (#147)
document.addEventListener('click', (evt) => {
  const el = evt.target.closest && evt.target.closest('[data-object-id]');
  const list = document.getElementById('objects-list');
  if (el && list && list.contains(el)) {
    const id = el.getAttribute('data-object-id');
    if (id) selectObject(id);
  }
});
document.getElementById('object-apply').addEventListener('click', applyObjectFields);
document.getElementById('object-sprite').addEventListener('change', applyObjectFields);
document.addEventListener('click', (evt) => {
  const el = evt.target.closest && evt.target.closest('[data-avatar-id]');
  if (el && avatarsList.contains(el)) {
    const id = el.getAttribute('data-avatar-id');
    if (id) {
      state.selectedEntity = id;
      entitySelect.value = id;
      updateUI();
    }
  }
});
document.getElementById('avatar-add-pc').addEventListener('click', () => addAvatarPreview('PC'));
document.getElementById('avatar-add-npc').addEventListener('click', () => addAvatarPreview('NPC'));
document.getElementById('avatar-apply').addEventListener('click', applyAvatarFields);
document.getElementById('avatar-copy').addEventListener('click', copyAvatarValues);
document.getElementById('avatar-remove').addEventListener('click', removeSelectedAvatarPreview);
snapshotRegionButton.addEventListener('click', snapshotRegion);
modeSelect.addEventListener('change', changeMode);
entitySelect.addEventListener('change', changeEntity);
addEntityButton.addEventListener('click', addEntity);
removeEntityButton.addEventListener('click', removeEntity);
hotspotRenameButton.addEventListener('click', renameHotspot);
hotspotNameInput.addEventListener('change', applyHotspotName);
hotspotAffordances.addEventListener('change', applyAffordances);
addVertexButton.addEventListener('click', toggleAddVertex);
deleteVertexButton.addEventListener('click', toggleDeleteVertex);
addPointButton.addEventListener('click', addPoint);
deletePointButton.addEventListener('click', deletePoint);
canvas.addEventListener('pointerdown', (evt) => {
  // Capture so a drag that wanders off the canvas still delivers move/up here,
  // instead of leaving state.dragTarget stuck set. Auto-releases on pointerup.
  try { canvas.setPointerCapture(evt.pointerId); } catch (_) { /* unsupported */ }
  handlePointerDown(evt);
});
canvas.addEventListener('pointermove', (evt) => {
  if ((evt.buttons & 1) !== 0) queuePointerMove(evt);
});
canvas.addEventListener('pointerup', () => {
  flushPointerMove();
  handlePointerUp();
});
canvas.addEventListener('pointercancel', () => {
  pendingPointerMove = null;
  if (pointerMoveFrame !== null) cancelAnimationFrame(pointerMoveFrame);
  pointerMoveFrame = null;
  handlePointerUp();
});
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
  const s = layerScale(layer);
  cctx.drawImage(img, ox - bbox.x, oy - bbox.y, img.naturalWidth * s, img.naturalHeight * s);

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

async function loadAssets() {
  try {
    const data = await fetchJson('/api/assets');
    state.assets = Array.isArray(data.assets) ? data.assets : [];
  } catch (_) {
    state.assets = [];
  }
  updateObjectsPanel();
}

updateModeOptions();
loadInfo()
  .then(loadAvatarCatalog)
  .then(loadRooms)
  .then(loadRoom)
  .then(loadAssets)
  .catch((err) => setStatus(err.message, true));
