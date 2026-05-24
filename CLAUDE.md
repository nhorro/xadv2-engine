# CLAUDE.md

Guidance for working in this repository. Keep it current as the engine takes shape.

## What this is

`xadv2-engine` is a ground-up rebuild of the **Extraordinary Adventures Engine**: a
C++17 / SFML engine for third-person, SCUMM-style point-and-click adventure games,
scripted in Lua and configured with YAML. It is a remake of
[ea-engine](https://github.com/nhorro/ea-engine); the old prototype is reference
material only (reusable assets, behavior ideas, comparison targets) — never
architecture to preserve.

**Status: M0–M5 are merged and fully closed. M6 (hardening) and M7 (presentation
& authoring polish) are largely merged — latest in: settings persistence (#66,
M6), display-mode settings (#71, M7), and localization infrastructure (#72, M7),
merged via PR #94. Still open: M6 authoring templates (#39), packaging smoke path
(#40), manual MVP regression checklist (#41), and the Windows build recipe (#68).**
- **M0 Core Shell**: CMake build, `pac_engine` (`pac::core` harness + `pac::pnc`),
  `pac_themummy` sample, headless doctest+CTest.
- **M1 Generic 2D**: `pac::geom`, `ResourceCache` + `AudioServices`, and `pac::gfx`
  (`Spritesheet`/`Animation`/`SequencePlayer`/`AnimatedSprite`) — verified by
  `pac_sprite_test` against the Julia atlas.
- **M2 Lua bridge**: `pac::core::Scripting` (single Lua 5.4 state via sol2, pimpl)
  with a coroutine scheduler (`spawn`/`wait`/`emit`/`wait_event`, script scopes +
  cancellation), the core Lua API (`resource_path`, audio, `get_state`/`set_state`
  over `StateStore`), the id-only `ScriptHandle` usertype, and the `StoryText`
  cutscene scene. sol2 + Lua are PRIVATE to the scripting TUs (never in a public
  header).
- **M3 P&C room slice**: `pac::pnc` room + cast loaders (`parse_room`/`parse_cast`,
  headless), `Avatar` (AnimatedSprite + straight-line move gated by the walkable
  area), `SpeechManager`, `RoomRuntime` (Lua behavior held opaquely), `RoomRenderer`,
  and the `RoomScene` orchestrator. Click-to-move + click-a-hotspot → run its
  default verb → caption as speech.
- **M4 Core gameplay**: room transitions via zones + `change_room`, scrolling
  `Camera` with dead-zone follow, region/object/z-order rendering, `InventoryModel`
  + inventory Lua handlers, `Command` model + `CommandBuilder` state machine +
  dispatcher, and the basic SCUMM panel (verb grid + command bar + text inventory).
  Verified through the 3-room sample (`study`/`hall`/`exterior`).
- **M5 Dialog, NPCs, save/load**: room-scoped NPC avatars (owned by `RoomRuntime`),
  the `DialogRuntime` (Lua tree, sol-pimpl, headless tests), the
  `RoomScene::ViewState` machine (`Command`/`Dialog`/`Blocked`/`Menu`), dialog
  options in the SCUMM panel + the `start_dialog` Lua API, full `GameState` +
  `SaveService` (3 manual slots + 1 autosave), and Title/Settings/Continue plus the
  in-game pause/save/load menu.
- **M6 Hardening**: loader validation diagnostics (#36), in-room debug overlays
  (#37) + dev actions (#38), background-layer visibility (`set_layer_visible`, #64)
  + z-order guidance (#65), edge-aware speech containment (#62), the settings UI's
  music/SFX volume controls (#67), and settings persistence to the per-user config
  location (`SettingsStore`, #66).
- **M7 Presentation & authoring polish**: custom mouse cursor + hover affordance
  (#73), scene/room fade transitions (#74), avatar shadows (#75), the
  close-up/examine scene type (#76), the restyled SCUMM panel (#77), approach
  points with command queueing (#70), dialog text placement via `talk_spot` (#69),
  display-mode settings (windowed/fullscreen with an APPLY/BACK flow, #71), and the
  localization infrastructure (`Localization`, manifest `languages` map + selector,
  Spanish default, #72).

The remaining MVP-hardening work is in **M6**: authoring templates (#39), a
packaging smoke path (#40), the manual MVP regression checklist (#41), and the
Windows build recipe (#68). **M7** is otherwise fully merged. See the GitHub
milestones.

## The design docs are the source of truth

The canonical design lives in [docs/sources/design/](docs/sources/design/). **The
implementation follows the design, not the other way around — where code diverges,
the code is what changes.** Read [00-index.md](docs/sources/design/00-index.md)
first (reading order, glossary, requirement traceability, conventions), then `01`–`06`:

| Doc | Covers |
|-----|--------|
| [01-engine-requirements.md](docs/sources/design/01-engine-requirements.md) | Requirements R1–R8, MVP / design-for / constraint scope tags, out-of-scope list. |
| [02-architecture-overview.md](docs/sources/design/02-architecture-overview.md) | Layers + dependency rule, sol2 Lua bridge, runtime spine, `EngineContext`, manifest, coordinate spaces, `GameState`, implementation guidelines. |
| [03-2d-game-concepts.md](docs/sources/design/03-2d-game-concepts.md) | Generic 2D layer: main loop, scene contract, resources, spritesheets / animated / composite sprites, geometry + pathfinding, settings, audio. |
| [04-point-and-click-concepts.md](docs/sources/design/04-point-and-click-concepts.md) | Genre layer: rooms, layers / regions, camera, z-order, hotspots / affordances, avatars, command builder + SCUMM panel, dialog, speech, inventory. |
| [05-scripting-api.md](docs/sources/design/05-scripting-api.md) | The full Lua API surface, game wiring, error handling. |
| [06-data-formats.md](docs/sources/design/06-data-formats.md) | Exhaustive field reference for every YAML / Lua data file. |

Reach for the relevant doc before implementing a subsystem; do not reconstruct
behavior from memory. The PDF in `docs/sources/design/` is generated from these
markdown files by `build-pdf.sh` — edit the markdown, not the PDF.

## Architecture: layers and the dependency rule

```
Game           Lua scripts + assets + game manifest          (no C++)
Point & Click  room view, title, cutscenes, rooms,           pac::pnc
               hotspots, avatars, SCUMM panel, dialog
Generic 2D     spritesheets, animated/composite sprites      pac::gfx
Core           window, loop, input, resources, audio,        pac::core
               settings, Scene, SceneManager, geometry       pac::geom
```

**A layer may depend only on the layers below it, never above.** This is part of
the design, not a guideline:

- `pac::core` / `pac::geom` depend on SFML only; Core must not know point-and-click concepts.
- `pac::gfx` depends on Core only; it must not know about verbs, inventory, rooms, or dialogs.
- `pac::pnc` may use `gfx` and `core`.
- Game scripts use **only** the Lua API — a standard game requires zero new C++.

Enforced by folder/namespace structure (`lib/include/engine/{core,geom,gfx,pnc}/`).
If a feature seems to require crossing a boundary, stop and revisit the design.

## Tech stack and constraints (fixed — R5/R6/R7)

- **Engine:** C++17 with SFML 2.6.
- **Scripting:** Lua 5.4 embedded via [sol2](https://github.com/ThePhD/sol2) (header-only).
- **Data:** YAML via `yaml-cpp`.
- **Tests:** doctest (single-header), registered with CTest; enable ASan/UBSan in dev.
- **Pathfinding:** built-in **visibility-graph** A* in `pac::geom`, behind the `find_path` seam. A grid A* (micropather) or navmesh smoother are design-for and not currently vendored.
- **Tools:** Python for asset/authoring tooling; prefer web-based *offline* authoring tools (the runtime is a native SFML app).
- **Dev OS:** Ubuntu 24.04. **Primary target:** Windows 10/11 x64. Linux is a possible later target.
- **Display:** fixed virtual resolution per game (from the manifest `resolution:`), uniformly scaled to the window with aspect-preserving letterbox/pillarbox bars.

Dependency acquisition: compiled libs from the system package manager (apt on Linux,
vcpkg on Windows); header-only libs (sol2, doctest) via CMake `FetchContent` pinned.
See [02 § Dependencies](docs/sources/design/02-architecture-overview.md).

## Build layout (target)

```
lib/include/engine/{core,geom,gfx,pnc}/   public headers
lib/src/                                  engine implementation  -> pac_engine
games/themummy/{CMakeLists.txt,main.cpp,data/}   example game     -> pac_themummy
tests/      doctest + CTest targets
experiments/  throwaway / exploratory executables
CMakeLists.txt   top-level, one include per group (lib/games/tests/experiments)
```

Games, tests, and experiments link the `pac_engine` library and may add a minimal
`main.cpp` and optional custom `Scene` subclasses. There is no build system yet —
when you scaffold it, follow this layout and update the build/test commands below.

## Build & test commands

**Linux (dev OS, system deps).** SFML / yaml-cpp / Lua come from the package manager
(`libsfml-dev liblua5.4-dev libyaml-cpp-dev pkg-config` on apt); Lua is discovered
via pkg-config.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug      # add -DPAC_ENABLE_SANITIZERS=ON in dev
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/games/themummy/pac_themummy games/themummy/data/game.yaml          # run the sample
./build/games/themummy/pac_themummy games/themummy/data/game.yaml --frames 5   # headless smoke
```

Equivalent via CMake presets (`CMakePresets.json`): `cmake --preset linux-debug`
then `cmake --build --preset linux-debug` and `ctest --preset linux-debug`
(`linux-release` for an optimized build).

**Windows (primary release target, MSVC + vcpkg).** Compiled deps come from the
vcpkg manifest (`vcpkg.json`); sol2/doctest stay header-only. Two `overrides`
pin the API-sensitive deps: SFML to **2.6.1** (the engine uses the SFML 2.x API,
not SFML 3) and Lua to **5.4.7** (R6 fixes Lua 5.4; the vendored sol2 rejects 5.5,
which is the current vcpkg default — and `find_package(Lua 5.4)` is a *minimum*, so
it would otherwise accept it). Lua is discovered with CMake's `FindLua` (no
pkg-config on MSVC).

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"             # a vcpkg checkout
# One-time: stamp the manifest with the local vcpkg baseline so the SFML override
# resolves. The committed vcpkg.json deliberately omits builtin-baseline (no fixed
# SHA pinned), so run this once per checkout (and CI does it automatically):
& "$env:VCPKG_ROOT\vcpkg.exe" x-update-baseline --add-initial-baseline
cmake --preset windows-msvc                       # configures + vcpkg-installs deps
cmake --build --preset windows-msvc-release       # or windows-msvc-debug
ctest --preset windows-msvc-release
```

Both OSes are covered by CI (`.github/workflows/ci.yml`): Ubuntu on every branch,
Windows on PRs and develop/main pushes.

Format before committing: `clang-format -i` on changed `*.hpp`/`*.cpp` (never
`tests/_vendor/` or `third_party/`).

**Offline note:** `github.com` (git) is unreachable here, so doctest can't be
FetchContent'd. `tests/_vendor/doctest/doctest.h` is the genuine doctest **v2.4.11**
single header (fetched from the raw CDN) and is preferred when present; delete
`tests/_vendor/` to fall back to `FetchContent` once git access is available. The
sample's UI font is **Departure Mono** (SIL OFL 1.1) at
`games/themummy/data/fonts/DepartureMono-Regular.otf`, with the license bundled
alongside as `DepartureMono-OFL.txt`.

## Conventions that must hold

- **C++ style.** Formatting is enforced by [.clang-format](.clang-format) (LLVM
  base, 4-space indent, 100-col, left-aligned pointers). Naming: `PascalCase`
  types, `snake_case` functions/methods, lowercase `pac::` namespaces,
  trailing-underscore members, `UPPER_SNAKE` enum values. Headers `.hpp` / sources
  `.cpp`, `snake_case` file names mirroring the namespace dirs, `#pragma once`. Full
  reference: [docs/coding-conventions.md](docs/coding-conventions.md).
- **Static data in YAML, behavior in Lua.** YAML defines *what exists* (manifest,
  cast, room layout, geometry, hotspots, affordances, layers, regions, objects,
  avatar start positions, inventory). Lua defines *what happens* (lifecycle hooks,
  verb handlers, dialog branching, scripted actions). Never put geometry, asset
  paths, or hotspot polygons in Lua.
- **`id` vs `name`.** Internal ids are ASCII, stable, script-friendly. Display
  `name`s may contain spaces, accents, and localized text. Keep them separate.
- **No hardcoded user-facing strings in C++ (R3).** Engine-emitted UI text (verb
  labels, connectors, menu labels) is looked up by key in the manifest `strings`
  resource. Game-content strings (`name`s, speech, dialog lines) stay inline in
  their own data files.
- **Logical resource paths only.** Assets are referenced by logical paths relative
  to `resources.src` (e.g. `backgrounds/study.png`), resolved by the resource
  layer. Never concatenate platform filesystem paths in engine code or scripts.
- **Lua API is flat `snake_case` globals**; state keys use dotted names
  (`mummy.awake`). Lua handles like `avatar(id)` store only a stable id, never a
  raw C++ pointer — the C++ side re-resolves the id each call.
- **State values are scalars** (bool / number / string) for the MVP. No tables.
- **All engine YAML files carry an optional `version:` int** (default 1).

## Invariants easy to get wrong

- **Persistent state lives only in the engine-owned `GameState`** (current
  scene/room, player pose, inventory, global state, per-room state, region states,
  dialog `once` flags). Lua locals, closures, and running coroutines are transient
  and are never saved. Scripts persist facts only through `set_state` /
  `set_room_state` / inventory / region / dialog stores. See
  [02 § Make persistent state explicit](docs/sources/design/02-architecture-overview.md).
- **Logic must be headless-testable.** Geometry, command model + builder, dialog
  runtime, state stores, inventory model, and YAML loaders must be constructable
  and exercisable with no window. Point-and-click *logic* must not reach for
  `sf::RenderTarget`; only rendering needs a graphics context.
- **Keep `RoomScene` an orchestrator**, not the owner of all logic. It delegates to
  `RoomRuntime`, `RoomLoader`, `RoomRenderer`, `CommandBuilder`, `CommandDispatcher`,
  `ScummPanel`, `InventoryModel`, `DialogRuntime`, `SpeechManager`, `AvatarController`.
- **Command system is independent of the UI.** The SCUMM panel is one way to build
  a `Command`; tests, debug tools, and scripted commands reuse the same dispatch path.
- **Scenes vs rooms are different levels.** Scenes are manifest-declared top-level
  states (`TitleScreen`, `StoryText`, `RoomScene`, `SettingsScene`) switched via
  `SceneManager`. Rooms are places inside `RoomScene`, loaded by id from
  `rooms/<id>.{yaml,lua}` and switched with `change_room` — rooms are not manifest entries.
- **Fail loudly in development builds** on authoring errors (bad ids, missing
  resources, unsupported state types); fall back safely and log in release.
- **Coroutine scopes:** `spawn(fn)` inherits the current script scope (room /
  dialog / scene / global). When a scope ends, its tasks are cancelled and never
  resumed; cancellation does not run Lua cleanup — use `on_unload` / dialog
  `on_exit`. The single `lua_State` is owned by the scripting service.
- **Prefer MVP behavior over speculative generality.** When a feature has both MVP
  and *design-for* versions, build the MVP first. Don't implement design-for unless
  the current vertical slice needs it or it prevents a known architectural dead end.

## How to build it: vertical slices, not isolated subsystems

The plan ([implementation-plan.md](docs/sources/design/implementation-plan.md)) is
to reach a playable 3-room MVP through milestones:

- **M0** Core shell: manifest-driven startup, window, 60 Hz loop, blank/title/settings scenes.
- **M1** Generic 2D: resource cache, geometry, spritesheet + animated sprite.
- **M2** Lua bridge: scripting service, scheduler (spawn/wait/emit), `StoryText` scene.
- **M3** P&C slice: one room — background, walkable area, player avatar, click-to-move, one hotspot.
- **M4** Core gameplay: room transitions, camera, regions/objects/z-order, inventory, command model + SCUMM panel (3-room prototype).
- **M5** Dialog, NPCs, full `GameState`, save/load (3 manual slots + 1 autosave), Title/Settings/Continue.
- **M6** Hardening: validation diagnostics, debug overlays, dev actions, templates, packaging smoke path.

A subsystem is useful only when it helps make the current slice playable. Treat the
worked examples in the design docs as regression-test candidates.

## Workflow

- Branch from `develop` (the main integration branch); PRs target `develop`. See
  [CONTRIBUTING.md](CONTRIBUTING.md).
- Keep commits small and focused; match existing style.
- If a change adds an engine feature or data format, update the relevant design doc
  under `docs/sources/design/` — the design stays authoritative.
</content>
</invoke>
