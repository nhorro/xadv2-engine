-- lab — cfg 3: ALONE. Schneider has left; Julia alone again.
local M = {}

function M.configure(c)
    despawn_npc("schneider")
    -- schneider_present / puzzle_enabled were cleared by schneider_leaves().
end

return M
