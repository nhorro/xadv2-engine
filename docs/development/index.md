# Development

Documentation for **engine developers** — people extending `xadv2-engine` in C++.

!!! abstract "The as-built tour is the source of truth"
    Start in the [architecture tour](tour/index.md). Those pages describe the
    engine as it exists and the target split (core 2D + P&C kit). The older
    [design documents](design/00-index.md) are [archived history](history/README.md):
    useful context, not a map for new work.

## Architecture tour

Read in this order. Do not start in `RoomScene`.

| Doc | Covers |
|-----|--------|
| [Tour index](tour/index.md) | How to teach the codebase; three-day onboarding. |
| [1 — Core abstractions](tour/01-core-abstractions.md) | Layers, `Game`, `EngineContext`, `Scene`, frame, resources, Lua preview. |
| [Point & click kit](tour/07-point-and-click.md) | Commands, view vs builder, processor, SCUMM widgets, sequences, pitfalls. |
| [Target architecture and debt](tour/target-and-debt.md) | Destination model, current gaps, easy fixes, promotions, branching rule. |

Further tour chapters (Lua in depth, draw pipeline, lighting) will land here
as they are written.

## Historical design

Frozen pages from the v2 rewrite. Glossary and data-format detail still help;
requirements R3/R7 and the 85/15 panel layout do not match the tree.

| Doc | Covers |
|-----|--------|
| [00 — Index](design/00-index.md) | Glossary, original reading order. |
| [01 — Engine requirements](design/01-engine-requirements.md) | R1–R8 as originally tagged. |
| [02 — Architecture overview](design/02-architecture-overview.md) | Layers, Lua bridge, runtime spine. |
| [03 — Generic 2D concepts](design/03-2d-game-concepts.md) | Loop, sprites, resources. |
| [04 — Point & click concepts](design/04-point-and-click-concepts.md) | Rooms, hotspots, z-order, dialog. |
| [05 — Scripting API](design/05-scripting-api.md) | Lua surface as specified. |
| [06 — Data formats](design/06-data-formats.md) | YAML field reference (prefer `docs/authoring/` when they disagree). |
| [Implementation plan](design/implementation-plan.md) | Milestone roadmap that produced the current tree. |

## Coding guides

- [Conventions & formatting](coding-conventions.md)
- [C++ engine guide](coding-guide/cpp-engine.md)
- [Lua & content guide](coding-guide/lua-game.md)

## Architecture in one diagram

```text
Game           Lua/YAML/assets + optional C++ scenes
Point & Click  rooms, commands, dialog, SCUMM composers     pac::pnc
Generic 2D     sprites, ScriptScene, shader chain           pac::gfx
Core           loop, Scene, resources, Lua, audio, state    pac::core
               polygons / pathfinding                       pac::geom
```

A layer may depend only downward. Point-and-click should *use* core 2D
capabilities (layered sprites, camera, persistence, audio). Where the room
view still owns a private compositor or persist maps, that is debt — see
[target architecture](tour/target-and-debt.md).
