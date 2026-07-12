# 01 — Hello, room

**Shows:** the minimum point-and-click game. One room, a walkable floor, an
avatar that walks where you click, hotspots that answer when you look at them.

```bash
./build/examples/01_hello_room/pac_example_01_hello_room
```

## Read, in this order

| File | What it teaches |
|---|---|
| `data/game.yaml` | The manifest: resolution, resource root, and one `RoomScene`. `entry` drops you straight into the room — no title screen yet. |
| `data/rooms/study.yaml` | **What exists**: the background layer, the `walkable` polygon, named `points`, and four `hotspots` with their polygons, approach points and verbs. Not one line of behaviour. |
| `data/rooms/study.lua` | **What happens**: a verb handler per hotspot. Return a string and the player says it; return nothing and it falls through to the game-wide fallback, then to the engine's default caption. |
| `data/scripts/game.lua` | The game-wide fallbacks. |

## The two ideas worth taking away

**Static data in YAML, behaviour in Lua.** A coordinate never appears in a `.lua`
file; a caption never appears in a `.yaml` file. That split is what lets the room
editor rewrite `study.yaml` without ever touching your code.

**Persistent state goes through the engine.** `study.lua`'s window handler calls
`set_room_state("window.open", true)` rather than setting a Lua local, because
only engine state survives leaving the room and lands in the save file.

Press **F1** in-game to see the walkable polygon, **F2** for the hotspots.
