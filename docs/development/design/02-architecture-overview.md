# A general view of the architecture

The engine is C++20 + SFML, scripted in Lua, and configured with YAML. Exact
versions and platform constraints are defined in
[Engine requirements](01-engine-requirements.md). This section specifies the
C++ architecture, the dependency rules, the runtime spine, the manifest, and the
display pipeline.

## Layers and dependency rule

The engine is split into layers with a single one-directional dependency rule: a
layer may depend only on the layers below it, never above.

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

| Layer | Namespace | Owns | Depends on |
|-------|-----------|------|------------|
| Core | `pac::core`, `pac::geom` | Window, main loop, input, resources, audio services, settings, diagnostics, geometry, `Scene`, `SceneManager` | SFML only |
| Generic 2D | `pac::gfx` | Spritesheets, animated sprites, composite sprites | Core |
| Point & Click | `pac::pnc` | Room view, title, cutscene scene, rooms, hotspots, avatars, SCUMM panel, dialog runtime | Generic 2D, Core |
| Game | — | Manifest, YAML data, Lua scripts, assets | Lua API only |

The dependency direction is enforced by folder structure and namespaces:

```text
include/engine/core/
include/engine/geom/
include/engine/gfx/
include/engine/pnc/
```

Separate static libraries are not required for the first implementation. The
folder and namespace structure is enough to signal and enforce the intended
architecture.

## Core

Core is the platform harness. A game developer does not edit Core to make a
standard point-and-click game.

Core owns:

- window and resolution management;
- the fixed-timestep main loop;
- physical-to-virtual input mapping;
- resource management;
- audio services;
- settings service;
- the diagnostics service (logging and assertions);
- basic graphics primitives;
- the geometry module;
- the `Scene` interface;
- the `SceneManager` and scene stack.

## Generic 2D

Generic 2D contains reusable building blocks for 2D games. These concepts are not
specific to point-and-click adventures.

Generic 2D owns:

- `Spritesheet`;
- `AnimatedSprite`;
- `CompositeSprite`;
- sprite anchors and sequence playback.

The scene interface and scene manager remain in Core. Concrete scenes such as
the room view, title screen, and cutscene player belong to the Point & Click
layer because they encode genre behavior.

## Point & Click layer

The Point & Click layer owns the genre-specific model:

- room-view scene;
- title scene;
- cutscene scene;
- room model;
- room YAML loader;
- room Lua binding;
- hotspots and affordances;
- avatars;
- SCUMM panel;
- command builder;
- dialog runtime;
- speech presentation.

See [Third-person point & click concepts](04-point-and-click-concepts.md).

## Lua scripting bridge

The Lua scripting bridge lets game scripts drive the engine through a restricted
API. Scripts do not access C++ objects directly except through registered wrapper
objects and functions.

The bridge has two directions:

| Direction | Purpose | Examples |
|----------|---------|----------|
| Engine → script | Invoke lifecycle hooks and verb handlers. | `on_load`, `on_unload`, `on_zone_enter`, `hotspots.drawer.open` |
| Script → engine | Drive engine services through the Lua API. | `talk`, `change_room`, `play_music`, `avatar(id):move_to` |

The Lua API uses `snake_case`. The full API is specified in
[The scripting API](05-scripting-api.md).

### Binding mechanism

The bridge is implemented with [sol2](https://github.com/ThePhD/sol2), a
header-only binding layer over Lua 5.4. The conventions below are part of the
design, not just an implementation detail:

- The engine embeds one `lua_State`, owned by the scripting service and wrapped
  in a `sol::state`. Nothing else creates Lua states.
- Engine → script calls go through `sol::protected_function`. A failed call is
  reported through the diagnostics service and never aborts the frame; in
  development builds it fails loudly (see the error handling rules in
  [The scripting API](05-scripting-api.md)).
- Script → engine functions are registered as flat `snake_case` globals, matching
  the API reference in `05`.
- Handles such as `avatar(id)` return a lightweight `sol::usertype` that stores
  **only a stable id**, never a raw C++ pointer. The C++ side resolves the id to
  an object on each call, so a handle that outlives a room change fails loudly
  instead of dangling.
- C++ holds no Lua references across frames except the coroutine threads owned by
  the script scheduler.

### Script task ownership

Lua coroutines are transient runtime work owned by the script scheduler. Every
scheduled task belongs to one explicit script scope:

| Scope | Lifetime |
|-------|----------|
| Scene | One top-level scene instance. |
| Room | The currently loaded room inside `RoomScene`. |
| Dialog | One active dialog. |
| Global | The current `RoomScene` / game session. |

`spawn(fn)` inherits the current script scope. Room hooks and hotspot handlers
spawn room-scoped tasks; dialog callbacks spawn dialog-scoped tasks; cutscene
scripts spawn scene-scoped tasks. Cross-room global tasks are design-for and must
be explicit if added later.

When a scope ends, the scheduler cancels all tasks owned by that scope before the
engine destroys objects from that scope. Cancellation removes the task from the
scheduler and it is never resumed. Lua cleanup code is not run as part of task
cancellation in the MVP; cleanup belongs in normal lifecycle hooks such as
`on_unload` and dialog `on_exit`.

Lifecycle boundaries:

- `change_room` calls the outgoing room's `on_unload`, cancels the outgoing room
  scope, destroys room-scoped objects, then loads the next room.
- Room script reload in development mode is treated as `on_unload` + room-scope
  cancellation + script reload + `on_load`.
- Dialog end calls `on_exit`, cancels the dialog scope, then returns the room
  view to `Command`.
- Scene replacement or pop cancels the scene scope and its child scopes before
  destroying the scene.
- Load-game cancels the current `RoomScene` script scopes, deserializes
  `GameState`, enters the restored scene / room, and runs the normal `on_load`.
  Saved games never resume old coroutines.

## Runtime spine

Startup and scene transitions are data-driven. A game declares its scenes in a
YAML manifest. The engine constructs scenes from that manifest using a factory
keyed by scene type.

### Startup sequence

1. Load the game manifest.
2. Initialize global services: resources, audio, settings, and scripting.
3. Install the core Lua API.
4. Invoke the optional compiled-game `ApplicationHooks::configure` callback.
5. Construct and enter the manifest's `entry` scene through the populated factory.
6. Create the window and letterboxed virtual view.
7. Run the fixed-timestep main loop.

The executable populates the factory with built-in and custom scene types before
calling `pac::core::run`. A compiled game uses
`ApplicationHooks::configure(EngineContext&, const Manifest&)` to bind game-owned
Lua APIs and initialize game-owned services after the core bindings exist but
before any scene is constructed. The manifest argument lets game-local modules
load declarative scene parameters during setup. An exception from this hook is a
startup failure.

### Scenes vs. rooms

Scenes and rooms are different levels of the runtime model.

| Concept | Meaning | Switched by | Declared in |
|---------|---------|-------------|-------------|
| Scene | Top-level application state. | `SceneManager::goto_scene`, `push_scene`, `pop_scene` | Manifest |
| Room | Place inside the point-and-click room-view scene. | `change_room("id")` | Room files |

Rooms are not manifest entries. The manifest contains one `RoomScene`, and that
scene loads rooms from the game's room directory.

## Engine context and services

Global services are created once at startup (step 3 of the startup sequence) and
owned by the application for the entire process lifetime. Scenes never own global
services; they borrow them through an `EngineContext` passed by reference at
construction.

```cpp
struct EngineContext {
    Display&        display;     // virtual<->window mapping + view
    ResourceCache&  resources;   // logical-path asset access
    AudioServices&  audio;       // MusicPlayer + SoundPlayer
    Settings&       settings;    // persisted player settings
    Scripting&      scripting;   // sol::state + coroutine scheduler
    SceneManager&   scenes;      // goto / push / pop
    Diagnostics&    log;         // logging + asserts
    const DevFlags& dev;         // development flags from the manifest
};
```

Ownership and lifetime:

- Services are owned by the application object, constructed before the first scene
  and destroyed after the last, so the references are always valid.
- A scene may store the `EngineContext` or copy out only the service references it
  needs. A scene shall not extend service lifetime, cache raw pointers to
  services, or share services with background threads.
- Point & Click components (`DialogRuntime`, `CommandBuilder`, `RoomLoader`, ...)
  receive only the specific services they use, not the whole context. This keeps
  the dependency graph explicit and the headless modules narrow.

Input is not a service in the context: input reaches the focused scene through
`handle_event`, and physical-to-virtual coordinate mapping is done by `Display`
before dispatch. A `Clock` / time service may be added later if needed.

## Scene types, outcomes, and wiring

A scene type defines named outcomes. The manifest wires each outcome to another
scene id or to a reserved token such as `QUIT`.

A scene decides when an outcome happens; the manifest decides where that outcome
leads. This keeps scene implementations reusable.

A scene is constructed with:

- its YAML `parameters`;
- the `EngineContext` described in "Engine context and services" above.

Built-in scene types:

| Type | Purpose |
|------|---------|
| `TitleScreen` | Main menu/title screen. |
| `StoryText` | Text-based cutscene. |
| `RoomScene` | SCUMM-style point-and-click gameplay. |

Custom scene types are the extension point for mini-games and special
interactions outside the standard point-and-click model.

## Game manifest

The game manifest is the single source of game-level configuration.

```yaml
# my_game.yaml
version: 1
resolution:
  width: 1280
  height: 720

window:
  fullscreen: false
  width: 1280
  height: 720

resources:
  src: games/themummy/data

strings: strings/es.yaml

settings:
  audio:
    music_volume: 0.8
    sfx_volume: 0.8

development:
  edit_mode: false
  show_walkboxes: false
  show_hotspots: false
  allow_room_reload: true
  profiling: false        # resource-profiling mode (#112)
  profiling_interval: 2.0 # seconds between profiling samples

entry: title

scenes:
  - id: title
    type: TitleScreen
    parameters:
      new_game: intro_cutscene
      exit: QUIT

  - id: intro_cutscene
    type: StoryText
    parameters:
      script: scripts/intro.lua
      on_finish: room_view

  - id: room_view
    type: RoomScene
    parameters:
      cast: cast.yaml
      logic: game.lua
      inventory: inventory.yaml
      inventory_logic: inventory.lua
      rooms: rooms
      start_room: study
      player: julia
      font: fonts/DepartureMono-Regular.otf
```

### Manifest fields

| Field | Scope | Meaning |
|-------|-------|---------|
| `version` | Optional | Data-format version integer (default 1). See [data formats](06-data-formats.md). |
| `resolution` | Required | Virtual design resolution. |
| `window` | Required | Initial physical window mode and size. |
| `resources.src` | Required | Resource source: filesystem directory for MVP; archive file design-for. |
| `strings` | Required | UI strings resource for engine-emitted text (verb labels, connectors, menu labels). One file in the MVP; a language→file map is design-for. See [data formats](06-data-formats.md). |
| `settings` | Optional | Default player-facing settings. User settings may override these. |
| `development` | Optional | Development-only flags. Not persisted as player settings. |
| `entry` | Required | Initial scene id. |
| `scenes` | Required | Scene list and outcome wiring. |

`resources` shall describe where assets are loaded from. It shall not contain
specific assets such as UI fonts. Fonts are requested by the scene, UI, or script
that needs them through logical resource paths.

`development` shall contain development-only flags. These flags are distinct from
player-facing settings, which are persisted by the settings service.

`strings` points at the UI strings resource. Every user-facing string the engine
itself generates — verb labels, command connectors, and built-in menu labels — is
looked up by key in this resource, never hardcoded in C++ (R3). Strings authored
as game content — hotspot and item `name`s, speech, and dialog lines — remain
inline in their own files and are not part of this resource.

## Resource source

The engine reads every asset through a `ResourceSource` (`engine/core/resource_source.hpp`).
Two backends ship with the engine; both expose the same `exists` / `read_text` /
`read_bytes` / `list` surface, so the rest of the engine stays backend-agnostic.

| Backend | Scope | Meaning |
|---------|-------|---------|
| `FilesystemResourceSource` | Authoring | Loose files under a host directory. The manifest's `resources.src` names the root. |
| `PackResourceSource` | Distribution (#109) | A single `resources.pak` archive (see below). Discovered automatically at startup. |

**Discovery (issue #109).** At startup the engine looks for `resources.pak`:

1. The path passed via `--pak <path>` (override), if any.
2. Next to the running executable (`/proc/self/exe` on Linux; `argv[0]` parent
   elsewhere as a best-effort fallback).
3. The current working directory.

If a pak is found, the engine reads the manifest from `game.yaml` inside the
archive and ignores the manifest path argument. If no pak is found, startup
falls back to the loose-files workflow: the CLI argument names the manifest
file on disk, and `resources.src` (from that manifest) names the resource root
directory.

The pak file format (`engine/core/pack_format.hpp`) is a flat list of files
keyed by logical path. The TOC lives at the end of the file so the packer can
stream payloads then patch a small header. Payload bytes are XOR-obfuscated
with a per-archive seed plus a per-file path hash; the goal is to defeat
casual snooping at scripts (Lua, YAML) in a hex editor, not cryptography. PNG
and MP3 stay compressed natively, so no extra compression is applied.

The packer lives at `tools/pack/pack.py`:

```
python tools/pack/pack.py <resources_root> <resources.pak>
```

Both backends are stream-and-cache safe: the `ResourceCache` keeps decoded
textures, fonts (loaded via `loadFromMemory` so the bytes outlive the font),
sound buffers, and shader programs alive for the cache's lifetime, and
`MusicPlayer` streams the current track from memory through
`openFromMemory` against the cache's persistent buffer.

All game assets are referenced by logical paths relative to the resource root,
for example:

```text
backgrounds/study.png
fonts/DepartureMono-Regular.otf
rooms/study.yaml
anims/hero_body.anim.yaml
```

The resource layer resolves logical paths to bytes. Game code and scripts shall
not construct platform-specific filesystem paths.

## Coordinate systems and display pipeline

The engine uses three coordinate spaces.

| Space | Meaning |
|-------|---------|
| World | Room-sized coordinate system for room geometry, points, hotspots, objects, and avatars. A room may be larger than the virtual resolution. |
| Virtual | Fixed design coordinate system declared by the game manifest. UI such as the SCUMM panel, speech, and dialog options live here. |
| Window | Actual physical OS window or fullscreen output. This may vary per machine. |

The transform chain is:

```text
world ──camera──▶ virtual ──letterbox scale──▶ window
```

The camera is a world-space rectangle the size of the **scenery viewport** — the
room-view layout's top region (~85% of the virtual height; the SCUMM panel
occupies the remaining ~15%), not the full virtual resolution. Each frame the room
view updates the camera, clamps it to the room bounds, and maps the world into the
scenery viewport; the SCUMM panel and other UI then draw in virtual space in their
own regions beneath it, not as an overlay on top of the scenery. The engine then
scales the whole virtual image uniformly into the physical window while preserving
aspect ratio, filling unused space with bars.

Pointer input is mapped from physical pixels back into virtual coordinates first.
A pointer inside the scenery viewport is mapped on into world coordinates through
the inverse camera transform for room and hotspot hit testing; a pointer inside a
UI region such as the SCUMM panel is handled in virtual coordinates. The mapped
input is then dispatched to the focused scene.

Example: a 1280×720 virtual game on a 2560×1440 window scales exactly 2×. On a
3440×1440 ultrawide monitor, it scales 2× to fit the height, occupies 2560
physical pixels horizontally, and leaves pillarbox bars on the sides.

## Scene stack

The `SceneManager` owns a stack of scenes. The top scene is focused.

| Operation | Meaning |
|-----------|---------|
| `goto_scene(id)` | Replace the active scene and collapse overlays. |
| `push_scene(id)` | Push an overlay scene above the current one. |
| `pop_scene()` | Remove the top scene and return to the one below. |

By default, only the top scene receives input and updates. Scenes draw from bottom
to top. A pushed overlay may be opaque or transparent.

### Scene transitions (fade)

A full-screen `goto_scene` can fade to black and back, configured by a transition
duration on the `SceneManager` (0 = instant swap). When a duration is set, a queued
GOTO fades the screen out, swaps the stack **at black** (so the old scene's
`leave()` and the new scene's `enter()` run while nothing is visible), then fades
back in; input is ignored while a swap is committed. Overlays (`push_scene` /
`pop_scene`, e.g. the pause or settings menu) and quitting are **never** faded. The
fade is a black quad drawn over the whole window (letterbox bars included) after
the scenes; the timing lives in a small headless `ScreenFade` so it is
unit-testable without a window.


## Implementation guidelines

This section defines implementation rules for the first engine refactor/rewrite.
The goal is not to over-engineer the engine, but to prevent the prototype from
growing again through accidental architecture.

The design document is the source of truth. Existing prototype code may be reused
when it matches the target design, but it shall not constrain the design.

### Build vertical slices first

Implementation should proceed through playable vertical slices, not isolated
subsystems.

A useful milestone is a minimal game with:

- one manifest-driven startup flow;
- one title scene;
- one room scene;
- two or three rooms loaded from YAML + Lua;
- a controllable player avatar;
- hotspots;
- the SCUMM command panel;
- inventory;
- one dialog;
- one simple puzzle spanning at least two rooms.

A subsystem is considered useful only when it helps make that slice playable.


### Separate engine, games, tests, and experiments

The repository should make the boundary between the engine and each executable
explicit. A standard game should eventually be authored without compiling new
C++ code, but the build layout shall still allow games, tests, experiments, and
custom scenes to have their own small C++ entry points when needed.

Recommended project layout:

```text
.
├── lib/
│   ├── include/engine/      # public engine headers
│   ├── src/                 # engine implementation
│   └── CMakeLists.txt       # engine library target
├── games/
│   ├── themummy/
│   │   ├── CMakeLists.txt   # example game executable
│   │   ├── main.cpp         # minimal entry point
│   │   └── data/            # manifest, Lua, YAML, assets
│   └── CMakeLists.txt
├── experiments/
│   ├── ...                  # throwaway or exploratory executables
│   └── CMakeLists.txt
├── tests/
│   ├── ...                  # unit/integration tests
│   └── CMakeLists.txt
└── CMakeLists.txt           # top-level build
```

The engine target owns the reusable implementation. Games, tests, and experiments
link against the engine target. During the first implementation phase, compiling
executables directly against the engine sources is acceptable if it keeps the
build simple, but this is a build-system convenience, not a design dependency.

Each game, test, or experiment may provide:

- a minimal `main.cpp` that initializes the engine and selects a manifest;
- its own game data: Lua scripts, YAML files, assets, dialogs, and room files;
- optional custom `Scene` subclasses when the interaction falls outside the
  standard point-and-click model;
- its own CMake target.

The top-level `CMakeLists.txt` should include one CMake file per group
(`lib`, `games`, `tests`, `experiments`) and each group should define its own
sub-targets. This keeps the common engine build separate from the executables
that exercise it.

Example target structure:

```text
pac_engine          # engine library
pac_themummy        # example game executable
pac_room_tests      # test executable
pac_dialog_tests    # test executable
pac_experiment_x    # exploratory executable
```

The build shall support Ubuntu and Windows builds on the host platform being
used. Cross-compilation is not required. Platform-specific differences should be
contained in CMake options, dependency setup, or small platform adapters, not in
game scripts.

Tests should be first-class build targets, written with the doctest framework
(single-header, registered with CTest). When supported by the compiler and
platform, the test configuration should enable sanitizers such as
AddressSanitizer and UndefinedBehaviorSanitizer to catch leaks, memory errors,
and undefined behavior early. Sanitizers are development tools and are not
part of release game builds.

This layout supports the long-term scripting-only authoring goal without making
it artificially strict during engine development. A game that needs only standard
point-and-click behavior should not need custom C++ code; a game that needs a
custom mini-game or experiment may still add a small executable or custom scene in
a controlled place.

### Dependencies and acquisition

| Dependency | Kind | Linux | Windows |
|------------|------|-------|---------|
| SFML 2.6 | compiled | apt `libsfml-dev` (`find_package(SFML 2.6)`) | vcpkg `sfml` |
| Lua 5.4 | compiled | apt `liblua5.4-dev` | vcpkg `lua` |
| yaml-cpp | compiled | apt `libyaml-cpp-dev` (`find_package(yaml-cpp)`) | vcpkg `yaml-cpp` |
| sol2 | header-only | CMake `FetchContent` (pinned) | CMake `FetchContent` (pinned) |
| doctest (tests) | header-only | CMake `FetchContent` (pinned, with vendored fallback) | CMake `FetchContent` (pinned, with vendored fallback) |
| micropather (design-for, unused) | vendored if a grid A* is adopted | `third_party/micropather/` | `third_party/micropather/` |

Acquisition rules:

- Compiled libraries come from the system package manager — apt on Linux,
  vcpkg on Windows. CMake picks them up through `find_package`, so a single
  CMake invocation works on both platforms once the packages are present.
- Header-only libraries (sol2, doctest) are pulled with CMake `FetchContent`
  at a pinned version on both platforms, so there is no per-platform
  divergence and no dependency on an apt package or vcpkg port.
- doctest may additionally be **vendored** as a single-header fallback under
  `tests/_vendor/`. The build prefers the vendored copy when present; this is
  useful in offline / air-gapped environments where git is unreachable.
- micropather is a design-for alternative, vendored under `third_party/` only if
  a coarse-grid A* is ever adopted; it ships as bare source files without a CMake
  target. The MVP ships a built-in visibility-graph A* behind the `find_path`
  interface, so micropather is not currently vendored (see
  [pathfinding](03-2d-game-concepts.md)).

### Keep `RoomScene` as an orchestrator

`RoomScene` coordinates the room-view state, but it shall not become the owner of
all point-and-click logic.

It should delegate to smaller components:

| Component | Responsibility |
|-----------|----------------|
| `RoomRuntime` | Loaded room data, current room state, zones, points, hotspots, avatars. |
| `RoomLoader` | Load `<room>.yaml` and `<room>.lua`, bind static data to script handlers. |
| `RoomRenderer` | Draw background layers, regions, objects, avatars, speech, and debug overlays. |
| `CommandBuilder` | Maintain the SCUMM command-building state machine. |
| `CommandDispatcher` | Convert a completed command into movement + Lua handler invocation. |
| `ScummPanel` | Render verbs, inventory pages, and command bar; translate UI clicks into intents. |
| `InventoryModel` | Store inventory items and expose inventory operations. |
| `DialogRuntime` | Run dialog trees and manage dialog-state transitions. |
| `SpeechManager` | Display spoken lines and later connect them to voice-over. |
| `AvatarController` | Move avatars, face targets, play actions, and report movement completion. |

`RoomScene` may own these components and coordinate them, but it should not absorb
their logic.

### Keep the command system independent from the UI

The SCUMM panel is one way to build commands. It is not the command system.

The command system shall accept a command model such as:

```cpp
struct Command {
    Verb verb;
    ObjectRef param1;
    std::optional<ObjectRef> param2;
};
```

This allows tests, debug tools, scripted commands, and future input methods to
reuse the same dispatch path.

### Keep static data out of Lua

Lua scripts define behavior. They should not define room geometry, visual layout,
asset paths, or hotspot polygons.

Static authoring data belongs in YAML:

- manifest;
- cast;
- room layout;
- geometry;
- hotspots;
- affordances;
- background layers;
- regions;
- object placement;
- avatar start positions.

Lua may refer to these entities by id and decide what happens when the player
interacts with them.

### Make persistent state explicit

Persistent game state shall live only in explicit engine-managed stores:

- current scene and current room;
- player/avatar position and orientation;
- inventory contents;
- global world state;
- per-room state;
- declared object or region state.

Lua local variables, closures, and currently running coroutines are transient and
shall not be part of save/load. Scripts must write every persistent fact through
the explicit state, inventory, region, or dialog-flag stores.

Scripts that need persistent information must use `get_state` / `set_state` or
`get_room_state` / `set_room_state`.

All of this lives in one engine-owned `GameState`, independent of which scene or
room is currently loaded — room instances are transient views over it. `GameState`
is the entire save payload:

```text
GameState {
  current_scene_id   : string
  room_view: {
    current_room_id  : string
    player: { x, y, facing, appearance_id }
  }
  inventory          : list<item_id>
  global_state       : map<string, Value>                  # set_state
  room_state         : map<room_id, map<string, Value>>    # all rooms, not only loaded
  region_states      : map<room_id, map<region_id, state_id>>
  dialog_flags       : map<dialog_id, consumed once-options>
  save_version       : int
}
```

- `room_state[room_id]` persists whether or not the room is loaded;
  `set_room_state` writes to the current room's entry, created lazily and never
  cleared on unload.
- `region_states` persists per room. A region's YAML `initial` state applies on a
  new game or the first time a room is entered; on load-game the persisted
  `region_states` win over `initial`. The `on_load` hook that runs after restore
  observes the restored states, not `initial`.
- `Value` is a scalar (boolean, number, string); see the scripting API state
  rules.
- Consumed `once` dialog options are keyed by `(node_id, option_index)` for the
  MVP; a stable explicit option id is design-for.
- Saving serializes `GameState` to YAML in the per-user location. Loading
  deserializes it, enters `current_scene_id`, and — for `RoomScene` — loads
  `current_room_id` and reseats the player at the saved position.
- In the MVP, saving (the menu save and the room-change autosave) happens only
  while `RoomScene` is active; `current_scene_id` is still recorded so saving from
  other scenes is a design-for extension rather than a format change.
- On restore, `on_load` runs again to rebuild transient room setup from persisted
  state; there is no separate `on_restore` hook.

Save format, slots, and autosave policy are specified in
[R8](01-engine-requirements.md).

### Prefer simple MVP behavior over speculative generality

When a feature has both an MVP version and a design-for version, implement the MVP
version first.

| Area | MVP | Design-for |
|------|-----|------------|
| Resources | Filesystem backend + cache | Packed archive. |
| Backgrounds | Layers and z-order | Parallax and shaders. |
| Sprites | Animated sprites with anchors | Advanced composite sprites. |
| Movement | Visibility-graph pathfinding around obstacles | Navmesh/funnel smoothing, dynamic obstacles. |
| Dialogs | Text choices | Voice-over synchronization. |
| Settings | Minimal display/audio support | Complete settings UI. |
| Save/load | Explicit state snapshot, three manual slots, one autosave slot | Thumbnails. |

Do not implement a design-for feature unless it is needed by the current vertical
slice or it prevents a known architectural dead end.

### Use debug tools as part of the MVP

Debug tooling is part of productivity, not polish.

The engine should support development flags for:

- showing walkable areas;
- showing obstacles;
- showing hotspots;
- showing approach points;
- showing avatar anchors;
- showing z-order values;
- showing current command-builder state;
- showing room/world state;
- reloading the current room script;
- jumping to a room;
- adding/removing inventory items.

These flags belong in development configuration, not in player-facing settings.

The visualization overlays are implemented (#37) as in-room layers, gated by the
manifest `development.edit_mode` flag (the master switch — when off, overlays never
render and the toggle keys are inert). The `show_*` flags seed each layer's initial
state; in a dev session they are toggled at runtime:

| Key | Layer |
|-----|-------|
| `F1` | walkable area (green) + obstacles (red) — `show_walkboxes` |
| `F2` | hotspot areas (cyan) + names + approach points (yellow) — `show_hotspots` |
| `F3` | avatar anchors (magenta cross) + z values — `show_anchors` |
| `F4` | command-builder state + room/world/region state HUD — `show_state` |

The action-style dev tools (#38) are also `edit_mode`-gated and bound to function
keys, but act only from the COMMAND view state (so they can't disrupt a running
dialog or the pause menu):

| Key | Action |
|-----|--------|
| `F5` | Reload the current room's behavior script — `on_unload`, cancel + reopen the room scope (reaping its tasks), re-evaluate `rooms/<id>.lua`, `on_load`. Keeps the room data, player pose, and persistent state; additionally gated by `allow_room_reload`. |
| `F6` | Jump to the next room (cyclic) found in the rooms directory. |
| `F7` | Add the first defined inventory item the player isn't holding. |
| `F8` | Remove the most recently added held inventory item. |

The reload follows the script-task-ownership rules: cancelling the room scope ends
its coroutines without running Lua cleanup, so authored teardown belongs in
`on_unload`.

### Resource profiling (#112)

The engine targets hardware comparable to other modern point-and-click games
(Return to Monkey Island, Thimbleweed Park). To keep an eye on resource usage
during development, the manifest `development.profiling` flag enables a
profiling mode in the core run loop. When on, the harness:

- measures **frame timing** every frame — the full frame-to-frame time (vsync
  included → real fps/pacing) and the update+draw "work" time before the vsync
  wait (→ CPU/draw headroom). These are the cheapest honest CPU proxy;
- counts **shader work** every frame — the number of fragment-shader passes
  (single-effect fast-path draws plus `ShaderChain` ping-pong passes) and the
  live VRAM of the `ShaderChain` render-target pools. These come from process-wide
  counters in `render_stats.hpp` (core), which the gfx layer reports into — so the
  core profiler reads them without breaking the layer dependency rule;
- samples **RAM** (resident set size, via `/proc` on Linux) and the
  **resource-cache footprint** every `profiling_interval` seconds (default
  `2.0`): an upper-bound texture-VRAM estimate (`width*height*4`, RGBA8) plus
  live texture / shader / sound / font counts;
- logs a one-line summary on each sample and writes a **per-scene aggregate
  report** to the per-user data dir (`profiling-report.txt`) at exit, so authors
  can see which scene drives peak VRAM / shader passes / frame time.

VRAM is the metric of interest: every cached `sf::Texture` is a live GPU texture,
so the cache's texture footprint bounds VRAM (the off-screen shader buffers are
tracked separately as RT VRAM). The report's per-scene peaks tell content authors
when a smarter load/unload-per-room strategy or a shader-budget guideline is
warranted. **Caveat:** when the GPU is the bottleneck, the work-time metric
under-reports the real frame cost (the GPU wait lands in the buffer swap, after
the work timer) — read frame time, not work time, in that regime. Profiling is
development-only and never a player setting.

### Keep layer boundaries strict

The dependency rule is part of the design.

- Core shall not know about point-and-click concepts.
- Generic 2D shall not know about verbs, inventory, rooms, or dialogs.
- Point & Click may use Generic 2D and Core.
- Game scripts may use only the Lua API.
- Standard games shall not require C++ changes.

If a feature seems to require violating a layer boundary, the design should be
reviewed before implementation continues.

### Keep logic separable from rendering

Game logic shall not depend on a live graphics context. The following modules
must be constructable and exercisable in a headless test with no window, so they
can be unit-tested and reused outside the renderer:

- `pac::geom` — points, polygons, point-in-polygon, pathfinding;
- the command model and command-builder state machine;
- the dialog runtime — tree walking, `when` / `run` / `once` / `silent`, `END`;
- the state stores — global, per-room, region, and dialog flags;
- the inventory model;
- the YAML loaders — manifest, room, cast, anim, dialog — producing validated
  data structures.

Rendering (`ZDrawable::draw`, SCUMM panel layout, speech presentation) may require
a graphics context and is exercised through the example game and edit mode rather
than unit tests. This separation is the practical meaning of the layer rule:
point-and-click logic must not reach for `sf::RenderTarget`. The list above is a
starting contract and may grow or shrink as the engine evolves.

### Treat examples as test candidates

Every example in the design document should be implementable.

When possible, examples should become regression tests or sample content in the
example game. The command-builder transition table and the dialog execution steps
in [Third-person point & click concepts](04-point-and-click-concepts.md) are
natural table-driven tests. If an example cannot be implemented cleanly, either
the design or the example is wrong.

### Avoid hidden behavior

Implicit behavior should be minimized.

Acceptable implicit behavior:

- default fallback responses for missing verb handlers;
- default standing animation after an action;
- default room entry point when no specific entry point is provided.

Risky implicit behavior:

- automatically inventing object interactions;
- silently accepting invalid commands;
- saving state that was not explicitly declared;
- resolving missing resources without visible diagnostics;
- allowing Lua scripts to depend on physical filesystem paths.

In development builds, authoring errors should fail loudly with clear diagnostics.
