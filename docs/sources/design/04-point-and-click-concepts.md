# Third-person point & click concepts

This layer (`pac::pnc`) contains the components specific to third-person
point-and-click adventure games. It builds on the generic 2D concepts specified
in [2D game concepts](03-2d-game-concepts.md).

## Genre scenes

| Scene | Purpose |
|-------|---------|
| `RoomScene` | Main SCUMM-style gameplay: rooms, scenery, characters, verb panel, inventory, dialog. |
| `TitleScreen` | Main menu/title screen customized for point-and-click games. |
| `Cutscene` / `StoryText` | Scripted text or visual sequences between gameplay scenes. |

`RoomScene` is specified in the rest of this document. The two supporting scenes
are summarized here.

### TitleScreen

The entry menu. Parameters:

| Parameter | Meaning |
|-----------|---------|
| `background` | Optional background image (logical path). |
| `music` | Optional music track played while the menu is shown. |
| `new_game` | Outcome scene id started by "New game". |
| `exit` | Outcome for "Quit" (usually the reserved `QUIT` token). |

Named outcomes: `new_game`, `continue` (resume the most recent save, offered only
when a save exists), `settings` (push the settings scene), and `exit`. The
manifest wires `new_game` and `exit`; the engine handles `continue` and
`settings`.

### StoryText / Cutscene

A text or scripted sequence between gameplay scenes. Parameters:

| Parameter | Meaning |
|-----------|---------|
| `script` | Lua script (logical path) that drives the sequence as a coroutine. |
| `on_finish` | Outcome scene id entered when the script completes. |

The script may show text pages, play music, and wait, using the same coroutine
API as room scripts. Text pages are shown with `show_text(text, dur?)`, a
speaker-less page drawn centered in virtual space and skippable as a single unit
(see [the scripting API](05-scripting-api.md)); `talk` is reserved for in-room,
near-speaker speech. The scene is skippable; skipping cancels the running script
(remaining pages and waits do not run) and fires `on_finish`. Any persistent state
a cutscene must guarantee should be written before its first yield or in the
receiving scene, since a skipped script does not run past the skip point. Named
outcome: `on_finish`.

## The room

A room is a unit of place in the game world. It contains scenery, interactive
contents, navigation geometry, and characters.

A room is authored as two files linked by id:

| File | Purpose |
|------|---------|
| `rooms/<id>.yaml` | Static definition: background, layers, geometry, points, zones, regions, objects, hotspots, affordances, initial avatars. |
| `rooms/<id>.lua` | Behavior: lifecycle hooks, zone hooks, hotspot verb handlers, scripted events. |

The YAML file defines what exists. The Lua file defines what happens.

### Example room YAML

```yaml
version: 1
id: study
# No explicit size: the room's world bounds are derived from the background
# layers (the union of their rects, floored to the room view). See below.

background:
  color: { r: 0, g: 0, b: 0, a: 255 }
  layers:
    - id: sky
      image: backgrounds/study/sky.png
      z: 0
      interactive: false

    - id: room_back
      image: backgrounds/study/back.png
      z: 10
      interactive: false

    - id: table_front
      image: backgrounds/study/table_front.png
      z: 640
      origin: { x: 980, y: 300 }   # foreground occluder, placed at native size
      interactive: false

perspective:
  top:    { y: 380, scale: 0.70 }
  bottom: { y: 700, scale: 1.15 }

walkable:
  - { x: 40,   y: 600 }
  - { x: 1560, y: 600 }
  - { x: 1560, y: 700 }
  - { x: 40,   y: 700 }

obstacles:
  - [ {x: 700, y: 610}, {x: 900, y: 610}, {x: 900, y: 690}, {x: 700, y: 690} ]

points:
  player_start: { x: 120, y: 650 }
  at_drawer:    { x: 520, y: 660 }

zones:
  - id: to_hall
    polygon: [ ... ]

regions:
  drawer:
    area: [ ... ]
    z: 520
    states:
      shut: backgrounds/regions/drawer_shut.png
      open: backgrounds/regions/drawer_open.png
    initial: shut

objects:
  lamp:
    sprite: anims/lamp.anim.yaml
    position: { x: 980, y: 520 }
    z: auto

hotspots:
  drawer:
    name: "el cajón"
    area: [ ... ]
    approach: at_drawer
    bind: region:drawer
    affordances: [ look_at, open, close ]

  lamp:
    name: "la lámpara"
    bind: object:lamp
    approach: { x: 960, y: 650 }
    affordances: [ look_at, use, pick_up ]

avatars:
  - { id: julia, start: player_start, orientation: down, player: true }
  - { id: schneider, start: at_drawer, orientation: left }
```

### Example room Lua

```lua
local room = {}

function room.on_load()
  play_music("music/study.ogg", true)
end

function room.on_unload()
  stop_music()
end

room.on_zone_enter = function(zone)
  if zone == "to_hall" then
    change_room("hall")
  end
end

room.hotspots = {
  drawer = {
    look_at = function()
      return "Es un cajón viejo."
    end,

    open = function()
      set_region_state("drawer", "open")
      set_room_state("drawer.open", true)
      return "Lo abro."
    end,

    close = function()
      set_region_state("drawer", "shut")
      set_room_state("drawer.open", false)
      return "Lo cierro."
    end,
  },

  lamp = {
    look_at = function()
      return "Una lámpara polvorienta."
    end,

    use = function()
      return "No enciende."
    end,
  },
}

return room
```

## Room contents

| Content | Source | Notes |
|---------|--------|-------|
| Background layers | YAML | Static or animated scenery layers. |
| Background regions | YAML + Lua | Changeable parts of the background, such as drawers or doors. |
| Objects | YAML | Active sprites placed in the room. |
| Characters / avatars | YAML + cast | Player and NPCs. Behavior comes from scripts. |
| Hotspots | YAML + Lua | Interactive areas and verb handlers. |
| Points | YAML | Named coordinates for starts, approach points, camera targets. |
| Walkable area | YAML | Main navigation polygon. |
| Obstacles | YAML | Non-walkable polygons inside the walkable area. |
| Zones | YAML + Lua | Trigger polygons for room exits or scripted events. |

## Background layers

A room background is a list of layers, not a single image. This supports layered
composition, animation, foreground occlusion, and future shader effects.

| Layer field | Meaning |
|-------------|---------|
| `id` | Stable layer id. |
| `image` | Logical resource path. |
| `z` | Draw depth. Larger values are nearer the camera. |
| `origin` | Optional `{x, y}` room-space top-left where the layer is drawn (default the world origin `(0,0)`), so layers may differ in size and be placed freely — a foreground occluder, a parallax-ready backdrop, a decal. |
| `scale` | Optional uniform render scale about `origin` (default `1.0` = native pixel size). Aspect ratio is **always preserved** — layers are never distorted, only uniformly enlarged/shrunk. Mainly a development aid for sizing furniture-style occluder layers; in production layers should ship at their correct native size (`scale: 1`). |
| `interactive` | Whether this layer can receive pointer interactions. Usually false. |
| `visible` | Optional initial visibility (default `true`). Toggled at runtime with `set_layer_visible(id, bool)`; requires the layer to carry an `id`. World bounds are derived from **all** layers, hidden or not, so toggling never reshapes the room. |
| `shader` | Optional shader resource or shader config. Design-for. |
| `animation` | Optional animation description. Design-for. |

Use cases:

| Use case | Example |
|----------|---------|
| Static background | Laboratory back wall. |
| Non-interactive animated layer | Moving clouds. |
| Foreground occluder | Table front drawn in front of characters. |
| Shader effect | Water distortion. |
| Interactive visual state | Prefer a `region` or `object`, not a raw background layer. |

A layer may use a solid `background.color` behind all images. This is useful for
transparent layers, exported foregrounds, and authoring workflows.

**Layer visibility vs. regions.** Toggling a whole layer with `set_layer_visible`
is the right tool for show/hide of a piece of scenery that occupies its own image
and depth — e.g. a foreground cart drawn with perspective, removed once it is taken
away. Use a **region** instead when the *same* footprint swaps between named state
images (drawer `shut`/`open`); use an **object** for an active sprite the player
interacts with via a bound hotspot. Layer visibility is persisted per room (like
region and object state), so a layer hidden by script stays hidden across room
changes and save/load.

#### World bounds

A room has no authored `size`. Its world bounds are **derived** from the layers:
each layer occupies `[origin, origin + native image size × scale)`, the world is
the union of those rects anchored at `(0,0)`, and it is floored to the room-view
size so the
world is never smaller than the visible scenery viewport. Only the right/bottom
extents grow the world; a layer at a negative `origin` spills off the top-left and
is simply never scrolled to. Anything not covered by a layer shows
`background.color` — leaving a gap is an authoring mistake, not a format error.
This keeps the world coordinate system a consequence of the art rather than a
number to keep in sync with it. Because the derivation needs image pixel
dimensions, it happens render-side when textures load (`compute_room_bounds`); the
headless room loader and geometry/camera logic never depend on it (the `Camera`
takes explicit bounds).

## Room view screen layout

```text
┌────────────────────────────────────────────┐
│                                            │
│              SCENERY  (~85%)               │
│       rooms · objects · characters         │
│          hotspots · speech                 │
├────────────────────────────────────────────┤
│              SCUMM PANEL (~15%)            │
│      command bar · verbs · inventory       │
└────────────────────────────────────────────┘
```

The scenery continues to draw in every room-view state. The bottom panel changes
according to the room-view state.

## Room view states

| State | Panel | Input | Use case |
|-------|-------|-------|----------|
| `Command` | SCUMM panel visible and enabled | Player can build commands. | Default gameplay. |
| `Dialog` | Dialog options panel | Player selects dialog options. | NPC conversations. |
| `Blocked` | Black or hidden panel | Player interaction disabled. | Scripted moments, cutscene-like actions. |

The room-view state is distinct from the command-builder state machine. The
command builder exists only while the room view is in `Command` state.

## Z-order and `ZDrawable`

Everything that draws in the scenery participates in depth ordering through a
common interface:

```text
interface ZDrawable {
  float z() const;
  void draw(target) const;
}
```

Each frame, the room view gathers all `ZDrawable`s, stable-sorts them by ascending
`z`, and draws them in order. Larger `z` values are nearer the camera and are
drawn later.

Default `z` values:

| Entity | Default z |
|--------|-----------|
| Background base | Behind all sorted drawables. |
| Background layer | Explicit layer `z`. |
| Region | Explicit `z`, or the `z` of the layer named by `over`. |
| Object | `baseline` (a floor-line world-Y) if set; else `z: auto` (the sprite's bottom edge); else numeric `z`. |
| Walk-behind | Its `baseline` (a floor-line world-Y); the masked layer patch sorts there. |
| Avatar | Its walking pivot y coordinate. |
| Speech | Drawn above scenery, not sorted as world geometry. |
| SCUMM panel | UI layer, not part of room z-order. |

An object's `z: auto` and a region's `over: <layer_id>` let scenery be authored
without hand-tuning depths: the object sorts by its pivot's world-Y like an
avatar, and the region sorts with the background layer it changes.

### Perspective objects and the object `baseline`

An avatar already sorts by a *baseline*: its depth key is the world-Y of its
walking pivot (its feet). The same idea makes a perspective object — one an avatar
can pass in front of and behind, like a cart or a table — order correctly without
arbitrary depths or splitting the art into "front/back" layers.

Give the object's foreground piece an explicit **`baseline`**: the world-Y of the
line where it meets the floor. The renderer then sorts that object at `z =
baseline`, in the *same coordinate space* as avatar feet:

- an avatar whose feet are **above** the line (smaller y → standing behind the
  object's contact line) is drawn first, so the object occludes it;
- an avatar whose feet are **below** the line (larger y → standing nearer) is
  drawn over the object.

So as the player walks past, the engine flips the occlusion at the baseline
automatically — one number with a physical meaning, not a hand-tuned `z`.

```yaml
objects:
  cart_front:                 # the near edge of the cart that should occlude feet
    image: objects/cart_front.png
    position: { x: 400, y: 360 }
    baseline: 640             # floor-contact line; sorts against avatar feet
```

The rest of the cart (the part always behind the player) is just background — a
layer or a `z: auto` object — so only the genuinely foreground piece needs a
`baseline`. `baseline` overrides `z` / `z: auto` for that object.

### Walk-behind areas

`baseline` on an object needs a separate foreground sprite. A **walk-behind area**
removes even that: it occludes using pixels sampled from an existing background
**layer**, so the perspective art lives in a single image and is never duplicated.

A walk-behind is a polygon mask over part of a layer plus a `baseline`. The engine
redraws that patch of the layer on top of the scene at `z = baseline`, sorted
against avatars exactly like a baseline object — but the pixels come straight from
the layer, so they always match the background.

```yaml
background:
  layers:
    - { id: bg, image: rooms/hall/bg.png, z: 0 }   # the whole cart is painted here
walkbehinds:
  cart:
    layer: bg                 # sample pixels from this layer
    area: [ {x: 360, y: 470}, {x: 720, y: 470}, {x: 720, y: 640}, {x: 360, y: 640} ]
    baseline: 640             # floor-contact line; sorts against avatar feet
```

The avatar walks behind the cart when its feet are above `baseline` (the patch is
drawn over it) and in front when below it. Because the patch is the same pixels
already in `bg`, the double-draw is invisible.

MVP constraint: the `area` polygon must be **convex** (it is filled as a triangle
fan). Model a concave occluder as several convex walk-behind areas sharing a
`baseline`. True per-pixel perspective (an avatar standing amid an object at many
depths at once) remains out of scope; the design-for answer would be a per-sprite
depth map, also reducible to this baseline model.

## Perspective scaling

A room may define how vertical position affects avatar scale.

```yaml
perspective:
  top:    { y: 380, scale: 0.70 }
  bottom: { y: 700, scale: 1.15 }
```

Each anchor is a floor line: a world-space `y` and the avatar render `scale` at
that line. The engine linearly interpolates an avatar's scale from its walking
pivot y between `top` and `bottom`, and clamps to the nearest anchor's scale
outside the band. This applies to every avatar in the room (player and NPCs), and
because an avatar scales about its walking pivot (feet), it stays planted as the
scale changes. Characters are usually smaller near the top of the screen and
larger near the bottom, because the bottom represents the foreground in a
front-facing room.

If `perspective` is omitted, each avatar keeps its base scale (the value it was
created with, `1.0` by default).

## Camera

The room camera is a world-space rectangle the size of the scenery viewport — the
room-view layout's top region, not the full virtual resolution (see
[coordinate systems](02-architecture-overview.md)). It follows the player and
never shows space outside the room.

### Follow behavior

- The camera follows the player by mapping the player's **reachable range** — the
  bounding box of the walkable area — onto the camera's full clamped scroll range,
  per axis. The leftmost/topmost reachable point shows the start of the
  background; the rightmost/bottommost shows the end. This aims to cover the whole
  background across a traversal, even though the player can never reach the room's
  literal edges (the walkable area is usually a strip inside the room).
- That reveal is bounded by an **on-screen clamp**: the scroll never moves so far
  that the player's pivot leaves the viewport. A margin (default 15% of the
  viewport per axis) keeps clickable floor around the player. When the walkable
  area is a thin slice of a much larger room, the raw reveal would scroll the
  player off screen — or into an unclickable sliver — and trap them; the clamp
  caps the scroll so the player stays visible, sacrificing edge coverage only as
  much as needed. Well-proportioned rooms (a walkable strip that is not tiny
  relative to the room) reach full coverage before the clamp engages. The room
  bounds remain the hard limit, so near a literal room edge the player may sit at
  the viewport edge (there is nothing further to reveal).
- The camera is always clamped to the room bounds. A room no larger than the
  scenery viewport is centered and does not scroll.
- Both axes use the same mapping. A room wider than the scenery viewport scrolls
  horizontally; a taller room scrolls vertically by the same mechanism. Vertical
  scrolling is therefore not a separate feature — it falls out of the clamp for
  tall rooms, which are uncommon.
- When no walkable area is defined, the reachable range defaults to the whole
  room, degenerating to ordinary clamped follow.

### Scripted overrides

| Call | Effect |
|------|--------|
| `camera_look_at(target)` | Snap the camera to the target and suspend follow. |
| `camera_go_to(target)` | Tween the camera to the target, yield the task, and suspend follow. |
| `camera_follow_player()` | Resume following the player. |

While a scripted override is active, automatic follow is suspended. Follow also
resumes automatically when the room view returns to `Command` state. See
[The scripting API](05-scripting-api.md) for the camera functions.

### Screen edges

`on_screen_edge(edge)` fires when the **player** reaches a room edge — not when
the camera does — so edge-triggered room exits work at the end of a scroll or in
rooms that do not scroll.

## Hotspots

A hotspot is an interactive part of the scenery. It may represent:

- a background region;
- an active object;
- an NPC;
- a purely logical area.

A hotspot has:

| Field | Meaning |
|-------|---------|
| `id` | Stable internal identifier. |
| `name` | Localized display noun shown in the command bar. |
| `area` | Optional explicit hit-test polygon. |
| `bind` | Optional binding to a visual object or region. |
| `approach` | Point the player walks toward when a command targets this hotspot. |
| `requires_approach` | Optional bool (default `false`). When `true`, the command does not run until the player reaches `approach`; until then input is blocked. When `false`, the player still walks toward `approach`, but the command fires immediately — allowing interactions from a distance. |
| `affordances` | Verbs that the UI may offer for this hotspot. |
| `default_verb` | Optional verb used on a plain click. Must be `look_at` or in `affordances`. Defaults to `look_at`. |

At least one of `area` or `bind` is required. If both are present, the hotspot is
hit when either the explicit area contains the pointer or the bound visual is hit.
This lets authors use a precise polygon, object/region collision, or both.

`id` and `name` are intentionally separate. IDs should be ASCII, stable, and
script-friendly. Names may contain spaces, accents, and localized text.

Example:

```yaml
hotspots:
  surface_sublimator:
    name: "aparato para sublimar superficies"
    area: [ ... ]
    approach: at_sublimator
    affordances: [ look_at, use ]
```

## Affordances

Affordances declare which verbs make sense to offer for a hotspot. They gate UI
selection; they do not guarantee success. The Lua handler decides the outcome.

Every hotspot and inventory item implicitly supports `look_at` even when it is not
listed; `affordances` enumerates the *additional* verbs offered. A `default_verb`
must therefore be `look_at` or one of the listed affordances.

| Affordance | Enables verb(s) |
|------------|-----------------|
| `look_at` | Look at |
| `talk_to` | Talk to |
| `pick_up` | Pick up |
| `use` | Use as one-operand command or as operand of `Use X with Y` |
| `give` | Give as recipient |
| `open` | Open |
| `close` | Close |
| `push` | Push |
| `pull` | Pull |

The first implementation shall use direct verb names as affordances. A future
version may introduce higher-level categories such as `open_closeable`, but the
runtime dispatch still resolves to concrete verbs.

## Avatars

An avatar is the visual and spatial representation of a player character or NPC.
The room does not depend on the drawing strategy used by the avatar.

Possible drawing strategies:

| Strategy | Cast `type` | Example |
|----------|-------------|---------|
| Single animated sprite | `animated_sprite` | One spritesheet with all frames. |
| Composite sprite | `composite` | Body + head + props. |
| Future skeletal/layered renderer | `skeletal` (design-for) | More advanced animation system. |

A character's appearance selects the strategy and supplies its parameters in
`cast.yaml`; see [appearance types](05-scripting-api.md). The room interacts with
avatars through a stable interface regardless of the chosen strategy.

### Player vs NPC avatars

The player avatar is a persistent world entity: it is created once when
`RoomScene` starts and survives every `change_room`. NPC avatars are room-scoped —
created on room load and destroyed on room unload. The player avatar is owned at
the world / `RoomScene` level; NPC avatars are owned by `RoomRuntime`.

The player character is named by the `RoomScene` manifest entry's `player`
parameter (a cast character id). At `RoomScene` start the engine builds the
persistent player avatar from that character's cast appearance and seats it in
`start_room` using the placement resolution order below.

A room's `avatars` entry with `player: true` does **not** create the player. It
only declares where the player is placed when this room is entered without an
explicit entry point, and its appearance binding is ignored for the player (it is
used only for NPCs). The player's appearance is fixed at game start from the cast
character named by `player`; there is no MVP script API to change it mid-game
(runtime appearance changes are design-for).

`change_room(id, entry_point?)` resolves the player's placement in order:

1. the `entry_point` argument, if given (a named point in the target room);
2. otherwise the target room's `player: true` avatar `start` point;
3. otherwise the room's `player_start` point;
4. otherwise fail loudly in development builds.

The player's position and orientation at unload are not carried into the next
room — the resolved entry point fully determines placement. Carrying position
across rooms is a design-for refinement.

`change_room` fades the view to black, loads the new room **at black**, then fades
back in (duration from the `fade_duration` scene parameter; 0 disables it). Input
is ignored during the fade-out, and the load — including the autosave on room
change — happens at black, so the swap is never seen mid-frame. Restoring a save
loads without a fade (the surrounding menu/scene transition covers it). This is the
room-level counterpart to the scene-level fade in
[02 § Scene transitions](02-architecture-overview.md).

### Avatar interface

| Method | Purpose |
|--------|---------|
| `set_position(x, y)` | Place avatar in room coordinates. |
| `position()` | Return avatar's main position, usually walking pivot. |
| `anchor(name)` | Return absolute position of a named anchor. |
| `face(direction)` | Set orientation. |
| `facing()` | Return orientation. |
| `set_scale(scale)` | Set visual scale. |
| `stand()` | Play standing animation. |
| `walk()` | Play walking animation. |
| `talk()` | Play talking animation. |
| `play(sequence)` | Play named animation sequence. |
| `play_until_end(sequence)` | Play non-looping sequence and yield script until done. |
| `move_to(target)` | Follow room pathfinding to a point or named target. |
| `draw(target)` | Draw avatar. |
| `z()` | Return depth key. |

This is the full C++ interface. The Lua script handle `avatar(id)` exposes a
deliberately smaller subset (`move_to`, `look_at`, `face`, `play`,
`play_until_end`, `position`, `anchor`); see
[The scripting API](05-scripting-api.md). Methods such as `set_position`,
`stand`, `walk`, `talk`, and `set_scale` are engine-internal and not
script-exposed — talking animation is driven by the global `talk`, and
standing/walking by movement and the default action mapping below.

Default action mapping:

| Command action | Default avatar animation |
|----------------|--------------------------|
| `look_at` | Face target, then stand. |
| `talk_to` | Talk. |
| `pick_up` | `pick_up_<direction>`, fallback to stand. |
| `open` | `open_<direction>`, fallback to stand. |
| `close` | `close_<direction>`, fallback to stand. |
| `push` | `push_<direction>`, fallback to stand. |
| `pull` | `pull_<direction>`, fallback to stand. |
| `give` | `give_<direction>`, fallback to stand. |
| `use` | Context-specific, fallback to stand. |

## Commands and SCUMM panel

The player builds commands of the form:

```text
<verb> <param1?> <param2?>
```

Parameters are hotspots or inventory items.

Supported verbs for the first implementation:

```text
Look at    Talk to    Pick up
Use        Give       Open
Close      Push       Pull
```

`Walk to` is not a verb button. Clicking a walkable floor point moves the player
there. Clicking a point **outside** the walkable area (that is not a hotspot)
routes the player to the nearest reachable point on the walkable boundary rather
than doing nothing. The target uses click-to-move only; keyboard-driven avatar
movement from the prototype is not part of the design.

Clicking empty scenery (no hotspot) while a command is being built **cancels**
the command and returns the builder to `IDLE`; the same click while already
`IDLE` walks the player as above.

### Default verb on click

Left-clicking a hotspot or inventory item with no verb currently selected starts
a command immediately using that entity's **default verb**:

- if the entity declares `default_verb` in YAML (which must appear in its
  `affordances`), that verb is used;
- otherwise the default verb is `look_at`.

The command then proceeds through the normal state machine, so a default verb
that needs a second operand (for example a `combinable` item under `use`) still
advances to `EXPECTING_PARAM2_*`. Left-clicking walkable floor with no verb
selected walks the player. Right-click as an "examine" shortcut is a design-for
input addition, not part of the MVP.

## Command argument types

| Type | Meaning |
|------|---------|
| `ROOM_OBJECT` | Hotspot in the current room, including NPC hotspots. |
| `INVENTORY_OBJECT` | Item in the inventory. |
| `ANY_OBJECT` | Room object or inventory object. |

The implementation may internally distinguish `NPC`, `REGION`, `OBJECT`, and
`EXIT`, but the command builder only needs the argument classes above.

## Verb argument rules

| Verb | Param 1 | Param 2 | Connector | Notes |
|------|---------|---------|-----------|-------|
| `look_at` | `ANY_OBJECT` | — | — | Display description or default fallback. |
| `talk_to` | `ROOM_OBJECT` | — | — | Usually requires NPC affordance. |
| `pick_up` | `ROOM_OBJECT` | — | — | Adds item or returns refusal. |
| `open` | `ANY_OBJECT` | — | — | Requires `open` affordance. |
| `close` | `ANY_OBJECT` | — | — | Requires `close` affordance. |
| `push` | `ANY_OBJECT` | — | — | Requires `push` affordance. |
| `pull` | `ANY_OBJECT` | — | — | Requires `pull` affordance. |
| `use` | `ANY_OBJECT` | optional `ANY_OBJECT` | `with` | One-operand or two-operand. |
| `give` | `INVENTORY_OBJECT` | `ROOM_OBJECT` | `to` | Handler runs on recipient. |

Localized connectors are handled by the UI formatter, not by the command model.
Verb labels and connectors are looked up by key in the
[UI strings resource](06-data-formats.md#ui-strings--stringslangyaml); they are
never hardcoded. For Spanish the strings file supplies, for example:

| Verb | Connector |
|------|-----------|
| `use` | `con` |
| `give` | `a` |

## Command builder state machine

The command builder exists only in the room-view `Command` state.

### States

| State | Meaning |
|-------|---------|
| `IDLE` | No command is being built. Hover text may show the hovered verb or object. |
| `VERB_SELECTED` | A verb has been selected; the builder chooses the required next parameter state. |
| `EXPECTING_PARAM1_ROOM_OBJECT` | Waiting for first argument; room hotspots only. |
| `EXPECTING_PARAM1_INVENTORY_OBJECT` | Waiting for first argument; inventory only. |
| `EXPECTING_PARAM1_ANY_OBJECT` | Waiting for first argument; room or inventory. |
| `EXPECTING_PARAM2_ROOM_OBJECT` | Waiting for second argument; room hotspots only. |
| `EXPECTING_PARAM2_INVENTORY_OBJECT` | Waiting for second argument; inventory only. |
| `EXPECTING_PARAM2_ANY_OBJECT` | Waiting for second argument; room or inventory. |
| `COMMAND_READY` | Command has all required data and can be executed. |
| `COMMAND_EXECUTING` | Player walks to approach point and Lua handler runs. |

### Transition table

| Current state | Input | Condition | Next state | Action |
|---------------|-------|-----------|------------|--------|
| `IDLE` | Click verb | Verb exists | `VERB_SELECTED` | Store verb. |
| `IDLE` | Click walkable floor | No verb selected | `COMMAND_EXECUTING` | Execute walk command. |
| `IDLE` | Click hotspot/inventory | No verb selected | Depends on default verb | Start command with the entity's `default_verb`, or `look_at` if none. |
| `VERB_SELECTED` | Internal | Verb expects room param 1 | `EXPECTING_PARAM1_ROOM_OBJECT` | — |
| `VERB_SELECTED` | Internal | Verb expects inventory param 1 | `EXPECTING_PARAM1_INVENTORY_OBJECT` | — |
| `VERB_SELECTED` | Internal | Verb expects any param 1 | `EXPECTING_PARAM1_ANY_OBJECT` | — |
| `EXPECTING_PARAM1_*` | Click valid object | Type and affordance match | Depends on verb/object | Store param 1. |
| `EXPECTING_PARAM1_*` | Click invalid object | Type or affordance mismatch | Same state | Optional feedback. |
| `EXPECTING_PARAM1_*` | Click another verb | — | `VERB_SELECTED` | Replace verb and clear params. |
| `EXPECTING_PARAM1_*` | Cancel | — | `IDLE` | Clear command. |
| `EXPECTING_PARAM2_*` | Click valid object | Type and affordance match | `COMMAND_READY` | Store param 2. |
| `EXPECTING_PARAM2_*` | Click invalid object | Type or affordance mismatch | Same state | Optional feedback. |
| `EXPECTING_PARAM2_*` | Click another verb | — | `VERB_SELECTED` | Replace command. |
| `EXPECTING_PARAM2_*` | Cancel | — | `IDLE` | Clear command. |
| `COMMAND_READY` | Internal | — | `COMMAND_EXECUTING` | Dispatch command. |
| `COMMAND_EXECUTING` | Command finished | — | `IDLE` | Clear command and restore UI. |

The **Cancel** input is the player clicking empty scenery (a point that is
neither a hotspot nor the panel) while a command is being built.

### Verb-to-state mapping

| Verb | After verb selected | After param 1 |
|------|---------------------|---------------|
| `look_at` | `EXPECTING_PARAM1_ANY_OBJECT` | `COMMAND_READY` |
| `talk_to` | `EXPECTING_PARAM1_ROOM_OBJECT` | `COMMAND_READY` |
| `pick_up` | `EXPECTING_PARAM1_ROOM_OBJECT` | `COMMAND_READY` |
| `open` | `EXPECTING_PARAM1_ANY_OBJECT` | `COMMAND_READY` |
| `close` | `EXPECTING_PARAM1_ANY_OBJECT` | `COMMAND_READY` |
| `push` | `EXPECTING_PARAM1_ANY_OBJECT` | `COMMAND_READY` |
| `pull` | `EXPECTING_PARAM1_ANY_OBJECT` | `COMMAND_READY` |
| `use` | `EXPECTING_PARAM1_ANY_OBJECT` | `COMMAND_READY` or `EXPECTING_PARAM2_ANY_OBJECT` |
| `give` | `EXPECTING_PARAM1_INVENTORY_OBJECT` | `EXPECTING_PARAM2_ROOM_OBJECT` |

### `use` disambiguation

`use` may be immediate or combinational. Only an inventory item can be the first
operand of a two-operand `use`; a room hotspot as the first operand is always
one-operand (`Use drawer`). To combine, the inventory item is the first operand
and the room hotspot or other item is the second (`Use key with drawer`).

| Param 1 | Condition | Next state |
|---------|-----------|------------|
| Room hotspot | always | `COMMAND_READY` |
| Inventory item | `combinable: false` | `COMMAND_READY` |
| Inventory item | `combinable: true` | `EXPECTING_PARAM2_ANY_OBJECT` |

Because only inventory items carry the `combinable` flag, the builder never needs a
one-vs-two-operand signal on hotspots. For the MVP, `combinable` is an acceptable
inventory-item flag. Later versions may replace it with a richer rule system where
an item may be immediate, combinable, or both depending on context.

## Command model

The command builder produces a language-independent command object.

```cpp
struct Command {
    Verb verb;
    ObjectRef param1;
    std::optional<ObjectRef> param2;
};
```

Script-friendly form:

```lua
{
  verb = "use",
  param1 = "lighter",
  param2 = "wood"
}
```

The UI formats this command into readable text using localized verb labels,
connectors, and object display names.

## Top bar behavior

The command bar previews the command currently being built.

| Command state | Hovered element | Display text example |
|---------------|-----------------|----------------------|
| `IDLE` | Nothing | Empty string or default prompt. |
| `IDLE` | Verb | `Use` |
| `IDLE` | Room object | `drawer` / localized name. |
| `IDLE` | Inventory item | `key` / localized name. |
| `IDLE` | Walkable floor | The walk label (`Ir a` / localized), from the [UI strings resource](06-data-formats.md#ui-strings--stringslangyaml). |
| `EXPECTING_PARAM1_*` | Nothing | `Use` |
| `EXPECTING_PARAM1_*` | Valid object | `Use key` |
| `EXPECTING_PARAM1_*` | Invalid object | `Use` |
| `EXPECTING_PARAM2_*` | Nothing | `Use key with` |
| `EXPECTING_PARAM2_*` | Valid object | `Use key with door` |
| `EXPECTING_PARAM2_*` | Invalid object | `Use key with` |
| `COMMAND_EXECUTING` | Any | Last command text, optionally styled as executing. |

The top bar shall not build logic from strings. It formats the internal command
state for the player.

## Building and running a command

When a command becomes ready:

1. The room view switches to `COMMAND_EXECUTING` within the command builder.
2. If the command target has an `approach` point, the player avatar walks there.
   An `approach` point should lie inside the walkable area; if it does not, the
   engine routes the avatar to the nearest walkable point and, in development
   builds, logs a warning.
3. The avatar faces the target when appropriate.
4. The engine dispatches the command to the Lua handler.
5. The handler may run scripted actions, change state, start a dialog, change a
   room, or return a caption.
6. When the command finishes, the command builder returns to `IDLE`.

For two-operand verbs, inventory-item behavior has priority when the first
operand is an inventory item and `inventory.lua` defines a matching handler.
Otherwise the handler is invoked on the second operand.

| Command | Handler |
|---------|---------|
| `Use key with door`, with `inventory.key.use` defined | `inventory.key.use("door")` |
| `Use key with door`, without `inventory.key.use` | `door.use("key")` |
| `Give map to Stan`, with `inventory.map.give` defined | `inventory.map.give("stan")` |
| `Give map to Stan`, without `inventory.map.give` | `stan.give("map")` |

## Dialog system

A dialog is a tree of NPC lines and player choices. Dialogs are written in Lua so
nodes can run script code, branch on conditions, set state, move characters, or
give items.

A dialog is started with:

```lua
start_dialog("stan")
```

The dialog id is also the **speaking NPC's character id**: it selects the dialog
file (`dialogs/stan.lua`) and the cast character whose speech color and position
render the node `npc` lines. The dialog file therefore declares no speaker field.

While a dialog runs, the room view enters `Dialog` state. The SCUMM panel is
replaced by dialog options. When the dialog ends, the room view returns to
`Command` state.

### Dialog format

```lua
return {
  start = "greet",

  greet = {
    npc = "¿Buscás un barco?",
    options = {
      { "Contame más.", to = "ships" },
      { "¿Cuánto cuesta?", to = "price", once = true },
      { "Chau.", to = END },
    },
  },

  ships = {
    npc = "Los mejores del lago.",
    to = "greet",
  },

  price = {
    npc = "Cinco mil piezas de ocho.",
    options = {
      {
        "Trato hecho.",
        when = function() return has_item("gold") end,
        run = function()
          remove_item("gold")
          add_item("ship")
        end,
        to = END,
      },
      { "Muy caro.", to = "greet" },
    },
  },
}
```

| Field | Applies to | Meaning |
|-------|------------|---------|
| `start` | Dialog | Entry node id. |
| `text_anchor` | Dialog | Optional room point name. NPC speech for this dialog is drawn at that fixed point instead of following the NPC avatar. Useful for off-screen or static speakers (e.g. a talking skull). |
| `on_enter` | Dialog | Optional setup callback. |
| `on_exit` | Dialog | Optional cleanup callback. |
| `npc` | Node | NPC line or list of lines. |
| `options` | Node | Player choices. |
| `to` | Node or option | Next node id or `END`. |
| `when` | Option | Visibility predicate, **re-evaluated every time the node's options are shown**. Return false to hide the option until its condition holds. |
| `run` | Option | Code executed when selected. |
| `once` | Option | Option is hidden permanently after it is used. Consumption is **persisted** (survives leaving and restarting the dialog, and save/load). |
| `silent` | Option | Player line is not spoken aloud. |

### Dialog execution

1. Speak the node's `npc` line or lines.
2. If the node has options, display available options whose `when` passes.
3. Wait for player selection.
4. Speak the selected player line unless `silent` is true.
5. Run the option's `run` callback, if present.
6. Consume `once` options.
7. Follow `to`.
8. End when `to == END`.

A node declares **either** `options` **or** a `to`, never both (the loader rejects
a node that has both). When an options node's choices are all hidden — every one
consumed by `once` or filtered out by a failing `when` — it has nothing left to
offer and the dialog ends. This is what lets a conversation "run dry" gracefully
once everything worth saying has been said; route the player somewhere instead by
keeping a always-available fallback option (e.g. a "Chau." line `to = END` or back
to a hub node).

### Evolving dialogs

A conversation is not a fixed menu: `once`, `when`, and the state stores together
let the option list **prune and unlock as the player learns things and the world
changes**. Three building blocks:

- **Ask-once lines.** Mark an option `once = true` so a question like "¿Quién
  sos?" appears the first time and never again. Consumption is persisted under an
  engine-reserved `__dialog.<npc>.<node>.<index>` key in global state (it folds
  into `GameState`), so it survives leaving the dialog, re-entering it, and
  save/load — the player never re-asks a settled question.
- **Conditional unlocking.** Gate an option with `when = function() ... end`. The
  predicate is checked each time the node's options are displayed, so an option can
  appear only once a fact is known or an item is held, and a path can be closed off
  by the same mechanism. Read the engine state stores from `when` / `run`
  (`get_state`, `get_room_state`, `has_item`) and write them from `run` / node
  callbacks (`set_state`, …) — never from Lua locals, which are not persisted.
- **Branch reshaping.** Combine the two: a `run` callback sets a state flag, and
  other options' `when` predicates read it, so choosing one line enables or
  disables others on the next pass through the node.

```lua
return {
  start = "greet",

  greet = {
    npc = "¿Y vos quién sos?",
    options = {
      -- Asked at most once, ever (persisted across save/load).
      { "Soy Julia.", to = "greet", once = true,
        run = function() set_state("skull.met_julia", true) end },

      -- Appears only after we've introduced ourselves.
      { "¿Conociste a mi abuelo?", to = "grandfather",
        when = function() return get_state("skull.met_julia") end },

      -- Appears only once the lever puzzle is solved, then never repeats.
      { "El mecanismo ya está abierto.", to = "reward", once = true,
        when = function() return get_room_state("lab.lever_pulled") end },

      { "Nada, me voy.", to = END },
    },
  },

  grandfather = {
    npc = "Lo recuerdo. Dejó algo para vos.",
    run = function() set_state("skull.grandfather_topic", true) end,
    to = "greet",
  },

  reward = {
    npc = "Tomá esto.",
    run = function() add_item("amulet") end,
    to = "greet",
  },
}
```

On the first visit only "Soy Julia." and "Nada, me voy." are offered. After the
introduction the grandfather line unlocks; once the lab lever is pulled the
mechanism line appears (and disappears after use). Because "Nada, me voy." has no
`when` or `once` it is always available, so `greet` never runs dry and the player
can always leave — drop that fallback (or gate it) and the node would instead end
the dialog on its own once every option is consumed or filtered out.

## Speech

Speech is rendered over the scenery near the speaker. The speech font is the
`RoomScene` `font` parameter (a logical path; an engine default applies when
omitted); per-character speech color and style come from the cast file.

Every spoken line shall have a stable id or be representable by one. This enables
future voice-over attachment.

Speech rendering supports:

- wrapping;
- configurable duration;
- skipping;
- per-character color;
- optional positioning override.

When `talk` is called without an explicit duration, the engine computes one from
the line length:

```text
duration = clamp(1.0 s, 0.5 s + 0.06 s × character_count, 7.0 s) / text_speed
```

`text_speed` is the player text-speed setting (default 1.0; higher is faster).
The computed duration is an upper bound — speech is always skippable, so the
player may advance sooner. When a voice-over clip is attached to a line, the clip
length overrides this estimate.

## Characters and movement

Characters move through room geometry. A destination must be inside the walkable
area and outside all obstacles. The pathfinder returns waypoints, and the avatar
walks them segment by segment.

Characters scale with the room's perspective function as they move. Their z-order
is normally their walking pivot y coordinate.

## Inventory

Inventory items are command parameters. They have ids, localized names, optional
descriptions, and affordances.

Inventory uses the same static-data / behavior split as rooms:

| File | Purpose |
|------|---------|
| `inventory.yaml` | Static item definitions: names, descriptions, affordances, default verbs, optional future icons. |
| `inventory.lua` | Item verb handlers and shared inventory behavior. |

Example `inventory.yaml`:

```yaml
items:
  key:
    name: "la llave"
    affordances: [ look_at, use ]
    combinable: true
```

Example `inventory.lua`:

```lua
local inventory = {}

inventory.key = {
  look_at = function()
    return "Una llave pequeña."
  end,

  use = function(target)
    if target == "door" then
      remove_item("key")
      change_room("hall")
      return "La llave funciona."
    end
    return "No parece servir."
  end,
}

return inventory
```

The MVP displays inventory items as text using their localized `name`. Each item
may later declare an optional `icon` — a spritesheet frame id or image logical
path — which the SCUMM panel draws instead of text. Inventory items support the
same optional `default_verb` as hotspots: it must be one of the item's
`affordances` and defaults to `look_at`.

## Migration notes

Prototype divergences and refactoring tasks are tracked separately in
[migration notes](migration-notes.md). They are not part of the target design.
