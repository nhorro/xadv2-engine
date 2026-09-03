# Scriptable scenes

Use a `ScriptScene` when you need a custom 2D interaction model but do not need a
new C++ scene type. It keeps initial visuals in YAML and behavior in Lua while
reusing the engine's resource cache, animated sprites, virtual coordinates,
fixed timestep, state, scene stack, and cooperative tasks.

The runnable reference is
[`examples/07_script_scene`](https://github.com/nhorro/xadv2-engine/tree/develop/examples/07_script_scene).
Move its actor with WASD/arrows and click to place a marker.

## Create one

From the engine checkout, point the recipe at any directory inside your game:

```bash
xadv2-scaffold add script-scene arcade --project ../games/my_game
```

This creates `data/scenes/arcade/scene.{yaml,lua}` and adds the following
manifest entry without reformatting the rest of `game.yaml`:

```yaml
  - id: arcade
    type: ScriptScene
    parameters:
      data: scenes/arcade/scene.yaml
      logic: scenes/arcade/scene.lua
```

Use `--dry-run` to inspect the affected paths first. The recipe refuses existing
scene files and duplicate manifest ids.

## Define entities in YAML

```yaml
version: 1
id: arcade
background:
  color: { r: 24, g: 28, b: 40 }

entities:
  player:
    animation:
      source: /characters/player/player.anim.yml
      sequence: idle
    transform:
      position: { x: 640, y: 360 }
      scale: { x: 0.5, y: 0.5 }
    z: 10

  marker:
    sprite:
      image: marker.png
      origin: { x: 12, y: 12 }
    visible: false
    z: 20
```

Every entity has a transform, `z`, and visibility, plus exactly one `sprite` or
`animation` visual. Relative assets resolve from the YAML file; `/...` resolves
from the game's resource root. See the exhaustive
[entity format](#define-entities-in-yaml).

## Script behavior in Lua

```lua
local scene = {}
local player

function scene.on_enter(ctx)
  player = ctx:entity("player")
end

function scene.on_input(ctx, event)
  if event.type == "pointer_down" and event.button == "left" then
    player:set_position(event.x, event.y)
  elseif event.type == "key_down" and event.key == "escape" then
    ctx:pop_scene()
  end
end

function scene.update(ctx, dt)
  if ctx:key_down("right") then
    player:translate(240 * dt, 0)
  end
end

return scene
```

The optional callbacks are `on_enter(ctx)`, `on_input(ctx,event)`,
`update(ctx,dt)`, and `on_leave(ctx)`. Input coordinates use the manifest's
virtual resolution on every platform. Use entity handles to change transforms,
visibility, z-order, and animation. The complete method and event tables are in
[the scene API below](#script-behavior-in-lua).

Callbacks themselves cannot yield. Start a yielding sequence with `spawn`; it is
scoped to the scene and cancelled automatically when the scene leaves:

```lua
function scene.on_enter(ctx)
  spawn(function()
    wait(0.5)
    ctx:entity("marker"):show()
  end)
end
```

Lua locals, entity transforms, and running tasks are transient. Store anything
that must survive a scene change or save/load with `set_state` and `get_state`.

## Current boundary

The first version instances entities only from YAML. It intentionally has no
runtime spawn/destroy, physics, hierarchy, or ECS query API yet. Entity handles
resolve by id rather than retaining pointers, and the YAML is component-shaped,
so those capabilities can be added gradually without replacing the authoring
model.
