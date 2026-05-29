# Data formats reference

!!! abstract "Canonical reference"
    The complete, authoritative field reference for every YAML and Lua data file
    lives in the design document
    [06 — Data formats](../development/design/06-data-formats.md). This page is the
    author-facing entry point. When this page and `06` disagree, **`06` wins**.

`Type` notation used throughout `06`: `{a, b}` is a mapping with those keys,
`[T]` is a list of `T`, `polygon` is `[{x, y}, ...]`, `path` is a logical
resource path relative to `resources.src`.

## The files a game is made of

| File | Purpose | Reference |
|------|---------|-----------|
| `game.yaml` (manifest) | Resolution, scenes, resources, `strings`, `languages`, save config. | [06 — Data formats](../development/design/06-data-formats.md) |
| `cast/*.yaml` | Characters: ids, names, spritesheets, default poses. | [06 — Data formats](../development/design/06-data-formats.md) |
| `rooms/<id>.yaml` | Room layout: layers, walkable area, regions, objects, hotspots, zones, avatar starts. | [06 — Data formats](../development/design/06-data-formats.md) |
| `rooms/<id>.lua` | Room behavior: hooks, verb handlers, scripted actions. | [Lua API](lua-api.md) |
| `strings/*` | Engine UI strings, looked up by key (R3). | [06 — Data formats](../development/design/06-data-formats.md) |
| Spritesheet `*.yaml` | Atlas + animation definitions (often tool-generated). | [Spritesheet packer](tools/spritesheet-packer.md) |

## Conventions

- **All engine YAML files carry an optional `version:` int** (default 1), bumped
  when a format changes incompatibly.
- **`id` vs `name`** — ids are ASCII and stable; `name`s are display text.
- **Logical resource paths only** — never platform filesystem paths.
- **Static data here, behavior in [Lua](lua-api.md)** — no geometry, asset paths,
  or hotspot polygons in Lua.

## See also

- [Lua & content authoring guide § The YAML / Lua split](../development/coding-guide/lua-game.md)
- [Lua API reference](lua-api.md)

---

> **TODO (skeleton):** add a minimal copy-pasteable starter for each file
> (manifest, a room, a cast entry) with inline comments, alongside the exhaustive
> reference in `06`.
