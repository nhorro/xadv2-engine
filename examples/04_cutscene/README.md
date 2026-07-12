# 04 — Title screen and cutscenes

**Shows:** the shell around the game — a title screen, an intro cutscene that
fades, and a second cutscene fired from inside a room.

```bash
./build/examples/04_cutscene/pac_example_04_cutscene
```

## Play it

New game → the intro plays (click/[enter] to advance, [esc] to skip) → you land
in the yard. `Open` the gate: a short auto-playing cutscene runs and hands you
straight back to the room.

## Read, in this order

| File | What it teaches |
|---|---|
| `data/game.yaml` | Five scene types wired to each other **by id**: title's `new_game` → `intro`, intro's `on_finish` → `room_view`. That chain is the flow of the game. |
| `data/cutscenes/intro.yaml` | Slides. `advance_mode: manual`, per-slide `fade`, a `text_band` and text outline for readability over an image, `defaults` for what every slide shares. |
| `data/cutscenes/memory.yaml` | `advance_mode: auto` — each slide holds for its own `duration`. |
| `data/rooms/yard.lua` | `start_cutscene("memory")` from a verb handler. |

## The idea worth taking away

`start_cutscene` **replaces** the room (the cutscene's `on_finish` decides where
you go next); `open_closeup` (example 05) **overlays** it and pops back. Reach for
the first when the story moves, the second when the player just looks closer.

Persistent state survives both. The live room does not — it is unloaded and
rebuilt, which is exactly why `set_state` exists.
