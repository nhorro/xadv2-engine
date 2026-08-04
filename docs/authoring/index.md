# Content Creators

For **game authors** — you write the **Lua** scripts and **YAML** data that make
up a game. A standard game requires **zero new C++**: everything you need is the
Lua API, the data formats, and the asset tools.

!!! tip "The golden rule: static data in YAML, behavior in Lua"
    **YAML** defines *what exists* — the manifest, cast, room layout, geometry,
    hotspots, affordances, layers, regions, objects, avatar start positions,
    inventory. **Lua** defines *what happens* — lifecycle hooks, verb handlers,
    dialog branching, scripted actions. Never put geometry, asset paths, or
    hotspot polygons in Lua.

!!! info "Your game lives in its own repository"
    The engine is a **library**. A game is a standalone project that links it —
    it does not live inside the engine repo. Start with
    **[Building a game](./building-a-game.md)**: scaffold, build (against an engine
    checkout or an installed engine), author, ship.

    The engine repo keeps only `examples/` — six tiny games, one per feature,
    written to be read.

## Where to start

0. **Set up your game repo: [Building a game](./building-a-game.md)**.
1. **Read the [Lua & content authoring guide](../development/coding-guide/lua-game.md)** —
   the rules and patterns for writing content (the YAML/Lua split, state, scopes,
   common pitfalls).
2. **Keep the [Lua API reference](lua-api.md)** open while you script.
3. **Keep the [Data formats reference](data-formats.md)** open while you write YAML.
4. **Follow the [Scenery authoring guide](scenery.md)** — recipes for building room
   contents: layers, regions, objects (static & animated), NPCs, hotspots,
   obstacles, walk-behinds, and perspective.
5. **Take a room from playable to cinematic with the
   [lighting, shadows, and grading tutorial](room-lighting-tutorial.md)** — it
   explains the rendering pipeline, editor primitives, YAML parameters, live F9
   tuning, and practical recipes.
6. **Use the [tools](tools/index.md)** to prepare backgrounds, spritesheets, and
   rooms.

## Concepts you need

| Concept | What it is | Reference |
|---------|-----------|-----------|
| Manifest | The game's top-level config: resolution, scenes, strings, languages. | [Data formats](data-formats.md) |
| Scene | A manifest-declared top-level state (title, cutscene, room view, settings). | [Lua API](lua-api.md) |
| Room | A place inside the room view, loaded by id from `rooms/<id>.{yaml,lua}`. | [Data formats](data-formats.md) |
| Cast & avatars | Characters; the player and NPCs that move and speak. | [Lua API](lua-api.md) |
| Hotspots & verbs | Interactive regions and the actions (verbs) that apply to them. | [Lua API](lua-api.md) |
| Dialog | Branching conversation trees. | [Lua API](lua-api.md) |
| Inventory | Items the player carries and combines. | [Lua API](lua-api.md) |
| State | Persistent facts saved across sessions (`set_state`, room/region stores). | [Lua API](lua-api.md) |

## Conventions that matter for authors

- **`id` vs `name`.** Ids are ASCII, stable, script-friendly. `name`s are display
  text and may contain spaces, accents, and localized strings. Keep them separate.
- **Logical resource paths only.** Reference assets by logical path relative to
  `resources.src` (e.g. `backgrounds/study.png`). Never hardcode filesystem paths.
- **No hardcoded user-facing engine strings.** Engine UI text is looked up by key
  in the manifest `strings` resource. Your *content* strings (names, speech,
  dialog lines) stay inline in your data files.
- **State values are scalars** (bool / number / string) for the MVP — no tables.

!!! note
    This section is a skeleton. The pages below currently frame and link the
    canonical references in the [design docs](../development/design/00-index.md); they
    will grow into standalone, task-oriented author guides over time.
