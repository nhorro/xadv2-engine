"use strict";

// --- DOM ---------------------------------------------------------------------
const canvas = document.getElementById("canvas");
const ctx = canvas.getContext("2d");
const closeupSelect = document.getElementById("closeup-select");
const newBtn = document.getElementById("new-btn");
const deleteBtn = document.getElementById("delete-btn");
const saveBtn = document.getElementById("save-btn");
const statusEl = document.getElementById("status");
const listEl = document.getElementById("hotspot-list");
const propsEl = document.getElementById("props");
const propIdEl = document.getElementById("prop-id");
const propNameEl = document.getElementById("prop-name");
const propApplyBtn = document.getElementById("prop-apply");

// --- state -------------------------------------------------------------------
const state = {
  info: null,
  background: null, // logical path
  bgImage: null,
  hotspots: [], // [{ id, name, area: [{x, y}] }]
  selected: null, // id
  mode: "idle", // 'idle' | 'drawing'
  draft: [], // points while drawing
  drag: null, // { id, index }
  dragMoved: false,
};

function setStatus(msg, isError = false) {
  statusEl.textContent = msg;
  statusEl.classList.toggle("error", !!isError);
}

async function getJson(url) {
  const res = await fetch(url);
  return res.json();
}

// --- coordinate mapping ------------------------------------------------------
// The canvas backing store is the virtual resolution (1280x720), so canvas
// coordinates ARE close-up-space coordinates. The element is scaled in CSS, so we
// map pointer pixels through that scale.
function scaleFactor() {
  const rect = canvas.getBoundingClientRect();
  return canvas.width / rect.width;
}
function toCanvas(ev) {
  const rect = canvas.getBoundingClientRect();
  const s = canvas.width / rect.width;
  return { x: (ev.clientX - rect.left) * s, y: (ev.clientY - rect.top) * s };
}
function hitRadius() {
  return 9 * scaleFactor();
}

// --- geometry helpers --------------------------------------------------------
function dist2(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  return dx * dx + dy * dy;
}
function pointInPolygon(p, poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i];
    const b = poly[j];
    const intersect =
      a.y > p.y !== b.y > p.y &&
      p.x < ((b.x - a.x) * (p.y - a.y)) / (b.y - a.y) + a.x;
    if (intersect) inside = !inside;
  }
  return inside;
}
function distToSegment(p, a, b) {
  const l2 = dist2(a, b);
  if (l2 === 0) return Math.sqrt(dist2(p, a));
  let t = ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2;
  t = Math.max(0, Math.min(1, t));
  return Math.sqrt(dist2(p, { x: a.x + t * (b.x - a.x), y: a.y + t * (b.y - a.y) }));
}
function centroid(poly) {
  let x = 0;
  let y = 0;
  for (const v of poly) {
    x += v.x;
    y += v.y;
  }
  return { x: x / poly.length, y: y / poly.length };
}
function findHotspot(id) {
  return state.hotspots.find((h) => h.id === id) || null;
}

// --- rendering ---------------------------------------------------------------
function render() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#14110d";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  if (state.bgImage) {
    ctx.drawImage(state.bgImage, 0, 0, canvas.width, canvas.height);
  }

  for (const h of state.hotspots) {
    const selected = h.id === state.selected;
    drawPolygon(h.area, selected, true);
    const c = centroid(h.area);
    ctx.fillStyle = selected ? "#ffe9a8" : "#dfe7ff";
    ctx.font = `${Math.round(20 * scaleFactor())}px system-ui, sans-serif`;
    ctx.textAlign = "center";
    ctx.fillText(h.id, c.x, c.y);
  }

  if (state.mode === "drawing" && state.draft.length > 0) {
    drawPolygon(state.draft, true, false);
  }
}

function drawPolygon(poly, selected, closed) {
  if (poly.length === 0) return;
  ctx.lineWidth = 2 * scaleFactor();
  ctx.strokeStyle = selected ? "#ffd24a" : "#5f86c6";
  ctx.fillStyle = selected ? "rgba(255, 210, 74, 0.18)" : "rgba(95, 134, 198, 0.15)";
  ctx.beginPath();
  ctx.moveTo(poly[0].x, poly[0].y);
  for (let i = 1; i < poly.length; i++) ctx.lineTo(poly[i].x, poly[i].y);
  if (closed) ctx.closePath();
  if (closed) ctx.fill();
  ctx.stroke();

  const r = 5 * scaleFactor();
  for (const v of poly) {
    ctx.beginPath();
    ctx.arc(v.x, v.y, r, 0, Math.PI * 2);
    ctx.fillStyle = selected ? "#ffd24a" : "#9ab4e0";
    ctx.fill();
  }
}

// --- list + props ------------------------------------------------------------
function refreshList() {
  listEl.innerHTML = "";
  for (const h of state.hotspots) {
    const li = document.createElement("li");
    li.textContent = `${h.id}${h.name ? ` — ${h.name}` : ""}`;
    if (h.id === state.selected) li.classList.add("selected");
    li.addEventListener("click", () => select(h.id));
    listEl.appendChild(li);
  }
  const sel = findHotspot(state.selected);
  propsEl.hidden = !sel;
  if (sel) {
    propIdEl.value = sel.id;
    propNameEl.value = sel.name || "";
  }
}

function select(id) {
  state.selected = id;
  refreshList();
  render();
}

// --- editing -----------------------------------------------------------------
function findVertex(p) {
  const r2 = hitRadius() * hitRadius();
  const order = state.selected
    ? [findHotspot(state.selected), ...state.hotspots.filter((h) => h.id !== state.selected)]
    : state.hotspots;
  for (const h of order) {
    if (!h) continue;
    for (let i = 0; i < h.area.length; i++) {
      if (dist2(p, h.area[i]) <= r2) return { id: h.id, index: i };
    }
  }
  return null;
}
function findPolygon(p) {
  for (const h of state.hotspots) if (pointInPolygon(p, h.area)) return h;
  return null;
}

function finishDraft() {
  if (state.draft.length < 3) {
    setStatus("A hotspot needs at least 3 points.", true);
    return;
  }
  let id = prompt("Hotspot id (lowercase, no spaces):", `hotspot${state.hotspots.length + 1}`);
  if (!id) {
    cancelDraft();
    return;
  }
  id = id.trim();
  if (findHotspot(id)) {
    setStatus(`A hotspot '${id}' already exists.`, true);
    return;
  }
  state.hotspots.push({ id, name: "", area: state.draft.map((p) => ({ x: p.x, y: p.y })) });
  state.draft = [];
  state.mode = "idle";
  newBtn.classList.remove("active");
  select(id);
  setStatus(`Added '${id}'. Remember to Save.`);
}

function cancelDraft() {
  state.draft = [];
  state.mode = "idle";
  newBtn.classList.remove("active");
  render();
}

// --- canvas events -----------------------------------------------------------
canvas.addEventListener("mousedown", (ev) => {
  if (state.mode === "drawing") return; // points added on click
  const p = toCanvas(ev);
  if (ev.button === 0 && !ev.shiftKey) {
    const v = findVertex(p);
    if (v) {
      state.drag = v;
      state.dragMoved = false;
      select(v.id);
    }
  }
});

canvas.addEventListener("mousemove", (ev) => {
  if (!state.drag) return;
  const p = toCanvas(ev);
  const h = findHotspot(state.drag.id);
  if (h) {
    h.area[state.drag.index] = { x: clampX(p.x), y: clampY(p.y) };
    state.dragMoved = true;
    render();
  }
});

window.addEventListener("mouseup", () => {
  if (state.drag) {
    state.drag = null;
    setStatus("Moved a vertex. Remember to Save.");
  }
});

canvas.addEventListener("click", (ev) => {
  const p = toCanvas(ev);
  if (state.mode === "drawing") {
    state.draft.push({ x: clampX(p.x), y: clampY(p.y) });
    render();
    return;
  }
  if (state.dragMoved) {
    state.dragMoved = false;
    return; // this click ended a drag; don't reselect
  }
  if (ev.shiftKey) {
    insertVertex(p);
    return;
  }
  const h = findPolygon(p);
  select(h ? h.id : null);
});

canvas.addEventListener("dblclick", (ev) => {
  ev.preventDefault();
  if (state.mode === "drawing") finishDraft();
});

canvas.addEventListener("contextmenu", (ev) => {
  ev.preventDefault();
  const p = toCanvas(ev);
  const v = findVertex(p);
  if (!v) return;
  const h = findHotspot(v.id);
  if (h.area.length <= 3) {
    setStatus("A hotspot needs at least 3 points.", true);
    return;
  }
  h.area.splice(v.index, 1);
  render();
  setStatus("Deleted a vertex. Remember to Save.");
});

function insertVertex(p) {
  const h = findHotspot(state.selected);
  if (!h) return;
  let best = -1;
  let bestD = 12 * scaleFactor();
  for (let i = 0; i < h.area.length; i++) {
    const a = h.area[i];
    const b = h.area[(i + 1) % h.area.length];
    const d = distToSegment(p, a, b);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  if (best >= 0) {
    h.area.splice(best + 1, 0, { x: clampX(p.x), y: clampY(p.y) });
    render();
    setStatus("Inserted a vertex. Remember to Save.");
  }
}

function clampX(x) {
  return Math.max(0, Math.min(canvas.width, x));
}
function clampY(y) {
  return Math.max(0, Math.min(canvas.height, y));
}

window.addEventListener("keydown", (ev) => {
  if (ev.target.tagName === "INPUT") return;
  if (ev.key === "Escape" && state.mode === "drawing") cancelDraft();
  if (ev.key === "Enter" && state.mode === "drawing") finishDraft();
});

// --- toolbar -----------------------------------------------------------------
newBtn.addEventListener("click", () => {
  if (state.mode === "drawing") {
    cancelDraft();
    return;
  }
  if (!state.background) {
    setStatus("Open a close-up first.", true);
    return;
  }
  state.mode = "drawing";
  state.draft = [];
  newBtn.classList.add("active");
  setStatus("Click points; double-click (or Enter) to finish, Esc to cancel.");
});

deleteBtn.addEventListener("click", () => {
  if (!state.selected) return;
  state.hotspots = state.hotspots.filter((h) => h.id !== state.selected);
  state.selected = null;
  refreshList();
  render();
  setStatus("Deleted hotspot. Remember to Save.");
});

propApplyBtn.addEventListener("click", () => {
  const h = findHotspot(state.selected);
  if (!h) return;
  const newId = propIdEl.value.trim();
  if (!newId) {
    setStatus("id cannot be empty.", true);
    return;
  }
  if (newId !== h.id && findHotspot(newId)) {
    setStatus(`A hotspot '${newId}' already exists.`, true);
    return;
  }
  h.id = newId;
  h.name = propNameEl.value.trim();
  state.selected = newId;
  refreshList();
  render();
  setStatus("Applied. Remember to Save.");
});

saveBtn.addEventListener("click", save);

async function save() {
  const map = {};
  for (const h of state.hotspots) {
    const entry = { area: h.area.map((p) => ({ x: Math.round(p.x), y: Math.round(p.y) })) };
    if (h.name) entry.name = h.name;
    map[h.id] = entry;
  }
  try {
    const res = await fetch("/api/save", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ hotspots: map }),
    });
    const data = await res.json();
    if (data.ok) setStatus(`Saved ${state.hotspots.length} hotspot(s).`);
    else setStatus(`Save failed: ${data.error}`, true);
  } catch (e) {
    setStatus(`Save failed: ${e}`, true);
  }
}

// --- loading -----------------------------------------------------------------
function loadBackground(logical) {
  return new Promise((resolve) => {
    if (!logical) {
      state.bgImage = null;
      resolve();
      return;
    }
    const img = new Image();
    img.onload = () => {
      state.bgImage = img;
      resolve();
    };
    img.onerror = () => {
      setStatus(`Could not load background '${logical}'.`, true);
      state.bgImage = null;
      resolve();
    };
    img.src = "/assets/" + logical.split("/").map(encodeURIComponent).join("/");
  });
}

async function loadCloseup() {
  const data = await getJson("/api/closeup");
  state.background = typeof data.background === "string" ? data.background : null;
  state.hotspots = [];
  const hs = data.hotspots || {};
  for (const [id, spec] of Object.entries(hs)) {
    if (!spec || !Array.isArray(spec.area)) continue;
    state.hotspots.push({
      id,
      name: typeof spec.name === "string" ? spec.name : "",
      area: spec.area.map((p) => ({ x: Number(p.x), y: Number(p.y) })),
    });
  }
  state.selected = null;
  await loadBackground(state.background);
  refreshList();
  render();
}

async function init() {
  state.info = await getJson("/api/info");
  if (state.info.resolution) {
    canvas.width = state.info.resolution.width;
    canvas.height = state.info.resolution.height;
  }

  const list = await getJson("/api/closeups");
  closeupSelect.innerHTML = "";
  for (const name of list.closeups || []) {
    const opt = document.createElement("option");
    opt.value = name;
    opt.textContent = name;
    if (name === list.current) opt.selected = true;
    closeupSelect.appendChild(opt);
  }
  closeupSelect.addEventListener("change", async () => {
    const res = await fetch("/api/open", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ closeup: closeupSelect.value }),
    });
    const data = await res.json();
    if (data.ok) {
      setStatus(`Opened ${data.closeup}.`);
      await loadCloseup();
    } else {
      setStatus(`Open failed: ${data.error}`, true);
    }
  });

  await loadCloseup();
  if (state.info.closeup) setStatus(`Editing ${state.info.closeup}.`);
  else setStatus("Pick a close-up to edit.");
}

init();
