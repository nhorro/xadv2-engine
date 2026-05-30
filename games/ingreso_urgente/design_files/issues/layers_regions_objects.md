Scenery: layers, regions, objects, NPCs, obstacles
--------------------------------------------------

This issue clarifies the usage and capabilities of these scenery concepts and
detects missing functionality required by a typical point & click game.

It reviews **what is currently implemented**, **whether it is documented clearly with
authoring examples**, and **what is missing** so it can be specified as a feature.

> Verified against the code on `develop` (loaders in `lib/src/pnc/room.cpp`, Lua
> bindings in `lib/src/pnc/room_scene.cpp`, structs in
> `lib/include/engine/pnc/room.hpp`) and the design docs (`04-point-and-click-concepts.md`
> §Background layers / §Z-order, `05-scripting-api.md` §Scenery, `06-data-formats.md`).
> Where the code and docs disagree, the code is the current truth.

### TL;DR — current reality vs. the mental model

| Concept | What it actually is today (MVP) | Runtime Lua control |
|---------|----------------------------------|---------------------|
| **Layer** | A background image at a `z`, optionally placed (`origin`), scaled (`scale`), shaded (`shaders`). | **Visibility only** (`set_layer_visible`). |
| **Region** | A **polygon** footprint of the background that swaps between named **state images**. Also a hit source for a bound hotspot. | **State swap** (`set_region_state` / `get_region_state`). |
| **Object** | A **static texture** placed at a world position, sorted by z/baseline. Visual only; interactivity comes from a separate hotspot bound to it. | **Show / hide only** (`show_object` / `hide_object`). |
| **NPC** | A cast character placed in the room `avatars:` list (no `player: true`). Drawn from its cast appearance, room-scoped. | **None.** No spawn/despawn/show/hide/move from Lua. |
| **Obstacle** | An **anonymous polygon** subtracted from the walkable area for pathfinding. No id, no name, no flag. | **None.** Cannot be toggled from Lua. |

Layers, regions, and objects persist their runtime state **per room**, across
`change_room` and save/load (`GameState::region_states`, `object_visible`,
`layer_visible`). **NPC presence and obstacle state are *not* persisted** — see those
sections.

---

### Layers

A room background is a *list* of layers (not one image), to allow layered composition,
foreground occlusion, and shader effects. Each layer draws at an explicit `z`; larger
`z` is nearer the camera. The room's **world bounds are derived** from the union of all
layer rects (hidden or not), so toggling visibility never reshapes the room.

> ⚠️ Correction to the earlier draft: layers can be **placed and scaled at *authoring*
> time** (`origin`, `scale`), but there is **no runtime Lua API to move or resize a
> layer**. The only runtime control is visibility. "Moved and resized" is a room-editor /
> YAML capability, not a scripting one.

**Room configuration** (`background.layers[]`, parsed in `room.cpp`):

| Field | Req | Meaning |
|-------|-----|---------|
| `id` | for visibility toggling | Stable layer id (needed by `set_layer_visible`). |
| `image` | yes | Logical resource path. |
| `z` | yes (defaults 0) | Draw depth. |
| `origin` | no (`{0,0}`) | Room-space top-left; lets layers differ in size / be placed freely. |
| `scale` | no (`1.0`) | Uniform render scale about `origin`, aspect always preserved. Must be > 0. **Authoring aid**; ship production layers at `1.0`. |
| `interactive` | no (`false`) | Whether the layer receives pointer interaction. |
| `visible` | no (`true`) | Initial visibility. |
| `shader` / `shaders` | no | Single shader or ordered multi-pass stack applied when drawing. |
| `animation` | — | **Documented as design-for; NOT parsed by the loader.** |

```yaml
background:
  color: { r: 24, g: 28, b: 40 }
  layers:
    - { id: background, image: exterior/00.png, z: 0 }
    - id: truck
      image: exterior/objects/truck.png
      z: 487
      origin: { x: 738, y: 278 }
```

**Lua API**

| Function | Effect |
|----------|--------|
| `set_layer_visible(id, visible)` | Show/hide a layer (requires the layer to have an `id`). Persisted per room. |

That is the **only** layer scripting call. No move, resize, re-z, or shader-parameter
control at runtime.

**Room editor support**

- Origin and size can be controlled.
- Show/hide toggling for complex scenes would be helpful but is not mandatory.

---

### Regions

A region is a **polygon** part of the background that swaps between two or more named
**state images** — e.g. a drawer `shut` / `open`, a screen on / off. State is controlled
from Lua and persisted per room. A region's `area` is **also a hit source**: a hotspot
can target it with `bind: region:<id>` and the hit test uses the polygon (constant,
independent of the current state image).

> ⚠️ Correction to the earlier draft: regions are **polygons, not rectangles**. A region
> state image may be **empty (draws nothing)** — that is how you "hide" a region.

**Room configuration** (`regions:` map, parsed in `room.cpp`):

| Field | Req | Meaning |
|-------|-----|---------|
| `area` | yes | Polygon footprint (and hit source). |
| `states` | yes | Map of state id → image logical path (a value may be empty = draws nothing). |
| `initial` | yes | Starting state id. |
| `z` | required unless `over`/`baseline` | Draw depth. |
| `over` | no | Inherit the named layer's `z` (and its shader stack, then the region's own on top). |
| `baseline` | no | Floor-line world-Y; sorts against avatar feet for a perspective region. Overrides `over`/`z`. |
| `shader` / `shaders` | no | Shader(s) applied when drawing. |

```yaml
regions:
  drawer:
    area: [ {x: 100, y: 400}, {x: 240, y: 400}, {x: 240, y: 470}, {x: 100, y: 470} ]
    over: background
    initial: shut
    states:
      shut: rooms/study/drawer_shut.png
      open: rooms/study/drawer_open.png
```

**Lua API**

| Function | Effect |
|----------|--------|
| `set_region_state(id, state)` | Swap the region to a named state. |
| `get_region_state(id)` | Read the current state id. |

**Room editor support**

- The editor takes snapshots of a region as PNGs to author the state images. Sufficient.

---

### Objects

> ⚠️ Major correction to the earlier draft. **Objects are NOT animated sprites in the
> current engine.** `RoomObject` holds a **static texture** (`image`, "M4: static
> texture") placed at a world position, with z / `baseline` sorting and a visibility
> flag. They are **visual only**; interactivity comes from a *separate* hotspot bound
> with `bind: object:<id>`. The only runtime scripting is **show / hide**.
>
> Animating an object or driving its sequence from Lua (`object(id):play(...)`) is
> **explicitly design-for** in `05-scripting-api.md` §Scenery — "the MVP only shows or
> hides objects." Moving an object from Lua is also unsupported.

**Room configuration** (`objects:` map, parsed in `room.cpp`):

| Field | Req | Meaning |
|-------|-----|---------|
| `image` | yes (in practice) | Logical path to a **static** texture. *(See doc mismatch below — docs call this `sprite`.)* |
| `position` | yes | World position (top-left). |
| `z` | no (`auto`) | `auto` = sprite bottom edge; a number overrides. |
| `baseline` | no | Floor-line world-Y; sorts against avatar feet. Overrides `z`/`auto`. |
| `visible` | no (`true`) | Initial visibility. |
| `shader` / `shaders` | no | Shader(s) applied when drawing. |

```yaml
objects:
  cart_front:
    image: objects/cart_front.png   # static texture
    position: { x: 400, y: 360 }
    baseline: 640                    # occlusion flips at the avatar's feet line
```

**Lua API**

| Function | Effect |
|----------|--------|
| `show_object(id)` | Make the object visible/enabled. |
| `hide_object(id)` | Hide it (and disable interaction if a hotspot binds to it). |

No movement, no position get/set, no animation/sequence control.

---

### NPCs

An NPC is a cast character placed in a room's `avatars:` list **without** `player: true`.
On room load `spawn_room_npcs` builds an avatar from the character's cast appearance,
seats it at its `start` point, and adds it to the room (room-scoped: destroyed on
unload). You talk to an NPC with `start_dialog(npc_id)`, usually from a hotspot's
`talk_to` handler.

What works today: **static placement + dialog.** Two gaps block the common cases:

**1.1 — NPC presence cannot be controlled from Lua / by global state.**

NPCs spawn **unconditionally** from the YAML `avatars:` list. There is no
`spawn_npc` / `despawn_npc` / `show_npc` / `hide_npc` binding, and presence is **not**
in `GameState`. So "this NPC appears only after global flag X" has **no clean
mechanism** today — you cannot add or remove an NPC from a room hook, and there is no
per-avatar `visible`/`condition` field in the room format.

Note this is part of a larger gap: the documented **`avatar(id)` handle**
(`avatar(id):move_to(...)`, `:face(...)`, `:play_until_end(...)` in
`05-scripting-api.md` §Avatars) is **never registered in Lua** — only the C++ `Mover`
exists. So NPCs also can't be moved or animated from script. NPC presence and NPC
motion should probably be designed together.

Two reasonable shapes for presence control (pick in the issue):
- **Imperative:** `spawn_npc(char_id, point[, orientation])` + `despawn_npc(id)`, called
  from `on_load` against global state. Presence stays derived from global state, so no
  new persistence is needed.
- **Declarative:** a `visible:` / `when:` field on the `avatars:` entry the loader
  evaluates against state at load. Simpler for authors, less flexible.

**1.2 — A hotspot cannot bind to a moving NPC.**

The `bind` hit-test resolves `object:<id>` (the object's current frame bounds) and
`region:<id>` (the region polygon) only — there is **no `npc:`/`avatar:` kind**
(`room_runtime.cpp` `hotspot_at`). So today an NPC needs a **fixed polygon** hotspot,
which is wrong for a character that moves (exactly your concern).

The fix is small and well-scoped: add a `bind: npc:<id>` (or `avatar:<id>`) kind that
resolves the avatar's **current** bounding box each frame — the same per-frame
`object_bounds` callback already used for `object:` binds. The hotspot should be
inactive when the NPC isn't present (ties to 1.1).

**Lua API (NPCs)**

| Function | Effect |
|----------|--------|
| `start_dialog(npc_id)` | Begin the dialog tree for that character. |

No other NPC-facing API exists today (no presence, position, facing, or animation).

---

### Obstacles

Obstacles are polygons **subtracted from the walkable area** for pathfinding: a point is
walkable iff it is inside `walkable` and inside **no** obstacle (`RoomData::is_walkable`),
and `find_path` routes around them. They are stored as an **anonymous
`std::vector<geom::Polygon>`** — **no id, no name, no enabled flag** — loaded from a bare
list of polygons in YAML and used directly by the pathfinder and the debug overlay.

**2.1 — Obstacles cannot be enabled/disabled from Lua.**

A box or truck that blocks the floor until it's removed is a standard need, but there is
**no runtime toggle**: obstacles are baked into pathfinding for the room's lifetime, and
obstacle state is **not** in `GameState`. Implementing this is a dependency chain:

1. give each obstacle an **id** (requires 2.2 / a format change);
2. add an **enabled** flag + per-room persistence (mirror `object_visible` /
   `hotspot_enabled` in `GameState`);
3. expose `enable_obstacle(id)` / `disable_obstacle(id)` to Lua;
4. make `is_walkable` / `find_path` **skip disabled** obstacles.

**2.2 — Obstacles have no editable name.**

Because the data model has no id field, the room editor can only auto-name them
(`obstacle0`, `obstacle1`, …). Add an optional `id` to each obstacle so authors can name
them (and so 2.1 can reference them). This is a **room-format change**: today
`obstacles:` is a list of bare polygons; it should accept id'd entries
(`{ id: crate, area: [...] }`) while ideally still accepting the bare-polygon form for
backward compatibility.

**Lua API (obstacles)**

None today.

---

## Documentation status (gaps & mismatches)

1. **`sprite` vs `image` (real mismatch, author-facing bug).** `06-data-formats.md`
   documents the object field as **`sprite`** ("Object animation/sprite", required),
   but the loader (`room.cpp`) reads **`image`** and ignores `sprite`. An author who
   follows the docs gets a silent empty object. → **Fix one side**: either change the
   loader to accept `sprite` (and treat `image` as an alias), or correct the docs to
   `image` and drop the "animation" wording.

2. **Objects described as animated, implemented as static.** `06` calls the object
   image an "animation/sprite"; the struct is a static texture and `05` says animation
   is design-for. The two docs are inconsistent with each other and with the code.
   → Make the MVP scope explicit in `06` (static texture; `object(id):play` design-for).

3. **No worked authoring examples.** `04`/`06` describe the fields but there is no
   end-to-end "here is a region with two states + the hotspot + the Lua that swaps it"
   example, nor the equivalent for a baseline object / walk-behind. The draft's repeated
   "Check in other doc" / "FIXME: document API" reflect this gap. → Add an authoring
   guide section with copy-pasteable YAML + Lua per concept.

4. **No sample content exercises objects/regions/walkbehinds.** Neither `themummy` nor
   `ingreso_urgente` defines `objects:` or `walkbehinds:`, so these paths are untested
   by example. → Add to a sample/template (ties into M6 authoring templates, #39).

5. **Stale code comment.** `RoomHotspot.bind` is annotated "(unused in M3)" but `bind`
   *is* implemented (`room_runtime.cpp` hit test for `object:`/`region:`). Minor; fix
   the comment.

---

## Candidate issues (missing capabilities)

Ordered roughly by how much a typical P&C game needs them.

- **[bug/docs] Object `sprite` vs `image` field mismatch.** Reconcile docs and loader so
  one documented field works. (See doc gap #1.)

- **[feature] Animated objects.** Make `RoomObject` able to use an `*.anim.yml` like an
  avatar (animated sprite), not just a static texture. Prerequisite for any motion.

- **[feature] Object animation control from Lua.** `object(id):play(seq)`, `stop()`,
  `set_frame(n)`, looping/one-shot. This is the headline "dynamic object" capability the
  draft assumed already exists. (Currently design-for in `05`.)

- **[feature] Object movement from Lua.** `object(id):move_to(x, y[, speed])` /
  `set_position` / `position()`, with z/baseline kept consistent during motion. Needed
  for things like a passing vehicle, a closing door rendered as an object, a moving prop.

- **[feature/design-for] Runtime layer transform.** Move/scale a layer from Lua for
  parallax or sliding scenery. Today layers are authoring-time placed and only toggle
  visibility. Decide whether this is in scope or should remain an object/region job.

- **[feature/design-for] Runtime shader parameters from Lua.** Animate a layer/region/
  object shader uniform (e.g. ramp a distortion). Today shaders are fixed at authoring.

- **[feature] NPC presence control from Lua.** `spawn_npc`/`despawn_npc` (or a
  declarative `visible:`/`when:` on the `avatars:` entry) so an NPC appears based on
  global state. (Topic 1.1.) Decide whether presence is derived from state (no new
  persistence) or persisted.

- **[feature] `avatar(id)` Lua handle.** Register the documented avatar handle
  (`move_to`, `face`, `play_until_end`) — currently in the docs but **not implemented**.
  Prerequisite for moving/animating NPCs and the player from script. (Underlies 1.1.)

- **[feature] Hotspot `bind: npc:<id>`.** Hit-test against the NPC's current (moving)
  bounding box, reusing the per-frame bounds callback used for `object:` binds; inactive
  when the NPC is absent. (Topic 1.2.)

- **[feature] Toggleable obstacles.** Obstacle ids + `enabled` flag + per-room
  persistence + `enable_obstacle`/`disable_obstacle` + pathfinder skips disabled ones.
  (Topic 2.1; depends on the obstacle-id format change below.)

- **[feature/format] Named obstacles.** Add an optional `id` to obstacle entries (room
  format change: accept `{ id, area }` entries, ideally keeping the bare-polygon list for
  back-compat) so the room editor and scripts can name/reference them. (Topic 2.2.)

- **[docs] Authoring guide for scenery.** Worked, copy-pasteable examples for layer
  visibility, region states, baseline objects, walk-behinds, NPC placement + dialog, and
  obstacles, with the matching Lua.

- **[content] Exercise objects/regions/walkbehinds/NPCs in a sample/template** so the
  paths have living examples and regression coverage (M6 #39).

### Quick scope notes for the issues

- **In the engine today:** layer visibility; region state swap; object show/hide; z /
  `baseline` perspective sorting; walk-behinds; per-room persistence of those; static NPC
  placement + `start_dialog`; static (baked) obstacles in pathfinding.
- **Design-for / documented-but-not-built:** the `avatar(id)` handle (`move_to`/`face`/
  `play_until_end`), object animation (`object(id):play`), object movement, layer
  `animation`, runtime layer transforms, runtime shader params.
- **Not yet designed (new):** NPC presence control, hotspot `bind: npc:`, toggleable +
  named obstacles.
