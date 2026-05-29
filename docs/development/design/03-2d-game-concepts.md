# 2D game concepts

This section defines concepts common to several types of 2D games. They make up
the Generic 2D layer and parts of Core. Point-and-click-specific behavior is
specified in [Third-person point & click concepts](04-point-and-click-concepts.md).

## Initialization, main loop, and scenes

The engine harness — startup, main loop, window, aspect scaling, and scene
transitions — is fixed engine code. Authors do not modify it to create a standard
game. A scene is the C++ extension point for behavior outside the standard engine
model, such as a custom mini-game.

### Main loop

The engine uses a fixed-timestep loop. Each iteration performs:

1. poll input events;
2. map physical coordinates to virtual coordinates;
3. dispatch events to the focused scene;
4. update the focused scene using a fixed `dt`;
5. draw the scene stack.

The fixed timestep is 60 Hz (`dt = 1/60 s`). The loop accumulates elapsed wall-clock
time and runs zero or more fixed `update(dt)` steps per rendered frame, so gameplay
stays deterministic and refresh-rate independent: a 144 Hz display draws more often
but still advances the simulation at 60 Hz, and a slow frame catches up with
several steps (capped to avoid a spiral of death).

### Scene contract

Every scene implements:

| Method | Called | Purpose |
|--------|--------|---------|
| `enter()` | Once, when becoming active or pushed | Acquire resources, initialize state, start music. |
| `leave()` | Once, when replaced or popped | Release scene-owned resources, stop scene-owned effects. |
| `handle_event(event)` | Per input event, focused scene only | Turn input into scene intent. |
| `update(dt)` | Fixed timestep, focused scene only | Advance scene state. |
| `draw(target, viewport)` | Each frame | Render in virtual coordinates. |

A scene is constructed with:

- its manifest `parameters`;
- the `EngineContext` of shared services (display/view, resources, audio,
  settings, scripting, scene manager, diagnostics, dev flags), specified in
  [the architecture overview](02-architecture-overview.md).

## Resources

The resource layer provides one way to access every game asset:

- textures;
- fonts;
- music;
- sound effects;
- shaders;
- Lua scripts;
- YAML files;
- atlas metadata;
- dialog and room data.

Assets are named by logical paths relative to the game's resource source. A
logical path is not a filesystem path.

Examples:

```text
backgrounds/study.png
rooms/study.yaml
scripts/intro.lua
fonts/DepartureMono-Regular.otf
```

### Resource source interface

A resource source opens a readable byte stream for a logical path.

| Backend | Scope | Meaning |
|---------|-------|---------|
| Filesystem source | MVP | Reads loose files under the resource root. |
| Packed archive source | Design-for / post-MVP | Reads assets from a single indexed archive. |

Game code, engine code above the resource layer, and scripts shall use logical
paths. They shall not concatenate platform-specific filesystem paths.

### Caching

Fully-loaded assets are cached by logical path. Repeated requests for the same
asset return the same loaded instance.

| Asset kind | Loading behavior |
|------------|------------------|
| Texture | Fully loaded and cached. |
| Font | Fully loaded and cached. |
| Sound buffer | Fully loaded and cached. |
| Shader | Fully loaded and cached. |
| YAML/Lua data | Loaded through the resource layer; caching depends on subsystem needs. |
| Music | Streamed as one active track; not cached as a full buffer. |

### Packed archive design-for

The packed backend is not required for the MVP, but the resource interface shall
allow it. A packed archive may be implemented as either:

- a custom file with an index mapping logical path to offset, size, and flags;
- a standard archive format such as zip.

The exact format is an open question. The MVP shall not assume loose filesystem
files outside the resource layer.

## Spritesheets, animated sprites, and composite sprites

SFML provides textured sprites. The engine builds three concepts on top:

```text
Spritesheet  →  AnimatedSprite  →  CompositeSprite
```

## Spritesheet

A spritesheet is an atlas texture plus YAML metadata describing named frames.

Each frame has:

| Field | Meaning |
|-------|---------|
| `id` | Stable frame identifier. |
| `rect` | Rectangle inside the atlas. |
| `source_rect` | Optional original source rectangle before packing. For traceability only. |
| `anchors` | Named frame-local points used for pivots and attachments. |

Example:

```yaml
image: body.png
size: { width: 1024, height: 1024 }

sprites:
  - id: frame_000
    rect:        { x: 109, y: 2,  width: 107, height: 236 }
    source_rect: { x: 121, y: 38, width: 107, height: 236 }
    anchors:
      walking_pivot: { x: 54, y: 236 }
      head_mount:    { x: 53, y: 42 }
```

Anchors are frame-local pixels with origin at the frame's top-left corner.

## AnimatedSprite

An `AnimatedSprite` plays named sequences of frames from a spritesheet. It
supports the usual sprite rendering operations:

- translation;
- rotation;
- scale;
- opacity;
- blend mode;
- shader;
- sequence selection;
- looping and non-looping playback.

### Positioning by anchor

An animated sprite designates one anchor as its pivot. Setting the sprite's
position places the current frame's pivot at that world position. This keeps feet,
hands, or other anchors stable across frames without per-frame offset logic.

Example animation file:

```yaml
# body.anim.yaml
version: 1
spritesheet: body.yaml
pivot: walking_pivot

sequences:
  idle_down:
    loop: true
    frames:
      - { sprite: frame_000, duration: 0.12 }

  walk_down:
    loop: true
    frames:
      - { sprite: frame_001, duration: 0.10 }
      - { sprite: frame_002, duration: 0.10 }
      - { sprite: frame_003, duration: 0.10 }
      - { sprite: frame_004, duration: 0.10 }

  pick_up_down:
    loop: false
    frames:
      - { sprite: frame_010, duration: 0.08 }
      - { sprite: frame_011, duration: 0.08 }
```

### Sequence completion

A non-looping sequence shall support both:

| Form | Purpose |
|------|---------|
| Callback | C++ code can be notified when playback finishes. |
| Blocking script call | Lua coroutine can wait until playback finishes. |

Example Lua usage:

```lua
avatar("player"):play_until_end("pick_up_down")
```

The call yields the coroutine and resumes it when the sequence ends.

## CompositeSprite

A `CompositeSprite` is a hierarchy of animated sprites attached through anchors.
It is the default model for composed characters.

Example: a body sprite exposes a `head_mount` anchor. A head sprite uses its
`neck_pivot` anchor. Each frame, the head's `neck_pivot` is aligned to the body's
`head_mount`.

```yaml
# hero.composite.yaml
root:
  id: body
  animation: anims/hero_body.anim.yaml

children:
  - id: head
    parent: body
    animation: anims/hero_head.anim.yaml
    parent_anchor: head_mount
    child_anchor: neck_pivot
```

Children may animate independently of parents. For example, the body can walk
while the head talks or turns.

Composite sprites shall expose the same high-level rendering operations as
animated sprites where meaningful: position, scale, opacity, and draw.

## Shaders

A shader is an optional GLSL fragment program applied when a drawable is rendered,
for effects the static art can't carry (colour grade, lighting, distortion, CRT,
etc. — see the taxonomy in issue #56). Shaders are **declared in data, not C++**:
the common case (uniform parameters from YAML, plus a small set of engine-provided
uniforms) needs no engine code per shader.

This is a generic 2D concept (`pac::gfx`): the data types live there, loading is a
`ResourceCache` resource, and each genre layer wires them onto its drawables. The
point-and-click slice applies them to room **background layers, regions, objects,
and avatars** (PCs and NPCs; declared on the cast appearance — design 04). A
walk-behind currently draws unshaded (it's a textured polygon, not a sprite, so it
needs a polygon-aware multi-pass path that is out of scope for now).

### Loading

Fragment source is a logical resource (e.g. `shaders/water.frag`), compiled and
cached by the resource layer like a texture. Shader source paths are resolved
**relative to the resources root** — shaders are shared across rooms — unlike the
room-relative image paths of layers/regions/objects.

A shader that fails to compile, is missing, or runs on a GPU without shader support
is reported **once** and the drawable renders unshaded; the game never crashes for
a bad shader. (Development builds make the failure prominent in the log.)

### Parameters

Each shader carries a set of named uniform `params` from YAML. The engine binds
the primitive GLSL types generically, so most shaders need no C++:

| YAML value | Uniform type |
|------------|--------------|
| `true` / `false` | `bool` |
| a number (e.g. `0.5`, `4`) | `float` |
| `{type: int, value: 8}` | `int` |
| a 2 / 3 / 4-number sequence (e.g. `[0.5, 0.3]`) | `vec2` / `vec3` / `vec4` |
| `{type: <bool\|int\|float\|vec2\|vec3\|vec4>, value: ...}` | explicit override |

A bare integer literal becomes a `float` (the overwhelmingly common uniform type);
use the explicit `{type: int, ...}` form for a true integer uniform.

### Reserved uniforms

The engine sets these automatically each frame when drawing a shaded item; declare
only the ones a shader uses. The engine inspects each shader's source on load and
sets a reserved uniform only when the shader actually declares it, so an effect that
needs none of them (e.g. a static colour grade) costs nothing and logs nothing:

| Uniform | Type | Meaning |
|---------|------|---------|
| `texture` | `sampler2D` | the drawable's own texture (SFML's current texture) |
| `u_time` | `float` | seconds since the scene began — for animated effects |
| `u_resolution` | `vec2` | the drawable's texture size in pixels |

Author params must not use the `u_` prefix or the name `texture`, which are reserved
for this convention.

### Shaders that need C++ (design-for)

Some effects need uniforms the declarative path can't express — a light that tracks
the avatar, a value derived from game state. These opt in via a **controller**: a
named C++ hook, registered by the game, that sets custom uniforms each frame. The
drawable references it by id:

```yaml
shader: { source: shaders/spot.frag, controller: avatar_light }
```

The `controller` field parses today and is **reserved**; the registry that runs it
is design-for and not yet implemented (a referenced controller is logged once and
the drawable draws unshaded until then). The declarative path stays untouched by it.

### Multiple shaders

A drawable may declare an ordered `shaders:` list as well as / instead of a single
`shader:`. The engine runs them as a multi-pass pipeline: render the drawable into
an off-screen `sf::RenderTexture`, then ping-pong through each enabled pass, with
the next pass sampling the previous pass's output as `texture`. The two pooled
render textures grow monotonically to fit the largest drawable seen, so a steady
scene reaches a steady allocation. Single-shader keeps the direct-draw fast path
(no RT). A pass that fails to load (missing source, unsupported GPU, compile
error) is logged once by the resource cache and skipped; the rest of the chain
still runs.

### Region shader inheritance

A region with `over: <layer>` is conceptually a swappable patch of that layer, so
it inherits the layer's shader stack: the layer's effects run first (so the
region matches the surrounding background) and the region's own effects run on
top. A region without `over:` uses only its own shaders.

### Avatar shaders

Avatars (PCs and NPCs) declare their shader stack on their **cast appearance**
(`cast.yaml` — see design 06). Every avatar that uses that appearance picks up
the stack at construction. The render path is the same chain used for layers /
regions / objects: the current animation frame is baked into the chain's RT,
passes apply, the final RT is blitted at the avatar's walking-pivot position with
the avatar's perspective scale (so the pivot stays planted under the feet).
Authoring tip: `u_resolution` for an avatar shader equals the **current frame
size in pixels** (not the spritesheet atlas), so author params in those units.

### Worked example

A subtle warm colour grade plus a soft vignette over a background layer — the
multi-pass chain in action:

```glsl
// shaders/ambient_grade.frag
uniform sampler2D texture; // engine-bound
uniform vec3 tint;         // per-channel multiplier
uniform float strength;    // 0 = original, 1 = fully graded
void main() {
    vec4 px = texture2D(texture, gl_TexCoord[0].xy);
    gl_FragColor = vec4(mix(px.rgb, px.rgb * tint, strength), px.a) * gl_Color;
}
```

```yaml
# in a room's background layer (study.yaml uses this exact stack)
shaders:
  - source: shaders/ambient_grade.frag
    params: { tint: [1.15, 1.0, 0.82], strength: 0.6 }
  - source: shaders/vignette.frag
    params: { strength: 0.45, inner: 0.5, outer: 1.0, color: [0.05, 0.02, 0.0] }
```

`games/themummy/data/shaders/` ships a small library of stock effects you can use
or adapt: `ambient_grade`, `candle_flicker`, `color_grade`, `spotlight`,
`omnilight`, `dust_particles`, `horizontal_displacement`, `vignette`. A dedicated
shader-showcase game (per issue #56) is a planned second iteration; this section
plus those examples are its reference.

## Lua integration

The engine embeds a single Lua state and runs game logic as cooperative
coroutines. Scripts can write straight-line logic using blocking-looking calls
without freezing the main loop.

Example:

```lua
spawn(function()
  talk("hero", "I should check the drawer.")
  avatar("hero"):move_to("at_drawer")
  avatar("hero"):play_until_end("open_down")
  wait(0.3)
  talk("hero", "Empty. Of course.")
end)
```

Each blocking call yields to the script scheduler. The main loop continues to
update and draw.

### Script task states

| Wait kind | Resumes when | Script call |
|-----------|--------------|-------------|
| `Ready` | Next scheduler update | Newly spawned task or explicit yield. |
| `Timer` | N seconds elapse | `wait(seconds)` |
| `Event` | Named event is emitted | `wait_event(name)` / `emit(name)` |
| `AvatarMove` | Avatar finishes moving | `avatar(id):move_to(target)` |
| `Animation` | Animation sequence finishes | `avatar(id):play_until_end(sequence)` |
| `Speech` | Speech line or text page finishes or is skipped | `talk(speaker, text_or_line_id)` / `show_text(text)` |
| `Done` | Never | Task finished. |

### Script task scopes and cancellation

Each task belongs to one script scope. `spawn(fn)` inherits the scope of the hook,
handler, dialog callback, or scene script that calls it.

| Spawn site | Default scope | Cancelled when |
|------------|---------------|----------------|
| Room lifecycle hook or hotspot handler | Current room | `change_room`, room reload, `RoomScene` leave, load restore. |
| Dialog callback / option `run` | Current dialog | Dialog ends, is cancelled, or `RoomScene` leaves. |
| `StoryText` / cutscene script | Current scene | Cutscene finishes, is skipped, or scene is replaced. |
| Engine-owned global startup work | Current game session | `RoomScene` leaves, new game starts, save is loaded, or process exits. |

Cancellation is immediate from the scheduler's point of view: a cancelled task is
removed and is never resumed. The engine detaches any waiter associated with the
task:

| Wait kind | Cancellation behavior |
|-----------|-----------------------|
| `Timer` / `Event` | Remove the wait; scoped events from later rooms do not resume old tasks. |
| `Speech` | Hide or skip the active speech line. |
| `Animation` | Stop waiting; the animation falls back according to avatar defaults. |
| `AvatarMove` | Stop movement if the avatar belongs to the cancelled scope; player re-seat rules still apply during room changes. |
| Camera tween | Stop the tween and restore the room camera policy. |
| Room transition | Once begun, finish the engine transition, but do not resume a cancelled caller. |

The MVP does not run Lua finalizers when cancelling a task. Room cleanup belongs
in `on_unload`; dialog cleanup belongs in `on_exit`. Development diagnostics may
log cancelled task ids and their wait kinds.

### C++ to Lua

The engine invokes Lua hooks for:

- scene events;
- room lifecycle;
- zone enter/exit;
- hotspot verb handlers;
- dialog callbacks.

### Lua to C++

Lua scripts use the registered API specified in
[The scripting API](05-scripting-api.md). Scripts shall not rely on C++ internal
classes or memory ownership.

## Geometry

The geometry module (`pac::geom`) is a small foundational module with no game or
genre dependencies.

### Types

| Type | C++ | YAML / Lua representation |
|------|-----|---------------------------|
| Point | `pac::geom::Point` / `sf::Vector2f` | `{ x: 10, y: 20 }` |
| Polygon | `std::vector<Point>` | Ordered list of points |

The module shall support serialization to and from YAML and Lua tables.

### Operations

| Operation | Purpose |
|-----------|---------|
| `distance(a, b)` | Proximity checks. |
| `distance_squared(a, b)` | Threshold tests without square root. |
| `point_in_polygon(p, poly)` | Hit testing, zones, walkable checks. |
| `segment_intersection(a, b, c, d)` | Navigation and tool support. |
| `polygon_bounds(poly)` | Debug rendering and spatial indexing. |

### Walkable area and obstacles

A room's navigable space is inside its walkable polygon and outside all obstacle
polygons.

```text
is_walkable(p) = point_in_polygon(p, walkable)
                 && !point_in_any_polygon(p, obstacles)
```

A room has one walkable polygon and zero or more obstacle polygons.

### Pathfinding

Pathfinding returns an ordered list of waypoints from a start point to a
destination, staying inside the walkable polygon and outside all obstacles.

The engine wraps the pathfinder behind a stable interface:

```text
find_path(start, destination) -> list<Point>
```

The MVP ships a built-in **visibility-graph** A* pathfinder, computed per query
with no room-load preprocessing:

- **Nodes** are the start, the destination, and one node inset a few pixels off
  each walkable concave corner and each obstacle corner, on the free-space side.
  The inset keeps graph edges clear of the boundary instead of grazing it (where
  the inside/outside test is ambiguous) and is what lets a path round both
  obstacles and concave walkable pockets (e.g. an L-shaped room).
- **Edges** connect any two nodes whose connecting chord stays in free space: its
  midpoint is walkable and outside every obstacle, and it crosses neither the
  walkable boundary nor an obstacle edge. Edge cost is euclidean distance.
- **Search** is A* with the euclidean distance to the destination as an
  (admissible, consistent) heuristic, so the result is a shortest path through
  the corner graph.

Contract details that callers rely on:

- The returned list excludes the start and ends at the destination; a clear line
  of sight short-circuits to a single waypoint.
- A start or destination outside the walkable area is clamped to the nearest
  reachable point, so a click just past the boundary still routes.
- The result is **never empty**: when no corner route exists it falls back to a
  straight walk truncated at the first boundary/obstacle, yielding one reachable
  waypoint. An empty or degenerate walkable polygon is treated as ungated and
  yields `{destination}`.
- Obstacles are assumed **convex** — the midpoint chord test is exact for them;
  author a concave obstacle as a union of convex polygons.

The avatar walks the returned waypoints in order, turning at each corner.

A coarse-grid A* (e.g. the **micropather** library, vendorable under
`third_party/` since it is not packaged for apt or vcpkg), a funnel / navmesh
smoother, and dynamic obstacles are design-for alternatives behind the same
`find_path` seam. None are required by the MVP, and micropather is not currently
vendored.

## Settings

Player-facing settings are preferences chosen by the player and persisted across
runs. They are distinct from the game manifest and distinct from development
flags.

### Stored settings

| Setting | Scope | Notes |
|---------|-------|-------|
| Display mode | MVP | Fullscreen/windowed and physical window size. |
| Music volume | MVP | Applied to the music player. |
| SFX volume | MVP | Applied to the sound player. |
| Language | MVP infra | UI-strings language, selected from the manifest `languages` map. MVP ships Spanish only; the selector and switch are in place for more. Per-language *game-content* files stay design-for. |
| Text speed | Nice-to-have | Useful for subtitle pacing. |

### Defaults and precedence

The manifest provides defaults. User settings override manifest defaults. On a
first run with no user settings file, manifest defaults are used; a corrupt file
also falls back to the defaults. Only keys present in the file are applied, so a
partial file still loads.

Settings are saved in a per-user **config** location (`settings.yaml`), not in the
resource root:

- `%APPDATA%\<id>\settings.yaml` on Windows;
- `$XDG_CONFIG_HOME/<id>/settings.yaml` or `~/.config/<id>/settings.yaml` on Linux;
- `Application Support/<id>` on macOS.

Save files use the same general per-user storage policy, under the per-user *data*
location. The settings file format is specified in
[06 — Data formats](06-data-formats.md#player-settings--settingsyaml).

### Settings scene

The settings UI is a scene pushed over the current scene, then popped back. This
makes the same settings available from the title screen or an in-game pause menu.

It offers a windowed-resolution selector, a fullscreen toggle, a language selector
(from the manifest `languages` map), and music/SFX volumes. Edits are made to a
**working copy**: display and language changes are *staged* and only take effect on
an explicit **APPLY** action (BACK / Esc discards them) — the window is never
recreated mid-edit. Audio volume previews live while editing and is restored on
BACK. APPLY commits the working copy to the settings service, swaps the active
language, requests any display change, and **persists** everything to the settings
file. This APPLY/BACK model matches player expectations and avoids recreating the
window on every keypress.

A display change does not happen in the scene: it is **requested** through the
`Display` service (`request_mode`), and the main loop recreates the OS window
between frames and reports the applied size/mode back to `Display`. This keeps the
window owned by the core harness while a genre-layer scene drives the change
through `EngineContext`. Because the **virtual resolution never changes** — only
the letterbox does — switching modes is transparent to the game (R6): gameplay
coordinates, geometry, and input mapping are unaffected.

The windowed sizes offered are aspect-matching multiples of the virtual resolution
that fit the desktop, so windowed mode never shows bars. Fullscreen uses the
**desktop's native video mode** (no mode switch) and letterboxes the virtual
resolution within it. Keeping the framebuffer at the desktop size is what keeps
input mapping correct in fullscreen: a video-mode switch can leave the OS pointer
in a different coordinate space, so clicks and avatar movement break.

## Music and sound

The engine exposes two global audio services.

| Service | Purpose |
|---------|---------|
| `MusicPlayer` | One streamed background track at a time. |
| `SoundPlayer` | Short overlapping sound effects. |

### Music player

The music player supports:

- `play(path, loop)`;
- `stop()`;
- replacing the current track by playing a new one;
- volume controlled by settings.

Lua API:

```lua
play_music("music/study.ogg", true)
stop_music()
```

### Sound player

The sound player supports short overlapping sounds, such as clicks, footsteps,
and object interaction sounds. Sound buffers are loaded through the resource
layer and cached, and played on a small pool of voices (oldest-free reused).

Each one-shot may set a per-call **volume** (0..1, scaling the global SFX volume)
and a stereo **pan** (-1 left .. 0 center .. +1 right). Panning is a simple stereo
balance that affects **mono** buffers only — a stereo clip plays as authored. A
position-aware/spatial mix (pan/volume derived from a source's world position
relative to the camera or player) is design-for.

Lua API:

```lua
play_sound("sfx/click.wav")            -- centered, full SFX volume
play_sound("sfx/door_open.ogg", 0.8)   -- quieter
play_sound("sfx/bird.ogg", 1.0, -0.6)  -- off to the left
stop_sounds()
```

### Packed resource note

`sf::Music` streams from a file. In packed mode, the selected music track may be
extracted to a temporary file and streamed from there. Other assets should load
from streams directly when possible.

