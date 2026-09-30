# SFML 3.1 migration

The engine now builds upstream SFML **3.1.0**, pinned to commit
`8835b6b955c96fa1ad1278a8dae6f05c4143effc`, through `cmake/pacSfml.cmake`.
The migration is tracked in [issue #210](https://github.com/nhorro/xadv2-engine/issues/210).
It remains unmerged until Fuera de Cuadro has been exercised against the migration branch.

## What the SFML 2 dependency patched

The previous dependency was `TheMaverickProgrammer/SFML_ANDROID_ES_2` at
`1ed82956eb8dfbfce09773274c95de92865ab5e1`, supplemented with official SFML 2.6.2
sources at `5383d2b3948f805af55c9f8a4587ac72ec5981d1`.
The configure-time patches and fork-specific renderer adapter are retired, not
ported blindly to a newer SFML tree. Their original code remains in Git history.

| Area | Previous modification and purpose | Migration decision |
| --- | --- | --- |
| GLES rendering | Fork's GLES2 draw implementation; EGL ES2 config; default-shader initialization; `textureEnabled` for untextured geometry | Upstream SFML owns rendering; remove `setDefaultShader` and shader-source rewriting. Android advanced rendering is deferred. |
| Blending | Separate RGB/alpha factors and equations to avoid dark fringes when compositing sprites and shadows through room render textures | Desktop uses upstream blending. Android compositing and shadow parity require device validation during renderer review. |
| Android surface lifecycle | Detach EGL context before surface destruction; clear destroyed window singleton; guard late activity callbacks; update old looper API | Remove source patches and use upstream lifecycle implementation. Exercise background/resume, orientation and exit on devices before claiming parity. |
| Hardware cursors | Backport official Xcursor ARGB cursor support to the older fork | Render a textured sprite last in window pixel coordinates. Retain authored hotspots, action variants, inversion, blink and hide requests; no platform cursor handles. |
| Audio codecs | Import and register official MP3 reader in the fork; use newer Android codec dependencies | Upstream SFML includes MP3 and uses miniaudio instead of OpenAL. Preserve music/ambience/voice service behavior for this iteration. |
| Font atlas | Separate older patch: in-place insertion, fixed 1024² atlas, disable atlas growth to avoid GLES1 texture-copy failures | This patch was already unused by the previous dependency module. Remove the obsolete file; evaluate SFML 3.1 text/atlas behavior on devices. |
| Windows dependencies | Fork-specific Freetype configuration and bundled OpenAL DLL staging | Use upstream SFML dependency acquisition and CMake transitive runtime DLL staging. |

## Consumer-facing changes

Games using SFML types through engine headers must compile against SFML 3:

- Events use `event.is<sf::Event::KeyPressed>()` and `event.getIf<T>()`;
  `pollEvent()` returns an optional event. Touch translation still emits move
  before click and ignores secondary touches while the primary touch is active.
- Keyboard/button/primitive/shader/status/blending enums are scoped.
- Rectangles contain `position` and `size`: `{{x, y}, {width, height}}`.
- SFML transforms take vector arguments and `sf::degrees(...)` for angles.
- `sf::Text` takes the font first; sprites and sounds require their texture or
  buffer at construction. Fonts open with `openFromMemory`.
- Texture/image/render-texture creation uses `resize`; smoke quads are expressed
  as two triangles, preserving their winding, UVs and colors.
- Minimum CMake is 3.24. Linux requires HarfBuzz and XInput development packages;
  OpenAL is no longer a dependency. Windows still obtains Lua/yaml-cpp via vcpkg.
- Installed consumers discover SFML 3.1 through `find_dependency`; source consumers
  use the same pinned upstream source. Do not combine engine headers with SFML 2.

The Lua/YAML game interfaces retain their existing field names and numeric
rotation units. SFML 3.1's new text shaping can change glyph metrics; review
layout and line wrapping in Fuera de Cuadro even when existing tests pass.

Software cursors follow rendering cadence and can have more latency than a
hardware cursor during frame stalls. They use window pixel coordinates so existing
cursor image sizes/hotspots retain their meaning across letterboxing and virtual
resolution changes. Touch-only Android does not draw a mouse cursor.

## Android scope and rendering direction

Upstream SFML 3.1's Android renderer uses **OpenGL ES 1**, and its `sf::Shader`
implementation reports unavailable on that backend. The activity now loads the
game library directly through `SFML::Main`; the old `sfml-activity` bootstrap and
OpenAL loading are gone. The native target requests 16 KB page alignment.

Basic compatibility lighting/color grading remains available through the existing
engine fallback. Custom shaders, normal-map lighting, shader-based occlusion and
advanced smoke composition are not guaranteed on Android. Latest lighting and
shadows were already broken there; Android parity is explicitly outside this
iteration's acceptance criteria. Android compilation/device behavior is not
established by desktop CI.

A subsequent renderer review should keep scene composition and authored effects
in the engine and define an explicit backend capability boundary. Evaluate a
supported GLES2/3 renderer or upstream support before selecting an implementation;
SFML 3.1 alone does not establish that capability. Avoid restoring
configure-time edits to SFML's private rendering internals merely to recreate the
old fork.

## Audio follow-up and merge gates

SFML 3 provides a changed playback backend, but the migration does not establish
that music problems are solved. Capture reproductions for looping, track end,
voice interruption, crossfade/offset preservation, audio-device changes and
Android pause/resume. The current unit tests mostly check envelopes and state,
not what a listener hears. If a specialized backend is justified, isolate it
behind the existing audio service API and retain those game-facing contracts.

First iteration checks: build the engine and seven examples, run unit tests and
windowed example smokes under a virtual display, and validate source/installed
consumers. Subsequent merge gates: adapt and exercise Fuera de Cuadro, review
text/cursor/audio behavior, and resolve or explicitly accept platform limitations.
