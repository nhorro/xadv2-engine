# Lua API

This is the current author-facing Lua surface. It is maintained against the
bindings in `lib/src/core/scripting.cpp`, `lib/src/core/lua_api.cpp`,
`lib/src/gfx/script_scene.cpp`, and `lib/src/pnc/room_scene.cpp`. Names beginning
with `_` are implementation details and are deliberately omitted.

For a runnable starting point, copy the nearest game under `examples/`:

| Need | Example |
|---|---|
| Room, hotspot, and movement | `examples/01_hello_room` |
| Verbs and inventory | `examples/02_scumm_inventory` |
| NPC and dialog | `examples/03_dialog_npc` |
| Scripted text/cutscene | `examples/04_cutscene` |
| Scripted close-up | `examples/05_closeup` |
| Generic 2D scene | `examples/07_script_scene` |

## Mental model

A Lua file is loaded in one shared Lua state and normally returns a table of
callbacks. The engine owns scenes, entities, and saved state; Lua refers to them
by stable string ids. A handle such as `avatar("guard")` stores an id and resolves
the current engine object on every call.

Lua tasks are cooperative coroutines. `wait`, `wait_event`, `show_text`, `talk`,
camera motion, avatar motion, and `play_until_end` can suspend the current task
without blocking the frame. Running tasks and Lua locals are transient: saves
contain engine-owned state, not the Lua stack.

The available standard libraries are `base`, `coroutine`, `string`, `math`, and
`table`. Use `include(path)` for another game script; `require`, `dofile`, `io`,
and `os` are not part of the authoring environment.

## Where yielding is allowed

The engine creates a coroutine automatically for:

- room hotspot handlers;
- room configuration `on_first_enter` and `on_reenter` beats;
- inventory handlers and global verb fallbacks;
- dialog option `run` callbacks;
- close-up hotspot handlers;
- a `cutscene(function() ... end)` body.

Lifecycle hooks are direct calls: `game.on_start`, room `on_load`, `on_unload`,
`on_player_entered`, `on_zone_enter`, and `on_zone_exit`; dialog/node and
close-up lifecycle hooks; and every `ScriptScene` callback. Start a task with
`spawn(function() ... end)` when one of those hooks needs an ordered blocking
sequence. `talk` is safe in a direct hook, but is fire-and-forget there.

Tasks inherit the active scope. Leaving a room, dialog, close-up, or script scene
cancels its tasks without running Lua cleanup. Put cleanup in the corresponding
`on_unload`, `on_exit`, or `on_leave` hook.

## Core functions

### Tasks, text, and events

| Call | Result | Behavior |
|---|---|---|
| `spawn(fn)` | task id | Start `fn` in the current scope. |
| `wait(seconds)` / `sleep(seconds)` | — | Yield until the timer expires. |
| `emit(name, payload?)` | — | Resume current-scope tasks waiting for `name`. Events are not queued. |
| `wait_event(name)` | payload | Yield until a same-scope `emit`. |
| `show_text(text, duration?, opts?)` | — | Show centered text and yield. `opts.id` is a localization id. The duration is derived from the text when omitted. |

`emit` reaches only listeners waiting at that moment in the same scope. It does
not cross a room or scene boundary.

### Script composition and localization

| Call | Result | Behavior |
|---|---|---|
| `include(path)` | script return value or `nil` | Read and execute a logical resource path. It runs on every call and is not cached. |
| `resource_path(path)` | path or `nil` | Validate and return a logical path; this never exposes an OS path. |
| `tr(id, source)` | localized string | Resolve stable content id `id`, falling back to `source`. Available while a room is active. |

### Persistent state

| Call | Result | Behavior |
|---|---|---|
| `get_state(key)` | bool, number, string, or `nil` | Read game-wide state. |
| `set_state(key, value)` | — | Store a bool, number, or string. Other types, including `nil` and tables, are rejected. |
| `get_room_state(key)` | scalar or `nil` | Read state scoped to the current room. |
| `set_room_state(key, value)` | — | Store a scalar in the current room. |

Use dotted global keys such as `archive.power_on`. Keys beginning with `__` are
reserved by the engine. Lua locals, coroutine positions, runtime object poses,
light overrides, and NPC presence are not saved.

For boolean story flags, declare `facts.yaml` and use the `facts` proxy:

```yaml
namespaces:
  archive: [power_on, guard_distracted]
```

```lua
if facts.archive.power_on then
  facts.archive.guard_distracted = true
end
```

This reads and writes the same dotted keys as `get_state` and `set_state`. In a
Debug build, undeclared names warn without preventing the access. Keep counters,
strings, and dynamically constructed keys on the explicit state functions.

### Audio

| Call | Defaults | Behavior |
|---|---|---|
| `play_music(path, loop?, gain?)` | `true`, `1` | Replace the current streamed music. |
| `crossfade_music(path, seconds?, preserve_offset?, loop?, gain?)` | `2.5`, `false`, `true`, `1` | Crossfade to a cue; returns whether it loaded. |
| `stop_music(seconds?)` | `0` | Stop now or fade out. |
| `play_sound(path, volume?, pan?)` | `1`, `0` | Play an SFX; pan is `-1..1`. |
| `stop_sound(path, seconds?)` | `0` | Stop every active instance of a path. |
| `stop_sounds(seconds?)` | `0` | Stop all SFX. |
| `set_ambience(path, volume?, seconds?)` | `1`, `2.5` | Set or crossfade the ambience base. |
| `set_ambience_volume(volume, seconds?)` | `1` second | Glide the base volume. |
| `stop_ambience(seconds?)` | `2.5` | Stop ambience and clear random layers. |
| `set_ambience_layer_enabled(id, enabled)` | — | Toggle a declared random layer; returns `false` for an unknown id. |
| `set_ambience_layer_volume(id, volume)` | — | Set a declared layer multiplier; returns `false` for an unknown id. |

### Case terms

`add_case_term(id)`, `has_case_term(id)`, and `remove_case_term(id)` return
whether they changed/found ownership. `clear_case_terms()` removes all owned
terms and returns the count. Ownership is persistent.

## Point-and-click room API

These globals exist while the manifest's `RoomScene` is active.

### A minimal room script

```lua
local room = {}

function room.on_load()
  light("desk_lamp"):set_enabled(facts.archive.power_on)
end

function room.on_zone_enter(id)
  if id == "exit" then change_room("hall", "from_archive") end
end

room.hotspots = {
  switch = {
    use = function()
      facts.archive.power_on = not facts.archive.power_on
      light("desk_lamp"):set_enabled(facts.archive.power_on)
      return "Click."
    end,
  },
}

return room
```

Room tables may define `on_load()`, `on_unload()`, `on_player_entered()`,
`on_zone_enter(id)`, `on_zone_exit(id)`, `hotspots[id][verb]`, and
`configs[id].on_first_enter` / `.on_reenter`. `on_load` runs on every entry and
after restoring a save. The Lua file and all of its locals are rebuilt each time.

A hotspot handler receives no argument for a one-object command. For `use X with
Y` or `give X to Y`, a handler bound to X receives Y's id and a handler bound to
Y receives X's id. Return a string for command feedback. A handler that returns
`nil` still counts as handled but produces no caption; resolution proceeds only
when that handler is absent.

### Rooms, scenes, and camera

| Call | Behavior |
|---|---|
| `change_room(id, entry_point?)` | Queue another room within the current `RoomScene`. |
| `current_room()` | Return the current room id. |
| `set_room_config(room_id, config_id)` | Persist a configuration and reconcile a live room immediately. |
| `current_room_config(room_id?)` | Return the current configuration id, or `""` when none is defined. |
| `finish_chapter()` | Move to the next composed chapter; remain put with a warning at the last chapter. |
| `open_closeup(scene_id)` | Push a `CloseUp` over the room. |
| `open_cutscene(scene_id)` | Push a `Cutscene`; use an `on_finish: POP` outcome to return. |
| `open_case_resolution(scene_id)` | Push a `CaseResolution` view. |
| `start_cutscene(scene_id)` | Replace the room scene with another manifest scene. |
| `camera_look_at(target)` | Snap to a target and stop following the player. |
| `camera_go_to(target)` | Tween to a target and yield until the pan finishes. |
| `camera_follow_player()` | Resume player tracking. |

A target is a named room point, an avatar id, or `{ x = number, y = number }`.
Room changes are deferred; once the current callback returns, unloading cancels
the old room scope.

### Speech and scripted beats

| Call | Behavior |
|---|---|
| `talk(speaker, text, opts?)` / `say(...)` | Speak and, in a coroutine, yield. `opts.continue_action`, `opts.face`, and `opts.id` are optional. |
| `remark(speaker, text, opts?)` | Speak without interrupting the current movement/gesture. |
| `float_text(text, where, opts?)` | Non-blocking label at a point, `"npc:id"`, `"object:id"`, or `{x,y}`. `opts` accepts `duration`, `color`, and localization `id`. |
| `move(id, target)` | Alias for `avatar(id):move_to(target)`. |
| `face(id, direction)` | Alias for `avatar(id):face(direction)`. |
| `cutscene(body, on_skip?)` | Return a callback that spawns `body` and blocks room input until it drains. |
| `skippable_cutscene(body, on_skip)` | As above, requiring a finalizer for skip/cancellation. |
| `on_room_resume(fn)` | From a close-up, schedule one callback for the room's next live update. A later registration replaces the first. |

`on_room_resume` is transient and intended for the frame after an overlay closes;
it is not serialized in a save.

### Avatars and room objects

`avatar(id)` returns an id handle with these methods:

| Method | Behavior |
|---|---|
| `:move_to(target)` | Pathfind and yield until arrival. |
| `:look_at(target)` / `:face(direction)` | Face a target or `up`, `right`, `down`, `left`. |
| `:position()` / `:anchor(name)` | Return `{x,y}`; an absent anchor returns `nil`. |
| `:set_visible(bool)` / `:show()` / `:hide()` | Change transient visibility. |
| `:set_shadow_opacity(value, seconds?)` | Set or tween the transient shadow multiplier. |
| `:play(sequence)` | Start a sequence. |
| `:play_until_end(sequence)` | Start a non-looping sequence and yield until it ends. |

`spawn_npc(id, start, orientation?)` creates or repositions a non-player cast
member; `despawn_npc(id)` removes it. Presence is rebuilt on room load, so derive
conditional presence from saved state or room configurations.

`object(id)` returns an id handle:

| Method | Behavior |
|---|---|
| `:move_to(target, speed?)` | Move directly and yield until arrival. |
| `:set_position(x,y)` / `:position()` | Write/read world position. |
| `:set_scale(value)` | Set uniform scale. |
| `:set_rotation(degrees)` / `:rotation()` | Write/read clockwise rotation. |
| `:play(sequence)` / `:play_until_end(sequence)` | Control animated/composite objects. |

Object pose is transient. Object visibility is persistent when changed through
`show_object(id)` or `hide_object(id)`.

### Scenery, light, and UI

| Call | Persistence |
|---|---|
| `set_region_state(id, state)` / `get_region_state(id)` | per room |
| `show_object(id)` / `hide_object(id)` | per room |
| `set_layer_visible(id, visible)` | per room |
| `enable_hotspot(id)` / `disable_hotspot(id)` | per room |
| `enable_obstacle(id)` / `disable_obstacle(id)` | per room |
| `set_ui_widget_visible(id, visible)` and `show_ui_widget` / `hide_ui_widget` | transient |
| `block_input()` / `unblock_input()` | transient |
| `set_room_view_state("command" | "blocked")` | transient |

Current widget ids are `scumm` and `dialog`. The `dialog` and `menu` room-view
states are engine-managed and cannot be selected through `set_room_view_state`.

`light(id)` exposes `set_enabled(bool)`, `enable()`, `disable()`, `enabled()`,
`set_intensity(value, seconds?)`, and `intensity()`. `light_occluder(id)` exposes
the same enabled controls. These overrides are transient; reapply saved story
state from `on_load`.

### Inventory and global fallbacks

`has_item(id)`, `add_item(id)`, `remove_item(id)`, and `list_items()` operate on
the saved inventory. `inventory.lua` returns `inventory[item_id][verb]`
callbacks. `game.lua` returns this shape:

```lua
return {
  on_start = function()
    facts.archive.power_on = false
    -- optionally return a room id to override start_room
  end,
  fallbacks = {
    look_at = function(target) return "Nothing special." end,
    use = function(first, second) return "That does not work." end,
  },
}
```

`on_start()` is a direct call made only for a new game, before the first room's
`on_load`; it is skipped for Continue/Load. Fallback handlers are coroutine-
enabled and receive the complete operand list.

## Dialog scripts

`start_dialog(id, speaker?)` enters dialog mode and loads `dialogs/<id>.lua`.
The speaker defaults to the dialog id. A plain dialog returns a tree:

```lua
return {
  start = "greeting",
  on_enter = function() end,
  on_exit = function() end,

  greeting = {
    npc = { "First line.", "Second line." },
    options = {
      { "Who are you?", to = "answer", once = true },
      { "Goodbye.", to = END },
    },
  },

  answer = {
    run = function() facts.archive.met_curator = true end,
    npc = "The curator.",
    to = "greeting",
  },
}
```

Tree hooks and a node's `run` are direct calls. An option may define `when`,
`run`, `to`, `once`, `silent`, and a stable `id`; its `run` is coroutine-enabled.
A node has either `options` or `to`. `END` is available only while the dialog
file is loading and ends the conversation.

For hub conversations, `dialog { ... }`, `topic "id" { ... }`, and
`uttered(id)` provide the current topic shorthand. A topic accepts `player`,
`npc`, optional `requires` (state-key string or predicate), optional `after`
(topic id or list), and optional `run`. It appears once and records its uttered
flag persistently.

## Close-ups

A close-up sidecar returns direct lifecycle hooks and coroutine-enabled hotspot
handlers:

```lua
return {
  on_enter = function() end,
  hotspots = {
    inscription = function()
      talk("player", "The date has been scratched out.")
      set_hotspot_name("inscription", "scratched date")
    end,
  },
  on_exit = function() end,
}
```

While it is open, `set_hotspot_name(id, name)` changes a hover label,
`shout(text?)` controls the top banner, and `close_closeup()` queues dismissal.
The room below is frozen, although its instant state/scenery functions remain
bound. Do not wait on room movement from the overlay; use `on_room_resume(fn)`
for a blocking beat after dismissal.

## Generic ScriptScene

`ScriptScene` has a separate context-and-entity API rather than the room globals.
Its `on_enter(ctx)`, `on_input(ctx,event)`, `update(ctx,dt)`, and `on_leave(ctx)`
callbacks are synchronous. See [Scriptable scenes](script-scenes.md) for the
event shapes and every context/entity method.

## Failure behavior

Invalid ids, paths, parameter types, and Lua errors are logged without aborting
the process where a safe fallback exists. Treat diagnostics as authoring errors:
the runtime may stay alive, but the requested action may have been skipped.
