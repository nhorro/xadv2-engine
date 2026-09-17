# Direct room interaction

`RoomScene` can replace the classic nine-verb panel with a compact, room-only
interface. Opt in through the composed scene parameters:

```yaml
scene_defaults:
  RoomScene:
    direct_room_ui: ./shared/ui/direct_room_ui.yml
```

This affects ordinary rooms only. Dialogs still use the dialog widget, and
close-ups, maps, notebooks, case resolution, and other pushed scenes keep their
own input and presentation.

Action-family hardware cursors are optional manifest assets. Missing family
images fall back to `interact`, then to the resting pointer:

```yaml
cursor:
  image: ui/cursor_arrow.png
  interact: ui/cursor_hand.png
  look: ui/cursor_magnifier.png
  look_seen: ui/cursor_magnifier_seen.png
  talk: ui/cursor_talk.png
  exit: ui/cursor_exit.png
  walk: ui/cursor_walk.png
  hotspot: {x: 5, y: 4}
  action_hotspot: {x: 32, y: 32}
```

## Gesture contract

- Click/tap a hotspot or inventory item: run its authored `default_verb`.
- Hovering a room hotspot uses its default action family cursor: magnifier for
  `look_at`, speech bubble for `talk_to`, and hand for open/close/push/pull/pick-up.
- Mark room transitions with `type: exit`; they use a textless exit cursor in
  every language, and their authored name is shown without a leading verb.
- Hovering walkable floor uses the `walk` cursor.
- Right-click a hotspot, or hold a touch over it, to open the secondary-action
  menu. It lists supported one-operand actions other than the default action;
  `give` and two-object `use` remain drag gestures.
- On touch screens, pressing and dragging updates the hovered hotspot before
  the long-press menu opens.
- Click/tap empty walkable floor: walk immediately.
- Drag an inventory item that affords `give` to an NPC: `give`. NPC hotspots
  accept the attempt intrinsically, so they only need to author `give` when they
  have a specific response; otherwise the normal fallback runs.
- Drag a combinable inventory item to another item or room hotspot: `use` when
  both operands afford it.
- Click/tap the bag: show or hide the compact bottom inventory.
- Click/tap the menu button: open the room pause menu; settings lives there.

Invalid drops are ignored and do not leave a hidden half-built command. While
an item is dragged, its artwork replaces the hardware cursor and the action
phrase starts as `Use <item> with`; over a valid target it updates to the full
localized `Use … with …` or `Give … to …` sentence before release.

`default_verb` belongs in the object's YAML and must be included in its
`affordances`. Keep inventory `use` items non-combinable when they activate by
themselves (for example, a map). Set `combinable: true` only when the item must be
dragged onto a target.

```yaml
hotspots:
  door:
    name: the door
    type: exit
    area: [{x: 20, y: 20}, {x: 90, y: 20}, {x: 90, y: 180}]
    approach: at_door
    default_verb: open
    affordances: [look_at, open, use]

items:
  map:
    name: the map
    icon: inventory/map.png
    default_verb: use
    affordances: [look_at, use, give]
    combinable: false
  key:
    name: the key
    icon: inventory/key.png
    affordances: [look_at, use, give]
    combinable: true
```

The resulting commands retain the normal approach, dispatch, fallback,
recording, and save behavior. The action phrase appears while hovering an
actionable target, while a command is accepted/executing, and briefly after
completion; otherwise it is invisible.

## UI configuration

All rectangles use `design_size` coordinates and scale to the virtual
resolution. The inventory is hidden initially; there is no visibility setting
in this file.

```yaml
direct_room_ui:
  design_size: [1280, 720]
  interaction:
    drag_threshold: 12
    long_press_seconds: 0.48
  bag_button: [1136, 632, 64, 64]
  menu_button: [1208, 632, 48, 64]
  action_text: [300, 566, 680, 42]
  action_text_offset: [18, 26]
  action_follows_cursor: true
  bag_label: inventory
  menu_label: options
  context_menu:
    cell_size: [78, 64]
    gap: 6
    padding: 8
  icons:
    sheet: shared/ui/direct_ui_icons.png
    columns: 4
    rows: 4
    bag: 0
    menu: 1
    previous: 2
    next: 3
    look_at: 4
    talk_to: 5
    pick_up: 6
    open: 7
    close: 8
    push: 9
    pull: 10
    use: 11
    give: 12
  inventory:
    panel: [522, 616, 590, 88]
    grid: [586, 624, 462, 72]
    rows: 1
    columns: 6
    cell_gap: [6, 0]
    compact_single_page: true
    compact_padding: 10
    previous: [534, 636, 40, 48]
    next: [1060, 636, 40, 48]
```

The optional `icons` mapping selects cells in a transparent atlas. A missing
sheet or a cell set to `-1` uses the engine's primitive fallback. Set
`compact_single_page` to fit the inventory ribbon to its occupied cells and
right-align it toward the bag; multi-page inventories retain stable geometry.

The optional `style` mapping accepts `#RRGGBB` or `#RRGGBBAA` colors for
`panel_background`, `panel_border`, `slot_background`, `slot_border`,
`hover_border`, `button_background`, `button_border`, `button_hover`, `text`,
`disabled_text`, `action_background`, `action_outline`, and `notification`. It
also accepts positive `border_thickness`, `text_size`, `action_text_size`, and
`action_outline_thickness` values. `button_radius_ratio` controls the persistent
control plate radius as a fraction of the button's shortest side and must not
exceed `0.5`. Set the action background alpha to zero for outlined text without
a container. Optional `bag_label` and `menu_label` values are localization keys;
when present, their labels fade in above the corresponding hovered control.
