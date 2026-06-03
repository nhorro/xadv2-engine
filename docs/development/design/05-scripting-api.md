# The scripting API

A game is static data plus behavior:

```text
YAML = structure, assets, geometry, appearances, affordances
Lua  = behavior, rules, dialog branching, scripted actions
```

Lua runs as cooperative coroutines. Blocking-looking calls such as `wait`,
`talk`, `avatar(id):move_to`, and `avatar(id):play_until_end` yield to the engine
loop instead of freezing the frame.

## Game wiring

A standard game is organized as:

```text
game.yaml          manifest: engine config + scene list + entry scene
strings/
  es.yaml          engine-emitted UI strings (verbs, connectors, menus)
cast.yaml          appearances + characters
game.lua           global logic and shared fallbacks
inventory.yaml     static inventory item definitions
inventory.lua      inventory item behavior
rooms/
  study.yaml       static room definition
  study.lua        room behavior
dialogs/
  stan.lua         dialog tree
```

The manifest declares the top-level scenes. Its `RoomScene` entry points at the
game cast, global logic file, inventory files, room directory, start room, and the
cast character id that is the player.

Example manifest excerpt:

```yaml
scenes:
  - id: room_view
    type: RoomScene
    parameters:
      cast: cast.yaml
      logic: game.lua
      inventory: inventory.yaml
      inventory_logic: inventory.lua
      rooms: rooms
      start_room: study
      player: julia
```

Room ids resolve to files:

```text
change_room("study") -> rooms/study.yaml + rooms/study.lua
```

Dialog ids resolve to files, and by default the same id names the speaking NPC
character. A second argument overrides the speaker, so one NPC can have several
topic-named dialogs:

```text
start_dialog("stan")                       -> dialogs/stan.lua, spoken by "stan"
start_dialog("skull_trauma_cause",         -> dialogs/skull_trauma_cause.lua,
             "schneider")                      spoken by cast character "schneider"
```

## Cast and appearances

Cast is static data and lives in YAML.

```yaml
appearances:
  julia_field:
    type: composite                       # CompositeSprite (gfx layer)
    composite: anims/julia.composite.yaml
    shadow:
      size: { x: 70, y: 18 }
      color: { r: 0, g: 0, b: 0, a: 70 }

  guard:
    type: animated_sprite                 # single AnimatedSprite (gfx layer)
    sprite: anims/guard.anim.yaml
    shadow:
      size: { x: 60, y: 16 }
      color: { r: 0, g: 0, b: 0, a: 70 }

characters:
  julia:
    appearance: julia_field
    name: "Julia"
    speech_color: { r: 255, g: 255, b: 180 }

  schneider:
    appearance: julia_field
    name: "Dra. Schneider"
    speech_color: { r: 120, g: 220, b: 255 }
```

| Section | Meaning |
|---------|---------|
| `appearances` | Reusable visual definitions. |
| `characters` | Character ids, display names, speech style, and appearance binding. |

Character ids are stable script identifiers. Display names are localized text.

### Appearance types

An appearance selects which avatar drawing strategy implements it and supplies
that type's parameters. The avatar interface is identical regardless of type
(see the avatar strategies in
[Third-person point & click concepts](04-point-and-click-concepts.md)); only
construction differs.

| `type` | Parameters | Backed by | Scope |
|--------|------------|-----------|-------|
| `animated_sprite` | `sprite`: an `*.anim.yaml` | `pac::gfx::AnimatedSprite` | MVP |
| `composite` | `composite`: a `*.composite.yaml` | `pac::gfx::CompositeSprite` | MVP |
| `skeletal` | renderer-specific | future layered/skeletal renderer | Design-for |

`shadow` is a common, optional appearance field handled by the point-and-click
layer for every type. The animated-sprite and composite file formats are
specified in [2D game concepts](03-2d-game-concepts.md).

## Room scripts

A room script returns a table containing lifecycle hooks and hotspot verb
handlers.

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
  },

  door = {
    use = function(item)
      if item == "key" then
        remove_item("key")
        change_room("hall")
        return "La llave funciona."
      end
      return "No parece servir."
    end,
  },
}

return room
```

### Room lifecycle hooks

| Hook | Called when |
|------|-------------|
| `on_load()` | Room has been loaded and initialized. |
| `on_unload()` | Room is about to be unloaded. |
| `on_screen_edge(edge)` | Player reaches a room screen edge (`left`/`right`/`top`/`bottom`); all four fire. |
| `on_zone_enter(zone)` | Player enters a zone polygon. |
| `on_zone_exit(zone)` | Player exits a zone polygon. |

### Hotspot handler signatures

| Verb kind | Example command | Handler location | Signature |
|-----------|-----------------|------------------|-----------|
| Single operand | `Look at drawer` | Target hotspot | `function()` |
| Single operand | `Open drawer` | Target hotspot | `function()` |
| Two operands: use | `Use key with door` | Second operand, if no inventory handler exists | `function(first_id)` |
| Two operands: give | `Give map to Stan` | Second operand, if no inventory handler exists | `function(first_id)` |

A per-hotspot handler is bound to its own target, so it receives only the
operands it does not already own: nothing for a single-operand verb, and the
other operand's id for a two-operand verb. Generic fallbacks in `game.lua`
receive the full operand list instead (see the Global game logic section).

A handler may:

- return a caption string;
- call engine API functions;
- start a dialog;
- move avatars;
- change room or global state;
- spawn longer scripted tasks.

If a handler returns a string, the engine displays it as speech or as configured
for command feedback. A handler need not return a string to be valid: one that
acts and speaks via `talk(...)` without returning text still shows that line. The
"nothing happens" fallback caption appears only when the handler returns no string
**and** did not speak during the command. (So `talk("julia", "...")` with no
`return` shows the line, not the fallback.)

## Global game logic

`game.lua` contains shared behavior that is not local to one room:

- default verb fallbacks;
- global puzzle rules;
- helper functions;
- shared events;
- world-state initialization.

Example:

```lua
local game = {}

game.fallbacks = {
  look_at = function(target)
    return "No veo nada especial."
  end,

  use = function(a, b)
    return "No funciona."
  end,
}

function game.on_start()
  set_state("mummy.awake", false)
end

return game
```

`game.on_start()` runs **once, on a new game**: when the `RoomScene` is entered
without a staged restore, in the global scope, **before the start room's
`on_load`** (so a room may read the world state `on_start` seeds — e.g. each
room's initial configuration). It is **skipped on Continue/Load**, so it never
overwrites a restored save. It is a plain (non-coroutine) call; wrap any blocking
sequence in `spawn(...)`.

If a hotspot does not provide a specific handler for a valid command, the command
system may fall back to `game.lua`.

## Inventory scripts

Inventory items are declared in `inventory.yaml`; their behavior lives in
`inventory.lua`.

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

Inventory handlers follow the same operand rule as hotspot handlers: the handler
is bound to its own item, so it receives only the operands it does not already
own. A one-operand `look_at` handler receives nothing. A two-operand `use` or
`give` handler on the first item receives the second object's id.

For two-operand commands whose first operand is an inventory item, the dispatcher
checks `inventory.lua` first. If the item has a matching handler, it runs that
handler. Otherwise dispatch falls back to the second operand's hotspot handler,
then to `game.lua` fallbacks.

## Dialog scripts

A dialog script returns a node table.

```lua
return {
  start = "greet",

  greet = {
    npc = "¿Necesitás algo?",
    options = {
      { "¿Quién sos?", to = "who" },
      { "Nada.", to = END },
    },
  },

  who = {
    npc = "Soy la Dra. Schneider.",
    to = "greet",
  },
}
```

Dialog callbacks may use the same API as room scripts.

## Close-up scripts

A close-up (`CloseUp` scene, design 04 §CloseUp) becomes **scripted** when its scene
declares a `logic:` Lua sidecar (`closeups/<id>.lua`). The sidecar returns a table:

```lua
return {
  on_enter = function() ... end,   -- optional: runs when the close-up opens
  on_exit  = function() ... end,   -- optional: runs when it closes
  hotspots = {
    <hotspot_id> = function() ... end,   -- runs when that hotspot is clicked
  },
}
```

`<hotspot_id>` matches a hotspot id in the close-up's YAML (which still owns the
polygon and hover `name`). Static data in YAML, behavior in Lua — same split as a
room's `.yaml` + `.lua`.

- **Hotspot handlers** run as coroutine tasks, so `talk` blocks line-to-line (the
  player advances with a click) and `wait`/`sleep` work — no `spawn` wrapper needed.
  A click while a handler is still running is ignored (it won't interrupt the line).
- **`on_enter` / `on_exit`** are *direct* (non-blocking) calls, like a room's
  `on_load` / `on_unload`. Wrap any blocking sequence in your own `spawn(...)`.
- **`talk`** inside a close-up shows in the close-up's own bubble (centred near the
  bottom). Speech colour comes from the optional `cast:` scene param.
- **Reaching the room beneath.** The room is frozen while the close-up is open, but
  its Lua API is still live, so a close-up script may make **instant** room changes
  that appear when the player backs out — `spawn_npc`, `despawn_npc`, `set_state`,
  `show_object`/`hide_object`, etc. This is the supported animation workaround (an NPC
  enters/leaves "off-screen"). Blocking room moves (`avatar(id):move_to`) are **not**
  available here — the room is not ticking, so they would never finish.

```lua
-- closeups/lab_skull.lua
return {
  on_enter = function() spawn_npc("schneider", "at_door", "down") end, -- seen on exit
  hotspots = {
    craneo = function()
      talk("player", "Un cráneo casi intacto.")
      talk("player", "Hay una inscripción en la base.")
      set_state("lab.read_skull", true)
    end,
  },
}
```

### Close-up-local functions

While a scripted close-up is open, two extra functions are bound (and unbound on
exit), in addition to the close-up `talk` and the live room API:

| Function | Parameters | Meaning |
|----------|------------|---------|
| `set_hotspot_name(id, name)` | hotspot id, string | Override a hotspot's hover/look label at runtime (e.g. rename an "unidentified" sample to its identification). Lasts for the open view; **persistence is the script's job** — save the chosen name in state and re-apply it in `on_enter` so it survives reopening and save/load. |
| `shout(text)` | string (empty/none clears) | Set an off-screen "shout" banner — a styled line word-wrapped across the top of the close-up, **independent of the speech bubble** (an unseen character calling from the room). Typically driven from a `spawn`-ed loop that alternates lines with `wait`; the close-up scope is cancelled on exit, so the loop stops and the banner clears automatically. |

```lua
-- closeups/window_llamas.lua: identify samples; after 3, an off-screen voice nags.
return {
  on_enter = function()
    -- Re-apply identifications persisted from a previous visit.
    for i = 1, 6 do
      local n = get_state("llamas.animal" .. i .. ".name")
      if n then set_hotspot_name("animal" .. i, n) end
    end
  end,
  hotspots = {
    animal1 = function()
      talk("player", "Pelaje fino y blanco... una alpaca juvenil.")
      local name = "Muestra 1 — alpaca juvenil, hembra"
      set_hotspot_name("animal1", name)
      set_state("llamas.animal1.name", name)         -- persist
      identify_and_maybe_nag()                        -- count + start shouting at 3
    end,
    -- ...
  },
}
```

## Coroutine rules

Blocking calls must run inside a spawned task or inside an engine-invoked script
context that is already coroutine-enabled.

`talk` is the one exception that degrades gracefully: inside a coroutine task it
yields until the line is dismissed (so consecutive lines play in sequence instead
of overwriting each other), but called on the main thread — directly in a plain
hook such as `on_load`, or in a verb handler — it shows the line fire-and-forget
rather than erroring. To play several lines in order, still wrap them in `spawn`.

Valid:

```lua
spawn(function()
  talk("julia", "Voy hasta la puerta.")
  avatar("julia"):move_to("at_door")
end)
```

Invalid in plain initialization code:

```lua
-- Do not block during module loading.
talk("julia", "This should not run at load time.")
```

`spawn(fn)` inherits the current script scope. A task spawned from a room hook or
hotspot handler is cancelled when that room unloads or reloads; a task spawned
from a dialog callback is cancelled when the dialog exits; a task spawned from a
cutscene script is cancelled when that scene finishes, is skipped, or is
replaced.

Cancelled tasks are removed from the scheduler and are never resumed. Cancellation
does not run Lua cleanup code; use `on_unload` for room cleanup and dialog
`on_exit` for dialog cleanup. Running coroutines are transient and are not saved.
On load-game, the engine cancels the current script scopes, restores `GameState`,
loads the restored room, and runs the normal `on_load`.

Cross-room background tasks are design-for. If the API later adds
`spawn_global(fn)`, it must be used deliberately and those tasks still will not be
part of save/load.

## API reference

### Flow and coroutines

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `spawn(fn)` | function | task id or nil | Run function as coroutine task in the current script scope. |
| `wait(seconds)` | number | — | Yield task until time elapses. `sleep(seconds)` is an alias. |
| `emit(name, payload?)` | string, optional value | — | Emit named event within the emitter's scope. |
| `wait_event(name)` | string | payload | Yield until named event occurs in the task's scope. |

Events are scoped: `emit` delivers only to waiters in the **same** script scope as
the emitter (room, dialog, scene, or global). Events do not cross scopes, so a
later room cannot resume a task that was waiting in an earlier one.

### Speech and dialog

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `talk(speaker, text, pos?, dur?)` | character id, string, optional position, optional duration | — | Show speech near the speaker and yield until finished/skipped. |
| `show_text(text, dur?)` | string, optional duration | — | Show a speaker-less centered text page in virtual space and yield until finished/skipped. |
| `clear_text()` | — | — | Remove the current text page immediately. |
| `start_dialog(id [, speaker])` | dialog id, optional cast character id | — | Enter dialog state and run `dialogs/<id>.lua`. `speaker` (default `id`) is the cast character whose speech colour and over-head bubble render the `npc` lines, letting one NPC own several topic-named dialogs. |
| `float_text(text, where [, opts])` | string, anchor, optional table | — | Show a **non-blocking** floating label over the scenery, independent of the single `talk` line: onomatopoeia (`"¡CLICK!"`) and background NPC chatter. Several can coexist and it returns immediately (drive a recurring effect from a `spawn`-ed loop with `wait`). `where` is a **point name**, `"npc:id"`/`"object:id"` (the label follows the moving thing), or `{ x=, y= }`. `opts` = `{ duration = seconds, color = { r=, g=, b= } }`. Labels are transient (not saved) and cleared on room change. |

`text` may later be replaced or supplemented by a stable line id for localization
and voice-over. When `dur` is omitted, the engine derives the display time from
the line length (see [Speech](04-point-and-click-concepts.md)); `talk` and
`show_text` share that formula and are both skippable.

`talk` is the in-room, near-speaker primitive; `show_text` is the speaker-less
page primitive used by `StoryText` cutscenes (and any script that wants a centered
page). Each `show_text` replaces the current page — a script calls it in sequence,
each call yielding until the page finishes or is skipped.

### Rooms and camera

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `change_room(id, entry_point?)` | room id, optional point id | — | Load another room inside `RoomScene`. |
| `current_room()` | — | room id | Return current room id. |
| `open_closeup(scene_id)` | `CloseUp` scene id | — | Open an examine view as an overlay over the room; it pops back here on Esc / right-click (design 04 §CloseUp). |
| `start_cutscene(scene_id)` | manifest scene id | — | Leave the room for a manifest scene (typically a `Cutscene` / `StoryText`) — e.g. an act-closing cutscene fired from a verb handler. Unlike `open_closeup` this **replaces** the room (it does not pop back); the target scene's own `on_finish` decides where to go next. Persistent `GameState` survives; the live room is unloaded. |
| `camera_look_at(target)` | target | — | Snap camera to target and suspend follow. |
| `camera_go_to(target)` | target | — | Tween camera to target, yield until done, and suspend follow. |
| `camera_follow_player()` | — | — | Resume following the player. |

A target may be:

- a named point;
- an avatar id;
- `{ x = ..., y = ... }` coordinates.

`change_room` reseats the persistent player avatar rather than recreating it;
`entry_point` overrides the target room's default placement. See
[player vs NPC avatars](04-point-and-click-concepts.md) for the full placement
resolution order.

### Avatars

`avatar(id)` returns an avatar handle.

| Method | Parameters | Returns | Meaning |
|--------|------------|---------|---------|
| `:move_to(target)` | target | — | Move through room pathfinding and yield until done. |
| `:look_at(target)` | target | — | Face target. |
| `:face(direction)` | `"up"`, `"right"`, `"down"`, `"left"` | — | Set orientation. |
| `:position()` | — | point (`{x, y}`) | Return current position. |
| `:play(sequence)` | string | — | Play an animation sequence, overriding stand/walk until it finishes (one-shot) or movement interrupts it. No-op if the sequence is absent. |
| `:play_until_end(sequence)` | string | — | Play a **non-looping** sequence and yield until it finishes. (A looping sequence would never end — use `:play`.) |
| `:anchor(name)` | string | point (`{x, y}`) or `nil` | World position of a named sprite anchor on the current frame, or `nil` if absent. |

`:look_at(target)` is a convenience that resolves a world target to the nearest
of the four orientations and faces it — equivalent to computing a direction and
calling `:face`. There is no `:talk()` in the script API: the talking animation
is started automatically by the global `talk(speaker, ...)`. Other C++ avatar
methods (`set_position`, `stand`, `walk`, `set_scale`) are engine-internal and
intentionally not script-exposed.

#### NPC presence

NPCs listed in a room's `avatars:` are created on room load. To make an NPC appear
**conditionally** — e.g. only after a global flag is set — spawn it from script
instead (typically in the room's `on_load`):

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `spawn_npc(id, start [, orientation])` | cast character id, point name or `{x, y}`, optional `"up"`/`"right"`/`"down"`/`"left"` | — | Create a room NPC from a cast character and seat it at `start`. If that NPC already exists it is repositioned (so calling each `on_load` is idempotent). The player character is not spawnable. |
| `despawn_npc(id)` | cast character id | — | Remove a room NPC. No-op if absent. |

NPC presence is **not** persisted (unlike region/object/layer/hotspot state) — it is
re-derived each load. Drive it from `on_load` against `get_state`, for example:

```lua
function room.on_load()
  if get_state("intro.delivery_arrived") then
    spawn_npc("delivery_guy", "delivery_guy_start", "left")
  end
end
```

Spawned NPCs are room-scoped like declared ones: they render and are destroyed on
room unload. Interaction still needs a hotspot (a fixed `area`, or a `bind` to the
NPC once that lands).

### State

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `get_state(key)` | string | value | Read global saved state. |
| `set_state(key, value)` | string, value | — | Write global saved state. |
| `get_room_state(key)` | string | value | Read current-room saved state. |
| `set_room_state(key, value)` | string, value | — | Write current-room saved state. |

State keys use dotted names by convention:

```lua
set_state("mummy.awake", true)
set_room_state("drawer.open", true)
```

`get_room_state` / `set_room_state` operate only on the **current** room's store;
they cannot read or write another room. Facts that must be visible across rooms
belong in global state under a namespaced key (for example
`set_state("hall.lever_pulled", true)`).

State values are restricted to **scalars — boolean, number, or string** — for the
MVP. Tables and nested values are out of scope so the save file stays simple and
serialization is total; storing an unsupported type fails loudly in development
builds. Region states are not stored here — they are managed by `set_region_state`
/ `get_region_state` (see Scenery below).

### Scenery

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `set_region_state(id, state)` | region id, state id | — | Swap a background region to a named state. |
| `get_region_state(id)` | region id | state id | Read a region's current state. |
| `show_object(id)` | object id | — | Make room object visible/enabled. |
| `hide_object(id)` | object id | — | Hide room object and disable interaction if bound. |
| `set_layer_visible(id, visible)` | layer id, bool | — | Show/hide a background layer (the layer must have an `id`). Persisted per room. |
| `enable_hotspot(id)` | hotspot id | — | Allow hotspot interaction. |
| `disable_hotspot(id)` | hotspot id | — | Prevent hotspot interaction. |
| `enable_obstacle(id)` | obstacle id | — | Re-enable a named obstacle (it blocks the walkable area again). Persisted per room. |
| `disable_obstacle(id)` | obstacle id | — | Disable a named obstacle so the player/NPCs can path through where it was (e.g. once a blocking crate is removed). Persisted per room. |

#### Object handle

`object(id)` returns a handle to a room object (the id is its `objects:` key).
Beyond `show_object`/`hide_object`, an object can be moved and resized from
script. This runtime pose is **transient** — re-derived from the object's def on
room load, like an avatar's pose (only object *visibility* persists).

| Method | Parameters | Returns | Meaning |
|--------|------------|---------|---------|
| `:move_to(target [, speed])` | point name or `{x, y}`, optional px/s | — | Move in a straight line to `target` (free — not walkable-gated), yielding until it arrives. |
| `:set_position(x, y)` | numbers | — | Place the object immediately (cancels a move). |
| `:position()` | — | point (`{x, y}`) | Current world position. |
| `:set_scale(s)` | number > 0 | — | Uniform render scale (resize); aspect preserved. |
| `:play(sequence)` | string | — | Play an animation sequence (looping per its def). **Animated objects only** (sprite is an `*.anim.yml`); no-op + warn otherwise. |
| `:play_until_end(sequence)` | string | — | Play a non-looping sequence and yield until it finishes. **Animated objects only.** |

An object whose `sprite` is an animation (`*.anim.yml`) is an **animated object** —
an `AnimatedSprite` like an avatar. Its `position` is the sprite **pivot** (not the
top-left used for a static texture), and it plays its `sequence:` on load. `play`
and `play_until_end` are no-ops on a static-texture object.

### Inventory

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `has_item(id)` | item id | bool | Whether player has item. |
| `add_item(id)` | item id | — | Add item to inventory. |
| `remove_item(id)` | item id | — | Remove item from inventory. |
| `list_items()` | — | list | Return current inventory ids. |

### Audio

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `play_music(path, loop?)` | logical path, optional bool | — | Start streamed music. |
| `stop_music()` | — | — | Stop current music. |
| `play_sound(path, volume?, pan?)` | logical path, optional number (0..1), optional number (-1..1) | — | Play a short sound effect. `volume` scales the global SFX volume; `pan` places it L/R (-1 left, 0 center, +1 right). Panning affects mono clips only; a position-aware/spatial path is design-for. |
| `stop_sounds()` | — | — | Stop all active sound effects. |

### Input and room-view control

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `block_input()` | — | — | Switch room view to the `blocked` state: input disabled and the SCUMM panel hidden (a black bar under the scenery). Use for cutscene-like moments. |
| `unblock_input()` | — | — | Restore the `command` state: input enabled and the panel shown. |
| `set_room_view_state(state)` | `"command"` \| `"blocked"` | — | Explicitly set the room-view state — the same two script-settable states as `unblock_input` / `block_input`. The `dialog` and `menu` states are engine-managed (entered via `start_dialog` / the pause menu); passing them, or any other string, logs a warning and is ignored. |

Hiding the SCUMM panel is not a separate operation: the panel is shown in the
`command` state and hidden in `blocked`, so `block_input()` / `set_room_view_state`
are how a script hides or shows it. Scripts should prefer high-level operations such
as `start_dialog` and command execution, which manage room-view state automatically.

### Resources

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `resource_path(rel)` | logical path | logical resource id | Normalize or validate a logical resource path. |
| `include(logical)` | logical path | the script's return value (or nil) | Load + run a Lua resource in the shared state and return its value. |

`resource_path` does not return a platform filesystem path. It returns a logical
resource id suitable for the engine resource layer.

`include` lets a script be split across files — the sandbox has no `require`, and
`dofile` would bypass the resource layer (and the release `.pak`). It runs the
file every call (no caching) and returns whatever the file `return`s, so a module
can expose a table:

```lua
-- rooms/lab.lua
local flow = include("rooms/_act_flow.lua")
local acts = { include("rooms/lab/_act1.lua"), include("rooms/lab/_act2.lua") }
function room.on_load() flow.enter("lab", acts) end
```

This is the basis of the **per-act room convention**: split a room whose behaviour
changes per act into one module per act (`configure` for instant presence,
`on_first_enter` / `on_reenter` for one-time vs returning beats), dispatched by the
shared `rooms/_act_flow.lua` helper. A copy-paste template lives under
`games/<game>/design_files/templates/room_acts/`, with `rooms/exterior.lua` as the
worked example.

## Error handling

Script API calls shall fail loudly during development when called with invalid
ids or malformed parameters. In release builds, failures should produce safe
fallback behavior when possible and log diagnostics.

Examples:

| Error | Development behavior | Release behavior |
|-------|----------------------|------------------|
| Missing hotspot handler | Log clear error and use fallback if available. | Use fallback response. |
| Unknown room id | Error and refuse transition. | Error and remain in current room. |
| Unknown animation sequence | Log error, fallback to `stand`. | Fallback to `stand`. |
| Invalid resource path | Error with logical path. | Log and skip asset/action. |

## Template scripts

The engine shall provide templates for:

- `game.yaml`;
- `cast.yaml`;
- `game.lua`;
- `rooms/<id>.yaml`;
- `rooms/<id>.lua`;
- `dialogs/<id>.lua`.

Templates encode the expected behavior and reduce the amount of boilerplate
required to start a new game.
