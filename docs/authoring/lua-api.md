# Lua API reference

!!! abstract "Canonical reference"
    The complete, authoritative Lua API surface lives in the design document
    [05 — Scripting API](../development/design/05-scripting-api.md). This page is the
    author-facing entry point: it frames the API and points you at the right part.
    When this page and `05` disagree, **`05` wins**.

A game is **static data plus behavior**:

```text
YAML = structure, assets, geometry, appearances, affordances
Lua  = behavior, rules, dialog branching, scripted actions
```

Lua runs as **cooperative coroutines**. Blocking-looking calls such as `wait`,
`talk`, `avatar(id):move_to`, and `avatar(id):play_until_end` yield to the engine
loop instead of freezing the frame.

## API conventions

- The Lua API is a set of **flat `snake_case` globals**.
- **State keys use dotted names** (e.g. `mummy.awake`).
- Handles like `avatar(id)` store only a **stable id**, never a raw C++ pointer —
  the engine re-resolves the id on each call.
- **State values are scalars** (bool / number / string) for the MVP.

## API areas

Jump to the matching section of [05 — Scripting API](../development/design/05-scripting-api.md):

- **Lifecycle hooks** — `on_load`, `on_unload`, per-room/scene entry points.
- **Coroutine scheduler** — `spawn`, `wait`, `emit`, `wait_event`, script scopes
  and cancellation.
- **State** — `get_state` / `set_state`, `set_room_state`, region and dialog stores.
- **Avatars** — `avatar(id):move_to`, `:say`, `:play_until_end`, facing, approach.
- **Speech** — captions and timed `talk`.
- **Rooms & navigation** — `change_room`, zones, camera.
- **Hotspots & verbs** — default-verb handlers and verb dispatch.
- **Inventory** — add/remove/query items and combinations.
- **Dialog** — `start_dialog`, tree options, `on_exit`, `once` flags.
- **Audio** — music and SFX playback.
- **Resources** — `resource_path` and logical asset paths.

!!! warning "Persistence"
    Lua locals, closures, and running coroutines are **transient** and are never
    saved. Persist facts only through `set_state` / `set_room_state` / inventory /
    region / dialog stores. See
    [02 § Make persistent state explicit](../development/design/02-architecture-overview.md).

## See also

- [Lua & content authoring guide](../development/coding-guide/lua-game.md) — patterns and rules.
- [Data formats](data-formats.md) — the YAML side of each subsystem.

---

> **TODO (skeleton):** expand into a quick-reference with one short, runnable
> snippet per API call, grouped by area, generated/cross-checked against `05`.
