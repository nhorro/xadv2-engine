# Authoring API

For **game authors** — Lua scripts and YAML data. A standard game requires
**zero new C++**: the Lua API, the data formats, and the asset tools.

!!! tip "Static data in YAML, behavior in Lua"
    **YAML** defines *what exists*. **Lua** defines *what happens*. Never put
    geometry, asset paths, or hotspot polygons in Lua.

!!! info "Your game lives in its own repository"
    The engine is a **library**. Start with
    **[Building a game](./building-a-game.md)**.
    The engine repo keeps only `examples/` — small games, one per feature.

This section is the **authoring API**: manifests, Lua, YAML fields, scenery
and lighting parameters, and tools that emit that data. It is not art direction
or narrative craft.

## Where to start

0. **[Building a game](./building-a-game.md)**.
1. **[Lua API reference](lua-api.md)** while you script.
2. **[Data formats reference](data-formats.md)** while you write YAML.
3. **[Game and chapter manifests](chapter-manifests.md)**.
4. **[Scriptable scenes](script-scenes.md)** when the interaction model is not a room.
5. **[Localization and native-language voice](localization.md)**.
6. **[Scenery authoring](scenery.md)** — layers, regions, objects, hotspots, walk-behinds.
7. **[Lighting, shadows, and grading](room-lighting-tutorial.md)** — YAML lights and F9 tuning.
8. **[xadv2-tools](https://github.com/nhorro/xadv2-tools)** — the separate room
   editor, close-up editor, packer, and scaffolder repository.

## Concepts

| Concept | What it is | Reference |
|---------|-----------|-----------|
| Manifest | Composed game-wide and chapter-local configuration. | [Game and chapter manifests](chapter-manifests.md) |
| Scene | A manifest-declared top-level state. | [Lua API](lua-api.md) |
| ScriptScene | YAML entity registry plus Lua input/update. | [Scriptable scenes](script-scenes.md) |
| Room | A place inside the room view, `rooms/<id>.{yaml,lua}`. | [Data formats](data-formats.md) |
| Cast & avatars | Characters that move and speak. | [Lua API](lua-api.md) |
| Hotspots & verbs | Interactive regions and actions. | [Lua API](lua-api.md) |
| Dialog | Branching conversation trees. | [Lua API](lua-api.md) |
| Inventory | Items the player carries and combines. | [Lua API](lua-api.md) |
| State | Persistent facts (`set_state`, room/region stores). | [Lua API](lua-api.md) |

## Conventions

- **`id` vs `name`.** Ids are ASCII and script-friendly. `name`s are display text.
- **Logical resource paths only.** `./` relative to the declaring YAML or `/` relative to `resources.src`.
- **No hardcoded engine UI strings.** Engine chrome uses the `strings` resource. Content strings stay inline; extra languages use [translation catalogs](localization.md).
- **State values are scalars** (bool / number / string).
