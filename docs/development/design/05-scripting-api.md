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
| `characters` | Character ids, display names, speech colour/placement, and appearance binding. Shared speech font and size live in `game.yaml`. |

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
| `composite` | `composite`: a `*.composite.yaml` | `pac::gfx::CompositeSprite` | Room objects implemented; avatar strategy pending |
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
  crossfade_music("music/study.ogg", 2.5)
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
| `on_load()` | The room has been (re)loaded and initialized — runs on **every entry** (see below). |
| `on_player_entered()` | An opt-in player `avatars[].enter_from` walk has reached its normal `start` point. It does not fire on save restore or when `change_room` supplies an explicit entry point. |
| `on_unload()` | The room is about to be unloaded (on leaving it, and on hot-reload). |
| `on_screen_edge(edge)` | Player reaches a room screen edge (`left`/`right`/`top`/`bottom`); all four fire. |
| `on_zone_enter(zone)` | Player enters a zone polygon. |
| `on_zone_exit(zone)` | Player exits a zone polygon. |

**`on_load` is the per-entry hook (there is no separate `on_enter`).** Every
`change_room` into a room — and a save restore — *re-runs the room's `.lua` from
scratch* (rebuilding its `room` table) and then calls `on_load()`. So:

- it fires **each time the player enters**, not once per game;
- it takes **no arguments** — the `change_room(id, entry_point)` entry point is
  used by the engine to seat the player, but is not passed to `on_load`;
- a room's Lua **locals and closures are transient** — they are rebuilt on every
  entry and never saved. Persist facts only through `set_state` / `set_room_state`
  / inventory / region state (see *State and persistence* below). To distinguish a
  first visit from a return, gate on a saved flag — or use a **room configuration**
  (below), whose `on_first_enter` / `on_reenter` beats the engine gates for you.

#### `game.on_start()` — one-time world-state init

`game.lua`'s optional `on_start()` runs **once, only when a new game begins**
(driven by `RoomScene`, before the start room's `on_load`). It is **skipped on
Continue / Load** so it never clobbers a restored save. Seed initial `set_state`
values here; see *Global game logic*. It may return a room id to override
`start_room` for that new game.

`RoomScene.parameters.development_logic` may point at a second script returning
the same `{ on_start = function() ... end }` shape. Its hook runs after
`game.on_start()` and can return the final room override. This is intended for
removable scenario catalogs and debug harnesses; omit the parameter in production.

#### State and persistence

Persistent state is the engine-owned **`GameState`** — a single struct that is the
only thing a save serializes: the current scene/room, player pose, inventory, the
flat `set_state`/`get_state` map (`global_state`), per-room state, and per-room
region / hotspot / object / layer / obstacle flags. There is **no per-scene
`save()`/`restore()`**: `RoomScene` materializes the whole `GameState` snapshot and
applies a restore (the MVP saves only while in `RoomScene`). Authors never touch
`GameState` directly — they write through `set_state` / `set_room_state` /
inventory / region APIs, and the engine folds those into the snapshot.

The live **room configuration** + its per-(room, config) first-enter flags are
engine-owned too (stored under reserved `__config.*` keys, like dialog `once`
flags), so they persist with no save-format change — authors use
`set_room_config` / `current_room_config`, never those keys.

#### Declared facts — `facts.<ns>.<name>` (typo guard)

`set_state("finding.radial_fractures", true)` works, but so does `set_state("finding.radial_fractres", true)` (a dropped letter) — it just sets a *different* key, and every condition that reads the correct one stays silently false forever. A game with dozens of flag keys has no central list and no spell-check.

Declare the flags in an optional **`facts.yaml`** (see [06 § Facts](06-data-formats.md)), grouped into namespaces, and read/write them through the `facts` global:

```lua
if facts.act1.bones_glanced then ... end      -- read  (false if unset)
facts.act1.context_glanced = true             -- write (persists like set_state)
```

`facts.<ns>.<name>` is sugar over `get_state` / `set_state` on the dotted key `"<ns>.<name>"` — a fact persists, saves, and interoperates with `get_state` exactly as before (so a topic's `requires = "finding.radial_fractures"` still names the same fact). The win is the **typo guard**: in a development build, reading or writing a key *not* declared in `facts.yaml` logs a warning naming the offending key. In a clean release build the access still works — a missing declaration never blocks shipping. The guard is loud in a Debug build and can be opted into in a Release build with `development.show_state: true`.

`facts.yaml` is optional and additive: without it, `facts.<ns>.<name>` behaves as plain state sugar with no guard, and `get_state` / `set_state` keep working untouched. Only **boolean flags** belong in `facts.yaml`; numeric counters (e.g. an attempts tally) and dynamic keys (`"llamas." .. id .. ".done"`) stay on `get_state` / `set_state`. Engine-owned reserved keys (`__config.*`, `__dialog.*`, `__uttered.*`) are never facts.

### Room configurations

A room is usually shown in one of a few discrete **configurations** — which actors
and objects are present (Julia alone vs. Julia + Dr. Schneider; an empty exterior
vs. one with a delivery truck). **Presence is declared in the room's YAML**
`configs:` block (see [06 § Room YAML](06-data-formats.md)); the engine tracks the
live config in `GameState` and *reconciles presence on every entry* — spawning /
despawning NPCs, showing / hiding objects, enabling / disabling obstacles to match.
There is no `"<room>.cfg"` integer to manage and no flow helper to call.

The room script supplies only the **behavior** half: optional first-enter /
re-enter *beats*, under `room.configs[<id>]`, keyed by the same ids as the YAML.

```lua
local room = {}

-- Presence (who/what is in each config) lives in rooms/lab.yaml. Here: only the
-- configs that have a beat. The engine runs on_first_enter the first time the room
-- is entered in that config, on_reenter on later entries (it gates the "seen" flag
-- for you), each auto-spawned so it can block (talk/move/wait) like a cutscene.
room.configs = {
  intro = {
    on_first_enter = cutscene(function()
      say("player", "Ah. Hola. No esperaba que ya hubiera alguien mirando.")
      -- ... the opening monologue ...
    end),
  },
}

-- A transition beat switches config. set_room_config reconciles the live room's
-- presence at once; for another room it records the value, applied when that room
-- next loads (so a beat in the lab can stage the hall's next config).
schneider_leaves = cutscene(function()
  say("schneider", "Eso espero.")
  set_room_config("lab", "alone")   -- the reconcile despawns Schneider
end)

return room
```

| Call | Meaning |
|------|---------|
| `set_room_config(room_id, config_id)` | Switch a room's configuration. The live room reconciles presence immediately (and counts this as the config's reveal, so a later re-entry runs `on_reenter`, not `on_first_enter`); another room just records the value, reconciled when it next loads. |
| `current_room_config(room_id?)` | The room's live config id (defaults to the current room). Use it instead of `get_state("<room>.cfg")` — e.g. `if current_room_config("lab") == "puzzle" then …`. |

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

#### Handlers are auto-spawned

Hotspot handlers, inventory item handlers, `game.fallbacks` entries, dialog
option `run` callbacks, and close-up hotspot handlers are run as coroutine
tasks in the relevant scope (room / dialog / close-up), so they can use
blocking primitives — `talk`, `wait`, `avatar(id):move_to`,
`avatar(id):play_until_end`, `show_text` — **without an explicit `spawn(...)`
wrapper**. Past code that wrapped itself in `spawn` for that reason
(`use = function() spawn(use_thermo) end`) reduces to `use = use_thermo`.

- If the handler returns before yielding, its return value is the caption,
  matching the synchronous semantics above.
- If the handler yields, it takes responsibility for the command — the
  fallback caption is suppressed, the SCUMM panel stays disabled, and input
  is blocked until the task drains. A second click during that window is
  silently ignored.
- If the handler calls `change_room` (or anything else that ends the room
  scope), the in-flight task is cancelled along with the rest of the scope;
  any code after the `change_room` call will not run. This matches the
  behaviour of an explicit `spawn(function() ... change_room(); after() end)`
  today — the `after()` likewise never fires.

The `spawn(...)` global stays available for **explicit** background tasks —
the off-screen nag loop a close-up runs while the player is identifying
something, a watcher in `on_load` — but writing a verb handler no longer
needs it. See *Coroutine rules* below for which hooks remain direct (non-
coroutine) calls.

#### Cutscene blocks

A scripted beat — an NPC walking in and saying a few lines, the opening
monologue of a room, a transition between configurations — used to mean a
ritual: `block_input()` at the top, `unblock_input()` at the bottom, every
line as `talk("speaker", "...")`, every gesture as `avatar("id"):method(...)`.
The `cutscene { ... }` wrapper (M9 #184) collapses that into something that
reads like a screenplay:

```lua
schneider_arrives = cutscene(function()
    set_state("lab.cfg", CFG_PUZZLE)
    spawn_npc("schneider", "at_door", "down")
    move("schneider", "schneider_start")
    face("schneider", "left")
    say("schneider", "Serrategui.")
    say("player",    "Dra. Schneider.")
    move("schneider", "at_desk")
    face("schneider", "up")
    say("schneider", "Interesante. Había asumido que era una falla eléctrica.")
    stop_music()
    -- ...
end)
```

`cutscene(body)` returns a wrapper function. Calling the wrapper:

1. Spawns `body` as a coroutine task in the current scope (the room scope
   when called from a verb handler / `on_first_enter` / a free function
   inside a room script).
2. Marks the room view as `blocked` for the duration of the body — input is
   suppressed, the SCUMM panel fades out, and the cursor is hidden until the
   beat finishes.
3. Returns immediately. The wrapper itself never blocks; the body runs
   asynchronously in the spawned task.

When the body finishes — or is cancelled by `change_room` / room unload —
the engine restores the `command` view state from the C++ side, so even an
interrupted cutscene leaves the room responsive. (Lua cleanup doesn't run
on scope cancellation, so a `finally` in Lua wouldn't be enough.)

`skippable_cutscene(body, on_skip)` is the opt-in form for long, non-interactive
room sequences. While it is active, ESC cancels only its body task, clears active
speech, calls the synchronous `on_skip` finalizer, and restores command mode.
The finalizer must not yield; it should place every actor/object/camera and story
flag in the same canonical end state as normal completion. Ordinary `cutscene`
blocks continue to ignore ESC.

Inside a cutscene body (and elsewhere — they're plain Lua globals), three
flat verbs mirror the most common stage directions:

| Verb | Equivalent to | Use for |
|------|---------------|---------|
| `say(speaker, text)` | `talk(speaker, text)` | A line of dialogue. |
| `move(id, target)` | `avatar(id):move_to(target)` | Walk an avatar to a point. |
| `face(id, dir)` | `avatar(id):face(dir)` | Turn an avatar to face `"up"` / `"right"` / `"down"` / `"left"`. |

The full `avatar(id):method` API stays available for less common gestures
(`:look_at`, `:play`, `:play_until_end`, `:anchor`, `:position`).

A cutscene is composable with the per-configuration room helper:
`on_first_enter = cutscene(function() ... end)` — the flow helper still
spawns it, the outer task just arms the cutscene and dies; the inner body
runs as the real beat.

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
  -- Optional: return "laboratory"
end

return game
```

`game.on_start()` runs **once, on a new game**: when the `RoomScene` is entered
without a staged restore, in the global scope, **before the start room's
`on_load`** (so a room may read the world state `on_start` seeds — e.g. each
room's initial configuration). It is **skipped on Continue/Load**, so it never
overwrites a restored save. It is a plain (non-coroutine) call; wrap any blocking
sequence in `spawn(...)`. Returning a non-empty room id overrides the manifest's
`start_room` for that new game.

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

A node may carry a `run = function() ... end`. It fires **synchronously when the
node is entered** — however the conversation reached it — before the NPC line is
spoken; it is the dialog counterpart of a room's `on_load`. Use it for synchronous
side effects (record a fact, give/take an item):

```lua
ask_package = {
  npc = { "Una caja. Pesada, sin remitente claro." },
  run = function() set_state("delivery.knows_package", true) end,  -- on entry
  to  = "hub",
}
```

An **option** `run` differs: it fires the moment that reply is *chosen* (before
following `to`) and is spawned as a coroutine task, so blocking primitives (`talk`,
`wait`, `move_to`) work inside it. A node `run` is a direct call — for a blocking
or control-flow beat (`change_room`), put it on an option `run` or a `cutscene`.

### Topic shorthand — `dialog { ... }` + `topic`

A common shape is a **hub**: one menu the player keeps returning to, with topics
that appear only once the player has the right evidence, are said once, then route
back to the hub. Written by hand each topic costs an option (with `when`/`run`/`to`)
*plus* a separate response node — the two halves drift apart and the screenplay
disappears under bookkeeping. Wrap the tree in `dialog { ... }` and declare those
topics with `topic`, which keeps the player's line and the reply together:

```lua
return dialog {
  start = "hub",

  hub = {
    npc = "Empiece por lo observable. ¿Qué puede sostener?",

    topics = {
      topic "fracture" {
        requires = "finding.radial_fractures",   -- visible once this state is true
        player   = "Hay una lesión focal con fracturas radiales.",
        npc      = { "Bien. Eso sí es una observación útil." },
      },
      topic "discard_blade" {
        after  = "fracture",                      -- ...or: once another topic was said
        player = "Una herramienta filosa no explica el patrón.",
        npc    = { "De acuerdo. Apunta a impacto, no a filo." },
      },
    },

    -- Raw options still live alongside topics in the same node.
    options = {
      { "Después seguimos.", to = END },
    },
  },
}
```

`dialog { ... }` expands every node that carries a `topics` list — before the
validator or runtime ever see it — into the standard form: each `topic` becomes a
hub option (placed ahead of the node's own `options`) plus a response node that
routes back to the hub. It is pure Lua sugar; a tree with no `topics` needs no
`dialog{}` wrapper (see `dialogs/delivery_guy.lua` for the plain form).

A `topic "id" { ... }` takes:

| Field | Req | Meaning |
|-------|-----|---------|
| `id`  | yes | The topic id (the string before the `{ }`). Becomes the response node's id, so it must not collide with another node. |
| `player` | yes | The line Julia says — the option's text. |
| `npc` | yes | The reply: one line or a `{ }` list, like any node's `npc`. |
| `requires` | opt | Visible only when this holds: a **state-key string** (`get_state(key) == true`) or a **predicate function** returning a bool. |
| `after` | opt | Visible only after another topic was stated: a topic `id`, or a list of ids (all must be uttered). |
| `run` | opt | Extra action when the topic is chosen, after it is marked uttered. |

Each topic is offered **once**: stating it sets a reserved `__uttered.<id>` flag
(persisted like any `set_state`, surviving save/load), and the topic hides itself
once uttered. Read that flag with `uttered(id)` to gate later topics or build
cross-topic predicates without hand-maintained `argument.*_stated` flags:

```lua
local function has_basics()
  return uttered("fracture") and uttered("no_cut_marks") and uttered("no_collapse")
end
-- then:  topic "possible_fall" { requires = has_basics, ... }
```

`requires` and `after` compose: a topic with both appears only when its `requires`
holds *and* every `after` topic has been uttered. The worked example is the
Schneider puzzle, `dialogs/skull_trauma_cause.lua`.

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
  bottom). Font and size come from the manifest's top-level `speech` block;
  character colour comes from the optional `cast:` scene param.
- **Reaching the room beneath.** The room is frozen while the close-up is open, but
  its Lua API is still live, so a close-up script may make **instant** room changes
  that appear when the player backs out — `spawn_npc`, `despawn_npc`, `set_state`,
  `show_object`/`hide_object`, etc. This is the supported animation workaround (an NPC
  enters/leaves "off-screen"). Blocking room moves (`avatar(id):move_to`) are **not**
  available here — the room is not ticking, so they would never finish.
- **Deferring a blocking room beat — `on_room_resume(fn)` (M9 #186).** When backing
  out should play a *blocking* beat (Schneider walks in and speaks), the close-up
  cannot run it (the room is frozen) and a hand-rolled "leave a flag, poll for it in
  the room" watcher is the kind of plumbing a screenplay shouldn't carry. Instead the
  close-up hands the beat back: `on_room_resume(fn)` registers `fn` against the room
  beneath, and the engine runs it the moment that room is the live, ticking scene
  again — once, in the room's scope, blocking input until it finishes (exactly like a
  `cutscene { ... }` fired from a verb handler). `fn` is usually a room beat (a global
  `cutscene`-wrapped function), so the close-up just names it. It fires within a frame
  of the close-up closing, so it is **not** part of `GameState` (its state effects
  persist normally); a room change before it fires drops it.

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
  on_exit = function()
    -- Backing out turns Julia around to find Schneider already walking in. The
    -- close-up can't run that blocking beat itself, so hand it to the live room:
    if get_state("lab.read_skull") then
      on_room_resume(schneider_arrives) -- a cutscene { ... } beat defined in lab.lua
    end
  end,
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

The engine **auto-spawns** these script-author entry points (M9 #183), so a
handler can use `talk`, `wait`, `avatar(id):move_to`, `show_text`, and any
other blocking primitive directly:

- room hotspot verb handlers (`room.hotspots.<id>.<verb>`);
- inventory item verb handlers (`inventory.<id>.<verb>`);
- `game.fallbacks.<verb>`;
- dialog option `run` actions (a *node* `run` is a direct call, not spawned — see §Dialog scripts);
- close-up hotspot handlers (`closeup.hotspots.<id>`).

Beyond these dispatch sites, the `cutscene { ... }` block (M9 #184) also
spawns its body so a scripted beat doesn't have to wrap itself in
`spawn(...)`. See *Cutscene blocks* in §Hotspot handler signatures above.

`on_room_resume(fn)` (M9 #186) is the **cross-scope** variant: a close-up's
`on_exit` (a direct hook) can't block, so it registers `fn`, and the engine
fires it later through the same auto-spawn seam — in the **room's** scope, not
the close-up's. Because the close-up scope is torn down before `fn` runs, `fn`
(and any upvalues it closes over) lives only by being held on the engine side;
this is safe by construction — scope cancellation reaps running *tasks*, not Lua
*values*. See §Close-up scripts.

The following hooks stay **direct** (non-coroutine), so blocking calls inside
them must still be wrapped in `spawn(...)` (or assigned a `cutscene`-wrapped
function):

- `room.on_load` / `room.on_player_entered` / `room.on_unload`;
- `room.on_zone_enter` / `room.on_zone_exit` / `room.on_screen_edge`;
- dialog `on_enter` / `on_exit` and a dialog **node** `run` (fires on entry);
- close-up `on_enter` / `on_exit`;
- `game.on_start`.

`talk` is the one blocking primitive that degrades gracefully on the main
thread: called directly from a non-coroutine hook it shows the line
fire-and-forget rather than erroring. To play several lines in order from
such a hook, still wrap them in `spawn`.

Valid (auto-spawned handler — no wrapper needed):

```lua
room.hotspots = {
  termo = {
    use = function()
      avatar("player"):move_to("at_desk")
      talk("player", "Cargo el mate.")
      wait(0.5)
      talk("player", "Listo.")
    end,
  },
}
```

Valid (explicit spawn for a background task inside a non-coroutine hook):

```lua
function room.on_load()
  spawn(function()
    talk("julia", "Voy hasta la puerta.")
    avatar("julia"):move_to("at_door")
  end)
end
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
| `talk(speaker, text [, opts])` | character id, string, optional table | — | Show speech near the speaker, play its talk animation, and yield until finished/skipped. `opts.continue_action = true` preserves walking/current action; `opts.face` may name an avatar or room point to face first. |
| `remark(speaker, text)` | character id, string | — | Incidental speech that preserves the speaker's current movement or gesture. Equivalent to `talk(speaker, text, { continue_action = true })`. |
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

By default `talk` stops the speaker and selects `talk_<current-facing>`. Partial
character rigs are valid: the engine falls back to another authored direction and
then to bare `talk`; if no talk loop exists, the character simply stands. Use
`talk("player", line, { continue_action = true })` for an incidental remark that
must not interrupt walking or a gesture; the shorter `remark("player", line)` is
preferred when no other options are needed. `start_dialog` automatically stops
the player and NPC, faces them toward each other, and applies this animation
selection to each line's active speaker.

### Rooms and camera

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `change_room(id, entry_point?)` | room id, optional point id | — | Load another room inside `RoomScene`. |
| `current_room()` | — | room id | Return current room id. |
| `set_room_config(room_id, config_id)` | room id, config id | — | Switch a room's [configuration](#room-configurations) (#185). Live room: reconcile presence now; another room: record it, applied on its next load. |
| `current_room_config(room_id?)` | optional room id | config id | The room's live config id (defaults to the current room); `""` for a room without `configs:`. |
| `open_closeup(scene_id)` | `CloseUp` scene id | — | Open an examine view as an overlay over the room; it pops back here on Esc / right-click (design 04 §CloseUp). |
| `open_case_resolution(scene_id)` | `CaseResolution` scene id | — | Open a deduction template over the room. Esc/right-click cancels; slot assignments persist when it is reopened. Its sidecar may handle `on_check(invalid_slots)` and `on_exit(status)`, where status is `solved`, `incorrect`, or `cancelled`. |
| `start_cutscene(scene_id)` | manifest scene id | — | Leave the room for a manifest scene (typically a `Cutscene` / `StoryText`) — e.g. an act-closing cutscene fired from a verb handler. Unlike `open_closeup` this **replaces** the room (it does not pop back); the target scene's own `on_finish` decides where to go next. Persistent `GameState` survives; the live room is unloaded. |
| `on_room_resume(fn)` | a function (usually a `cutscene`-wrapped beat) | — | Defer a blocking room beat until the room is the live, ticking scene again — the bridge a **close-up** uses (from its `on_exit`) to hand a beat back to its frozen room (design §Close-up scripts). `fn` runs **once**, in the room's scope, blocking input until it finishes. Transient: it fires within a frame of the close-up closing, so it is not saved in `GameState`; a room change before it fires drops it. |
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
| `:set_visible(visible)` | bool | — | Show/hide the avatar transiently. `:show()` and `:hide()` are aliases. |
| `:set_shadow_opacity(opacity, transition_seconds?)` | number, optional seconds | — | Set a transient 0..1 multiplier for both projected and contact shadows. A positive duration interpolates from the current value. |
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
| `add_case_term(id)` | term id | bool | Discover a case term. Returns `true` only when newly added. |
| `has_case_term(id)` | term id | bool | Test whether a case term has been discovered. |
| `remove_case_term(id)` | term id | bool | Remove an owned case term. Returns `true` only when it was owned. |
| `clear_case_terms()` | — | integer | Remove every owned case term and return how many were removed. Use when starting a new case. |
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

Case terms are authored in the term-bank YAML but start unavailable. Award them
from exploration, conversation, or event handlers with `add_case_term("tomas")`.
Ownership is idempotent and is saved in `GameState`; a case-resolution scene only
shows the terms the player owns. `has_case_term` can guard one-time feedback when
needed. Terms are not consumed automatically when a template is solved: case
logic may call `remove_case_term(id)` for any terms that should leave the shared
bank, while reusable terms remain owned.
Call `clear_case_terms()` when a new case begins if terms from the previous case
must not carry over.

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

#### Light handle

`light(id)` returns a handle to a dynamic light declared in the current room's
`lighting.lights`. Runtime enabled/intensity overrides are **transient**: they
reset to the YAML values whenever the room loads. If a story decision must
survive leaving the room, store that decision with `set_state` and reapply it in
`on_load`.

| Method | Parameters | Returns | Meaning |
|--------|------------|---------|---------|
| `:set_enabled(enabled)` | bool | handle | Include or exclude the light from the lighting pass. |
| `:enable()` / `:disable()` | — | handle | Convenience aliases for `:set_enabled`. |
| `:enabled()` | — | bool or `nil` | Current runtime value, or `nil` when the light id is absent. |
| `:set_intensity(value [, transition_seconds])` | number, optional seconds | handle | Set peak intensity, clamped to `0..4`; a positive duration applies a smooth transition and authored modulation still applies. |
| `:intensity()` | — | number or `nil` | Current peak intensity, or `nil` when the light id is absent. |

```lua
function room.on_load()
  light("desk_lamp"):set_enabled(get_state("archive.power_on") == true)
end

function room.hotspots.switch.use()
  local lamp = light("desk_lamp")
  lamp:set_enabled(not lamp:enabled())
end
```

`light_occluder(id)` controls a polygon declared in `lighting.occluders`. Its
`:set_enabled(bool)`, `:enable()`, `:disable()`, and `:enabled()` methods mirror
the light handle. This state is also transient, making it suitable for opening a
door after the room script has reapplied the saved story state:

```lua
light_occluder("vault_door"):set_enabled(not get_state("vault.open"))
```

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
| `:set_rotation(degrees)` | number | — | Set clockwise object rotation. Applies to static, animated, and composite visuals. |
| `:rotation()` | — | number or `nil` | Current object rotation in degrees. |
| `:play(sequence)` | string | — | Play an animation or composite sequence; no-op + warn for a static texture. |
| `:play_until_end(sequence)` | string | — | Play a non-looping animation/composite sequence and yield until it finishes. |

An object whose `sprite` is an animation (`*.anim.yml`) or composite
(`*.composite.yml`) owns a `VisualSprite`. Its `position` is the visual pivot (not
the top-left used for a static texture), and it plays its `sequence:` on load.
`play` and `play_until_end` are no-ops on a static-texture object.

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
| `play_music(path, loop?, gain?)` | logical path, optional bool/number | — | Start streamed music immediately, replacing any current track. `gain` (default `1`) scales this cue without changing the user's Music setting. |
| `crossfade_music(path, seconds?, preserve_offset?, loop?, gain?)` | logical path, optional number/bool/bool/number | bool | Equal-power fade from the current track to a new one. Defaults: `2.5`, `false`, `true`, `1`. Returns false if the new track cannot be loaded. |
| `stop_music(fade_seconds?)` | optional seconds (default `0`) | — | Stop current music immediately, or fade it out over a positive duration. |
| `play_sound(path, volume?, pan?)` | logical path, optional number (0..1), optional number (-1..1) | — | Play a short sound effect. `volume` scales the global SFX volume; `pan` places it L/R (-1 left, 0 center, +1 right). Panning affects mono clips only; a position-aware/spatial path is design-for. |
| `stop_sound(path, fade_seconds?)` | logical path, optional seconds (default `0`) | — | Stop every active instance of the sound at `path`. A positive duration fades from its current gain before stopping; omitted or `0` stops immediately. |
| `stop_sounds(fade_seconds?)` | optional seconds (default `0`) | — | Stop all active sound effects, optionally fading them first. |
| `set_ambience(path, volume?, seconds?)` | logical path, optional number, optional number | — | Replace the streamed ambience base, crossfading over `seconds` (default `2.5`). If `path` is already playing, it is not restarted; only its volume glides. YAML random layers remain active. |
| `set_ambience_volume(volume, seconds?)` | number (0..1), optional number | — | Glide the current ambience base to a new room-relative volume (default `1.0` second). |
| `stop_ambience(seconds?)` | optional number | — | Clear random layers and fade out the ambience base (default `2.5` seconds). |
| `set_ambience_layer_enabled(id, enabled)` | string, bool | bool | Enable/disable a YAML-declared random layer. Returns false for an unknown id. |
| `set_ambience_layer_volume(id, volume)` | string, number (0..1) | bool | Set a multiplier over a random layer's authored volume range. Returns false for an unknown id. |

Use `play_music` when the new cue must start immediately. Use `crossfade_music`
for room-to-room ambience or score changes:

```lua
crossfade_music("music/archive_interior.ogg", 3.0)
```

Pass a cue-local gain when a track should sit quietly beneath dialogue. This is
multiplied by, and does not replace, the player's Music setting:

```lua
crossfade_music("music/quiet_room.ogg", 2.5, false, true, 0.2)
```

Set `preserve_offset` only when both files are synchronized variations of the
same composition. The incoming offset is the outgoing position modulo its own
duration:

```lua
crossfade_music("music/archive_danger.ogg", 1.5, true)
```

Room YAML should supply the normal ambience. Runtime calls are for variations,
for example suppressing traffic during a story beat without disturbing the city
loop:

```lua
set_ambience_layer_enabled("traffic", false)
set_ambience_volume(0.25, 1.5)
```

### Input and room-view control

| Function | Parameters | Returns | Meaning |
|----------|------------|---------|---------|
| `block_input()` | — | — | Switch room view to the `blocked` state: input disabled and the SCUMM panel faded to a black bar under the scenery. Use for cutscene-like moments. |
| `unblock_input()` | — | — | Restore the `command` state: input enabled and the panel faded back in. |
| `set_room_view_state(state)` | `"command"` \| `"blocked"` | — | Explicitly set the room-view state — the same two script-settable states as `unblock_input` / `block_input`. The `dialog` and `menu` states are engine-managed (entered via `start_dialog` / the pause menu); passing them, or any other string, logs a warning and is ignored. |

Hiding the SCUMM panel is not a separate operation: the panel is shown in the
`command` state and faded out in `blocked`, so `block_input()` / `set_room_view_state`
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
local flow = include("rooms/_room_flow.lua")
local configs = { include("rooms/lab/act1/intro.lua"), include("rooms/lab/act1/puzzle.lua") }
function room.on_load() flow.enter("lab", configs) end
```

This is the basis of the **per-configuration room convention**: a room is shown in
one of several configurations (the `<room>.cfg` integer; a story act just appends
more). Split each config into its own module (`configure` for instant presence,
`on_first_enter` / `on_reenter` for one-time vs returning beats), grouped by act in
subdirs (`rooms/<room>/act1/<role>.lua`), dispatched by the shared
`rooms/_room_flow.lua` helper. Hotspots stay centralized in the room entry. A
copy-paste template lives under `games/<game>/design_files/templates/room_configs/`,
with `rooms/lab.lua` as the worked example.

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
