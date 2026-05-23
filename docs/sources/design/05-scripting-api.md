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

Dialog ids resolve to files, and the same id names the speaking NPC character:

```text
start_dialog("stan") -> dialogs/stan.lua, spoken by cast character "stan"
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
for command feedback.

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

## Coroutine rules

Blocking calls must run inside a spawned task or inside an engine-invoked script
context that is already coroutine-enabled.

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
| `wait(seconds)` | number | — | Yield task until time elapses. |
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
| `start_dialog(id)` | dialog id | — | Enter dialog state and run dialog. |

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
| `:play(sequence)` | string | — | Play animation sequence. |
| `:play_until_end(sequence)` | string | — | Play non-looping animation and yield until done. |
| `:position()` | — | point | Return current position. |
| `:anchor(name)` | string | point | Return absolute anchor position. |

`:look_at(target)` is a convenience that resolves a world target to the nearest
of the four orientations and faces it — equivalent to computing a direction and
calling `:face`. There is no `:talk()` in the script API: the talking animation
is started automatically by the global `talk(speaker, ...)`. Other C++ avatar
methods (`set_position`, `stand`, `walk`, `set_scale`) are engine-internal and
intentionally not script-exposed.

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
| `enable_hotspot(id)` | hotspot id | — | Allow hotspot interaction. |
| `disable_hotspot(id)` | hotspot id | — | Prevent hotspot interaction. |

Driving an object's own animation from a script (`object(id):play(...)`) is a
design-for addition; the MVP only shows or hides objects.

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
| `play_sound(path)` | logical path | — | Play short sound effect. |
| `stop_sounds()` | — | — | Stop all active sound effects. |

### Input and room-view control

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `block_input()` | — | — | Switch room view to blocked input mode. |
| `unblock_input()` | — | — | Restore command input mode. |
| `set_room_view_state(state)` | string | — | Explicitly set room-view state when needed. |

Scripts should prefer high-level operations such as `start_dialog` and command
execution, which manage room-view state automatically.

### Resources

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `resource_path(rel)` | logical path | logical resource id | Normalize or validate a logical resource path. |

`resource_path` does not return a platform filesystem path. It returns a logical
resource id suitable for the engine resource layer.

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
