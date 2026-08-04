# Scenery authoring

A practical, recipe-style guide to building what fills a room: **layers,
lighting, regions, objects, NPCs, hotspots, obstacles, walk-behinds, and perspective**.
Each section is "I want to… → do this", with the YAML that declares it and the
Lua that drives it.

!!! tip "The golden rule"
    **YAML** says *what exists* (layout, art paths, polygons, placements).
    **Lua** says *what happens* (verb handlers, lifecycle hooks, scripted action).
    Never put geometry or asset paths in Lua.

A room is two files loaded by id: `rooms/<id>.yaml` (static layout) and
`rooms/<id>.lua` (behavior). Asset paths are **logical** and resolved relative to
the room's folder (e.g. `lab/00.png` → `rooms/lab/00.png`). For the exhaustive
field list see [Data formats](data-formats.md); for every Lua call see the
[Lua API reference](lua-api.md).

---

## Background layers

A room background is a **list of layers**, not one image — so you can stack a
backdrop, parallax pieces, and foreground occluders.

```yaml
background:
  color: { r: 24, g: 28, b: 40 }   # shows wherever no layer covers
  layers:
    - { id: bg,    image: lab/00.png, z: 0 }
    - id: shelf                       # a foreground piece the player walks behind
      image: lab/shelf.png
      z: 600
      origin: { x: 740, y: 300 }      # top-left placement (default 0,0)
      scale: 1.0                      # uniform, aspect-locked (authoring aid)
```

- **`z`** is draw depth — larger is nearer the camera. See [Depth](#depth-and-z-order).
- **Hide/show a layer at runtime** (it must have an `id`): `set_layer_visible("shelf", false)`. Persisted per room.

> Use a layer for static scenery and foreground occluders. For a piece that
> *changes* use a **region**; for one that *moves or animates* use an **object**.

## Dynamic lighting

Keep source artwork bright or neutral, then establish the room's baseline
darkness with `ambient` and place radial `omni` or directional `spot` lights:

```yaml
lighting:
  ambient: { color: [0.65, 0.70, 0.85], intensity: 0.4 }
  normal_map: { image: room_normals.png, strength: 0.8 }
  lights:
    - id: lamp
      type: omni
      at: { x: 520, y: 210 }
      radius: 280
      color: [1.0, 0.78, 0.45]
      intensity: 0.9
      modulation: { type: flicker, amount: 0.04, speed: 5 }
    - id: torch
      type: spot
      attach: player
      offset: { x: 0, y: -55 }
      range: 360
      follow_facing: true
      angle: 50
      softness: 12
  occluders:
    - id: closed_door
      area: [{ x: 720, y: 180 }, { x: 720, y: 520 }]
  projected_shadows:
    source: lamp
    casters: all
```

Use `sine` for regular pulsing, `flicker` for fire/torches, and `faulty` for
lamps with abrupt dropouts. Modulation is part of the one lighting pass. Lights
may also attach to `avatar:<id>` or `object:<id>`; a missing or hidden attachment
temporarily suppresses the light. Room `post_process` grading runs afterward, so
use it for the final palette/mood rather than repeating a grade on every layer.

### Tune lighting and grading in game

When the manifest enables `development.edit_mode`, press **F9** from the normal
room command view to replace the SCUMM panel with a live tuning overlay. While
it is open, all mouse and keyboard input is captured by the overlay; the room,
animated lights, and post-processing continue to render.

- **Ambient** edits the room's RGB ambient colour and intensity.
- **Lights** selects each authored light and exposes core colour/intensity,
  radius/cone geometry, and modulation controls.
- **Grading** selects any post-process pass, parameter, and vector component.
  Common grading names receive useful ranges; other numeric shader parameters
  receive a range derived from their current value.
- **View: Live/YAML** compares the working values with the loaded room data.
- **Reset** restores the working copy without reloading the room.
- **Copy YAML** (or `Ctrl+C`) copies complete `lighting:` and `post_process:`
  sections, including preserved normal maps, occluders, and projected shadows.

Press **F9** or **Escape** to close the overlay. Its changes are intentionally
temporary until the copied YAML is pasted into the room file; scripts and saved
game state are not modified.

Scripts may switch an authored light or change its peak intensity at runtime:

```lua
light("lamp"):disable()
light("torch"):set_intensity(0.55, 0.4) -- smooth 0.4-second transition
light_occluder("closed_door"):disable()
```

These overrides reset when the room reloads. Reapply them from `on_load` using
saved story state when the change must persist. Modulation continues to multiply
the runtime intensity.

`occluders` block direct light along authored polygon boundaries. A two-point
area is one wall segment; a larger area is closed automatically. Use the runtime
handle when a door or barrier changes. `projected_shadows.source` follows a
dynamic light (including attachments, modulation, and runtime intensity); use
the older `light: {x, y}` form for an independent fixed direction.

An optional room-space tangent normal map adds surface direction without extra
passes. Neutral/flat is RGB `(128, 128, 255)`; red points right, green points
down, and blue points out of the image. It is sampled over the complete composed
scene, so keep moving-character corridors close to flat unless their lighting
should inherit the receiver surface.

The engine renders the first eight visible lights that overlap the camera. See
[Data formats](data-formats.md#room--roomsidyaml) for all cone, colour, and
modulation fields.

---

## Depth and z-order

Everything in the scenery is sorted by a `z` each frame; the player and NPCs sort
by their **feet** (walking-pivot y). Three tools place scenery in that order:

| Want | Use |
|------|-----|
| A fixed depth | `z:` on the layer/region/object. |
| A piece the player passes **in front of and behind** | a **`baseline`** — the world-Y floor line where the piece meets the ground. The avatar is occluded when its feet are above the line, drawn in front when below. |
| The player to walk behind part of a **background layer** (no separate art) | a **walk-behind**. |

```yaml
objects:
  cart_front: { sprite: ext/cart.png, position: { x: 400, y: 360 }, baseline: 640 }

walkbehinds:
  counter:
    layer: bg                          # samples pixels from this layer
    area: [ {x: 360, y: 470}, {x: 720, y: 470}, {x: 720, y: 640}, {x: 360, y: 640} ]
    baseline: 640
```

Walk-behind `area` must be **convex** (split a concave shape into several).

### Perspective (fake depth)

Scale avatars by how far "up" the floor they stand:

```yaml
perspective:
  top:    { y: 380, scale: 0.70 }   # far line
  bottom: { y: 700, scale: 1.15 }   # near line
```

Avatars between the lines interpolate; outside, they clamp. Omit it to keep every
avatar at its base scale.

---

## Regions — scenery that changes

A **region** is a polygon patch of the background that swaps between named
**state images** (a drawer shut/open, a screen on/off). State is set from Lua and
saved per room.

```yaml
regions:
  drawer:
    area: [ {x: 100, y: 400}, {x: 240, y: 400}, {x: 240, y: 470}, {x: 100, y: 470} ]
    over: bg                  # inherit this layer's z (or use z:/baseline:)
    initial: shut
    states:
      shut: lab/drawer_shut.png
      open: lab/drawer_open.png   # a state image may be empty to draw nothing
```

```lua
-- in rooms/lab.lua
open = function() set_region_state("drawer", "open") end
```

`get_region_state("drawer")` reads it back. A region's `area` can also back a
hotspot — see [Hotspots](#hotspots-making-things-interactive).

---

## Objects — props, static or animated

An **object** is a sprite placed in the room. It is *visual only* — interactivity
comes from a [hotspot](#hotspots-making-things-interactive). An object's `sprite`
is either a **static image**, an **animation** (`*.anim.yml`), or a composed
hierarchy (`*.composite.yml`). Animated and composite objects share the same Lua
surface.

```yaml
objects:
  # static prop
  box:    { sprite: ext/objects/box.png, position: { x: 380, y: 360 } }
  # animated object: an AnimatedSprite, like an avatar
  fan:
    sprite: lab/fan.anim.yml
    sequence: spin           # initial looping sequence
    position: { x: 900, y: 300 }   # = the sprite pivot for animated objects
  vehicle:
    sprite: vehicles/bus.composite.yml
    sequence: stopped
    position: { x: 1400, y: 540 }
```

- `z:` is `auto` by default (sorts by the scaled bottom edge); set a number or a `baseline:` for perspective pieces.
- `scale:` resizes it (uniform, aspect-locked). The [room editor](tools/room-editor.md) sets `position`/`scale` visually.
- `rotation:` is clockwise degrees around the visual pivot and can also be changed from Lua.

Drive it from Lua with the **`object(id)` handle**:

```lua
object("box"):set_scale(1.5)
object("box"):move_to({ x = 700, y = 520 }, 200)   -- free linear move; yields until it arrives

object("fan"):play("spin")                          -- loop a sequence
object("vehicle"):play("moving")                   -- may animate independent parts
object("door"):play_until_end("open")               -- one-shot; yields until it finishes
show_object("box"); hide_object("box")              -- visibility (persisted per room)
```

`move_to`/`play_until_end` **block the calling task** — run them inside `spawn(function() … end)`.

---

## NPCs — characters in the room

An NPC is a [cast](lua-api.md) character placed in the room's `avatars:` list
(without `player: true`). It is drawn from its cast appearance and lives only
while the room is loaded.

```yaml
avatars:
  - { id: player,       start: player_start, player: true }
  - { id: delivery_guy, start: at_door, orientation: left }   # an NPC
```

**Make an NPC appear conditionally** — leave it out of `avatars:` and spawn it
from `on_load` against saved state:

```lua
function room.on_load()
  if get_state("intro.delivery_arrived") then
    spawn_npc("delivery_guy", "at_door", "left")   -- or { x=.., y=.. }
  end
end
```

`despawn_npc(id)` removes it. Move/turn any avatar (player or NPC) with the
**`avatar(id)` handle**:

```lua
spawn(function()
  avatar("delivery_guy"):move_to("counter")   -- pathfinds, yields until arrival
  avatar("delivery_guy"):face("down")
  talk("delivery_guy", "Acá está su paquete.")
end)
```

---

## Hotspots — making things interactive

A **hotspot** is what the player can act on. It has a hit area and the **verbs**
(affordances) the UI offers. The hit area is either an explicit polygon or a
**`bind`** to a visual — and a bind tracks a *moving* target:

```yaml
hotspots:
  door:                              # fixed polygon
    name: puerta
    area: [ {x: 489, y: 211}, {x: 547, y: 211}, {x: 547, y: 334}, {x: 489, y: 334} ]
    approach: at_door                # walk here first, then act
    affordances: [ look_at, open ]
  drawer:    { name: cajón,  bind: region:drawer, affordances: [ look_at, open ] }
  fan:       { name: ventilador, bind: object:fan, affordances: [ look_at, use ] }
  repartidor:{ name: repartidor, bind: npc:delivery_guy, affordances: [ look_at, talk_to ] }
```

- **`bind: object:<id>` / `region:<id>` / `npc:<id>`** hit-tests the bound visual's *live* bounds — so a hotspot on a moving object or NPC follows it, and is inactive while that NPC/object is absent.
- **`approach`** (a point) makes it walk-then-act; omit it (or on a moving bind) and the verb fires immediately.
- Toggle interactivity: `enable_hotspot(id)` / `disable_hotspot(id)`.

Handlers live in the room's Lua, keyed by hotspot id then verb:

```lua
room.hotspots = {
  door = {
    look_at = function() return "La puerta da al pasillo." end,
    open    = function() change_room("hall", "from_lab") end,
  },
  repartidor = {
    talk_to = function() start_dialog("delivery_guy") end,
  },
}
```

A verb handler returns a caption string (shown as speech) or speaks via `talk()`.
The noun (`name`) is what the command bar shows; keep ids ASCII and `name`s
localized.

---

## Obstacles — shaping where the player can walk

The **walkable** polygon is where the player can go; **obstacles** are polygons
subtracted from it (furniture the player must route around). Name an obstacle to
toggle it from script — e.g. a crate that blocks the floor until it's moved:

```yaml
walkable: [ {x: 369, y: 527}, {x: 1166, y: 612}, {x: 295, y: 608} ]
obstacles:
  - [ {x: 367, y: 390}, {x: 460, y: 390}, {x: 460, y: 354}, {x: 367, y: 354} ]   # unnamed
  - { id: crate, area: [ {x: 470, y: 455}, {x: 560, y: 455}, {x: 560, y: 520}, {x: 470, y: 520} ] }
```

```lua
disable_obstacle("crate")   -- player can now path through where it was
enable_obstacle("crate")    -- block it again
```

Obstacle enable/disable is saved per room. The [room editor](tools/room-editor.md)
draws the walkable area and obstacles (press F1 in-game to see them too).

---

## Putting it together

A lab where a delivery guy appears once a flag is set, paces while you can talk to
him, and a crate blocks a doorway until you clear it:

```yaml
# rooms/lab.yaml (excerpt)
objects:
  crate_prop: { sprite: lab/crate.png, position: { x: 470, y: 380 } }
obstacles:
  - { id: crate, area: [ {x: 470, y: 455}, {x: 560, y: 455}, {x: 560, y: 520}, {x: 470, y: 520} ] }
hotspots:
  crate_prop: { name: caja, bind: object:crate_prop, affordances: [ look_at, pull ] }
  repartidor: { name: repartidor, bind: npc:delivery_guy, affordances: [ look_at, talk_to ] }
```

```lua
-- rooms/lab.lua (excerpt)
function room.on_load()
  if get_state("delivery.arrived") then
    spawn_npc("delivery_guy", "at_door")
  end
end

room.hotspots = {
  crate_prop = {
    pull = function()
      object("crate_prop"):move_to({ x = 300, y = 380 }, 120) -- slide it aside
      disable_obstacle("crate")                               -- floor now clear
      return "Empujo la caja a un lado."
    end,
  },
  repartidor = { talk_to = function() start_dialog("delivery_guy") end },
}
```

---

## Where to go next

- [Lua API reference](lua-api.md) — every function and handle.
- [Data formats](data-formats.md) — every field for rooms, cast, manifest.
- [Tools](tools/index.md) — the room editor, spritesheet packer, and chroma-key lab for preparing this art.
