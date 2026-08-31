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

    The engine repo keeps only `examples/` — small games, one per feature,
    written to be read.

## Where to start

0. **Set up your game repo: [Building a game](./building-a-game.md)**.
1. **Read the [Lua & content authoring guide](../development/coding-guide/lua-game.md)** —
   the rules and patterns for writing content (the YAML/Lua split, state, scopes,
   common pitfalls).
2. **Keep the [Lua API reference](lua-api.md)** open while you script.
3. **Keep the [Data formats reference](data-formats.md)** open while you write YAML.
4. **Use [Game and chapter manifests](chapter-manifests.md)** to keep game-wide
   presentation separate from chapter-owned scenes and content.
5. **Use [Scriptable scenes](script-scenes.md)** for a custom interaction model
   that can stay in YAML + Lua instead of becoming a C++ scene.
6. **Use [Localization and native-language voice](localization.md)** when adding
   subtitle languages or recorded dialogue.
7. **Follow the [Scenery authoring guide](scenery.md)** — recipes for building room
   contents: layers, regions, objects (static & animated), NPCs, hotspots,
   obstacles, walk-behinds, and perspective.
8. **Take a room from playable to cinematic with the
   [lighting, shadows, and grading tutorial](room-lighting-tutorial.md)** — it
   explains the rendering pipeline, editor primitives, YAML parameters, live F9
   tuning, and practical recipes.
9. **Use the [tools](tools/index.md)** to prepare backgrounds, spritesheets, and
   rooms.

## Concepts you need

| Concept | What it is | Reference |
|---------|-----------|-----------|
| Manifest | Composed game-wide and chapter-local configuration. | [Game and chapter manifests](chapter-manifests.md) |
| Scene | A manifest-declared top-level state (title, cutscene, room view, settings). | [Lua API](lua-api.md) |
| ScriptScene | A generic YAML entity registry plus Lua input/update logic. | [Scriptable scenes](script-scenes.md) |
| Room | A place inside the room view, loaded by id from `rooms/<id>.{yaml,lua}`. | [Data formats](data-formats.md) |
| Cast & avatars | Characters; the player and NPCs that move and speak. | [Lua API](lua-api.md) |
| Hotspots & verbs | Interactive regions and the actions (verbs) that apply to them. | [Lua API](lua-api.md) |
| Dialog | Branching conversation trees. | [Lua API](lua-api.md) |
| Inventory | Items the player carries and combines. | [Lua API](lua-api.md) |
| State | Persistent facts saved across sessions (`set_state`, room/region stores). | [Lua API](lua-api.md) |

## Conventions that matter for authors

- **`id` vs `name`.** Ids are ASCII, stable, script-friendly. `name`s are display
  text and may contain spaces, accents, and localized strings. Keep them separate.
- **Logical resource paths only.** In composed manifests, use `./` relative to the
  declaring YAML or `/` relative to `resources.src`. Never hardcode host paths.
- **No hardcoded user-facing engine strings.** Engine UI text is looked up by key
  in the manifest `strings` resource. Source-language *content* strings stay
  inline; additional languages use [translation catalogs](localization.md).
- **State values are scalars** (bool / number / string) for the MVP — no tables.

!!! note
    This section is a skeleton. The pages below currently frame and link the
    canonical references in the [design docs](../development/design/00-index.md); they
    will grow into standalone, task-oriented author guides over time.
