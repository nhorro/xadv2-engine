-- exterior — cfg 1: EMPTY. Nobody outside, no delivery.
local M = {}

function M.configure(c)
    despawn_npc("delivery_guy")
    hide_object("truck")
    hide_object("box")
end

-- Nothing special on first entry or re-entry while empty.
-- function M.on_first_enter() end
-- function M.on_reenter() end

return M
