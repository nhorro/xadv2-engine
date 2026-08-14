# 06 — A custom scene, in C++

**Shows:** the escape hatch. Every other example is data only; this one adds a
screen the engine does not ship — a field-notes journal — as the game's own C++.

```bash
./build/examples/06_cpp_scene/pac_example_06_cpp_scene
```

## Play it

`Look at` the desk, `Open` it, `Look at` the board. Each discovers a note. Press
**[esc]** → **Field notes** to read what you have found; the third discovery opens
it for you.

## Read, in this order

| File | What it teaches |
|---|---|
| `src/field_notes.hpp` | The whole contract, in three pieces: a `pac::core::Scene` subclass, a type registered with the `SceneFactory`, and Lua bindings installed via `ApplicationHooks::configure`. |
| `src/field_notes.cpp` | The scene draws itself with SFML; `configure()` parses the game's own YAML and adds `discover_note` / `has_note` / `open_notes` to the Lua state via sol2. |
| `src/game.cpp` | Builds the `SceneFactory` by hand—engine built-ins **plus one of ours**—and exposes the same composition for filesystem and resource-backed platforms. |
| `main.cpp` | Parses the desktop command line and invokes that shared game composition. |
| `android/bootstrap.cpp` | Adapts Android's packaged-resource source to the same composition without adding Android branches to the scene or Lua code. |
| `data/game.yaml` | `type: FieldNotes` — once registered, a custom scene is indistinguishable from a built-in one. The `pause_menu.overlays` block puts it on the Esc menu. |
| `data/rooms/office.lua` | Calls `discover_note("desk")`. From Lua, a game function looks exactly like an engine one. |

## The two ideas worth taking away

**Persistent state still belongs to the engine.** A note's "found" flag lives in
the `StateStore` under `notes.<id>`, not in a C++ member — so it lands in the save
file. A `bool found` on the `Note` struct would be silently lost on reload.

**Only the linking changes.** This example links `pac_sol2` (to add Lua functions)
and `yaml-cpp` (to read its own data file). A game with no custom C++ links
neither — just the engine. Reach for this pattern only when the *interaction
model itself* is new; a room, a dialog, a close-up or a cutscene is data.
