-- Logic for ScriptScene {{scene_id}}.
-- Callbacks are synchronous; use spawn(function() ... end) for a yielding task.

local scene = {}

function scene.on_enter(ctx)
end

function scene.on_input(ctx, event)
    if event.type == "key_down" and event.key == "escape" then
        ctx:pop_scene()
    end
end

function scene.update(ctx, dt)
end

function scene.on_leave(ctx)
end

return scene
