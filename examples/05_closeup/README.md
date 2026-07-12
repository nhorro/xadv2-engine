# 05 — Close-ups (and shaders, and a cursor)

**Shows:** examining something up close — a full-screen overlay with its own
hotspots and its own Lua. Plus two smaller things this was the natural place for:
a shader chain on a room layer, and a custom mouse cursor.

```bash
./build/examples/05_closeup/pac_example_05_closeup
```

## Play it

`Look at` the painting. Click the face, the crack, the signature — each says its
piece. Back out with [esc] or right-click: you are exactly where you were, and
the player now remarks on what he found.

## Read, in this order

| File | What it teaches |
|---|---|
| `data/rooms/gallery.lua` | `open_closeup("painting_closeup")`, and `on_room_resume(fn)` — armed **before** opening, it fires when the room is back on top. |
| `data/closeups/painting/closeup.yml` | Background + hotspot polygons. No verbs, no walkable area: you are looking, not acting. Traced with the close-up editor. |
| `data/closeups/painting/logic.lua` | A function per hotspot. `talk()` **blocks** here (handlers are coroutines), so lines play one after another. Findings go into `set_state` so the room can read them. |
| `data/rooms/gallery.yaml` | A `shaders:` chain on the layer — a warm grade, then a vignette. Drop the block and the art draws untouched. |
| `data/game.yaml` | The `cursor:` block: a custom pointer, with an `interact` variant swapped in automatically over anything clickable. |

## The idea worth taking away

A close-up is an **overlay, not a room change**. The room stays loaded underneath,
so backing out is free and nothing has to be restored. It is the right tool for
"look closer" and the wrong tool for "go somewhere".
