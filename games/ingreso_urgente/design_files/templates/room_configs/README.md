# Per-configuration room script template

Scaffolding for a room whose presence/behaviour changes per **configuration**
(`<room>.cfg`). Copy these into `data/rooms/` and rename. Authoring-only — these
files are **not** shipped/loaded (they live under `design_files/`); the real,
working examples are `data/rooms/{lab,exterior,hall}.lua` + their
`data/rooms/<room>/act1/*.lua` config modules.

## Configurations vs acts
- A room is shown in one of several **configurations**, indexed by the global
  integer `"<room>.cfg"` (1, 2, 3, …). See `data/scripts/game.lua`.
- A **story act** doesn't change the mechanism — it just appends more configs to
  that flat index. Group the config files by act in subdirs for readability:
  `rooms/<room>/act1/<role>.lua`, `rooms/<room>/act2/<role>.lua`, …

## How it works
- `data/rooms/_room_flow.lua` (shared, loaded via `include`) dispatches per config:
  it runs the config's `configure(c)` on every entry, then plays
  `on_first_enter()` the first time the player enters that config, or
  `on_reenter()` on later entries.
- `include(logical)` loads a Lua resource into the shared state and returns its
  value (the sandbox has no `require`; `dofile` would bypass the resource layer).

## Files
- `room.lua`   → `data/rooms/<room>.lua` (the entry the engine loads by room id).
- `config.lua` → `data/rooms/<room>/act<N>/<role>.lua` (one per `<room>.cfg` value).

## Rules
- `configure(c)` = INSTANT only (spawn/despawn, show/hide, obstacles, regions).
  Exhaustive + idempotent: it must fully rebuild presence for config `c`.
- `on_first_enter` / `on_reenter` / transitions = BLOCKING (talk/wait/move_to);
  the flow helper already `spawn()`s the enter beats for you.
- Hotspots can stay centralized in `<room>.lua` (branching on state) — only the
  per-config *setup* needs to move out to keep the entry short.
