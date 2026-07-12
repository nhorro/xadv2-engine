# 02 — SCUMM verbs and inventory

**Shows:** the verb grid building a sentence, picking things up, using one thing
on another, and walking from one room into the next.

```bash
./build/examples/02_scumm_inventory/pac_example_02_scumm_inventory
```

## Play it

Pick up the key from the bench. Try `Open` → `the door`: locked. Then
`Use` → `a rusty key` → `the door`, and walk through. In the yard, `Open` the
crate for a note, and walk into the right-hand edge to come back.

## Read, in this order

| File | What it teaches |
|---|---|
| `data/inventory.yaml` | The items. `combinable: true` is what allows an item to be the **first** operand of "Use X with Y". |
| `data/inventory.lua` | Item behaviour. **Inventory-first dispatch**: "Use key with door" reaches `inventory.key.use("door")` before the room sees it. |
| `data/rooms/workshop.lua` | `add_item`, `disable_hotspot`, and a locked door gated on `get_room_state`. |
| `data/rooms/yard.yaml` | A `zones` polygon — walk into it and `on_zone_enter` fires. The hands-free room change: no verb, no click. |

## The idea worth taking away

The command is a **model, not a UI**. The panel builds `use(key, door)` and hands
it to the dispatcher; a test, a debug tool or a script can build the same command
and take the same path. Nothing about the puzzle depends on the verb grid.
