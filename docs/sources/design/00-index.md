# Engine design — index

The **Extraordinary Adventures Engine** is a C++17 / SFML engine for
third-person SCUMM-style point-and-click adventure games, scripted in Lua and
configured with YAML. These documents are the single source of truth for the
engine; the implementation follows them, not the other way around.

## Reading order

| Doc | Covers |
|-----|--------|
| [01 — Engine requirements](01-engine-requirements.md) | Scope, requirements R1–R8, implementation/platform constraints, out-of-scope list. |
| [02 — Architecture overview](02-architecture-overview.md) | Layers and dependency rule, Lua bridge (sol2), runtime spine, `EngineContext`, scene types, manifest, coordinate spaces + camera, persistent state / `GameState`, implementation guidelines. |
| [03 — 2D game concepts](03-2d-game-concepts.md) | Generic 2D layer: main loop, scene contract, resources, spritesheets / animated / composite sprites, geometry + pathfinding, settings, audio. |
| [04 — Point & click concepts](04-point-and-click-concepts.md) | Genre layer: rooms, background layers / regions, camera, z-order, hotspots / affordances, avatars, commands + SCUMM panel, dialog, speech, inventory. |
| [05 — Scripting API](05-scripting-api.md) | The Lua surface: game wiring, cast, room / dialog scripts, full API reference, error handling. |
| [06 — Data formats](06-data-formats.md) | Complete field reference for every YAML / Lua data file *(in progress)*. |

Working documents (not canonical design): [migration notes](migration-notes.md)
and the [design review / decision record](design-review-proposals.md).

## Architecture at a glance

```text
┌─────────────────────────────────────────────────────────────┐
│  Game            Lua scripts + assets + game manifest         │  no C++
├─────────────────────────────────────────────────────────────┤
│  Point & Click   room view, title, cutscenes, rooms,          │  pac::pnc
│                  hotspots, avatars, SCUMM panel, dialog       │
├─────────────────────────────────────────────────────────────┤
│  Generic 2D      spritesheets, animated sprites,              │  pac::gfx
│                  composite sprites                            │
├─────────────────────────────────────────────────────────────┤
│  Core            window, loop, input, resources, audio,       │  pac::core
│                  settings, Scene, SceneManager, geometry       │  pac::geom
└─────────────────────────────────────────────────────────────┘
        Lua scripting bridge exposes a controlled API to scripts
```

Dependency rule: a layer may depend only on the layers below it, never above.
Game scripts use only the Lua API. See
[Architecture overview](02-architecture-overview.md) for the full rule and
folder layout.

## Requirement traceability

Where each requirement from [01](01-engine-requirements.md) is specified.

| Req | Summary | Specified in |
|-----|---------|--------------|
| R1 | Classic SCUMM experience | `04` (genre scenes, commands + SCUMM panel, dialog, inventory); `05` (room scripts). |
| R2 | Scripting-only authoring | `02` (keep static data out of Lua, scripting bridge); `03` (Lua integration); `05` (whole document); `06` (data formats). |
| R3 | Localization-ready, Spanish first | `04` (hotspot `id` vs `name`, speech, localized connectors); `05` (cast names vs ids); `03` (settings language design-for). |
| R4 | Voice-over ready | `04` (speech stable line id); `05` (`talk` line-id note). |
| R5 | Languages, frameworks, tools | `01` R5; `02` (binding mechanism / sol2). |
| R6 | Fixed virtual resolution + scaling | `02` (coordinate systems and display pipeline); `01` R6. |
| R7 | Operating systems | `01` R7; `02` (build layout, platform handling). |
| R8 | Save / load | `01` R8; `02` (persistent state / `GameState`); `05` (state API). |

## Glossary

**Coordinate spaces** — *World* is the room-sized space for geometry, hotspots,
objects, and avatars; *Virtual* is the fixed design resolution where UI, speech,
and dialog draw; *Window* is the physical OS output. Chain:
`world → camera → virtual → letterbox → window`. The camera maps world into the
*scenery viewport* — the top region of virtual space (~85%); the SCUMM panel
occupies the region beneath it, not an overlay.

**Scene** — a top-level application state listed in the manifest (e.g.
`TitleScreen`, `StoryText`, `RoomScene`), switched via the `SceneManager`.

**Room** — a place inside the `RoomScene`, loaded by id from `rooms/<id>.{yaml,lua}`
and switched with `change_room`. Rooms are not manifest entries.

**Character** — a cast entry: a stable `id`, display `name`, speech style, and an
`appearance`. Lives in `cast.yaml`.

**Appearance** — a typed visual definition (`animated_sprite`, `composite`, or
future `skeletal`) bound to a character; supplies the drawing strategy's
parameters plus genre extras like `shadow`.

**Avatar** — the in-room visual and spatial instance of a character. The player
avatar is a persistent world entity; NPC avatars are room-scoped.

**AnimatedSprite / CompositeSprite** — the generic-2D (`pac::gfx`) drawing
strategies an appearance can use: a single animated sprite, or a hierarchy of
animated sprites attached through anchors.

**Anchor / pivot** — a named frame-local point used for attachment; the *pivot*
is the anchor whose world position the sprite's position sets.

**Background layer** — one image (or color) in a room's layered background, drawn
at an explicit `z`. Distinct from an architecture *layer*.

**Region** — a changeable part of the background with named states (e.g.
drawer `shut`/`open`), swapped with `set_region_state`.

**Object** — an active sprite placed in the room (visual only); interactivity is
added by binding a hotspot to it.

**Hotspot** — an interactive area with a hit polygon or a `bind` to an object /
region, an `approach` point, `affordances`, and Lua verb handlers.

**Affordance** — a verb the UI may offer for a hotspot or item. Affordances gate
UI selection; the Lua handler decides the outcome.

**Verb / Command** — the player builds a *command* `verb param1 param2?`; the
*command model* is language-independent and formatted for display by the UI.

**Point / Zone / Walkable / Obstacle** — a *point* is a named coordinate; a
*zone* is a trigger polygon (room exits / scripted events); the *walkable*
polygon is the room's navigable area; *obstacles* are non-walkable polygons
inside it.

**ZDrawable** — anything drawn in the scenery; the room view stable-sorts
ZDrawables by ascending `z` each frame.

**State** — *global state* (`set_state`, cross-room facts), *room state*
(`set_room_state`, current room only), and *region state*. Values are scalars
(bool / number / string) for the MVP.

**GameState** — the single engine-owned object that is the entire save payload
(see [02](02-architecture-overview.md)).

**EngineContext** — the struct of borrowed, app-owned service references passed
to scenes (display, resources, audio, settings, scripting, scenes, log, dev).

**Manifest** — `game.yaml`: engine config, scene list, and entry scene.

## Conventions

- All engine YAML data files carry an optional integer `version:` (default 1),
  bumped when a format changes incompatibly.
- Internal `id`s are ASCII, stable, and script-friendly; display `name`s may
  contain spaces, accents, and localized text.
- Resource references are logical paths relative to `resources.src`, never
  platform filesystem paths.
- The Lua API uses `snake_case`; state keys use dotted names (`mummy.awake`).
- Engine-emitted UI strings (verb labels, connectors, menu labels) are looked up
  by key in the strings resource; game-content strings (`name`s, speech, dialog)
  stay inline. No user-facing string is hardcoded in C++ (R3).
