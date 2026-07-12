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
| `window` | req | `{fullscreen, width, height}` | — | Initial display mode. `fullscreen: true` starts the game fullscreen at the **desktop's native video mode**, with the virtual `resolution` letterboxed within it (no mode switch — this keeps input mapping correct). `width`/`height` are the initial **windowed** client size (default to `resolution`). Player settings override these at runtime via the settings scene. |
| `resources` | req | `{src}` | — | Resource source root. `src` is a directory (MVP) or archive (design-for). |
| `strings` | req* | path | — | Single-language shorthand: the UI strings resource (engine-emitted text). Mutually exclusive with `languages`; exactly one is required. See [UI strings](#ui-strings--stringslangyaml). |
| `languages` | req* | `[{id, name?, strings}]` | — | UI-strings languages (R3). Each entry: `id` (stable ASCII id stored in settings), `name` (opt display label in its own language, defaults to `id`), `strings` (path to that language's file). The MVP ships one entry (Spanish); the list is design-ready for more. |
| `default_language` | opt | language id | first entry | Which `languages` entry is active when no player preference is stored. Must name an entry in `languages`. |
| `settings` | opt | map | — | Default player-facing settings (e.g. `audio.music_volume`, `audio.sfx_volume`). User settings override these. |
| `cursor` | opt | `{image, interact?, hotspot?}` | OS cursor | Custom point-and-click cursor. `image` is the resting cursor; `interact` (opt) shows over an interactive hotspot; `hotspot` (opt `{x, y}`, default `0,0`) is the active click pixel within both images. Omitted ⇒ the OS cursor is used. |
| `development` | opt | map | — | Dev-only flags: `edit_mode` (master gate for the debug overlays), `show_walkboxes`, `show_hotspots`, `show_anchors`, `show_state` (seed the overlay layers), `allow_room_reload`, `profiling` (resource-profiling mode, #112), `profiling_interval` (seconds between samples, default `2.0`). Not persisted as player settings. |
| `entry` | req | scene id | — | Initial scene. |
| `scenes` | req | `[scene]` | — | Scene list and outcome wiring. |

`*` — `strings` and `languages` are alternatives: provide exactly one. The
single-language form

```yaml
strings: strings/es.yaml
```

is equivalent to a one-entry list whose id is `default`. The explicit form names
each language and is what the settings language selector lists:

```yaml
languages:
  - { id: es, name: "Español", strings: strings/es.yaml }
  - { id: en, name: "English", strings: strings/en.yaml }
default_language: es   # optional; defaults to the first entry
```

**`scene`** entry:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | string | — | Scene id, referenced by `entry` and outcomes. |
| `type` | req | enum | — | `TitleScreen`, `StoryText`, `Cutscene`, `RoomScene`, `SettingsScene`, `SaveLoadScene`, `CloseUp`, or a registered custom type. |
| `parameters` | opt | map | — | Type-specific parameters (see below). |

**Scene parameters by type:**

- `TitleScreen` — `background` (opt path — full-screen image scaled to the virtual
  resolution; solid black when omitted), `music` (opt path — played in a loop),
  `font` (opt path), `font_size` (opt int — menu label size in virtual pixels),
  and a `menu` map: `menu.position` `{x, y}` (anchor as a 0..1 screen fraction;
  `{0.5, 0.5}` centers the menu block) and `menu.options` with the outcome scene
  ids `new_game`, `continue` (the entry shows only when a save exists),
  `load_game` (opt, #108 — when wired *and* a save exists, the title adds a
  "Recuperar partida" entry that pushes the load picker; the value is reserved
  for a future opt-out routing override, any non-empty value enables the entry),
  and `exit` (often `QUIT`). Menu entries are borderless text; hover highlights
  the entry and switches the cursor to its interact variant.
- `StoryText` — Lua-driven cut-scene (script invokes `show_text(...)`).
  Parameters: `script` (path), `on_finish` (outcome scene id), `font` (opt path).
  Skippable with Enter / Space / Esc / click. Use for scripted yield-based
  sequences (waits, branches, event hooks).
- `Cutscene` (issue #116) — YAML slide-based cut-scene. Parameters: `data`
  (req path to the cutscene YAML — see [cutscene file](#cutscene--cutscenesidyaml)),
  `on_finish` (outcome scene id), `font` (opt path — fallback when a slide's
  `text_style.font` is empty). Three advancement modes (`auto`, `manual`,
  `timed`); `Esc` always skips. The localized manual continue/skip hint comes
  from `strings.ui.manual_continue_hint`. Use for declarative, scrollable
  slide shows; pick `StoryText` instead when the cutscene needs scripted
  yields or branching.
- `RoomScene` — `cast` (path), `logic` (path), `inventory` (path),
  `inventory_logic` (path), `rooms` (directory path), `start_room` (room id),
  `player` (req, cast character id — the persistent player avatar; appearance comes
  from this character's cast entry), `font` (opt path — speech and default panel
  font), `scumm_panel` (opt path — YAML panel layout/skin config),
  `pause_menu.overlays.<id>` (opt map — custom pause actions with `scene`,
  `label_key`, and an `order` from 30 through 89). Custom actions push their
  scene as a transparent or opaque overlay. Built-ins remain ordered at resume
  0, save 10, load 20, settings 90, and quit-to-title 100.

  ```yaml
  pause_menu:
    overlays:
      notebook:
        scene: notebook
        label_key: notebook
        order: 30
  ```
- `SettingsScene` — `background` (opt path — full-screen image scaled to the virtual
  resolution; a dark fill when omitted), `font` (opt path), `font_size` (opt int —
  menu-row text size; the title derives from it). Rows are navigable by keyboard
  (arrows / Enter / Esc) **and** mouse (hover selects, left-click a value's left/right
  half decrements/increments, click APPLY/BACK to confirm/cancel), using the same
  custom cursor and hover affordance as the rest of the game.
- `SaveLoadScene` — `mode` (req, `save` | `load`), `background` (opt path — same
  shape as `SettingsScene`, the same image is fine), `font` (opt path),
  `font_size` (opt int), `room_scene` (opt scene id — where `load` goes after
  a successful pick, default `room_view`). One scene type, two manifest entries
  (one per mode); the engine auto-detects each by `parameters.mode` at startup
  and exposes them via `SceneManager::open_save()` / `open_load()` (issue #108).
  Lists the autosave slot (read-only) plus the manual slots; each row has a
  thumbnail placeholder, a label, a description and a date/time (from the save
  metadata, falling back to the file's mtime). In `save` mode every manual row
  carries its own text-input field for the description; clicking that row's
  **Save** button writes the snapshot the in-game pause menu staged via
  `SaveService::stage_pending_snap`. In `load` mode every populated row offers
  a **Load** button that stages the restore and triggers a `goto_scene` to
  `room_scene`.

Scenes that render text take their `font` as a logical-path parameter; an engine
default is used when omitted. Per-character speech color and style come from the
cast file, not the scene `font`.

## SCUMM panel config — `ui/scumm_panel*.yml`

`RoomScene.parameters.scumm_panel` may point at a panel layout/skin YAML file.
When omitted, the engine uses the built-in classic 85% scenery / 15% panel
layout. The config controls visual layout only: command construction and command
preview state still live in `CommandController`.

Coordinate rules:

- `scumm_panel.design_size` defines the nominal coordinate system.
- `layout.panel.rect` is in `design_size` coordinates.
- `layout.command_bar.rect` and `layout.body.rect` are relative to the panel.
- `layout.verb_panel.rect`, `layout.inventory_panel.rect`,
  `evidence_indicator.rect`, and `notebook.rect` are all relative to the body.
- `layout.inventory_arrows.*.hitbox` is relative to the inventory panel.
- `settings_button.position` is normalized within the panel: `[0, 0]` is the
  panel's top-left and `[1, 1]` is its bottom-right. `anchor` determines which
  point of the button is placed there.
- At runtime, rectangles and hitboxes scale by
  `runtime_width / design_width` and `runtime_height / design_height`.

Top-level shape:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `scumm_panel.design_size` | req | `[w, h]` | — | Positive design-space size. |
| `scumm_panel.smooth` | opt | bool | SFML default | Font smoothing (linear filtering) for the panel fonts; `false` = crisp pixels. Applied to the shared font objects. |
| `layout.panel.rect` | req | `[x, y, w, h]` | — | Complete panel rectangle. |
| `layout.panel.background` | opt | map | solid dark fill | `type: solid`, `image`, or `nine_slice`; image paths resolve relative to this YAML file. |
| `layout.command_bar.rect` | req | rect | — | Current command text region. |
| `layout.body.rect` | req | rect | — | Parent region for verbs and inventory. |
| `layout.verb_panel` | opt | grid | 3x3 classic grid | Grid rect, row/column count, padding, and cell gap. Optional `style: buttons` (default) or `text`. |
| `layout.inventory_panel` | opt | grid | 2x4 classic grid | Inventory grid; page capacity is `rows * columns`. Optional `style: text` (default) or `icons`. |
| `layout.inventory_arrows.mode` | opt | enum | `draw` | `draw`, `background_variants`, or `none`. Ignored when `inventory_panel.style: icons`. |
| `content.verbs` | opt | `[verb id]` | classic 9 verbs | Row-major verb order. Use canonical ids such as `open`, `look_at`, and `pick_up`; labels come from UI strings. |
| `evidence_indicator` | opt | map | disabled | Optional `{label} x/n` readout (issue #172). See below. |
| `notebook` | opt | map | disabled | Optional in-panel notebook access links (issue #172). See below. |
| `settings_button` | opt | map | disabled | Optional Settings button. Supports `enabled`, normalized `position`, `anchor`, design-pixel `size`, `render_mode: panel | image`, `panel` label/style, and `image.normal` / `image.hovered`. |
| `skin.command_text`, `skin.verb_text`, `skin.inventory_text` | opt | text style | built-in colors | `font`, `size`, `color`, `hover_color`, `disabled_color`, `align`, and an optional `outline_thickness` (design px, `0` = none) + `outline_color` (default black). The `evidence_indicator.text` / `notebook.text` styles take the same fields. |
| `skin.panel.background_variants` | opt | map key -> path | none | Background images for arrow visibility/hover states. |

Arrow modes:

- `draw`: the engine draws `skin.arrows.draw.previous_text` and `next_text` in
  the configured hitboxes.
- `background_variants`: the engine does not draw arrows; it selects a panel
  background variant such as `inv_prev_visible`, `inv_next_hover`, or
  `inv_both_hover`, while still using the configured hitboxes for input.
- `none`: arrows are not displayed and hitboxes are ignored.

Settings button:

- `enabled: false` preserves existing panel behavior.
- `render_mode: panel` draws a small engine-rendered button using
  `panel.label_key` from UI strings plus optional font and colors.
- `render_mode: image` draws `image.normal`, switching to `image.hovered` while
  the pointer is inside the button. If `hovered` is omitted, `normal` is reused.
- Clicking the button is a UI navigation intent: `RoomScene` opens the configured
  `SettingsScene` overlay through `SceneManager::open_settings()`. It does not
  select verbs, select inventory, page inventory, or mutate command state.

Presentation styles (issue #172) — all default to the classic look, so an
existing panel config (and the engine default) is unchanged:

- `layout.verb_panel.style: text` draws verbs as plain horizontal pixel-text
  labels (no boxes); the selected verb takes the accent color. The grid
  `rows`/`columns` capacity bound does not apply — verbs lay out in one row sized
  by the rect. `style: buttons` (default) keeps the framed grid.
- `layout.inventory_panel.style: icons` renders exactly `rows * columns` fixed
  slots with a frame and, per held item, its `icon` (from `inventory.yaml`) or a
  drawn placeholder glyph. There is no paging, so `inventory_arrows` is ignored.
  `style: text` (default) keeps the localized-name list with paging.

Evidence indicator (`evidence_indicator`) — a non-clickable `{label} x/n` readout
for deduction-style games. The counts come from two engine state keys the game /
notebook writes via `set_state`; an unset key reads as `0`.

- `enabled` (default `false`), body-relative `rect`.
- `label_key` — UI-strings key for the leading label (e.g. `evidencias`).
- `collected_state`, `total_state` — `get_state` keys for the two counts.
- optional `icon` (image path, else a drawn placeholder) and a `text` style.

Notebook access (`notebook`) — one or more clickable links in the panel that open
a notebook/deduction scene. Each entry opens the same `scene`, writing its `tab`
hint to `tab_state` (via `set_state`) first so the scene can select its initial
tab on construct.

- `enabled` (default `false`), body-relative `rect` (entries split it evenly,
  after an optional leading `icon`).
- `scene` — manifest scene id to push (e.g. `notebook`).
- `tab_state` — state key written with the clicked entry's `tab`.
- `entries: [{ label_key, tab }]` — one link per entry; `label_key` is a
  UI-strings key, `tab` is an opaque hint the target scene interprets.
- optional `text` style.
- Clicking a link is a UI navigation intent (`PanelIntent::OPEN_NOTEBOOK`):
  `RoomScene` sets `tab_state` and pushes `scene`; it does not touch command state.

Validation rejects malformed rectangles, non-positive grid sizes, too many verbs
for the verb grid, unknown arrow/button modes, `background_variants` configs that
provide neither `skin.panel.background_variants.normal` nor a panel background
image, and image-rendered Settings buttons without `image.normal`.

## UI strings — `strings/<lang>.yaml`

The strings resource holds every user-facing string the engine itself generates:
verb labels, command connectors, and built-in menu labels. It does **not** hold
game content strings — hotspot/item `name`s, speech, and dialog lines stay inline
in their own files. The MVP ships one file (Spanish). The manifest `languages` map
and a runtime selector in the settings scene make additional UI-strings languages
selectable (R3); per-language *game-content* files remain design-for. The active
language is persisted in the player settings file (see below).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `language` | req | string | — | Language tag, e.g. `es`. |
| `verbs` | req | map verb id → string | — | Display label per verb. Keys are the verb ids: `look_at`, `talk_to`, `pick_up`, `use`, `give`, `open`, `close`, `push`, `pull`. Used in the command bar. |
| `verb_panel` | opt | map verb id → string | falls back to `verbs` | Short SCUMM-panel button labels (e.g. `talk_to: "Hablar"` while `verbs.talk_to` stays `"Hablar con"` for the command bar). |
| `connectors` | req | map verb id → string | — | Two-operand connector per verb: `use` (e.g. `con`), `give` (e.g. `a`). |
| `ui` | req | map key → string | — | Built-in UI labels. Menu (`new_game`, `continue`, `settings`, `settings_button`, `quit`); save/load picker (`save_game`, `load_game`, `save_button`, `load_button`, `autosave`, `slot`, `slot_empty`, `description_hint`, `thumbnail_placeholder`); in-game pause (`pause`, `resume`, `settings`, `quit_to_title`); settings (`back`, `apply`, `resolution`, `fullscreen`, `language`, `music`, `sfx`, `on`, `off`); the cutscene manual-continue hint (`manual_continue_hint`); and the top-bar walk label `walk_to` (shown when hovering walkable floor). |
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
  settings_button: "OP"
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
| `shader` / `shaders` | opt | shader ref / `[shader ref]` | — | Shader(s) applied to the avatar's animated sprite (issue #106 — every PC / NPC that uses this appearance inherits the stack). `u_resolution` for an avatar shader equals the **current frame size in pixels**. See **Shaders** below. |

**`characters`** — map of character id → character:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `appearance` | req | appearance id | — | Visual definition from `appearances`. |
| `name` | req | string | — | Localized display name. |
| `speech_color` | opt | `{r, g, b}` | engine default | Speech text color. |
| `speech_gap` | opt | number (room px) | `48` | Clearance kept between the speaker and a speech line when it is placed beside them (speaker near the top/bottom edge). Wider characters want a larger value so the line clears the sprite. |

## Room — `rooms/<id>.yaml`

Worked example: [04 — Point & click concepts](04-point-and-click-concepts.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `id` | req | string | — | Room id (matches the file name). |
| `background` | req | `{color?, layers}` | — | See below. |
| `perspective` | opt | `{top: {y, scale}, bottom: {y, scale}}` | base scale | Avatar render scale interpolated by walking-pivot y, clamped outside `[top.y, bottom.y]`. Omitted ⇒ each avatar keeps its base scale. See [04 § Perspective scaling](04-point-and-click-concepts.md). |
| `walkable` | req | polygon | — | Navigable area. |
| `obstacles` | opt | `[polygon \| obstacle]` | `[]` | Non-walkable polygons inside `walkable`. Each entry is either a bare polygon, or a mapping `{ id, area, enabled? }` — a **named** obstacle that can be toggled at runtime with `enable_obstacle`/`disable_obstacle` (e.g. a crate that blocks the floor until removed). `enabled` (default `true`) is the initial state; runtime toggles are persisted per room. A disabled obstacle does not block walking. |
| `points` | opt | map id → `{x, y}` | — | Named coordinates (starts, approach, camera targets). |
| `zones` | opt | `[{id, polygon}]` | — | Trigger polygons for exits / scripted events. |
| `regions` | opt | map | — | Changeable background parts (see below). |
| `objects` | opt | map | — | Active sprites placed in the room (see below). |
| `hotspots` | opt | map | — | Interactive areas + verb handlers (see below). |
| `avatars` | opt | `[avatar]` | — | Player placement + NPCs *always* present at load (see below). Config-managed NPCs are spawned by `configs`, not here. |
| `configs` | opt | `{start, <id>: config}` | — | Declarative **configurations** — which NPCs/objects/obstacles are present per named room state (see below). |

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
| `shader` | opt | shader ref | — | A shader applied when drawing the layer. See **Shaders** below. |
| `shaders` | opt | `[shader ref]` | — | Ordered shader stack — the engine runs them as a multi-pass chain in this order. See **Shaders** below. |
| `animation` | opt | anim ref | — | Design-for. |

`background.color` (`{r, g, b, a}`) is an optional solid fill behind all layers.

**`regions`** — map of region id → region:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `area` | req | polygon | — | Region footprint. |
| `z` | req unless `over`/`baseline` | number | — | Draw depth. |
| `over` | opt | layer id | — | Inherit the named layer's `z` instead of an explicit `z`. The region also **inherits the layer's shader stack** (issue #105): layer effects run first so the region matches the surrounding background; the region's own `shader`/`shaders` run on top. |
| `baseline` | opt | number | — | Floor-line world-Y; the region sorts here against avatar feet (occludes feet above the line, is occluded by feet below), for a perspective region the player passes. Overrides `over` / `z`. |
| `states` | req | map state id → path | — | Image per named state. |
| `initial` | req | state id | — | Starting state. |
| `shader` / `shaders` | opt | shader ref / `[shader ref]` | — | Shader(s) applied when drawing the region. See **Shaders** below. |

**`objects`** — map of object id → object:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `sprite` | req | path | — | Object visual: a **static image** (e.g. `*.png`; the loader accepts `image` as a deprecated alias), or an **animation** (`*.anim.yml` / `*.yaml`) which makes this an *animated object* — an `AnimatedSprite` that can play sequences and be moved/resized from script (see [05 § Object handle](05-scripting-api.md)). One of `sprite`/`image` is required, else the loader fails with `room.object-sprite-missing`. |
| `sequence` | opt | string | — | For an **animated** object: the initial sequence to play (looping). Recommended so the object shows a frame on load; otherwise it stays blank until `object(id):play(...)`. |
| `position` | req | `{x, y}` | — | World position — the **top-left** for a static image, the sprite **pivot** for an animated object. |
| `z` | opt | `auto` \| number | `auto` | `auto` = the sprite's bottom edge (scaled); a number overrides. |
| `scale` | opt | number | `1.0` | Uniform render scale about `position`, aspect always preserved (like a layer's `scale`); must be > 0. The room editor sets this when resizing an object. |
| `baseline` | opt | number | — | Floor-line world-Y. When set, the object sorts at this depth against avatar feet (occludes feet above the line, is occluded by feet below) — for a perspective object's foreground piece. Overrides `z`. |
| `visible` | opt | bool | `true` | Initial visibility; `show_object`/`hide_object` change it at runtime. |
| `shader` / `shaders` | opt | shader ref / `[shader ref]` | — | Shader(s) applied when drawing the object. See **Shaders** below. |

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
| `bind` | req if no `area` | `object:<id>` / `region:<id>` / `npc:<id>` | — | Hit test using the bound visual. `npc:<id>` tracks the NPC's current (moving) bounds and is inactive while that NPC is absent — the right choice for a character that walks around. |
| `approach` | opt | point id \| `{x, y}` | — | Point the player walks toward when a command targets this hotspot. May be omitted on a hotspot bound to a moving NPC/object, which then walks toward the target's live position (see `requires_approach`). |
| `requires_approach` | opt | bool | `true` | If `true` (the default), the command waits until the player reaches `approach` (input blocked meanwhile) — walk-then-act. Set `false` for the rare act-from-a-distance interaction: the player still walks toward `approach` but the command fires immediately. On a hotspot bound to a *moving* NPC/object with no fixed `approach`, `true` instead walks toward the target's live position, re-targeting as it moves and firing once in range (with a give-up timeout) — `#158`. |
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

**`configs`** (issue #185) — a room is shown in one of a few discrete
**configurations** (which actors/objects are present). Declare them here; the
engine tracks the live config in `GameState` (no author-managed `"<room>.cfg"`
state) and **reconciles presence on every entry**. The optional first-enter /
re-enter *beats* live in Lua under `room.configs[<id>]` — see
[05 § Room configurations](05-scripting-api.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `start` | req | config id | — | The config a **new game** begins the room in. |
| `<id>` | — | `{present?}` | — | One configuration, keyed by a symbolic id (`intro`, `puzzle`, …). |

Each config's **`present`** lists what is ON in that config. Reconciliation is
**exhaustive**: the engine forms the *managed set* = the union of every id named
across all of the room's configs, and on entry turns **on** exactly what the
current config names and **off** the rest. So an empty config is `present: {}` and
needs no explicit "hide everything".

| `present` key | Type | Meaning |
|-----|------|---------|
| `npcs` | map id → `{at, facing?}` | NPCs spawned in this config. `at` is a room **point**; `facing` (default `down`) the initial orientation. A managed NPC absent from a config is despawned there. |
| `objects` | `[object id]` | Object ids **shown** in this config (managed objects not listed are hidden). |
| `obstacles` | `[obstacle id]` | Named obstacle ids **enabled** in this config (managed obstacles not listed are disabled). Only obstacles with an `id` can be config-managed. |

```yaml
configs:
  start: intro
  intro:                 # Julia alone — managed set all off
    present: {}
  puzzle:                # Julia + Dr. Schneider
    present:
      npcs:
        schneider: { at: schneider_start, facing: left }
  alone:                 # Schneider has left again
    present: {}
```

The loader fails (`room.config-*`) on a missing/unknown `start`, an unknown
point/object/obstacle reference, or an NPC entry without `at`. Configs compose with
ad-hoc `spawn_npc`/`despawn_npc` inside beats — a temporary actor added after the
reconcile lives until the next config change reconciles back to the declared set.

**Shaders** (on a layer, region, object, or [appearance](#cast--castyaml)) — see [03 § Shaders](03-2d-game-concepts.md) for the model. A **shader ref** is either a string (shorthand for `{source: <string>}`) or a mapping:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `source` | req | path | — | Fragment-shader logical path, **relative to the resources root** (not the room dir — shaders are shared). |
| `params` | opt | map name → value | — | Uniform values (see types below). |
| `enabled` | opt | bool | `true` | Skips the shader when `false`. |
| `controller` | opt | string | — | Id of a C++ controller hook (design-for; reserved — drawable draws unshaded until the registry lands). |

**`params`** value forms: `true`/`false` → `bool`; a number → `float` (a bare integer included — use `{type: int, value: N}` for an `int`); a 2/3/4-number sequence → `vec2`/`vec3`/`vec4`; `{type: <bool|int|float|vec2|vec3|vec4>, value: …}` to force a type. Names starting with `u_`, and `texture`, are reserved for engine-set uniforms (`u_time`, `u_resolution`, `texture`).

```yaml
# layer/region/object shader, full form
shader:
  source: shaders/water.frag
  params:
    wind_speed: 0.5            # float
    samples: {type: int, value: 8}
    center: [0.5, 0.3]         # vec2
    tint:   [0.2, 0.4, 0.8, 1.0]   # vec4
```

## Cutscene — `cutscenes/<id>.yaml`

Data file for the `Cutscene` scene type (issue #116) — a list of slides over a
solid-black background, with auto / manual / timed advancement.

Top level:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | `1` | Format version. |
| `advance_mode` | opt | enum | `auto` | `auto`, `manual`, or `timed` — see below. |
| `audio` | opt | path | — | Background music, played at scene start. In `timed` mode it also drives slide transitions (slides read its playback offset so they stay in sync when the engine stutters); in `auto` / `manual` it loops as background. |
| `audio_persist` | opt | bool | `false` | Keep `audio` playing past the cutscene so the next scene owns it (e.g. a room script calls `stop_music()` on entry). Otherwise the track stops when the cutscene ends. |
| `fade` | opt | float \| map | `0` | Dip-to-black between slides (and fade-in at the start / fade-out at the end). A number sets both halves; a map is `{in, out, color}` — seconds, plus the fade `color` (default black). `0`/`0` = hard cuts. Applies to `auto` / `manual`; `timed` keeps hard cuts to stay locked to its audio. |
| `defaults` | opt | map | — | Style / layout defaults applied to every slide. Per-slide fields override these. |
| `slides` | req | `[slide]` | — | Non-empty list of slides. |

**Advance modes:**

| Mode | Behavior | Input |
|------|----------|-------|
| `auto` | Each slide stays on screen for `duration` seconds, then advances. Equivalent to the old text-only cutscene. | `Esc` skips. |
| `manual` | Player advances slides themselves. A localized hint is drawn at the bottom-right (`strings.ui.manual_continue_hint`). | `Enter` / `Space` / left-click advance; `Esc` skips. |
| `timed` | Slides become active at their `at` timestamp. When `audio` is set, the music playback offset drives the clock; otherwise the scene's wall-clock is used. | `Esc` skips. |

**`defaults`** — any subset of:

| Field | Type | Meaning |
|-------|------|---------|
| `text_style` | map | Default `{font, size, color, outline_color, outline_thickness}` (see slide section). |
| `text_position` | `[x, y]` | Default normalized anchor for slide text. |
| `text_align` | enum | Default `left` / `center` / `right`. |
| `text_band` | map | Optional `{color, height}` scrim drawn behind the text — a "lower third" for readable narration over a full-bleed image. `height` is a fraction of screen height (`0` = none); the color's alpha sets opacity. |
| `image_position` | `[x, y]` | Default normalized anchor for slide images. |
| `image_size` | `[w, h]` | Default normalized box (max extent in screen space). |
| `image_fit` | enum | `contain` (preserve aspect) or `stretch`. |
| `duration` | float | Default per-slide duration for `auto` mode. |

**`slide`** — one slide:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `text` | opt | string | — | Single-line text drawn at `text_position`. |
| `image` | opt | path | — | Image drawn behind the text. |
| `text_position` | opt | `[x, y]` | from `defaults` | Normalized anchor (0–1). |
| `text_align` | opt | enum | from `defaults` | Horizontal alignment relative to the anchor. Vertical anchoring is always centered on the y. |
| `text_style` | opt | map | from `defaults` | Per-slide override. Each field (`font`, `size`, `color`, `outline_color`, `outline_thickness`) composes individually with the defaults. |
| `text_band` | opt | map | from `defaults` | `{color, height}` scrim behind this slide's text (`height` 0 = none); composes with `defaults.text_band`. |
| `image_position` | opt | `[x, y]` | from `defaults` | Normalized anchor; the image is centered (horizontally and vertically) on this point. |
| `image_size` | opt | `[w, h]` | from `defaults` | Normalized box. `contain` keeps the image inside it; `stretch` fills it. |
| `image_fit` | opt | enum | from `defaults` | `contain` or `stretch`. |
| `duration` | opt | float | from `defaults.duration` | `auto` mode only — seconds the slide stays before advancing. |
| `at` | req in `timed` | float | — | Seconds from scene start (or audio offset) when the slide becomes active. Must be monotonically non-decreasing across slides. |

**Positions are normalized screen coordinates:**

```text
[0.0, 0.0] = top-left
[0.5, 0.5] = center of the screen
[1.0, 1.0] = bottom-right
```

**`text_style.color`** and **`outline_color`** accept `"#RRGGBB"` or
`"#RRGGBBAA"` (case-insensitive, `#` optional). White (`#FFFFFF`) is the default
fill. A non-zero **`outline_thickness`** (virtual px) draws an outline in
`outline_color` (default opaque black) — a cheap way to keep narration legible
over a full-bleed image without a `text_band` scrim.

```yaml
# A full-bleed manual intro: the music carries into the next scene, slides dip to
# black, and the narration uses a black outline (no scrim) to stay readable.
version: 1
advance_mode: manual
audio: music/intro.mp3
audio_persist: true
fade: { in: 0.5, out: 0.5 }

defaults:
  image_position: [0.5, 0.5]
  image_size: [1.0, 1.0]
  image_fit: contain
  text_position: [0.5, 0.88]
  text_align: center
  text_style:
    size: 27
    color: "#F2F0E8"
    outline_color: "#000000"
    outline_thickness: 2

slides:
  - image: cutscenes/island.png
    text: "El Cairo, 1936."
    text_style: { color: "#F0D87A" }
  - text: "Algo no anda bien..."
```

```yaml
# A narrated cutscene: audio drives the slide changes via their `at` timestamps.
advance_mode: timed
audio: audio/intro_narration.mp3
slides:
  - at: 0.0
    image: cutscenes/lake.png
  - at: 3.2
    text: "Todo empezó una noche de viento."
  - at: 7.5
    image: cutscenes/institute.png
    text: "En el instituto, una máquina seguía funcionando."
```

The existing `StoryText` Lua-driven cutscene is unaffected — pick `Cutscene`
for declarative slide shows and `StoryText` when you need scripted timing or
branching.

## Close-up — `closeups/<id>.yaml`

A full-screen examine view (the `CloseUp` scene; design 04 §Genre scenes). Top
level:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | `1` | Format version. |
| `id` | req | string | — | Stable close-up id. |
| `background` | req | path | — | Full-screen background image (scaled to the virtual resolution). Resolved **relative to this YAML file's directory** (a co-located image is just `background.png`); a leading `/` makes it resources-root-relative (`/rooms/b/bg.png`) for sharing an asset from elsewhere. |
| `background_color` | opt | `{r, g, b, a?}` | black | Fill shown behind a transparent background. |
| `hotspots` | opt | map | — | Examinable regions (see below). |

**`hotspots`** — map of hotspot id → hotspot:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `name` | opt | string | id | Localized label; shown as a caption on a look and as the hover label. |
| `area` | req | polygon | — | Hit-test polygon, in the close-up's virtual-resolution space (≥ 3 points). |
| `goto` | opt | scene id | — | If set, clicking switches to this scene (when the hotspot has no scripted handler); otherwise the click shows `name` as a look caption. |

### Close-up scripting — `closeups/<id>.lua` (optional)

When the `CloseUp` scene declares a `logic:` param (see below), the named Lua file
gives the close-up its behavior. It returns a table with optional `on_enter`/
`on_exit` functions and a `hotspots` map of hotspot id → click-handler function;
clicking a hotspot runs its handler. Full surface + rules: [05 §Close-up
scripts](05-scripting-api.md). A scripted hotspot handler takes precedence over the
YAML `goto`/`name` action.

**Scene parameters** (manifest `parameters:` for a `type: CloseUp` scene): `data`
(req, path to the close-up YAML), `logic` (opt, path to the Lua sidecar — enables
scripting), `cast` (opt, cast file for `talk` speech colours), `font` (opt), and
`on_exit` (opt scene id entered on back-out; omitted ⇒ pop back to the opener).

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
| `anchors` | opt | map name → `{x, y}` | — | Frame-local pivot/attachment points (origin = frame top-left). A `head_pivot` anchor, when present, is where a character's speech balloon floats above; without it the engine estimates the head as the top-centre of the frame. |

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
[04 — Point & click concepts](04-point-and-click-concepts.md). The dialog id is the
file name; by default it is also the speaking NPC's cast character id, so there is no
speaker field in the file. `start_dialog(id, speaker)` overrides the speaker, letting
one NPC own several topic-named dialog files (e.g. `dialogs/skull_trauma_cause.lua`
spoken by `schneider`).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `start` | req | node id | — | Entry node. |
| `text_anchor` | opt | point id | — | Room point where this dialog's NPC speech is drawn; defaults to following the NPC avatar. |
| `on_enter` | opt | function | — | Setup callback. |
| `on_exit` | opt | function | — | Cleanup callback. |
| *(node id)* | — | node | — | Each remaining key is a node. |

**`node`:** `npc` (string or `[string]`), and either `options` (`[option]`) or
`to` (node id / `END`). A node may also carry `run` (a function fired
**synchronously on entry**, before the NPC line — for synchronous side effects like
recording a fact; an option `run` is the coroutine-spawned, blocking-capable form).
In a `dialog { ... }` tree a node may also carry `topics` (`[topic]`, expanded into
options + response nodes — see below).

**`option`** (a list whose first element is the player line):

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `[1]` | req | string | — | Player line text. |
| `to` | req | node id \| `END` | — | Next node. |
| `when` | opt | function → bool | always | Visibility condition. |
| `run` | opt | function | — | Code run **when the option is selected** (spawned as a coroutine task; a *node* `run` fires on entry instead — see node fields). |
| `once` | opt | bool | `false` | Option disappears after use (persisted by `(node_id, option_index)`). |
| `silent` | opt | bool | `false` | Player line is not spoken aloud. |

**Topic shorthand (`dialog { ... }` + `topic`).** Wrapping the returned table in
`dialog { ... }` lets a node carry a `topics` list instead of repeating the
hub-option-plus-response-node pattern by hand; the wrapper expands it to the
standard form (option + response node routing back to the hub) before validation.
Pure Lua sugar — see [05 §Topic shorthand](05-scripting-api.md). A node's optional
`topics` field is a list of `topic "id" { ... }` descriptors:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | node id | — | The string before the `{ }`; becomes the response node id (must not collide with another node). |
| `player` | req | string | — | Player line — the generated option's text. |
| `npc` | req | string \| `[string]` | — | The reply, as a node's `npc`. |
| `requires` | opt | state-key string \| function → bool | always | Offered only when the key is `true` / the predicate returns true. |
| `after` | opt | topic id \| `[topic id]` | — | Offered only after the named topic(s) were uttered. |
| `run` | opt | function | — | Extra action when chosen, after the topic is marked uttered. |

Stating a topic sets the reserved `__uttered.<id>` flag (persisted like any
`set_state`); the topic then hides itself (offered once). Read it with
`uttered(id)` to gate later topics or build cross-topic predicates.

## Inventory — `inventory.yaml`

Items are declared in `inventory.yaml`. Worked example:
[04 — Point & click concepts](04-point-and-click-concepts.md).

Top-level fields: `items` (req), a map of item id → item; and `icons` (opt), the
icon source mode for the SCUMM panel's icon slots.

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `name` | req | string | — | Localized display name. |
| `icon` | opt | path | none | Individual icon PNG (development icon mode), scaled into the panel slot. |
| `icon_cell` | opt | int | `-1` | Row-major, 0-based cell index into the shared sheet (production icon mode). |
| `description` | opt | string | — | Optional examine text. |
| `affordances` | req | `[verb]` | — | Verbs the UI may offer. |
| `default_verb` | opt | verb | `look_at` | Verb on a plain click; must be `look_at` or in `affordances`. |
| `combinable` | opt | bool | `false` | Whether `use` expects a second operand. |

The optional `icons` block selects how the panel sources item icons (issue #172):

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `icons.mode` | opt | enum | `development` | `development` = one PNG per item (`item.icon`); `production` = a shared MxN sheet (`item.icon_cell`). |
| `icons.sheet` | prod | path | — | Sheet texture, required when `mode: production`. |
| `icons.columns`, `icons.rows` | prod | int | `1` | Sheet grid; the cell size is the texture size divided by the grid. Must be positive in production. |

Development mode keeps the loose, any-size individual PNGs that are quick to
iterate on; production packs everything into one texture (one GPU upload) and each
item picks its cell by index.

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

## Facts — `facts.yaml`

Optional. The central inventory of the game's boolean state-flag keys, enabling
the `facts.<ns>.<name>` typo guard (issue #188; see
[05 § Declared facts](05-scripting-api.md)). A missing file leaves the proxy
working as plain `get_state`/`set_state` sugar with the guard off, so a game opts
in simply by shipping this file. Auto-discovered at the resource root as
`facts.yaml`.

```yaml
version: 1
namespaces:
  act1:    [bones_glanced, context_glanced, thermo_checked]
  finding: [radial_fractures, no_cut_marks, no_collapse]
  case:    [report_ready, schneider_dialog_done]
```

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | `1` | Format version. |
| `namespaces` | req | map | — | Maps each namespace id to a sequence of flag names. Each `<ns>: [<name>, ...]` pair declares the facts `<ns>.<name>`. |

Validation (loud, on load): `namespaces` must be a mapping of id → sequence;
namespace and key names must be non-empty; a duplicate `<ns>.<name>` is an error.
Declare **boolean flags** only — numeric counters and dynamic keys
(`"llamas." .. id .. ".done"`) stay on `get_state`/`set_state` and are not
declared. The guard is active in development builds (and in Release when
`development.show_state` is set); a key outside this registry warns but still
reads/writes, so release is never blocked.

## Player settings — `settings.yaml`

Player-facing preferences, persisted in the per-user **config** location (not the
resource root): `$XDG_CONFIG_HOME/<id>/settings.yaml` (or `~/.config/<id>/…`) on
Linux, `%APPDATA%\<id>\settings.yaml` on Windows, `Application Support/<id>` on
macOS. Written by the settings scene when the player applies a change. On startup
the file is overlaid on the manifest defaults: manifest defaults < user settings,
and a missing file (first run) or a corrupt one falls back to the defaults.

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `audio.music_volume` | opt | float `0..1` | manifest | Music volume. |
| `audio.sfx_volume` | opt | float `0..1` | manifest | SFX volume. |
| `display.fullscreen` | opt | bool | manifest | Fullscreen vs windowed. |
| `display.width` / `display.height` | opt | int | manifest | Windowed client size. |
| `language` | opt | language id | manifest default | Active UI-strings language (a `languages` id). An unknown id falls back to the default. |

Only keys present in the file are applied, so a partial file still loads. Example:

```yaml
version: 1
audio: { music_volume: 0.8, sfx_volume: 0.8 }
display: { fullscreen: false, width: 1280, height: 720 }
language: es
```

## Save file — `GameState`

The save payload and its YAML serialization are specified under "Make persistent
state explicit" in [02 — Architecture overview](02-architecture-overview.md);
format, slots, and autosave policy are in
[R8](01-engine-requirements.md). Saves carry a `save_version` integer.

Top-level fields the save/load picker uses (issue #108):

| Field | Type | Meaning |
|-------|------|---------|
| `save_version` | int | Format version; older/newer values are refused. |
| `description` | string | Player-supplied short label shown in the picker. The autosave normally leaves this empty so the row falls back to its timestamp. |
| `saved_at` | int (Unix seconds) | Wall-clock at save time, stamped by `SaveService::save`. The picker formats this as a local date/time; an older save without the field falls back to the file's mtime. |

`SaveService::slot_summary(slot)` reads only these header fields off disk so
the picker UI never has to decode the full payload to list slots.

### Thumbnail sidecar (issue #119)

Saves carry an optional preview image alongside the YAML: `slot_N.thumb.png`
next to `slot_N.yaml`, written when `SaveService::save` is called with a
non-empty `sf::Image`. The runtime captures it from the framebuffer:

- The app loop refreshes a `Thumbnail` service every ~half-second while the
  active scene returns `wants_thumbnail() == true` (RoomScene's COMMAND
  state). The captured image is cropped to the gameplay viewport (no
  letterbox bars) and downscaled to 256×144.
- When the player picks "Guardar partida" from the in-game pause menu,
  RoomScene stages both the snapshot *and* the latest thumbnail; the save
  picker writes both atomically.
- The save picker loads each slot's sidecar when present and renders it in
  the slot row, falling back to the "Sin captura" placeholder when missing
  (an older save, a slot whose first capture never landed, or a corrupt
  PNG). A failed thumbnail write is logged but does **not** fail the save.
