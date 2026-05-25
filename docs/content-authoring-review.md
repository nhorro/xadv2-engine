# Content-authoring review — design vs. implementation

**Date:** 2026-05-23 · **Branch:** `content-authoring-branch` · **Author:** review pass
(nhorro findings + code/design audit)

## Purpose

The milestone issues (M0–M5) were generated from the high-level implementation plan
and accepted with limited review, because game assets weren't ready to exercise them.
After integration and a first real content-authoring pass, gaps and inconsistencies
surfaced. This document audits **where the implementation stands against the design
docs**, folds in the limitations nhorro recorded in PR #60 plus the additional ones
raised verbally, and proposes a concrete set of issues for the **upcoming** work.

Scope is forward-looking: we don't relitigate closed milestones. Where something was
nominally "done" but is missing pieces, we file fresh work rather than reopen history.

## Method

- Read the canonical design (`docs/sources/design/01`–`06`).
- Audited the implementation for each finding (`lib/src`, `lib/include`, scenes,
  Lua API surface).
- Cross-referenced PR #60's review notes and the open issues (#36–#41 M6, #54–#56).

## Summary scoreboard

Legend: ✅ implemented · 🟡 partial / designed-but-thin · ❌ missing · 🐞 bug

| # | Area | Design says | Today | State |
|---|------|-------------|-------|-------|
| J | Verb handler that only `talk()`s | command may act without returning text | fallback caption overwrites the speech | 🐞 |
| C | Speech near screen edges | speech "rendered over scenery near the speaker" | centered on speaker, no edge containment (outline ✅) | 🟡 |
| M | Dialog NPC text placement | dialog format | no `talk_spot`/anchor; reuses speaker pos | ❌ |
| L | Dialog option pruning/evolution | `once` + `when` (04 §Dialog) | **implemented** (once/when filtering) | ✅ (docs thin) |
| K | Approach points | hotspot `approach`, characters walk geometry | parsed; no walk-to-approach-then-act, no distance gating | 🟡 |
| O | Background-layer visibility | layers drawn at `z` | no `visible` flag, no Lua API (objects have `hide_object`) | ❌ |
| N | Perspective objects vs z-order | z = pivot.y | breaks for perspective-drawn art; no authoring guidance | 🟡 |
| H | Settings: display/lang/volume | display-mode (MVP), music (MVP), SFX (MVP), language (design-for) | settings scene edits **music only**, no display/lang/SFX | 🟡 |
| P | Settings persistence | saved to per-user location | in-memory only (header says "later milestones") | ❌ |
| I | Sound player per-channel volume | `MusicPlayer`/`SoundPlayer` with volume | infra ✅ (`set_volume`, `apply_settings`); not surfaced in UI | 🟡 |
| A | Mouse cursor | — (not in design) | OS default cursor; no P&C cursor / hover affordance | ❌ |
| B | Scene fade in/out | — (not in design) | hard cuts; `SceneManager` "transitions" = stack ops, no visual fade | ❌ |
| G | Avatar shadow | `shadow` appearance field (05, 06 §appearance) | parsed but "realized later"; not drawn | ❌ (designed) |
| F | Close-up / examine scenes | R2 custom-scene extension point | none | ❌ |
| D | SCUMM panel look | basic panel | functional, plain styling | 🟡 (polish) |
| E | Windows build | R7: Windows is the **primary** target | CI is Ubuntu-only (#48); no vcpkg/MSVC recipe | ❌ |

## Findings in detail

### 🐞 J — Commands that only talk are discarded

A verb handler that calls `talk(...)` and returns nothing is overwritten. Flow:
[room_runtime.cpp](../lib/src/pnc/room_runtime.cpp#L244) turns the handler's return
into an optional caption; [room_scene.cpp](../lib/src/pnc/room_scene.cpp#L685-L697)
then does `say(caption.value_or("No pasa nada."))`, clobbering the speech that
`talk()` just set. nhorro's hall `salida.open` example is exactly this. **A command
that performs an action (talking, walking, state change) is valid even with no return
string.** Fix: only apply the fallback caption when the handler neither returned text
nor produced a side-effect that already spoke/changed view state.

### 🟡 C / ❌ M — Speech containment and dialog text placement

Speech already has the outline nhorro wanted ([speech_manager.cpp](../lib/src/pnc/speech_manager.cpp#L94-L95)).
What's missing is the "invisible balloon" — text centered on the speaker but **kept
on screen**: when the speaker is near an edge, the block is clamped so it stays
readable instead of running off-frame. There is also no way to place an NPC's line
at an authored point: dialog should support a `talk_spot` (static point now; sprite
anchor later for moving NPCs), e.g. `skull_talk_spot`. C and M share the same
text-layout code path and are good to do together.

### ✅ L — Dialog evolution already works (document it)

nhorro wasn't sure if option-pruning was implemented — it is. [dialog.cpp](../lib/src/pnc/dialog.cpp#L122)
filters by `when` and consumes `once` options, persisting consumption via the host.
So "ask once / unlock paths as the conversation evolves" is supported. The gap is
**documentation** (04 §Dialog has only a minimal example) and a verification pass
against a richer tree. File as a docs/verify task, not a feature.

### 🟡 K — Approach points

Hotspots carry an optional `approach` point but the runtime neither walks to it nor
gates interaction by distance. Two behaviors to design (nhorro's note):
distance-gated rejection (simple) vs. **auto-walk to the approach point, then run the
verb** (smoother, needs the command to queue and the following input to wait until
arrival). The smoother path is the SCUMM-standard one and is worth the queueing
complexity.

### ❌ O / 🟡 N — Layers, regions, and z-order with perspective art

`BackgroundLayer` has no `visible` flag and no Lua control, while `RoomObject` does
(`hide_object`/`show_object`). nhorro wants the mummy cart toggled as a layer →
add `visible` to layers plus a `set_layer_visible(id, bool)` Lua API. Separately,
art drawn **with perspective** (the cart) breaks the `z = pivot.y` sort; we need
authoring guidance and possibly an explicit per-object/region z override so foreground
occluders compose correctly.

### 🟡 H / ❌ P / 🟡 I — Settings, display, audio, language

Design 03 §Settings lists display-mode, music volume, SFX volume (all MVP) and
language (design-for). Today [settings_scene.cpp](../lib/src/pnc/settings_scene.cpp)
edits **music volume only**; there is no SFX control, no display/resolution/fullscreen
switching, no language selector, and [settings.cpp](../lib/src/core/settings.cpp) has
**no persistence** (the header itself flags this as deferred). The audio plumbing is
ready — `SoundPlayer`/`MusicPlayer` expose `set_volume` and `AudioServices::apply_settings`
exists — so this is mostly UI + persistence + a display-mode service using SFML's
`getFullscreenModes()`/window recreation, kept transparent to the game (R6 scaling
already isolates gameplay coordinates).

### ❌ A / ❌ B / ❌ G / ❌ F — Presentation features (new)

- **A — Cursor:** swap the OS cursor for a P&C cursor (e.g. blinking crosshair) and
  give hover affordance feedback. Not in the design yet; add to 04.
- **B — Fades:** fade-to-black in/out on room and scene changes. `SceneManager`'s
  "transitions" today are just stack operations; no visual transition exists.
- **G — Shadows:** the `shadow` appearance field is designed (05, 06 §appearance) but
  not drawn. Start with a cheap ellipse blob under the avatar; the shader version is
  later work (relates to #56).
- **F — Close-up scenes:** a full-screen examine view (background fills the screen,
  no SCUMM panel) where the player only hovers/clicks hotspots with a single action
  (`look_at`, or a script that changes scene — e.g. a map close-up). Fits R2's
  custom-scene extension point; useful for books, devices, maps.

### 🟡 D — SCUMM panel styling

Functional but plain. Research how modern titles in this aesthetic do it (Thimbleweed
Park and others) and introduce a themeable look. Polish, not blocking.

### ❌ E — Windows build

R7 makes **Windows the primary release target**, yet CI is Ubuntu-only (#48) and there
is no vcpkg/MSVC build recipe. Needs CMake presets + vcpkg manifest for SFML/yaml-cpp/Lua
and a Windows CI job. Related to but distinct from #40 (packaging smoke path).

### Tools / pipelines (from PR #60)

The room editor, transparentizer, and packer work but need bug-fixing, docs, and the
ad-hoc "pipelines" directories replaced with a documented flow. Tracked under #54;
recommend updating that issue rather than opening a new one.

## Filed issues

Filed 2026-05-23. M6 = "M6 — MVP Hardening And Debug Tools" (existing); M7 =
"M7 — Presentation & Authoring Polish" (created in this pass, milestone #8).
Bugs/authoring-correctness fixes → M6; feature-level UX → M7.

| Issue | Title | Labels | Milestone | Maps |
|-------|-------|--------|-----------|------|
| #61 | pnc: verb handlers that only `talk()` shouldn't be overwritten by fallback caption | bug, area:pnc | M6 | J |
| #62 | pnc: edge-aware speech containment (keep text on screen near the speaker) | enhancement, area:pnc | M6 | C |
| #63 | docs: document dialog `once`/`when` evolution + richer example & verify | documentation, area:docs | M6 | L |
| #64 | pnc: background-layer `visible` flag + `set_layer_visible` Lua API | enhancement, area:pnc | M6 | O |
| #65 | pnc/docs: z-order for perspective-drawn art (guidance + explicit z override) | enhancement, area:pnc | M6 | N |
| #66 | core: persist player settings to the per-user location | enhancement, area:core | M6 | P |
| #67 | pnc: settings UI — music + SFX volume controls wired through `AudioServices` | enhancement, area:pnc | M6 | H,I |
| #68 | build: Windows build recipe (vcpkg + CMake presets) and Windows CI | enhancement, area:build | M6 | E |
| #69 | pnc: dialog NPC text placement via `talk_spot` (static point now, sprite anchor later) | enhancement, area:pnc | M7 | M |
| #70 | pnc: approach points — auto-walk-to-approach-then-act with command queueing | enhancement, area:pnc | M7 | K |
| #71 | core/pnc: display-mode settings — resolution + fullscreen via SFML video modes | enhancement, area:core | M7 | H |
| #72 | core: localization infrastructure — language string-set map + selector (Spanish default) | enhancement, area:core | M7 | H (R3) |
| #73 | pnc: custom point-and-click mouse cursor + hover affordance feedback | enhancement, area:pnc | M7 | A |
| #74 | core/pnc: scene & room fade in/out transitions | enhancement, area:pnc | M7 | B |
| #75 | pnc: avatar shadows — draw the designed `shadow` appearance (ellipse blob) | enhancement, area:pnc | M7 | G |
| #76 | pnc: close-up / examine scene type (full-screen, hotspot-only, single action) | enhancement, area:pnc | M7 | F |
| #77 | pnc: restyle the SCUMM panel (research modern aesthetics, themeable) | enhancement, area:pnc | M7 | D |
| #54 ✎ | Updated with: room-editor hardening + replace ad-hoc "pipelines" with a documented flow | area:tools | — | tools |

Notes:
- #14 (shadow blob) and #56 (shaders) overlap on the "shadow via shader" future path;
  do the cheap ellipse first, leave the shader version to #56.
- #17 (Windows build) is distinct from #40 (packaging smoke path) — build first, then package.
- Design docs to update as these land: **04** (cursor, fades, close-up scene, speech
  containment, dialog `talk_spot`, layer visibility), **03** (settings UI scope).
