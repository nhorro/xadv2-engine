# Lua and Game Content Authoring Guide

This guide is for game authors writing the Lua scripts and YAML data that make
up a game. The [design documents](../design/00-index.md) are the source of truth
for schemas and runtime semantics; this guide focuses on the rules and patterns
authors should follow when writing content.

The C++ engine never enters this guide. If you need to change C++, see the
[C++ engine coding guide](cpp-engine.md).

## 1. The YAML / Lua split

| Data kind | File |
|-----------|------|
| Static structure: positions, polygons, asset paths, items, characters, appearances | YAML |
| Behavior: what happens on click, dialog flow, scripted actions, fallbacks | Lua |

The engine refuses to load a room without both files. If you find yourself
about to write a number in Lua that represents a position, polygon, or asset
path, stop — that belongs in YAML.

## 2. File layout

A standard game lives under `games/<game>/data/`:

```text
data/
├── game.yaml              Manifest
├── cast.yaml              Appearances + characters + inventory items
├── game.lua               Global logic, fallbacks, on_start
├── inventory.lua          Inventory-side verb handlers
├── rooms/
│   ├── <id>.yaml          Room static data
│   └── <id>.lua           Room behavior
├── dialogs/
│   └── <id>.lua           Dialog tree
├── scripts/
│   └── <name>.lua         Scene-bound scripts (StoryText, etc.)
├── strings/
│   └── <lang>.yaml        Engine-emitted UI text and default fallbacks
├── backgrounds/           Layered art
├── anims/                 Animation YAML + spritesheets
├── fonts/                 TTF files
├── music/                 OGG tracks
├── sfx/                   WAV samples
└── ui/                    Title screens, panels
```

Room id rules:

- ASCII, lowercase, underscores;
- max 64 characters;
- `rooms/<id>.yaml` and `rooms/<id>.lua` must both exist.

Asset paths use forward slashes on every platform, even Windows.

## 3. Lua style

| Rule | Example |
|------|---------|
| `snake_case` identifiers | `set_state("drawer.open", true)` |
| One module per file; file returns a table | `local room = {} ... return room` |
| `local` declarations only — never assign to a global | `local foo = ...` |
| 2-space indent | |
| 80-column soft limit | |
| Strings: single quotes by default, double when the string contains apostrophes | `'No funciona.'` vs `"No, gracias."` |

Never use globals. Persistent values go through `set_state` / `set_room_state`.

## 4. Module skeletons

### Room script (`rooms/<id>.lua`)

```lua
local room = {}

function room.on_load()
  crossfade_music("music/study.ogg", 2.5)
end

function room.on_unload()
  stop_music(2.5)
end

room.on_zone_enter = function(zone)
  if zone == "to_hall" then
    change_room("hall")
  end
end

room.hotspots = {
  drawer = {
    look_at = function() return "Es un cajón viejo." end,
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
}

room.characters = {
  -- handlers for character hotspots that bind: avatar:<id>
  schneider = {
    talk_to = function() start_dialog("schneider") end,
  },
}

return room
```

Available fields on the returned table:

| Field | Required | Meaning |
|-------|----------|---------|
| `on_load` | optional | Called in a coroutine after room load. |
| `on_unload` | optional | Called synchronously before unload. **Non-blocking only.** |
| `on_zone_enter(zone_id)` | optional | Called when the player enters a zone. |
| `on_zone_exit(zone_id)` | optional | Called when the player exits a zone. |
| `hotspots[id][verb]` | optional | Per-hotspot verb handler. |
| `characters[id][verb]` | optional | Per-character handler (for character hotspots in the room). |
| `[verb]` (room-wide) | optional | Room-wide fallback for the verb; receives `(target_id)` for single-operand or `(p1_id, p2_id)` for two-operand. |

### `game.lua`

`game.lua` may define `on_start`, `on_room_change`, and `fallbacks`.

```lua
local game = {}

function game.on_start(is_new_game)
  -- is_new_game is true on a fresh game, false after loading a save.
  if is_new_game then
    set_state("mummy.awake", false)
  end
end

function game.on_room_change(prev_room_id, next_room_id)
  -- Called between the outgoing on_unload and the incoming on_load.
end

game.fallbacks = {
  look_at = function(target_id) return "No veo nada." end,
  use     = function(a, b)      return "No funciona." end,
  give    = function(a, b)      return "No, gracias." end,
}

return game
```

The hooks fire as follows:

| Hook | Fires |
|------|-------|
| `on_start(is_new_game)` | Once when `RoomScene` enters its first room of the session. `is_new_game` is `true` for a fresh game, `false` after a load. |
| `on_room_change(prev, next)` | Between `prev`'s `on_unload` and `next`'s `on_load`. Use it for global bookkeeping that doesn't belong to either room. |
| `fallbacks[verb]` | Last in the handler chain (see §5). Receives full operand list. |

### `inventory.lua`

```lua
local inventory = {}

inventory.key = {
  look_at = function() return "Una llave de bronce." end,
  use = function(other)
    -- Only reachable when key is param2 of "Use X with key".
    -- key is combinable: true in cast.yaml, so one-operand use isn't called.
    return nil
  end,
}

inventory.map = {
  look_at = function() return "Un mapa de la región." end,
  use = function() return "Me ubico mejor con el mapa." end,
}

return inventory
```

### Dialog script (`dialogs/<id>.lua`)

```lua
return {
  speaker = "schneider",   -- optional; defaults to the dialog id

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

`END` is a constant injected into the dialog scope by the engine; it
terminates the dialog. A node has *either* `options` *or* `to`; nodes with both
are rejected at load time.

## 5. Handler resolution

When the player executes a command, the engine walks a chain to find a handler.
Returning `nil` from any link falls through to the next. Returning a string
speaks it and stops the chain. Returning any other type is an authoring error
(development build: traceback; release: logged and treated as `nil`).

**Single-operand** (e.g., `Look at drawer`):

1. `room.hotspots.drawer.look_at` (or `inventory.drawer.look_at` if drawer were
   an inventory item)
2. `room.look_at(drawer_id)` (room-wide override)
3. `game.fallbacks.look_at(drawer_id)`
4. Engine default from `strings/<lang>.yaml` → `defaults.cant_look_at`
   (e.g., "No veo nada especial.")

**Two-operand `Use X with Y`** — handler runs on **X first** (inventory wins):

1. `inventory.X.use(Y_id)` — if X is an inventory item with a `use` handler.
2. `room.hotspots.Y.use(X_id)` — if Y is a room hotspot.
3. `game.fallbacks.use(X_id, Y_id)`
4. Engine default `defaults.cant_use_that_way` (e.g., "No funciona.")

**Two-operand `Give X to Y`** — handler runs on **X first** (inventory wins),
then on Y:

1. `inventory.X.give(Y_id)` — X is always an inventory item; if it has a
   `give` handler, that runs.
2. `room.hotspots.Y.give(X_id)` or `room.characters.Y.give(X_id)`
3. `game.fallbacks.give(X_id, Y_id)`
4. Engine default `defaults.no_one_to_give_to` (e.g., "No, gracias.")

Handler arguments are always **bare id strings**.

### NPC default verb

A character hotspot (one that binds to an NPC avatar) should declare
`default_verb: talk_to` in YAML so a plain click starts a conversation. Without
this, a click defaults to `look_at` — correct but rarely what the player wants
for an NPC. The room template includes `default_verb: talk_to` for every NPC
hotspot.

## 6. Blocking calls

These yield the current coroutine; the engine resumes you later:

- `wait(seconds)`
- `wait_event(name)`
- `talk(speaker, text [, opts])`
- `remark(speaker, text)`
- `start_dialog(id)`
- `avatar(id):move_to(target)`
- `avatar(id):play_until_end(sequence)`

Rules:

- Only call these from a coroutine context. Lifecycle hooks (`on_load`,
  `on_zone_enter`, `on_zone_exit`), verb handlers, dialog `run` callbacks, and
  `spawn`ed functions are coroutine contexts.
- **`on_unload` is not a coroutine context.** Blocking calls in `on_unload`
  fail.
- Module top-level (the body executed when the file is required) is not a
  coroutine context. Don't call `talk(...)` at the top of a file.

If you need to schedule background work, use `spawn`:

```lua
spawn(function()
  wait(2)
  talk("ghost", "Buuuh.")
end)
```

`talk` normally stops the speaker and plays its directional talking loop. For
speech that should not break a walk or gesture, pass
`{ continue_action = true }`. A scripted conversation can also pass
`{ face = "other_character" }`; `start_dialog` handles stopping and mutual facing
for player/NPC dialogs automatically. For the common incidental case, use
`remark("player", text)` instead of spelling out the options table. The semantic
name is intentional: unlike `talk`, a remark does not take over the character's
current action.

### `change_room` is deferred

`change_room` is safe to call from inside any handler, hook, or dialog
callback. The engine queues the room change and applies it at the end of the
current update step — the handler keeps running until it returns. This means:

```lua
room.on_zone_enter = function(zone)
  if zone == "to_hall" then
    change_room("hall")
    -- It is fine to keep executing code here; the room change has not happened
    -- yet. The current room is unloaded after this function returns.
  end
end
```

Calling `change_room` twice in the same step is an authoring error; only the
first call is honored and a warning is logged.

## 7. Coroutine lifetime

- A coroutine spawned from a room (room hooks, room hotspot handlers, room
  character handlers) is tagged with the room. When the room unloads (via
  `change_room`), the coroutine is cancelled. Any blocking call it was waiting
  on is silently dropped.
- A coroutine spawned in `game.lua` is tagged `global` and lives across rooms.
- `inventory.lua` handlers are tagged with the current room when invoked.
- Dialog `run` callbacks are tagged with the dialog; the coroutine is cancelled
  when the dialog ends.
- `emit` / `wait_event` are fire-and-forget. Only listeners registered at the
  exact moment of `emit` are resumed. Events do not cross room boundaries.

Practical consequence: never assume a blocking call returns. If a `move_to`
fires and the player triggers a `change_room` before it finishes, your handler
simply never resumes. Don't put save-critical logic *after* a blocking call
without first writing the state.

## 8. State storage rules

`set_state` and `set_room_state` accept these value types only:

- boolean
- number
- string
- flat tables of the above (no nested tables, no functions, no userdata)

Anything else is rejected (development build: error; release: logged and
ignored).

Reading rules:

- `get_state(key)` returns `nil` for a key that was never set.
- `set_state(key, nil)` deletes the key.
- Numbers round-trip as IEEE-754 doubles; an integer value (`42`) survives a
  save/load as the same Lua number.
- `get_room_state` reads only the current room. To check a fact from another
  room, use global state with a namespaced key
  (`set_state("hall.lever_pulled", true)`).

Key conventions:

- Use dotted names: `set_state("mummy.awake", true)`.
- Reserved prefix `__` — do not use. Keys starting with `__` are engine-managed
  (e.g., `__dialog.stan.once.greet.1`).
- Per-room state lives in `set_room_state` for the current room; global state
  lives in `set_state`.

## 9. Save / load implications

The player can save when the command builder is idle in `Command` state and
the active scene is the `RoomScene`. Saving from a cutscene, settings overlay,
or dialog is not supported in the MVP. On load:

- All `set_state` / `set_room_state` / `set_region_state` values are restored.
- Inventory is restored.
- Region states are restored.
- Hotspot enabled state (`enable_hotspot` / `disable_hotspot`) is restored
  per-room.
- Object visibility (`show_object` / `hide_object`) is restored per-room.
- Consumed `once` dialog options are restored.
- Avatar positions and orientations are restored.

**Not restored:**

- Scheduled coroutines.
- Pending `wait_event` listeners.
- Music position; active speech.
- Command currently being built (you can't save mid-command anyway).

Design implication: don't keep critical logic only in a long-running coroutine.
Mirror the important parts in `set_state` so the room's `on_load` can pick up
where things left off.

## 10. YAML schemas reference

The YAML schemas are the most likely place to get details wrong. The full set
lives in the design docs; quick links:

| File | Schema location |
|------|-----------------|
| `game.yaml` | [Architecture overview](../design/02-architecture-overview.md) §Game manifest |
| `cast.yaml` | [Data formats](../design/06-data-formats.md) §Cast |
| `rooms/<id>.yaml` | [Data formats](../design/06-data-formats.md) §Room |
| `*.anim.yaml` | [Data formats](../design/06-data-formats.md) §Animation |
| Spritesheet YAML | [Data formats](../design/06-data-formats.md) §Spritesheet |
| `inventory.yaml` | [Data formats](../design/06-data-formats.md) §Inventory |
| `dialogs/<id>.lua` | [Data formats](../design/06-data-formats.md) §Dialog |
| `strings/<lang>.yaml` | [Data formats](../design/06-data-formats.md) §UI strings |
| Save file | [Architecture overview](../design/02-architecture-overview.md) §Make persistent state explicit |

Common YAML mistakes:

| Mistake | Symptom | Fix |
|---------|---------|-----|
| Backslash in a path | Loader error | Use `/` only, even on Windows. |
| Uppercase or spaces in an id | Loader error | ASCII lowercase + underscores. |
| Hotspot with no `area` and no `bind` | Loader error | Every hotspot needs at least one hit source. |
| `bind: region:<id>` with a changing visual | Hit area "doesn't follow" the open/shut state | The hit polygon is the region's `area` field — it is **constant** regardless of state. Use a per-state `area` only by splitting into multiple hotspots, or add an explicit `area:` to the hotspot. |
| `start_room` not declared in `rooms/` | Load failure | Ensure the YAML + Lua pair exists. |
| Region `initial` not in `states` | Loader error | The initial state id must be a key under `states:`. |
| Affordance verb not understood | Loader error | Use the verbs from §Affordances in the design doc. |
| NPC hotspot without `default_verb: talk_to` | Plain click examines instead of talking | Declare `default_verb: talk_to` for every NPC hotspot. |
| `default_verb` not in `affordances` and not `look_at` | Loader error | The default verb must be `look_at` or appear in `affordances`. |

## 11. Error handling at author time

In development builds:

- Lua syntax errors at load → room fails to load; error overlay shows the file
  and line.
- Lua runtime errors in a handler → traceback in console; default verb response
  speaks; builder resets.
- Blocking call outside a coroutine → error logged, call skipped.
- Disallowed state value → error logged, call skipped.

In release builds, the same effects apply but errors do not interrupt gameplay
— the default response speaks and the world keeps running.

The `allow_room_reload: true` development flag enables F5 to reload the
current room's YAML and Lua, preserving inventory, global state, and avatar
position. Use this for fast iteration.

## 12. Common pitfalls

| Pitfall | Avoidance |
|---------|-----------|
| Using globals | Don't. Always `local`. Persist via `set_state` / `set_room_state`. |
| Module top-level blocking calls | Don't `talk(...)` at the top of a room script. Put it in `on_load`. |
| Mutating `room.hotspots` at runtime | Don't. Use `enable_hotspot` / `disable_hotspot` and `set_region_state` instead. |
| Calling `change_room` from `on_unload` | Forbidden. The room is unloading already. |
| Forgetting `return nil` to fall through | A handler that falls off the end of its body returns `nil` implicitly — that's fine. Be deliberate about explicit returns. |
| Reaching into another room's script | You can't. Use `set_state` for cross-room signals. |
| Long single `npc` lines | Break into a list: `npc = { "...", "..." }`. |
| Hard-coding asset paths in Lua | Asset paths belong in YAML. Lua refers to entities by id. |
| Writing physics-style code | This is an adventure game. The engine handles movement; you describe behavior. |
| Stashing a `talk` result | `talk` returns nothing meaningful. Don't try to chain on its return. |
| Assuming `set_region_state` also moves the hit area | It changes the *visual* state only. Hotspot hit area is its `bind`'s `area` polygon, which is constant. |

## 13. Useful patterns

### Cross-room flag

```lua
-- room study: when player picks up the key
room.hotspots.key = {
  pick_up = function()
    add_item("key")
    set_state("found_key", true)
    return "Una llave."
  end,
}

-- room hall: door reacts
room.hotspots.door = {
  look_at = function()
    if get_state("found_key") then
      return "La puerta cerrada con llave."
    end
    return "Una puerta."
  end,
  use = function(item)
    if item == "key" then
      remove_item("key")
      change_room("crypt")
      return nil
    end
    return nil  -- fall through to game.fallbacks.use
  end,
}
```

### Dialog with reactive option

```lua
options = {
  { "¿Me das eso?",
    when = function() return get_state("guard.likes_player") end,
    run  = function() add_item("gem") end,
    to   = END },
  { "Adiós.", to = END },
}
```

### Cutscene-like sequence in a room

```lua
function room.on_load()
  block_input()
  avatar("hero"):move_to("at_door")
  talk("hero", "Por fin.")
  unblock_input()
end
```

### Multi-line NPC speech

```lua
greet = {
  npc = {
    "Buenas tardes.",
    "¿En qué puedo ayudarte?",
  },
  options = { ... },
}
```

Each entry of the list is spoken in order; the speech auto-duration applies to
each line individually and the dialog yields between them.

### Once-only dialog branch

```lua
options = {
  { "¿Por qué estás acá?",
    once = true,
    to = "backstory" },
  { "Hasta luego.", to = END },
}
```

After the player picks the once-only option, it disappears for the rest of the
game (persisted across saves).
