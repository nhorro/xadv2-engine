-- exterior — cfg 3: BOX only. The institution accepted it, so the truck left.
local M = {}

function M.configure(c)
    despawn_npc("delivery_guy")
    hide_object("truck")
    show_object("box")
end

return M
