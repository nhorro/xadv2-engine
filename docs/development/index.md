# Development

Documentation for **engine developers** — the people extending `xadv2-engine`
in C++. The goal of this section is to give you enough context to make
decisions that keep the engine powerful and keep content authoring easy.

!!! abstract "The design docs are the source of truth"
    The canonical design lives in the [design documents](design/00-index.md).
    The implementation follows the design; where the code diverges, **the code is
    what changes**. Read [00 — Index](design/00-index.md) first.

## Design documents

The architecture, requirements, and concept references. Read in order.

| Doc | Covers |
|-----|--------|
| [00 — Index](design/00-index.md) | Reading order, glossary, requirement traceability, conventions. |
| [01 — Engine requirements](design/01-engine-requirements.md) | Requirements R1–R8, MVP / design-for / constraint scope, out-of-scope. |
| [02 — Architecture overview](design/02-architecture-overview.md) | Layers + dependency rule, Lua bridge, runtime spine, `EngineContext`, coordinate spaces, `GameState`. |
| [03 — Generic 2D concepts](design/03-2d-game-concepts.md) | Main loop, scene contract, resources, spritesheets/sprites, geometry, audio. |
| [04 — Point & click concepts](design/04-point-and-click-concepts.md) | Rooms, layers/regions, camera, z-order, hotspots, avatars, SCUMM panel, dialog, inventory. |
| [05 — Scripting API](design/05-scripting-api.md) | The full Lua API surface, game wiring, error handling. |
| [06 — Data formats](design/06-data-formats.md) | Exhaustive field reference for every YAML / Lua data file. |
| [Implementation plan](design/implementation-plan.md) | Milestone roadmap (M0–M7) toward the playable MVP. |

## Coding guides

How to actually write the code, once you know what to build.

- [Conventions & formatting](coding-conventions.md) — clang-format machinery,
  install, run, CI check.
- [C++ engine guide](coding-guide/cpp-engine.md) — naming, headers, ownership,
  error handling, C++ usage rules. The implementation source of truth.
- [Lua & content guide](coding-guide/lua-game.md) — for the YAML/Lua split and
  the patterns authors follow (also linked from Content Creators).

## Architecture in one diagram

```text
Game           Lua scripts + assets + game manifest          (no C++)
Point & Click  room view, title, cutscenes, rooms,           pac::pnc
               hotspots, avatars, SCUMM panel, dialog
Generic 2D     spritesheets, animated/composite sprites,     pac::gfx
               shader effects (ShaderEffect/params)
Core           window, loop, input, resources, audio,        pac::core
               settings, Scene, SceneManager, geometry       pac::geom
```

**A layer may depend only on the layers below it, never above.** See
[02 — Architecture overview](design/02-architecture-overview.md) for
the full dependency rule.
