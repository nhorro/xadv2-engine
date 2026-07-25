# C++ Engine Coding Guide

This guide is the implementation source of truth for engine C++ code. The
[design documents](../design/00-index.md) describe *what* the engine does; this
guide describes *how* to write it. If this guide conflicts with the design
docs, the design docs win — fix this guide.

This guide applies to all C++ code under `lib/`, `games/<game>/main.cpp`,
`tests/`, and `experiments/`.

## 1. Toolchain

| Item | Choice | Notes |
|------|--------|-------|
| Language | C++20 | `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`. |
| Build system | CMake ≥ 3.16 | Matches the floor set in the top-level `CMakeLists.txt`. |
| Dependencies | Hybrid | Compiled libraries (SFML, yaml-cpp, Lua) via `find_package` (apt on Linux, vcpkg on Windows); header-only libraries (sol2, doctest) via pinned `FetchContent`. See design 02 §Dependencies. |
| Compilers | GCC 13+, Clang 17+, MSVC 19.30+ | All three exercised in CI. |
| Test framework | doctest | One headless executable today (`pac_core_tests`); split per area as the test surface grows. |
| Lua binding | sol2 v3 | Header-only. |
| YAML | yaml-cpp | |
| Graphics / audio / window | SFML 2.6 | API-compatible with SFML 2.5. |
| Editor LSP | `clangd` via `.clangd` | |
| Static analysis | `clang-tidy` via `.clang-tidy` | Run in CI. |

## 2. Repository layout

```text
/
├── lib/
│   ├── include/engine/<layer>/  Public headers, organized by namespace
│   └── src/<layer>/             Implementation files
├── games/<game>/
│   ├── CMakeLists.txt
│   ├── main.cpp                 Minimal entry point only
│   └── data/                    Manifest, Lua, YAML, assets
├── tests/<area>/                One executable per subsystem
├── experiments/                 Throwaway; not in CI
├── docs/development/coding-guide/           This guide
├── docs/development/design/         Design specs
├── CMakeLists.txt               Top-level (declares dependencies via FetchContent)
├── .clangd                      Editor LSP config
├── .clang-tidy                  Static analysis config
└── .gitattributes               UTF-8 + LF enforcement
```

Layers: `core`, `geom`, `gfx`, `pnc`. Dependency direction is one-way; see
[Architecture overview](../design/02-architecture-overview.md) §Layers.

## 3. File and directory naming

| Item | Rule | Example |
|------|------|---------|
| Header file | snake_case, `.hpp` | `command_builder.hpp` |
| Source file | snake_case, `.cpp` | `command_builder.cpp` |
| Test file | `<area>_test.cpp` | `command_builder_test.cpp` |
| Directory | snake_case | `room_scene/` |
| One public type per header | strongly preferred | `Command` in `command.hpp` |

Exception: small related types may share a header (e.g., `ObjectRef`, `Verb`,
`Command` together in `command.hpp`).

## 4. Identifier naming

| Kind | Convention | Example |
|------|------------|---------|
| Namespace | lower_case | `pac::pnc` |
| Class / Struct / Enum / Concept | CamelCase | `CommandBuilder` |
| Enum constant | UPPER_CASE | `LogLevel::DEBUG`, `OpKind::GOTO` |
| Function (free or method) | snake_case | `set_region_state` |
| Variable / parameter | snake_case | `current_room` |
| Member | snake_case | `room_data` |
| Private member | trailing underscore | `room_data_` |
| Constant (`constexpr`/`const`) | `k` + CamelCase | `static constexpr std::size_t kMaxVoices = 16;` |
| Macro | UPPER_SNAKE_CASE (avoid) | `PAC_ASSERT(...)` |
| Template parameter | CamelCase | `template<class Loader>` |

The names in the design docs are canonical: if a doc says `change_room`, the
C++ function is `change_room`. If a doc says `Command`, the C++ type is
`Command`.

## 5. Header conventions

```cpp
#pragma once

// 1. Standard library (alphabetical)
#include <optional>
#include <span>
#include <string>

// 2. Third-party (alphabetical)
#include <SFML/Graphics.hpp>
#include <sol/sol.hpp>
#include <yaml-cpp/yaml.h>

// 3. Engine, from lower layers upward
#include "engine/core/scene.hpp"
#include "engine/geom/polygon.hpp"
#include "engine/gfx/animated_sprite.hpp"

// 4. Same-layer headers
#include "engine/pnc/command.hpp"
```

Rules:

- `#pragma once`, not include guards.
- No `#include <bits/stdc++.h>`.
- Forward-declare types in public headers when the full definition isn't needed.
- Public headers don't include implementation-only headers (e.g., `sol/sol.hpp`
  is allowed in `script_host.hpp` because the binding is the public contract;
  it's not allowed in `command.hpp`).
- `using namespace` is forbidden in headers. Use `using sf::Vector2f;` in a
  `.cpp` only when it materially shortens code.

## 6. C++20 usage policy

| Feature | Policy |
|---------|--------|
| Designated initializers | **Use** for POD construction, especially from YAML-loaded structs. |
| `std::span<const std::byte>` | **Use** for byte streams (the resource backend interface). |
| Concepts | **Use** to constrain templated APIs. Prefer concepts over `enable_if`. |
| `<format>` | **Use** for diagnostics and logging. |
| `[[nodiscard]]` | **Required** on functions whose return value carries an error or owns a resource. |
| Three-way comparison `<=>` | Use for value types when natural. |
| `consteval` | Use for compile-time-only helpers (e.g., schema-version literals). |
| `<ranges>` | Use sparingly — only when a chained pipeline is clearer than a loop. |
| Modules | **Do not use.** Not portable enough across our compilers/toolchains. |
| C++ coroutines | **Do not use.** The engine uses Lua coroutines via sol2; mixing the two confuses everyone. |
| C-style casts | **Forbidden.** Use `static_cast`, `reinterpret_cast` (only for byte-level work), `const_cast` (extremely rare), or list initialization. |
| `using` for type aliases | Prefer over `typedef`. |

## 7. Namespaces and layer boundaries

- All engine code lives in `pac::<layer>`.
- `pac::core` may **not** reference `pac::gfx`, `pac::pnc`, or any game code.
- `pac::gfx` may reference `pac::core` and `pac::geom`.
- `pac::pnc` may reference `pac::gfx`, `pac::geom`, `pac::core`.
- The game has no direct C++ access; only the Lua API exposed by `ScriptHost`.

If a change requires a forbidden include, the design needs review before
coding. Don't quietly add the include.

## 8. Ownership and lifetime

| Need | Tool |
|------|------|
| Single owner | `std::unique_ptr<T>` |
| Shared owner | `std::shared_ptr<T>` — rare; document the reason |
| Non-owning, optional | `T*` (raw pointer; document that null is valid) |
| Non-owning, mandatory | `T&` reference |
| Optional value | `std::optional<T>` |
| Variant value | `std::variant<...>` |
| Borrowed string | `std::string_view` (do not store past the call) |
| Borrowed buffer | `std::span<const std::byte>` |

Specific to `RoomScene`:

- `RoomScene` owns all components by `std::unique_ptr`.
- No component owns another component.
- Per-room components are destroyed and recreated on `change_room`.
- Persistent components (`InventoryModel`, `GlobalStateStore`, `ScriptHost`,
  `SpeechManager`, `ScummPanel`) are not touched by `change_room`.
- The player avatar is owned by `RoomScene`, not by `RoomRuntime` — it
  survives every `change_room`. NPC avatars are owned by `RoomRuntime` and are
  destroyed/recreated per room.

See [Architecture overview](../design/02-architecture-overview.md)
§Keep `RoomScene` as an orchestrator for the full wiring.

Never use `new` or `delete` directly. Allocate through `std::make_unique` or
`std::make_shared`.

## 9. RAII and resources

- Acquire in constructors, release in destructors.
- Resource handles (texture, font, sound) are typed wrappers around entries in
  the resource cache; do not load from disk directly.
- Never call `sf::Texture::loadFromFile` — go through `ResourceSource`. Use
  `loadFromStream` with the byte stream returned by the resource layer.
- Constructors should not partially-initialize an object. If a constructor can
  fail, use a factory function that returns `std::optional<T>` or throws a
  typed loader exception.

### Cache eviction policy (MVP)

The resource cache is **append-only** in the MVP: a loaded asset stays
resident for the rest of the process. This is acceptable for a game in La
Momia's scope; it is a known limitation and a deliberate one. Do not add LRU
or eviction code without a measured memory pressure case. When the time
comes, the eviction hook is the `ResourceCache` boundary — the rest of the
engine sees only typed handles and need not change.

## 10. Error handling

| Source | Handling |
|--------|----------|
| Engine bug (invariant broken) | `PAC_ASSERT(...)` — terminates cleanly in release. |
| Author error (bad YAML, missing asset, malformed script) | Loaders throw a typed exception (`LoadError`). The top-level loader catches and shows a dev-build error overlay or refuses the load in release with a clear log. |
| Player input | No exception. Soft feedback (sound / cursor flash) only. |
| Script runtime error | Caught at the C++/Lua boundary in `ScriptHost`. Logged. Default verb response speaks. Command builder resets. |
| Resource load failure | Throw at load time. Never silently substitute a missing asset. |

Use `std::expected<T, Error>` (or equivalent) at API boundaries where the
caller can act on the error (e.g., `find_path` may return "no path found").
Internal code uses exceptions for genuinely exceptional conditions and direct
return for normal control flow.

Catch `(...)` **only** at the top-level coroutine resume boundary inside
`ScriptHost`. Never swallow exceptions silently.

Error categories (with dev vs release behavior) are tabulated in
[Scripting API](../design/05-scripting-api.md) §Error handling —
that table is authoritative for what each category does.

### Loader diagnostics envelope

All YAML/asset loaders raise `pac::core::LoadError` (in
`engine/core/load_error.hpp`), which carries the structured envelope
`{ source, location, id, message }`:

- **`source`** — the subsystem tag: `manifest-loader`, `room-loader`,
  `cast-loader`, `inventory-loader`, `strings-loader`, `anim-loader`,
  `spritesheet-loader`, `dialog-loader`.
- **`location`** — `{ file, line, column }`. The text-only `parse_*` functions
  fill `line`/`column` from the offending `YAML::Node` mark (see the private
  `core/load_error_yaml.hpp` helpers `loc_of` / `fail_at`); the `load_*` /
  call-site boundary attaches `file` via `LoadError::with_file(...)` and
  rethrows. This keeps parsers path-agnostic and headless-testable.
- **`id`** — a short, stable, dotted error **code** (e.g. `room.id-mismatch`,
  `strings.defaults-missing-key`) so offline authoring tools and tests can match
  on it instead of parsing free text. Tests assert `LoadError::code()`.
- **`what()`** renders the whole envelope on one line:
  `[source] file:line:col (id): message`.

The per-layer typed exceptions (`ManifestError`, `pnc::DataError`,
`gfx::AssetError`) are thin subclasses of `LoadError`, so both
`catch (const LoadError&)` and `catch (const DataError&)` work and existing
`CHECK_THROWS_AS(..., DataError)` tests keep matching. Loaders throw their typed
subclass via `fail_at<DataError>("room-loader", "code", msg, node)`.

Loaders **fail loud always** (`parse_*` throw unconditionally); the caller
decides fatality — startup-required resources (manifest, strings) abort `run()`,
while per-room/asset loads log and fall back. The dev-only downgrade of
soft checks (e.g. unknown `strings.defaults` keys → warn in release) is
design-for, pending a release/dev build-mode gate.

## 11. Logging

- Single engine logger: `pac::log::info(...)`, `pac::log::warn(...)`,
  `pac::log::error(...)`.
- Tag with subsystem prefix in the message:
  ```cpp
  pac::log::warn("[script] missing handler for {}/{}", verb_name(v), target_id);
  ```
- Use `<format>` placeholders.
- Log lines are written to stderr always; to `<config>/<game_id>/log/run-<timestamp>.log`
  in development builds.
- Do not log from the audio callback thread.
- Do not log per-frame at info level.

## 12. sol2 binding patterns

- The single `lua_State*` is owned by `ScriptHost`. No other component sees
  `lua_State*` or sol2 types in its public interface.
- Bindings are registered once at `ScriptHost::register_api` startup.
- Lua tables passed to engine code are converted at the boundary into typed
  C++ structs. Do not pass `sol::object` through internal APIs.
- Coroutine tasks are a small `Task` struct holding the `sol::coroutine`, the
  context tag, and the current wait condition.
- Handler invocation receives **bare id strings**, never `ObjectRef` tables.
  See [Scripting API](../design/05-scripting-api.md) §Hotspot handler
  signatures.
- Do not stash `sol::object`, `sol::table`, or `sol::function` in member
  variables across frames unless lifetime is clearly tied to `ScriptHost`.
  Lua garbage collection can move things; convert at the boundary.

Minimal binding sketch:

```cpp
void ScriptHost::register_api(sol::state& lua) {
    lua.set_function("change_room", [this](std::string id, sol::optional<std::string> entry) {
        this->change_room(id, entry.value_or(""));
    });

    lua.new_usertype<AvatarHandle>(
        "AvatarHandle",
        sol::no_constructor,
        "move_to",        &AvatarHandle::move_to,
        "play_until_end", &AvatarHandle::play_until_end,
        "position",       &AvatarHandle::position
        // ...
    );

    lua.set_function("avatar", [this](std::string id) {
        return this->avatar_handle(id);
    });
}
```

The `END` constant used by dialog scripts is injected as a unique sentinel
table into the dialog scope by `DialogLoader` before evaluating each dialog
file. Loaders fail loudly if any `to` field is `nil` (typo of `END`).

## 13. yaml-cpp loading patterns

- One loader function per schema (`load_manifest`, `load_room`, `load_cast`,
  `load_animation`, `load_spritesheet`, `load_dialog_table`, `load_save`).
- Loaders take a `YAML::Node` (or a resource path) and return a typed C++ struct.
- Loaders validate **all** required fields and throw `LoadError` on failure
  with the field name and location.
- Use designated initializers when constructing the C++ struct.
- `pac::geom` provides canonical loaders for `Point`, `Polygon`, `Rect`,
  `Color`. Other loaders compose with these.
- Do not store `YAML::Node` past the loader's return. Parse once into typed
  structs.

Pattern:

```cpp
RoomData load_room(YAML::Node const& node, ResourceSource& res) {
    require_field(node, "id");
    require_field(node, "size");
    require_field(node, "background");
    require_field(node, "walkable");

    return RoomData{
        .id        = node["id"].as<std::string>(),
        .size      = load_rect(node["size"]),
        .background= load_background(node["background"]),
        .walkable  = load_polygon(node["walkable"]),
        .obstacles = load_polygons(node["obstacles"]),
        .points    = load_points(node["points"]),
        .zones     = load_zones(node["zones"]),
        .regions   = load_regions(node["regions"]),
        .objects   = load_objects(node["objects"]),
        .hotspots  = load_hotspots(node["hotspots"]),
        .avatars   = load_avatar_placements(node["avatars"]),
    };
}
```

### Hotspot hit-test rule

`HotspotData` carries an optional `area` polygon and an optional `bind`. The
hit test, applied each frame to the cursor in world space, is:

1. If `area` is present and contains the cursor → hit.
2. Else if `bind` is `object:<id>` → use the object sprite's frame bounds.
3. Else if `bind` is `region:<id>` → use the region's `area` polygon
   (constant, independent of the current state image).
4. Else → no hit.

The region rule is deliberate: a region's state images may differ in shape,
but the hit polygon stays put. Authors who want a state-dependent hit area
use an explicit `area` on the hotspot or split the region.

## 14. Coroutine scheduler conventions

- Every task carries a context tag (`Room`, `Dialog`, `Global`, `Scene`).
- The tag is stored on the `Task` struct, not on the sol2 coroutine.
- Cancelling a context drops tasks with that tag; their `sol::coroutine`
  references are released for Lua GC.
- Spawn order is preserved within a single tick.
- A task's wait condition is a `std::variant<Ready, Timer, Event, AvatarMove,
  Animation, Speech, Done>` matching the design-doc table in
  [2D game concepts](../design/03-2d-game-concepts.md) §Script task
  states.
- The scheduler runs once per fixed update, after `update(dt)`, before `draw`.
- Tasks waiting on `Speech` or `AvatarMove` are resumed when the engine-side
  state indicates completion — the engine pushes a "complete" signal; tasks
  don't poll.
- `change_room` from inside a handler is queued, not applied immediately. The
  queued request fires after the current update step finishes and before the
  next one begins, so handlers always run to completion before their room
  vanishes.

## 15. Persistence map

Every state-mutating API writes to exactly one slot of `GameState`. The
dispatcher must not assume any other implicit persistence happens.

| API | Writes to |
|-----|-----------|
| `set_state(k, v)` | `GameState.global_state[k]` |
| `set_room_state(k, v)` | `GameState.room_state[current_room_id][k]` |
| `add_item(id)` / `remove_item(id)` | `GameState.inventory` |
| `set_region_state(id, st)` | `GameState.region_states[current_room_id][id]` |
| `enable_hotspot(id)` / `disable_hotspot(id)` | `GameState.hotspot_enabled[current_room_id][id]` |
| `show_object(id)` / `hide_object(id)` | `GameState.object_visible[current_room_id][id]` |
| Dialog engine on `once` consumption | `GameState.dialog_flags[dialog_id]` |
| `change_room(id, entry)` | `GameState.current_room_id`, player position |

Anything not in this map is **transient** and is not saved. New mutating APIs
must extend this map and `GameState` together in the same change.

## 16. Testing with doctest

- MVP today: one headless executable `pac_core_tests` aggregating every
  `<area>_test.cpp` under `tests/`. Split per area (`pac_geom_tests`,
  `pac_command_tests`, `pac_dialog_tests`, `pac_save_tests`,
  `pac_room_loader_tests`, `pac_script_tests`, `pac_resource_tests`) when
  the surface grows large enough that per-binary test-times matter.
- Filename: `tests/<area>_test.cpp`. A single `test_main.cpp` provides the
  doctest entry point.
- Unit tests run in under one second each.
- Integration tests may load real YAML/Lua fixtures from `tests/fixtures/`.
- Smoke test for every example: `ctest -L gui` runs each `pac_example_*` for N
  frames (`--frames`) and fails on any load or render error. These are the only
  tests that open a window, hence the label; the rest of the suite is headless.
- Use `SUBCASE` for related variants of a setup.
- For parameterized cases (e.g. one row per transition-table row), use a small
  table of structs and a `for` loop with `CAPTURE` / `INFO` so the failing row
  is identified by name in the test output.

Skeleton:

```cpp
#include <doctest/doctest.h>

TEST_CASE("CommandBuilder transitions on verb click") {
    CommandBuilder cb;
    REQUIRE(cb.state() == BuilderState::Idle);

    SUBCASE("look_at -> expecting any") {
        cb.click_verb(Verb::LookAt);
        CHECK(cb.state() == BuilderState::ExpectingParam1AnyObject);
    }

    SUBCASE("give -> expecting inventory") {
        cb.click_verb(Verb::Give);
        CHECK(cb.state() == BuilderState::ExpectingParam1InventoryObject);
    }
}
```

The command-builder transition table and the dispatch precedence matrix from
the design docs are tracked as data-driven tests — one `SUBCASE` (or one row
in a table walked by a `for` loop) per table row. When a row changes in the
doc, the test changes in lockstep.

## 17. CMake conventions

- One `CMakeLists.txt` per directory.
- All engine targets prefixed `pac_`.
- Use `target_compile_features(<tgt> PUBLIC cxx_std_20)`.
- Use `target_include_directories(<tgt> PUBLIC ...)`; avoid `include_directories`.
- Compiler flags via `target_compile_options(<tgt> PRIVATE ...)`. The warning
  baseline: `-Wall -Wextra -Wpedantic -Werror` on Clang/GCC, `/W4 /WX` on
  MSVC. Centralized in an interface library `pac_compile_options` so every
  target inherits the same flags — no per-target drift.
- Sanitizer config is a CMake option `PAC_ENABLE_SANITIZERS`, default ON in
  the Linux test build, OFF elsewhere. ASan + UBSan on Linux; ASan-only on
  MSVC (UBSan is not supported there).
- Compiled deps (SFML 2.6, yaml-cpp, Lua 5.4 when added) via `find_package`
  (apt on Linux, vcpkg on Windows). Header-only deps (sol2, doctest) via
  `FetchContent` pinned by git tag. doctest may be vendored as a single-header
  fallback under `tests/_vendor/` and is preferred when present (useful in
  offline / air-gapped environments).
- Do not check in compiled third-party source.
- `pac_engine` is a static library — no DSO concerns in the MVP.
- A `CMakePresets.json` ships preset configurations for `linux-debug`,
  `linux-release`, `windows-debug`, `windows-release`. Contributors use those
  presets instead of inventing their own.

## 18. Performance

Adventure games of this scale are not CPU- or memory-bound. Avoid pessimization
but do not optimize speculatively.

| Do | Don't |
|----|-------|
| `vector::reserve` when final size is known. | Manually unroll small loops. |
| Stable-sort `ZDrawable`s once per frame. | Re-sort within the frame. |
| Cache parsed `RoomData` while the room is loaded. | Re-parse YAML on every frame. |
| Pass `string_view` for non-owning string args. | Pass `const std::string&` if `string_view` works. |
| Use `std::span` for non-owning ranges. | Pass `(ptr, size)` pairs. |

Measure before optimizing. If a path needs optimization, add a comment with
the *why* and the measured number.

## 19. Comments and documentation

Default: no comments.

Add a comment when the *why* is non-obvious:

- a hidden constraint;
- a subtle invariant;
- a workaround for a specific bug or library quirk;
- behavior that would surprise a reader.

Do not write *what* the code does — the code does that. Public APIs in headers
get a one-line description above the declaration only when the name does not
already make the purpose obvious.

No file-level boilerplate banners. No "Last modified" headers.

## 20. Common pitfalls in this engine

| Pitfall | Avoidance |
|---------|-----------|
| Forgetting to tag a coroutine | Always go through `ScriptHost::spawn(context_tag, fn)`. |
| Storing physical filesystem paths | Use logical paths only. Engine code above the resource layer must not see filesystem paths. |
| Bypassing the handler resolution chain | The dispatcher is the only place verb→handler resolution happens. Components don't look up handlers themselves. |
| Mutating `RoomData` after load | `RoomData` is immutable. Mutable state lives in `RoomRuntime`. |
| Drawing without `ZDrawable` | Every world-space drawable participates in the sorted list. |
| Holding sol2 handles across frames | Convert at the boundary. Lua GC can move things. |
| Calling SFML `loadFromFile` directly | Always go through `ResourceSource::open` and `loadFromStream`. |
| Adding includes to `pac::core` from `pac::pnc` | Layer boundary violation; design review required first. |
| Catching `(...)` outside `ScriptHost` | Forbidden. Catch typed exceptions only. |
| Logging per-frame at info level | Don't. Use `warn` or higher for repeated events. |
| Applying `change_room` synchronously inside a handler | Queue it; apply at end-of-step. Tearing down a room while one of its own handlers is on the stack is the prototype's #1 crash. |
| Adding a new mutating Lua API without extending `GameState` | If it isn't in the persistence map (§15), the next save will silently drop the change. |
