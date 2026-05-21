# Engine requirements

This section defines the requirements for the first implementable version of the
engine. The reader is assumed to be familiar with classic SCUMM-style adventure
games. Detailed design for each subsystem is specified in the following sections.

> **Requirement tags.** Each requirement carries a scope tag:
> *[MVP]* — required for the first release;
> *[design-for]* — not built in the MVP, but the architecture shall not preclude
> it;
> *[constraint]* — a fixed implementation or platform decision. Requirement IDs
> are stable references and do not imply priority or document order.

## Functionality

**R1 — Classic SCUMM experience.** *[MVP]*

The engine shall deliver a classic third-person point-and-click adventure
experience inspired by SCUMM games. The committed core features are:

- verb-based interaction;
- inventory;
- dialog with NPCs;
- navigation across multiple rooms;
- scripted room behavior and puzzle logic.

The MVP shall prioritize clarity and authoring consistency over novelty. Modern
presentation features such as high-resolution backgrounds, layered scenery,
parallax, and shader effects may be supported when they naturally fit the design,
but they must not weaken the classic command-based interaction model.

**R2 — Scripting-only authoring.** *[MVP]*

A complete standard game shall be authorable without compiling game-specific C++.
Game logic is written in Lua against a high-level engine API; static game data is
declared in YAML. Artists and writers should be able to work from templates and
fill in room files, dialog files, and object behavior without modifying the engine.

The engine shall ship template files for the expected SCUMM behavior:

- game manifest;
- cast definition;
- room YAML + Lua pair;
- dialog tree;
- global game logic.

**Exception — custom interactions.** Mini-games or special scenes that fall
outside the classic point-and-click model may require dedicated code. The engine
shall provide an extension point for these through custom scene types. Whether
custom scenes are implemented in C++, Lua, or both remains a design-for decision.

**R3 — Localization-ready, Spanish first.** *[MVP: Spanish only · design-for:
multi-language]*

The MVP ships in Spanish with no runtime language selector. The design shall not
preclude adding other Western languages later.

Even in the MVP:

- no user-facing or in-game string shall be hardcoded in C++;
- fonts and text rendering shall support accented characters and common Western
  punctuation, including `á`, `é`, `í`, `ó`, `ú`, `ñ`, `ü`, and `ç`;
- word wrapping shall not rely on Spanish-specific assumptions;
- internal identifiers shall be separated from display names.

Engine-emitted UI text (verb labels, command connectors, built-in menu labels) is
satisfied in the MVP by a single UI strings resource declared in the manifest; see
[data formats](06-data-formats.md). A language→file map and a runtime selector are
the design-for path to multi-language support.

**R4 — Voice-over ready.** *[MVP: text-only · design-for: voice-over]*

The MVP is text-only. The design shall allow voice-over to be added later without
restructuring dialogs or speech:

- the design shall not preclude assigning a stable identifier to every spoken line
  (the MVP may use inline text; dialog `once`-options are already keyed stably);
- an audio clip may later be attached to a spoken line;
- text display shall be synchronizable with audio playback;
- speech and audio shall be skippable as a single unit.

Voice-over is per-language and therefore depends on R3.

**R8 — Save / load.** *[MVP]*

The engine shall let the player save the game state and restore it later. A saved
game shall capture at least:

- current top-level scene, when relevant;
- current room inside the room-view scene;
- player/avatar position and orientation;
- inventory contents;
- global scripted state;
- per-room scripted state;
- relevant room region states;
- dialog state required by `once` options or equivalent flags.

Decisions for the first implementation:

- save-file format: YAML through `yaml-cpp`, with a `save_version` integer; a
  compact binary format and save thumbnails are design-for;
- save slots: three manual slots plus one autosave slot;
- autosave policy: autosave on room change;
- triggering: saves are made from a menu plus engine autosave — there is no
  script-facing `save_game()` in the MVP (design-for);
- storage location: the per-user writable path (see
  [2D game concepts](03-2d-game-concepts.md)).

The full persistent state model is specified under "Make persistent state
explicit" in [the architecture overview](02-architecture-overview.md).

## Implementation and design constraints

**R5 — Languages, frameworks, and tools.** *[constraint]*

- Engine: C++17 with SFML 2.6.
- Scripting: Lua 5.4, embedded through the sol2 binding library.
- Configuration and text-based data files: YAML through `yaml-cpp`.
- Asset and authoring tools: Python.
- Authoring UI: prefer web-based tools when practical.

A web-based UI refers to offline authoring tools, not to the game runtime. The
game runtime is a native SFML application.

**R6 — Fixed virtual resolution with aspect-preserving scaling.** *[MVP ·
constraint]*

Each game is authored against a fixed virtual resolution declared in the manifest
under `resolution:`. "Fixed" means fixed for a given game, not an engine-wide
constant. All gameplay coordinates, object positions, geometry, and input events
use this virtual coordinate space.

The game may run fullscreen or windowed at any physical resolution supported by
the platform. The engine maps between physical window coordinates and virtual
coordinates automatically. The virtual image is scaled uniformly to preserve
aspect ratio:

- if the window is wider than the virtual aspect ratio, vertical bars are shown
  on the left and right;
- if the window is taller or narrower than the virtual aspect ratio, horizontal
  bars are shown on the top and bottom.

## Platform

**R7 — Operating systems.** *[constraint]*

- Development environment: Ubuntu 24.04 LTS.
- Primary release target: Windows 10/11 x64.
- Linux is a possible later release target.

## Out of scope for the first version

The following are out of scope for the first implementation:

- character switching, as in *Maniac Mansion*, *Day of the Tentacle*, or
  *Thimbleweed Park*;
- runtime multi-language UI, beyond being design-ready for it;
- voice-over playback, beyond being design-ready for it;
- 3D rendering;
- networking or multiplayer;
- a full visual IDE for authoring;
- runtime modding or user-generated content support.

Lightweight in-game edit-mode helpers and standalone Python tools are allowed,
but they are not the same as a full authoring IDE.
