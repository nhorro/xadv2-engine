# Coding conventions

Conventions for the C++ engine code. They follow the style the
[design docs](sources/design/) already use in their interface listings (the
`Scene` contract, `SceneManager`, `EngineContext`, and the avatar interface), so
the implementation reads the same as the design. Formatting is enforced by
[`.clang-format`](../.clang-format); naming is a convention this document defines.

## Formatting (clang-format)

[`.clang-format`](../.clang-format) is the source of truth for whitespace, braces,
and include ordering. Highlights: LLVM base, **4-space indent**, **100-column
limit**, pointers/references bound left (`const std::string& id`), attached braces,
one parameter/argument per line when wrapping, namespace-closing comments.

Install and run (Ubuntu 24.04 ships clang-format 18):

```bash
sudo apt install clang-format

# format everything we own (never third_party/ — see below)
find lib games tests experiments -type f \( -name '*.hpp' -o -name '*.cpp' \) \
  -not -path 'third_party/*' -print0 | xargs -0 clang-format -i

# check only (CI): non-zero exit on any diff
clang-format --dry-run --Werror path/to/file.cpp
```

Vendored code under `third_party/` (e.g. micropather) is **not** reformatted. When
that directory is added, drop a `third_party/.clang-format` containing
`DisableFormat: true` so editor-on-save and recursive runs leave it untouched.

## Naming

clang-format does not check naming; these rules are enforced by review (and later,
optionally, clang-tidy).

| Kind | Style | Examples |
|------|-------|----------|
| Types — class / struct / enum / alias | `PascalCase` | `EngineContext`, `SceneManager`, `RoomRuntime`, `AnimatedSprite`, `ZDrawable`, `Command`, `Verb` |
| Functions and methods | `snake_case` | `goto_scene`, `handle_event`, `find_path`, `point_in_polygon`, `play_until_end` |
| Namespaces | lowercase, rooted at `pac` | `pac::core`, `pac::geom`, `pac::gfx`, `pac::pnc` |
| Member variables | `snake_case_` (trailing underscore) | `stack_`, `current_room_`, `dt_accumulator_` |
| Locals and parameters | `snake_case` | `entry_point`, `target`, `dt` |
| Enum values | `UPPER_SNAKE_CASE` | `IDLE`, `VERB_SELECTED`, `EXPECTING_PARAM1_ROOM_OBJECT`, `COMMAND_READY` |
| Compile-time constants (`constexpr`) | `kPascalCase` | `kFixedTimestep`, `kSceneryViewportRatio` |
| Macros (avoid; SFML/platform shims only) | `UPPER_SNAKE_CASE` | `PAC_ASSERT` |

The enum-value and namespace styles are taken directly from the design docs (the
command-builder state machine in
[04](sources/design/04-point-and-click-concepts.md) and the layer table in
[02](sources/design/02-architecture-overview.md)).

The Lua-facing API is a separate surface with its own rules — flat `snake_case`
globals and dotted state keys (`mummy.awake`); see
[05 — Scripting API](sources/design/05-scripting-api.md). C++ method names that the
bridge exposes (e.g. `move_to`, `play_until_end`) are already `snake_case`, so the
two surfaces match.

## Files and headers

- Header extension `.hpp`, source extension `.cpp`.
- File names are `snake_case`, matching the namespace directories:
  `lib/include/engine/core/scene_manager.hpp`, `lib/src/core/scene_manager.cpp`.
- One primary type per header where practical; the file name reflects that type
  (`SceneManager` → `scene_manager.hpp`).
- Use `#pragma once`, not include guards.
- Public engine headers live under `lib/include/engine/{core,geom,gfx,pnc}/` and
  are included as `#include "engine/core/scene_manager.hpp"`. The directory mirrors
  the namespace and the layer (see [the dependency rule](../CLAUDE.md)).

## C++ usage

- Target **C++17**; do not use later-standard features.
- Prefer `enum class` over plain `enum`.
- West const (`const T&`, not `T const&`), matching `PointerAlignment: Left`.
- Prefer `#include` of what you use; rely on the include grouping in
  `.clang-format` rather than hand-ordering.
- Keep the [layer dependency rule](../CLAUDE.md) intact — it is part of the design,
  not a style preference.

## Scope

These conventions cover engine C++. YAML/Lua data-file conventions (ids vs names,
logical resource paths, `version:` fields, scalar state values) are defined in
[06 — Data formats](sources/design/06-data-formats.md) and summarized in
[CLAUDE.md](../CLAUDE.md).
