# Target architecture and how to pay the debt

!!! warning "Proposal, not current architecture"
    This page records a possible destination and migration sequence. For the
    implementation that exists today, read the [as-built tour](../tour/index.md).

Working reference for what XADV2 *could* become, how far the tree is from that,
and how to move without stranding existing games.

Historical pages under `docs/development/history/design/` are an archive of
intent-plus-changelog. They are not the map for new work.

---

## 1. The model (accept this)

Point-and-click is a **domain kit**, not a second engine. Almost everything a
room does is a *use* of generic 2D + core services.

```
P&C kit
  Avatar API (talk / walk_to / look_at / face)
  Command state machine + inventory + dialog trees
  Hotspots / affordances / rooms-as-data
  Optional composers: SCUMM panel, one-click, tests
Generic 2D
  Layered sprites, animated/composite sprites
  Camera + viewport + z-sort
  Shader chain, lighting as a shader prefix
  Widget chrome drawn with the same sprite/text system
Core
  Scene stack, loop, Display, resources
  Lua (one state, scopes, wait/emit)
  StateStore / GameState / saves
  Audio, input in virtual coordinates
  Geometry: polygons, PIP  (+ P&C may keep nav)
```

Honest exceptions that may stay in `pnc` (or `geom` with a P&C policy):

- Walkable polygon + obstacle pathfinding (visibility graph / future navmesh)
- Verb/affordance grammar and approach-to-interact
- Dialog tree policy (`when` / `once` / `END`)
- Room-as-authoring-unit (`rooms/id.yaml` + sidecar Lua + `change_room`)

Everything else should be *core/gfx capability that P&C calls*.

**This is not the current state.** It is the destination. New work is accepted
only if it moves toward this split or is a justified exception from the list
above.

---

## 2. Current state vs that model

| Claim | Today | Gap |
|---|---|---|
| Room viewport = layered core sprites | `RoomRenderer` (pnc) is a private compositor. `ScriptScene` is a parallel entity list. | Large |
| Avatars = façade over animated sprites | `Avatar` wraps `VisualSprite` + `Mover`. `talk` is a scene API + wait lists. | Medium |
| SCUMM = model + thin UI | `CommandBuilder` + `InventoryModel` are models. `ScummPanel` is a ~1300-line painter. | UI far |
| Persistence through Lua = engine | `StateStore` is core. Room persist maps and `snap`/`restore` still live on the scene. | Half |
| Audio = engine | Service is core. Footstep / voice-skip policy leaked into the scene. | Small |
| Camera / viewport | `RoomViewport` is a size. `Camera` is pnc. Design 04's 85/15 split is obsolete. | Medium |
| Lighting / shaders | `ShaderChain` is gfx. Light resolve + GLES fallback + RT ownership sit in pnc. | Medium |
| Lua | One state + scopes are core. Most bindings are registered by `RoomScene`. | Medium |

The action model and service boundaries exist in pieces. The world compositor
and UI toolkit assumed as “core 2D” do not. P&C grew those instead of demanding
them from below. That is the debt.

---

## 3. Archive the historical design

This repository now treats `docs/development/history/design/` as frozen history
([note](../history/README.md)). The as-built tour is `docs/development/tour/`.
Do not rewrite 01–06 in place.

[`scumm-command-flow.md`](../scumm-command-flow.md) points at
`RoomCommandProcessor`. Do not extract a
third dispatcher.

---

## 4. Do not rewrite P&C on a long-lived experimental core

A season-long `experimental` core with P&C rewritten afterwards forks the
product. FdC will keep moving on `develop`. Parity with rooms, `talk` yield,
room configs, close-ups, and chapter saves is the expensive part. Seams already
exist (`CommandController`, `RoomCommandProcessor`, `RoomViewport`, widgets).

What works: **promote missing core types downward on the same line that still
runs examples + FdC**, and thin P&C until it matches §1.

Think “extract the engine that RoomScene secretly is,” not “build a new engine
and port the game.”

Short spike branches (days) are fine. A second engine is not.

---

## 5. Working method

### Step 1 — Understand how it works now

Tour, not the archive: [core](../tour/01-core-abstractions.md), then
[P&C kit](../tour/02-point-and-click.md). Acceptance: a new developer can trace
`LOOK_AT door` and `change_room` without reading all of `room_scene.cpp`.

### Step 2 — Easy fixes that do not compromise stability

| Fix | Why it is cheap | Why it helps |
|---|---|---|
| Archive design docs (this PR) | Docs only | Stops new work from citing stale R3/R7 / 85% panel |
| Move Lua usertypes out of `RoomScene` into `pnc/lua_room_api.cpp` | Move-only | Scene stops being the binding file |
| Extract `ScriptWaitBoard` | Same emit protocol | Core-shaped wait API |
| Pause menu via existing `PauseOverlay` | Overlay exists | Session ≠ chrome |
| Camera math must not read panel height | Comment + one test | Locks `RoomViewport` |
| Move `CaseResolution` to FdC | Cross-repo move | Engine stops owning game dossier UI |
| `gfx::supports_shader_rt()` | Small | Android `#ifdef` leaves the scene |

Do **not** in step 2: rewrite `ScummPanel` in Lua, invent an ECS, replace
pathfinding, or change `talk` yield semantics.

### Step 3 — Promote, then thin P&C

**P3a — Generic 2D world.** `WorldView` / camera, `WorldLayer`, `WorldCompositor`
in `gfx`. `RoomRenderer` *feeds* it. Stop when `ScriptScene` shares the
compositor.

**P3b — Actor façade.** Avatar = sprite + optional mover + P&C verbs, using a
core wait-board.

**P3c — Persistence.** Fold room persist maps into `StateStore` prefixes.
`snap()` serializes stores.

**P3d — UI composers.** Keep models. Sprite-based SCUMM chrome only after P3a.

**P3e — Shrink `RoomScene`.** Target under ~800 lines, after P3a–c.

---

## 6. Branching

```
develop              ← FdC and examples must always run
  feature/world-compositor
  feature/wait-board
  feature/lua-room-api
  feature/move-case-resolution-to-fdc
```

Optional 2–3 day `experiment/world-compositor-spike`. Spike dies or becomes the
feature branch.

Gate every merge: `ctest -LE gui`, example smoke, FdC `--frames` pack smoke.
No new public methods on `RoomScene` unless they are moves out of it.

---

## 7. Day-to-day PR rule

1. Is this a core/gfx capability, a P&C policy, or a game feature?
2. If it is a capability, does it live below `pnc`?
3. Does it add lines to `room_scene.cpp`? If yes, the default is reject.
