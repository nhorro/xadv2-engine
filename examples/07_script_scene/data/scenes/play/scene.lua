local scene = {}
local hero
local target
local facing = "down"
local moving = false

function scene.on_enter(ctx)
    hero = ctx:entity("hero")
    target = ctx:entity("target")
end

function scene.on_input(ctx, event)
    if event.type == "key_down" and event.key == "escape" then
        ctx:quit()
    elseif event.type == "pointer_down" and event.button == "left" then
        target:set_position(event.x, event.y):show()
    end
end

function scene.update(ctx, dt)
    local x, y = 0, 0
    if ctx:key_down("left") or ctx:key_down("a") then x = x - 1 end
    if ctx:key_down("right") or ctx:key_down("d") then x = x + 1 end
    if ctx:key_down("up") or ctx:key_down("w") then y = y - 1 end
    if ctx:key_down("down") or ctx:key_down("s") then y = y + 1 end

    local is_moving = x ~= 0 or y ~= 0
    if is_moving then
        local length = math.sqrt(x * x + y * y)
        hero:translate(x / length * 240 * dt, y / length * 240 * dt)
        if math.abs(x) > math.abs(y) then
            facing = x < 0 and "left" or "right"
        else
            facing = y < 0 and "up" or "down"
        end
    end

    if is_moving ~= moving then
        moving = is_moving
        hero:play((moving and "walk_" or "stand_") .. facing, false)
    elseif moving then
        hero:play("walk_" .. facing, false)
    end
end

return scene
