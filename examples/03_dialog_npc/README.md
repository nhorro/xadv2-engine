# 03 — NPCs and dialog trees

**Shows:** an NPC standing in the room, a hotspot that follows it, and a
conversation with conditional options.

```bash
./build/examples/03_dialog_npc/pac_example_03_dialog_npc
```

## Play it

`Talk to` the curator. Ask what the painting is — then notice a **new option
appears**, because asking unlocked it. Offer her the ticket you are carrying and
the option disappears for good.

## Read, in this order

| File | What it teaches |
|---|---|
| `data/rooms/gallery.lua` | `spawn_npc("curator", "curator_spot", "left")` — NPC presence is **scripted**, not baked into the room YAML. `despawn_npc` takes it away again. |
| `data/rooms/gallery.yaml` | The `curator` hotspot has **no `area`**: `bind: npc:curator` glues it to the NPC, so it still works when the NPC walks. |
| `data/dialogs/curator.lua` | The tree. `when` shows an option conditionally, `once` retires it after use, `run` fires a side effect, `to` jumps — `END` closes. |

## The idea worth taking away

The **"greet once, then hub"** shape. `intro` speaks and falls into `hub`, a
pure-options node that every branch returns to. Give the hub its own `npc` line
and the NPC re-greets you on every lap — correct, and unbearable.

Conditional options read game state (`get_state`, `has_item`), so a conversation
is a view of the world, not a separate machine.
