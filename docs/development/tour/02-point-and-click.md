# Point & click kit

Companion: [tour index](index.md). Read this before editing `room_scene.cpp` or the verb panel.

The room view is not one class. It is a **session** (`RoomScene`) that wires an
**action model** (commands) to **widgets** (SCUMM panel, dialog list) and a
**world** (room data, avatars, camera). The SCUMM panel is one way to *compose*
a command. It is not the command system.

---

## 1. Concepts

### Scene vs room

A **scene** is a top-level app state (`TitleScreen`, `RoomScene`, `Notebook`).
A **room** is a place *inside* `RoomScene`, loaded from `rooms/<id>.{yaml,lua}`
via `change_room`. Saves record chapter + RoomScene id + current room + pose.
Do not invent a new `Scene` per room.

### Three coordinate spaces

```
world  --camera-->  virtual  --letterbox-->  window
```

`RoomViewport` is the scenery rectangle in virtual pixels. It does **not**
derive from the SCUMM panel. Design 04's 85/15 split is historical.

Pointer path: window event → `Display` → virtual `RoutedInput` → first widget
that `captures` the point → else scenery `virtual_to_world` → hotspot / walkable.

### Action model

```cpp
struct Command {
    Verb verb;                       // look_at, talk_to, pick_up, use, give, …
    ObjectRef param1;                // room hotspot or inventory item
    std::optional<ObjectRef> param2; // give X to Y; use X with Y
};
```

Verb ids are Lua handler names and strings-table keys. Labels are localized.
Affordances gate the UI; the Lua handler decides the outcome.

| Verb | param1 | param2 |
|---|---|---|
| `talk_to`, `pick_up` | room object | — |
| `give` | inventory item | room object |
| `use` on a combinable item | inventory item | anything |
| other verbs | room or inventory | — |

### Two state machines

**View state** (`COMMAND` / `DIALOG` / `BLOCKED` / `MENU`) is the session.
**Builder state** (`IDLE` → `EXPECTING_PARAM*` → `COMMAND_READY` →
`COMMAND_EXECUTING` → `IDLE`) is the sentence. Do not collapse them.
`BLOCKED` is not `COMMAND_EXECUTING`.

### Room contents

YAML says what exists; Lua says what happens. Layers, regions, objects,
hotspots, points, walkable + obstacles, zones, avatars, lights.

Hit-test: enabled hotspots, first match. Per hotspot: explicit `area`, else
bound object frame, else bound region polygon.

### Scopes and yielding

One `lua_State`. Tasks belong to a scope. `change_room` runs `on_unload`,
**cancels the room scope** (no Lua `finally`), destroys `RoomRuntime`, loads
the next room, runs `on_load`. Yielding APIs (`talk`, `move_to`) wait on events
emitted from `update`. From a frozen overlay they never complete.

---

## 2. Main types

World: `RoomData`, `RoomRuntime`, `Avatar`/`Mover`, `Camera`, `RoomViewport`,
`RoomRenderer`, `RoomLightingRenderer`, `SpeechManager`, `InventoryModel`,
`DialogRuntime`, `Cast`.

Action: `Command` / `CommandBuilder` / `CommandState` / `CommandController` /
`RoomCommandSink` / `RoomCommandProcessor`.

UI: `RoomUiIntent`, `RoomUiState` + stream, `UiWidget`, `RoomInputRouter`,
`ScummPanel` (painter), `ScummWidget` (adapter), `DialogWidget`.

`ScummPanel` must not call Lua, own the builder, or know hotspot polygons.

```
RoomScene
  RoomRuntime + Camera + Avatars + Renderer
  CommandController → CommandBuilder
  RoomCommandProcessor  (submit / approach / Lua order)
  ScummWidget + DialogWidget  ← RoomUiStateStream
```

`RoomScene` is also the host, Lua façade, wait-board, persist-map owner, and
pause-menu painter. That is debt.

`enter()` registers input layers in order: `DialogWidget`, `ScummWidget`,
then the scenery adapter (`RoomScene` itself).

---

## 3. Sequences

**Look at door.** Verb click → `SELECT_VERB` → builder expects param1. Scenery
click → hotspot → `provide_object` → `COMMAND_READY` → `submit`. If the door
has an approach and the player is far: `BLOCKED` + deferred. When the mover
stops: dispatch `hotspots.door.look_at`, else `game.lua` fallback.

**Give key to clerk.** `GIVE` → inventory `key` → room `clerk` → walk →
`inventory.key.give("clerk")` then hotspot then `game.lua`.

**Walk.** Builder `IDLE`, walkable, no hotspot → `follow_path`. Not a `Command`.

**Interrupt approach.** Primary click while deferred: cancel, stop player,
restore `COMMAND`, route the new click.

**Dialog.** `start_dialog` → view `DIALOG` → options in `RoomUiState` →
`CHOOSE_DIALOG_OPTION`. `END` cancels the dialog scope and returns to `COMMAND`.

**Room change.** Fade out → `on_unload` → cancel room scope → destroy runtime →
load → restore persist maps → seat player → spawn YAML NPCs → `on_load` →
autosave if allowed → fade in.

---

## 4. Pitfalls

1. Treating the panel as the command system. A one-click UI must `submit(Command)`.
2. Confusing view state with builder state.
3. Adding `api_*` / `pending_*` on `RoomScene` instead of extracting.
4. Holding `RoomHotspot*` across `unload_room`. Handles store ids.
5. Saving Lua locals. Only engine stores survive load.
6. `avatar:move_to` from a pushed overlay: room does not `update`.
7. Binding genre globals to a `RoomScene*`. One owner. Overlays use `push_scene`.
8. Sizing the camera from panel height. Viewport is independent.
9. Missing affordance vs missing handler (click ignored vs “nothing happens”).
10. Assuming default-walk-or-smart-verb. `IDLE` + scenery = walk.
11. Extracting a third `CommandDispatcher`. It is `RoomCommandProcessor`.
12. Growing `RoomCommandProcessorHost` with draw/widget methods.

---

## 5. Where to change what

| You want to… | Touch |
|---|---|
| Add a verb | `command.hpp/cpp`, strings, builder, processor `valid`, panel YAML, Lua docs |
| Sentence preview | `CommandController` + strings |
| Walk-to-use / chase | `RoomCommandProcessor`, `approach_follow.hpp` |
| Lua handler order | `RoomCommandProcessor::dispatch` only |
| Move/hide the panel | `scumm_panel.yml`, `set_ui_widget_visible` |
| Hit-testing | `RoomRuntime::hotspot_at`, `RoomScene::hotspot_under` |
| Z-order | YAML `z` / `baseline`; renderer sort |
| Yielding wait | a wait-board, not another `pending_*` on the scene |
| Tests | submit a `Command`; do not synthesize clicks unless testing a widget |

First afternoon: `command.hpp`, `command_builder.hpp`, builder tests,
`room_command_processor.hpp`, `scumm_widget.hpp`, `room_viewport.hpp`, then
skim `RoomScene::handle_event` / `handle` / `handle_ui_intent`. Play
`examples/02_scumm_inventory` with F4.
