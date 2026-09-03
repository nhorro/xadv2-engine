# Data formats reference

This is the current map of the data files the engine loads. Focused pages linked
below contain the maintained examples and field descriptions. The archived v2
design is background only; it is not an API contract.

Type notation: `{a, b}` is a mapping with those keys, `[T]` is a list of `T`,
`polygon` is `[{x, y}, ...]`, and `path` is a logical resource path.

## The files a game is made of

| File | Purpose | Reference |
|------|---------|-----------|
| `game.yaml` / `chapter.yaml` | Resolution, resources, languages, scenes, chapter composition. | [Game and chapter manifests](chapter-manifests.md) |
| `cast.yaml` | Appearances and characters: ids, names, sprites, speech presentation. | [Lua API: avatars](lua-api.md#avatars-and-room-objects) |
| `rooms/<id>.yaml` | Layers, walkable area, regions, objects, hotspots, zones, avatars, lights. | [Scenery authoring](scenery.md) |
| `rooms/<id>.lua` | Room behavior: hooks, verb handlers, scripted actions. | [Lua API](lua-api.md) |
| `scenes/<id>/scene.yaml` | Generic static/animated entities and initial transforms for a `ScriptScene`. | [Scriptable scenes](script-scenes.md) |
| `scenes/<id>/scene.lua` | Generic lifecycle, normalized input, and fixed-step logic. | [Scriptable scenes](script-scenes.md) |
| `dialogs/<id>.lua` | Dialog nodes, options, predicates, and actions. | [Lua API: dialog scripts](lua-api.md#dialog-scripts) |
| `inventory.yaml` / `inventory.lua` | Item presentation/affordances and item verb handlers. | [Lua API: inventory](lua-api.md#inventory-and-global-fallbacks) |
| `closeups/<id>/closeup.yml` / `logic.lua` | Examine image, polygon hotspots, and click behavior. | [Lua API: close-ups](lua-api.md#close-ups) |
| `facts.yaml` | Declared boolean story flags used by the Lua typo guard. | [Lua API: persistent state](lua-api.md#persistent-state) |
| `strings/*` | Engine UI strings, looked up by key. | [Localization](localization.md) |
| `translations/*` | Additional-language game-content catalogs keyed by stable text id. | [Localization](localization.md) |
| Spritesheet `*.yaml` | Atlas + animation definitions (often tool-generated). | [xadv2-tools](https://github.com/nhorro/xadv2-tools) |

## Conventions

- **All engine YAML files carry an optional `version:` int** (default 1), bumped
  when a format changes incompatibly.
- **`id` vs `name`** — ids are ASCII and stable; `name`s are display text.
- **Logical resource paths only** — never platform filesystem paths.
- **Static data here, behavior in [Lua](lua-api.md)** — no geometry, asset paths,
  or hotspot polygons in Lua.

## See also

- [Lua API reference](lua-api.md)
- [Lua API reference](lua-api.md)
- [Localization and native-language voice](localization.md)
