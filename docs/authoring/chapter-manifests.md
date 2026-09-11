# Game and chapter manifests

Version-2 manifests separate game-wide presentation from chapter-owned content.
The engine composes every declaration at startup into one validated runtime
manifest; loose files and packed resources follow the same rules.

## Ownership

Keep these in `game.yaml`:

- display, languages, speech, cursor, settings, and development policy;
- title, settings, save/load, campaign notices, and credits;
- game-wide scene defaults and reusable profiles;
- the ordered list of chapter descriptors.

Keep these in each `chapters/<id>/chapter.yaml`:

- facts, cast, rooms, dialogs, inventory, and chapter logic roots;
- the chapter's playable room declaration;
- its cutscenes, close-ups, map, notebook, and deduction scenes;
- chapter-level defaults and profiles.

```yaml
# game.yaml
version: 2
id: example
resolution: { width: 1280, height: 720 }
window: {}
resources: { src: "." }
strings: ./campaign/strings/es.yaml
entry: title

chapters:
  - ./chapters/01_arrival/chapter.yaml

scene_defaults:
  all:
    font: ./shared/fonts/ui.ttf
  RoomScene:
    player: player
    direct_room_ui: ./shared/ui/direct_room_ui.yml

scenes:
  - id: title
    type: TitleScreen
    background: ./campaign/title.png
    menu:
      options: { new_game: intro, continue: room_01, exit: QUIT }

  - id: confirm_exit
    type: ConfirmationScene
```

Scene-specific fields may be written directly below `id` and `type`. The legacy
`parameters:` mapping remains valid in version-1 manifests and in normalized
runtime data.

## Chapter descriptor

```yaml
version: 2
id: 01_arrival
title: Arrival

facts: ./facts.yaml
cast: ./cast.yaml
rooms: ./rooms
dialogs: ./dialogs
inventory: ./inventory/inventory.yaml
inventory_logic: ./inventory/inventory.lua
logic: ./scripts/game.lua

room:
  id: room_01
  start: station

scene_defaults:
  CloseUp:
    cast: ./cast.yaml
    background_color: { r: 20, g: 16, b: 12 }

scenes:
  - id: intro
    type: Cutscene
    data: ./cutscenes/intro.yaml
    on_finish: room_01

  - id: letter
    type: CloseUp
    source: ./closeups/letter
```

The `room` block generates one independently restorable `RoomScene`. Its content
paths come from the chapter roots; game and chapter `RoomScene` defaults provide
shared presentation. Use a stable, globally unique room scene id for every
chapter because saves store it alongside the chapter id.

## Paths

Manifest composition recognizes explicit path forms:

- `./file`: relative to the YAML file containing the value;
- `/shared/file`: relative to `resources.src`;
- a bare scalar is preserved, which keeps scene ids such as `room_01`, `POP`, and
  legacy resource paths unambiguous.

Paths are normalized and may not escape the resource root. Imported chapter
files are loaded through the resource backend, so the same declarations work in
a `.pak`.

`RoomScene` accepts either the classic `scumm_panel` composer or the streamlined
`direct_room_ui` composer. The latter changes only ordinary room interaction;
pushed close-ups, maps, notebooks, and deduction scenes keep their own UI. See
[Direct room interaction](direct-room-interaction.md).

## Defaults and profiles

`scene_defaults.all` applies to every scene. A key matching a scene type applies
only to that type. Both game and chapter descriptors may declare defaults.

A named profile captures settings shared by a related subset:

```yaml
scene_profiles:
  evidence_document:
    type: CloseUp
    music: /shared/music/mystery.ogg
    music_transition: 2.5
    music_exit_transition: 0.25

scenes:
  - id: letter
    profile: evidence_document
    source: ./closeups/letter
```

Composition precedence is: engine defaults, game `all`, game type, chapter
`all`, chapter type, profile, then explicit scene fields. Maps merge recursively;
later scalar values replace earlier ones. Profiles are deliberately single-level
and do not inherit from one another.

## Close-up convention

For a `CloseUp`, `source` may name either a directory or a YAML file. A directory
expands to `source/closeup.yml`; an existing sibling `logic.lua` is connected
automatically. Font and cast usually come from defaults.

The scene id supplies the close-up data id, so the ordinary data file only needs
its actual content:

```yaml
background: background.png
hotspots:
  signature:
    name: signature
    area: [{x: 10, y: 10}, {x: 80, y: 10}, {x: 80, y: 40}]
  back:
    name: back
    type: exit
    area: [{x: 0, y: 0}, {x: 40, y: 0}, {x: 40, y: 40}]
  next:
    type: next_page
    goto: letter_page_2
    area: [{x: 1160, y: 0}, {x: 1280, y: 0}, {x: 1280, y: 720}, {x: 1160, y: 720}]
```

An explicit data `id` remains supported and is checked against the scene id.
`background_color` may be supplied by CloseUp scene defaults and overridden in
the data file. A hotspot `type` may be `object` (the default), `exit`,
`previous_page`, or `next_page`. Exit hotspots use the same textless exit cursor
as room transitions. Page hotspots require a `goto` target, always draw a
directional arrow, and replace only the close-up overlay so the underlying room
remains live.

Object hotspots receive persistent discovery affordances automatically: a
pulsing question marker before activation and a subdued check afterward. A
script whose handler gains new content later can call
`set_hotspot_new("signature", true)` during `on_enter`; pass `false` to mark it
exhausted again.
