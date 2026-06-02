-- hall — cfg 3: MUMMY. The mummy — it was inside the box.
local M = {}

function M.configure(c)
    hide_object("box") -- or swap to an "open box" object / region state
    -- spawn_npc("mummy", "at_mummy", "down")
end

return M
