# Ground-Up Engine Implementation Plan

## Summary

Build the new engine from the design docs as a greenfield implementation. The
current prototype is reference material only: reusable assets, sample behavior
ideas, and comparison targets, not architecture to preserve.

Target endpoint: a playable 3-room MVP game with manifest-driven startup, fixed
virtual resolution, YAML/Lua authoring, animated player avatar, hotspots,
inventory, SCUMM panel, dialog, room transitions, save/load with 3 manual slots +
autosave, and basic debug tools.

Accept Follow-up F3 as part of the plan: `SettingsScene` is a built-in
manifest-declared scene; `continue` is an engine-owned load-latest-save action.

## Milestones And Issues

### M0 — Core Shell: Blank App

Goal: an application that starts from `game.yaml`, opens a window, runs the loop,
and can show a blank scene.

Issues:

1. Build layout and dependencies: create engine library, game executable, tests
   target, CTest/doctest setup, SFML 2.6, Lua 5.4, yaml-cpp, sol2.
2. Core application: fixed 60 Hz loop, window creation, virtual resolution,
   letterboxing, physical-to-virtual input mapping.
3. Core services: `EngineContext`, diagnostics, dev flags, settings skeleton,
   resource source interface with filesystem backend.
4. Scene system: `Scene`, `SceneManager`, scene stack, scene registry/factory,
   manifest parser with `entry`, `scenes`, `resources.src`, `strings`.
5. Built-in blank/title/settings stubs: `TitleScreen` can route `new_game`,
   `settings`, `exit`; `SettingsScene` can push/pop with placeholder controls.

Acceptance:

- `pac_sample` starts from a manifest and displays a blank/title screen.
- Window resize preserves aspect ratio.
- Unit tests cover manifest validation, scene stack behavior, resource logical
  path validation.

### M1 — Generic 2D

Goal: load image/atlas/animation data and display a single animated sprite. Skip
`CompositeSprite` and full pathfinding.

Issues:

1. Resource cache: textures, fonts, YAML files, Lua/script bytes, sound buffers;
   logical path diagnostics.
2. Geometry module: `Point`, polygon parsing, point-in-polygon, bounds, segment
   intersection.
3. Spritesheet loader: atlas texture + named frames + anchors from YAML.
4. `AnimatedSprite`: sequence playback, pivot anchor positioning, loop/non-loop
   completion callback.
5. Minimal audio/settings: music player, sound player skeleton, volume settings.
6. Test/sample scene: display an animated sprite over a background or solid
   color.

Acceptance:

- Sample scene loads `*.yaml` spritesheet + `*.anim.yaml` and animates a sprite.
- Headless tests cover geometry, spritesheet parsing, animation timing, resource
  cache misses.
- Manual check: sprite pivot remains stable across frames.

### M2 — Lua Bridge Skeleton

Goal: scripts run as scoped cooperative tasks and can call basic engine APIs.

Issues:

1. Scripting service using sol2: one `sol::state`, protected calls, flat
   snake_case globals.
2. Script scheduler: `spawn`, `wait`, `emit`, `wait_event`, task ids, task
   scopes, lifecycle cancellation.
3. Script handles: id-only usertypes for future `avatar(id)` pattern.
4. API skeleton: `resource_path`, `play_music`, `stop_music`, basic state store,
   diagnostics for invalid ids.
5. Script-driven `StoryText` scene: runs a Lua coroutine, displays text pages,
   supports skip.

Acceptance:

- Lua sample can `spawn`, `wait`, emit/wait events, and call a C++ registered
  function.
- Tests cover task state transitions, scoped cancellation, script error
  reporting.
- Manual check: intro/cutscene script runs and exits to the next scene.

### M3 — P&C Vertical Slice: One Basic Room

Goal: first playable room with background, walkable area, player avatar,
click-to-move, and one hotspot.

Issues:

1. `RoomScene` orchestrator skeleton with `RoomRuntime`, `RoomLoader`,
   `RoomRenderer`.
2. Room YAML loader: size, background layers, walkable polygon, points, hotspots.
3. Cast loader: one animated-sprite player appearance, speech color/name.
4. Persistent player avatar: create once for `RoomScene`, place from
   `start_room`, move by click.
5. Basic movement: straight-line walk that stops/refuses if destination or
   segment is not walkable.
6. Hotspot hit testing: `area`, `bind`, or both; approach point; `look_at`
   handler call.
7. Speech manager: text near speaker, duration from text length, skippable.

Acceptance:

- Sample game enters one room, renders background, places player, walks by click,
  runs `look_at` on a hotspot.
- Tests cover room YAML validation, player placement order, hotspot hit rules,
  walkable checks.
- Manual check: author can change the hotspot text in Lua without recompiling.

### M4 — P&C Core Gameplay: 3-Room Prototype

Goal: use the 3-room sample as the main manual regression target.

Issues:

1. Room transitions: zones, `change_room(id, entry_point?)`, room
   `on_load`/`on_unload`, autosave trigger.
2. Camera: world/virtual/window transforms, clamped dead-zone follow,
   `camera_look_at`, `camera_go_to`, `camera_follow_player`.
3. Objects and regions: background layers, region states, object visibility,
   z-order via `ZDrawable`.
4. Inventory model: `inventory.yaml`, `inventory.lua`, text inventory display,
   `add_item`, `remove_item`, `has_item`, `list_items`.
5. Command model and dispatcher: verbs, affordances, default verb,
   one/two-operand dispatch, inventory-first routing.
6. Basic SCUMM panel: verbs, command bar, inventory text items, click targets.

Acceptance:

- 3-room sample supports walking between rooms, opening/changing a region,
  picking/using an inventory item, and preserving state across room changes.
- Tests cover command-builder transitions, inventory dispatch precedence, z-sort
  ordering, region state persistence.
- Manual checklist covers all verbs at least once with fallback behavior.

### M5 — Dialog, NPCs, Save/Load

Goal: complete the classic adventure loop.

Issues:

1. NPC avatars: room-scoped avatar creation, hotspot binding to NPCs, `talk_to`.
2. Dialog runtime: `dialogs/<id>.lua`, node traversal, options, `when`, `run`,
   `once`, `silent`, `END`.
3. Dialog UI state: room view switches between `Command`, `Dialog`, and
   `Blocked`.
4. Full `GameState`: current scene/room, player position/facing/appearance,
   inventory, global state, room state, region states, dialog flags.
5. Save service: YAML serialization, 3 manual slots, 1 autosave slot,
   latest-save lookup for Continue.
6. Title/Settings/Continue: `SettingsScene` from manifest; Continue loads latest
   save; save/load menu from gameplay.

Acceptance:

- Sample has one NPC dialog with a once-only option and a state-changing option.
- Save, quit, continue restores room, player position, inventory, region state,
  and consumed dialog option.
- Tests cover dialog traversal, once flags, `GameState` round trip, save slot
  selection.

### M6 — MVP Hardening And Debug Tools

Goal: make the playable MVP maintainable and author-friendly.

Issues:

1. Validation pass: all YAML loaders produce clear diagnostics with path/id
   context.
2. Debug overlays: walkable, obstacles, hotspots, approach points, avatar
   anchors, z values, command-builder state.
3. Dev actions: room jump, room script reload, add/remove inventory item.
4. Templates: `game.yaml`, strings file, cast, room YAML/Lua, inventory YAML/Lua,
   dialog Lua.
5. Packaging smoke path: Ubuntu dev run and Windows/vcpkg build path documented.
6. Manual regression script: one checklist for the 3-room sample covering
   startup, movement, commands, dialog, save/load, settings.

Acceptance:

- A new minimal game can be created from templates and launched without editing
  C++.
- All headless tests pass through CTest.
- Manual MVP checklist passes on the 3-room sample.

## Public Interfaces And Data Contracts

- Manifest uses `resources.src`, `strings`, scene list, and built-in scene types:
  `TitleScreen`, `SettingsScene`, `StoryText`, `RoomScene`.
- `RoomScene` parameters include `cast`, `logic`, `inventory`,
  `inventory_logic`, `rooms`, `start_room`, `player`, optional `font`.
- Static data stays in YAML: manifest, strings, cast, rooms, spritesheets,
  animations, inventory.
- Behavior stays in Lua: game logic, room hooks/handlers, inventory handlers,
  dialog nodes.
- Lua API MVP includes flow/coroutines, speech/dialog, room/camera, avatar handle
  subset, state, scenery, inventory, audio, input blocking, and resource
  validation.
- `CompositeSprite`, packed archives, voice-over playback, runtime
  multi-language selector, thumbnails, and full grid A* are deferred unless
  needed after the playable MVP.

## Test Plan

- Unit tests: manifest/data loaders, geometry, animation timing, command
  builder, dialog runtime, state stores, inventory model, save serialization.
- Integration tests: Lua scheduler/task scopes, room loading, command dispatch,
  dialog `once`, save/load round trip.
- Manual scenarios: blank app, animated sprite sample, one-room P&C slice,
  3-room sample, full MVP checklist.
- Build checks: Linux debug build with CTest every milestone; Windows/vcpkg
  smoke build at M0, M3, and M6.

## Assumptions

- Greenfield implementation: current prototype code is reference only.
- Existing prototype assets and 3-room game concept may be reused as sample
  content.
- F3 is accepted: manifest-declared `SettingsScene`, engine-owned Continue.
- MVP pathfinding is straight-line with obstacle/walkability refusal; full
  micropather grid A* can be a post-MVP issue or late M6 stretch.
- Inventory icons remain design-for; MVP inventory renders localized item names
  as text.
