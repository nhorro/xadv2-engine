# Per-act room script template

Scaffolding for a room whose behaviour changes per **act**. Copy these into
`data/rooms/` and rename. Authoring-only — these files are **not** shipped/loaded
(they live under `design_files/`); the real, working example is
`data/rooms/exterior.lua` + `data/rooms/exterior/_act*.lua`.

## How it works
- Each room is shown in one of a few **configurations** keyed by the global state
  `"<room>.cfg"` (an integer; see `data/scripts/game.lua`).
- `data/rooms/_act_flow.lua` (shared, loaded via `include`) dispatches per act:
  it runs the act's `configure(c)` on every entry, then plays `on_first_enter()`
  the first time the player enters that act, or `on_reenter()` on later entries.
- `include(logical)` loads a Lua resource into the shared state and returns its
  value (the sandbox has no `require`; `dofile` would bypass the resource layer).

## Files
- `room.lua` → `data/rooms/<room>.lua` (the entry the engine loads by room id).
- `act.lua`  → `data/rooms/<room>/_act<N>.lua` (one per act/cfg value).

## Rules (same as the single-file convention)
- `configure(c)` = INSTANT only (spawn/despawn, show/hide, obstacles, regions).
  Exhaustive + idempotent: it must fully rebuild presence for act `c`.
- `on_first_enter` / `on_reenter` / transitions = BLOCKING (talk/wait/move_to);
  the flow helper already `spawn()`s the enter beats for you.
