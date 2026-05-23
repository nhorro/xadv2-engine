# Data formats

Complete field reference for every YAML and Lua data file the engine reads. The
worked examples live in `02`–`05`; this appendix is the exhaustive
required / optional / default reference. `Type` notation: `{a, b}` is a mapping
with those keys, `[T]` is a list of `T`, `polygon` is `[{x, y}, ...]`, `path` is a
logical resource path relative to `resources.src`.

## Conventions

- All engine YAML data files accept an optional integer `version:` (default 1),
  bumped when the format changes incompatibly.
- Internal `id`s are ASCII, stable, and script-friendly; display `name`s may
  contain spaces, accents, and localized text.
- Resource references are logical paths, never platform filesystem paths.
- State keys use dotted names; state values are scalars (boolean, number,
  string).

## Game manifest — `game.yaml`

Worked example: [02 — Architecture overview](02-architecture-overview.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `id` | req | string | — | Stable game id used as the per-game subdirectory under the per-user data path (e.g. `~/.local/share/<id>/saves/`). Must match `[a-z0-9_-]+`. Two games using this engine get separate save folders by choosing distinct ids. |
| `resolution` | req | `{width, height}` | — | Virtual design resolution. |
| `window` | req | `{fullscreen, width, height}` | — | Initial display mode. `fullscreen: true` starts the game fullscreen at the **optimal mode for the resolution** (the video mode matching `resolution` when available, else the smallest larger mode, letterboxed). `width`/`height` are the initial **windowed** client size (default to `resolution`). Player settings override these at runtime via the settings scene. |
| `resources` | req | `{src}` | — | Resource source root. `src` is a directory (MVP) or archive (design-for). |
| `strings` | req | path | — | UI strings resource (engine-emitted text). One file in the MVP; a language→file map is design-for. See [UI strings](#ui-strings--stringslangyaml). |
| `settings` | opt | map | — | Default player-facing settings (e.g. `audio.music_volume`, `audio.sfx_volume`). User settings override these. |
| `cursor` | opt | `{image, interact?, hotspot?}` | OS cursor | Custom point-and-click cursor. `image` is the resting cursor; `interact` (opt) shows over an interactive hotspot; `hotspot` (opt `{x, y}`, default `0,0`) is the active click pixel within both images. Omitted ⇒ the OS cursor is used. |
| `development` | opt | map | — | Dev-only flags: `edit_mode` (master gate for the debug overlays), `show_walkboxes`, `show_hotspots`, `show_anchors`, `show_state` (seed the overlay layers), `allow_room_reload`. Not persisted as player settings. |
| `entry` | req | scene id | — | Initial scene. |
| `scenes` | req | `[scene]` | — | Scene list and outcome wiring. |

**`scene`** entry:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | string | — | Scene id, referenced by `entry` and outcomes. |
| `type` | req | enum | — | `TitleScreen`, `StoryText`, `RoomScene`, or a registered custom type. |
| `parameters` | opt | map | — | Type-specific parameters (see below). |

**Scene parameters by type:**

- `TitleScreen` — `background` (opt path), `music` (opt path), `new_game`
  (outcome scene id), `exit` (outcome, often `QUIT`), `font` (opt path).
- `StoryText` / `Cutscene` — `script` (path), `on_finish` (outcome scene id),
  `font` (opt path).
- `RoomScene` — `cast` (path), `logic` (path), `inventory` (path),
  `inventory_logic` (path), `rooms` (directory path), `start_room` (room id),
  `player` (req, cast character id — the persistent player avatar; appearance comes
  from this character's cast entry), `font` (opt path — speech and panel font).

Scenes that render text take their `font` as a logical-path parameter; an engine
default is used when omitted. Per-character speech color and style come from the
cast file, not the scene `font`.

## UI strings — `strings/<lang>.yaml`

The strings resource holds every user-facing string the engine itself generates:
verb labels, command connectors, and built-in menu labels. It does **not** hold
game content strings — hotspot/item `name`s, speech, and dialog lines stay inline
in their own files. The MVP ships one file (Spanish); a language→file map and a
runtime selector are design-for (R3).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `language` | req | string | — | Language tag, e.g. `es`. |
| `verbs` | req | map verb id → string | — | Display label per verb. Keys are the verb ids: `look_at`, `talk_to`, `pick_up`, `use`, `give`, `open`, `close`, `push`, `pull`. |
| `connectors` | req | map verb id → string | — | Two-operand connector per verb: `use` (e.g. `con`), `give` (e.g. `a`). |
| `ui` | req | map key → string | — | Built-in UI labels: menu (`new_game`, `continue`, `settings`, `quit`) and the top-bar walk label `walk_to` (shown when hovering walkable floor). |
| `defaults` | req | map key → string | — | Engine last-resort captions, spoken when no game handler produced text for a verb. The loader requires the exact key set below — no missing keys, and (in dev) no unknown keys. |

The required `defaults` keys and when each fires:

| Key | When fired |
|-----|------------|
| `cant_look_at` | No handler returned a string for `look_at`. |
| `cant_pick_up` | No handler returned for `pick_up`. |
| `wont_open` / `wont_close` | No handler for `open` / `close`. |
| `wont_push` / `wont_pull` | No handler for `push` / `pull`. |
| `cant_use_that_way` | No handler for one- or two-operand `use`. |
| `no_one_to_give_to` | No handler for `give`. |
| `nothing_to_say` | No handler for `talk_to`. |
| `nothing_happens` | Generic fallback when a command performed no action and spoke nothing. |

```yaml
version: 1
language: es

verbs:
  look_at: "Mirar"
  talk_to: "Hablar con"
  pick_up: "Agarrar"
  use:     "Usar"
  give:    "Dar"
  open:    "Abrir"
  close:   "Cerrar"
  push:    "Empujar"
  pull:    "Tirar"

connectors:
  use: "con"
  give: "a"

ui:
  walk_to:  "Ir a"
  new_game: "Nuevo juego"
  continue: "Continuar"
  settings: "Opciones"
  quit:     "Salir"

defaults:
  cant_look_at:      "No veo nada interesante."
  cant_pick_up:      "No puedo agarrar eso."
  wont_open:         "No se abre."
  wont_close:        "No se cierra."
  wont_push:         "No puedo empujar eso."
  wont_pull:         "No puedo tirar de eso."
  cant_use_that_way: "No puedo usar eso así."
  no_one_to_give_to: "No hay a quién dárselo."
  nothing_to_say:    "No tengo nada que decir."
  nothing_happens:   "No pasa nada."
```

> **MVP note.** Of these, only `nothing_happens` is wired into a fire site today;
> the per-verb captions are validated now (the contract is fixed) but their
> dispatch wiring is tracked as a follow-up.

## Cast — `cast.yaml`

Worked example: [05 — Scripting API](05-scripting-api.md).

**`appearances`** — map of appearance id → appearance:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `type` | req | enum | — | `animated_sprite`, `composite`, or `skeletal` (design-for). |
| `sprite` | req if `animated_sprite` | path | — | An `*.anim.yaml`. |
| `composite` | req if `composite` | path | — | A `*.composite.yaml`. |
| `shadow` | opt | `{size: {x, y}, color: {r, g, b, a}}` | none | Ground shadow drawn under the avatar. |

**`characters`** — map of character id → character:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `appearance` | req | appearance id | — | Visual definition from `appearances`. |
| `name` | req | string | — | Localized display name. |
| `speech_color` | opt | `{r, g, b}` | engine default | Speech text color. |

## Room — `rooms/<id>.yaml`

Worked example: [04 — Point & click concepts](04-point-and-click-concepts.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `id` | req | string | — | Room id (matches the file name). |
| `background` | req | `{color?, layers}` | — | See below. |
| `perspective` | opt | `{top: {y, scale}, bottom: {y, scale}}` | base scale | Avatar render scale interpolated by walking-pivot y, clamped outside `[top.y, bottom.y]`. Omitted ⇒ each avatar keeps its base scale. See [04 § Perspective scaling](04-point-and-click-concepts.md). |
| `walkable` | req | polygon | — | Navigable area. |
| `obstacles` | opt | `[polygon]` | `[]` | Non-walkable polygons inside `walkable`. |
| `points` | opt | map id → `{x, y}` | — | Named coordinates (starts, approach, camera targets). |
| `zones` | opt | `[{id, polygon}]` | — | Trigger polygons for exits / scripted events. |
| `regions` | opt | map | — | Changeable background parts (see below). |
| `objects` | opt | map | — | Active sprites placed in the room (see below). |
| `hotspots` | opt | map | — | Interactive areas + verb handlers (see below). |
| `avatars` | opt | `[avatar]` | — | Player placement + NPCs present at load (see below). |

**`background.layers`** — `[layer]`:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | string | — | Layer id. |
| `image` | req | path | — | Layer image. |
| `z` | req | number | — | Draw depth; larger is nearer the camera. |
| `origin` | opt | `{x, y}` | `{0, 0}` | Room-space top-left of the layer image (layers may differ in size and be placed freely). The room's world bounds are the union of all layer rects, floored to the room view; see [04 § World bounds](04-point-and-click-concepts.md). |
| `scale` | opt | number | `1.0` | Uniform render scale about `origin`, **aspect always preserved** (never distorted). A development aid for sizing furniture-style occluders; production layers ship native (`1.0`). The room editor resizes about the layer's base (bottom-centre) and can set `z` to the scaled base line. |
| `interactive` | opt | bool | `false` | Whether the layer receives pointer interaction. |
| `visible` | opt | bool | `true` | Initial visibility. Toggle at runtime with `set_layer_visible(id, bool)` (needs an `id`); persisted per room. |
| `shader` | opt | shader ref | — | Design-for. |
| `animation` | opt | anim ref | — | Design-for. |

`background.color` (`{r, g, b, a}`) is an optional solid fill behind all layers.

**`regions`** — map of region id → region:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `area` | req | polygon | — | Region footprint. |
| `z` | req unless `over` | number | — | Draw depth. |
| `over` | opt | layer id | — | Inherit the named layer's `z` instead of an explicit `z`. |
| `states` | req | map state id → path | — | Image per named state. |
| `initial` | req | state id | — | Starting state. |

**`objects`** — map of object id → object:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `sprite` | req | path | — | Object animation/sprite. |
| `position` | req | `{x, y}` | — | World position. |
| `z` | opt | `auto` \| number | `auto` | `auto` = the sprite's bottom edge; a number overrides. |
| `baseline` | opt | number | — | Floor-line world-Y. When set, the object sorts at this depth against avatar feet (occludes feet above the line, is occluded by feet below) — for a perspective object's foreground piece. Overrides `z`. |
| `visible` | opt | bool | `true` | Initial visibility; `show_object`/`hide_object` change it at runtime. |

**`walkbehinds`** — map of walk-behind id → area (no art duplication: pixels are sampled from a layer):

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `layer` | req | layer id | — | Source background layer whose pixels are sampled. Must exist. |
| `area` | req | polygon | — | Convex mask over the layer; the patch redrawn on top. Split a concave occluder into several areas. |
| `baseline` | req | number | — | Floor-line world-Y; the patch sorts here against avatar feet (in front when feet are below, behind when above). |

**`hotspots`** — map of hotspot id → hotspot:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `name` | req | string | — | Localized noun shown in the command bar. |
| `area` | req if no `bind` | polygon | — | Explicit hit-test polygon. |
| `bind` | req if no `area` | `object:<id>` / `region:<id>` | — | Hit test using the bound visual. |
| `approach` | opt | point id \| `{x, y}` | — | Point the player walks toward when a command targets this hotspot. |
| `requires_approach` | opt | bool | `true` | If `true` (the default), the command waits until the player reaches `approach` (input blocked meanwhile) — walk-then-act. Set `false` for the rare act-from-a-distance interaction: the player still walks toward `approach` but the command fires immediately. |
| `affordances` | req | `[verb]` | — | Verbs the UI may offer. |
| `default_verb` | opt | verb | `look_at` | Verb used on a plain click; must be `look_at` or in `affordances`. |
| `enabled` | opt | bool | `true` | Initial interactivity; `enable_hotspot`/`disable_hotspot` change it at runtime. |

At least one of `area` or `bind` is required. If both are present, either hit
source activates the hotspot.

**`avatars`** — `[avatar]`:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | character id | — | Character to place. |
| `start` | req | point id | — | Placement point. |
| `orientation` | opt | `up`/`right`/`down`/`left` | `down` | Initial facing. |
| `player` | opt | bool | `false` | Marks the player's placement entry (does not create the player; see [player vs NPC avatars](04-point-and-click-concepts.md)). |

## Spritesheet — `<name>.yaml`

Worked example: [03 — 2D game concepts](03-2d-game-concepts.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `image` | req | path | — | Atlas texture. |
| `size` | req | `{width, height}` | — | Atlas dimensions. |
| `sprites` | req | `[frame]` | — | Named frames. |

**`frame`:**

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | string | — | Stable frame id. |
| `rect` | req | `{x, y, width, height}` | — | Rectangle in the atlas. |
| `source_rect` | opt | `{x, y, width, height}` | — | Pre-pack source rect (traceability only). |
| `anchors` | opt | map name → `{x, y}` | — | Frame-local pivot/attachment points (origin = frame top-left). |

## Animation — `<name>.anim.yaml`

Worked example: [03 — 2D game concepts](03-2d-game-concepts.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `spritesheet` | req | path | — | Source spritesheet. |
| `pivot` | req | anchor name | — | Anchor used as the sprite's position pivot. |
| `sequences` | req | map name → sequence | — | Named playable sequences. |

**`sequence`:** `loop` (bool, default `false`), `frames` (`[{sprite, duration}]`)
where `sprite` is a frame id and `duration` is seconds.

## Composite — `<name>.composite.yaml`

Worked example: [03 — 2D game concepts](03-2d-game-concepts.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `root` | req | `{id, animation}` | — | Root animated sprite (e.g. body). |
| `children` | opt | `[child]` | — | Attached animated sprites. |

**`child`:** `id`, `parent` (id), `animation` (path), `parent_anchor` (anchor on
the parent), `child_anchor` (anchor on the child, aligned to `parent_anchor`).

## Dialog — `dialogs/<id>.lua`

Returns a node table. Worked example:
[04 — Point & click concepts](04-point-and-click-concepts.md). The dialog id is
both the file name and the speaking NPC's cast character id, so there is no
speaker field in the file.

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `start` | req | node id | — | Entry node. |
| `text_anchor` | opt | point id | — | Room point where this dialog's NPC speech is drawn; defaults to following the NPC avatar. |
| `on_enter` | opt | function | — | Setup callback. |
| `on_exit` | opt | function | — | Cleanup callback. |
| *(node id)* | — | node | — | Each remaining key is a node. |

**`node`:** `npc` (string or `[string]`), and either `options` (`[option]`) or
`to` (node id / `END`).

**`option`** (a list whose first element is the player line):

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `[1]` | req | string | — | Player line text. |
| `to` | req | node id \| `END` | — | Next node. |
| `when` | opt | function → bool | always | Visibility condition. |
| `run` | opt | function | — | Code run when selected. |
| `once` | opt | bool | `false` | Option disappears after use (persisted by `(node_id, option_index)`). |
| `silent` | opt | bool | `false` | Player line is not spoken aloud. |

## Inventory — `inventory.yaml`

Items are declared in `inventory.yaml`. Worked example:
[04 — Point & click concepts](04-point-and-click-concepts.md).

Top-level field: `items` (req), a map of item id → item.

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `name` | req | string | — | Localized display name. |
| `icon` | opt | frame id \| path | name as text | Design-for image drawn in the SCUMM panel. |
| `description` | opt | string | — | Optional examine text. |
| `affordances` | req | `[verb]` | — | Verbs the UI may offer. |
| `default_verb` | opt | verb | `look_at` | Verb on a plain click; must be `look_at` or in `affordances`. |
| `combinable` | opt | bool | `false` | Whether `use` expects a second operand. |

## Inventory behavior — `inventory.lua`

Returns a table keyed by inventory item id. Each item table may define verb
handlers using the same handler naming as room hotspots.

| Handler kind | Example command | Signature |
|--------------|-----------------|-----------|
| Single operand | `Look at key` | `function()` |
| Two operands: use | `Use key with door` | `function(target_id)` |
| Two operands: give | `Give map to Stan` | `function(recipient_id)` |

For two-operand commands whose first operand is an inventory item, inventory
handlers are checked before the second operand's hotspot handler.

## Save file — `GameState`

The save payload and its YAML serialization are specified under "Make persistent
state explicit" in [02 — Architecture overview](02-architecture-overview.md);
format, slots, and autosave policy are in
[R8](01-engine-requirements.md). Saves carry a `save_version` integer.
