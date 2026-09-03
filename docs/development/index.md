# Development

Documentation for **engine developers** — people extending `xadv2-engine` in C++.

!!! abstract "The as-built tour is the source of truth"
    Start in the [architecture tour](tour/index.md), beginning with
    [design drivers](tour/00-drivers.md). The older
    [design documents](design/00-index.md) are [archived history](history/README.md).

## Architecture tour

| Doc | Covers |
|-----|--------|
| [Tour index](tour/index.md) | Charter and reading order. |
| [Design drivers](tour/00-drivers.md) | Platform-agnostic games; core 2D + kits; Lua-complete simple games. |
| [1 — Core abstractions](tour/01-core-abstractions.md) | `Game`, `EngineContext`, `Scene`, frame, resources, Lua preview. |
| [Point & click kit](tour/07-point-and-click.md) | Commands, session vs widgets, sequences, pitfalls. |
| [Target architecture and debt](tour/target-and-debt.md) | Gaps vs drivers; extract order; no long experimental rewrite. |
| [Code layout and Doxygen](tour/code-layout.md) | Folder subdivisions; Doxygen as API index; Mermaid vs PlantUML. |

Further chapters (Lua in depth, draw pipeline, lighting) will land here as written.

## Historical design

Frozen pages from the v2 rewrite. Prefer `docs/authoring/` when field lists disagree.

## Coding guides

- [Conventions & formatting](coding-conventions.md)
- [C++ engine guide](coding-guide/cpp-engine.md)
- [Lua & content guide](coding-guide/lua-game.md)
