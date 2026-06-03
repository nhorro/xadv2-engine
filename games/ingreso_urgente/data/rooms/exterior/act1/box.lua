-- exterior — cfg 3: BOX only. The institution accepted it, so the truck left.
local M = {}

function M.configure(c)
    despawn_npc("delivery_guy")
    hide_object("truck")
    show_object("box")
    -- The truck left; only the box still blocks the floor.
    enable_obstacle("box_block")
    disable_obstacle("truck_block")
end

return M
