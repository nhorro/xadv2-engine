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
| `rendering` | opt | `{smooth_textures?}` | — | Rendering policy. `smooth_textures` defaults to `true`, enabling linear filtering for loaded textures and shader/post-process render targets to reduce temporal shimmer when art moves or scales fractionally. Pixel-art games can set it to `false` for nearest-neighbour sampling. |
| `resources` | req | `{src}` | — | Resource source root. `src` is a directory (MVP) or archive (design-for). |
| `strings` | req* | path | — | Single-language shorthand: the UI strings resource (engine-emitted text). Mutually exclusive with `languages`; exactly one is required. See [UI strings](#ui-strings--stringslangyaml). |
| `languages` | req* | `[{id, name?, strings}]` | — | UI-strings languages (R3). Each entry: `id` (stable ASCII id stored in settings), `name` (opt display label in its own language, defaults to `id`), `strings` (path to that language's file). The MVP ships one entry (Spanish); the list is design-ready for more. |
| `default_language` | opt | language id | first entry | Which `languages` entry is active when no player preference is stored. Must name an entry in `languages`. |
| `speech` | opt | `{font?, font_size?}` | scene font, `24` | Shared typography for character `talk` / `remark` lines in rooms and close-ups. `font` is a logical resource path; when omitted, the active scene's UI font is used as a compatibility fallback. `font_size` is a positive integer in virtual pixels. |
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
  ids `new_game`, `continue` (resumes the most recent save — the autosave or a
  manual slot, whichever is newer; by default the entry shows only when a save
  exists), `load_game` (opt, #108 — when wired *and* a save exists, the title adds
  a "Recuperar partida" entry that pushes the load picker; the value is reserved
  for a future opt-out routing override, any non-empty value enables the entry),
  and `exit` (often `QUIT`). Menu entries are borderless text; hover highlights
  the entry and switches the cursor to its interact variant.
  `menu.continue_fallback: new_game` (opt) instead keeps `Continue` always listed
  and makes it start a new game when nothing is saved, so it reads as a stable
  "just play" entry rather than one that appears and disappears. An unreadable
  save is *not* a fallback case: the title stays put and logs, rather than
  silently starting the player over.
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
  `development_logic` (opt path — removable development sidecar whose
  `on_start()` runs after the main game hook and may return a start-room id),
  `player` (req, cast character id — the persistent player avatar; appearance comes
  from this character's cast entry), `font` (opt path — default panel and other
  room UI text; spoken-line typography comes from top-level `speech`),
  `scumm_panel` (opt path — YAML panel layout/skin config),
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

Scenes that render UI text take their `font` as a logical-path parameter. Spoken
`talk` / `remark` lines instead use the manifest's top-level `speech.font` and
`speech.font_size`, consistently across rooms and close-ups. If `speech.font` is
omitted, the scene font is used as a compatibility fallback. Cast entries still
own character-specific speech colour and placement clearance (`speech_gap`).

## SCUMM panel config — `ui/scumm_panel*.yml`

<a id="scumm-panel-config--uiscumm_panelyml"></a>

`RoomScene.parameters.scumm_panel` may point at a panel layout/skin YAML file.
When omitted, the engine uses the built-in classic 85% scenery / 15% panel
layout. The config controls visual layout only: command construction and command
preview state still live in `CommandController`.

**The panel's top edge sets the scene/panel split.** `RoomScene` derives the
scenery viewport height from `layout.panel.rect.top` (converted from `design_size`
to the virtual resolution), so it also governs the camera viewport, hotspot
hit-testing and walk clamping — not just where the panel is painted. A panel that
keeps the classic rect lands back on exactly 85%. Room geometry (walkables,
points, hotspots) must stay above that line or it is drawn under the panel and
cannot be clicked.

Coordinate rules:

- `scumm_panel.design_size` defines the nominal coordinate system.
- `layout.panel.rect` is in `design_size` coordinates.
- `layout.command_bar.rect` and `layout.body.rect` are relative to the panel.
- `layout.verb_panel.rect`, `layout.inventory_panel.rect`,
  `layout.inventory_pagination.rect`, `system_buttons[].rect`,
  `evidence_indicator.rect`, and `notebook.rect` are all relative to the body.
- `layout.inventory_arrows.*.hitbox` is relative to the inventory panel;
  `layout.inventory_pagination.previous` / `.next` are relative to the pagination
  rect.
- `settings_button.position` is normalized within the panel: `[0, 0]` is the
  panel's top-left and `[1, 1]` is its bottom-right. `anchor` determines which
  point of the button is placed there.
- At runtime, rectangles and hitboxes scale by
  `runtime_width / design_width` and `runtime_height / design_height`.

Colors are `#RRGGBB`, or `#RRGGBBAA` where a control should fade rather than
recolor (e.g. a disabled cell dimming over panel art).

!!! warning "State the grid `padding` and `cell_gap`"
    `layout.verb_panel` and `layout.inventory_panel` inherit the *classic* grid
    defaults for any key they omit — including the inventory's 52px right padding
    (the old arrow gutter) and 8px cell gap. A new panel that sets its own `rect`
    but leaves those unset will get non-square, mis-sized cells. Set them
    explicitly.

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
| `layout.inventory_arrows.mode` | opt | enum | `draw` | `draw`, `background_variants`, or `none`. Paging for the **text** inventory. |
| `layout.inventory_pagination` | opt | map | disabled | Paging for the **icons** inventory, in its own zone instead of a gutter cut from the inventory rect. Body-relative `rect`, plus `previous` / `next` sub-rects. Requires `inventory_panel.style: icons`. |
| `content.verbs` | opt | `[verb id]` | classic 9 verbs | Row-major verb order. Use canonical ids such as `open`, `look_at`, and `pick_up`; labels come from UI strings. |
| `evidence_indicator` | opt | map | disabled | Optional `{label} x/n` readout (issue #172). See below. |
| `notebook` | opt | map | disabled | Optional in-panel notebook access links (issue #172). See below. |
| `settings_button` | opt | map | disabled | A single Settings button placed by a normalized `position` + `anchor`. Superseded by `system_buttons` for anything with more than one control; both still work. |
| `system_buttons` | opt | list | none | Ordered systemic controls, each with a body-relative `rect`, an `action` (`open_settings`, `open_menu`, `push_scene` + `scene`), an optional `label_key` and `icon`, and `render_mode: panel \| image`. See below. |
| `skin.command_text`, `skin.verb_text`, `skin.inventory_text`, `skin.system_button_text` | opt | text style | built-in colors | `font`, `size`, `color`, `hover_color`, `selected_color`, `disabled_color`, `align`, and an optional `outline_thickness` (design px, `0` = none) + `outline_color` (default black). The `evidence_indicator.text` / `notebook.text` styles take the same fields. |
| `skin.verb_button`, `skin.inventory_slot`, `skin.system_button` | opt | button skin | classic accent fills | The *box* of a framed control, per state: `background`/`border`, `hover_background`/`hover_border`, `selected_background`/`selected_border`, `disabled_background`/`disabled_border`, and `border_thickness` (design px). The label color lives in the matching text style. Borders grow inward, so a thick highlight never bleeds into the neighbouring cell. |
| `skin.command_bar` | opt | map | classic strip | `background`, `separator`, `separator_thickness` (design px; `0` = no rule) for the command-bar band drawn over a solid panel. |
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
  drawn placeholder glyph. An empty slot keeps its frame and draws nothing else.
  The held item that is the command's current operand keeps the `selected_*`
  accent — including across a page flip, since selection is tracked by item id.
  `style: text` (default) keeps the localized-name list.
- The icons inventory pages with vertical arrows. By default they sit in a gutter
  carved out of the right of the inventory rect; set `layout.inventory_pagination`
  to move them into their own zone and give the slots the whole rect.
  `layout.inventory_arrows` does not apply to the icons style.

Systemic buttons (`system_buttons`) — an ordered list of panel controls that are
UI navigation, not command grammar: they never select verbs, select inventory,
page inventory, or mutate command state. Each has a body-relative `rect`, so a
panel can lay out a real controls column (`settings_button` could only ever be one
control anchored at a normalized point).

- `action: open_settings` opens the `SettingsScene` overlay
  (`SceneManager::open_settings()`).
- `action: open_menu` opens the in-room pause menu (save / load / settings / quit).
- `action: push_scene` pushes the named `scene` — the general escape hatch (a
  journal, a map, a close-up).
- `render_mode: panel` (default) draws a `skin.system_button` box with an optional
  leading `icon` and the `label_key` label; `render_mode: image` draws
  `image.normal`, switching to `image.hovered` under the pointer.
- Dialog mode hides and disables them. Their column is reclaimed for dialog
  options, with any pagination arrows placed at the panel's true right edge.

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

<a id="ui-strings--stringslangyaml"></a>

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
| `ui` | req | map key → string | — | Built-in UI labels. Title menu (`new_game`, `continue`, `settings`, `settings_button`, `quit_to_os`); save/load picker (`save_game`, `load_game`, `save_button`, `load_button`, `autosave`, `slot`, `slot_empty`, `description_hint`, `thumbnail_placeholder`); in-game pause (`pause`, `resume`, `settings`, `quit_to_title`); settings (`back`, `apply`, `resolution`, `fullscreen`, `language`, `music`, `sfx`, `on`, `off`); the cutscene manual-continue hint (`manual_continue_hint`); and the top-bar walk label `walk_to` (shown when hovering walkable floor). |
| `defaults` | req | map key → string | — | Engine last-resort captions, spoken when no game handler produced text for a verb. The loader requires the exact key set below — no missing keys, and (in dev) no unknown keys. |

The two exit labels are intentionally distinct: `quit_to_os` closes the
application from the title screen, while `quit_to_title` leaves the active game
and returns to that title screen.

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
  quit_to_os: "Salir del juego"
  quit_to_title: "Volver al inicio"

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

<a id="cast--castyaml"></a>

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
| `ambience` | opt | `{transition?, base?, random?}` | — | Room soundscape: one persistent streamed base loop plus randomly scheduled one-shot layers. Entering a room without this field fades out any previous room ambience. See **Room ambience** below. |
| `post_process` | opt | `{enabled?, shader?/shaders?}` | — | Shader stack applied once to the fully composed scenery viewport. Layers, objects, walk-behinds, avatar sprites, and their shadows are included; speech, debug overlays, and the room UI remain unprocessed. `enabled: false` bypasses the stack without removing its parameters. See **Room post-processing** below. |
| `lighting` | opt | `{ambient?, lights?, projected_shadows?}` | — | Room-level illumination and shadows. `ambient` plus `lights` runs one dynamic-light pass over the composed scenery before grading; `projected_shadows` draws live avatar silhouettes. See **Dynamic room lighting** and **Projected avatar shadows** below. |
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
| `origin` | opt | `{x, y}` | `{0, 0}` | Room-space top-left of the layer image (layers may differ in size and be placed freely). Bounds-extending layer rects determine the room's world bounds, floored to the room view; see [04 § World bounds](04-point-and-click-concepts.md). |
| `scale` | opt | number | `1.0` | Uniform render scale about `origin`, **aspect always preserved** (never distorted). A development aid for sizing furniture-style occluders; production layers ship native (`1.0`). The room editor resizes about the layer's base (bottom-centre) and can set `z` to the scaled base line. |
| `interactive` | opt | bool | `false` | Whether the layer receives pointer interaction. |
| `visible` | opt | bool | `true` | Initial visibility. Toggle at runtime with `set_layer_visible(id, bool)` (needs an `id`); persisted per room. |
| `extend_bounds` | opt | bool | `true` | Whether the layer can enlarge the room's world bounds. Set `false` on foreground art that should be clipped to the base scenery canvas. |
| `shader` | opt | shader ref | — | A shader applied when drawing the layer. See **Shaders** below. |
| `shaders` | opt | `[shader ref]` | — | Ordered shader stack — the engine runs them as a multi-pass chain in this order. See **Shaders** below. |
| `animation` | opt | anim ref | — | Design-for. |

`background.color` (`{r, g, b, a}`) is an optional solid fill behind all layers.

**Room ambience** uses resources-root-relative logical paths:

```yaml
ambience:
  transition: 3.0
  base:
    sound: sounds/city.ogg
    volume: 0.8
  random:
    - id: traffic
      sounds: [sounds/car_pass.ogg, sounds/horn.ogg]
      delay: {min: 5, max: 16}
      volume: {min: 0.2, max: 0.55}
      pan: {min: -0.9, max: 0.9}
```

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `transition` | opt | non-negative number | `2.5` | Seconds used to crossfade different bases, glide the same base to its new room volume, or fade an omitted base out. |
| `base.sound` | req when `base` exists | path | — | Streamed looping foundation. Adjacent rooms using the same path share uninterrupted playback. |
| `base.volume` | opt | number `0..1` | `1` | Room-relative base gain, scaled by the player's SFX setting. |
| `random[].id` | req | unique string | — | Runtime control id used by `set_ambience_layer_enabled` and `set_ambience_layer_volume`. Keep it stable across connected rooms to preserve its countdown and Lua overrides. |
| `random[].sound` | req if `sounds` omitted | path | — | A single candidate one-shot. |
| `random[].sounds` | req if `sound` omitted | `[path]` | — | Candidate one-shots; one is chosen uniformly each occurrence. |
| `random[].delay` | opt | number or `{min, max}` seconds | `{5, 15}` | Random delay before each occurrence, including the first. Minimum is `0.01`. |
| `random[].volume` | opt | number or `{min, max}` in `0..1` | `1` | Per-occurrence gain, scaled by the player's SFX setting and any Lua layer multiplier. |
| `random[].pan` | opt | number or `{min, max}` in `-1..1` | `{-1, 1}` | Per-occurrence stereo position. Panning affects mono clips; stereo clips play as authored. |

`base` and `random` are independently optional, so a room may have only a bed,
only sporadic details, or both. A random layer's short clips are decoded through
the sound cache; the base streams from the resource source and works identically
with loose files and `resources.pak`.

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
| `sprite` | req | path | — | A static image, `*.anim.yml` AnimatedSprite, or `*.composite.yml` CompositeSprite. YAML-backed visuals share the object animation API. |
| `sequence` | opt | string | — | Initial animation/composite sequence. Recommended so the object shows a frame on load. |
| `position` | req | `{x, y}` | — | World position — the **top-left** for a static image, the visual **pivot** for animated/composite objects. |
| `z` | opt | `auto` \| number | `auto` | `auto` = the sprite's bottom edge (scaled); a number overrides. |
| `scale` | opt | number | `1.0` | Uniform render scale about `position`, aspect always preserved (like a layer's `scale`); must be > 0. The room editor sets this when resizing an object. |
| `rotation` | opt | number | `0` | Clockwise degrees about `position`; scriptable through the object handle. |
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
| `enter_from` | opt, player only | point id | — | On a normal room entry, place the player at this point (typically just beyond a screen edge), block input, and pathfind to `start`. Skipped for save restores and `change_room` calls with an explicit entry point. When the walk finishes, the room's optional `on_player_entered` Lua hook fires. The point must be inside the room's `walkable` polygon even when it lies beyond the visible background. |
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

**Room post-processing** uses the same shader refs and parameter types, but runs
after the scenery has been composed rather than once per drawable:

```yaml
post_process:
  enabled: true
  shader:
    source: shaders/color_grade.frag
    params:
      tint: [0.92, 0.97, 1.04]
      brightness: -0.05
      contrast: 1.08
      saturation: 0.82
      strength: 0.75
```

Set the outer `enabled: false` to compare against the unprocessed room while
keeping the authored shader and parameters intact. A `shaders:` list is accepted
instead of `shader:` for an ordered multi-pass stack.

**Dynamic room lighting** darkens the fully composed scenery with an ambient
term, adds up to eight visible omnilights / spotlights, then hands the result to
`post_process`. Local layer/object/avatar shaders therefore run before lighting;
colour grading runs after it. Speech, debug overlays, and UI remain unaffected.

```yaml
lighting:
  ambient: {color: [0.58, 0.64, 0.78], intensity: 0.36}
  lights:
    - id: desk_lamp
      type: omni
      at: {x: 520, y: 230}
      radius: 260
      color: [1.0, 0.76, 0.42]
      intensity: 0.85
      modulation: {type: flicker, amount: 0.04, speed: 5, seed: 12}
    - id: flashlight
      type: spot
      attach: player
      offset: {x: 24, y: -65}
      range: 420
      direction: -4
      follow_facing: true
      angle: 42
      softness: 10
      color: [0.90, 0.95, 1.0]
      intensity: 1.0
```

`ambient` defaults to white at intensity `0.35` whenever dynamic lighting is
declared. Its colour components and intensity are in `[0, 1]`. Keep source art
neutral/bright and use ambient illumination for runtime darkness; this preserves
colour information for lights to reveal.

Each entry in `lights` supports:

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `id` | req | unique string | — | Stable authoring id. |
| `type` | req | `omni` / `spot` | — | Radial point light or directional cone. |
| `at` | req* | `{x, y}` | — | Static origin in room coordinates. Exactly one of `at` / `attach` is required. |
| `attach` | req* | `player` / `avatar:<id>` / `object:<id>` | — | Follow a live entity. A missing/hidden attachment suppresses the light for that frame. |
| `offset` | opt | `{x, y}` | `{0, 0}` | World-pixel offset from an attachment. |
| `radius` / `range` | req | positive number | — | Radial reach in room pixels (`range` is a spotlight-friendly alias). |
| `color` | opt | `[r, g, b]` in `[0, 1]` | white | Light colour. |
| `intensity` | opt | number in `[0, 4]` | `1` | Peak contribution before modulation. The LDR compositor clamps final illumination to 1. |
| `enabled` | opt | bool | `true` | Initial inclusion in the pass. |
| `direction` | spot* | degrees | — | Direction in screen coordinates: `0` right, `90` down. Required unless `follow_facing` is true. When following, it becomes an offset from the avatar's cardinal direction. |
| `follow_facing` | spot opt | bool | `false` | Rotate with an attached player/avatar. Invalid on static/object-attached lights. |
| `angle` | spot opt | degrees | `45` | Full outer cone angle; greater than 0 and less than 180. |
| `softness` | spot opt | degrees | `8` | Angular penumbra width at each edge; less than `angle / 2`. |
| `modulation` | opt | map | none | Time-varying intensity; see below. |

`modulation` is evaluated before uniforms are uploaded and does not add a render
pass. Its `type` is `none`, `sine`, `flicker` (smooth deterministic noise for
fire/torches), or `faulty` (noise plus occasional dropouts). `amount` is `0..1`,
`speed` is positive, and `seed` is any finite number. Defaults are `amount: 0.08`,
`speed: 6`, `seed: 0` when a modulation block exists.

Lights outside the camera are culled. When more than eight overlap it, the engine
draws the first eight authored visible lights and logs one warning. This fixed
budget keeps the single-pass shader predictable on ordinary laptop GPUs.

**Projected avatar shadows** are an optional room-level 2D treatment:

```yaml
lighting:
  projected_shadows:
    enabled: true
    light: {x: 180, y: 180}
    casters: player
    length: 0.62
    width: 0.78
    opacity: 0.30
    softness: 3.0
    contact_shadow: 0.60
    z: 1
    color: {r: 14, g: 17, b: 22}
```

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `enabled` | opt | bool | `true` | Bypass projected shadows when false. The appearance's normal contact ellipse is then drawn at full opacity. |
| `light` | req | `{x, y}` | — | Light origin in room coordinates. Each shadow points from this origin through the caster's current walking pivot. |
| `casters` | opt | `player` / `all` | `player` | Apply the treatment only to the player, or to the player and every present NPC. |
| `length` | opt | positive number | `0.45` | Fraction of the live sprite height projected along the floor direction. |
| `width` | opt | positive number | `0.75` | Scale of the silhouette across the projection direction. |
| `opacity` | opt | number in `[0, 1]` | `0.18` | Target opacity at the fully overlapped center of the softened silhouette. |
| `softness` | opt | non-negative number | `4.0` | Room-pixel radius of the sampled penumbra. Zero draws one crisp silhouette. |
| `contact_shadow` | opt | number in `[0, 1]` | `0.55` | Opacity multiplier for the appearance's ordinary ellipse, retained to keep the feet grounded. |
| `z` | opt | number | avatar depth | Fixed floor depth for both the projected silhouette and contact ellipse. Use a value above the base floor layer and below foreground props so furniture occludes shadows independently of avatar sorting. |
| `color` | opt | `{r, g, b}` | `{12, 14, 18}` | Shadow tint before the room's optional post-process is applied. |

The silhouette follows the caster's current animation, perspective scale, and
position. It is drawn immediately before that avatar at the same depth, so later
foreground layers can cover it. It is intentionally not clipped to walkable
polygons and cannot infer surfaces or occluders painted into a flat background;
author it as a subtle unifying effect rather than a physically exact shadow.

## Cutscene — `cutscenes/<id>.yaml`

<a id="cutscene--cutscenesidyaml"></a>

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
scripting), `cast` (opt, cast file for `talk` speech colours), `font` (opt, UI
text such as hover labels, banners, and the back hint), and `on_exit` (opt scene
id entered on back-out; omitted ⇒ pop back to the opener). Scripted `talk` uses
the manifest's top-level `speech` style.

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

**`sequence`:** `loop` (bool, default `false`), `h_mirror` (bool, default
`false`), and `frames` (`[{sprite, duration}]`) where `sprite` is a frame id and
`duration` is seconds. A mirrored sequence reuses the declared atlas frames but
renders them horizontally flipped; pivots and other frame-local anchors mirror
with the image.

## Composite — `<name>.composite.yaml`

Worked example: [03 — 2D game concepts](03-2d-game-concepts.md).

| Field | Req | Type | Default | Meaning |
|-------|-----|------|---------|---------|
| `version` | opt | int | 1 | Data-format version. |
| `root` | req | `{id, animation}` | — | Root animated sprite (e.g. body). |
| `children` | opt | `[child]` | — | Attached animated sprites. |
| `sequences` | req | map | — | High-level sequences mapping node ids to part playback/rotation. |

**`child`:** `id`, `parent` (id), `animation` (path), `parent_anchor` (anchor on
the parent), `child_anchor` (anchor on the child, aligned to `parent_anchor`), and
optional `offset`, `scale`, `rotation`, `z`. Lower `z` nodes draw first, allowing
wheels or props to sit behind the root artwork.

Each `sequences.<id>.parts.<node>` may select the node's ordinary AnimatedSprite
`sequence` and/or a rotation track:

```yaml
sequences:
  moving:
    parts:
      body: { sequence: idle }
      wheel:
        sequence: idle
        rotation: { from: 0, to: -360, duration: 0.72, loop: true }
```

Rotation is evaluated from elapsed time, independently of frame rate. Playing a
new composite sequence resets every node to its authored local rotation.

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

<a id="player-settings--settingsyaml"></a>

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
