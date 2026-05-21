# Third Person Point & Click Game Engine — Design

Design documentation for the **Extraordinary Adventures Engine**, a C++20 / SFML
engine for third-person SCUMM-style point-and-click adventure games, scripted in
Lua and configured with YAML.

> **These documents are the source of truth for the engine.** The implementation
> follows the design; where the code diverges, the code is what changes.

## Start here

**[00 — Index](00-index.md)** — reading order, architecture diagram, glossary,
requirement traceability, and conventions. Read it first, then `01`–`06` in
order.

The design proper is [`01`](01-engine-requirements.md) through
[`05`](05-scripting-api.md); the per-file field reference is
[`06 — Data formats`](06-data-formats.md) (in progress).

## Working documents

These track work in progress and are **not** canonical design:

- [migration-notes.md](migration-notes.md) — gaps between the prototype and the
  target design.
- [design-review-proposals.md](design-review-proposals.md) — decision record for
  the design review: resolved blocking decisions and the remaining smaller gaps.
