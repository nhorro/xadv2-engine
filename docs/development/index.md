# Development

Documentation for **engine developers** — people extending `xadv2-engine` in C++.

!!! abstract "The as-built tour is the source of truth"
    Start in the [architecture tour](tour/index.md), beginning with
    [design drivers](tour/00-drivers.md). The older
    [design documents](history/design/00-index.md) are
    [archived history](history/README.md).

## Architecture tour

| Doc | Covers |
|-----|--------|
| [Tour index](tour/index.md) | Charter and reading order. |
| [Design drivers](tour/00-drivers.md) | Platform-agnostic games; core 2D + kits; Lua-complete simple games. |
| [1 — Core abstractions](tour/01-core-abstractions.md) | `Game`, `EngineContext`, `Scene`, frame, resources, Lua preview. |
| [Point & click kit](tour/02-point-and-click.md) | Commands, session vs widgets, sequences, pitfalls. |

## Plans and technical debt

Plans are deliberately separated from descriptions of the current code:

- [Target architecture and debt](plans/target-and-debt.md)
- [Proposed code layout and Doxygen](plans/code-layout.md)

## Historical design

Frozen pages from the v2 rewrite. Prefer `docs/authoring/` when field lists disagree.

## Coding guides

- [Conventions & formatting](coding-conventions.md)

Lua and YAML contracts live in the [authoring documentation](../authoring/index.md).
